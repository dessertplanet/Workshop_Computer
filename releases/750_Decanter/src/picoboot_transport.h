// Target transport over USB: the Computer as USB host, the target RP2040 in
// BOOTSEL, talking the bootrom's PICOBOOT protocol as picotool does. Driven
// through TinyUSB's endpoint API (no class driver). Proven on hardware by
// tests/usb_host_check.
//
// Every call blocks, running tuh_task() while it waits, so it must be called
// from the main loop and never from a TinyUSB callback.
#pragma once

#include <stdint.h>

#include "flasher.h"

class PicobootTransport : public TargetTransport
{
public:
	enum class Error : uint8_t
	{
		None,
		NotBootsel,     // not 2E8A:0003
		NoConfigDesc,
		NoInterface,    // no vendor interface with two bulk endpoints
		EndpointOpen,
		ResetRequest,   // PICOBOOT_IF_RESET
		CommandOut,     // the 32-byte command
		DataPhase,
		Ack,            // the status packet after the command
	};

	// Check the device, claim its PICOBOOT interface, take exclusive access
	// and leave XIP mode, ready for flash commands
	bool Open(uint8_t daddr);

	bool EraseSector(uint32_t offset) override;
	bool ProgramSector(uint32_t offset, const uint8_t *data) override;
	bool Read(uint32_t offset, uint8_t *buf, uint32_t len) override;

	// Reboot the target into its normal boot path, after a short delay
	bool Reboot();

	// The device's serial number string (the bootrom reports the flash
	// unique ID), as UTF-16 code units; zero-length if unavailable
	const uint16_t *Serial() const { return serial; }
	uint8_t SerialLength() const { return serialLen; }

	Error LastError() const { return error; }
	uint8_t LastCommand() const { return lastCmd; }
	uint32_t LastStatus() const { return lastStatus; } // PICOBOOT dStatusCode after a failure

private:
	bool Command(uint8_t cmdId, const void *args, uint8_t argsLen, uint32_t transferLen, uint8_t *data);
	bool Fail(Error e);
	void FetchStatus();

	uint8_t daddr = 0;
	uint8_t itf = 0, epOut = 0, epIn = 0;
	uint32_t token = 1;
	Error error = Error::None;
	uint8_t lastCmd = 0;
	uint32_t lastStatus = 0;
	uint16_t serial[33];
	uint8_t serialLen = 0;
};
