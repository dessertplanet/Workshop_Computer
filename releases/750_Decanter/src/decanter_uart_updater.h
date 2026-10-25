/*
decanter_uart_updater.h - the target side of Decanter's UART updates

The reference updater for a target RP2040: include it in the target's
firmware, and a Decanter (Workshop Computer card 750) wired to the target's
UART0 pins can flash new firmware onto it while it runs. No buttons, no USB,
no BOOTSEL. This file is shared: a target keeps its own copy, identical to the
one in Workshop_Computer (releases/750_Decanter/src), next to
decanter_uart_proto.h.

Wiring: the target's GPIO 0 (UART0 TX) to the Decanter's TX pad, GPIO 1
(UART0 RX) to its RX pad, GND to GND. (Those are the SDA and SCL pins on
boards that label them for I2C: SDA to SDA, SCL to SCL.)

The target only drives its TX pin while it is answering a Decanter: the rest
of the time GPIO 0 is an input with a pull-up. Only a Decanter sends requests,
so if the cable is left connected to a Computer running some other card (which
will usually drive both pins as outputs), the two never fight. A 1k resistor
in series with each signal wire protects every combination, I2C included.

Use, with the system clock already set (uart_init takes its baud rate from it):

    static decanter::UartUpdater<> updater;  // or <MyBoard>, see below
    updater.Begin();                         // takes UART0 on GPIO 0/1
    while (true) {
        updater.Poll();                      // call often; cheap when nothing arrives
        ...
    }

While the target runs normally, Poll answers the Decanter's HELLO and CRC (a
checksum of a flash sector, so the Decanter can tell whether the target
already has its firmware) and nothing else. That costs one register read per
call when the line is quiet, and no interrupts, DMA or timers. When the
Decanter sends ENTER, the updater stops the other core, all DMA, both PIOs and
every interrupt, and runs from RAM: the Decanter then erases, writes and reads
back flash sector by sector, and finally reboots the target into the new
firmware. Flash at or above the board's flashLimit is never erased or written,
whatever the Decanter asks. If the Decanter goes quiet for 30 seconds the
target reboots by itself.

Poll may run on either core. In update mode the 4kB packet buffer is the
stopped core's stack bank (SCRATCH_X or SCRATCH_Y), so no RAM is set aside
for it while the app runs.

The Decanter erases target flash sector 0 (boot2) first and writes it last, so
a target whose update was interrupted can't boot half a firmware: it starts in
the RP2040's USB bootloader instead. Recover it over USB (a Decanter in USB
host mode, or a computer), since the updater went with the rest of the
firmware.

Everything that runs once update mode starts is in RAM and calls nothing in
flash: no SDK functions except the flash ones (which run from RAM), no switch
statements (on the M0+ their jump tables call a helper in flash), no division
(it would call a helper in flash), no copy loops GCC could turn into memcpy.

Board traits: what the updater can't know about a board, as a struct of
statics passed as the template argument. Anything not given comes from
DefaultBoard:

    struct MyBoard : decanter::DefaultBoard
    {
        static constexpr char board0 = 'X', board1 = 'Y'; // reported in HELLO
        static constexpr uint32_t flashLimit = 0x1FF000;  // protect from here up

        // Called as update mode starts, after the app is stopped and before
        // flash is touched, so it may run from flash
        static void Quiesce() {}

        // Called in update mode after each packet: must be RAM-only, as above
        static void RU_RAMFUNC(ShowProgress)(uint32_t done, uint32_t total, bool activity) {}
    };

Protocol: decanter_uart_proto.h.
*/
#pragma once

#include <string.h>

#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/uart.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "pico.h"
#include "pico/unique_id.h"

// Update-mode code is resident in RAM all the time, but only runs with the
// app stopped, so it is built for size, not speed
#ifndef RU_RAMFUNC
#define RU_RAMFUNC(f) __attribute__((optimize("Os"))) __not_in_flash_func(f)
#endif
#include "decanter_uart_proto.h"

