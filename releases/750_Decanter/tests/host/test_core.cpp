// Host tests for the Decanter core: payload store, flashing engine and
// fake FAT volume, against simulated flash. Build and run with `make` here.
//
//   ./test_core [real.uf2 real.bin]
//
// With a real SDK-built UF2 and its matching .bin, also checks that the
// payload reproduces the binary exactly.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <random>
#include <vector>

#include "crc32.h"
#include "flasher.h"
#include "ghostfat.h"
#include "payload.h"
#include "uf2.h"

using namespace payload;

static int failures = 0;
#define CHECK(cond)                                                       \
	do                                                                    \
	{                                                                     \
		if (!(cond))                                                      \
		{                                                                 \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
			failures++;                                                   \
		}                                                                 \
	} while (0)

// NOR flash: erase sets 0xFF, program can only clear bits
class SimFlash : public FlashBackend
{
public:
	explicit SimFlash(uint32_t size) : mem(size, 0xFF) {}

	void Erase(uint32_t offset, uint32_t len) override
	{
		if (offset % kSectorSize || len % kSectorSize || offset + len > mem.size())
		{
			printf("  bad erase %x+%x\n", offset, len);
			failures++;
			return;
		}
		memset(&mem[offset], 0xFF, len);
		erases++;
	}

	void Program(uint32_t offset, const uint8_t *data, uint32_t len) override
	{
		if (offset % kPageSize || len % kPageSize || offset + len > mem.size())
		{
			printf("  bad program %x+%x\n", offset, len);
			failures++;
			return;
		}
		for (uint32_t i = 0; i < len; i++)
		{
			if (mem[offset + i] != 0xFF) notErased++;
			mem[offset + i] &= data[i];
		}
	}

	const uint8_t *Read(uint32_t offset) override { return &mem[offset]; }
	uint32_t Size() override { return uint32_t(mem.size()); }

	std::vector<uint8_t> mem;
	uint32_t erases = 0;
	uint32_t notErased = 0; // programs over unerased bytes
};

// A target RP2040's flash, reached through a transport
class SimTarget : public TargetTransport
{
public:
	SimTarget() : flash(2 * 1024 * 1024) {}

	bool EraseSector(uint32_t offset) override
	{
		log.push_back('E');
		logOffsets.push_back(offset);
		if (failEraseAt == int64_t(offset)) return false;
		flash.Erase(offset, kSectorSize);
		return true;
	}
	bool ProgramSector(uint32_t offset, const uint8_t *data) override
	{
		log.push_back('P');
		logOffsets.push_back(offset);
		flash.Program(offset, data, kSectorSize);
		if (corruptAt == int64_t(offset)) flash.mem[offset + 17] ^= 0x40;
		return true;
	}
	bool Read(uint32_t offset, uint8_t *buf, uint32_t len) override
	{
		memcpy(buf, flash.Read(offset), len);
		return true;
	}

	SimFlash flash;
	std::vector<char> log;
	std::vector<uint32_t> logOffsets;
	int64_t failEraseAt = -1;
	int64_t corruptAt = -1;
};

static uf2::Block MakeBlock(uint32_t addr, uint32_t blockNo, uint32_t numBlocks, const uint8_t *data,
                            uint32_t family = uf2::kFamilyRP2040)
{
	uf2::Block b;
	memset(&b, 0, sizeof(b));
	b.magicStart0 = uf2::kMagicStart0;
	b.magicStart1 = uf2::kMagicStart1;
	b.flags = uf2::kFlagFamilyIdPresent;
	b.targetAddr = addr;
	b.payloadSize = 256;
	b.blockNo = blockNo;
	b.numBlocks = numBlocks;
	b.familyId = family;
	memcpy(b.data, data, 256);
	b.magicEnd = uf2::kMagicEnd;
	return b;
}

// A synthetic image: the given target offsets, each a 256-byte page of
// pseudo-random data
struct Image
{
	std::vector<uint32_t> offsets;
	std::vector<std::vector<uint8_t>> pages;

	std::vector<uf2::Block> Blocks() const
	{
		std::vector<uf2::Block> v;
		for (size_t i = 0; i < offsets.size(); i++)
			v.push_back(MakeBlock(kTargetFlashBase + offsets[i], uint32_t(i), uint32_t(offsets.size()), pages[i].data()));
		return v;
	}
};

static Image MakeImage(const std::vector<uint32_t> &offsets, uint32_t seed)
{
	std::mt19937 rng(seed);
	Image img;
	img.offsets = offsets;
	for (size_t i = 0; i < offsets.size(); i++)
	{
		std::vector<uint8_t> p(256);
		for (auto &x : p) x = uint8_t(rng());
		img.pages.push_back(p);
	}
	return img;
}

