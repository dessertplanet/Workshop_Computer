// Host tests for the UART update protocol framing (decanter_uart_proto.h), and
// an end-to-end run of the flashing engine through a simulated UART link to a
// simulated target that handles commands as a target's updater does.

#include <stdio.h>
#include <string.h>

#include <deque>
#include <random>
#include <vector>

#include "crc32.h"
#include "flasher.h"
#include "payload.h"
#include "decanter_uart_proto.h"

static int failures = 0;
#define CHECK(cond)                                                  \
	do                                                               \
	{                                                                \
		if (!(cond))                                                 \
		{                                                            \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                              \
		}                                                            \
	} while (0)

static std::vector<uint8_t> Packet(uint8_t cmd, uint16_t seq, uint32_t offset, const std::vector<uint8_t> &payload)
{
	std::vector<uint8_t> out(ru::kMaxPacket);
	ru::Header h = {cmd, 0, seq, uint16_t(payload.size()), offset};
	out.resize(ru::Encode(out.data(), h, payload.data()));
	return out;
}

static uint8_t bufA[4096], bufB[4096], bufC[4096], bufD[64];

static void TestFraming()
{
	printf("framing\n");
	std::vector<uint8_t> data(4096);
	for (size_t i = 0; i < data.size(); i++) data[i] = uint8_t(i * 7);
	auto pkt = Packet(ru::CmdProgram, 42, 0x3000, data);
	CHECK(pkt.size() == ru::kHeaderSize + 4096 + ru::kCrcSize);
	CHECK(ru::Crc32Update(0, pkt.data(), pkt.size() - 4) == Crc32(pkt.data(), pkt.size() - 4));

	ru::Parser p;
	p.SetBuffer(bufA, sizeof(bufA));
	int packets = 0;
	for (uint8_t b : pkt)
		if (p.Push(b) == ru::Parser::Packet) packets++;
	CHECK(packets == 1);
	CHECK(p.header().cmd == ru::CmdProgram && p.header().seq == 42 && p.header().offset == 0x3000);
	CHECK(p.header().len == 4096 && memcmp(p.payload(), data.data(), 4096) == 0);

	// Garbage and false magics before a packet, then a damaged packet, then a good one
	std::vector<uint8_t> stream = {0x00, 'R', 'R', 'U', 0xFF, 0xFF, 'R', 0x12};
	auto small = Packet(ru::CmdHello, 7, 0, {});
	auto damaged = small;
	damaged[9] ^= 0x10;
	stream.insert(stream.end(), damaged.begin(), damaged.end());
	stream.insert(stream.end(), small.begin(), small.end());
	ru::Parser q;
	q.SetBuffer(bufB, sizeof(bufB));
	int good = 0, bad = 0;
	for (uint8_t b : stream)
	{
		auto e = q.Push(b);
		if (e == ru::Parser::Packet) good++;
		if (e == ru::Parser::BadCrc) bad++;
	}
	CHECK(good == 1);
	CHECK(q.header().seq == 7);

	// A damaged packet on its own is reported
	ru::Parser d;
	d.SetBuffer(bufD, sizeof(bufD));
	bad = 0;
	for (uint8_t b : damaged)
		if (d.Push(b) == ru::Parser::BadCrc) bad++;
	CHECK(bad == 1);

	// An impossible length in a false header doesn't swallow what follows
	std::vector<uint8_t> fake = {'R', 'U', 1, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0};
	fake.insert(fake.end(), small.begin(), small.end());
	ru::Parser r;
	r.SetBuffer(bufC, sizeof(bufC));
	good = 0;
	for (uint8_t b : fake)
		if (r.Push(b) == ru::Parser::Packet) good++;
	CHECK(good == 1);

	// A small-buffer receiver (the target in normal running) skips a packet too
	// long for it and still catches the next one
	ru::Parser small16;
	uint8_t b16[16];
	small16.SetBuffer(b16, sizeof(b16));
	std::vector<uint8_t> mixed = pkt;
	mixed.insert(mixed.end(), small.begin(), small.end());
	good = 0;
	for (uint8_t b : mixed)
		if (small16.Push(b) == ru::Parser::Packet) good++;
	CHECK(good == 1 && small16.header().seq == 7);
}