namespace decanter
{

struct DefaultBoard
{
	static constexpr char board0 = '?', board1 = '?';

	// The last 4kB sector of a 2MB flash, kept for calibration data
	static constexpr uint32_t flashLimit = 0x1FF000;

	static void Quiesce() {}
	static void RU_RAMFUNC(ShowProgress)(uint32_t done, uint32_t total, bool activity)
	{
		(void)done;
		(void)total;
		(void)activity;
	}
};

template <class Board = DefaultBoard>
class UartUpdater
{
public:
	static constexpr uint32_t flashLimit = Board::flashLimit;
	static constexpr uint32_t idleRebootUs = 30u * 1000 * 1000;
	static constexpr uint32_t interByteTimeoutUs = 2000;

	/// Take UART0 on GPIO 0 (TX) and GPIO 1 (RX). Those pins then can't be
	/// used for anything else, such as I2C.
	void Begin()
	{
		uart_init(uart0, ru::kBaud);
		gpio_set_function(1, GPIO_FUNC_UART);
		gpio_pull_up(1); // idle high when nothing is connected

		// TX is connected to the UART only while answering (see Reply)
		gpio_set_function(0, GPIO_FUNC_NULL);
		gpio_pull_up(0);

		pico_unique_board_id_t id;
		pico_get_unique_board_id(&id);
		memcpy(uniqueId, id.id, sizeof(uniqueId));

		// Normal running only ever receives HELLO, CRC and ENTER
		parser.SetBuffer(smallBuf, sizeof(smallBuf));
	}

	/// Serve the Decanter while running normally. Returns only if nothing asked
	/// to update; after an ENTER it never returns (the target reboots).
	void Poll()
	{
		// Let go of the TX pin once the last reply has gone out
		if (txDriving && !(uart0_hw->fr & UART_UARTFR_BUSY_BITS)) DriveTx(false);

		while (RxReady())
		{
			ru::Parser::Event e = parser.Push(RxByte());
			if (e == ru::Parser::BadCrc) Reply(0, ru::StatusBadCrc, 0, 0, nullptr, 0);
			if (e != ru::Parser::Packet) continue;

			const ru::Header &h = parser.header();
			if (h.cmd == ru::CmdHello)
				ReplyHello(h.seq, ru::ModeRunning);
			else if (h.cmd == ru::CmdCrc)
				ReplyCrc(h, parser.payload());
			else if (h.cmd == ru::CmdEnter)
			{
				totalSectors = h.len >= 4 ? ru::Get32(parser.payload()) : 0;
				Reply(h.cmd, ru::StatusOk, h.seq, 0, nullptr, 0);
				EnterUpdateMode();
			}
			else
				Reply(h.cmd, ru::StatusBadMode, h.seq, 0, nullptr, 0);
		}
	}

private:
	ru::Parser parser;
	uint8_t smallBuf[16];
	uint8_t uniqueId[8];
	uint32_t totalSectors = 0;
	uint32_t programmed = 0;

	static inline bool txDriving = false; // TX pin connected to the UART
	static inline bool holdTx = false;    // update mode: keep it connected

	// Connect or release the TX pin (GPIO 0), by register so it is RAM-safe.
	// Released, the pin's output is off and its pull-up holds the line high.
	static inline void RU_RAMFUNC(DriveTx)(bool on)
	{
		if (!on && holdTx) return;
		io_bank0_hw->io[0].ctrl = on ? GPIO_FUNC_UART : GPIO_FUNC_NULL;
		txDriving = on;
	}

	// UART0 registers directly: the SDK's uart functions live in flash
	static inline bool RU_RAMFUNC(RxReady)() {return !(uart0_hw->fr & UART_UARTFR_RXFE_BITS);}
	static inline uint8_t RU_RAMFUNC(RxByte)() {return uint8_t(uart0_hw->dr);}

