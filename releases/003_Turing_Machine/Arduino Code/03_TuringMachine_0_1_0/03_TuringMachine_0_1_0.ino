/*
  Music Thing Modular
  Workshop System
  Turing Machine Card

  by Tom Whitwell
  in Herne Hill, London
  November 2024

  Switch Z Down = Tap Tempo
  Switch Z Middle = [0] settings
  Swtich Z Up = [1] settings

  Pot Y = Clock Divider for Pulse2/CV2/CV-Audio2 outputs

  CV Input 1 = CV Control over Clock Divider

  Main pot = Turing control CCW = double Noon = random CW = Lock

  Pot X = Length - 2, 3, 4, 5, 6, 8, 12, 16

  HOLD DOWN tap switch to toggle Output 2 into pulse mode

  Notes:
  This is a very chaotic mess and needs to be refactored into something that makes sense. Sorry
  There are two types of EEPROM in play here - Calibration.h reads from a real i2c EEPROM built into the Computer module
  Flash_Load uses an EEPROM emulation code in the Arduino Pico framework to read from pseudo-eeprom on the Flash memory - the program card

  To do:
  -- Add Pulse inputs
      -- One for main clock - disables internal tap tempo once a pulse is received [restarts if tap received]
      -- One for 2nd output, disables diviply once a pulse is recieve [maybe restarts if diviply is changed]
  -- Add CV for Clock


*/

// Config variables in HEX
const int CARD_NUMBER = 0x03;
const int MAJOR_VERSION = 0x00;
const int MINOR_VERSION = 0x01;
const int POINT_VERSION = 0x00;

#include <Arduino.h>
#include <SPI.h>
#include <EEPROM.h>
#include "hardware/flash.h"
#include <float.h>
#include <math.h>
#include <string.h>
#include <Wire.h>

#include "RPi_Pico_TimerInterrupt.h"

// Include local headers
#include "ResponsiveAnalogRead.h"
#include "CV.h"
#include "DACChannel.h"
#include "Click.h"
#include "Output.h"
#include "turing.h"
#include "config.h"
#include "quantize.h"
#include "calibration.h"

// Timer used for Tap Tempo
RPI_PICO_Timer RPTimer1(1);

// variables for Local flash storage on the Program Card
// = Virtual Eeprom, vs the real Eeprom from which calibration values are read
// define memory locations
// These Flash memory locations are the same numbers that we send/receive settings from the web browser
void Flash_load();
#define EE_active 0
#define EE_divide 1
#define EE_bpm_lo 2
#define EE_bpm_hi 3

#define EE_scale 4
#define EE_notes 5
#define EE_range 6
#define EE_length 7
#define EE_looplen 8

#define EE_scale2 9
#define EE_notes2 10
#define EE_range2 11
#define EE_length2 12
#define EE_looplen2 13

#define EE_CVRange 14

#define EE_CV1PulseMode 15
#define EE_CV2PulseMode 16
#define EE_CV1PulseMode2 17
#define EE_CV2PulseMode2 18

const int adcResolutionBits = 12;
int maxRange = 1 << adcResolutionBits;
const int channelCount = 8; // three knobs, 1 switch, 2 CV
ResponsiveAnalogRead *analog[channelCount];

// 0 = Audio In 1
// 1 = Audio In 2
// 2 = CV In 1
// 3 = CV In 2
// 4 = Main knob
// 5 = Knob X
// 6 = Knob Y
// 7 = Switch

boolean pulse1 = 0;
boolean pulse2 = 0;

// Switch between settings A and settings B
boolean settingsSwitch = 0;


// Define the number of precision PWM DAC channels
#define NUM_DACS 2
DACChannel* dacChannels[NUM_DACS];

// Instantiate CV object
CV cv;


