# Monome Grid for Workshop System Computer

Use a [monome grid](https://monome.org/docs/grid/) (or arc) with the
[Music Thing Modular Workshop System](https://www.musicthing.co.uk/workshopsystem/)
Computer card, over USB host.

Compatible with **all monome grid hardware** — modern CDC/RP2040 grids and older
FTDI-based editions (including DIY Neotrellis grids).

## Files

| File | What it does |
|---|---|
| `MonomeGrid.h` | Friendly C++ wrapper — the recommended API |
| `monome_mext.h` | Low-level C header (types, state, raw API) |
| `monome_mext.c` | Mext protocol engine, USB host loop, TinyUSB callbacks |
| `tusb_config.h` | TinyUSB configuration (CDC + FTDI host, DTR/RTS, 115200 8N1; also enables CDC device for USB serial) |
| `main.cpp` | MLRws card firmware (grid-driven sample looper) |
| `CMakeLists.txt` | Build configuration |

## Quick start

### 1. Copy files into your card folder

```
monome_mext.h
monome_mext.c
MonomeGrid.h
tusb_config.h
```

### 2. Update your CMakeLists.txt

Add `monome_mext.c` to your sources and link the required libraries:

```cmake
add_executable(my_card
    main.cpp
    monome_mext.c
)

target_link_libraries(my_card
    pico_stdlib
    pico_multicore
    hardware_adc hardware_clocks hardware_dma hardware_flash
    hardware_gpio hardware_i2c hardware_irq hardware_pwm hardware_spi
    tinyusb_host
    tinyusb_board
)
```

If your `tusb_config.h` also enables device mode (`CFG_TUD_CDC`), add
`tinyusb_device` to `target_link_libraries` as well.

```cmake
```

### 3. Write your card

```cpp
#include "ComputerCard.h"
#include "MonomeGrid.h"

class MyCard : public ComputerCard
{
public:
    MyCard()
    {
        grid.begin();  // initialises mext + launches USB host on core 1
    }

    void ProcessSample() override
    {
        grid.poll();  // drain event queue — call once per sample

        // React to key presses
        if (grid.keyDown()) {
            uint8_t x = grid.lastX();
            uint8_t y = grid.lastY();
            grid.ledToggle(x, y);
        }

        // Gate output when any key is held
        PulseOut1(grid.anyHeld());
    }

private:
    MonomeGrid grid;
};

int main()
{
    MyCard card;
    card.Run();
}
```

### 4. Build and flash

```bash
cd your_card_folder
make build    # or: make flash (if OpenOCD is running)
```

## API reference — MonomeGrid.h (C++)

### Lifecycle

| Method | Description |
|---|---|
| `grid.begin()` | Init + launch USB host on core 1. Call in constructor. |
| `grid.ready()` | `true` once grid dimensions are discovered |
| `grid.connected()` | `true` if a device is physically connected |
| `grid.cols()` | Grid width (0 before discovery) |
| `grid.rows()` | Grid height (0 before discovery) |

### Events

Call `grid.poll()` once at the top of `ProcessSample()`.

| Method | Description |
|---|---|
| `grid.poll()` | Drain event queue. Call once per sample. |
| `grid.keyDown()` | `true` if a key was pressed this sample |
| `grid.keyUp()` | `true` if a key was released this sample |
| `grid.anyHeld()` | `true` if any key is currently held |
| `grid.held(x, y)` | `true` if key (x, y) is currently held |
| `grid.lastX()` | X of the most recent key event |
| `grid.lastY()` | Y of the most recent key event |

### LEDs

LED changes are buffered and automatically flushed to the grid at ~60 fps
by the USB host loop on core 1.

| Method | Description |
|---|---|
| `grid.led(x, y, level)` | Set brightness (0–15) |
| `grid.ledOn(x, y)` | Set to full brightness |
| `grid.ledOff(x, y)` | Turn off |
| `grid.ledToggle(x, y)` | Toggle between off and full |
| `grid.ledSet(x, y, on)` | Set from a bool |
| `grid.ledGet(x, y)` | Read back current level |
| `grid.clear()` | All LEDs off |
| `grid.all(level)` | Set all LEDs to a level |
| `grid.fill(x0, y0, x1, y1, level)` | Fill a rectangle |
| `grid.row(y, level)` | Fill a row |
| `grid.col(x, level)` | Fill a column |
| `grid.intensity(level)` | Set global brightness (0–15) |
| `grid.showHeld(level)` | Light all currently held keys |

### Advanced / direct buffer access

| Method | Description |
|---|---|
| `grid.ledBuffer()` | Raw `uint8_t*` to the 16×16 LED buffer |
| `grid.markDirty()` | Mark all quadrants for refresh (after writing buffer directly) |

## Low-level C API — monome_mext.h

For developers who want full control (e.g., custom core 1 loop, arc support):

```cpp
#include "pico/multicore.h"
#include "bsp/board.h"
#include "tusb.h"

extern "C" {
#include "monome_mext.h"
}

// In your constructor:
mext_init(MEXT_TRANSPORT_HOST, 0);   // host mode, CDC interface 0
multicore_launch_core1(my_core1_func);

// Your core 1 loop:
void my_core1_func() {
    board_init();
    tusb_init();
    while (true) {
        mext_task();          // USB host + mext polling (non-blocking)
        // ... your own core 1 work here ...
    }
}

// In ProcessSample (core 0):
mext_event_t ev;
while (mext_event_pop(&ev)) {
    if (ev.type == MEXT_EVENT_GRID_KEY) {
        // ev.grid.x, ev.grid.y, ev.grid.z (1=down, 0=up)
    }
}

mext_grid_led_set(x, y, level);  // buffer an LED change
// mext_grid_refresh() is called automatically by mext_task()
```

### Key C functions

| Function | Description |
|---|---|
| `mext_init(transport, cdc_itf)` | Initialise state (`MEXT_TRANSPORT_HOST` or `_DEVICE`, CDC interface index) |
| `mext_task()` | Poll USB + mext (call from core 1 loop) |
| `mext_grid_ready()` | Discovery complete? |
| `mext_event_pop(&ev)` | Pop next event (returns `false` if empty) |
| `mext_grid_led_set(x, y, level)` | Set LED (0–15) |
| `mext_grid_led_all(level)` | Set all LEDs |
| `mext_grid_led_intensity(level)` | Global brightness |
| `mext_grid_refresh()` | Flush dirty quads (auto-called by `mext_task`) |
| `mext_grid_frame_submit(levels)` | Submit a full 16×16 frame (double-buffered); returns `false` if busy |
| `mext_grid_all_off()` | Send LED_ALL_OFF command |
| `mext_send_discovery()` | Re-send system queries |

### Event types

```c
MEXT_EVENT_GRID_KEY   // ev.grid.x, ev.grid.y, ev.grid.z
MEXT_EVENT_ENC_DELTA  // ev.enc.n, ev.enc.delta  (arc)
MEXT_EVENT_ENC_KEY    // ev.enc_key.n, ev.enc_key.z  (arc)
```

## Architecture

```
Core 0                          Core 1
──────                          ──────
ComputerCard::Run()             board_init() + tusb_init()
  └─ ProcessSample() @ 48kHz   └─ while(true) { mext_task(); }
       │                              │
       ├─ grid.poll()                 ├─ tuh_task()       ← USB host stack
       ├─ grid.keyDown() ...          ├─ tuh_cdc_read()   ← serial RX → parser
       ├─ grid.led(x,y,lev)          ├─ discovery retry   ← if grid_x == 0
       └─ CVOut / PulseOut            └─ mext_grid_refresh() ← LED TX @ ~60fps
                    │                           │
                    └── event queue ────────────┘
                        (lock-free SPSC ring)
```

## Hardware compatibility

| Grid type | USB chip | TinyUSB driver | Status |
|---|---|---|---|
| Grid (2024+) | RP2040 native | `CFG_TUH_CDC` | ✓ |
| Grid (2021–2023) | FTDI FT232R | `CFG_TUH_CDC_FTDI` | ✓ |
| Grid (older) | FTDI FT230X | `CFG_TUH_CDC_FTDI` | ✓ |
| Neotrellis DIY | FTDI | `CFG_TUH_CDC_FTDI` | ✓ |
| Arc | Same as above | Same as above | Partial (events only) |

## Protocol notes

The mext serial protocol is used by all monome devices. Key implementation
details (learned from [dessertplanet/viii](https://github.com/dessertplanet/viii)
and [monome/ansible](https://github.com/monome/ansible)):

- **LED commands are padded to 64 bytes** with `0xFF` for USB bulk packet alignment.
  This is essential for reliable operation across all hardware versions.
- **Discovery queries are sent unpadded** — FTDI grid firmware doesn't handle
  63 bytes of `0xFF` padding after a system query well.
- **DTR + RTS must be asserted** during USB enumeration. FTDI chips gate serial
  data through DTR — without it, the chip enumerates but won't pass bytes.
- **Line coding is set to 115200 8N1** automatically via
  `CFG_TUH_CDC_LINE_CODING_ON_ENUM`.
- The RX parser skips `0xFF` bytes between messages (standard mext behaviour).

## Credits

- Protocol implementation modelled on [dessertplanet/viii](https://github.com/dessertplanet/viii)
  and [monome/ansible](https://github.com/monome/ansible)
- [monome](https://monome.org/) for the grid hardware and open mext protocol
- [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer) by Tom Whitwell
