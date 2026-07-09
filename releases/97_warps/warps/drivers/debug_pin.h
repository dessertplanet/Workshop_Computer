// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Driver for the debug (timing) pin.

#ifndef WARPS_DRIVERS_DEBUG_PIN_H_
#define WARPS_DRIVERS_DEBUG_PIN_H_

#include "stmlib/stmlib.h"

// Provide platform-specific implementations.
// RP2040/Pico SDK path
#if defined(PICO_ON_DEVICE)
#include "pico/stdlib.h"
#ifndef WARPS_DEBUG_GPIO
#define WARPS_DEBUG_GPIO PICO_DEFAULT_LED_PIN
#endif

#elif !defined(TEST)
// Original STM32F4 path used on Mutable hardware
#include <stm32f4xx_conf.h>
#endif

namespace warps {

class DebugPin {
 public:
  DebugPin() { }
  ~DebugPin() { }

#if defined(PICO_ON_DEVICE)
  static void Init() {
    gpio_init(WARPS_DEBUG_GPIO);
    gpio_set_dir(WARPS_DEBUG_GPIO, true);
    gpio_put(WARPS_DEBUG_GPIO, 0);
  }
  static inline void High() { gpio_put(WARPS_DEBUG_GPIO, 1); }
  static inline void Low()  { gpio_put(WARPS_DEBUG_GPIO, 0); }

#elif defined(TEST)
  // Test builds: keep explicit functions but still do nothing. Tests should
  // assert calls rather than rely on GPIO side-effects.
  static void Init() { }
  static void High() { }
  static void Low() { }

#else
  // STM32F4 (original Mutable hardware)
  static void Init() {
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef gpio_init;
    gpio_init.GPIO_Pin = GPIO_Pin_9;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &gpio_init);
  }
  static inline void High() { GPIOA->BSRRL = GPIO_Pin_9; }
  static inline void Low()  { GPIOA->BSRRH = GPIO_Pin_9; }
#endif

 private:
  DISALLOW_COPY_AND_ASSIGN(DebugPin);
};

#define TIC DebugPin::High();
#define TOC DebugPin::Low();

}  // namespace warps

#endif  // WARPS_DRIVERS_DEBUG_PIN_H_
