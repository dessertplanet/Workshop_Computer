// Target transport over the UART pads, to a target running a firmware with a
// Decanter UART updater. Protocol: decanter_uart_proto.h.
//
// Every call blocks until its reply arrives (or the retries run out), so it
// must not be called from an interrupt.
#pragma once

#include <stdint.h>

#include "flasher.h"
#include "decanter_uart_proto.h"

class UartTransport : public TargetTransport
{
public:
	enum class Error : uint8_t
	{
		None,
		Timeout,     // no reply after every retry
		Garbled,     // replies kept failing their CRC
		Refused,     // the target answered with a non-OK status (see LastStatus)
		BadReply,    // a reply of the wrong length
	};

	// PIO UART: TX on GPIO 1 (SCL pad), RX on GPIO 0 (SDA pad)
	void Init();

	// One HELLO, without retries: for polling whether a target is there
	bool Hello(ru::HelloInfo &info, uint32_t timeoutMs);

	// Put the target in update mode
	bool Enter(uint32_t totalSectors);

	bool EraseSector(uint32_t offset) override;
	bool ProgramSector(uint32_t offset, const uint8_t *data) override;
	bool Read(uint32_t offset, uint8_t *buf, uint32_t len) override;

	bool Reboot();

	bool SectorCrc(uint32_t offset, uint32_t *crc) override;

	Error LastError() const { return error; }
	uint8_t LastCommand() const { return lastCmd; }
	uint8_t LastStatus() const { return lastStatus; }
	uint32_t Retries() const { return retries; } // since Init, for diagnostics

private:
	bool Transact(uint8_t cmd, uint32_t offset, const uint8_t *payload, uint32_t len,
	              uint8_t *reply, uint32_t replyLen, uint32_t timeoutMs, int tries);
	void Send(const uint8_t *p, uint32_t n);
	bool Fail(Error e);

	ru::Parser parser;
	uint16_t seq = 0;
	Error error = Error::None;
	uint8_t lastCmd = 0;
	uint8_t lastStatus = 0;
	uint32_t retries = 0;
};