// Tap Tempo section
// Attach Switch momentary 'tap' to two functions
extern void tempoTap();
extern void longHold();
Click tap(tempoTap, longHold);
unsigned long tapTime = 0;
unsigned long divTime = 0;
boolean tapping = false;
boolean divChange = false;
bool ninetysixppq();
boolean reSync = false;
// Clock outputs
Output tapOut(leds[4], PULSE_1_RAW_OUT, 0, 5);
Output divOut(leds[5], PULSE_2_RAW_OUT, 0, 5);
Turing TuringDAC1(8);
Turing TuringDAC2(8);
Turing TuringPWM1(8);
Turing TuringPWM2(8);
Turing TuringPulseLength1(8);
Turing TuringPulseLength2(8);
int pulseLength[2] = {1, 1}; // initial pulse length
int pulseLengthMod[2] = {0, 0}; // range in percent of pulse length modulation
int pulseLengthSetting[2] = {0, 0};
int Loop_len[2] = {1, 1}; // extend loop length on output 2 = -1, 0, +1
bool pulseModes[2][2] = {{0, 1}, {0, 1}}; // Clock Pulse or Turing pulse pulseModes[CV output, 0 = 1, 1 = 2] [mode switch up = 1 mid = 0]

bool externalClockActive = false;
unsigned long lastExternalClockTime = 0;
const unsigned long externalClockTimeout = 2000; // External clock timeout in milliseconds




// Random Seed from Flash Memory Serial Number
uint32_t process_unique_id();
uint32_t seed = process_unique_id();

static const int lengthEntries = 8;
static const int lengths[lengthEntries] = {2, 3, 4, 5, 6, 8, 12, 16};
bool lengthChanging = 0;
unsigned long lengthChangeStart = 0;
bool internalClock = 1;


// QUANTIZER

int low_note = noteOffset;  // Middle C (C3)
int high_note = noteOffset + 48 ; // Three octave above Middle C (C6)
ScaleType scale[2] = {SCALE_MINOR_PENTATONIC, SCALE_MINOR_PENTATONIC}; // default to minor pentatonic because why not
SieveType notes[2] = {SIEVE_SCALE, SIEVE_SCALE};
int range[2] = {0, 0}; // holds octave range


// Global variables for Calibration
Calibration calibration;


/* APPLICATION STARTS HERE */
void setup()
{

  // Set up pins etc
  SetupComputerIO();

  EEPROM.begin(24); // Ensure this is large enough to include all data written into the Flash Eeprom


  // Read initial pot / switch settings:
  for (int i = 0; i < 4; i++) {
    muxUpdate(i);
    delay(1);
    muxRead(i);
  }

  Flash_load(1); // load defaults from EEprom AND start timer

  // Initialise randomisation with unique ID stored on the memory chip on the card - each card is unique
  randomSeed(seed);

  // Set pulse mode on Outputs
  tapOut.pulseMode_off();
  divOut.pulseMode_on();


  // Begin calibration (send version number )
  // At this point it will create a blank calibration if none present
  calibration.begin(MAJOR_VERSION, MINOR_VERSION, POINT_VERSION);

  // Now connect up calibration.h to DACChannel.h
  int numberOfChannels = calibration.returnNumChannels();
  for (int chan = 0; chan < numberOfChannels; chan++) {
    int numberOfSettings = calibration.returnNumSettings(chan);
    float voltages[numberOfSettings] = {};
    uint32_t dacValues[numberOfSettings] = {};
    for (int setting = 0; setting < numberOfSettings; setting++) {
      voltages[setting] = calibration.returnVoltage(chan, setting) * 0.1;
      dacValues[setting] = calibration.returnSetting(chan, setting);
    }
    dacChannels[chan] = new DACChannel(numberOfSettings, voltages, dacValues, noteOffset);
    dacChannels[chan]->calculateCalibrationConstants();
  }

}

int muxCount = 0; // Counter for reading through the mux each time
int readCount = 0; // Counter for how often we read analog inputs - loop should read MIDI constantly, can read analog inputs much less often
int readFrequency = 16; //

