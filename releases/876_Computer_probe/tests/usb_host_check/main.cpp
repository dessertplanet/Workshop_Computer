/*
usb_host_check - Computer-Probe feasibility test, step 1

Can a Workshop Computer, as USB host, reach an RP2040 in BOOTSEL (for example a
Radio Music Mk2 put into BOOTSEL with Reset + both knobs) over a USB-C cable,
and talk PICOBOOT to it?

The test is read-only: it never erases, writes or reboots the target. Once the
target enumerates, it reads 32 bytes of the target's bootrom over PICOBOOT
and checks the RP2040 bootrom magic, which proves the bulk-transfer path the
flasher will use.

LEDs (0 top left ... 5 bottom right), each one a step that passed:
  0  USB role is host (DFP), from the Computer's USB-C chip
  1  A device is attached to the port
  2  It enumerated
  3  It is 2E8A:0003 (RP2040 BOOTSEL) with a PICOBOOT interface
  4  PICOBOOT read of the bootrom returned the RP2040 magic: PASS
  5  Slow blink: waiting / in progress. Fast blink: a step failed
     (the last lit LED shows how far it got). Solid: PASS

The same results are in the global `report`, for reading over SWD (GDB:
`p/x report`).

Core 1 runs ComputerCard (LEDs); core 0 runs the TinyUSB host stack.
*/

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "tusb.h"
#include "host/hcd.h"
#include "boot/picoboot.h"

#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Results, shared with core 1 and readable over SWD

enum class Step : uint8_t
{
	Waiting = 0,  // nothing attached
	Attached,     // port sees a device
	Mounted,      // enumerated
	Picoboot,     // RP2040 BOOTSEL with PICOBOOT interface, endpoints open
	Pass,         // bootrom magic read over PICOBOOT
};

enum class Error : uint8_t
{
	None = 0,
	NotBootsel,       // enumerated, but not 2E8A:0003
	NoConfigDesc,     // couldn't read the configuration descriptor
	NoPicobootItf,    // no vendor interface with two bulk endpoints
	EndpointOpen,     // tuh_edpt_open failed
	ResetRequest,     // PICOBOOT_IF_RESET control request failed
	CommandOut,       // sending the 32-byte command failed
	DataIn,           // reading the data phase failed or came up short
	Ack,              // the zero-length status phase failed
	BadMagic,         // data arrived but isn't the RP2040 bootrom
};

struct Report
{
	volatile bool hostRole;        // USBPowerState() == DFP
	volatile Step step;
	volatile Error error;
	volatile uint16_t vid, pid;
	volatile uint8_t itfNum, epOut, epIn;
	volatile uint8_t romVersion;   // 1 = B0, 2 = B1, 3 = B2
	volatile uint32_t attempts;    // devices mounted since power-up
	volatile uint8_t rom[32];      // bootrom bytes 0x00-0x1f
};

Report report;

////////////////////////////////////////////////////////////////////////////////
// Core 1: LEDs

class UsbHostCheck : public ComputerCard
{
	uint32_t ticks = 0;

public:
	bool HostRole() { return USBPowerState() == DFP; }

	virtual void ProcessSample()
	{
		ticks++;
		Step step = report.step;
		bool failed = report.error != Error::None;

		LedOn(0, report.hostRole);
		LedOn(1, step >= Step::Attached);
		LedOn(2, step >= Step::Mounted);
		LedOn(3, step >= Step::Picoboot);
		LedOn(4, step == Step::Pass);

		bool blink;
		if (step == Step::Pass)
			blink = true;
		else if (failed)
			blink = ticks & (1 << 12);  // ~6 Hz at 48 kHz
		else
			blink = ticks & (1 << 15);  // ~0.7 Hz
		LedOn(5, blink);
	}
};

static UsbHostCheck *card = nullptr;

static void Core1Entry()
{
	card->Run();
}

////////////////////////////////////////////////////////////////////////////////
// Core 0: blocking wrappers around TinyUSB's async transfers. Only called from
// the main loop, never from a TinyUSB callback.

struct XferWait
{
	volatile bool done;
	volatile xfer_result_t result;
	volatile uint32_t len;
};

static void XferDone(tuh_xfer_t *xfer)
{
	XferWait *w = (XferWait *)xfer->user_data;
	w->result = xfer->result;
	w->len = xfer->actual_len;
	w->done = true;
}

