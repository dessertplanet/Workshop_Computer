// Flashing engine: writes the stored payload to a target RP2040 through a
// transport (USB PICOBOOT now, UART later). Transport-independent, so it is
// exercised by the host tests with a simulated target.
#pragma once

#include <stdint.h>

#include "payload.h"

// What a transport provides. Offsets are from the start of target flash;
// erase and program are whole 4kB sectors.
class TargetTransport
{
public:
	virtual ~TargetTransport() = default;
	virtual bool EraseSector(uint32_t offset) = 0;
	virtual bool ProgramSector(uint32_t offset, const uint8_t *data) = 0;
	virtual bool Read(uint32_t offset, uint8_t *buf, uint32_t len) = 0;
};

enum class FlashResult : uint8_t
{
	Ok,
	NoPayload,
	PayloadCorrupt,  // slots don't match the header CRC
	OutOfRange,      // a payload sector at/above kTargetFlashLimit (never written)
	EraseFailed,
	ProgramFailed,
	ReadFailed,
	VerifyFailed,
};

const char *FlashResultName(FlashResult r);

// Called after each sector is written and verified
typedef void (*FlashProgressFn)(uint32_t done, uint32_t total, void *ctx);

// Sector 0 (boot2 and the vector table) is erased first and written last, so
// a flash that is interrupted leaves the target without a valid boot2: the
// RP2040 bootrom then stays in BOOTSEL, ready for another try, instead of
// running half-written firmware.
FlashResult FlashPayload(payload::Store &store, TargetTransport &target, FlashProgressFn progress = nullptr, void *ctx = nullptr);
