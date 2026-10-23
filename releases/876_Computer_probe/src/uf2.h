// UF2 block format: https://github.com/microsoft/uf2
#pragma once

#include <stdint.h>

namespace uf2
{

constexpr uint32_t kMagicStart0 = 0x0A324655; // "UF2\n"
constexpr uint32_t kMagicStart1 = 0x9E5D5157;
constexpr uint32_t kMagicEnd = 0x0AB16F30;

constexpr uint32_t kFlagNotMainFlash = 0x00000001;
constexpr uint32_t kFlagFamilyIdPresent = 0x00002000;

constexpr uint32_t kFamilyRP2040 = 0xE48BFF56;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kPayloadSize = 256; // what the Pico SDK and picotool always write

struct Block
{
	uint32_t magicStart0;
	uint32_t magicStart1;
	uint32_t flags;
	uint32_t targetAddr;
	uint32_t payloadSize;
	uint32_t blockNo;
	uint32_t numBlocks;
	uint32_t familyId; // or file size, if kFlagFamilyIdPresent is clear
	uint8_t data[476];
	uint32_t magicEnd;
};
static_assert(sizeof(Block) == kBlockSize, "UF2 block must be 512 bytes");

inline bool IsBlock(const Block &b)
{
	return b.magicStart0 == kMagicStart0 && b.magicStart1 == kMagicStart1 && b.magicEnd == kMagicEnd;
}

} // namespace uf2