void loop() {

  // THIS SECTION TRIGGERED EVERY readFrequency LOOPS I.E. EVERY 16 LOOPS
  if (readCount > readFrequency) {
    // Read mux variables according to MuxCount
    muxRead(muxCount);
    muxCount++;
    if (muxCount >= 4) muxCount = 0;

    // Updade Mux settings according to new muxCount
    muxUpdate(muxCount); // If results are odd, put a delay after mux Update

    // Divisor is Knob Y, analog[6], CV1 input is [2]
    if ((analog[6]->hasChanged()) || (analog[2]->hasChanged()) ) {
      if (divOut.DividorSet(analog[6]->getValue() + (analog[2]->getValue() - 2200) , maxRange)) {
        tapOut.linkReset(&divOut, true);
      }
    }



    // if switch is up, load alternative sets of settings
    if (analog[7]-> hasChanged()) {
      if (analog[7] ->getValue() > 3000) {
        settingsSwitch = 0;

        int lengthPlus = Loop_len[settingsSwitch] - 1;

        int Length1 = TuringDAC1.returnLength();
        TuringDAC2.updateLength(Length1 + lengthPlus);

        Length1 = TuringPWM1.returnLength();
        TuringPWM2.updateLength(Length1 + lengthPlus);

        Length1 = TuringPulseLength1.returnLength();
        TuringPulseLength2.updateLength(Length1 + lengthPlus);



      }
      else if (analog[7] ->getValue() > 1000) {

        settingsSwitch = 1;

        int lengthPlus = Loop_len[settingsSwitch] - 1;

        int Length1 = TuringDAC1.returnLength();
        TuringDAC2.updateLength(Length1 + lengthPlus);

        Length1 = TuringPWM1.returnLength();
        TuringPWM2.updateLength(Length1 + lengthPlus);

        Length1 = TuringPulseLength1.returnLength();
        TuringPulseLength2.updateLength(Length1 + lengthPlus);



      }

      if (pulseModes[0][settingsSwitch]) {
        tapOut.pulseMode_on();
      }
      else {
        tapOut.pulseMode_off();
      }

      if (pulseModes[1][settingsSwitch]) {
        divOut.pulseMode_on();
      }
      else {
        divOut.pulseMode_off();
      }

    }


    if (analog[5]-> hasChanged()) {
      int reading = analog[5]->getValue();
      reading = map(reading, 0, maxRange - 50, 0, lengthEntries - 1);
      int lengthPlus = Loop_len[settingsSwitch] - 1;
      int newVal = lengths[reading];
      if (newVal != TuringDAC1.returnLength()) {
        TuringDAC1.updateLength(newVal);
        TuringDAC2.updateLength(newVal + lengthPlus);
        TuringPWM1.updateLength(newVal);
        TuringPWM2.updateLength(newVal + lengthPlus);
        TuringPulseLength1.updateLength(newVal);
        TuringPulseLength2.updateLength(newVal + lengthPlus);


        // Turn off LEDs
        lengthChanging = 1;
        divOut.LED_off();
        tapOut.LED_off();
        ledPattern(newVal);
        lengthChangeStart = millis();

      }
    }

    if (lengthChanging && (millis() - lengthChangeStart > 2000)) {
      lengthChanging = 0;
      divOut.LED_on();
      tapOut.LED_on();
    }
    readCount = 0;

  }

  // THIS SECTION TRIGGERED EVERY LOOP

  readCount ++;

  // Switch = analog[7] - this checks to see if the switch is pulled down and triggers the tap debouncing
  tap.Update((analog[7]->getValue() < 200));



  webConfig();

}



