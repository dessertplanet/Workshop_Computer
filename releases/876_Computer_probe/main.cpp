/*
Computer-Probe (card 876)

Stores a firmware image (UF2) on the card and flashes it onto a target RP2040,
such as a Radio Music Mk2 in BOOTSEL.

The USB role comes from the Computer's USB-C chip, which settles it at
power-up and keeps it until the next power cycle (re-plugging the cable, or
rebooting the RP2040, doesn't change it). So the mode is set by what the
Computer is connected to when it powers up:
  - Device (plugged into a computer): a drive appears. Copy an RP2040 .uf2
    onto it to store it as the payload. INFO.TXT on the drive describes the
    stored payload.
  - Host (plugged into a target): whenever an RP2040 in BOOTSEL appears, the
    payload is flashed onto it, verified, and the target rebooted.

LEDs (0 top left ... 5 bottom right):
  0     payload stored and valid
  1     device mode: on when a computer has mounted the drive
        host mode: slow blink while waiting, on while a target is connected
  2-5   progress bar (upload, or flashing a target)
        all four on: target flashed. All four blinking fast: failed
  all   blinking together: the firmware overlaps the payload area (build error)

Core 1 runs ComputerCard (LEDs); core 0 runs USB and the payload store.
The binary runs from RAM (copy_to_ram), so core 1 keeps running while core 0
erases and programs flash.
*/

#include "ComputerCard.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "tusb.h"

#include <stdio.h>
#include <string.h>

#include "flasher.h"
#include "ghostfat.h"
#include "payload.h"
#include "picoboot_transport.h"

#define PROBE_VERSION "0.1"

////////////////////////////////////////////////////////////////////////////////
// The probe card's flash

class Rp2040Flash : public FlashBackend
{
public:
	void Erase(uint32_t offset, uint32_t len) override
	{
		uint32_t irq = save_and_disable_interrupts();
		flash_range_erase(offset, len);
		restore_interrupts(irq);
	}

	void Program(uint32_t offset, const uint8_t *data, uint32_t len) override
	{
		uint32_t irq = save_and_disable_interrupts();
		flash_range_program(offset, data, len);
		restore_interrupts(irq);
	}

	// flash_range_erase/program flush the XIP cache, so this is never stale
	const uint8_t *Read(uint32_t offset) override { return (const uint8_t *)(XIP_BASE + offset); }

	uint32_t Size() override { return PICO_FLASH_SIZE_BYTES; }
};

extern char __flash_binary_end;

static bool FirmwareFits()
{
	return (uintptr_t)&__flash_binary_end <= XIP_BASE + payload::kHeaderOffset;
}

////////////////////////////////////////////////////////////////////////////////
// State shared with core 1, and readable over SWD (GDB: p probeStatus)

enum class Mode : uint8_t
{
	Device,
	Host,
	Fatal,
};

enum class HostState : uint8_t
{
	Waiting,     // for a target in BOOTSEL
	Flashing,
	Done,        // flashed, verified and rebooted
	Failed,      // see flashResult / picobootError; the target is left in BOOTSEL
	NoPayload,   // a target appeared, but there is nothing valid to flash
	DidNotBoot,  // the target we just flashed came straight back in BOOTSEL
};

struct ProbeStatus
{
	volatile Mode mode;
	volatile bool payloadValid;
	volatile bool driveMounted;
	volatile payload::UploadState upload;
	volatile payload::UploadError uploadError;
	volatile uint32_t blocksReceived;
	volatile uint32_t blocksExpected;

	volatile HostState host;
	volatile bool targetConnected;
	volatile uint32_t sectorsDone;
	volatile uint32_t sectorsTotal;
	volatile FlashResult flashResult;
	volatile PicobootTransport::Error picobootError;
	volatile uint8_t picobootCmd;      // last PICOBOOT command sent
	volatile uint32_t picobootStatus;  // bootrom status code after a failure
	volatile uint32_t targetsFlashed;
};

ProbeStatus probeStatus;

////////////////////////////////////////////////////////////////////////////////
// Core 1: LEDs

class ProbeCard : public ComputerCard
{
	uint32_t ticks = 0;

public:
	bool HostRole() { return USBPowerState() == DFP; }
	bool CanDetectRole() { return USBPowerState() != Unsupported; }

	// Four-LED bar on LEDs 2-5, the leading LED partly lit
	void ProgressBar(uint32_t done, uint32_t total)
	{
		uint32_t level = total ? (done * 4 * 4095) / total : 0;
		for (int i = 0; i < 4; i++)
		{
			int32_t b = int32_t(level) - i * 4095;
			LedBrightness(2 + i, uint16_t(b < 0 ? 0 : (b > 4095 ? 4095 : b)));
		}
	}

