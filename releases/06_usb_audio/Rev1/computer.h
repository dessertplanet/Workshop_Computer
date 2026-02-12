#ifndef COMPUTER_H
#define COMPUTER_H

// Mux pins
#define MX_A 24
#define MX_B 25

// ADC input pins
#define AUDIO_L_IN_1 26
#define AUDIO_R_IN_1 27
#define MUX_IO_1 28
#define MUX_IO_2 29

#define NUM_LEDS 6
#define LED1 10
#define LED2 11
#define LED3 12
#define LED4 13
#define LED5 14
#define LED6 15
uint leds[NUM_LEDS] = {LED1, LED2, LED3, LED4, LED5, LED6};

#define DAC_CHANNEL_A 0x0000
#define DAC_CHANNEL_B 0x8000

#define DAC_CS 21
#define DAC_SCK 18
#define DAC_TX 19

#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3

#define PULSE_1_RAW_OUT 8
#define PULSE_2_RAW_OUT 9

#define KNOB_MAIN 0
#define KNOB_X 1
#define KNOB_Y 2
#define KNOB_SWITCH 3


#define CV_OUT_1 23
#define CV_OUT_2 22


#define DEBUG_1 0
#define DEBUG_2 1


// Write 12 low bits of 'value' to DAC
void WriteToDAC(int16_t value, uint16_t dacChannel)
{
	spi_get_hw(spi0)->dr = (dacChannel | 0x3000) | (((uint16_t)((value&0x0FFF)+0x800))&0x0FFF);
}

void SetupComputerIO()
{

    // Initialize all LEDs
    for (int i = 0; i < NUM_LEDS; i++)
    {
        gpio_init(leds[i]);
        gpio_set_dir(leds[i], GPIO_OUT);
    }
	
	
    adc_init(); // Initialize the ADC

    // Set ADC pins
    adc_gpio_init(AUDIO_L_IN_1);
    adc_gpio_init(AUDIO_R_IN_1);
    adc_gpio_init(MUX_IO_1);
    adc_gpio_init(MUX_IO_2);

    // Initialize Mux Control pins
    gpio_init(MX_A);
    gpio_init(MX_B);
    gpio_set_dir(MX_A, GPIO_OUT);
    gpio_set_dir(MX_B, GPIO_OUT);
	
	// Initialize pulse out
    gpio_init(PULSE_1_RAW_OUT);
    gpio_set_dir(PULSE_1_RAW_OUT, GPIO_OUT);

    gpio_init(PULSE_2_RAW_OUT);
    gpio_set_dir(PULSE_2_RAW_OUT, GPIO_OUT);
	
    // Initialize pulse in
    gpio_init(PULSE_1_INPUT);
    gpio_set_dir(PULSE_1_INPUT, GPIO_IN);
    gpio_pull_up(PULSE_1_INPUT); // NB Needs pullup to activate transistor on inputs

    gpio_init(PULSE_2_INPUT);
    gpio_set_dir(PULSE_2_INPUT, GPIO_IN);
    gpio_pull_up(PULSE_2_INPUT); // NB: Needs pullup to activate transistor on inputs

	
	// Setup SPI for DAC output
	spi_init(spi0, 15625000);
	spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(DAC_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DAC_TX, GPIO_FUNC_SPI);
    gpio_set_function(DAC_CS, GPIO_FUNC_SPI);

	// Setup CV PWM
	// First, tell the CV pins that the PWM is in charge of the value.
    gpio_set_function(CV_OUT_1, GPIO_FUNC_PWM);
    gpio_set_function(CV_OUT_2, GPIO_FUNC_PWM);

	// now create PWM config struct
    pwm_config config = pwm_get_default_config(); 
	pwm_config_set_wrap(&config, 2047); // 11-bit PWM

	
	// now set this PWM config to apply to the two outputs
	// NB: CV_A and CV_B share the same PWM slice, which means that they share a PWM config
	// They have separate 'gpio_level's (output compare unit) though, so they can have different PWM on-times
	pwm_init(pwm_gpio_to_slice_num(CV_OUT_1), &config, true); // Slice 1, channel A
    pwm_init(pwm_gpio_to_slice_num(CV_OUT_2), &config, true); // slice 1 channel B (redundant to set up again)

	// set initial level to half way (0V)
	pwm_set_gpio_level(CV_OUT_1,1024);
	pwm_set_gpio_level(CV_OUT_2,1024);

	
	// Debug pins
    gpio_init(DEBUG_1);
    gpio_set_dir(DEBUG_1, GPIO_OUT);

    gpio_init(DEBUG_2);
    gpio_set_dir(DEBUG_2, GPIO_OUT);
	

}
#endif
