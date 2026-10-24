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

	// CRC-32 of a 4kB sector, computed on the target. Optional: a transport
	// without it can't check a target without stopping it.
	virtual bool SectorCrc(uint32_t offset, uint32_t *crc)
	{
		(void)offset;
		(void)crc;
		return false;
	}
};

enum class MatchResult : uint8_t
{
	Matches,   // every payload sector is already on the target
	Differs,
	Unknown,   // no payload, or the target couldn't answer
};

// Does the target already hold the payload? Asks the target for a CRC of each
// payload sector and compares, without changing anything.
MatchResult PayloadMatchesTarget(payload::Store &store, TargetTransport &target);

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