	virtual void ProcessSample()
	{
		ticks++;
		bool fast = ticks & (1 << 12);  // ~6 Hz at 48 kHz
		bool slow = ticks & (1 << 15);  // ~0.7 Hz

		if (probeStatus.mode == Mode::Fatal)
		{
			for (int i = 0; i < 6; i++) LedOn(i, fast);
			return;
		}

		LedOn(0, probeStatus.payloadValid);

		if (probeStatus.mode == Mode::Host)
		{
			LedOn(1, probeStatus.targetConnected || slow);
			switch (probeStatus.host)
			{
			case HostState::Flashing:
				ProgressBar(probeStatus.sectorsDone, probeStatus.sectorsTotal);
				break;
			case HostState::Done:
				for (int i = 2; i < 6; i++) LedOn(i);
				break;
			case HostState::Failed:
			case HostState::NoPayload:
			case HostState::DidNotBoot:
				for (int i = 2; i < 6; i++) LedOn(i, fast);
				break;
			default:
				for (int i = 2; i < 6; i++) LedOff(i);
				break;
			}
			return;
		}

		LedOn(1, probeStatus.driveMounted);
		switch (probeStatus.upload)
		{
		case payload::UploadState::Receiving:
			ProgressBar(probeStatus.blocksReceived, probeStatus.blocksExpected);
			break;
		case payload::UploadState::Failed:
			for (int i = 2; i < 6; i++) LedOn(i, fast);
			break;
		default:
			for (int i = 2; i < 6; i++) LedOff(i);
			break;
		}
	}
};

static ProbeCard *card = nullptr;

static void Core1Entry()
{
	card->Run();
}

////////////////////////////////////////////////////////////////////////////////
// Core 0: payload store and the USB drive

static Rp2040Flash probeFlash;
static payload::Store payloadStore(probeFlash); // ~4kB of tables: not on the stack
static payload::Store *const store = &payloadStore;

static char infoText[ghostfat::kMaxInfoLen];
static uint32_t infoLen = 0;

static const char *UploadErrorText(payload::UploadError e)
{
	switch (e)
	{
	case payload::UploadError::None: return "none";
	case payload::UploadError::BadBlock: return "malformed UF2 block";
	case payload::UploadError::OutOfRange: return "the file writes outside target flash, or at/above 0x1FF000";
	case payload::UploadError::TooLarge: return "the file is too large for the probe card";
	}
	return "?";
}

static void UpdateInfoText()
{
	int n = 0;
	auto add = [&](const char *fmt, auto... args) {
		if (n < (int)sizeof(infoText))
			n += snprintf(infoText + n, sizeof(infoText) - n, fmt, args...);
	};

	add("Computer-Probe (card 876), firmware " PROBE_VERSION "\r\n\r\n");

	const payload::Header *h = store->GetHeader();
	if (h)
	{
		uint32_t lo = 0xFFFFFFFF, hi = 0;
		for (uint32_t i = 0; i < h->numSectors; i++)
		{
			uint32_t o = uint32_t(h->targetSector[i]) * payload::kSectorSize;
			if (o < lo) lo = o;
			if (o + payload::kSectorSize > hi) hi = o + payload::kSectorSize;
		}
		add("Payload: RP2040 firmware, %lu UF2 blocks in %lu flash sectors (%lu kB)\r\n",
		    (unsigned long)h->numBlocks, (unsigned long)h->numSectors, (unsigned long)(h->numSectors * 4));
		add("  Target flash 0x%06lX - 0x%06lX, CRC-32 0x%08lX\r\n",
		    (unsigned long)lo, (unsigned long)hi, (unsigned long)h->dataCrc);
	}
	else
	{
		add("Payload: none stored\r\n");
	}

	switch (store->State())
	{
	case payload::UploadState::Idle:
		add("Last upload: none since power-up\r\n");
		break;
	case payload::UploadState::Receiving:
		add("Last upload: in progress, %lu of %lu blocks\r\n",
		    (unsigned long)store->BlocksReceived(), (unsigned long)store->BlocksExpected());
		break;
	case payload::UploadState::Complete:
		add("Last upload: complete\r\n");
		break;
	case payload::UploadState::Failed:
		add("Last upload: FAILED, %s. No payload is stored.\r\n", UploadErrorText(store->Error()));
		break;
	}
	if (store->IgnoredBlocks())
		add("  (%lu blocks for other chips ignored)\r\n", (unsigned long)store->IgnoredBlocks());

	add("Probe capacity: %lu sectors (%lu kB)\r\n\r\n",
	    (unsigned long)store->Capacity(), (unsigned long)(store->Capacity() * 4));
	add("To store a payload, copy an RP2040 .uf2 file onto this drive. It replaces\r\n"
	    "the stored payload. Target flash at or above 0x1FF000 is never written (the\r\n"
	    "Radio Music Mk2 keeps its calibration there), and files that reach it are\r\n"
	    "refused.\r\n\r\n"
	    "To flash a target, power down, connect the Computer to the target with\r\n"
	    "a USB-C cable, and power up with the target in BOOTSEL (Radio Music Mk2:\r\n"
	    "hold Reset while powering up, then push both knobs). The payload is\r\n"
	    "flashed automatically. The USB role is only chosen at power-up.\r\n");

	infoLen = uint32_t(n < (int)sizeof(infoText) ? n : (int)sizeof(infoText) - 1);
}