static void Feed(Store &s, const std::vector<uf2::Block> &blocks)
{
	for (const auto &b : blocks) s.HandleBlock((const uint8_t *)&b);
}

// What the target's flash should hold after flashing img onto an erased chip
static bool TargetMatches(SimTarget &t, const Image &img)
{
	std::vector<uint8_t> expect(2 * 1024 * 1024, 0xFF);
	for (size_t i = 0; i < img.offsets.size(); i++)
		memcpy(&expect[img.offsets[i]], img.pages[i].data(), 256);
	return t.flash.mem == expect;
}

static std::vector<uint32_t> PagesCovering(uint32_t start, uint32_t len)
{
	std::vector<uint32_t> v;
	for (uint32_t o = start; o < start + len; o += 256) v.push_back(o);
	return v;
}

static void TestUploadAndFlash()
{
	printf("upload in random order, then flash\n");
	SimFlash card(2 * 1024 * 1024);
	Store s(card);
	CHECK(!s.Valid());
	CHECK(s.Capacity() == (2 * 1024 * 1024 - kSlotsOffset) / kSectorSize);

	// Sector 0 in full, a partial sector, and a page just below the limit
	std::vector<uint32_t> offs = PagesCovering(0, 4096);
	for (uint32_t o : PagesCovering(0x5100, 0x300)) offs.push_back(o);
	offs.push_back(kTargetFlashLimit - 256);
	Image img = MakeImage(offs, 1);

	auto blocks = img.Blocks();
	std::shuffle(blocks.begin(), blocks.end(), std::mt19937(7));
	Feed(s, blocks);

	CHECK(s.State() == UploadState::Complete);
	CHECK(s.Error() == UploadError::None);
	CHECK(s.Valid());
	CHECK(s.SectorCount() == 3);
	CHECK(s.DataCrcMatches());
	CHECK(card.notErased == 0);

	SimTarget t;
	uint32_t lastDone = 0, lastTotal = 0;
	auto progress = [](uint32_t done, uint32_t total, void *ctx) {
		uint32_t *p = (uint32_t *)ctx;
		p[0] = done;
		p[1] = total;
	};
	uint32_t pr[2] = {0, 0};
	FlashResult r = FlashPayload(s, t, progress, pr);
	lastDone = pr[0];
	lastTotal = pr[1];
	CHECK(r == FlashResult::Ok);
	CHECK(lastDone == 3 && lastTotal == 3);
	CHECK(TargetMatches(t, img));

	// Sector 0: erased first, programmed last, and erased only once
	CHECK(t.log.front() == 'E' && t.logOffsets.front() == 0);
	CHECK(t.log.back() == 'P' && t.logOffsets.back() == 0);
	CHECK(std::count(t.logOffsets.begin(), t.logOffsets.end(), 0u) == 2);
	CHECK(t.flash.notErased == 0);

	// Nothing ever written at or above the limit
	for (uint32_t o : t.logOffsets) CHECK(o < kTargetFlashLimit);
}

static void TestRefusesProtectedSector()
{
	printf("refuses the protected last sector\n");
	SimFlash card(2 * 1024 * 1024);
	Store s(card);

	Image good = MakeImage(PagesCovering(0, 1024), 2);
	Feed(s, good.Blocks());
	CHECK(s.Valid());

	std::vector<uint32_t> offs = PagesCovering(0, 512);
	offs.push_back(kTargetFlashLimit); // first page of 0x1FF000
	Image bad = MakeImage(offs, 3);
	Feed(s, bad.Blocks());
	CHECK(s.State() == UploadState::Failed);
	CHECK(s.Error() == UploadError::OutOfRange);
	CHECK(!s.Valid()); // the old payload is gone, never a mix

	// Addresses outside flash entirely
	SimFlash card2(2 * 1024 * 1024);
	Store s2(card2);
	uint8_t page[256] = {0};
	uf2::Block b = MakeBlock(0x20000000, 0, 1, page);
	s2.HandleBlock((const uint8_t *)&b);
	CHECK(s2.Error() == UploadError::OutOfRange);
	b = MakeBlock(0x0FFFFF00, 0, 1, page);
	s2.HandleBlock((const uint8_t *)&b);
	CHECK(s2.Error() == UploadError::OutOfRange);
}

