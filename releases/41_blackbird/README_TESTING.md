# Blackbird Crow Emulator - Testing Guide

## Current Implementation Status

This is the first iteration of the Blackbird Crow emulator, implementing basic crow communication protocol.

### Implemented Features

- ✅ Basic crow command parsing (`^^v`, `^^i`, `^^p`, etc.)
- ✅ Multi-core architecture (Core 0: audio processing, Core 1: USB communication)
- ✅ Proper crow-style line endings (`\n\r`)
- ✅ Audio passthrough (Workshop inputs → Workshop outputs)
- ✅ CV monitoring and periodic debug output

### Supported Commands

| Command | Response | Description |
|---------|----------|-------------|
| `^^v` | `^^version('blackbird-0.1')` | Version information |
| `^^i` | `^^identity('0x[unique_id]')` | Hardware identity |
| `^^p` | `-- no script loaded --` | Print current script |
| `^^r` | `restarting...` | Restart (placeholder) |
| `^^k` | `lua killed` | Kill Lua (placeholder) |
| `^^b` | `entering bootloader mode` | Boot/bootloader mode (placeholder) |
| `^^s` | `script upload started` | Start script upload (placeholder) |
| `^^e` | `script uploaded` | End upload to RAM (placeholder) |
| `^^w` | `script saved to flash` | Flash upload/save (placeholder) |
| `^^c` | `flash cleared` | Clear flash (placeholder) |
| `^^f` or `^^F` | `loading first.lua` | Load First.lua script (placeholder) |

## Testing Instructions

### Hardware Setup

1. Flash `UF2/blackbird.uf2` to your Workshop Computer
2. Connect USB cable to computer
3. Audio inputs/outputs will pass through automatically

### Software Testing

1. **Serial Terminal Testing**
   ```bash
   # Connect to the USB serial port (usually /dev/ttyACM0 on Linux, /dev/cu.usbmodem* on macOS)
   # Baud rate: 115200
   screen /dev/cu.usbmodem* 115200
   ```

2. **Send Test Commands**
   ```
   ^^v    # Should respond: ^^version('blackbird-0.1')
   ^^i    # Should respond: ^^identity('0x[16-digit-hex]')
   ^^p    # Should respond: -- no script loaded --
   ```

3. **Verify Line Endings**
   - Responses should have crow-style `\n\r` (LF+CR) line endings
   - This is critical for compatibility with druid and other crow tools

### Expected Behavior

- **On startup**: "Blackbird Crow Emulator v0.1" message
- **Every 5 seconds**: CV debugging output showing current CV1/CV2 values
- **Audio**: Direct passthrough from inputs to outputs
- **Commands**: Immediate responses with proper formatting

### Debugging

- CV values are displayed every 5 seconds for monitoring
- Audio processing runs at 48kHz (Workshop standard)
- USB communication is handled on separate core to avoid audio dropouts

## Next Steps

This basic implementation provides the foundation for:

1. **Hardware abstraction layer** - voltage scaling, input/output mapping
2. **Core crow features** - slopes, basic Lua integration
3. **Advanced features** - full crow compatibility

## Development Notes

- Built with Pico SDK 2.1.1
- Uses stdio USB (not TinyUSB CDC) for simplicity
- CMake configured with Ninja build system
- Auto-copies UF2 to UF2/ directory on build