static void PublishStatus()
{
	probeStatus.payloadValid = store->Valid();
	probeStatus.upload = store->State();
	probeStatus.uploadError = store->Error();
	probeStatus.blocksReceived = store->BlocksReceived();
	probeStatus.blocksExpected = store->BlocksExpected();
}

static volatile bool ejected = false;
static volatile bool uploadEnded = false; // an upload just completed or failed

extern "C" uint32_t tud_msc_inquiry2_cb(uint8_t lun, scsi_inquiry_resp_t *resp, uint32_t bufsize)
{
	(void)lun;
	(void)bufsize;
	memcpy(resp->vendor_id, "MTM     ", 8);
	memcpy(resp->product_id, "Computer-Probe  ", 16);
	memcpy(resp->product_rev, PROBE_VERSION "  ", 4);
	return sizeof(scsi_inquiry_resp_t);
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
	if (ejected) return tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
	return true;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
	(void)lun;
	*block_count = ghostfat::kSectorCount;
	*block_size = ghostfat::kSectorSize;
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
	(void)lun;
	(void)power_condition;
	if (load_eject) ejected = !start;
	return true;
}

extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
	(void)lun;
	if (lba >= ghostfat::kSectorCount) return -1;

	UpdateInfoText();
	static uint8_t sector[ghostfat::kSectorSize] __attribute__((aligned(4)));
	uint8_t *out = (uint8_t *)buffer;
	uint32_t done = 0;
	while (done < bufsize)
	{
		uint32_t pos = offset + done;
		ghostfat::ReadSector(lba + pos / ghostfat::kSectorSize, sector, infoText, infoLen);
		uint32_t within = pos % ghostfat::kSectorSize;
		uint32_t n = ghostfat::kSectorSize - within;
		if (n > bufsize - done) n = bufsize - done;
		memcpy(out + done, sector + within, n);
		done += n;
	}
	return (int32_t)bufsize;
}

extern "C" bool tud_msc_is_writable_cb(uint8_t lun)
{
	(void)lun;
	return true;
}

extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
	(void)lun;
	(void)lba;
	// With CFG_TUD_MSC_EP_BUFSIZE at 512, each call is one whole sector. Only
	// whole sectors can be UF2 blocks; anything else (FAT and directory
	// updates included) is accepted and dropped.
	if (offset == 0)
	{
		payload::UploadState before = store->State();
		for (uint32_t i = 0; i + ghostfat::kSectorSize <= bufsize; i += ghostfat::kSectorSize)
			store->HandleBlock(buffer + i);
		payload::UploadState after = store->State();
		if (after != before && (after == payload::UploadState::Complete || after == payload::UploadState::Failed))
			uploadEnded = true;
		PublishStatus();
	}
	return (int32_t)bufsize;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
	(void)scsi_cmd;
	(void)buffer;
	(void)bufsize;
	tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
	return -1;
}

// After an upload ends, drop off the bus and come back, so the computer
// re-reads the volume and shows the new INFO.TXT (UF2 bootloaders reboot
// instead). The computer may warn that the drive wasn't ejected.
static void Reenumerate()
{
	tud_disconnect();
	sleep_ms(300);
	ejected = false;
	tud_connect();
}

////////////////////////////////////////////////////////////////////////////////
// Core 0: host mode, flashing targets

static volatile uint8_t mountedAddr = 0;   // set by tuh_mount_cb
static volatile bool mountPending = false;