static void TestIgnoresOtherFamiliesAndJunk()
{
	printf("ignores other families and non-UF2 sectors\n");
	SimFlash card(2 * 1024 * 1024);
	Store s(card);

	uint8_t junk[512];
	memset(junk, 0x5A, sizeof(junk));
	s.HandleBlock(junk);
	CHECK(s.State() == UploadState::Idle);

	Image img = MakeImage(PagesCovering(0, 2048), 4);
	auto blocks = img.Blocks();
	// RP2350 blocks interleaved, as in a universal UF2
	std::vector<uf2::Block> mixed;
	uint8_t page[256] = {1};
	for (size_t i = 0; i < blocks.size(); i++)
	{
		mixed.push_back(MakeBlock(kTargetFlashBase + uint32_t(i) * 256, uint32_t(i), uint32_t(blocks.size()), page, 0xE48BFF59));
		mixed.push_back(blocks[i]);
	}
	Feed(s, mixed);
	CHECK(s.State() == UploadState::Complete);
	CHECK(s.IgnoredBlocks() == blocks.size());

	SimTarget t;
	CHECK(FlashPayload(s, t) == FlashResult::Ok);
	CHECK(TargetMatches(t, img));
}

static void TestReuploadAndRestart()
{
	printf("re-upload replaces; interrupted copy restarts cleanly\n");
	SimFlash card(2 * 1024 * 1024);
	Store s(card);

	Image a = MakeImage(PagesCovering(0, 8192), 5);
	Feed(s, a.Blocks());
	CHECK(s.Valid());

	// Same size file, first half only (copy interrupted), then the whole file
	Image b = MakeImage(PagesCovering(0, 8192), 6);
	auto bb = b.Blocks();
	Feed(s, std::vector<uf2::Block>(bb.begin(), bb.begin() + 16));
	CHECK(s.State() == UploadState::Receiving);
	CHECK(!s.Valid());
	Feed(s, bb);
	CHECK(s.State() == UploadState::Complete);

	SimTarget t;
	CHECK(FlashPayload(s, t) == FlashResult::Ok);
	CHECK(TargetMatches(t, b));

	// Duplicate blocks are harmless
	Image c = MakeImage(PagesCovering(0x10000, 4096), 7);
	auto cb = c.Blocks();
	std::vector<uf2::Block> dup;
	for (auto &x : cb)
	{
		dup.push_back(x);
		dup.push_back(x);
	}
	Feed(s, dup);
	CHECK(s.State() == UploadState::Complete);
	CHECK(card.notErased == 0);

	// Stray re-writes of the finished file's blocks keep the payload
	Feed(s, std::vector<uf2::Block>(cb.end() - 3, cb.end()));
	CHECK(s.State() == UploadState::Complete);
	CHECK(s.Valid());
	SimTarget tc;
	CHECK(FlashPayload(s, tc) == FlashResult::Ok);
	CHECK(TargetMatches(tc, c));
}

static void TestNeverMixesFiles()
{
	printf("interrupted copy of one file, then a different same-size file\n");
	SimFlash card(2 * 1024 * 1024);
	Store s(card);

	Image a = MakeImage(PagesCovering(0, 8192), 10);
	Image b = MakeImage(PagesCovering(0, 8192), 11);
	b.pages[0] = a.pages[0]; // same boot2, as two builds with the same SDK
	b.pages[1] = a.pages[1];

	auto ab = a.Blocks(), bb = b.Blocks();
	Feed(s, std::vector<uf2::Block>(ab.begin(), ab.begin() + 20));
	Feed(s, bb);
	if (s.State() == UploadState::Complete)
	{
		SimTarget t;
		CHECK(FlashPayload(s, t) == FlashResult::Ok);
		CHECK(TargetMatches(t, b));
	}
	else
	{
		CHECK(!s.Valid()); // not complete is fine; a mix of a and b is not
	}

	// Copying b again always gives b
	Feed(s, bb);
	CHECK(s.State() == UploadState::Complete);
	SimTarget t2;
	CHECK(FlashPayload(s, t2) == FlashResult::Ok);
	CHECK(TargetMatches(t2, b));
}

static void TestBadBlocksAndCapacity()
{
	printf("bad blocks and capacity\n");
	uint8_t page[256] = {0};

	SimFlash card(2 * 1024 * 1024);
	Store s(card);
	uf2::Block b = MakeBlock(kTargetFlashBase + 0x80, 0, 1, page); // misaligned
	s.HandleBlock((const uint8_t *)&b);
	CHECK(s.Error() == UploadError::BadBlock);

	Store s2(card);
	b = MakeBlock(kTargetFlashBase, 0, 1, page);
	b.payloadSize = 476;
	s2.HandleBlock((const uint8_t *)&b);
	CHECK(s2.Error() == UploadError::BadBlock);

	// A program card too small for the payload: 0x41000 + 2 slots
	SimFlash tiny(kSlotsOffset + 2 * kSectorSize);
	Store s3(tiny);
	CHECK(s3.Capacity() == 2);
	Image img = MakeImage({0, 0x1000, 0x2000}, 8);
	Feed(s3, img.Blocks());
	CHECK(s3.Error() == UploadError::TooLarge);
	CHECK(!s3.Valid());
}

