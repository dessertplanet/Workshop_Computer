/*
  Music Thing Modular
  Computer (Proto 1.2)
  Bytebeat

  by Matt Kuebrich
  October 2024

  Flash via Arudino IDE using Earle F. Philhower's Raspberry Pi Pico Arduino core:
  https://github.com/earlephilhower/arduino-pico

  Main pot = Speed (Sample Rate)
  Pot X = Bank Select
  Pot Y = Parameter 1

  Switch up - Built-in Formulas
  Switch middle - User Formulas
  Switch down (momentary) = Reset
  
  Audio In 1 = Parameter 1 Modulation
  Audio In 2 = Parameter 2 Modulation

  Audio Out 1 = Bytebeat Out
  Audio Out 2 = Next Bytebeat Out

  CV In 1 = Formula Select Modulation 
  CV In 2 = Sample Rate Modulation

  CV Out 1 = ByteBeat Out (slow)
  CV Out 2 = ByteBeat Out (fast)

  Pulse In 1 = Reset
  Pulse In 2 = Reverse

  Pulse Out 1 = 1Bit Output (Bitbeat)
  Pulse Out 2 = Division of t (can be used as a clock)

*/

#include <Arduino.h>
#include <SPI.h>
#include "RPi_Pico_TimerInterrupt.h"
#include <EEPROM.h>

//Formula Bank
#include "formulas.h"

// Tinyexpr
#define TE_POW_FROM_RIGHT
#include "tinyexpr_bitw.h"

// EEPROM addresses
const int EE_FORMULA1 = 0;
const int EE_FORMULA2 = 100;
const int EE_FORMULA3 = 200;
const int EE_FORMULA4 = 300;
const int EE_FORMULA5 = 400;
const int EE_FORMULA6 = 500;

const int EE_FORMULAS[] = {EE_FORMULA1, EE_FORMULA2, EE_FORMULA3, EE_FORMULA4, EE_FORMULA5, EE_FORMULA6};

const char SAVE_EXPR_KEYWORD1[] = "_SAVE1";
const char SAVE_EXPR_KEYWORD2[] = "_SAVE2";
const char SAVE_EXPR_KEYWORD3[] = "_SAVE3";
const char SAVE_EXPR_KEYWORD4[] = "_SAVE4";
const char SAVE_EXPR_KEYWORD5[] = "_SAVE5";
const char SAVE_EXPR_KEYWORD6[] = "_SAVE6";
const char CLEAR_KEYWORD[] = "_CLEAR";

te_expr *expr;
te_expr *exprs[6];
#define MAX_EXPR_LEN 95

int err;
int len;

const char* formulaChar;

RPI_PICO_Timer sampleTimer(1);

// Rough calibrations from one device
int CVzeroPoint = 2085;   // 0v
int DACzeroPoint = 2048;  // 0v


int leds[6] = { 10, 11, 12, 13, 14, 15 };
unsigned long previousMillis = 0;  // Stores the last time the LED was updated
const long blinkInterval = 50;  // Interval for blinking in milliseconds
bool ledState = LOW;  // Current state of the LED
int switchState;

#include "ResponsiveAnalogRead.h"
const int adcResolutionBits = 12;
int maxRange = 1 << adcResolutionBits;
const int channelCount = 8;  // three knobs, 1 switch, 2 CV
ResponsiveAnalogRead *analog[channelCount];

// 0 = Audio In 1
// 1 = Audio In 2
// 2 = CV In 1
// 3 = CV In 2
// 4 = Main knob
// 5 = Knob X
// 6 = Knob Y
// 7 = Switch

const char *labels[] = {
  "Audio1",
  "Audio2",
  "CV In 1",
  "CV In 2",
  "Main",
  "Knob X",
  "Knob Y",
  "Switch"
};

// ADC input pins
#define AUDIO_L_IN_1 26
#define AUDIO_R_IN_1 27
#define MUX_IO_1 28
#define MUX_IO_2 29

// Mux pins
#define MUX_LOGIC_A 24
#define MUX_LOGIC_B 25

// Pulse Pins
#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3

boolean pulse1 = 0;
boolean pulse2 = 0;
boolean pulse1Prev = 0;
boolean pulse2Prev = 0;

// Controls for mapping, range and inversion
int scaleHigh = 4095;
int scaleLow = 0;

