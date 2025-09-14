# Workshop Computer Crow Emulator - Development Context

## Project Overview
Building a Program Card for the Workshop Computer that emulates the monome crow module using the ComputerCard framework. The goal is drop-in compatibility with existing crow scripts and norns integration.

## Hardware Mapping
- **Workshop Audio In 1/2** → **Crow Input 1/2** (AudioIn1(), AudioIn2())
- **Workshop Audio Out 1/2** → **Crow Output 1/2** (AudioOut1(), AudioOut2())  
- **Workshop CV Out 1/2** → **Crow Output 3/4** (CVOut1(), CVOut2())
- **Workshop Knobs/Switch** → Crow parameter controls
- **Workshop LEDs** → Crow status indicators
- **Workshop Pulse In/Out** → Additional crow functionality

## Architecture Overview

### Core Components
1. **CrowEmulator** - Main ComputerCard subclass
2. **Crow USB Communication** - USB CDC serial interface (like real crow)
3. **Lua Environment** - Full crow lua interpreter and libraries
4. **Hardware Abstraction** - Maps ComputerCard hardware to crow expectations
5. **Event System** - Processes crow events at 48kHz audio rate
6. **Command Parser** - Handles crow system commands (^^b, ^^s, etc.)

### Key Technical Details
- **Sample Rate**: 48kHz (ComputerCard ProcessSample callback)
- **USB Protocol**: CDC serial device (appears as /dev/ttyACM0 on Linux)
- **Lua Memory**: Standard implementation, optimize if needed
- **Timing**: Crow event system integrated with 48kHz audio processing
- **I²C**: Port crow's ii system (may not be used initially)

## Implementation Phases

### Phase 1: Core Integration & USB Communication ✓ NEXT
**Goal**: Basic USB serial communication with crow command parsing
- Create CrowEmulator ComputerCard subclass
- Implement USB CDC serial communication (building on existing main.cpp pattern)
- Port crow's command parser (^^b, ^^s, ^^r, etc.)
- Basic REPL structure (receive/echo commands)
- Test with simple "hi from crow!" message

**Key Files**:
- `main.cpp` - Modified to instantiate CrowEmulator
- `crow_emulator.h/cpp` - Main emulator class
- `crow_usb.h/cpp` - USB communication layer

**Testing**: Verify USB enumeration, command recognition, basic serial communication

### Phase 2: Lua Environment Setup
**Goal**: Working lua interpreter with basic crow libraries
- Integrate lua interpreter from crow submodule
- Port essential lua libraries (lualink, l_bootstrap, l_crowlib)
- Basic lua REPL functionality
- Simple hardware access (voltage output)

**Key Files**:
- `crow_lua.h/cpp` - Lua integration layer
- Port from `submodules/crow/lib/lualink.c`
- Port from `submodules/crow/lib/l_*.c`

**Testing**: Execute simple lua commands, basic hardware control

### Phase 3: Hardware Abstraction Layer  
**Goal**: Full hardware mapping between ComputerCard and crow expectations
- Input/Output mapping functions
- Voltage scaling and calibration
- Timing integration with ProcessSample
- Event system integration

**Key Files**:
- `crow_hardware.h/cpp` - Hardware abstraction layer
- Port crow detection, slopes, shapes libraries

**Testing**: Verify I/O mapping, voltage accuracy, timing precision

### Phase 4: System Integration
**Goal**: Complete crow functionality (metros, clocks, envelopes, etc.)
- Metro and clock systems
- Slopes and shapes (envelopes, LFOs)
- ASL (Attack/Sustain/Release) system  
- CASL (sequencing)
- Detection system

**Key Files**:
- Port libraries from `submodules/crow/lib/`
- `crow_events.h/cpp` - Event processing system

**Testing**: Complex crow scripts, timing accuracy, envelope generation

### Phase 5: USB & Communication Polish
**Goal**: Full norns compatibility and crow protocol compliance
- USB descriptor matching real crow
- Complete command set implementation
- Error handling and lua error reporting
- Status LED integration
- Streaming output optimization

**Testing**: Norns integration, existing crow script compatibility

### Phase 6: Testing & Validation
**Goal**: Production-ready crow emulator
- Comprehensive testing with real crow scripts
- Performance optimization
- Memory usage optimization
- Documentation

## Key Code Patterns

### ComputerCard Integration
```cpp
class CrowEmulator : public ComputerCard {
    void ProcessSample() override {
        // 48kHz callback - process crow events here
        crow_process_events();
        crow_update_outputs();
    }
};
```

### USB Communication Pattern (from existing main.cpp)
```cpp
// Multicore: Core 1 handles USB, Core 0 handles audio
static void core1() {
    while(1) {
        // USB CDC communication
        // Command parsing
        // Lua REPL
    }
}
```

### Hardware Abstraction
```cpp
// Map crow hardware calls to ComputerCard
void crow_output_set(int channel, float volts) {
    if(channel <= 2) {
        AudioOut(channel-1, volts_to_audio_samples(volts));
    } else {
        CVOut(channel-3, volts_to_cv_dac(volts));
    }
}
```

## Crow Command Reference
- `^^b` - Enter bootloader
- `^^s` - Start upload
- `^^e` - End upload  
- `^^w` - Flash upload
- `^^r` - Restart
- `^^p` - Print script
- `^^v` - Version
- `^^i` - Identity
- `^^k` - Kill lua
- `^^c` - Clear flash
- `^^f` - Load first script

## File Structure
```
/
├── main.cpp (modified)
├── ComputerCard.h (existing)
├── context.md (this file)
├── crow_emulator.h/cpp
├── crow_usb.h/cpp
├── crow_lua.h/cpp  
├── crow_hardware.h/cpp
├── crow_events.h/cpp
└── submodules/
    ├── crow/ (existing)
    └── littlefs/ (existing, may not be needed initially)
```

## Dependencies
- Pico SDK (stdio, multicore, USB CDC)
- Crow submodule (lua interpreter, libraries)
- ComputerCard framework
- Standard C libraries

## Memory Considerations
- Pico: 264KB RAM, 2MB Flash
- Lua can be memory intensive
- Crow libraries add significant code size
- May need to optimize lua memory allocation
- Consider removing unused crow features

## Testing Hardware Requirements
- Workshop Computer with ComputerCard
- USB connection to computer/norns
- Test cables for I/O verification
- Oscilloscope/multimeter for voltage verification

## Development Notes
- Start simple, build incrementally
- Maintain compatibility with existing crow scripts
- Reuse crow code where possible
- Adapt STM32-specific code to Pico SDK
- Test each phase thoroughly before proceeding

## Next Steps (Phase 1)
1. Create basic CrowEmulator class structure
2. Implement USB CDC communication
3. Add crow command parser
4. Test basic serial communication
5. Add simple "hi from crow!" response

This context document should be updated as development progresses and new insights emerge.
