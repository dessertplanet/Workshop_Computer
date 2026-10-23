#include "payload.h"

#include <stddef.h>
#include <string.h>

#include "crc32.h"

namespace payload
{

Store::Store(FlashBackend &flash) : flash(flash)
{
	memset(slotOf, 0, sizeof(slotOf));
	memset(slotTarget, 0, sizeof(slotTarget));
	memset(pagesWritten, 0, sizeof(pagesWritten));
	memset(blockSeen, 0, sizeof(blockSeen));
}

const Header *Store::GetHeader()
{
	const Header *h = (const Header *)flash.Read(kHeaderOffset);
	if (h->magic != kHeaderMagic || h->version != kHeaderVersion) return nullptr;
	if (h->numSectors == 0 || h->numSectors > Capacity()) return nullptr;
	if (Crc32((const uint8_t *)h, offsetof(Header, headerCrc)) != h->headerCrc) return nullptr;
	for (uint32_t i = 0; i < h->numSectors; i++)
		if (h->targetSector[i] >= kMaxTargetSectors) return nullptr;
	return h;
}

bool Store::Valid()
{
	return GetHeader() != nullptr;
}

uint32_t Store::SectorCount()
{
	const Header *h = GetHeader();
	return h ? h->numSectors : 0;
}

uint32_t Store::TargetOffset(uint32_t slot)
{
	return uint32_t(GetHeader()->targetSector[slot]) * kSectorSize;
}

const uint8_t *Store::SectorData(uint32_t slot)
{
	return flash.Read(kSlotsOffset + slot * kSectorSize);
}

bool Store::DataCrcMatches()
{
	const Header *h = GetHeader();
	if (!h) return false;
	uint32_t crc = 0;
	for (uint32_t i = 0; i < h->numSectors; i++)
		crc = Crc32Update(crc, SectorData(i), kSectorSize);
	return crc == h->dataCrc;
}

uint32_t Store::Capacity()
{
	if (flash.Size() <= kSlotsOffset) return 0;
	uint32_t slots = (flash.Size() - kSlotsOffset) / kSectorSize;
	return slots < kMaxTargetSectors ? slots : kMaxTargetSectors;
}

void Store::StartSession(uint32_t blocks, uint32_t family)
{
	// Invalidate the stored payload before touching any slot
	flash.Erase(kHeaderOffset, kSectorSize);

	state = UploadState::Receiving;
	error = UploadError::None;
	numBlocks = blocks;
	familyId = family;
	received = 0;
	ignored = ignoredBeforeSession;
	ignoredBeforeSession = 0;
	slotsUsed = 0;
	memset(slotOf, 0, sizeof(slotOf));
	memset(pagesWritten, 0, sizeof(pagesWritten));
	memset(blockSeen, 0, sizeof(blockSeen));
}

void Store::Fail(UploadError e)
{
	state = UploadState::Failed;
	error = e;
}

void Store::HandleBlock(const uint8_t *block512)
{
	uf2::Block b;
	memcpy(&b, block512, sizeof(b)); // the caller's buffer may not be aligned
	if (!uf2::IsBlock(b)) return;

	// Blocks for other chips (e.g. the RP2350 half of a universal UF2) and
	// non-flash blocks aren't ours to store
	bool rp2040 = (b.flags & uf2::kFlagFamilyIdPresent) && b.familyId == uf2::kFamilyRP2040;
	if (!rp2040 || (b.flags & uf2::kFlagNotMainFlash))
	{
		if (state == UploadState::Receiving) ignored++;
		else ignoredBeforeSession++;
		return;
	}

	bool seen = b.blockNo < kMaxBlocks && (blockSeen[b.blockNo / 32] & (1u << (b.blockNo % 32)));

	// Decide whether this block starts a new upload. Hosts copy a file from
	// its start, so once an upload has finished (or failed) only block 0
	// begins another one, and stray re-writes of the old file's blocks are
	// ignored. During an upload, a repeated block that matches what is stored
	// is a duplicate; one that differs means a different file of the same size
	// is being copied, so start again rather than mix the two.
	switch (state)
	{
	case UploadState::Idle:
		StartSession(b.numBlocks, b.familyId);
		break;
	case UploadState::Complete:
	case UploadState::Failed:
		if (b.blockNo != 0) return;
		StartSession(b.numBlocks, b.familyId);
		break;
	case UploadState::Receiving:
		if (b.numBlocks != numBlocks || (seen && !SameAsStored(b)))
			StartSession(b.numBlocks, b.familyId);
		else if (seen)
			return;
		break;
	}

	if (b.numBlocks == 0 || b.numBlocks > kMaxBlocks)
	{
		Fail(UploadError::TooLarge);
		return;
	}
	if (b.payloadSize != kPageSize || (b.targetAddr % kPageSize) != 0 || b.blockNo >= b.numBlocks)
	{
		Fail(UploadError::BadBlock);
		return;
	}
	if (b.targetAddr < kTargetFlashBase || b.targetAddr - kTargetFlashBase + kPageSize > kTargetFlashLimit)
	{
		Fail(UploadError::OutOfRange);
		return;
	}

	uint32_t offset = b.targetAddr - kTargetFlashBase;
	uint32_t sector = offset / kSectorSize;
	uint32_t page = (offset % kSectorSize) / kPageSize;

	if (slotOf[sector] == 0)
	{
		if (slotsUsed >= Capacity())
		{
			Fail(UploadError::TooLarge);
			return;
		}
		uint32_t slot = slotsUsed++;
		flash.Erase(kSlotsOffset + slot * kSectorSize, kSectorSize);
		slotOf[sector] = uint16_t(slot + 1);
		slotTarget[slot] = uint16_t(sector);
	}

	// Two blocks for the same page (with different block numbers) would AND
	// together in flash; keep the first
	if (!(pagesWritten[sector] & (1u << page)))
	{
		memcpy(pageBuf, b.data, kPageSize);
		uint32_t slot = slotOf[sector] - 1u;
		flash.Program(kSlotsOffset + slot * kSectorSize + page * kPageSize, pageBuf, kPageSize);
		pagesWritten[sector] = uint16_t(pagesWritten[sector] | (1u << page));
	}

	blockSeen[b.blockNo / 32] |= 1u << (b.blockNo % 32);
	received++;
	if (received == numBlocks) Finalize();
}

bool Store::SameAsStored(const uf2::Block &b)
{
	if (b.targetAddr < kTargetFlashBase) return false;
	uint32_t offset = b.targetAddr - kTargetFlashBase;
	if (offset >= kTargetFlashLimit) return false;
	uint32_t sector = offset / kSectorSize;
	uint32_t page = (offset % kSectorSize) / kPageSize;
	if (slotOf[sector] == 0 || !(pagesWritten[sector] & (1u << page))) return false;
	uint32_t slot = slotOf[sector] - 1u;
	return memcmp(flash.Read(kSlotsOffset + slot * kSectorSize + page * kPageSize), b.data, kPageSize) == 0;
}

void Store::Finalize()
{
	// Built in RAM a page at a time: the header is bigger than one page
	static uint8_t headerBuf[(sizeof(Header) + kPageSize - 1) / kPageSize * kPageSize] __attribute__((aligned(4)));
	memset(headerBuf, 0xFF, sizeof(headerBuf));

	Header *h = (Header *)headerBuf;
	h->magic = kHeaderMagic;
	h->version = kHeaderVersion;
	h->familyId = familyId;
	h->numBlocks = numBlocks;
	h->numSectors = slotsUsed;
	h->dataCrc = 0;
	for (uint32_t i = 0; i < slotsUsed; i++)
		h->dataCrc = Crc32Update(h->dataCrc, SectorData(i), kSectorSize);
	for (uint32_t i = 0; i < kMaxTargetSectors; i++)
		h->targetSector[i] = i < slotsUsed ? slotTarget[i] : 0xFFFF;
	h->_pad = 0xFFFF;
	h->headerCrc = Crc32(headerBuf, offsetof(Header, headerCrc));

	flash.Program(kHeaderOffset, headerBuf, sizeof(headerBuf));
	state = UploadState::Complete;
}

} // namespace payload
