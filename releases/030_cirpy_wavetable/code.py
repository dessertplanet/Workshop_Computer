# SPDX-FileCopyrightText: 2024 Tod Kurt
# SPDX-License-Identifier: MIT
"""
  Music Thing Modular
  Workshop System Computer
  Wavetable Oscillator in CircuitPython Card

  by Tod Kurt, May 2024

  - Main knob   -- controls wavetable position 
  - X knob      -- controls amount of triangle wave LFO to add to wavetable position
  - Y knob      -- controls frequency of LFO
  - Z switch down -- selects next wavetable
  - Z switch up   -- toggles quantized notes or not on CV 1 Input
  
  - CV 1 In     -- pitch: 0-5V should kinda track 1V/oct
  - CV 2 In     -- adds to main knob to control wavetable position

  - CV 1 Out    -- reflects current wavetable position
  - CV 2 Out    -- reflects wavemod LFO
  
  - Pulse 1 & 2 Out -- PWM audio out

"""

import os
import time
import math
import audiocore
import synthio
import ulab.numpy as np
# installed from bundle, or "circup install adafruit_wave adafruit_debouncer"
import adafruit_wave 
from adafruit_debouncer import Debouncer
from mtm_computer import Computer, map_range, gamma_correct

wav_dir = "/wav"
note_quant = True  # Set to True to track MIDI notes, else slides between notes
sample_rate = 44100   # audio rate, or 22050 
detune_amount = 0   # set to 0.001 or more to get phat dual-oscillator action

class Wavetable:
    """Simple WAV-based wavetable oscillator """
    
    # mix between values a and b, works with numpy arrays too,  t ranges 0-1
    def lerp(a, b, t):  return (1-t)*a + t*b

    def __init__(self, filepath, wave_len=256):
        self.w = adafruit_wave.open(filepath)
        self.wave_len = wave_len  # how many samples in each wave
        if self.w.getsampwidth() != 2 or self.w.getnchannels() != 1:
            raise ValueError("unsupported WAV format")
        self.waveform = np.zeros(wave_len, dtype=np.int16)  # empty buffer we'll copy into
        self.num_waves = self.w.getnframes() / self.wave_len

    def set_wave_pos(self, pos):
        """Pick where in wavetable to be, morphing between waves"""
        pos = min(max(pos, 0), self.num_waves-1)  # constrain
        samp_pos = int(pos) * self.wave_len  # get sample position
        self.w.setpos(samp_pos)
        waveA = np.frombuffer(self.w.readframes(self.wave_len), dtype=np.int16)
        self.w.setpos(samp_pos + self.wave_len)  # one wave up
        waveB = np.frombuffer(self.w.readframes(self.wave_len), dtype=np.int16)
        pos_frac = pos - int(pos)  # fractional position between wave A & B
        self.waveform[:] = Wavetable.lerp(waveA, waveB, pos_frac) # mix waveforms A & B

def find_wavs(wav_dir):
    """Find all the wavs in a given dir"""
    wav_fnames =[]
    for fname in sorted(os.listdir(wav_dir)):
        if fname.lower().endswith('.wav') and not fname.startswith('.'):
            wav_fnames.append(wav_dir+"/"+fname)
    return wav_fnames


wav_fnames = find_wavs(wav_dir)
if len(wav_fnames) == 0:
    print("No WAV files found in", wav_dir)
    
wav_fname = wav_fnames[0]

comp = Computer()
# set pulse outputs to be stereo output, but with a mono signal,
# playing only a single source (our synthio synthesizer)
comp.pulse_outs_to_audio(sample_rate=sample_rate,
                         channel_count=1, voice_count=1)

# todo: something like this should really be in Computer()
z_button_up = Debouncer(lambda: comp.switch > 60000) # up position
z_button_down = Debouncer(lambda: not comp.switch < 2000) # momentary down position

# create a synthio synth, attach it to the mixer
synth = synthio.Synthesizer(sample_rate=sample_rate)
comp.mixer.voice[0].play(synth)

# create a wavetable and start it playing with a synthio note
wavetable = Wavetable(wav_fname)
note = synthio.Note(frequency=110, waveform=wavetable.waveform)
synth.press(note)  # start the wavetable playing
if detune_amount:
    note2 = synthio.Note(frequency=110, waveform=wavetable.waveform)
    synth.press(note2)

lfo1 = synthio.LFO()  # defaults to triangle wave
synth.blocks.append(lfo1)   # start free-running lfo running

last_time = 0
wav_fname_idx = 0
wavemod_range = wavetable.num_waves

print("using wavetable:",wav_fname, ", num_waves=",wavetable.num_waves)

while True:
    comp.update()
    
    z_button_down.update()
    z_button_up.update()
    if z_button_up.rose:
        note_quant = not note_quant
        print("Quantize CV 1 Input:", note_quant)
    if z_button_down.fell:
        # load up next wavetable
        wav_fname_idx = (wav_fname_idx + 1) % len(wav_fnames)
        wav_fname = wav_fnames[wav_fname_idx]
        wavetable = Wavetable(wav_fname)
        note.waveform = wavetable.waveform
        wavemod_range = wavetable.num_waves
        print("using wavetable:",wav_fname, ", num_waves=",wavetable.num_waves)

    # map main knob to wave position
    wavepos = map_range(comp.knob_main, 0,65535, 0, wavemod_range)

    # map X knob to modulation amount
    wavemod = 0.25 * (comp.knob_x/65535) * lfo1.value
    
    # map Y knob to modulation frequency
    wavemod_rate = map_range(comp.knob_y, 0,65535, 0.01, 10)
    lfo1.rate = wavemod_rate

    # CV1 In controls pitch, map it to MIDI note values
    # note the magic values determined experimentally for 0-5V input
    # detect if CV 1 has been plugged in
    if comp.cv_1_in < 33000:  # a little above midpoint determined experimentally
        note_val = 36
    else:
        note_val = map_range(comp.cv_1_in, 33000, 59700, 12, 72)  # five octave range
    if note_quant:
        note_val = int(note_val)
    note.frequency = synthio.midi_to_hz(note_val)
    # if we're in dual-oscillator mode, track 2nd osc's pitch
    if detune_amount:
        note2.frequency = synthio.midi_to_hz(note_val + detune_amount)
    
    # CV2 In goes up and down 1/4th of the wavetable position space
    cv2_in = map_range(comp.cv_2_in, 0,65535, -wavemod_range//4, wavemod_range//4)
    wavepos = wavepos + (wavemod_range/2) * wavemod + cv2_in

    wavetable.set_wave_pos(wavepos)

    # CV outs echo the values of wavemod and y knob
    comp.cv_1_out = map_range(wavepos, 0, wavemod_range, 0, 65535)
    comp.cv_2_out = 32767 * (1+wavemod)  # makes it centered-ish around zero

    # LEDs reflects knob & cv position
    comp.leds[0].duty_cycle = gamma_correct(32767 * (1+wavemod))
    comp.leds[1].duty_cycle = gamma_correct(comp.knob_x)
    comp.leds[2].duty_cycle = gamma_correct(comp.knob_y)
    comp.leds[4].duty_cycle = gamma_correct(comp.cv_1_in)
    comp.leds[5].duty_cycle = gamma_correct(comp.cv_2_in)


    # Print out the input state periodically
    if time.monotonic() - last_time > 0.5:
        last_time = time.monotonic()
        #print("analog:", comp.analog)  # can also see analog array
        print("knobs:", comp.knob_main, comp.knob_x, comp.knob_y,
              " cvin:", comp.cv_1_in, comp.cv_2_in,
              " quant:", note_quant,
              " note: %.2f" % note_val)