extern "C" void tuh_mount_cb(uint8_t daddr)
{
	mountedAddr = daddr;
	mountPending = true;
	probeStatus.targetConnected = true;
}

extern "C" void tuh_umount_cb(uint8_t daddr)
{
	if (daddr == mountedAddr)
	{
		mountedAddr = 0;
		mountPending = false;
	}
	probeStatus.targetConnected = false;
}

static PicobootTransport picoboot;

// The last target flashed, to catch a payload that doesn't boot: the RP2040
// bootrom falls back to BOOTSEL, and without this the card would flash it
// again, forever
static uint16_t lastSerial[33];
static uint8_t lastSerialLen = 0;
static absolute_time_t lastFlashedAt = nil_time;
constexpr uint32_t kDidNotBootWindowMs = 10000;

static void FlashProgress(uint32_t done, uint32_t total, void *ctx)
{
	(void)ctx;
	probeStatus.sectorsDone = done;
	probeStatus.sectorsTotal = total;
}

static void RecordPicoboot()
{
	probeStatus.picobootError = picoboot.LastError();
	probeStatus.picobootCmd = picoboot.LastCommand();
	probeStatus.picobootStatus = picoboot.LastStatus();
}

static void FlashTarget(uint8_t daddr)
{
	probeStatus.flashResult = FlashResult::Ok;
	probeStatus.picobootError = PicobootTransport::Error::None;
	probeStatus.sectorsDone = 0;
	probeStatus.sectorsTotal = store->SectorCount();

	if (!store->Valid())
	{
		probeStatus.host = HostState::NoPayload;
		return;
	}

	probeStatus.host = HostState::Flashing;
	if (!picoboot.Open(daddr))
	{
		RecordPicoboot();
		probeStatus.host = HostState::Failed;
		return;
	}

	bool sameTarget = lastSerialLen && picoboot.SerialLength() == lastSerialLen &&
	                  memcmp(picoboot.Serial(), lastSerial, lastSerialLen * 2) == 0;
	if (sameTarget && !is_nil_time(lastFlashedAt) &&
	    absolute_time_diff_us(lastFlashedAt, get_absolute_time()) < int64_t(kDidNotBootWindowMs) * 1000)
	{
		probeStatus.host = HostState::DidNotBoot;
		return;
	}

	FlashResult r = FlashPayload(*store, picoboot, FlashProgress, nullptr);
	probeStatus.flashResult = r;
	RecordPicoboot();
	if (r != FlashResult::Ok)
	{
		probeStatus.host = HostState::Failed;
		return;
	}

	// The target unmounts as it reboots; a failed reboot command still leaves
	// a correctly flashed target, which boots on its next power-up
	picoboot.Reboot();
	lastSerialLen = picoboot.SerialLength();
	memcpy(lastSerial, picoboot.Serial(), lastSerialLen * 2);
	lastFlashedAt = get_absolute_time();
	probeStatus.targetsFlashed = probeStatus.targetsFlashed + 1;
	probeStatus.host = HostState::Done;
}

////////////////////////////////////////////////////////////////////////////////
// Core 0: main loop

int main()
{
	// Constructed on core 0 so it exists before core 1 runs it (a plain local,
	// as in the usb_msc_host example, to avoid static-init guard code)
	ProbeCard probe;
	card = &probe;

	if (!FirmwareFits())
	{
		probeStatus.mode = Mode::Fatal;
		multicore_launch_core1(Core1Entry);
		while (true) tight_loop_contents();
	}

	bool host = card->CanDetectRole() && card->HostRole();
	probeStatus.mode = host ? Mode::Host : Mode::Device;
	PublishStatus();
	multicore_launch_core1(Core1Entry);

	tusb_rhport_init_t usbInit = {
		.role = host ? TUSB_ROLE_HOST : TUSB_ROLE_DEVICE,
		.speed = TUSB_SPEED_AUTO
	};
	tusb_init(BOARD_TUD_RHPORT, &usbInit);

	absolute_time_t endedAt = nil_time;
	while (true)
	{
		if (host)
		{
			tuh_task();
			if (mountPending)
			{
				mountPending = false;
				FlashTarget(mountedAddr);
			}
			continue;
		}

		tud_task();
		probeStatus.driveMounted = tud_mounted() && !ejected;

		// Re-enumerate once the host has had a moment to finish writing
		if (uploadEnded)
		{
			uploadEnded = false;
			endedAt = make_timeout_time_ms(1000);
		}
		if (!is_nil_time(endedAt) && time_reached(endedAt))
		{
			endedAt = nil_time;
			Reenumerate();
		}
	}
}