// Define output pins
#define PULSE_1_RAW_OUT 8
#define PULSE_2_RAW_OUT 9
#define CV_1_PWM 23
#define CV_2_PWM 22

//// Proto 1.2 Computer
#define DAC_config_chan_A_gain 0b0001000000000000
#define DAC_config_chan_B_gain 0b1001000000000000
#define DAC_config_chan_A_nogain 0b0011000000000000
#define DAC_config_chan_B_nogain 0b1011000000000000

#define PIN_CS 21

// Bytebeat
uint32_t t = 0;
uint32_t w = 0;
uint32_t w2 = 0;
uint32_t w3 = 0;
uint32_t w4 = 0;

uint16_t p1 = 0; // 0 to 255
uint16_t p2 = 1;
uint16_t p3 = 0; 

int x = 0;
int y = 0;
int z = 0;

int formulaUpdate = 0;
bool reversePlayback = false;
int sr = 8000;  // Sample rate for bytebeat
int srPrev = 8000;
static bool momentaryReadPrev = false;
int formulaIndex = 0;
int halfbright = 30; // Half the maximum brightness (0-255)
int fullbright = 255; // Half the maximum brightness (0-255)
//int CV1_IN, CV2_IN;
int CV1_IN = 2085;
int CV2_IN = 2085;

int AUDIO1_IN, AUDIO2_IN;
int switchRead, switchReadPrev;
int timerInterval;
String serialInput;
String bytebeatFormula;
int userslot = 0;
int prev_userslot = 0;

int knobMRead, knobXRead, knobYRead;

const int NUM_FORMULAS = 36;

typedef uint32_t (*FormulaFunction)(uint32_t, uint8_t, uint32_t, uint32_t, uint32_t);

FormulaFunction formulas[NUM_FORMULAS] = {
  formula1, formula2, formula3, formula4, formula5,
  formula6, formula7, formula8, formula9, formula10,
  formula11, formula12, formula13, formula14, formula15,
  formula16, formula17, formula18, formula19, formula20,
  formula21, formula22, formula23, formula24, formula25,
  formula26, formula27, formula28, formula29, formula30,
  formula31, formula32, formula33, formula34, formula35,
  formula36
};


void setup() {
  Serial.begin(115200);

  int cnt = 1000;     // Will wait for up to ~1 second for Serial to connect.
  while (!Serial && cnt--) {
    delay(1);
  }

  for (int i = 0; i < 6; i++) {
    exprs[i] = nullptr;
}

  EEPROM.begin(2048);
  eeRead(EE_FORMULA1);
  eeRead(EE_FORMULA2);
  eeRead(EE_FORMULA3);
  eeRead(EE_FORMULA4);
  eeRead(EE_FORMULA5);
  eeRead(EE_FORMULA6);

  uint32_t sampleRate = 8000;
  
  timerInterval = 1000000 / sampleRate; // interval in microseconds (125)
  sampleTimer.attachInterruptInterval(timerInterval, sampleInterruptHandler);


  for (int i = 0; i < 6; i++) {
    pinMode(leds[i], OUTPUT);
  }

  analogReadResolution(adcResolutionBits);

  analogWriteFreq(60000);
  analogWriteRange(4095);  // not sure this is right - should be 60khz = 0-2047 = 11 bits

  pinMode(PULSE_1_INPUT, INPUT_PULLUP);
  pinMode(PULSE_2_INPUT, INPUT_PULLUP);
  pinMode(PULSE_1_RAW_OUT, OUTPUT);
  pinMode(PULSE_2_RAW_OUT, OUTPUT);
  pinMode(CV_1_PWM, OUTPUT);
  pinMode(CV_2_PWM, OUTPUT);
  pinMode(AUDIO_L_IN_1, INPUT_PULLUP);
  analogWrite(CV_1_PWM, CVzeroPoint);  // set to zero
  analogWrite(CV_2_PWM, CVzeroPoint);
  digitalWrite(PULSE_1_RAW_OUT, 1);  // inverted outputs, so this turns them off
  digitalWrite(PULSE_2_RAW_OUT, 1);  // inverted outputs, so this turns them off
  pinMode(MUX_LOGIC_A, OUTPUT);
  pinMode(MUX_LOGIC_B, OUTPUT);

  // Set up the 8 analog channels in Responsive Analog Read.
  // NB this is very crude - values more suited for knobs than CV and certainly not for audio

  for (int i = 0; i < channelCount; i++) {
    analog[i] = new ResponsiveAnalogRead(0, true, .1);  //  lower is smoother
    analog[i]->setActivityThreshold(16);                // sleep threshold
    analog[i]->setAnalogResolution(1 << adcResolutionBits);
  }

  // Setup SPI
  SPI.begin();                 // Initialize the SPI bus
  pinMode(PIN_CS, OUTPUT);     // Set the Chip Select pin as an output
  digitalWrite(PIN_CS, HIGH);  // Deselect the SPI device to start
  dacWrite(0, DACzeroPoint);   // Set outputs to zero
  dacWrite(1, DACzeroPoint);

  // Read initial pot / switch settings:
  for (int i = 0; i < 4; i++) {
    muxUpdate(i);
    delay(1);
    muxRead(i);
  }
}

