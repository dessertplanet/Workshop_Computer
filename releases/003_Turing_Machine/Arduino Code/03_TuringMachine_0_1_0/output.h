/*
   Class to output pulses with clock division
   Should be triggered every 96ppq
   Tom Whitwell, Music Thing Modular, November 2018
   Updated May 2024 for Computer module
   Herne Hill, London

*/


#ifndef Output_h
#define Output_h

class Output

{
  public:
    Output* linkedOutput;
    bool oneTimeLink;
    Output(int led_pin, int out_pin, int mode, int dividor, Output* linkedOutput = nullptr, bool oneTimeLink = false);
    boolean DividorUp();
    boolean DividorDown();
    boolean DividorSet(int pot, int maxRange);
    void ModeChange();
    void ModeSet(int mode);
    boolean Update();
    void Reset();
    void Unlink();
    void linkReset(Output* o, bool oneTime = false);
    int Position();
    int ModeReport();
    void Save(int divideLocation);
    void Settings(int mode, int dividor);
    void LED_on();
    void LED_off();
    void pulseMode_on();
    void pulseMode_off();
    void pulseMode_flip();
    bool pulseMode_return(); 
    void setLinkedSequence(uint16_t seq);
    void LengthSet(int len_percent);


  private:
    int _position = 0;
    int _led_pin;
    int _out_pin;
    int _mode;
    int _dividor; // dividor index
    int _div = 12; // actual dividor
    unsigned long _pulseStart;
    int _pulseLength = 60; // pulse length in percent
    boolean _pulseOn;
    boolean _showLEDs = 1;
    boolean _pulseMode = 0; // If PulseMode is true, output reflects state of Bit 1 in the sequence.
    uint16_t _linkedSequence = 65535;
};


/*
   Class to output pulses with clock division
   Should be triggered every 24ppq
   Tom Whitwell, Music Thing Modular, November 2018
   Updated May 2024 for Computer module
   Herne Hill, London

*/

#include "Arduino.h"
#include <EEPROM.h>

// Variables for Clock & Division
// 0= even 1 = odd
static const int modeCount = 2;
static const int modeEntries = 12;
static const int divideValues[modeCount][modeEntries] = {
  {3, 6, 12, 24, 48, 96, 192, 384, 768, 1536, 3072, 6144},
  {4, 8, 16, 32, 64, 96, 144, 288, 84, 90, 94, 95},
  //experimental = five odd - 1/6, 1/3, 2/3, 1, 3/2, then five phase patterns that sync every 4,6,8,12,24 steps
};

Output::Output(int led_pin, int out_pin, int mode, int dividor, Output* linkedOutput, bool oneTimeLink) {
  pinMode(led_pin, OUTPUT);
  linkedOutput = linkedOutput;
  _led_pin = led_pin;
  _out_pin = out_pin;
  _mode = mode;
  _dividor = dividor;
  _div = divideValues[_mode][_dividor];
}

boolean Output::DividorDown() {
  boolean result = true;
  _dividor ++;
  if (_dividor > modeEntries - 1) {
    _dividor = (modeEntries - 1);
    result = false;
  }
  return result;
}

boolean Output::DividorUp() {
  boolean result = true;
  _dividor --;
  if (_dividor < 0) {
    _dividor = 0;
    result = false;
  }
  return result;
}

void Output::Reset() {
  _position = 0;
  if (linkedOutput) {
    linkedOutput->Reset();  // Reset linked instance
    if (oneTimeLink) {
      Unlink();  // Unlink after resetting if it's a one-time link
    }

  }
}

void Output::linkReset(Output * o, bool oneTime) {
  linkedOutput = o;  // Set the linked instance
  oneTimeLink = oneTime;  // Set the one-time link flag
}

void Output::Unlink() {
  linkedOutput = nullptr;  // Remove the link
  oneTimeLink = false;  // Reset the one-time link flag
}

boolean Output::DividorSet(int pot, int maxRange) {
  // Pot value will be 0 to maxRange
  int newVal = map(pot, 0, maxRange, modeEntries - 1, 0);

  boolean changed = 0;
  if (newVal != _dividor) {
    _dividor = newVal;
    changed = 1;
  }
  return changed;
}

void Output::LengthSet(int len_percent) {
  _pulseLength = len_percent;
}


void Output::ModeChange() {
  _mode++;
  if (_mode >= modeCount) _mode = 0;
}

void Output::ModeSet(int mode) {
  if (mode < modeCount) {
    _mode = mode;
  }
}

int Output::ModeReport() {
  return _mode;
}

boolean Output::Update() {

  boolean result = false;
  boolean activePulse = bitRead(_linkedSequence, 0);

  if ((_position == 0) && (_pulseMode == 0)) {
    if (_showLEDs) digitalWrite(_led_pin, HIGH);
    digitalWrite(_out_pin, LOW); // output pins are inverted
    _pulseStart = millis();
    _pulseOn = true;
    result = true;
  }

  else if ((_position == 0) && _pulseMode && activePulse) {
    if (_showLEDs) digitalWrite(_led_pin, HIGH);
    digitalWrite(_out_pin, LOW); // output pins are inverted
    _pulseStart = millis();
    _pulseOn = true;
    result = true;

  }

  else if ((_position == 0) && _pulseMode) {
    // send true value because it is the start of a pulse, so pitches / turings should be updated
    result = true;
  }

  int pulseStop = (_div * _pulseLength) / 100;

  if (pulseStop < 2) pulseStop = 2;

  if (_pulseOn == true && (_position > pulseStop)) { // wait two 96ppq pulses before turning off LED
    if (_showLEDs) digitalWrite(_led_pin, LOW);
    digitalWrite(_out_pin, HIGH);
  }
  _position++;
  _div = divideValues[_mode][_dividor];
  if (_position >= _div ) {
    Output::Reset();
  }

  return result;
}

void Output::LED_on() {
  _showLEDs = 1;
}

void Output::LED_off() {
  _showLEDs = 0;
}


void Output::pulseMode_on() {
  _pulseMode = 1;
}
void Output::pulseMode_off() {
  _pulseMode = 0;
}

void Output::pulseMode_flip() {
  _pulseMode = !_pulseMode;
}


bool Output::pulseMode_return(){
  return _pulseMode; 
}

void Output::setLinkedSequence( uint16_t seq) {
  _linkedSequence = seq;
}

int Output::Position() {
  return _position;
}

void Output::Save(int divideLocation) {
  EEPROM.write(divideLocation, _dividor);
  EEPROM.commit(); 
}

void Output::Settings(int mode, int dividor) {
  _mode = mode;
  _dividor = dividor;
}


#endif
