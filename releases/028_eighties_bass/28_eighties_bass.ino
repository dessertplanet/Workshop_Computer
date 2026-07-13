/*
  Music Thing Modular
  Computer
  Eighties Bass
  30 Jul 2024 - @todbot / Tod Kurt

  Functions:
  - main knob -- adjust filter cutoff frequency
  - X knob -- set pitch offset (CV1 controls voct pitch) 
  - Y knob -- set filter resonance
  - switch -- tap down to change filter
  - CV 1 in -- V/oct pitch, maybe
  - CV 2 in -- +/- additive to main knob filter cutoff frequency
  - audio L in -- CV controls detune amount
  - audio R in -- CV controls noise mix
  - LEDs -- LEDs 2,4,6 represent which filter mode (LPF, BPF, HPF)
  - audio L & R out -- audio out
*/

#include "MozziConfigValues.h"  // for named option values
#define MOZZI_AUDIO_MODE MOZZI_OUTPUT_EXTERNAL_TIMED
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#define MOZZI_ANALOG_READ MOZZI_ANALOG_READ_NONE
#define MOZZI_AUDIO_BITS 16
#define MOZZI_AUDIO_RATE 32768 // must be power of two
#define MOZZI_CONTROL_RATE 128  // times per second rate for updateControl()
#define OSCIL_DITHER_PHASE 1 
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/saw_analogue512_int8.h> // oscillator waveform
#include <ResonantFilter.h>
#include <mozzi_rand.h>  // for rand()
#include <mozzi_midi.h>  // for mtof()

#include "MTM_Computer.h"

#define NUM_VOICES 5

Oscil<SAW_ANALOGUE512_NUM_CELLS, MOZZI_AUDIO_RATE> aOscs [NUM_VOICES]; // audio oscillators

MultiResonantFilter filt;

int filt_mode = 0;   //  0 = lpf, 1 = bpf, 2 = hpf
const int num_filt_modes = 3;

uint32_t last_status_millis = 0;

float detune = 1;
float noisey_amt = 0.1; 
float signal_amt = 1 - noisey_amt;  // precalc this value so we don't do it every audio sample
float midi_note; 

Computer comp;

void audioOutput(const AudioOutput f) {
  // signal is passed as 16 bit, zero-centered, internally. 
  // This DAC expects 12 bits unsigned, so shift back four bits, 
  // and add a bias of 2^(12-1)=2048
  uint16_t lout = (f.l() >> 4) + 2048;
  uint16_t rout = (f.r() >> 4) + 2048;
  comp.dacWrite(lout,rout);
}

void setup() {
   
  Serial.begin(115200);
  
  comp.begin();

  startMozzi();

  filt.setCutoffFreqAndResonance(20, 170);
  for( int i=0; i<NUM_VOICES; i++) { 
     aOscs[i].setTable(SAW_ANALOGUE512_DATA);
  }

  comp.setLED(filt_mode*2, HIGH);
}

void loop() {
  audioHook();
}

// just like map() but for floats
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setNotes(int midi_note) { // could be float for finer control
  float f = mtof(midi_note);
  for(int i=1; i<NUM_VOICES-1; i++) {
    aOscs[i].setFreq( f + (float)(i * detune * rand(100)/100.0) );
  }
  aOscs[NUM_VOICES-1].setFreq( (float)(f/2.0) ); //(float)i * detune * rand(100)/100.0 );
}

// mozzi function, called MOZZI_CONTROL_RATE times per second
void updateControl() {
  comp.update();

  if( millis() - last_status_millis > 200) { 
    last_status_millis = millis();

    Serial.printf("ins: %3d X: %3d Y: %3d sw:%3d cv1: %3d cv2: %3d  aud1: %3d aud2: %3d",
      (comp.knobMain()/16), (comp.knobX()/16), (comp.knobY()/16), (comp.switchPos()/16), 
      (comp.cv1In()/16), (comp.cv2In()/16), (comp.audio1In()/16), (comp.audio2In()/16) );
    Serial.printf("  detune: %.2f noise: %.2f\n", detune, noisey_amt );

    // only do detene change occasionally
    if( comp.audio1In() > 2000 and comp.audio1In() < 2100 ) { 
      detune = 1;
    } else { 
      detune = constrain(mapfloat(comp.audio1In(), CVlowPoint5V, CVhighPoint5V, 0.02, 10), 0.02, 10);
    }
    // switch is filter mode. Check it only every 200 millis as cheap "debounce"
    if( comp.switchPos() < 500 ) {   // switch pushed down
      comp.setLED(1+filt_mode*2, LOW);
      filt_mode = (filt_mode+1) % num_filt_modes; // go to next filter type
      comp.setLED(1+filt_mode*2, HIGH);
    }
  }

  noisey_amt = abs(constrain(mapfloat(comp.audio2In(), CVlowPoint5V, CVhighPoint5V, -0.7, 0.7), -0.7, 0.7)); // don't let in too much noise
  signal_amt = 1-noisey_amt;

  // filter range (0-255) corresponds with 0-8191Hz
  int cutoff_freq = map( comp.knobMain(), 0, 4095, 1, 150 ); 
  int resonance  = map( comp.knobY(), 0, 4095, 0, 255 );
  int cutoff_freq_cv = map(comp.cv2In(), CVlowPoint, CVhighPoint, -100, 100);

  cutoff_freq = constrain(cutoff_freq + cutoff_freq_cv, 0,255);
  filt.setCutoffFreqAndResonance(cutoff_freq, resonance);

  float midi_note_knob = map(comp.knobX(), 0, 4096, -63, 64);
  float midi_note_cv1 = map(comp.cv1In(), CVlowPoint5V, CVhighPoint5V, 0, 120); 
  midi_note = constrain( midi_note_cv1 + midi_note_knob, 0, 127);

  setNotes( (int)midi_note );

}

// mozzi function, called every MOZZI_AUDIO_RATE to output sample
AudioOutput updateAudio() {
  int16_t asig = (long) 0;
  for( int i=0; i<NUM_VOICES; i++) {
    asig += aOscs[i].next();
  }
  asig = signal_amt * asig + noisey_amt * rand(255);  // add in some noise, maybe

  filt.next(asig);

  switch(filt_mode) { 
    case 0: asig = filt.low(); break;
    case 1: asig = filt.band(); break;
    case 2: asig = filt.high(); break;
  }
  return StereoOutput::fromAlmostNBit(13, asig, asig); // "stereo"
}

