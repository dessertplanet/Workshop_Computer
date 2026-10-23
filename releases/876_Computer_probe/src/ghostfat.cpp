#include "ghostfat.h"

#include <string.h>

namespace ghostfat
{

// Volume layout, in 512-byte sectors:
//   0          boot sector
//   1..128     FAT 1
//   129..256   FAT 2
//   257..288   root directory (512 entries)
//   289..      data, 8-sector (4kB) clusters; cluster 2 is INFO.TXT
constexpr uint32_t kReservedSectors = 1;
constexpr uint32_t kNumFats = 2;
constexpr uint32_t kSectorsPerFat = 128;   // 32768 entries >= 32733 clusters + 2
constexpr uint32_t kRootEntries = 512;
constexpr uint32_t kRootSectors = kRootEntries * 32 / kSectorSize;
constexpr uint32_t kSectorsPerCluster = 8;

constexpr uint32_t kFatStart = kReservedSectors;
constexpr uint32_t kRootStart = kFatStart + kNumFats * kSectorsPerFat;
constexpr uint32_t kDataStart = kRootStart + kRootSectors;

constexpr uint32_t kClusterCount = (kSectorCount - kDataStart) / kSectorsPerCluster;
static_assert(kClusterCount >= 4085 && kClusterCount < 65525, "must be FAT16");
static_assert(kClusterCount + 2 <= kSectorsPerFat * kSectorSize / 2, "FAT too small");
static_assert(kMaxInfoLen == kSectorsPerCluster * kSectorSize, "INFO.TXT is one cluster");

constexpr uint16_t kFatDate = ((2026 - 1980) << 9) | (10 << 5) | 23;

static void Put16(uint8_t *p, uint16_t v)
{
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
}

static void Put32(uint8_t *p, uint32_t v)
{
	Put16(p, uint16_t(v));
	Put16(p + 2, uint16_t(v >> 16));
}

static void BootSector(uint8_t *s)
{
	static const uint8_t jump[3] = {0xEB, 0x3C, 0x90};
	memcpy(s, jump, 3);
	memcpy(s + 3, "UF2 UF2 ", 8);         // OEM name
	Put16(s + 11, kSectorSize);
	s[13] = kSectorsPerCluster;
	Put16(s + 14, kReservedSectors);
	s[16] = kNumFats;
	Put16(s + 17, kRootEntries);
	Put16(s + 19, 0);                     // total sectors in the 32-bit field
	s[21] = 0xF8;                         // fixed disk
	Put16(s + 22, kSectorsPerFat);
	Put16(s + 24, 1);                     // sectors per track
	Put16(s + 26, 1);                     // heads
	Put32(s + 28, 0);                     // hidden sectors
	Put32(s + 32, kSectorCount);
	s[36] = 0x80;                         // drive number
	s[38] = 0x29;                         // extended boot signature
	Put32(s + 39, 0x0876C0DE);            // volume serial
	memcpy(s + 43, "COMP-PROBE ", 11);
	memcpy(s + 54, "FAT16   ", 8);
	s[510] = 0x55;
	s[511] = 0xAA;
}

static void DirEntry(uint8_t *e, const char name83[11], uint8_t attr, uint16_t cluster, uint32_t size)
{
	memcpy(e, name83, 11);
	e[11] = attr;
	Put16(e + 16, kFatDate); // created
	Put16(e + 18, kFatDate); // accessed
	Put16(e + 24, kFatDate); // modified
	Put16(e + 26, cluster);
	Put32(e + 28, size);
}

void ReadSector(uint32_t lba, uint8_t *buf, const char *infoText, uint32_t infoLen)
{
	memset(buf, 0, kSectorSize);
	if (infoLen > kMaxInfoLen) infoLen = kMaxInfoLen;

	if (lba == 0)
	{
		BootSector(buf);
	}
	else if (lba >= kFatStart && lba < kRootStart)
	{
		// Only the first sector of each FAT has entries: media, end-of-chain,
		// and cluster 2 (INFO.TXT, a one-cluster file)
		if ((lba - kFatStart) % kSectorsPerFat == 0)
		{
			Put16(buf + 0, 0xFFF8);
			Put16(buf + 2, 0xFFFF);
			Put16(buf + 4, 0xFFFF);
		}
	}
	else if (lba == kRootStart)
	{
		DirEntry(buf, "COMP-PROBE ", 0x08, 0, 0);          // volume label
		DirEntry(buf + 32, "INFO    TXT", 0x01, 2, infoLen); // read-only
	}
	else if (lba >= kDataStart && lba < kDataStart + kSectorsPerCluster)
	{
		uint32_t start = (lba - kDataStart) * kSectorSize;
		if (start < infoLen)
		{
			uint32_t n = infoLen - start;
			memcpy(buf, infoText + start, n < kSectorSize ? n : kSectorSize);
		}
	}
}

} // namespace ghostfat