static bool WaitXfer(XferWait &w, uint8_t daddr, uint8_t ep, uint32_t timeoutMs)
{
	absolute_time_t deadline = make_timeout_time_ms(timeoutMs);
	while (!w.done)
	{
		tuh_task();
		if (time_reached(deadline))
		{
			tuh_edpt_abort_xfer(daddr, ep);
			return false;
		}
	}
	return w.result == XFER_RESULT_SUCCESS;
}

static bool BulkXfer(uint8_t daddr, uint8_t ep, uint8_t *buf, uint32_t len, uint32_t *actual, uint32_t timeoutMs)
{
	XferWait w = {false, XFER_RESULT_INVALID, 0};
	tuh_xfer_t xfer = {};
	xfer.daddr = daddr;
	xfer.ep_addr = ep;
	xfer.buflen = len;
	xfer.buffer = buf;
	xfer.complete_cb = XferDone;
	xfer.user_data = (uintptr_t)&w;
	if (!tuh_edpt_xfer(&xfer)) return false;
	bool ok = WaitXfer(w, daddr, ep, timeoutMs);
	if (actual) *actual = w.len;
	return ok;
}

static bool PicobootInterfaceReset(uint8_t daddr, uint8_t itf)
{
	tusb_control_request_t req = {};
	req.bmRequestType = 0x41; // vendor, interface, OUT
	req.bRequest = PICOBOOT_IF_RESET;
	req.wValue = 0;
	req.wIndex = itf;
	req.wLength = 0;

	XferWait w = {false, XFER_RESULT_INVALID, 0};
	tuh_xfer_t xfer = {};
	xfer.daddr = daddr;
	xfer.ep_addr = 0;
	xfer.setup = &req;
	xfer.buffer = nullptr;
	xfer.complete_cb = XferDone;
	xfer.user_data = (uintptr_t)&w;
	if (!tuh_control_xfer(&xfer)) return false;
	return WaitXfer(w, daddr, 0, 1000);
}

////////////////////////////////////////////////////////////////////////////////
// Core 0: the check itself

static volatile uint8_t mountedAddr = 0;   // set by tuh_mount_cb
static volatile bool checkPending = false;

static uint8_t descBuf[256] __attribute__((aligned(4)));
static uint8_t dataBuf[64] __attribute__((aligned(4)));
static picoboot_cmd cmd;

static void Fail(Error e)
{
	report.error = e;
}

// Find the vendor interface with one bulk OUT and one bulk IN endpoint, and
// open both. (The RP2040 bootrom has MSC on interface 0, PICOBOOT on 1.)
static bool OpenPicobootInterface(uint8_t daddr)
{
	if (tuh_descriptor_get_configuration_sync(daddr, 0, descBuf, sizeof(descBuf)) != XFER_RESULT_SUCCESS)
	{
		Fail(Error::NoConfigDesc);
		return false;
	}

	tusb_desc_configuration_t const *cfg = (tusb_desc_configuration_t const *)descBuf;
	uint8_t const *p = descBuf;
	uint8_t const *end = descBuf + tu_min16(cfg->wTotalLength, sizeof(descBuf));

	tusb_desc_interface_t const *itf = nullptr;
	tusb_desc_endpoint_t const *epOut = nullptr, *epIn = nullptr;

	while (p < end && tu_desc_len(p) > 0)
	{
		if (tu_desc_type(p) == TUSB_DESC_INTERFACE)
		{
			if (itf && epOut && epIn) break; // already found it
			tusb_desc_interface_t const *d = (tusb_desc_interface_t const *)p;
			bool candidate = d->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC && d->bNumEndpoints == 2 && d->bAlternateSetting == 0;
			itf = candidate ? d : nullptr;
			epOut = epIn = nullptr;
		}
		else if (itf && tu_desc_type(p) == TUSB_DESC_ENDPOINT)
		{
			tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
			if (ep->bmAttributes.xfer == TUSB_XFER_BULK)
			{
				if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) epIn = ep;
				else epOut = ep;
			}
		}
		p = tu_desc_next(p);
	}

	if (!(itf && epOut && epIn))
	{
		Fail(Error::NoPicobootItf);
		return false;
	}

	report.itfNum = itf->bInterfaceNumber;
	report.epOut = epOut->bEndpointAddress;
	report.epIn = epIn->bEndpointAddress;

	if (!tuh_edpt_open(daddr, epOut) || !tuh_edpt_open(daddr, epIn))
	{
		Fail(Error::EndpointOpen);
		return false;
	}
	return true;
}