int muxCount = 0;


void loop() {

  //unsigned long currentMillis = millis();
  

  muxRead(muxCount);
  muxCount++;
  if (muxCount >= 4) muxCount = 0;
  muxUpdate(muxCount);  // If results are odd, put a delay after mux Update

  // the first analog in is actually A1, while the 2nd is A0, kinda confusing
  analog[0]->update(maxRange - analogRead(A1));  // inverted
  analog[1]->update(maxRange - analogRead(A0));  // inverted

  pulse1 = !digitalRead(PULSE_1_INPUT);
  pulse2 = !digitalRead(PULSE_2_INPUT);

  knobMRead = analog[4]->getValue();
  knobXRead = analog[5]->getValue();
  knobYRead = analog[6]->getValue();
  switchRead = analog[7]->getValue();
  // Switch Z
  // Reset bytebeat when momentary switch is tapped
  bool momentaryRead = analog[7]->getValue() < 200;
  if (momentaryRead && !momentaryReadPrev) {
    t = 0;  // Reset t
    Serial.println("Reset");
    switchState = 3;  //momentary

    //clearEEPROM(); //clear
  }

  momentaryReadPrev = momentaryRead;

switchRead = analog[7]->getValue();

  if (switchRead > 4000 & switchReadPrev < 4000) {
    //switch goes from middle to up
    //Serial.println("switched up");
  } else if (switchRead< 4000 & switchReadPrev > 4000) {
    //Serial.println("switched down");
  }

  switchReadPrev = switchRead;

  sr = map(knobMRead, 0, 4095, 20, 8000);  // it should be 8000 in middle of pot?
  //sr = map(knobMRead, 0, 4095, 20, 32000); //32k
  

  formulaIndex = map(knobXRead, 0, 4000, 0, 35);
  p1 = map(knobYRead, 0, 4095, 0, 255);
  p2 = 126; //default 
  p3 = 127; //default

  if (analog[7]->getValue() > 4000) {
    // Switch is up
    switchState = 0;  //up
  } else {
    // Switch is in middle
    switchState = 1;  //middle
  }

  AUDIO1_IN = analog[0]->getValue();  
  AUDIO2_IN = analog[1]->getValue();

  CV1_IN = analog[2]->getValue();
  CV2_IN = analog[3]->getValue();

  //formulaIndex = formulaIndex * (CV1_IN + 2048) / 4096; // Scale CV1_IN to the range 0-15
  //formulaIndex %= 15; // Wrap around 15

  //2085 0 point

  //Serial.println(formulaIndex);

  /*
  // Reverse when switch is up, otherwise play normally
  if (switchRead > 4000) {
    reversePlayback = true;  // Up switch position
  } else {
    reversePlayback = false;  // Middle switch position
  }
  //Serial.println(reversePlayback);

  */

  // Reset on low to high pulse
  if (pulse1 && !pulse1Prev) {
    t = 0;
  }
  pulse1Prev = pulse1;

  // Reverse on low to high pulse
  if (pulse2 && !pulse2Prev) {
    //Serial.println("pulse!");
    reversePlayback = !reversePlayback;
  }
  pulse2Prev = pulse2;


  // Sample rate modulation
  unsigned long newTimerInterval = 1000000 / (sr * (CV2_IN + 2048) / 4096);


  // Update the timer interval if changed
  static unsigned long prevTimerInterval = 0;

  if (newTimerInterval != prevTimerInterval) {
    sampleTimer.attachInterruptInterval(newTimerInterval, sampleInterruptHandler);
    prevTimerInterval = newTimerInterval;
  }


// parameter modulation
p1 = ((p1 * (AUDIO1_IN + 2048)) / 4096) % 255;
p2 = ((p2 * (AUDIO2_IN + 2048)) / 4096) % 255;

// BUILT IN
if (switchState == 0) {

  //seems to have issues
  formulaIndex = ((formulaIndex * (CV1_IN + 2048)) / 4096) % 36;

  /*
Serial.print("formula=");
Serial.println(formulaIndex);
Serial.print("cv=");
Serial.println(CV1_IN);
*/
  // Turn LEDS off
  for (int i = 0; i < 6; i++) {
    digitalWrite(leds[i], LOW);
  }

  // dim LED shows slot
  analogWrite(leds[(formulaIndex % 6)], halfbright);

  // bright LED shows bank
  analogWrite(leds[(formulaIndex / 6)], fullbright);

  w = formulas[formulaIndex](labs(t), w, p1, p2, p3);

  // the next bytebeat
  w2 = formulas[constrain(formulaIndex + 1, 1, 32)](labs(t), w, p1, p2, p3);

  //cv slow 
  w3 = formulas[formulaIndex](labs(t>>6), w, p1, p2, p3);

  //cv fast
  w4 = formulas[formulaIndex](labs(t<<3), w, p1, p2, p3);

  //Serial.println("ra" + String(formulaIndex));
}



// USER 
if (switchState == 1) {

  userslot = formulaIndex / 6;
  userslot = (((userslot * (CV1_IN + 2048)) / 4096) % 6);

  //Serial.println(userslot);
  //Serial.println(CV1_IN);

  // Turn LEDS off
  for (int i = 0; i < 6; i++) {
    //turn all LEDS off
    digitalWrite(leds[i], LOW);
  }

  // LED shows slot
  analogWrite(leds[(userslot)], fullbright);



  

  if (Serial.available()) {


    serialInput = Serial.readStringUntil('\n');


    if (!serialInput.startsWith("_")) {
      bytebeatFormula = serialInput;
      Serial.println("Formula Recieved!");

    } else if (serialInput == SAVE_EXPR_KEYWORD1) {
      eeSave(EE_FORMULA1);
      Serial.println("User Slot 1 Saved");

    } else if (serialInput == SAVE_EXPR_KEYWORD2) {
      eeSave(EE_FORMULA2);
      Serial.println("User Slot 2 Saved");

    } else if (serialInput == SAVE_EXPR_KEYWORD3) {
      eeSave(EE_FORMULA3);
      Serial.println("User Slot 3 Saved");

    } else if (serialInput == SAVE_EXPR_KEYWORD4) {
      eeSave(EE_FORMULA4);
      Serial.println("User Slot 4 Saved");

    } else if (serialInput == SAVE_EXPR_KEYWORD5) {
      eeSave(EE_FORMULA5);
      Serial.println("User Slot 5 Saved");

    } else if (serialInput == SAVE_EXPR_KEYWORD6) {
      eeSave(EE_FORMULA6);
      Serial.println("User Slot 6 Saved");

    } else if (serialInput == CLEAR_KEYWORD) {
      clearEEPROM();
      Serial.println("EEPROM Cleared");
    }

    // Serial.print("Received formula: ");
    // Serial.println(bytebeatFormula);

    if (expr) {
      te_free(expr);
      expr = nullptr;
      formulaUpdate = 1;
    }

    //removes warnings about converting a string constant to 'char*'
    char tchar[] = "t";
    char xchar[] = "p1";
    char ychar[] = "p2";
    char zchar[] = "p3";
    char wchar[] = "w";

    te_variable vars[] = { { tchar, &t }, { xchar, &x }, { ychar, &y }, { zchar, &z }, { wchar, &w } };

    const char* formulaChar = bytebeatFormula.c_str();
    expr = te_compile(formulaChar, vars, 5, &err);

    if (!expr) {
      Serial.print("error compiling: ");
      Serial.print(err);
    }

/*
    //blink when recieve formula from htmlpage, redo
    if (currentMillis - previousMillis >= blinkInterval) {
      previousMillis = currentMillis;
      digitalWrite(leds[0], HIGH);
    }
*/



  }  //end serial

  // this didnt work w/ p1, etc. maybe it go confused w/ variables with numbers?
  x = p1;
  y = p2;
  z = p3;

  if (userslot != prev_userslot) {
    formulaUpdate = 0;
  }

  prev_userslot = userslot;


    if (formulaUpdate) {
      w = te_eval(expr);
    } else if (userslot <= 6) {
      w = te_eval(exprs[userslot]);                        // Recall from flash
      w2 = te_eval(exprs[constrain(userslot + 1, 0, 5)]);  // Recall from flash
    }

}  //end switch1



// Write Outputs
  dacWrite(0, DACzeroPoint + (((int)w - 128) << 4) + ((int)w == 255 ? 15 : 0));
  dacWrite(1, DACzeroPoint + (((int)w2 - 128) << 4) + ((int)w2 == 255 ? 15 : 0)); 
  analogWrite(CV_1_PWM, CVzeroPoint + ((int)w3 - 128) * 16 + ((int)w3 == 255 ? 15 : 0));  // scale up 8 bit output to 12 bit PWM
  analogWrite(CV_2_PWM, CVzeroPoint + ((int)w4 - 128) * 16 + ((int)w4 == 255 ? 15 : 0));  // scale up 8 bit output to 12 bit PWM
  digitalWrite(PULSE_1_RAW_OUT, w & 1); // Inverted
  digitalWrite(PULSE_2_RAW_OUT, t >> 4); // Divided by 16 to give a "tempo" pulse
  
}  //end loop