void tempoTap() {

  // first tap
  if (!tapping) {
    tapTime = millis();
    tapping = true;
  }

  else {
    unsigned long sinceLast = millis() - tapTime;
    if (sinceLast > 50 && sinceLast < 3000 ) {        // ignore bounces and forgotten taps > 2 seconds
      //      Timer1.setPeriod((sinceLast * 1000) / 96);      // set timer to new bpm
      RPTimer1.setInterval((sinceLast * 1000) / 96, ninetysixppq);

      tapTime = millis();                             // record time ready for next tap
      int bpmValue = (unsigned long) (625000 / ((sinceLast * 1000) / 96));


      byte highByte = (bpmValue >> 8) & 0xFF;
      byte lowByte = bpmValue & 0xFF;
      EEPROM.write(EE_bpm_hi, highByte);
      EEPROM.write(EE_bpm_lo, lowByte);
      EEPROM.commit();

      tapOut.Reset();                                 // reset tap out to zero
      reSync = true;                                  // reset divide out at next zero

    }
  }
}


void longHold() {
  divOut.pulseMode_flip();
  bool currentPulseMode = divOut.pulseMode_return();
  EEPROM.write(EE_CV2PulseMode2, currentPulseMode);
  EEPROM.commit();
}


// Initialise = if this is being run on first launch of the card, initialise=true. If run when updating values from web config, initialise=false.
void Flash_load(boolean initialise) {

  // reset defaults if switch held down on startup
  // OR if wrong data is found in the EEPROM

  int Startup_mode = 0;
  int Startup_divide = 5;
  int Startup_bpm = 120; // do not change


  // If on first run from setup(), check for held down button to reset values
  if (initialise) {

    if ((analog[7]->getValue() < 200) ||
        (EEPROM.read(EE_active) != 37) ) {
      for (int i = 0 ; i < EEPROM.length() ; i++) { // wipe entire eeprom
        EEPROM.write(i, 0);
      }
      EEPROM.commit();

      // now write defaults
// 2 versions - switch in middle 
      
      EEPROM.write(EE_active, 37);
      EEPROM.write(EE_bpm_lo, Startup_bpm);
      EEPROM.write(EE_bpm_hi, 0);
      EEPROM.write(EE_divide, Startup_divide);
      EEPROM.write(EE_scale, 3);
      EEPROM.write(EE_scale2, 3);
      EEPROM.write(EE_notes, 0); // scale
      EEPROM.write(EE_notes2, 1); // 1-3-5
      EEPROM.write(EE_range, 2); // 3 octaves
      EEPROM.write(EE_range2, 1); // 2 octaves
      EEPROM.write(EE_length, 5); // Short modulated length 
      EEPROM.write(EE_length2, 5); // Short modulated length 
      
      EEPROM.write(EE_looplen, 1); // same loop length
      EEPROM.write(EE_looplen2, 1); // same loop length
      EEPROM.write(EE_CVRange, 0); // +/-6V

      EEPROM.write(EE_CV1PulseMode, 0); // *Clock pulse vs Turing pulse
      EEPROM.write(EE_CV2PulseMode, 0); // Clock pulse vs *Turing pulse
      EEPROM.write(EE_CV1PulseMode2, 0); // Clock pulse vs Turing pulse
      EEPROM.write(EE_CV2PulseMode2, 1); // Clock pulse vs *Turing pulse

      EEPROM.commit();

      for (int i = 0; i < 40; i++) {  //  blinkenlights
        digitalWrite(leds[i % 6], HIGH); //  blinkenlights
        delay((40 - i) * 2);          //  blinkenlights
        digitalWrite((leds[i % 6]), LOW); //  blinkenlights
        delay((40 - i) * 2);          //  blinkenlights
      }                               //  blinkenlights
    }
  }

  // The rest of the function operates every time it is run, for example when reading values from the web config
  if (EEPROM.read(EE_active) == 37) { // if eprom data is found, read values
    if (initialise) {
      Startup_divide = EEPROM.read(EE_divide);
      Startup_bpm = word(EEPROM.read(EE_bpm_hi), EEPROM.read(EE_bpm_lo));
    }

    uint8_t storedValue = EEPROM.read(EE_scale);
    if (storedValue <= NUM_SCALES) { // Ensure it's within the enum range
      scale[0] = static_cast<ScaleType>(storedValue);

    } else {
      // Handle invalid value
      scale[0] = SCALE_MINOR_PENTATONIC;
    }

    storedValue = EEPROM.read(EE_scale2);
    if (storedValue <= NUM_SCALES) { // Ensure it's within the enum range
      scale[1] = static_cast<ScaleType>(storedValue);
    } else {
      // Handle invalid value
      scale[1] = SCALE_MINOR_PENTATONIC;
    }


    storedValue = EEPROM.read(EE_notes);
    if (storedValue <= 4) {
      notes[0] = static_cast<SieveType>(storedValue);
    } else {
      // Handle invalid value
      notes[0] = SIEVE_SCALE;
    }

    storedValue = EEPROM.read(EE_notes2);
    if (storedValue <= 4) {
      notes[1] = static_cast<SieveType>(storedValue);
    } else {
      // Handle invalid value
      notes[1] = SIEVE_SCALE;
    }

    storedValue = EEPROM.read(EE_range);
    if (storedValue <= 5) { // Ensure it's within the enum range
      range[0] = int(storedValue);
    } else {
      // Handle invalid value
      range[0] = 0;
    }

    storedValue = EEPROM.read(EE_range2);
    if (storedValue <= 5) { // Ensure it's within the enum range
      range[1] = int(storedValue);
    } else {
      // Handle invalid value
      range[1] = 0;
    }

    storedValue = EEPROM.read(EE_length);
    if (storedValue <= 7) {
      pulseLengthSetting[0] = int(storedValue);
      setPulseLength(pulseLengthSetting[0], 0);
    } else {
      // Handle invalid value
      pulseLengthSetting[0] = 1;
      setPulseLength(pulseLengthSetting[0], 0);
    }

    storedValue = EEPROM.read(EE_length2);
    if (storedValue <= 7) {
      pulseLengthSetting[1] = int(storedValue);
      setPulseLength(pulseLengthSetting[1], 1);

    } else {
      // Handle invalid value
      pulseLengthSetting[1] = 1;
      setPulseLength(pulseLengthSetting[1], 1);
    }


    storedValue = EEPROM.read(EE_looplen);
    if (storedValue <= 2) {
      Loop_len[0] = storedValue;

      int lengthPlus = Loop_len[0] - 1;

      int Length1 = TuringDAC1.returnLength();
      TuringDAC2.updateLength(Length1 + lengthPlus);

      Length1 = TuringPWM1.returnLength();
      TuringPWM2.updateLength(Length1 + lengthPlus);

      Length1 = TuringPulseLength1.returnLength();
      TuringPulseLength2.updateLength(Length1 + lengthPlus);


    } else {
      // Handle invalid value
      Loop_len[0] = 1;
    }

    storedValue = EEPROM.read(EE_looplen2);
    if (storedValue <= 2) {
      Loop_len[1] = storedValue;

      int lengthPlus = Loop_len[1] - 1;

      int Length1 = TuringDAC1.returnLength();
      TuringDAC2.updateLength(Length1 + lengthPlus);

      Length1 = TuringPWM1.returnLength();
      TuringPWM2.updateLength(Length1 + lengthPlus);

      Length1 = TuringPulseLength1.returnLength();
      TuringPulseLength2.updateLength(Length1 + lengthPlus);
    } else {
      // Handle invalid value
      Loop_len[1] = 1;
    }

    storedValue = EEPROM.read(EE_CVRange);
    if (storedValue <= 3) {
      switch (storedValue) {
        case 0: // +/-6V
          DAChighPoint = 0;   // +5V
          DAClowPoint = 4095;   // -5V
          break;
        case 1: // +/-3V

          DAChighPoint = 1023;  // +2.5V
          DAClowPoint = 3071;   // -2.5V
          break;
        case 2: // 0 to +6V
          DAChighPoint = 0;   // +5V
          DAClowPoint = 2047;   // 0V
          break;
        case 3: // 0 to +3V
          DAChighPoint = 1023;  // +2.5V
          DAClowPoint = 2047;   // 0V
          break;
      }
    } else {
      // Handle invalid value
      DAChighPoint = 0;   // +5V
      DAClowPoint = 4095;   // -5V
    }

    storedValue = EEPROM.read(EE_CV1PulseMode);
    pulseModes[0][0] = (bool)storedValue;
    storedValue = EEPROM.read(EE_CV1PulseMode2);
    pulseModes[0][1] = (bool)storedValue;
    storedValue = EEPROM.read(EE_CV2PulseMode);
    pulseModes[1][0] = (bool)storedValue;
    storedValue = EEPROM.read(EE_CV2PulseMode2);
    pulseModes[1][1] = (bool)storedValue;

    if (pulseModes[0][settingsSwitch]) {
      tapOut.pulseMode_on();
    }
    else {
      tapOut.pulseMode_off();
    }

    if (pulseModes[1][settingsSwitch]) {
      divOut.pulseMode_on();
    }
    else {
      divOut.pulseMode_off();
    }


  }


  if (initialise) {
    unsigned long bpmCalculation = 625000 / Startup_bpm;
    //  Timer1.initialize(bpmCalculation); //will call the function at 96ppq: 120bpm = 500000 per quarter, 5208 at 96ppq
    //  Timer1.attachInterrupt(ninetysixppq);

    RPTimer1.attachInterruptInterval(bpmCalculation, ninetysixppq);

    tapOut.Settings(Startup_mode, 5);
    divOut.Settings(Startup_mode, Startup_divide);
  }

}


