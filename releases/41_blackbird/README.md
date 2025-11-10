# Blackbird v1.0

A fully monome-crow-compatible program card for the Music Thing Modular Workshop Computer.

This enables you to:
- Live code with the workshop computer connected to a non-workshop computer running [druid](https://monome.org/docs/crow/druid/)
- Connect a [monome norns](https://monome.org/docs/norns/) via USB and run scripts on norns that interact with crow natively (try [awake](https://llllllll.co/t/awake/21022), [buoys](https://llllllll.co/t/buoys-v1-2-0/37639), [loom](https://llllllll.co/t/loom/21091) ... many of these also allow you do use a monome grid for WS interactions)
- Connect the workshop to a non-WS computer running [Max MSP or MaxforLive](https://monome.org/docs/crow/max-m4l/) with Ableton and use the monome-built M4L instruments to make the WS interact with Ableton or your own Max creations. 
- Write and upload your own (simple) program cards in the [Lua (5.4.8) language](https://www.lua.org/manual/5.4/). Upload to the WS computer via the "u" command in druid (on a non-WS computer). Uploaded scripts are saved to flash on the physical card itself- So you can write many different blackbird cards and hot-swap!

Blackbird does everything monome crow can do and works with all (I hope!) existing crow scripts. It has also been extended with Blackbird-specific functionality via the `bb` namespace. 

A great place to get started is the [original crow documentation](https://monome.org/docs/crow/) since the examples there work here perfectly according to the hardware mapping below. 

Tested with druid, norns, MAX/MSP, pyserial - works with **ANY** serial host that sends compatible strings.

## Hardware Mapping

Blackbird maps the Workshop Computer's hardware to crow's inputs and outputs as follows:

### Outputs

| Crow Script | Workshop Computer | Type |
|-------------|-------------------|------|
| `output[1]` | CV Out 1 | CV Output (callibrated)|
| `output[2]` | CV Out 2 | CV Output (callibrated)|
| `output[3]` | Audio Out 1 | CV/Audio Output (not callibrated)|
| `output[4]` | Audio Out 2 | CV/Audio Output (not callibrated)|

### Inputs

| Crow Script | Workshop Computer | Type |
|-------------|-------------------|------|
| `input[1]` | CV In 1 | CV Input |
| `input[2]` | CV In 2 | CV Input |

### Blackbird-Specific Hardware (bb namespace)

| Lua API | Workshop Computer | Type |
|---------|-------------------|------|
| `bb.knob.main` | Main Knob | Analog Input |
| `bb.knob.x` | X Knob | Analog Input |
| `bb.knob.y` | Y Knob | Analog Input |
| `bb.switch.position` | 3-Position Switch | Digital Input |
| `bb.audioin[1]` | Audio In L | Audio Input |
| `bb.audioin[2]` | Audio In R | Audio Input |
| `bb.pulsein[1]` | Pulse In 1 | Digital Input |
| `bb.pulsein[2]` | Pulse In 2 | Digital Input |
| `bb.pulseout[1]` | Pulse Out 1 | Digital Output |
| `bb.pulseout[2]` | Pulse Out 2 | Digital Output |

## Other Blackbird-specific goodness

### Noise Generation

**Audio-rate noise action** with `bb.noise()` - generates noise directly in the audio loop for clean, efficient noise generation on any CV/Audio output.

Example:

```lua
output[4]( bb.noise() )

--OR

output[2].action = bb.noise() -- define action
output[2]() -- run the action
```

### Define an 'ASAP' function
`bb.asap` - Run code as fast as possible in the control thread with no detection. Useful for updating parameters smoothly with knobs. Use carefully as if you do much in here then performance will be impacted.

Only runs if it exists. To clear/stop this function use `bb.asap = nil`

Example:
```lua
bb.asap = function()
    myParameter = bb.knob.main
end
```

### Choose your priorities (advanced)
`bb.priority()` - Balance accurate timing with accurate output (configures failure mode when overloaded)

## Documentation

- **Crow Documentation**: [https://monome.org/docs/crow/](https://monome.org/docs/crow/)
- **Crow Script Reference**: [https://monome.org/docs/crow/reference/](https://monome.org/docs/crow/reference/)
- **Lua Documentation**: [https://www.lua.org/manual/5.4/](https://www.lua.org/manual/5.4/)
- **Blackbird-Specific Features: supplementary docs**:
  - [Knob & Switch API](docs/KNOB_SWITCH_API.md)
  - [Using Knobs with ASL Dynamics](docs/KNOBS_WITH_ASL.md)
  - [Pulse Input Detection](docs/PULSEIN_DETECTION.md)
  - [Pulse Output Actions](docs/PULSEOUT_ACTIONS.md)

## Credits & Thank yous

Written by Dune Desormeaux / [@dessertplanet](https://github.com/dessertplanet), 2025

Special thanks to:
- **Tom Whitwell** for the Workshop System
- **Chris Johnson** for the ComputerCard framework
- **Brian Crabtree** and **Trent Gill** (monome & Whimsical Raps) for the original crow and the open source crow firmware
- **Zack Scholl** (infinitedigits) for encouragement and proving Lua can work on RP2040 with his midi-to-cv project
- **Ben Regnier** (Q*Ben) for extensive testing and feedback without which this would not have been possible

## License

GPLv3 or later - see [LICENSE](LICENSE.txt) file for details.
