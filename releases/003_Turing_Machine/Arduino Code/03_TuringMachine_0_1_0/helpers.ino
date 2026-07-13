// set the MUX logic pins - reading channels 0,1,2,3 = 00 01 10 11
void muxUpdate(int num)
{
  digitalWrite(MX_A, ((num >> 0)) & 1);
  digitalWrite(MX_B, ((num >> 1)) & 1);
  // NB this seems to need 1us delay for pins to 'settle' before reading.
}

// Read the Mux IO pins into the value store
void muxRead(int num)
{
  switch (num)
  {

    case 0:
      // read Main Knob
      analog[4]->update(analogRead(A2));
      // Read CV 1
      analog[2]->update(maxRange - analogRead(A3)); // inverted
      break;

    case 1:
      // read Knob X
      analog[5]->update(analogRead(A2));
      // Read CV 2
      analog[3]->update(maxRange - analogRead(A3)); // inverted
      break;

    case 2:
      // read Knob Y
      analog[6]->update(analogRead(A2));
      // Read CV 1
      analog[2]->update(maxRange - analogRead(A3)); // inverted

      break;

    case 3:
      // read Switch
      analog[7]->update(analogRead(A2));
      // Read CV 2
      analog[3]->update(maxRange - analogRead(A3)); // inverted

      break;
  }
}

void dacWrite(int channel, int value) {
  uint16_t DAC_data = (channel == 0) ? (DAC_config_chan_A_nogain | (value & 0xFFF)) : (DAC_config_chan_B_nogain | (value & 0xFFF));
  uint8_t highByte = DAC_data >> 8;
  uint8_t lowByte = DAC_data & 0xFF;

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // Start SPI transaction
  digitalWrite(DAC_CS, LOW); // Select the SPI device

  SPI.transfer(highByte); // Send first byte
  SPI.transfer(lowByte); // Send second byte

  digitalWrite(DAC_CS, HIGH); // Deselect the SPI device
  SPI.endTransaction(); // End SPI transaction
}

// Retrieve unique ID from flash memory on the program card, turn it into 32 bit number to use in random seed like this:
//    uint32_t seed = process_unique_id(); // Process unique ID to get a 32-bit seed
//    randomSeed(seed);

uint32_t process_unique_id() {
  uint8_t unique_id[8]; // Array to store the 64-bit ID
  flash_get_unique_id(unique_id); // Retrieve the unique ID

  // Cast bytes to uint32_t and combine
  uint32_t high = (unique_id[0] << 24) | (unique_id[1] << 16) | (unique_id[2] << 8) | unique_id[3];
  uint32_t low = (unique_id[4] << 24) | (unique_id[5] << 16) | (unique_id[6] << 8) | unique_id[7];

  // XOR high and low parts to mix the bits
  return high ^ low;
}



// Define the LED patterns for each number
//const int patterns[8][6] = {
//  {HIGH, LOW,  LOW,  HIGH, HIGH, LOW},   // 2
//  {HIGH, LOW,  HIGH, HIGH, LOW,  HIGH},  // 3
//  {LOW,  HIGH, HIGH, HIGH, LOW,  HIGH},  // 4
//  {HIGH, HIGH, LOW,  LOW,  HIGH, HIGH},  // 5
//  {HIGH, HIGH, HIGH, LOW,  HIGH, HIGH},  // 6
//  {LOW,  HIGH, HIGH, LOW,  LOW,  HIGH},  // 8
//  {HIGH, LOW,  HIGH, LOW,  HIGH, LOW},   // 12
//  {LOW,  HIGH, LOW,  HIGH, HIGH, HIGH}   // 16
//};


