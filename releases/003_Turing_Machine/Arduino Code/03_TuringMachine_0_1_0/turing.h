/*
   Class to create turing machine values
   Tom Whitwell, Music Thing Modular, May 2024 for Computer module
   Herne Hill, London

*/


#ifndef Turing_h
#define Turing_h

class Turing

{
  public:
    Turing(int length);
    void Update(int pot, int maxRange);
    void updateLength(int newLen);
    int returnLength();
    int DAC_16();
    int DAC_8();
    void DAC_print8();
    void DAC_print16();



  private:
    uint16_t _sequence = random(65536);
    int _length = 16;
};



#include "Arduino.h"

#define bitRotateL(value, len) ((((value) >> ((len) - 1)) & 0x01) | ((value) << 1))

#define bitRotateLflip(value, len) (((~((value) >> ((len) - 1)) & 0x01) | ((value) << 1)))



Turing::Turing(int length)
{
  _length = length;
}

// Call this each time the clock 'ticks'
// Pick a random number 0 to maxRange
// if the number is below the pot reading: bitRotateLflip, otherwise bitRotateL
// set _outputValue as
void Turing::Update(int pot, int maxRange) {
  int safeZone = maxRange >> 5;
  int sample = safeZone + random(maxRange - (safeZone * 2)); // add safe zones at top and bottom

  if (sample >= pot) {
    _sequence = bitRotateLflip(_sequence, _length);
  }

  else {
    _sequence = bitRotateL(_sequence, _length);
  }
}

// returns the full current sequence value as 16 bit number 0 to 65535
int Turing::DAC_16() {
  return _sequence;
}

// returns the current sequence value as 8 bit number 0 to 255 = ignores the last 8 binary digits
int Turing::DAC_8() {

  //  return _sequence >> 8 ; // left hand 8 bits
  return _sequence & 0xFF; // right hand 8 bits
}


void Turing::DAC_print8() {
  for (int i = 0; i < 8; i++) {
    // Serial.print(bitRead(_sequence >> 8, i)); // left hand 8 bits
    Serial.print(bitRead(_sequence & 0xFF, i));// right hand 8 bits



  }

}

void Turing::DAC_print16() {
  for (int i = 0; i < 16; i++) {
    Serial.print(bitRead(_sequence, i));
  }

}

void Turing::updateLength(int newLen) {
  _length = newLen;
}

int Turing::returnLength() {
  return _length;
}

#endif