bool ninetysixppq(struct repeating_timer *t) {
  if (reSync == true && tapOut.Position() == 0) {
    divOut.Reset();
    reSync = false;
    divOut.Save(EE_divide); // write divide settings to relevant EEprom positions
  }
  if (tapOut.Update()) {


    tapOut.setLinkedSequence(TuringPWM1.DAC_16());
    divOut.setLinkedSequence(TuringPWM2.DAC_16());
    // Update Both left hand 'tap' channels
    TuringDAC1.Update(analog[4]->getValue(), maxRange);
    TuringPWM1.Update(analog[4]->getValue(), maxRange);
    TuringPulseLength1.Update(analog[4]->getValue(), maxRange);

    // Update Pulse length
    int bipolarModulation = TuringPulseLength1.DAC_8() - 128;
    int modulationValue = (bipolarModulation * pulseLengthMod[settingsSwitch]) / 128;
    int modulatedPulseLength = pulseLength[settingsSwitch] + modulationValue;
    modulatedPulseLength = constrain(modulatedPulseLength, 1, 100);
    tapOut.LengthSet(modulatedPulseLength);


    // Update Audio Out 1

    int dacTemp = map(TuringDAC1.DAC_8(), 0, 255, DAClowPoint, DAChighPoint);
    dacWrite(0, dacTemp);
    //    dacWrite(0, DACzeroPoint - (TuringDAC1.DAC_8() << 3) );          // scale up 8 bit turing output to 12 bit DAC + invert
    // Update CV OUt

    high_note = noteOffset + 12 + (12 * range[settingsSwitch]);

    // Update CV Out 1
    int midi_note = getMidiNote(TuringPWM1.DAC_8(), low_note, high_note, scale[settingsSwitch], notes[settingsSwitch]);
    cv.Set(0, dacChannels[0]->midiToDac(midi_note));



    //    analogWrite(CV_1_PWM, CVzeroPoint + (TuringPWM1.DAC_8() << 3)  );        // scale up 8 bit turing output to 12 bit PWM
    // Update LEDs
    if (!lengthChanging) {
      analogWrite(leds[0], TuringDAC1.DAC_8() >> 1 );        // scale down to 7 bit LED PWM
      analogWrite(leds[2], TuringPWM1.DAC_8() >> 1 );         // scale down to 7 bit LED PWM
    }
  }

  if (divOut.Update()) {
    tapOut.setLinkedSequence(TuringPWM1.DAC_16());
    divOut.setLinkedSequence(TuringPWM2.DAC_16());
    // Update Both rightt hand 'diviply' channels
    TuringDAC2.Update(analog[4]->getValue(), maxRange);
    TuringPWM2.Update(analog[4]->getValue(), maxRange);
    TuringPulseLength2.Update(analog[4]->getValue(), maxRange);

    // Update Pulse2 length
    int bipolarModulation2 = TuringPulseLength2.DAC_8() - 128;
    int modulationValue2 = (bipolarModulation2 * pulseLengthMod[settingsSwitch]) / 128;
    int modulatedPulseLength2 = pulseLength[settingsSwitch] + modulationValue2;
    modulatedPulseLength2 = constrain(modulatedPulseLength2, 1, 100);
    divOut.LengthSet(modulatedPulseLength2);




    // Update Audio out 2

    int dacTemp = map(TuringDAC2.DAC_8(), 0, 255, DAClowPoint, DAChighPoint);
    dacWrite(1, dacTemp);

    //    dacWrite(1, DACzeroPoint - (TuringDAC2.DAC_8() << 3) );

    // Update CV Out 2
    int midi_note2 = getMidiNote(TuringPWM2.DAC_8(), low_note, high_note, scale[settingsSwitch], notes[settingsSwitch]);
    cv.Set(1, dacChannels[1]->midiToDac(midi_note2));

    // update LEDs
    if (!lengthChanging) {
      analogWrite(leds[1], TuringDAC2.DAC_8() >> 1 );        // scale down to 7 bit LED PWM
      analogWrite(leds[3], TuringPWM2.DAC_8() >> 1 );         // scale down to 7 bit LED PWM
    }

  }
  if ((millis() - tapTime) > 3000) tapping = false; // turn off tapping after period of inactivity
  if ((millis() - divTime) > 100) divChange = false; // turn off flash after making change to dividor
  //    digitalWrite(ledOdd, ( tapping  ^ divOut.ModeReport()) ^ divChange); // ODD led lights when in Odd mode, OR when tapping is activated. When in odd mode, tapping turns off the Odd LED
  return true;
}


