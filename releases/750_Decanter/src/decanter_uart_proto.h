// UART update protocol between a Decanter (Workshop Computer card 750)
// and a target RP2040 running a firmware with a Decanter updater. This file is
// shared by both sides: a target's updater includes its own copy, which must
// stay identical to the one in Workshop_Computer (releases/750_Decanter/src).
//
// Link: 921600 baud, 8N1, 3.3V. Straight-through wiring: SDA to SDA (GPIO0),
// SCL to SCL (GPIO1), GND to GND. The target uses its hardware UART0 (TX
// GPIO0, RX GPIO1); the Decanter uses PIO with the pins swapped. Neither side
// drives its TX pin except while it is sending (the target: while a Decanter
// is talking to it); released, each holds its line high with a pull-up.
//
// Every packet, in both directions:
//   Header, 12 bytes, little-endian:
//     uint8  magic0 'R', magic1 'U'
//     uint8  cmd
//     uint8  status   requests: 0; replies: Status
//     uint16 seq      a reply echoes its request's
//     uint16 len      payload bytes
//     uint32 offset   target flash offset (ERASE, PROGRAM, READ)
//   Payload, len bytes
//   CRC-32 (IEEE, as zlib) of header and payload, 4 bytes, little-endian
//
// The Decanter sends one request and waits for its reply, retrying on timeout or
// a bad CRC (every command is safe to repeat). Commands:
//   HELLO    answered in normal running and in update mode; reply payload is a
//            HelloInfo. Harmless: the target keeps running.
//   ENTER    request payload uint32 totalSectors (for the target's progress
//            LEDs). The target replies, stops everything else and enters
//            update mode, running from RAM until it reboots.
//   ERASE    erase the 4kB sector at offset
//   PROGRAM  payload: 4096 bytes, written to the (erased) sector at offset
//   READ     request payload uint32 length (<= 4096); reply payload: the data
//   REBOOT   reply, then reboot into the (new) firmware
//   CRC      request payload uint32 length (<= 4096); reply payload uint32
//            CRC-32 of that much flash from offset. Answered in normal running
//            too (about 0.3ms of CPU per 4kB), so the Decanter can check whether
//            a target already has the payload without stopping it.
// In update mode the target erases and programs only below flashLimit (it
// reports which in HELLO; 0x1FF000 keeps the last sector of a 2MB flash, for
// calibration data), and reboots by itself after 30 seconds without a valid
// packet.
//
// Code that must run from RAM on the target (while its flash is rewritten) is
// marked RU_RAMFUNC, which the target defines as __not_in_flash_func. Nothing
// here calls library code, so it also builds on a PC for tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef RU_RAMFUNC
#define RU_RAMFUNC(f) f
#endif

