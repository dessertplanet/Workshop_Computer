#include "flasher.h"

#include <string.h>

using namespace payload;

const char *FlashResultName(FlashResult r)
{
	switch (r)
	{
	case FlashResult::Ok: return "ok";
	case FlashResult::NoPayload: return "no payload stored";
	case FlashResult::PayloadCorrupt: return "stored payload is corrupt";
	case FlashResult::OutOfRange: return "payload reaches protected target flash";
	case FlashResult::EraseFailed: return "target erase failed";
	case FlashResult::ProgramFailed: return "target write failed";
	case FlashResult::ReadFailed: return "target read-back failed";
	case FlashResult::VerifyFailed: return "target read-back did not match";
	}
	return "?";
}

static uint8_t verifyBuf[kSectorSize] __attribute__((aligned(4)));

static FlashResult WriteSector(TargetTransport &target, uint32_t offset, const uint8_t *data, bool erase)
{
	if (erase && !target.EraseSector(offset)) return FlashResult::EraseFailed;
	if (!target.ProgramSector(offset, data)) return FlashResult::ProgramFailed;
	if (!target.Read(offset, verifyBuf, kSectorSize)) return FlashResult::ReadFailed;
	if (memcmp(verifyBuf, data, kSectorSize) != 0) return FlashResult::VerifyFailed;
	return FlashResult::Ok;
}

FlashResult FlashPayload(Store &store, TargetTransport &target, FlashProgressFn progress, void *ctx)
{
	if (!store.Valid()) return FlashResult::NoPayload;
	if (!store.DataCrcMatches()) return FlashResult::PayloadCorrupt;

	uint32_t n = store.SectorCount();
	int32_t bootSlot = -1;
	for (uint32_t i = 0; i < n; i++)
	{
		// The store refuses these on upload; check again before touching the target
		if (store.TargetOffset(i) >= kTargetFlashLimit) return FlashResult::OutOfRange;
		if (store.TargetOffset(i) == 0) bootSlot = int32_t(i);
	}

	if (bootSlot >= 0 && !target.EraseSector(0)) return FlashResult::EraseFailed;

	uint32_t done = 0;
	for (uint32_t i = 0; i < n; i++)
	{
		if (int32_t(i) == bootSlot) continue;
		FlashResult r = WriteSector(target, store.TargetOffset(i), store.SectorData(i), true);
		if (r != FlashResult::Ok) return r;
		if (progress) progress(++done, n, ctx);
	}

	if (bootSlot >= 0)
	{
		FlashResult r = WriteSector(target, 0, store.SectorData(uint32_t(bootSlot)), false);
		if (r != FlashResult::Ok) return r;
		if (progress) progress(++done, n, ctx);
	}

	return FlashResult::Ok;
}
