
#include <SPI.h>
#include <Bounce2.h>
#include "ResponsiveAnalogRead.h"

// Rough calibrations from one device
const int CVzeroPoint = 2085; // 0v
const int CVlowPoint = 100; // -6v ;
const int CVhighPoint = 4065; // +6v

const int CVzeroPoint5V = 2085; // 0v
const int CVlowPoint5V = 384; // -5v ;
const int CVhighPoint5V = 3744; // +5v

// NB MCP4822 wrongly configured - -5 to +5v, was supposed to be +-6
const int DACzeroPoint = 1657; // 0v
const int DAClowPoint = 3031; // -5v ;
const int DAChighPoint = 281; // +5v

// ADC input pins
#define AUDIO_L_IN_1 26
#define AUDIO_R_IN_1 27
#define MUX_IO_1 28
#define MUX_IO_2 29

// PWM CV output pins
#define CV_1_PWM 23
#define CV_2_PWM 22

// Mux pins
#define MUX_LOGIC_A 24
#define MUX_LOGIC_B 25

// Pulse in / out pins
#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3
#define PULSE_1_OUTPUT 8
#define PULSE_2_OUTPUT 9

#define NUM_PULSE_INS 2
#define NUM_PULSE_OUTS 2

// DAC parameters
#define DAC_config_chan_A_gain 0b0001000000000000
#define DAC_config_chan_B_gain 0b1001000000000000
#define PIN_CS 21

const int adcResolutionBits = 12;
int maxRange = 1 << adcResolutionBits;

const int channelCount = 8; // three knobs, 1 switch, 2 CV, two audio
const int pulse_in_pins[] = { PULSE_1_INPUT, PULSE_2_INPUT };
const int pulse_out_pins[] = { PULSE_1_OUTPUT, PULSE_2_OUTPUT };
const int cv_out_pins[] = { CV_1_PWM, CV_2_PWM };
const int leds[] = { 10, 11, 12, 13, 14, 15 };  // LED pin numbers


int mux_num;
Bounce pulse1_in = Bounce();
Bounce pulse2_in = Bounce();

ResponsiveAnalogRead* analog[channelCount];
uint32_t pulse_outs_time[NUM_PULSE_OUTS];
uint16_t pulse_outs_high_time[NUM_PULSE_OUTS];

class Computer {
public:
  Computer() {}

  void begin() {

    analogReadResolution(adcResolutionBits);
    for (int i = 0; i < 6; i++) {
      pinMode(leds[i], OUTPUT);
    }

    pinMode(MUX_LOGIC_A, OUTPUT);
    pinMode(MUX_LOGIC_B, OUTPUT);

    pinMode(PULSE_1_OUTPUT, OUTPUT);
    pinMode(PULSE_2_OUTPUT, OUTPUT);

    pulse1_in.attach(PULSE_1_INPUT, INPUT_PULLUP);
    pulse2_in.attach(PULSE_2_INPUT, INPUT_PULLUP);

    for (int i = 0; i < channelCount; i++) {
      analog[i] = new ResponsiveAnalogRead(0, true, .1);  //  lower is smoother
      analog[i]->setActivityThreshold(16);                // sleep threshold
      analog[i]->setAnalogResolution(1 << adcResolutionBits);
    }

    analogWrite(CV_1_PWM, CVzeroPoint); // set to zero
    analogWrite(CV_2_PWM, CVzeroPoint);

    // Setup SPI
    SPI.begin(); // Initialize the SPI bus
    pinMode(PIN_CS, OUTPUT); // Set the Chip Select pin as an output
    digitalWrite(PIN_CS, HIGH); // Deselect the SPI device to start
    dacWrite(0, DACzeroPoint); // Set outputs to zero 
    dacWrite(1, DACzeroPoint);
  }

  /**
   * Call frequently in loop() or similar. Manages updating input state and pulse output timing.
   * We only update a pair of mux inputs at a time to minimize time spent in update().
   */
  void update() {
    //uint32_t now = millis();
    // pulseOutUpdate(now);
    pulse1_in.update();
    pulse2_in.update();
    analog[0]->update(maxRange - analogRead(A0)); // inverted
    analog[1]->update(maxRange - analogRead(A1)); // inverted
    muxRead(mux_num);
    mux_num = (mux_num + 1) % 4; 
    muxSelect(mux_num);  // prepare mux for next read
  }