const int patterns[8][6] = {
  {HIGH, HIGH,  LOW,  LOW, LOW, LOW},   // 2
  {HIGH, HIGH,  HIGH, LOW, LOW,  LOW},  // 3
  {HIGH,  HIGH, HIGH, HIGH, LOW,  LOW},  // 4
  {HIGH, HIGH, HIGH,  HIGH,  HIGH, LOW},  // 5
  {HIGH, HIGH, HIGH, HIGH,  HIGH, HIGH},  // 6
  {LOW,  LOW, HIGH, HIGH,  HIGH,  HIGH},  // 8
  {LOW, LOW,  LOW, LOW,  HIGH, HIGH},   // 12
  {HIGH,  HIGH, LOW,  LOW, HIGH, HIGH}   // 16
};


// Function to display the pattern for a given number
void ledPattern(int number) {





  int patternIndex = -1;

  // Find the index of the given number in the lengths array
  for (int i = 0; i < 8; i++) {
    if (lengths[i] == number) {
      patternIndex = i;
      break;
    }
  }
//  Serial.print(" patternIndex ");
//  Serial.println(patternIndex);
  // If the number is found in the lengths array
  if (patternIndex != -1) {
    // Set the LED states based on the pattern
    for (int i = 0; i < 6; i++) {
      digitalWrite(leds[i], patterns[patternIndex][i]);
    }
  } else {
    // If the number is not found, turn off all LEDs
    for (int i = 0; i < 6; i++) {
      digitalWrite(leds[i], LOW);
    }
  }
}


void SetupComputerIO() {

  analogReadResolution(adcResolutionBits);

  // Initialize all LEDs
  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(leds[i], OUTPUT);
  }

  // Setup SPI for DAC output
  SPI.begin();
  pinMode(DAC_CS, OUTPUT);
  digitalWrite(DAC_CS, HIGH); // Set CS high initially

  // If Normalisation probe not used, this reduces crosstalk between connected pins and sets input pins to known level
  pinMode(NORM_PROBE, OUTPUT);
  digitalWrite(NORM_PROBE, 1); 

  pinMode(MX_A, OUTPUT);
  pinMode(MX_B, OUTPUT);

  pinMode(PULSE_1_INPUT, INPUT_PULLUP);
  pinMode(PULSE_2_INPUT, INPUT_PULLUP);
  pinMode(PULSE_1_RAW_OUT, OUTPUT);
  pinMode(PULSE_2_RAW_OUT, OUTPUT);


  for (int i = 0; i < channelCount; i++) {
    analog[i] = new ResponsiveAnalogRead(0, true, .1); //  lower is smoother
    analog[i]->setActivityThreshold(16); // sleep threshold
    analog[i]->setAnalogResolution(1 << adcResolutionBits);
  }


  // Set up Eeprom
  Wire.setSDA(16);
  Wire.setSCL(17);
  Wire.begin();
  Wire.setClock(400000);

  attachInterrupt(digitalPinToInterrupt(PULSE_1_INPUT), Pulse1Trigger, FALLING);
  attachInterrupt(digitalPinToInterrupt(PULSE_2_INPUT), Pulse2Trigger, FALLING);

}


void setPulseLength(int setting, int channel) {
  switch (setting) {
    case 0:
      pulseLength[channel] = 1;
      pulseLengthMod[channel] = 0;
      break;
    case 1:
      pulseLength[channel] = 25;
      pulseLengthMod[channel] = 0;
      break;
    case 2:
      pulseLength[channel] = 50;
      pulseLengthMod[channel] = 0;
      break;
    case 3:
      pulseLength[channel] = 75;
      pulseLengthMod[channel] = 0;
      break;
    case 4:
      pulseLength[channel] = 95;
      pulseLengthMod[channel] = 0;
      break;
    case 5:
      pulseLength[channel] = 15;
      pulseLengthMod[channel] = 35;
      break;
    case 6:
      pulseLength[channel] = 50;
      pulseLengthMod[channel] = 40;
      break;
    default:
      // Optional: Handle out-of-range values for `setting`
      pulseLength[channel] = 1;
      pulseLengthMod[channel] = 0;
      break;
  }
}
