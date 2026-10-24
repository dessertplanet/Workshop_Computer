# Decanter (card 750)

**Work in progress.** A Workshop Computer card that carries a firmware image (UF2) and flashes it onto a second RP2040-based module without a desktop/laptop computer

Status:

- **Done:** storing a payload. The card appears as a USB drive; copy a `.uf2` onto it.
- **Done:** flashing the payload onto a target RP2040 in BOOTSEL over USB (PICOBOOT), automatically when the target appears. Tested with the Radio Music Mk2 factory firmware.
- **Built, being tested:** flashing a *running* target over the UART pads, when its firmware includes the [rm2-diy](https://github.com/dessertplanet/rm2-diy) UART updater (as the `sd_stream` example does).

## Use

The Computer's USB-C chip picks the USB role at power-up and keeps it until the next power cycle. What the Computer is connected to when it powers up sets the mode.

### Load a payload

1. Connect the Computer to a computer and power up. A drive called **DECANTER** appears; the top right LED lights when it is mounted.
2. Copy an RP2040 `.uf2` file onto it. The middle and bottom LEDs fill up as a progress bar.
3. The drive disconnects and comes back (your computer may say it wasn't ejected properly; that's expected). The top left LED lights: the payload is stored. `INFO.TXT` on the drive describes it.

A new file replaces the stored payload. The old one is wiped as soon as the new copy starts, so a failed or interrupted copy leaves no payload rather than a broken one.

Files are refused (the four middle and bottom LEDs blink fast, and `INFO.TXT` says why) if they:

- write at or above target flash offset `0x1FF000`, where the Radio Music Mk2 keeps its factory 1V/oct calibration;
- write outside target flash;
- are too large for the program card (it holds 447 sectors, 1788 kB, on a 2 MB card).

Blocks for other chips in a universal UF2 (RP2350) are ignored.

### Flash a target

1. Power down. Connect the Computer to the target with a USB-C cable.
2. Power up with the target in BOOTSEL. Radio Music Mk2: hold Reset while powering up, then push both knobs.
3. The Computer flashes the payload as soon as the target appears: the middle and bottom LEDs fill as a progress bar, every sector is read back and checked, and the target reboots into its new firmware. All four lit means done.

Any RP2040 in BOOTSEL plugged into the Computer in this mode is flashed, so only connect the target you mean to flash.

If flashing fails, the four LEDs blink fast and the target is left in BOOTSEL (target sector 0, which holds boot2, is erased first and written last, so a half-flashed target never boots). Power-cycle to try again. They also blink if there's no payload stored, or if the target comes straight back in BOOTSEL after being flashed, which means the payload didn't boot. In that case the card doesn't flash the same target again if it reappears within 10 seconds, so a payload that can't boot doesn't get flashed over and over.

### Flash a running target over UART

For a target whose firmware includes the rm2-diy UART updater (`RM2UartUpdate.h`), such as rm2-diy's `sd_stream`.

1. Wire the Computer's UART pads to the target's rear header straight through: **SDA to SDA, SCL to SCL, GND to GND**. (The Computer runs its UART on PIO with TX and RX swapped, so no crossover.)
2. Power up. That's all: the Computer looks for a target all the time, in either USB mode.

When the target answers, the Computer asks it for a checksum of each payload sector, which the target answers without stopping. If it already has the payload, nothing happens and the four LEDs light: up to date. If not, the target stops, its top LEDs show progress, the Computer's middle and bottom LEDs fill as a progress bar, every sector is read back and checked, and the target reboots into the new firmware.

A target is checked again when it reconnects or when the payload changes, so it can stay wired. The switch isn't used. While flashing, the USB drive stays mounted but refuses writes.

USB host mode only flashes a target in BOOTSEL. Any other USB device, such as a target running its normal firmware, is ignored, so with both cables connected a running target is flashed over UART.

The link is 921600 baud with CRC-checked packets; damaged packets are resent. The target refuses to erase or write at or above `0x1FF000` itself, as well as the Computer never asking it to.

## LEDs

| LED | Meaning |
|---|---|
| Top left | Payload stored and valid |
| Top right | Device mode: drive mounted by a computer. Host mode: slow blink while waiting, on while a target is connected |
| Middle and bottom | Progress bar (upload, or flashing). All four on: target flashed, or already up to date. All four blinking fast: upload refused, or flashing failed |
| All six blinking | Build error: the firmware overlaps the payload area |

## How it works

- **Payload store** ([src/payload.cpp](src/payload.cpp)): the program card's flash holds the firmware from `0x000000`, a header sector at `0x040000`, and 4 kB slots from `0x041000`. Each 4 kB target sector in the UF2 gets a slot, filled as blocks arrive in any order. When the whole file has arrived, the header (listing which target sector each slot holds, with CRC-32s) is written, and the payload becomes valid.
- **USB drive** ([src/ghostfat.cpp](src/ghostfat.cpp)): a generated FAT16 volume holding only `INFO.TXT`, as in UF2 bootloaders. Every sector written to it goes to the payload store, which keeps the UF2 blocks and ignores everything else.
- **Flashing engine** ([src/flasher.cpp](src/flasher.cpp)): checks the payload CRC, then writes the target sector by sector through a transport, reading each one back to verify. It never writes at or above `0x1FF000`. Target sector 0 is erased first and written last, so an interrupted flash leaves the target in BOOTSEL rather than running half-written firmware.
- **USB transport** ([src/picoboot_transport.cpp](src/picoboot_transport.cpp)): the Computer as USB host talks the RP2040 bootrom's PICOBOOT protocol as picotool does (exclusive access, exit XIP, erase, write, read, reboot), through TinyUSB's endpoint API.
- **UART transport** ([src/uart_transport.cpp](src/uart_transport.cpp), [src/decanter_uart_proto.h](src/decanter_uart_proto.h)): framed packets with CRC-32, one request at a time, retried on timeout or damage. A target's updater includes its own copy of the protocol header, which must stay identical.
- **Target-side updater** ([src/decanter_uart_updater.h](src/decanter_uart_updater.h)): the reference updater for a target's firmware, header-only. Copy it and `decanter_uart_proto.h` into the target's source, call `Begin()` once and `Poll()` from the main loop, and describe the board (protected flash limit, progress LEDs) in a small traits struct. While the target runs, it costs 76 bytes of RAM, about 1.3 kB of RAM-resident code, and one register read per `Poll()`. Decanter itself doesn't build this file; it's here as the definitive copy.
- The firmware runs from RAM (`copy_to_ram`), so the LEDs keep running while flash is written.

## Building

```
make                  # in this folder: builds UF2/decanter.uf2
cd tests/host && make # host tests: payload store, flasher, drive, UART protocol
```

Built with Pico SDK 2.2 and TinyUSB 0.21. The host tests run under AddressSanitizer and UBSan, and check the drive image with `fsck.fat` if it is installed.

## License

MIT