void Pulse1Trigger() {

  // Pulse 1 Input
}


void Pulse2Trigger() {
  // Pulse 2 Input

}



void webConfig() {

  bool changed = 0;

  if (Serial.available()) {
    String serialInput = Serial.readStringUntil('\n');


    if (serialInput == "GET_VERSION") {  // New command for version info
      // This worked, printing individual numbers one at a time didn't.
      String versionString = "Version Number: " + String(MAJOR_VERSION) + "." + String(MINOR_VERSION) + "." + String(POINT_VERSION);
      Serial.println(versionString);

    }


    // SEND values to the browser
    if (serialInput == "GET_VALS") {

      String valString = "";

      for (int i = 4; i < 19; i++) {
        valString = "_SET:" + String(i - 4) + "_VAL:" + String(EEPROM.read(i));
        Serial.println(valString);
      }


      valString = "_SEED:" + String(seed);
      Serial.println(valString);

    }


    // If this is a SET command from the browser:
    if (serialInput.indexOf("_SET:") > -1 ) {
      int cmdIndex = serialInput.indexOf("_SET:");
      int valIndex = serialInput.indexOf("_VAL:");
      int commandNumber = serialInput.substring(cmdIndex + 5, valIndex).toInt();
      int value = serialInput.substring(valIndex + 5).toInt();

      EEPROM.write(commandNumber + 4, value);
      EEPROM.commit();
      Flash_load(0);

    }




  }



}