static void TestFlashFailures()
{
	printf("flash failures are reported, payload corruption is caught\n");
	SimFlash card(2 * 1024 * 1024);
	Store s(card);
	Image img = MakeImage(PagesCovering(0, 3 * 4096), 9);
	Feed(s, img.Blocks());
	CHECK(s.Valid());

	SimTarget t1;
	t1.corruptAt = 0x1000;
	CHECK(FlashPayload(s, t1) == FlashResult::VerifyFailed);
	// It stopped before writing sector 0, which is still erased: the target
	// would stay in BOOTSEL
	std::vector<uint8_t> erased(4096, 0xFF);
	CHECK(memcmp(t1.flash.Read(0), erased.data(), 4096) == 0);

	SimTarget t2;
	t2.failEraseAt = 0x2000;
	CHECK(FlashPayload(s, t2) == FlashResult::EraseFailed);

	// Flip a bit in a stored slot
	card.mem[kSlotsOffset + 100] ^= 0x01;
	CHECK(s.Valid());
	CHECK(!s.DataCrcMatches());
	SimTarget t3;
	CHECK(FlashPayload(s, t3) == FlashResult::PayloadCorrupt);
	CHECK(t3.log.empty());

	SimFlash empty(2 * 1024 * 1024);
	Store s4(empty);
	SimTarget t4;
	CHECK(FlashPayload(s4, t4) == FlashResult::NoPayload);
}

static void TestGhostFat(const char *imagePath)
{
	printf("fake FAT16 volume\n");
	const char *info = "Hello from Decanter\r\n";
	uint32_t len = uint32_t(strlen(info));
	uint8_t s[512];

	ghostfat::ReadSector(0, s, info, len);
	CHECK(s[510] == 0x55 && s[511] == 0xAA);
	CHECK(memcmp(s + 54, "FAT16   ", 8) == 0);

	ghostfat::ReadSector(1, s, info, len);
	CHECK(s[0] == 0xF8 && s[1] == 0xFF && s[4] == 0xFF && s[5] == 0xFF);
	ghostfat::ReadSector(129, s, info, len); // second FAT
	CHECK(s[0] == 0xF8);

	ghostfat::ReadSector(257, s, info, len);
	CHECK(memcmp(s + 32, "INFO    TXT", 11) == 0);
	CHECK(s[32 + 28] == len);

	ghostfat::ReadSector(289, s, info, len);
	CHECK(memcmp(s, info, len) == 0);

	// Write out the non-empty sectors as a sparse disk image, for fsck
	if (imagePath)
	{
		FILE *f = fopen(imagePath, "wb");
		if (!f) return;
		for (uint32_t lba = 0; lba < 300; lba++)
		{
			ghostfat::ReadSector(lba, s, info, len);
			fseek(f, long(lba) * 512, SEEK_SET);
			fwrite(s, 1, 512, f);
		}
		fseek(f, long(ghostfat::kSectorCount) * 512 - 1, SEEK_SET);
		fputc(0, f);
		fclose(f);
	}
}

// A real UF2 built by the Pico SDK, against the .bin from the same build
static void TestRealUf2(const char *uf2Path, const char *binPath)
{
	printf("real UF2: %s\n", uf2Path);
	FILE *f = fopen(uf2Path, "rb");
	FILE *g = fopen(binPath, "rb");
	if (!f || !g)
	{
		printf("  FAIL can't open inputs\n");
		failures++;
		return;
	}
	std::vector<uint8_t> uf2data, bin;
	int c;
	while ((c = fgetc(f)) != EOF) uf2data.push_back(uint8_t(c));
	while ((c = fgetc(g)) != EOF) bin.push_back(uint8_t(c));
	fclose(f);
	fclose(g);

	SimFlash card(2 * 1024 * 1024);
	Store s(card);
	for (size_t i = 0; i + 512 <= uf2data.size(); i += 512) s.HandleBlock(&uf2data[i]);
	CHECK(s.State() == UploadState::Complete);
	CHECK(s.BlocksReceived() == uf2data.size() / 512);

	SimTarget t;
	CHECK(FlashPayload(s, t) == FlashResult::Ok);
	CHECK(bin.size() < t.flash.mem.size());
	CHECK(memcmp(t.flash.Read(0), bin.data(), bin.size()) == 0);
	printf("  %zu blocks, %u sectors, %zu-byte image reproduced\n", uf2data.size() / 512, s.SectorCount(), bin.size());
}

int main(int argc, char **argv)
{
	TestUploadAndFlash();
	TestRefusesProtectedSector();
	TestIgnoresOtherFamiliesAndJunk();
	TestReuploadAndRestart();
	TestNeverMixesFiles();
	TestBadBlocksAndCapacity();
	TestFlashFailures();
	TestGhostFat(getenv("FAT_IMAGE"));
	if (argc == 3) TestRealUf2(argv[1], argv[2]);

	if (failures)
	{
		printf("%d check(s) FAILED\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
