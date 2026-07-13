#include "braids/drivers/display.h"

#include "braids/resources.h"

namespace braids {

void Display::Init() {
  for(int i=PIN_LED1_OUT; i<PIN_LED1_OUT+6; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_OUT);
  }
  SetBits(0);
}

void Display::SetBits(uint8_t values) {
  gpio_put_masked(PIN_MASK_LEDS, (values & 0x3F) << PIN_LED1_OUT);
}

}  // namespace braids
