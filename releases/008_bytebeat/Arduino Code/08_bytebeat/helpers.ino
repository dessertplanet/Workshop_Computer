// set the MUX logic pins - reading channels 0,1,2,3 = 00 01 10 11
void muxUpdate(int num)
{
  digitalWrite(MUX_LOGIC_A, ((num >> 0)) & 1);
  digitalWrite(MUX_LOGIC_B, ((num >> 1)) & 1);
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
  digitalWrite(PIN_CS, LOW); // Select the SPI device

  SPI.transfer(highByte); // Send first byte
  SPI.transfer(lowByte); // Send second byte

  digitalWrite(PIN_CS, HIGH); // Deselect the SPI device
  SPI.endTransaction(); // End SPI transaction
}