// The target side, as a target's updater handles commands, over flash in memory
class SimUartTarget
{
public:
	SimUartTarget() : flash(2 * 1024 * 1024, 0x00) { parser.SetBuffer(rxBuf, sizeof(rxBuf)); }

	std::vector<uint8_t> flash;
	bool updateMode = false;
	bool rebooted = false;
	uint32_t crcRequests = 0;
	uint32_t erasedProtected = 0;

	// One burst of bytes; the gap before it resets a half-received packet,
	// as a target updater's inter-byte timeout does
	std::vector<uint8_t> Receive(const std::vector<uint8_t> &bytes)
	{
		std::vector<uint8_t> out;
		parser.Reset();
		for (uint8_t b : bytes)
		{
			auto e = parser.Push(b);
			if (e == ru::Parser::BadCrc) Reply(out, 0, ru::StatusBadCrc, 0, 0, nullptr, 0);
			if (e != ru::Parser::Packet) continue;
			Handle(out, parser.header(), parser.payload());
		}
		return out;
	}

private:
	static constexpr uint32_t kLimit = 0x1FF000;
	ru::Parser parser;
	uint8_t rxBuf[4096];

	void Reply(std::vector<uint8_t> &out, uint8_t cmd, uint8_t status, uint16_t seq, uint32_t offset, const uint8_t *payload, uint32_t len)
	{
		std::vector<uint8_t> pkt(ru::kMaxPacket);
		ru::Header h = {cmd, status, seq, uint16_t(len), offset};
		pkt.resize(ru::Encode(pkt.data(), h, payload));
		out.insert(out.end(), pkt.begin(), pkt.end());
	}

	void Handle(std::vector<uint8_t> &out, const ru::Header &h, const uint8_t *payload)
	{
		bool aligned = (h.offset & 4095) == 0;
		bool writable = h.offset + 4096 <= kLimit;
		if (h.cmd == ru::CmdHello)
		{
			ru::HelloInfo info = {};
			info.protocolVersion = ru::kProtocolVersion;
			info.flashLimit = kLimit;
			info.mode = updateMode;
			Reply(out, h.cmd, 0, h.seq, 0, (const uint8_t *)&info, sizeof(info));
			return;
		}
		if (h.cmd == ru::CmdEnter)
		{
			updateMode = true;
			Reply(out, h.cmd, 0, h.seq, 0, nullptr, 0);
			return;
		}
		if (h.cmd == ru::CmdCrc)
		{
			uint8_t crc[4];
			ru::Put32(crc, Crc32(&flash[h.offset], ru::Get32(payload)));
			Reply(out, h.cmd, 0, h.seq, h.offset, crc, 4);
			crcRequests++;
			return;
		}
		uint8_t status = ru::StatusOk;
		if (!updateMode) status = ru::StatusBadMode;
		else if (h.cmd == ru::CmdErase)
		{
			if (!aligned) status = ru::StatusBadArgs;
			else if (!writable) { status = ru::StatusProtected; erasedProtected++; }
			else memset(&flash[h.offset], 0xFF, 4096);
		}
		else if (h.cmd == ru::CmdProgram)
		{
			if (!aligned || h.len != 4096) status = ru::StatusBadArgs;
			else if (!writable) status = ru::StatusProtected;
			else for (int i = 0; i < 4096; i++) flash[h.offset + i] &= payload[i];
		}
		else if (h.cmd == ru::CmdRead)
		{
			uint32_t len = ru::Get32(payload);
			Reply(out, h.cmd, 0, h.seq, h.offset, &flash[h.offset], len);
			return;
		}
		else if (h.cmd == ru::CmdReboot)
		{
			rebooted = true;
		}

		else status = ru::StatusUnknown;
		Reply(out, h.cmd, status, h.seq, h.offset, nullptr, 0);
	}
};

