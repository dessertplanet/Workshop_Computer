# Turing Machine Program Card V1.5  

Read these notes alongside the [previous version instructions](https://www.musicthing.co.uk/Computer_Program_Cards/#03-turing-machine)

## What's new
- **Clock In**: 
  - Send a clock to Pulse 1 and the tap tempo is replaced with the clock in. 
  - The clock _Diviply_ function that drives Channel 2 will be driven by the external input clock, so can be used as a standalone clock divide/multiply. It's fairly simple - if the external clock changes speed, the diviply clock waits for a second pulse before changing speed itself. I found this more musical/pleasingly weird than attempting to average clock speed changes. 
  - This clock input works into high audio rate, turning the Turing Machine into a random wavetable oscillator. 
  - A clock into Pulse 2 overwrites the diviply clock, so you have two independently clocked Turing Machines. In this case Pulse Out 2 simply mirrors Pulse In 2. 
- **New Editor**: 
  - The web editor has been redesigned to emphasise the two presets accessed with the toggle switch. It now connects to the Workshop Computer using MIDI sysex, which seems more reliable and easier, once MIDI communication has been permitted. Once again, Chrome is the only browser that has been tested, although others may work (Mac Safari does not work).
  - The [new web editor is here]([https://www.musicthing.co.uk/web_config/turing2.html](https://tomwhitwell.github.io/Turing_Machine_Workshop_Computer/)) or if you access [the old editor](https://www.musicthing.co.uk/web_config/turing.html) it should redirect automatically.  
  - What can you change in the editor? 
    - Scale: A choice of 7 scales / modes / not sure what they're techincally called
    - Octave range: 1 - 4 Octaves
    - Note length: This controls the pulses from Pulse 1 & 2 when the relevant notes start. Blip is  1% of the note length, or 2ms. There are two variable lengths, short and long, which are driven by their own Turing Machines, so they lock and randomise with the notes. 
    - Loop Length: Allows you to set Channel 2 as one step shorter, for uneven patterns. 
    - Pulse Mode 1 & 2: Clock outputs every clock pulse, Turing outputs on a clock pulse where bit 1 of the Turing machine sequence is 1. This is more or less the same as the 'Pulse' output on a hardware Turing Machine. 
    - Audio/CV Output Range: Sets the ranges for the top two outputs. 

- **New CV inputs**: 
  - CV In 1 = Diviply 
    - Accepts positive and negative values to speed up or slow down the divide / multiply rate on Channel 2. Try patching this to the CV/Audio outputs to get complicated rhythmic sequences.  
  - CV In 2 = Offset 
    - Experimental: This takes a v/oct signal between 0v and 1v and applies that as an offset to both channels - CV1 and CV2 . It is not calibrated but is quantised to a chromatic scale. Let me know how you get on with it. 
  - Audio/CV 1 = Reset 
    - Experimental. Many sequencers have a reset button that jumps the sequence back to Step 1. Hardware Turing Machines never had this kind of reset input, because there's no easy way to step back to a previous state - the shift register that holds the binary sequence has moved on. If you send a pulse to this input (a rising edge) it will reset all the sequences back to their first step. Let me know if you find it interesting or useful. 
  - Audio/CV 2 = Switch 
    - Experimental: This input gives CV control over the switch to choose which preset is active. An input of +1v or more pushes the switch up, -1v or less pulls the switch down. 


## Tips: 
- Hold down the toggle switch and tap the reset/load button to clear the internal settings. When you see a fast animation on the LEDs it's safe to release the toggle switch. You may need to do this after updating the Program Card. 

## Behind the scenes: 
- The entire thing has been rewritten in Pico SDK C++ using [Chris Johnston's Computer Card library](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard). 
- One core runs at 48khz, tracking input pulses and updating the clocks. The second core handles LED updates, USB connection to the editor, and flash writes. 

## Quirks & not yes working: 
- Card version check / upgrade: At some point I'll move this card to it's own Repo, at which point upgrades will be shown automatically. 
- The visualiser in the web editor is slightly unscientific - clocked by both pulse outputs 
- The CSS on the web editor could be improved to make it responsive on different screen types. If you can help with that, please let me know. 
- Tiny delays in the main clock are noticeable a second after setting tap tempo and when making changes in the editor. If you can work out how to reduce those glitches please let me know. 


## Specific questions for testers 
- Any problems with different web editors? 
- How is external clocking? Any devices that don't work well? 
- Any glitches, points where the system becomes unresponsive? 
- Note lengths are proportional to divisions - so if the CV2/Pulse2 is running 4x faster than the main output, the pulses are 4 x shorter (to a minimum of 2ms. I'm not sure if this is the right behaviour 
- Adding or swapping scales for more useful ones is easy, I'm up for suggestions 
- Are the variable length pulses short and long the right durations / ranges? 


