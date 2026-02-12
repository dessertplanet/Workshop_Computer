# Workshop System Computer - USB Audio & MIDI Firmware

This firmware turns the Workshop System Computer into a class-compliant USB Composite Device providing:
- **6-Channel USB Audio Interface** (Input & Output)
- **USB MIDI Interface**
- **CV/Gate Integration** via Hardware I/O

## Features

### Mode 1: Normal (Switch Middle)
- **6-Channel Audio I/O**: Direct mapping of the 6 hardware channels to USB Audio channels 1-6.

### Mode 2: Alt Mode (Switch Up)
- **Audio L/R**: Stereo Passthrough on USB Channels 5/6.
- **CV 1**: 1V/Oct Pitch Interface (MIDI Note <-> CV).
- **CV 2**: CC Interface (MIDI CC <-> CV).
- **Pulse 1**: Gate Interface (Note On/Off <-> Pulse).
- **Pulse 2**: Clock/Run Interface.

### Mode 3: Audio Only (Switch Down)
- Turns off all Midi functionality, leaving only the configured USB Audio channels.

## Configuration & Defaults

The firmware ships with the following **Default Settings** (customizable via the Web Interface):
- **Sample Rate**: 44.1 kHz
- **Channel Count**: 4 Channels (Audio 1/2 + CV 1/2) enabled for both Input and Output.
- **Input Mapping**:
  - Audio 1/2: Audio Stream
  - CV 1: Pitch (Ch 1)
  - CV 2: CC 4 (Ch 1)
  - Pulse 1: Gate (for CV 1)
- **Output Mapping**: 
  - Audio 1/2: Audio Stream
  - CV 1: Pitch (Ch 1)
  - CV 2: CC 4 (Ch 1)
  - Pulse 1: Gate
  - Pulse 2: Clock
- **Knobs**:
  - Main: CC 1 (Ch 1)
  - X: CC 2 (Ch 1)
  - Y: CC 3 (Ch 1)

**Web Interface**:
All settings (Sample Rate, Channel Count, CV/Pulse Modes) can be adjusted using the **Workshop System Web Tools**. The configuration can be saved to the device's flash memory.

> [!WARNING]
> **USB 1.1 Bandwidth & Stability**
>
> This device operates as a Full Speed USB 1.1 device. Bandwidth is extremely limited, especially when combined with MIDI traffic. Performance varies significantly by Operating System and Host Controller.
>
> **Tested Configurations (Testing Required):**
> - **macOS**: 6 Channels @ 48 kHz .
> - **Windows**: Limited bandwidth. Usually requires reducing to **6 Channels @ 24 kHz** or **4 Channels @ 44.1 kHz**.
> - **Linux**: 2 channel audio works up to 48kHz. 4 channel audio works up to 24kHz
>
> *Please test different configurations/ Audio Drivers for stability on your specific system.*


- Raspberry Pi Pico SDK
- CMake > 3.13

> [!IMPORTANT]
> **You MUST update the TinyUSB library within your Pico SDK to the latest version.**  
> The default version included with older SDK releases may contain bugs affecting Audio Class devices.
>
> To update:
> ```bash
> cd $PICO_SDK_PATH/lib/tinyusb
> git checkout master
> git pull
> ```

## Setup & Building

1. **Configure Environment**:
   Run the provided helper script to add SDK paths to your shell configuration (Mac/Linux):
   ```bash
   ./configure_shell.sh
   source ~/.zshrc  # Or restart terminal
   ```

2. **Build**:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

3. **Flash**:
   Hold the BOOTSEL button on your Pico/Computer board, plug it in, and copy the generated `usb_bridge.uf2` to the drive.


Created for the Music Thing Modular Workshop System by Vincent Maurer (https://github.com/vincent-maurer/) with assistance from Google Gemini.

Thank you to everyone on the Workshop System Discord server, that helped testing and especially to Chris Johnson (https://github.com/chrisgjohnson) for the initial USB-Audio project, the ComputerCard library and the great support.
