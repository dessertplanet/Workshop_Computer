#ifndef BRAIDS_DRIVERS_DAC_H_
#define BRAIDS_DRIVERS_DAC_H_

#include "stmlib/stmlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#define PIN_SCK             18
#define PIN_MOSI            19
#define PIN_CS              21

#define DAC_SPI_PORT            spi0
#define DAC_config_chan_A_gain  0b0001000000000000  //  A-channel, 2x, active -- NB 2x = ~3.27v max, not 4.096
#define DAC_config_chan_B_gain  0b1001000000000000  // B-channel, 1x, active

namespace braids {

class Dac {
 public:
  Dac() { }
  ~Dac() { }
  
  void Init();
  inline void Write(uint16_t value) {
    uint16_t DAC_data = DAC_config_chan_A_gain | (value & 0x0fff);
    spi_write16_blocking(DAC_SPI_PORT, &DAC_data, 1);
  }
 
 private:
  
  DISALLOW_COPY_AND_ASSIGN(Dac);
};

}  // namespace braids

#endif  // BRAIDS_DRIVERS_DAC_H_