	static void RU_RAMFUNC(TxBytes)(const uint8_t *p, uint32_t n)
	{
		for (uint32_t i = 0; i < n; i++)
		{
			while (uart0_hw->fr & UART_UARTFR_TXFF_BITS) {}
			uart0_hw->dr = p[i];
		}
	}

	// Streamed straight to the UART with a running CRC: no copy (GCC turns copy
	// loops into memcpy calls, and memcpy is in flash)
	static void RU_RAMFUNC(Reply)(uint8_t cmd, uint8_t status, uint16_t seq, uint32_t offset, const uint8_t *payload, uint32_t len)
	{
		uint8_t hdr[ru::kHeaderSize];
		hdr[0] = ru::kMagic0;
		hdr[1] = ru::kMagic1;
		hdr[2] = cmd;
		hdr[3] = status;
		ru::Put16(hdr + 4, seq);
		ru::Put16(hdr + 6, len);
		ru::Put32(hdr + 8, offset);
		uint32_t crc = ru::Crc32Update(0, hdr, sizeof(hdr));
		crc = ru::Crc32Update(crc, payload, len);
		uint8_t tail[ru::kCrcSize];
		ru::Put32(tail, crc);
		if (!txDriving) DriveTx(true); // released again by Poll once sent
		TxBytes(hdr, sizeof(hdr));
		TxBytes(payload, len);
		TxBytes(tail, sizeof(tail));
	}

	void RU_RAMFUNC(ReplyHello)(uint16_t seq, uint8_t mode)
	{
		ru::HelloInfo info;
		info.protocolVersion = ru::kProtocolVersion;
		info.mode = mode;
		info.board[0] = Board::board0;
		info.board[1] = Board::board1;
		info.flashLimit = flashLimit;
		const volatile uint8_t *id = uniqueId; // volatile: keep this a loop, not a memcpy
		for (int i = 0; i < 8; i++) info.uniqueId[i] = id[i];
		Reply(ru::CmdHello, ru::StatusOk, seq, 0, (const uint8_t *)&info, sizeof(info));
	}

	static void RU_RAMFUNC(ReplyCrc)(const ru::Header &h, const uint8_t *payload)
	{
		uint32_t len = h.len >= 4 ? ru::Get32(payload) : 0;
		if (len > ru::kSectorSize || (h.offset & 3u) || h.offset + len > PICO_FLASH_SIZE_BYTES)
		{
			Reply(h.cmd, ru::StatusBadArgs, h.seq, h.offset, nullptr, 0);
			return;
		}
		// Word reads through the no-allocate XIP window: hits use the cache,
		// misses don't evict the app's code from it
		uint32_t crc = 0;
		const volatile uint32_t *p = (const volatile uint32_t *)(XIP_NOALLOC_BASE + (h.offset & ~3u));
		for (uint32_t i = 0; i < len; i += 4)
		{
			uint8_t w[4];
			ru::Put32(w, p[i / 4]);
			crc = ru::Crc32Update(crc, w, len - i < 4 ? len - i : 4);
		}
		uint8_t out[4];
		ru::Put32(out, crc);
		Reply(h.cmd, ru::StatusOk, h.seq, h.offset, out, sizeof(out));
	}

	// Watchdog reset, from RAM: the SDK's watchdog_reboot is in flash, which
	// by now may hold a different firmware
	static void RU_RAMFUNC(Reboot)()
	{
		while (uart0_hw->fr & UART_UARTFR_BUSY_BITS) {}
		watchdog_hw->scratch[4] = 0; // no bootrom "jump to address" request
		hw_set_bits(&psm_hw->wdsel, PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));
		watchdog_hw->ctrl = WATCHDOG_CTRL_TRIGGER_BITS;
		while (true) {}
	}