  void muxSelect(int num) {
    digitalWrite(MUX_LOGIC_A, ((num >> 0)) & 1);
    digitalWrite(MUX_LOGIC_B, ((num >> 1)) & 1);
    // NB this seems to need 1us delay for pins to 'settle' before reading.
  }

  void muxRead(int num) {
    //Serial.printf("mux num:%d\n",num);
    switch (num) {
      case 0:
        analog[4]->update(analogRead(A2));             // read Main Knob
        analog[2]->update(maxRange - analogRead(A3));  // Read CV 1 inverted
        break;
      case 1:
        analog[5]->update(analogRead(A2));             // read Knob X
        analog[3]->update(maxRange - analogRead(A3));  // Read CV 2 inverted
        break;
      case 2:
        analog[6]->update(analogRead(A2));             // read Knob Y
        analog[2]->update(maxRange - analogRead(A3));  // Read CV 1 inverted
        break;
      case 3:
        analog[7]->update(analogRead(A2));             // read Switch
        analog[3]->update(maxRange - analogRead(A3));  // Read CV 2inverted
        break;
    }
  }

  int audio1In() {  return analog[0]->getValue();  }
  int audio2In() {  return analog[1]->getValue();  }
  int cv1In()    {  return analog[2]->getValue();  }
  int cv2In()    {  return analog[3]->getValue();  }
  int knobMain() {  return analog[4]->getValue();  }
  int knobX()    {  return analog[5]->getValue();  }
  int knobY()    {  return analog[6]->getValue();  }
  int switchPos() {   // FIXME: do debouncing on this
      return analog[7]->getValue();
  }

  void setLED(int num, int val) {
    digitalWrite(leds[num], val);
  }

  // analogWrite(CV_1_PWM, CVzeroPoint + (((note - 60) * 275) / 10)); // 27.5 = 1 note, experimentally 
  int volts_to_pwm(float volts) {
    float pwm_per_volt = (CVhighPoint - CVlowPoint) / 12;  // -6 to +6 = 12V span
    int pwmval = pwm_per_volt * volts;
    return constrain(pwmval, 0, 4095);
    // float mV = 1000 * (midi_note * voltsPerNote);
    //return (constrain(map(mV, 0, 5000, 0, 4095), 0, 4095));
  }

  // int midi_to_pwm(float volts) { 
  //   return CVzeroPoint + (((note - 60) * 275) / 10)); // 27.5 = 1 note, experimentally   // FIXME
  // }

  int analogIn(uint ch) { 
    return analog[ch]->getRawValue();
  }

  /**
   * Output a CV voltage on a particular CV channel
   */
  void cvOut(uint num, float volts ) { 
    num--;
    int pwm_val = volts_to_pwm(volts);
    analogWrite( cv_out_pins[num], pwm_val);
  }

  /** 
   * Make the specified pulse output HIGH for a given number of millis. non-blocking
   */
  void pulseOut(uint num, uint16_t high_millis) {
    num--; // pulse1 and pulse2
    digitalWrite(pulse_out_pins[num], HIGH);
    pulse_outs_high_time[num] = high_millis;
    pulse_outs_time[num] = millis();
  }

  /** 
   * called by update(), used to lower a pulse output after the time specified in pulseOut()
   */
  void pulseOutUpdate(uint32_t now) {
    for (int i = 0; i < NUM_PULSE_OUTS; i++) {
      if (pulse_outs_time[i] and now - pulse_outs_time[i] >= pulse_outs_high_time[i]) {
        pulse_outs_time[i] = 0;
        digitalWrite(pulse_out_pins[i], LOW);
      }
    }
  }


  void dacWrite(int lchan, int rchan) { 
    uint16_t DAC_dataL = DAC_config_chan_A_gain | (lchan & 0xFFF);
    uint16_t DAC_dataR = DAC_config_chan_B_gain | (rchan & 0xFFF);

    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // Start SPI transaction
    digitalWrite(PIN_CS, LOW); // Select the SPI device

    SPI.transfer(DAC_dataL >> 8); // Send first byte
    SPI.transfer(DAC_dataL & 0xFF); // Send second byte

    digitalWrite(PIN_CS, HIGH); // Deselect the SPI device
    SPI.endTransaction(); // End SPI transaction

    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // Start SPI transaction
    digitalWrite(PIN_CS, LOW); // Select the SPI device

    SPI.transfer(DAC_dataR >> 8); // Send first byte
    SPI.transfer(DAC_dataR & 0xFF); // Send second byte

    digitalWrite(PIN_CS, HIGH); // Deselect the SPI device
    SPI.endTransaction(); // End SPI transaction

  }


};
