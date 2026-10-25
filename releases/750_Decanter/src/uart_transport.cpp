#include "uart_transport.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "uart_link.pio.h"

static PIO const pio = pio0;
constexpr uint kSmTx = 0, kSmRx = 1;
constexpr uint kPinTx = 1; // SCL pad, to the target's RX (GPIO 1, SCL)
constexpr uint kPinRx = 0; // SDA pad, from the target's TX (GPIO 0, SDA)

constexpr uint32_t kTimeoutMs = 1000; // generous: a sector erase can take ~400ms
constexpr int kTries = 6;
constexpr uint32_t kReadChunk = 1024; // a damaged reply costs a 1kB resend, not 4kB

static uint8_t txBuf[ru::kMaxPacket] __attribute__((aligned(4)));
static uint8_t rxPayload[ru::kMaxPayload] __attribute__((aligned(4)));

void UartTransport::Init()
{
	parser.SetBuffer(rxPayload, sizeof(rxPayload));
	uint txOffset = pio_add_program(pio, &uart_link_tx_program);
	uint rxOffset = pio_add_program(pio, &uart_link_rx_program);
	uart_link_tx_program_init(pio, kSmTx, txOffset, kPinTx, ru::kBaud);
	uart_link_rx_program_init(pio, kSmRx, rxOffset, kPinRx, ru::kBaud);

	// TX is driven only while a packet goes out (see Send). Released, its
	// output is off and the pull-up holds the line idle high, so a target
	// using these pins some other way (I2C, or its own outputs) isn't fought.
	gpio_pull_up(kPinTx);
	gpio_set_oeover(kPinTx, GPIO_OVERRIDE_LOW);
}

void UartTransport::Send(const uint8_t *p, uint32_t n)
{
	const uint32_t stalled = 1u << (PIO_FDEBUG_TXSTALL_LSB + kSmTx);
	pio->fdebug = stalled; // clear the "waiting for data" flag

	gpio_set_oeover(kPinTx, GPIO_OVERRIDE_NORMAL);
	for (uint32_t i = 0; i < n; i++) pio_sm_put_blocking(pio, kSmTx, p[i]);

	// Done once the FIFO is empty and the program is stalled at its pull,
	// holding the line high: the last stop bit is out. Then let go.
	while (!pio_sm_is_tx_fifo_empty(pio, kSmTx)) {}
	pio->fdebug = stalled;
	uint32_t start = time_us_32();
	while (!(pio->fdebug & stalled) && time_us_32() - start < 50) {} // a byte is ~11us
	gpio_set_oeover(kPinTx, GPIO_OVERRIDE_LOW);
}

bool UartTransport::Fail(Error e)
{
	error = e;
	return false;
}

bool UartTransport::Transact(uint8_t cmd, uint32_t offset, const uint8_t *payload, uint32_t len,
                             uint8_t *reply, uint32_t replyLen, uint32_t timeoutMs, int tries)
{
	lastCmd = cmd;
	lastStatus = ru::StatusOk;
	Error why = Error::Timeout;

	for (int attempt = 0; attempt < tries; attempt++)
	{
		if (attempt) retries++;

		// Drop anything stale (a late reply to an earlier attempt)
		while (!pio_sm_is_rx_fifo_empty(pio, kSmRx)) pio_sm_get(pio, kSmRx);
		parser.Reset();

		uint16_t s = ++seq;
		ru::Header h = {cmd, 0, s, uint16_t(len), offset};
		Send(txBuf, ru::Encode(txBuf, h, payload));

		absolute_time_t deadline = make_timeout_time_ms(timeoutMs);
		bool retry = false;
		while (!retry)
		{
			if (time_reached(deadline)) break;
			if (pio_sm_is_rx_fifo_empty(pio, kSmRx)) continue;

			ru::Parser::Event e = parser.Push(uint8_t(pio_sm_get(pio, kSmRx) >> 24));
			if (e == ru::Parser::BadCrc)
			{
				why = Error::Garbled;
				retry = true;
				continue;
			}
			if (e != ru::Parser::Packet) continue;

			const ru::Header &r = parser.header();
			if (r.status == ru::StatusBadCrc)
			{
				// Our request arrived damaged
				why = Error::Garbled;
				retry = true;
				continue;
			}
			if (r.seq != s || r.cmd != cmd) continue; // not the reply to this request

			if (r.status != ru::StatusOk)
			{
				lastStatus = r.status;
				return Fail(Error::Refused);
			}
			if (r.len != replyLen) return Fail(Error::BadReply);
			if (replyLen) memcpy(reply, parser.payload(), replyLen);
			error = Error::None;
			return true;
		}
	}
	return Fail(why);
}

bool UartTransport::Hello(ru::HelloInfo &info, uint32_t timeoutMs)
{
	return Transact(ru::CmdHello, 0, nullptr, 0, (uint8_t *)&info, sizeof(info), timeoutMs, 1);
}

bool UartTransport::Enter(uint32_t totalSectors)
{
	uint8_t arg[4];
	ru::Put32(arg, totalSectors);
	return Transact(ru::CmdEnter, 0, arg, sizeof(arg), nullptr, 0, kTimeoutMs, kTries);
}

bool UartTransport::EraseSector(uint32_t offset)
{
	return Transact(ru::CmdErase, offset, nullptr, 0, nullptr, 0, kTimeoutMs, kTries);
}

bool UartTransport::ProgramSector(uint32_t offset, const uint8_t *data)
{
	return Transact(ru::CmdProgram, offset, data, ru::kSectorSize, nullptr, 0, kTimeoutMs, kTries);
}

bool UartTransport::Read(uint32_t offset, uint8_t *buf, uint32_t len)
{
	for (uint32_t done = 0; done < len; done += kReadChunk)
	{
		uint32_t n = len - done < kReadChunk ? len - done : kReadChunk;
		uint8_t arg[4];
		ru::Put32(arg, n);
		if (!Transact(ru::CmdRead, offset + done, arg, sizeof(arg), buf + done, n, kTimeoutMs, kTries)) return false;
	}
	return true;
}

bool UartTransport::SectorCrc(uint32_t offset, uint32_t *crc)
{
	uint8_t arg[4], out[4];
	ru::Put32(arg, ru::kSectorSize);
	if (!Transact(ru::CmdCrc, offset, arg, sizeof(arg), out, sizeof(out), kTimeoutMs, kTries)) return false;
	*crc = ru::Get32(out);
	return true;
}

bool UartTransport::Reboot()
{
	return Transact(ru::CmdReboot, 0, nullptr, 0, nullptr, 0, kTimeoutMs, kTries);
}
