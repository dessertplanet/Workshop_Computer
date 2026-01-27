/*
USB audio output for Music Thing Modular Workshop Computer

48kHz 16-bit stereo input through USB

12-bit output through both audio out jacks.

Main knob controls volume:
- Knob at 12 o'clock is the default value (0dB amplification).
- This volume and quieter will not introduce clipping 
- Louder volumes may clip but could be useful for boosting quiet sources


Heavily based on GPL code at
  https://github.com/tierneytim/Pico-USB-audio

with modifications only to: 
- use MTM Computer DAC and knob
- to convert to 48kHz stereo.

 */
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/spi.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "usb_audio.h"
#include "computer.h"
#include "pico/multicore.h"


volatile int32_t knobs[4] = {0,0,0,0}; // 0-4095

void Sample()
{
	static volatile uint8_t mxPos = 0; // external multiplexer value
	static volatile int32_t knobssm[4] = {0,0,0,0};

	static int orc=0, olc=0; // clipping indicator counters
	
	uint16_t adc = adc_fifo_get_blocking(); 
	static uint32_t left, right;
   
	if (multicore_fifo_rvalid())
	{
		uint32_t audiodata = multicore_fifo_pop_blocking();
		left=audiodata &0xFFFF;
		right= (audiodata & 0xFFFF0000)>>16;

	}

	int16_t sl = left;
	int16_t sr = right;

	int32_t sl32, sr32;

	int32_t knob2 = (knobs[KNOB_MAIN]*knobs[KNOB_MAIN])>>12;
	sl32 = ((int32_t)sl*knob2)>>14;
	sr32 = ((int32_t)sr*knob2)>>14;

	if (sl32>2047) {sl32=2047; olc=4800;}
	if (sr32>2047) {sr32=2047; orc=4800;}
	if (sl32<-2047) {sl32=-2047; olc=4800;}
	if (sr32<-2047) {sr32=-2047; orc=4800;}

	sl=sl32;
	sr=sr32;
	
	int16_t aleft = sl;
	if (aleft<0) aleft=-aleft;
	int16_t aright = sr;
	if (aright<0) aright=-aright;


	// Display VU meter
	gpio_put(leds[4], aleft>128);
	gpio_put(leds[2], aleft>512);
	gpio_put(leds[0], olc>0);
	gpio_put(leds[5], aright>128);
	gpio_put(leds[3], aright>512);
	gpio_put(leds[1], orc>0);

	
	WriteToDAC(sl, DAC_CHANNEL_A);
	WriteToDAC(sr, DAC_CHANNEL_B);

	
	// (untested) best attempt at correction of DNL errors in ADC
	uint16_t adc512=adc+512;
	if (!(adc512 % 0x01FF)) adc += 4;
	adc -= (adc512>>10) << 3;

	switch(adc_get_selected_input()) 
	{
	case 0: // CVs (12kHz)
		break;
		
	case 1: // Audio in R  (12kHz)
		break;
		
	case 2: // Audio in L  (12kHz)
		mxPos=(mxPos+1)&0x03;
		break;
		
	case 3: // Knobs (12kHz)
		// Each knob sampled at 12kHz/4 = 3kHz
		// Then IIR filter with time constant ~32 samples, so around 100Hz
		int knob=(mxPos+3)&0x03; // why??
		knobssm[knob]=(31*(knobssm[knob])+16*adc)>>5;
		knobs[knob]=knobssm[knob]>>4;
		break;
	}	

	if (orc) orc--;
	if (olc) olc--;

}
void core1_worker() {

	// Set up interrupt on ADC sample received,
	// as this gives low jitter
	adc_set_round_robin(0x0F);
	adc_fifo_setup(true, true, 1, false, false);
	adc_set_clkdiv(8*125 - 1); // 48MHz / (2*125) = 192Hz interrupt
	irq_set_exclusive_handler(ADC_IRQ_FIFO, Sample);
	adc_irq_set_enabled(true);
	irq_set_enabled(ADC_IRQ_FIFO, true);
	adc_run(true);

	
    // Infinite While Loop to wait for interrupt
    while (1){
    }
}

int main(void) {
	SetupComputerIO();
    
    multicore_launch_core1(core1_worker);
    sleep_ms(100);
	usb_sound_card_init();
    
    while (1){};
}
