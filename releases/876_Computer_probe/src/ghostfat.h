// A fake FAT16 volume for the USB drive, in the style of UF2 bootloaders:
// nothing is stored, every sector is generated on read. The volume holds one
// file, INFO.TXT. Writes are not interpreted as a filesystem at all: main.cpp
// passes every written sector to the payload store, which picks out the UF2
// blocks of whatever file the host copies.
#pragma once

#include <stdint.h>

namespace ghostfat
{

constexpr uint32_t kSectorSize = 512;
constexpr uint32_t kSectorCount = 262144; // 128MB, so any UF2 file fits as far as the host is concerned
constexpr uint32_t kMaxInfoLen = 4096;    // INFO.TXT is one 4kB cluster

// Fill buf (512 bytes) with sector `lba` of the volume
void ReadSector(uint32_t lba, uint8_t *buf, const char *infoText, uint32_t infoLen);

} // namespace ghostfat