	void EnterUpdateMode()
	{
		while (uart0_hw->fr & UART_UARTFR_BUSY_BITS) {} // the ENTER reply goes out first
		holdTx = true; // a Decanter is listening from now on
		DriveTx(true);

		// Stop everything that might run code or touch flash: the other core,
		// every interrupt, all DMA and both PIOs
		bool onCore0 = get_core_num() == 0;
		if (onCore0) multicore_reset_core1();
		else hw_set_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC0_BITS);
		save_and_disable_interrupts();
		dma_hw->abort = (1u << NUM_DMA_CHANNELS) - 1;
		while (dma_hw->abort) {}
		pio0_hw->ctrl = 0;
		pio1_hw->ctrl = 0;

		// Nothing else runs now; flash is still intact
		Board::Quiesce();

		// Packets of up to 4kB from now on, in the stopped core's stack bank
		// (exactly 4kB), so no RAM is set aside for this while the app runs
		parser.SetBuffer((uint8_t *)(onCore0 ? SRAM4_BASE : SRAM5_BASE), ru::kMaxPayload);

		UpdateLoop();
	}

	void RU_RAMFUNC(UpdateLoop)()
	{
		parser.Reset();
		uint32_t lastPacket = timer_hw->timerawl;
		bool activity = false;
		Board::ShowProgress(0, totalSectors, false);

		uint32_t lastByte = lastPacket;
		while (true)
		{
			uint32_t now = timer_hw->timerawl;
			if (now - lastPacket > idleRebootUs) Reboot();
			if (!RxReady())
			{
				// A pause part way through a packet means bytes were lost: drop
				// it, so the Decanter's resend isn't swallowed as its remainder
				if (parser.Busy() && now - lastByte > interByteTimeoutUs) parser.Reset();
				continue;
			}
			lastByte = now;

			ru::Parser::Event e = parser.Push(RxByte());
			if (e == ru::Parser::BadCrc) Reply(0, ru::StatusBadCrc, 0, 0, nullptr, 0);
			if (e != ru::Parser::Packet) continue;

			lastPacket = timer_hw->timerawl;
			activity = !activity;
			Handle(parser.header(), parser.payload());
			Board::ShowProgress(programmed, totalSectors, activity);
		}
	}

	void RU_RAMFUNC(Handle)(const ru::Header &h, const uint8_t *payload)
	{
		bool aligned = (h.offset & (ru::kSectorSize - 1)) == 0;
		bool writable = h.offset < flashLimit && h.offset + ru::kSectorSize <= flashLimit;
		uint8_t status = ru::StatusOk;

		if (h.cmd == ru::CmdHello)
		{
			ReplyHello(h.seq, ru::ModeUpdate);
			return;
		}
		else if (h.cmd == ru::CmdEnter)
		{
		}
		else if (h.cmd == ru::CmdErase)
		{
			if (!aligned) status = ru::StatusBadArgs;
			else if (!writable) status = ru::StatusProtected;
			else flash_range_erase(h.offset, ru::kSectorSize);
		}
		else if (h.cmd == ru::CmdProgram)
		{
			if (!aligned || h.len != ru::kSectorSize) status = ru::StatusBadArgs;
			else if (!writable) status = ru::StatusProtected;
			else
			{
				flash_range_program(h.offset, payload, ru::kSectorSize);
				programmed++;
			}
		}
		else if (h.cmd == ru::CmdRead)
		{
			uint32_t len = h.len >= 4 ? ru::Get32(payload) : 0;
			if (len > ru::kMaxPayload || h.offset + len > PICO_FLASH_SIZE_BYTES)
				status = ru::StatusBadArgs;
			else
			{
				// Uncached, so it is the flash itself, not a stale cache line
				Reply(h.cmd, ru::StatusOk, h.seq, h.offset, (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + h.offset), len);
				return;
			}
		}
		else if (h.cmd == ru::CmdCrc)
		{
			ReplyCrc(h, payload);
			return;
		}
		else if (h.cmd == ru::CmdReboot)
		{
			Reply(h.cmd, ru::StatusOk, h.seq, 0, nullptr, 0);
			Reboot();
		}
		else
		{
			status = ru::StatusUnknown;
		}
		Reply(h.cmd, status, h.seq, h.offset, nullptr, 0);
	}
};

} // namespace decanter
