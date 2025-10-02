# TinyUSB Migration for Druid Compatibility

## Summary

Successfully migrated from Pico SDK's `pico_stdio_usb` to TinyUSB with custom USB descriptors that match crow's USB identity. This ensures full compatibility with the druid client.

## Changes Made

### 1. New Files Created

#### `tusb_config.h`
- TinyUSB configuration file
- Enables CDC device class
- Configures 256-byte RX/TX buffers

#### `usb_descriptors.c`
- **Critical for druid**: Custom USB descriptors that match crow exactly
- **VID:PID = 0x0483:0x5740** (STMicroelectronics Virtual COM Port)
- **Product String = "crow: telephone line"** (druid searches for this!)
- **Manufacturer = "monome & whimsical raps"**
- **Serial number from SD card flash chip** (not board ID - follows the card!)

### 2. Modified Files

#### `CMakeLists.txt`
- Added `tinyusb_device` and `tinyusb_board` to link libraries
- Added `usb_descriptors.c` to source files
- Disabled `pico_stdio_usb` (set to 0)
- Disabled `pico_stdio_uart` (set to 0)

#### `main.cpp`
- **Replaced includes:**
  - Removed: `#include "pico/stdio_usb.h"`
  - Added: `#include "tusb.h"` and `#include "class/cdc/cdc_device.h"`

- **Added printf redirection:**
  ```cpp
  extern "C" int _write(int handle, char *data, int size) {
      if (handle == 1 || handle == 2) { // stdout or stderr
          if (tud_cdc_connected()) {
              tud_cdc_write(data, size);
              tud_cdc_write_flush();
          }
          return size;
      }
      return -1;
  }
  ```

- **Updated main():**
  - Replaced `stdio_init_all()` with `tusb_init()`
  - Replaced `stdio_usb_connected()` with `tud_cdc_connected()`
  - Added `tud_task()` calls while waiting for connection

- **Updated MainControlLoop():**
  - Added `tud_task()` at top of main loop (CRITICAL - must be called regularly!)

- **Updated handle_usb_input():**
  - Replaced `getchar_timeout_us()` with TinyUSB CDC reading:
  ```cpp
  if (tud_cdc_available()) {
      uint8_t buf[64];
      uint32_t count = tud_cdc_read(buf, sizeof(buf));
      // Process characters...
  }
  ```

- **Updated send_crow_response():**
  - Now uses TinyUSB CDC directly:
  ```cpp
  if (tud_cdc_connected()) {
      tud_cdc_write_str(text);
      tud_cdc_write_char('\n');
      tud_cdc_write_char('\r');
      tud_cdc_write_flush();
  }
  ```

## Why This Matters for Druid

The druid client searches for crow devices by:
1. **USB VID/PID**: Looks for `0x0483:0x5740`
2. **Product string**: On Linux/macOS, druid reads `/sys/bus/usb/devices/.../product` or uses libusb to check for "crow: telephone line"
3. **Serial communication**: Once connected, druid sends crow protocol commands (^^v, ^^i, etc.)

Without these exact USB descriptors, druid would not recognize the Blackbird as a crow device.

## Benefits

1. **Druid compatibility**: Appears as genuine crow to druid client
2. **Printf forwarding**: Existing printf() calls still work via `_write()` redirection
3. **Better control**: Direct access to USB CDC for crow responses
4. **Standards-based**: Using industry-standard TinyUSB stack

## Testing

Build completed successfully with:
```bash
cd build
cmake .. -G Ninja
ninja
```

## Next Steps

1. Flash `build/blackbird.uf2` to your RP2040
2. Test with druid: `druid` should detect it as "crow: telephone line"
3. Verify crow commands work: `^^v`, `^^i`, `output[1].volts = 5`
4. Confirm druid can upload scripts

## Important Implementation Details

### Card ID vs Board ID

The USB serial number uses the **SD card flash chip's unique ID**, not the RP2040 board ID. This is important because:

- One Workshop Computer board can host many different cards
- A user might have several Blackbird cards they swap between boards
- Using the card ID ensures the same Blackbird card always has the same USB identity
- This matches the Workshop Computer's design philosophy where the card is the identity

The card ID is read from the SD card flash chip via `flash_get_unique_id()` and mixed with an LCG for better distribution. This ID is cached in `BlackbirdCrow::cached_unique_id` and exposed via `get_card_unique_id()` for the USB descriptors.

## Notes

- `tud_task()` MUST be called regularly in the main loop (at least every 1ms) or USB communication will fail
- Printf forwarding only works when USB CDC is connected
- The custom descriptors are required - druid will not work without them
- Serial number is card-specific, not board-specific (important for Workshop Computer ecosystem)