// The Decanter side's request/retry logic (as UartTransport), over a link that
// corrupts or drops bytes
class SimLinkTransport : public TargetTransport
{
public:
	SimLinkTransport(SimUartTarget &t, double corruptRate, uint32_t seed) : target(t), rate(corruptRate), rng(seed) { parser.SetBuffer(rxBuf, sizeof(rxBuf)); }

	bool Enter() { return Transact(ru::CmdEnter, 0, {0, 0, 0, 0}, nullptr, 0); }
	bool EraseSector(uint32_t o) override { return Transact(ru::CmdErase, o, {}, nullptr, 0); }
	bool ProgramSector(uint32_t o, const uint8_t *d) override { return Transact(ru::CmdProgram, o, std::vector<uint8_t>(d, d + 4096), nullptr, 0); }
	bool Read(uint32_t o, uint8_t *b, uint32_t n) override
	{
		for (uint32_t done = 0; done < n; done += 1024) // as UartTransport
		{
			std::vector<uint8_t> a(4);
			ru::Put32(a.data(), 1024);
			if (!Transact(ru::CmdRead, o + done, a, b + done, 1024)) return false;
		}
		return true;
	}
	bool Reboot() { return Transact(ru::CmdReboot, 0, {}, nullptr, 0); }
	bool SectorCrc(uint32_t o, uint32_t *crc) override
	{
		std::vector<uint8_t> a(4);
		ru::Put32(a.data(), 4096);
		uint8_t out[4];
		if (!Transact(ru::CmdCrc, o, a, out, 4)) return false;
		*crc = ru::Get32(out);
		return true;
	}

	uint32_t retries = 0;
	uint8_t lastStatus = 0;

private:
	SimUartTarget &target;
	double rate;
	std::mt19937 rng;
	uint16_t seq = 0;
	ru::Parser parser;
	uint8_t rxBuf[4096];

	std::vector<uint8_t> Noisy(std::vector<uint8_t> v)
	{
		std::uniform_real_distribution<double> u(0, 1);
		std::vector<uint8_t> out;
		for (uint8_t b : v)
		{
			double x = u(rng);
			if (x < rate / 2) continue;               // dropped byte
			if (x < rate) b ^= uint8_t(1 << (rng() % 8)); // flipped bit
			out.push_back(b);
		}
		return out;
	}

	bool Transact(uint8_t cmd, uint32_t offset, const std::vector<uint8_t> &payload, uint8_t *reply, uint32_t replyLen)
	{
		for (int attempt = 0; attempt < 6; attempt++) // as UartTransport
		{
			if (attempt) retries++;
			parser.Reset();
			uint16_t s = ++seq;
			auto back = Noisy(target.Receive(Noisy(Packet(cmd, s, offset, payload))));
			for (uint8_t b : back)
			{
				if (parser.Push(b) != ru::Parser::Packet) continue;
				const ru::Header &r = parser.header();
				if (r.status == ru::StatusBadCrc || r.seq != s || r.cmd != cmd) continue;
				if (r.status != ru::StatusOk)
				{
					lastStatus = r.status;
					return false;
				}
				if (r.len != replyLen) return false;
				if (replyLen) memcpy(reply, parser.payload(), replyLen);
				return true;
			}
		}
		return false;
	}
};

class SimFlash : public FlashBackend
{
public:
	SimFlash() : mem(2 * 1024 * 1024, 0xFF) {}
	void Erase(uint32_t o, uint32_t n) override { memset(&mem[o], 0xFF, n); }
	void Program(uint32_t o, const uint8_t *d, uint32_t n) override { for (uint32_t i = 0; i < n; i++) mem[o + i] &= d[i]; }
	const uint8_t *Read(uint32_t o) override { return &mem[o]; }
	uint32_t Size() override { return uint32_t(mem.size()); }
	std::vector<uint8_t> mem;
};

