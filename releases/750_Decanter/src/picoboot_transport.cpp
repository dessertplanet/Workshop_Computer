#include "picoboot_transport.h"

#include <string.h>

#include "pico.h"
#include "boot/picoboot.h"
#include "pico/time.h"
#include "tusb.h"

constexpr uint32_t kTargetXipBase = 0x10000000;
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kTimeoutMs = 3000;

////////////////////////////////////////////////////////////////////////////////
// Blocking wrappers around TinyUSB's async transfers

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

static bool BulkXfer(uint8_t daddr, uint8_t ep, uint8_t *buf, uint32_t len, uint32_t *actual)
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
	bool ok = WaitXfer(w, daddr, ep, kTimeoutMs);
	if (actual) *actual = w.len;
	return ok;
}

static bool ControlXfer(uint8_t daddr, uint8_t bmRequestType, uint8_t bRequest, uint16_t wIndex, uint8_t *buf, uint16_t len)
{
	tusb_control_request_t req = {};
	req.bmRequestType = bmRequestType;
	req.bRequest = bRequest;
	req.wValue = 0;
	req.wIndex = wIndex;
	req.wLength = len;

	XferWait w = {false, XFER_RESULT_INVALID, 0};
	tuh_xfer_t xfer = {};
	xfer.daddr = daddr;
	xfer.ep_addr = 0;
	xfer.setup = &req;
	xfer.buffer = buf;
	xfer.complete_cb = XferDone;
	xfer.user_data = (uintptr_t)&w;
	if (!tuh_control_xfer(&xfer)) return false;
	return WaitXfer(w, daddr, 0, 1000);
}

////////////////////////////////////////////////////////////////////////////////

static uint8_t descBuf[256] __attribute__((aligned(4)));
static picoboot_cmd cmdBuf;
// The USB controller is fed by CPU copies, but keep transfer buffers in RAM
// and aligned regardless; payload data is copied here before sending
static uint8_t sectorBuf[kSectorSize] __attribute__((aligned(4)));

bool PicobootTransport::Fail(Error e)
{
	error = e;
	return false;
}

void PicobootTransport::FetchStatus()
{
	static picoboot_cmd_status status __attribute__((aligned(4)));
	memset(&status, 0, sizeof(status));
	// vendor, interface, IN
	if (ControlXfer(daddr, 0xC1, PICOBOOT_IF_CMD_STATUS, itf, (uint8_t *)&status, sizeof(status)))
		lastStatus = status.dStatusCode;
}

bool PicobootTransport::Open(uint8_t addr)
{
	daddr = addr;
	error = Error::None;
	lastStatus = 0;
	serialLen = 0;

	uint16_t vid = 0, pid = 0;
	tuh_vid_pid_get(daddr, &vid, &pid);
	if (vid != 0x2E8A || pid != 0x0003) return Fail(Error::NotBootsel);

	// Serial number, to recognise the same target coming back
	tusb_desc_device_t dev;
	if (tuh_descriptor_get_device_sync(daddr, &dev, sizeof(dev)) == XFER_RESULT_SUCCESS && dev.iSerialNumber &&
	    tuh_descriptor_get_serial_string_sync(daddr, 0x0409, descBuf, sizeof(descBuf)) == XFER_RESULT_SUCCESS)
	{
		uint8_t n = (descBuf[0] > 2 ? descBuf[0] - 2 : 0) / 2;
		if (n > 32) n = 32;
		memcpy(serial, descBuf + 2, n * 2);
		serialLen = n;
	}

	// Find the vendor interface with one bulk OUT and one bulk IN endpoint.
	// (The RP2040 bootrom has MSC on interface 0, PICOBOOT on 1.)
	if (tuh_descriptor_get_configuration_sync(daddr, 0, descBuf, sizeof(descBuf)) != XFER_RESULT_SUCCESS)
		return Fail(Error::NoConfigDesc);

	tusb_desc_configuration_t const *cfg = (tusb_desc_configuration_t const *)descBuf;
	uint8_t const *p = descBuf;
	uint8_t const *end = descBuf + tu_min16(cfg->wTotalLength, sizeof(descBuf));
	tusb_desc_interface_t const *found = nullptr;
	tusb_desc_endpoint_t const *out = nullptr, *in = nullptr;

	while (p < end && tu_desc_len(p) > 0)
	{
		if (tu_desc_type(p) == TUSB_DESC_INTERFACE)
		{
			if (found && out && in) break;
			tusb_desc_interface_t const *d = (tusb_desc_interface_t const *)p;
			bool candidate = d->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC && d->bNumEndpoints == 2 && d->bAlternateSetting == 0;
			found = candidate ? d : nullptr;
			out = in = nullptr;
		}
		else if (found && tu_desc_type(p) == TUSB_DESC_ENDPOINT)
		{
			tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
			if (ep->bmAttributes.xfer == TUSB_XFER_BULK)
			{
				if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) in = ep;
				else out = ep;
			}
		}
		p = tu_desc_next(p);
	}
	if (!(found && out && in)) return Fail(Error::NoInterface);

	itf = found->bInterfaceNumber;
	epOut = out->bEndpointAddress;
	epIn = in->bEndpointAddress;
	if (!tuh_edpt_open(daddr, out) || !tuh_edpt_open(daddr, in)) return Fail(Error::EndpointOpen);

	// vendor, interface, OUT
	if (!ControlXfer(daddr, 0x41, PICOBOOT_IF_RESET, itf, nullptr, 0)) return Fail(Error::ResetRequest);

	picoboot_exclusive_cmd excl = {EXCLUSIVE};
	if (!Command(PC_EXCLUSIVE_ACCESS, &excl, sizeof(excl), 0, nullptr)) return false;
	return Command(PC_EXIT_XIP, nullptr, 0, 0, nullptr);
}

