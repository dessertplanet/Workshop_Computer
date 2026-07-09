# Warps (Workshop System Computer port) — WIP

A port of Mutable Instruments' [Warps](https://mutable-instruments.net/modules/warps/)
meta-modulator (the community **parasites** firmware) to the Music Thing
Workshop System **Computer** (RP2040 / Pico SDK, via `ComputerCard`).

> **Status: work in progress.** The DSP is vendored and wired to the hardware,
> but real-time performance on the FPU-less RP2040 is unproven for the heavier
> algorithms. See [Performance reality](#performance-reality) below.

## What's here

This card is **self-contained** — it does not depend on anything outside its own
folder. Only the DSP subset the Warps `Modulator` actually needs is vendored:

```
releases/97_warps/
├── main.cpp              # hardware glue: ComputerCard I/O <-> warps::Modulator
├── CMakeLists.txt        # builds main.cpp + the vendored .cc files
├── ComputerCard.h        # the shared Workshop System HAL
├── pico_sdk_import.cmake
├── stmlib/               # minimal Mutable Instruments stmlib subset
│   ├── stmlib.h
│   ├── dsp/{dsp,filter,parameter_interpolator,units,cosine_oscillator}.h
│   └── utils/{random.h,random.cc}
└── warps/                # Warps DSP subset (from the parasites firmware)
    ├── drivers/debug_pin.h   # already has an RP2040 (PICO_ON_DEVICE) path
    ├── dsp/{modulator,oscillator,vocoder,filter_bank}.{h,cc}
    ├── dsp/{limiter,parameters,quadrature_*,sample_rate_*}.h
    └── resources.{h,cc}      # generated lookup tables
```

The original firmware's STM32 CMSIS/USB drivers, linker scripts, Python
resource generators and hardware-design files are **intentionally excluded** —
they play no part in the RP2040 build. The full original tree still lives (for
reference) at
`Demonstrations+HelloWorlds/PicoSDK/ComputerCard/examples/warps/`.

### Port modifications

The vendored DSP is unmodified from upstream **except** where it was already
adapted for RP2040:

- `stmlib/dsp/dsp.h` — `Clip16`/`ClipU16`/`Sqrt` use portable C on ARMv6-M
  (`__ARM_ARCH_6M__`) instead of the STM32 `ssat`/`usat`/`vsqrt` inline asm.
- `warps/drivers/debug_pin.h` — has a `PICO_ON_DEVICE` GPIO path.

If you touch the vendored files, keep the changes minimal and note them here so
a future re-sync against upstream stays tractable.

## Build, flash & iterate (RPi Debug Probe)

This repo builds inside the VS Code **Dev Container**, with **OpenOCD running on
the host** talking to the Raspberry Pi Debug Probe (CMSIS-DAP); the container
flashes over GDB at `host.docker.internal:3333`.

**One-time per session — on the Windows host:**

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_openocd_host.ps1
```

**Then, inside the Dev Container**, from this folder:

```sh
cd releases/97_warps
make            # configure + build -> ./build/, stage warps.uf2 -> ./UF2/
make flash      # load build/warps.elf onto the chip via host OpenOCD
```

Re-run `make flash` after each edit. Or press **F5** in VS Code
("ComputerCard: attach (OpenOCD)") to build + flash + attach the debugger and
break at `main` — pick this folder when prompted for the card.

## Performance reality

Warps was written for an **STM32F4 with a hardware FPU at 168 MHz**, processing
at 96 kHz. The Workshop Computer's **RP2040 is a Cortex-M0+ with no FPU**, so
every `float` operation is software-emulated. `main.cpp` overclocks to 225 MHz,
but that does not close the gap for the float-heavy modes.

Practical expectations:

- **Likely OK:** the cheap per-sample cross-mod algorithms (crossfade, analog /
  digital ring modulation, wavefolder, XOR, comparator).
- **Likely too slow at 48 kHz:** vocoder (filter bank), frequency shifter
  (quadrature transform), delay/doppler.

Suggested path forward:

1. Get the build green in the container, confirm audio passes through.
2. Profile a single block (`ProcessMeta`) — measure microseconds per `kBlock`
   samples against the 48 kHz budget (~667 µs for 32 samples).
3. If over budget, restrict `feature_mode` to the cheap algorithms, and/or port
   the hot paths to fixed-point (the sibling `fold_int` example is a reference
   for the integer-DSP approach).

## Current wiring

| Control        | Mapped to                                              |
|----------------|--------------------------------------------------------|
| Audio In 1     | Carrier (Modulator ch 0)                               |
| Audio In 2     | Modulator (Modulator ch 1)                             |
| Audio Out 1    | Main output (cross-modulated)                          |
| Audio Out 2    | Aux output (carrier + modulator)                       |
| Main knob      | `modulation_algorithm` (0..8)                          |
| Knob X         | `modulation_parameter` (timbre)                        |
| Knob Y         | `channel_drive[0/1]` (input drive)                     |
| Switch         | unused (reserved for `feature_mode` selection)         |

`feature_mode` is fixed to `FEATURE_MODE_META` (classic Warps) for now.