// One PICOBOOT IN command, as picotool's picoboot_cmd(): 32-byte command on
// bulk OUT, data on bulk IN, then a status packet in the opposite direction.
static bool PicobootRead(uint8_t daddr, uint32_t addr, uint8_t *buf, uint32_t len)
{
	static uint32_t token = 1;
	memset(&cmd, 0, sizeof(cmd));
	cmd.dMagic = PICOBOOT_MAGIC;
	cmd.dToken = token++;
	cmd.bCmdId = PC_READ;
	cmd.bCmdSize = sizeof(cmd.range_cmd);
	cmd.dTransferLength = len;
	cmd.range_cmd.dAddr = addr;
	cmd.range_cmd.dSize = len;

	uint32_t actual = 0;
	if (!BulkXfer(daddr, report.epOut, (uint8_t *)&cmd, sizeof(cmd), &actual, 3000) || actual != sizeof(cmd))
	{
		Fail(Error::CommandOut);
		return false;
	}
	if (!BulkXfer(daddr, report.epIn, buf, len, &actual, 3000) || actual != len)
	{
		Fail(Error::DataIn);
		return false;
	}
	// picotool sends a 1-byte OUT packet as the status phase of IN commands
	static uint8_t spoon[4] __attribute__((aligned(4)));
	if (!BulkXfer(daddr, report.epOut, spoon, 1, nullptr, 3000))
	{
		Fail(Error::Ack);
		return false;
	}
	return true;
}

static void RunCheck(uint8_t daddr)
{
	report.attempts = report.attempts + 1;

	uint16_t vid = 0, pid = 0;
	tuh_vid_pid_get(daddr, &vid, &pid);
	report.vid = vid;
	report.pid = pid;
	if (vid != 0x2E8A || pid != 0x0003)
	{
		Fail(Error::NotBootsel);
		return;
	}

	if (!OpenPicobootInterface(daddr)) return;
	report.step = Step::Picoboot;

	if (!PicobootInterfaceReset(daddr, report.itfNum))
	{
		Fail(Error::ResetRequest);
		return;
	}

	if (!PicobootRead(daddr, 0x00000000, dataBuf, 32)) return;

	for (int i = 0; i < 32; i++) report.rom[i] = dataBuf[i];
	uint32_t magic = dataBuf[0x10] | (dataBuf[0x11] << 8) | (dataBuf[0x12] << 16);
	report.romVersion = dataBuf[0x13];
	if (magic != 0x01754d) // 'M', 'u', 1: RP2040 bootrom
	{
		Fail(Error::BadMagic);
		return;
	}
	report.step = Step::Pass;
}

extern "C" void tuh_mount_cb(uint8_t daddr)
{
	mountedAddr = daddr;
	checkPending = true;
}

extern "C" void tuh_umount_cb(uint8_t daddr)
{
	if (daddr == mountedAddr)
	{
		mountedAddr = 0;
		checkPending = false;
	}
}

int main()
{
	// Constructed on core 0 so it exists before core 1 runs it (a plain local,
	// as in the usb_msc_host example, to avoid static-init guard code).
	UsbHostCheck checker;
	card = &checker;
	multicore_launch_core1(Core1Entry);

	// Start as host whatever the USB-C chip reports, so the LEDs show the role
	// and the attach state independently.
	tusb_rhport_init_t hostInit = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO
	};
	tusb_init(BOARD_TUH_RHPORT, &hostInit);

	while (true)
	{
		tuh_task();

		report.hostRole = card->HostRole();

		bool attached = hcd_port_connect_status(BOARD_TUH_RHPORT);
		if (!attached)
		{
			// Unplugged: clear everything for the next attempt
			report.step = Step::Waiting;
			report.error = Error::None;
		}
		else if (report.step == Step::Waiting)
		{
			report.step = Step::Attached;
		}

		if (checkPending)
		{
			checkPending = false;
			report.step = Step::Mounted;
			report.error = Error::None;
			RunCheck(mountedAddr);
		}
	}
}
