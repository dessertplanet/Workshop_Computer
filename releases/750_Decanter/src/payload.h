// Payload store: the target firmware kept in the program card's flash.
//
// A UF2 file arrives as a stream of 512-byte blocks (from the USB drive), in
// any order. Each 4kB target flash sector it touches gets a 4kB slot in the
// card's flash, filled page by page as blocks arrive. When every block of the
// file has arrived, a header listing which target sector each slot holds is
// written, and the payload becomes valid. Starting a new upload erases the
// header first, so a half-received file is never mistaken for a payload.
//
// Card flash layout:
//   0x000000  Decanter firmware (must end below kHeaderOffset)
//   0x040000  header sector
//   0x041000  slots, 4kB each, to the end of flash
#pragma once

#include <stdint.h>

#include "flash_backend.h"
#include "uf2.h"

namespace payload
{

constexpr uint32_t kSectorSize = FlashBackend::kSectorSize;
constexpr uint32_t kPageSize = FlashBackend::kPageSize;
constexpr uint32_t kPagesPerSector = kSectorSize / kPageSize;

// Target address space: an RP2040's XIP flash
constexpr uint32_t kTargetFlashBase = 0x10000000;

// Nothing is ever stored or written at or above this target flash offset: the
// last 4kB sector of a 2MB flash, where a target may keep calibration data
// that must survive a firmware update.
constexpr uint32_t kTargetFlashLimit = 0x1FF000;
constexpr uint32_t kMaxTargetSectors = kTargetFlashLimit / kSectorSize; // 511

// UF2 files with more blocks than this can't fit below kTargetFlashLimit
constexpr uint32_t kMaxBlocks = 8192;

constexpr uint32_t kHeaderOffset = 0x40000;
constexpr uint32_t kSlotsOffset = kHeaderOffset + kSectorSize;

constexpr uint32_t kHeaderMagic = 0x31425250; // "PRB1" as bytes in flash
constexpr uint32_t kHeaderVersion = 1;

struct Header
{
	uint32_t magic;
	uint32_t version;
	uint32_t familyId;
	uint32_t numBlocks;   // UF2 blocks in the uploaded file
	uint32_t numSectors;  // slots used
	uint32_t dataCrc;     // CRC-32 of all used slots, in slot order
	uint16_t targetSector[kMaxTargetSectors]; // target flash sector held by each slot
	uint16_t _pad;
	uint32_t headerCrc;   // CRC-32 of everything above
};
static_assert(sizeof(Header) <= kSectorSize, "header must fit its sector");

enum class UploadState : uint8_t
{
	Idle,       // no upload since power-up
	Receiving,
	Complete,   // payload valid
	Failed,     // see UploadError; the stored payload is gone
};

enum class UploadError : uint8_t
{
	None,
	BadBlock,     // payload size not 256, misaligned address, or blockNo out of range
	OutOfRange,   // a block below the target flash base or at/above kTargetFlashLimit
	TooLarge,     // more target sectors than card flash slots, or too many blocks
};

class Store
{
public:
	explicit Store(FlashBackend &flash);

	// Stored payload
	bool Valid();
	uint32_t SectorCount();                 // 0 if not valid
	uint32_t TargetOffset(uint32_t slot);   // target flash offset of a slot
	const uint8_t *SectorData(uint32_t slot);
	bool DataCrcMatches();                  // re-checks the slots against the header
	const Header *GetHeader();              // nullptr if not valid
	uint32_t Capacity();                    // slots available, in sectors

	// Upload. Feed every 512-byte sector written to the drive; anything that
	// isn't a UF2 block is ignored, and so are blocks for other chip families.
	void HandleBlock(const uint8_t *block512);

	UploadState State() const { return state; }
	UploadError Error() const { return error; }
	uint32_t BlocksReceived() const { return received; }
	uint32_t BlocksExpected() const { return numBlocks; }
	uint32_t IgnoredBlocks() const { return ignored; } // other families / non-flash

private:
	void StartSession(uint32_t blocks, uint32_t family);
	void Fail(UploadError e);
	void Finalize();
	bool SameAsStored(const uf2::Block &b);

	FlashBackend &flash;

	UploadState state = UploadState::Idle;
	UploadError error = UploadError::None;
	uint32_t numBlocks = 0;
	uint32_t familyId = 0;
	uint32_t received = 0;
	uint32_t ignored = 0;
	uint32_t ignoredBeforeSession = 0;
	uint32_t slotsUsed = 0;

	uint16_t slotOf[kMaxTargetSectors];        // slot + 1, or 0 if none yet
	uint16_t slotTarget[kMaxTargetSectors];    // target sector held by each slot
	uint16_t pagesWritten[kMaxTargetSectors];  // bit per page, by target sector
	uint32_t blockSeen[kMaxBlocks / 32];

	// Flash programming needs its source in RAM, a page at a time
	uint8_t pageBuf[kPageSize] __attribute__((aligned(4)));
};

} // namespace payload