bool sampleInterruptHandler(repeating_timer_t *rt) {
    t = reversePlayback ? t - 1 : t + 1;
    return true;
}


void eeSave(const int address) {

    // Saves the bytebeat string to the address on EEPROM

    //Serial.print("Saved formula: ");
    //Serial.println(bytebeatFormula);

    int strLength = bytebeatFormula.length() + 1; // Include the null terminator

    if (strLength > MAX_EXPR_LEN) {
        Serial.println("Error: Expression length exceeds maximum allowed length.");
        return;
    }

    char exprString[MAX_EXPR_LEN];
    strncpy(exprString, bytebeatFormula.c_str(), MAX_EXPR_LEN - 1); // Copy the string, ensuring null termination
    exprString[MAX_EXPR_LEN - 1] = '\0';

    len = strlen(exprString) + 1; // Include the null terminator
    //Serial.print("Save with length: ");
    //Serial.println(len);

    EEPROM.put(address, len);
    EEPROM.put(address + sizeof(int), exprString);
    delay(100); // ?
    EEPROM.commit();
    delay(100); // ?

    eeRead(address); // After saving, load it immeditaly so it updates
}


void eeRead(int address) {

  // Reads the bytebeat string

  delay(100);
  EEPROM.get(address, len);
  delay(100);

  //Serial.print("Read length from address ");
  //Serial.print(address);
  //Serial.print(": ");
  //Serial.println(len);

  if (len > 0 && len <= MAX_EXPR_LEN) {
    char exprString[MAX_EXPR_LEN];

    EEPROM.get(address + sizeof(int), exprString);

    exprString[len - 1] = '\0';
    Serial.print(address);
    Serial.print("Read from EEPROM: ");
    Serial.println(exprString);

    char tchar[] = "t";
    char xchar[] = "p1";
    char ychar[] = "p2";
    char zchar[] = "p3";
    char wchar[] = "w";

    te_variable vars[] = { { tchar, &t }, { xchar, &x }, { ychar, &y }, { zchar, &z }, { wchar, &w } };
    
    if (exprs[address / 100]) {
      te_free(exprs[address / 100]);
    }

    exprs[address / 100] = te_compile(exprString, vars, 5, &err);

    if (!exprs[address / 100]) {
      Serial.print("te_compile error at address ");
      Serial.print(address);
      Serial.print(": ");
      Serial.println(err);
    }

  } else {
    Serial.print(address);
    Serial.println("Error: Invalid string length read from EEPROM.");
  }

}


void clearEEPROM() {

    for (int i = 0 ; i < EEPROM.length() ; i++) { // wipe entire eeprom
      EEPROM.write(i, 0);
    }
        EEPROM.commit(); // ? 
        delay(100); // ?

  // Clear the exprs array
  for (int i = 0; i < 6; i++) {
    if (exprs[i]) {
      te_free(exprs[i]);
      exprs[i] = nullptr;
    }
  }

}



