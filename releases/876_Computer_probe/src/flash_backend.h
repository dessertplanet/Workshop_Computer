// The probe card's own flash, as the payload store sees it. On the Computer
// this is the RP2040's QSPI flash (main.cpp); in the host tests it is an array.
#pragma once

#include <stdint.h>

class FlashBackend
{
public:
	static constexpr uint32_t kSectorSize = 4096;
	static constexpr uint32_t kPageSize = 256;

	virtual ~FlashBackend() = default;

	// Offsets are from the start of flash. Erase works on whole sectors,
	// Program on whole pages, as the RP2040 flash functions.
	virtual void Erase(uint32_t offset, uint32_t len) = 0;
	virtual void Program(uint32_t offset, const uint8_t *data, uint32_t len) = 0;

	// Memory-mapped view of flash, valid until the next Erase/Program
	virtual const uint8_t *Read(uint32_t offset) = 0;

	virtual uint32_t Size() = 0;
};