namespace ru
{

constexpr uint32_t kBaud = 921600;
constexpr uint32_t kProtocolVersion = 1;

constexpr uint8_t kMagic0 = 'R';
constexpr uint8_t kMagic1 = 'U';

constexpr uint32_t kHeaderSize = 12;
constexpr uint32_t kCrcSize = 4;
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kMaxPayload = kSectorSize;
constexpr uint32_t kMaxPacket = kHeaderSize + kMaxPayload + kCrcSize;

enum Cmd : uint8_t
{
	CmdHello = 1,
	CmdEnter = 2,
	CmdErase = 3,
	CmdProgram = 4,
	CmdRead = 5,
	CmdReboot = 6,
	CmdCrc = 7,
};

enum Status : uint8_t
{
	StatusOk = 0,
	StatusBadCrc = 1,     // the request arrived damaged: resend it
	StatusBadArgs = 2,    // misaligned offset, wrong length
	StatusProtected = 3,  // at or above flashLimit
	StatusBadMode = 4,    // a flash command outside update mode
	StatusUnknown = 5,
};

enum Mode : uint8_t
{
	ModeRunning = 0,
	ModeUpdate = 1,
};

// 16 bytes, so a HELLO reply (32 bytes) fits the target's UART TX FIFO and
// answering never makes its main loop wait
struct HelloInfo
{
	uint8_t protocolVersion;
	uint8_t mode;            // Mode
	char board[2];           // the kind of target, two characters chosen by its updater
	uint32_t flashLimit;     // erase/program refused at or above this offset
	uint8_t uniqueId[8];     // the target's flash unique ID
};
static_assert(sizeof(HelloInfo) == 16, "HelloInfo layout");

struct Header
{
	uint8_t cmd;
	uint8_t status;
	uint16_t seq;
	uint16_t len;
	uint32_t offset;
};

static inline uint32_t RU_RAMFUNC(Crc32Update)(uint32_t crc, const uint8_t *data, size_t len)
{
	crc = ~crc;
	for (size_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

static inline void RU_RAMFUNC(Put16)(uint8_t *p, uint32_t v)
{
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
}

static inline void RU_RAMFUNC(Put32)(uint8_t *p, uint32_t v)
{
	Put16(p, v);
	Put16(p + 2, v >> 16);
}

static inline uint32_t RU_RAMFUNC(Get16)(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8);
}

static inline uint32_t RU_RAMFUNC(Get32)(const uint8_t *p)
{
	return Get16(p) | (Get16(p + 2) << 16);
}

// Build a packet in out (at least kHeaderSize + len + kCrcSize bytes) and
// return its length
static inline uint32_t RU_RAMFUNC(Encode)(uint8_t *out, const Header &h, const uint8_t *payload)
{
	out[0] = kMagic0;
	out[1] = kMagic1;
	out[2] = h.cmd;
	out[3] = h.status;
	Put16(out + 4, h.seq);
	Put16(out + 6, h.len);
	Put32(out + 8, h.offset);
	for (uint32_t i = 0; i < h.len; i++) out[kHeaderSize + i] = payload[i];
	uint32_t n = kHeaderSize + h.len;
	Put32(out + n, Crc32Update(0, out, n));
	return n + kCrcSize;
}

// Incremental receiver: feed bytes one at a time. It hunts for the magic,
// so it resynchronises by itself after noise or a lost byte. The payload goes
// in a buffer the owner provides (SetBuffer), so a receiver that only ever
// expects small packets needs only a small one; longer packets are skipped.
class Parser
{
public:
	enum Event
	{
		None,
		Packet,  // header and payload are valid until the next Push
		BadCrc,  // a complete packet that failed its CRC; header() is unreliable
	};

	void RU_RAMFUNC(SetBuffer)(uint8_t *buffer, uint32_t capacity)
	{
		payloadBuf = buffer;
		cap = capacity;
		Reset();
	}

	Event RU_RAMFUNC(Push)(uint8_t b)
	{
		if (count == 0 && b != kMagic0) return None;
		if (count == 1 && b != kMagic1)
		{
			count = (b == kMagic0) ? 1 : 0;
			return None;
		}

		if (count < kHeaderSize)
		{
			head[count++] = b;
			if (count < kHeaderSize) return None;
			len = Get16(head + 6);
			if (len > cap) count = 0; // too long for us (or a false magic): keep hunting
			return None;
		}
		if (count < kHeaderSize + len)
		{
			payloadBuf[count++ - kHeaderSize] = b;
			return None;
		}
		tail[count++ - kHeaderSize - len] = b;
		if (count < kHeaderSize + len + kCrcSize) return None;

		count = 0;
		uint32_t crc = Crc32Update(Crc32Update(0, head, kHeaderSize), payloadBuf, len);
		if (crc != Get32(tail)) return BadCrc;

		hdr.cmd = head[2];
		hdr.status = head[3];
		hdr.seq = uint16_t(Get16(head + 4));
		hdr.len = uint16_t(len);
		hdr.offset = Get32(head + 8);
		return Packet;
	}

	void RU_RAMFUNC(Reset)()
	{
		count = 0;
	}

	// Part way through a packet (for an inter-byte timeout)
	bool Busy() const { return count != 0; }

	const Header &header() const { return hdr; }
	const uint8_t *payload() const { return payloadBuf; }

private:
	uint8_t head[kHeaderSize];
	uint8_t tail[kCrcSize];
	uint8_t *payloadBuf = nullptr;
	uint32_t cap = 0;
	uint32_t count = 0;
	uint32_t len = 0;
	Header hdr = {};
};

} // namespace ru