static void StorePayload(payload::Store &s, uint32_t bytes, uint32_t seed)
{
	std::mt19937 rng(seed);
	uint32_t n = bytes / 256;
	for (uint32_t i = 0; i < n; i++)
	{
		uint8_t blk[512] = {};
		ru::Put32(blk + 0, 0x0A324655);
		ru::Put32(blk + 4, 0x9E5D5157);
		ru::Put32(blk + 8, 0x2000);
		ru::Put32(blk + 12, 0x10000000 + i * 256);
		ru::Put32(blk + 16, 256);
		ru::Put32(blk + 20, i);
		ru::Put32(blk + 24, n);
		ru::Put32(blk + 28, 0xE48BFF56);
		for (int j = 0; j < 256; j++) blk[32 + j] = uint8_t(rng());
		ru::Put32(blk + 508, 0x0AB16F30);
		s.HandleBlock(blk);
	}
}

static void TestEndToEnd(double rate)
{
	printf("flash over a simulated UART link, %.2f%% of bytes damaged\n", rate * 100);
	SimFlash card;
	payload::Store s(card);
	StorePayload(s, 60 * 4096, 3);
	CHECK(s.Valid());

	SimUartTarget target;
	target.flash[payload::kTargetFlashLimit + 5] = 0x42; // calibration
	SimLinkTransport link(target, rate, 11);
	CHECK(link.Enter());
	FlashResult fr = FlashPayload(s, link);
	if (fr != FlashResult::Ok) printf("  result: %s\n", FlashResultName(fr));
	CHECK(fr == FlashResult::Ok);
	CHECK(link.Reboot());
	CHECK(target.rebooted);
	for (uint32_t i = 0; i < s.SectorCount(); i++)
		CHECK(memcmp(&target.flash[s.TargetOffset(i)], s.SectorData(i), 4096) == 0);
	CHECK(target.flash[payload::kTargetFlashLimit + 5] == 0x42);
	printf("  %u retries\n", link.retries);
}

static void TestTargetRefusals()
{
	printf("target refuses flash commands outside update mode, and the protected sector\n");
	SimUartTarget target;
	SimLinkTransport link(target, 0, 1);
	uint8_t page[4096] = {};
	CHECK(!link.EraseSector(0));
	CHECK(link.lastStatus == ru::StatusBadMode);
	CHECK(link.Enter());
	CHECK(!link.EraseSector(0x1FF000));
	CHECK(link.lastStatus == ru::StatusProtected);
	CHECK(!link.ProgramSector(0x1FF000, page));
	CHECK(!link.EraseSector(0x100));
	CHECK(link.lastStatus == ru::StatusBadArgs);
}

static void TestCompareBeforeFlash()
{
	printf("compare before flashing: a target that has the payload is left running\n");
	SimFlash card;
	payload::Store s(card);
	StorePayload(s, 20 * 4096, 5);

	SimUartTarget target;
	SimLinkTransport link(target, 0, 2);
	CHECK(PayloadMatchesTarget(s, link) == MatchResult::Differs);
	CHECK(!target.updateMode); // checking never stops the target

	CHECK(link.Enter());
	CHECK(FlashPayload(s, link) == FlashResult::Ok);
	target.updateMode = false; // rebooted into the new firmware
	CHECK(PayloadMatchesTarget(s, link) == MatchResult::Matches);
	CHECK(!target.updateMode);

	// One byte different anywhere: differs
	target.flash[7 * 4096 + 1234] ^= 1;
	CHECK(PayloadMatchesTarget(s, link) == MatchResult::Differs);

	SimFlash empty;
	payload::Store none(empty);
	CHECK(PayloadMatchesTarget(none, link) == MatchResult::Unknown);
}

int main()
{
	TestFraming();
	TestTargetRefusals();
	TestCompareBeforeFlash();
	TestEndToEnd(0.0);
	TestEndToEnd(0.0001);
	if (failures)
	{
		printf("%d check(s) FAILED\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
