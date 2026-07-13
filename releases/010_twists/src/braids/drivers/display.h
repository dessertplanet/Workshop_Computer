#ifndef BRAIDS_DRIVERS_DISPLAY_H_
#define BRAIDS_DRIVERS_DISPLAY_H_

#include "stmlib/stmlib.h"
#include "hardware/gpio.h"

#define PIN_LED1_OUT        10
#define PIN_MASK_LEDS       0xFC00

namespace braids {

class Display {
 public:
  Display() { }
  ~Display() { }
  
  void Init();
  void SetBits(uint8_t values);
  
  DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace braids

#endif  // BRAIDS_DRIVERS_DISPLAY_H_
