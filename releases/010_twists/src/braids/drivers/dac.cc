#include "braids/drivers/dac.h"

#include <string.h>

namespace braids {
  
void Dac::Init() {
  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(DAC_SPI_PORT, 20000000);
  // Format (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(DAC_SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_LSB_FIRST);

  // Map SPI signals to GPIO ports    
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
}

}  // namespace braids