// One PICOBOOT command, as picotool's picoboot_cmd(): the 32-byte command on
// bulk OUT, an optional data phase (IN if the command's top bit is set), then
// a status packet in the opposite direction to the data
bool PicobootTransport::Command(uint8_t cmdId, const void *args, uint8_t argsLen, uint32_t transferLen, uint8_t *data)
{
	lastCmd = cmdId;
	memset(&cmdBuf, 0, sizeof(cmdBuf));
	cmdBuf.dMagic = PICOBOOT_MAGIC;
	cmdBuf.dToken = token++;
	cmdBuf.bCmdId = cmdId;
	cmdBuf.bCmdSize = argsLen;
	cmdBuf.dTransferLength = transferLen;
	if (argsLen) memcpy(cmdBuf.args, args, argsLen);

	uint32_t actual = 0;
	if (!BulkXfer(daddr, epOut, (uint8_t *)&cmdBuf, sizeof(cmdBuf), &actual) || actual != sizeof(cmdBuf))
	{
		FetchStatus();
		return Fail(Error::CommandOut);
	}

	bool isIn = cmdId & 0x80;
	if (transferLen)
	{
		if (!BulkXfer(daddr, isIn ? epIn : epOut, data, transferLen, &actual) || actual != transferLen)
		{
			FetchStatus();
			return Fail(Error::DataPhase);
		}
	}

	// picotool sends a 1-byte OUT packet after IN commands, and expects a
	// zero-length IN packet after OUT commands
	static uint8_t spoon[4] __attribute__((aligned(4)));
	if (!BulkXfer(daddr, isIn ? epOut : epIn, spoon, 1, nullptr))
	{
		FetchStatus();
		return Fail(Error::Ack);
	}
	return true;
}

bool PicobootTransport::EraseSector(uint32_t offset)
{
	picoboot_range_cmd range = {kTargetXipBase + offset, kSectorSize};
	return Command(PC_FLASH_ERASE, &range, sizeof(range), 0, nullptr);
}

bool PicobootTransport::ProgramSector(uint32_t offset, const uint8_t *data)
{
	memcpy(sectorBuf, data, kSectorSize);
	picoboot_range_cmd range = {kTargetXipBase + offset, kSectorSize};
	return Command(PC_WRITE, &range, sizeof(range), kSectorSize, sectorBuf);
}

bool PicobootTransport::Read(uint32_t offset, uint8_t *buf, uint32_t len)
{
	picoboot_range_cmd range = {kTargetXipBase + offset, len};
	return Command(PC_READ, &range, sizeof(range), len, buf);
}

bool PicobootTransport::Reboot()
{
	// As picotool: normal boot path, stack at the top of SRAM, after 500 ms
	picoboot_reboot_cmd reboot = {0, 0x20042000, 500};
	return Command(PC_REBOOT, &reboot, sizeof(reboot), 0, nullptr);
}
