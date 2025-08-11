# 13 Noisebox

<img width="615" height="1653" alt="noisecard" src="https://github.com/user-attachments/assets/c455c2f0-8165-4350-8eac-9da56125c553" />

My first card! A port of 13 Befaco Noise Plethora algorithms (some of which are not 1:1, which might just be part of the charm, so don't expect them to sound exactly the same, it's noise!)

**Main Knob:** Controls which algorithm you are currently selecting.

**X & Y** Controls various parameters of the algorithm for sound shaping. X usually maps more closely to pitch but it varies.

**Z**: Up toggles on a bitcrushing effect for crunchier noise. Middle is default, no effects.

The momentary toggle down randomizes all your controls - it lets you quickly get a new noise sound when you tap it and your controls now will all be offset.

Hold the momentary switch for over 2.5 seconds to reset this.

INS:

**CV In 1**: Offset CV for the algorithm being selected.

**CV In 2**: VCA - you can gate your noise with this. Also great for AM modulation of the noise. With nothing plugged in, this normalizes to always being on.

**CV In 3**: Offset CV for X Knob

**CV In 4**: Offset CV for Y Knob

**Pulse (CV In 5)**: Sample and Hold trigger (to be detailed further below)

**Pulse (CV In 6)**: Gate to turn on and off the bitcrushing effect. Effect is on when input is high.

OUTS:

**CV Out 1/CV Out 2**: Output of the noise algorithm and its current parameters. Both outs have the same source. I may consider making CV Out 2 go through a rainbow of noise instead.

**CV Out 3:** Output of the currently sample and held value, as determined by Pulse/CV In 5 - captures the current noise sample and holds until it receives another trigger.

**CV Out 4**: Slewed output of the above. Slew time is determined by the time between pulses in **CV In 5**

**Pulse/CV Out 5**: A binary gate toggled on or off if the sample and held value is greater than 0. Good for random gate patterns. Again this requires a clock or some signal into Pulse/CV In 5

**Pulse/CV Out 6**: Realtime comparator signal from whether or not the noise sample is above 0. Really glitchy PWM sound.


