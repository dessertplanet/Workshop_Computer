#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "computer.h"

void Computer::init() {
    initLEDs();
    initPulse();
    initCVOut();
    initMux();
    initAudioIn();
    initAudioOut();
    calibration_data = readCalibration();
    cv_in1_calibrated_helper = (12.0f / 4096.0f) * (float)calibration_data.cv_in1;
    cv_in2_calibrated_helper = (12.0f / 4096.0f) * (float)calibration_data.cv_in2;
    cv_out1_calibrated_helper = (2048.0f - (float)calibration_data.cv_out1) / 6.16f;
    cv_out2_calibrated_helper = (2048.0f - (float)calibration_data.cv_out2) / 6.16f;

    initVersion();
    readVersion();
}

void Computer::poll() {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // check if we should end any timed pulses
    for(int i=0; i<2; i++) {
        if(timed_pulse_out[i].enabled && now - timed_pulse_out[i].start_time >= timed_pulse_out[i].length) {
            timed_pulse_out[i].enabled = false;
            gpio_put(PIN_PULSE1_OUT + i, true); // inverted
        }
    }
}

#pragma region HardwareVersion
void Computer::initVersion() {
    gpio_init(PIN_VER1);
    gpio_set_dir(PIN_VER1, GPIO_IN);
    gpio_init(PIN_VER2);
    gpio_set_dir(PIN_VER2, GPIO_IN);
    gpio_init(PIN_VER3);
    gpio_set_dir(PIN_VER3, GPIO_IN);
}

void Computer::readVersion() {
    board_version = (gpio_get(PIN_VER3) << 2) | (gpio_get(PIN_VER2) << 1) | gpio_get(PIN_VER1);
}
#pragma endregion HardwareVersion

#pragma region LEDs
void Computer::initLEDs() {
    for(int i=PIN_LED1_OUT; i<PIN_LED1_OUT+6; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
    }
    setAllLEDs(false);
}

// Set all LEDs on or off
void Computer::setAllLEDs(bool value) {
    uint32_t set = value ? PIN_MASK_LEDS : 0;
    sleep_us(1);
    gpio_put_masked(PIN_MASK_LEDS, set);
    sleep_us(1);
}

// Set LED bits (e.g. LEDs 1 & 3 = 5)
void Computer::setLEDs(uint8_t values) {
    gpio_put_masked(PIN_MASK_LEDS, (values & 0x3F) << PIN_LED1_OUT);
}

// Set a single LED on or off (1 based)
void Computer::setLED(uint8_t led, bool value) {
    gpio_put(PIN_LED1_OUT + led - 1, value);
}
#pragma endregion

#pragma region Pulse
void Computer::initPulse() {
    // Pulse In pullup
    gpio_init(PIN_PULSE1_IN);
    gpio_set_dir(PIN_PULSE1_IN, GPIO_IN);
    gpio_pull_up(PIN_PULSE1_IN);

    gpio_init(PIN_PULSE2_IN);
    gpio_set_dir(PIN_PULSE2_IN, GPIO_IN);
    gpio_pull_up(PIN_PULSE2_IN);

    // Pulse Out
    gpio_init(PIN_PULSE1_OUT);
    gpio_set_dir(PIN_PULSE1_OUT, GPIO_OUT);
    gpio_put(PIN_PULSE1_OUT, true);

    gpio_init(PIN_PULSE2_OUT);
    gpio_set_dir(PIN_PULSE2_OUT, GPIO_OUT);
    gpio_put(PIN_PULSE2_OUT, true);

    for(int i=0; i<2; i++) {
        timed_pulse_out[i].enabled = false;
        timed_pulse_out[i].start_time = 0;
        timed_pulse_out[i].length = 0;
    }
}

// Get the value of Pulse In 1 or 2
bool Computer::getPulse(uint8_t output) {
    if(output < 1 || output > 2) { 
        return false;
    }
    return !gpio_get(PIN_PULSE1_IN + output - 1);
}

// Set the value of Pulse Out 1 or 2
void Computer::setPulse(uint8_t output, bool value) {
    if(output < 1 || output > 2) { 
        return;
    }

    // Pulse outputs are inverted
    gpio_put(PIN_PULSE1_OUT + output - 1, !value);
}

void Computer::setPulseCallback(uint8_t input, gpio_irq_callback_t callback) {
    uint gpio = input == 1 ? PIN_PULSE1_IN : PIN_PULSE2_IN;
    gpio_set_irq_enabled_with_callback(gpio, GPIO_IRQ_EDGE_FALL, true, callback);
}

// Output Pulse 1 or 2 for length_ms milliseconds
void Computer::setTimedPulse(uint8_t output, uint32_t length_ms) {
    uint8_t idx = output - 1;
    timed_pulse_out[idx].enabled = true;
    timed_pulse_out[idx].length = length_ms;
    timed_pulse_out[idx].start_time = to_ms_since_boot(get_absolute_time());
    gpio_put(PIN_PULSE1_OUT + idx, false); // inverted
}
#pragma endregion Pulse

#pragma region CVOut
void Computer::initCVOut() {
    gpio_set_function(PIN_CV1_OUT, GPIO_FUNC_PWM);
    gpio_set_function(PIN_CV2_OUT, GPIO_FUNC_PWM);

    cv_out1_slice = pwm_gpio_to_slice_num(PIN_CV1_OUT);
    cv_out1_channel = pwm_gpio_to_channel(PIN_CV1_OUT);
    cv_out2_slice = pwm_gpio_to_slice_num(PIN_CV2_OUT);
    cv_out2_channel = pwm_gpio_to_channel(PIN_CV2_OUT);

    pwm_set_wrap(cv_out1_slice, 2047); // 11 bit 60kHz
    pwm_set_wrap(cv_out2_slice, 2047);

    pwm_set_enabled(cv_out1_slice, true);
    pwm_set_enabled(cv_out2_slice, true);
}

void Computer::setCVOut(uint8_t output, uint16_t value) {
    if(board_version > 0) {
        value = 2047 - value;
    }

    if(output == 1) {
        pwm_set_chan_level(cv_out1_slice, cv_out1_channel, value);
    } else if(output == 2) {
        pwm_set_chan_level(cv_out2_slice, cv_out2_channel, value);
    }
}

void Computer::setCVOutVolts(uint8_t output, float volts) {
    if(output == 1) {
        setCVOut(output, calibration_data.cv_out1 + (volts * cv_out1_calibrated_helper));
    } else if(output == 2) {
        setCVOut(output, calibration_data.cv_out2 + (volts * cv_out2_calibrated_helper));
    }
}

#pragma endregion

#pragma region MUX
void Computer::initMux() {
    gpio_init(PIN_MUX_LOGIC_A);
    gpio_set_dir(PIN_MUX_LOGIC_A, GPIO_OUT);
    gpio_init(PIN_MUX_LOGIC_B);
    gpio_set_dir(PIN_MUX_LOGIC_B, GPIO_OUT);

    adc_init();
    adc_gpio_init(PIN_MUX_OUT_X);
    adc_gpio_init(PIN_MUX_OUT_Y);
}

/*
MUX Logic to Channel
b a
0 0 = x0 y0
0 1 = x1 y1
1 0 = x2 y2
1 1 = x3 y3
*/
// bank: x = 0, y = 1
// chan: 0 - 3
uint16_t Computer::getMuxValue(uint8_t bank, uint8_t chan) {
    bool logic_a = (chan & 1) ? true : false;
    bool logic_b = (chan >> 1) ? true : false;   
    gpio_put(PIN_MUX_LOGIC_A, logic_a);
    gpio_put(PIN_MUX_LOGIC_B, logic_b);
    
    sleep_us(10);
    adc_select_input(bank + 2);
    return adc_read();
}

// Get the current switch state
ComputerSwitchState Computer::getSwitchState() {
    // Switch = pin 11 x3
    uint16_t switch_value = getMuxValue(0, 3);

    if(switch_value > 3000) {
        return ComputerSwitchState::UP;
    } else if(switch_value < 1000) {
        return ComputerSwitchState::DOWN;
    } else {
        return ComputerSwitchState::MID;
    }
}

// ~ 0 to 4095
uint16_t Computer::getPotXValue() {
    // Pot x = pin 12 x0    
    return getMuxValue(0, 1);
}
uint16_t Computer::getPotYValue() {
    // Pot y = pin 14 x1
    return getMuxValue(0, 2);
}
uint16_t Computer::getPotZValue() {
    // Pot z = pin 15 x2
    return getMuxValue(0, 0);
}

uint16_t Computer::getCVIn1Value() {
    // CV 1 in = pin 1 & 2 y0, y2
    return getMuxValue(1, 0);
}
uint16_t Computer::getCVIn2Value() {
    // CV 2 in = pin 4 & 5 y3, y1
    return getMuxValue(1, 1);
}

float Computer::getCVInVolts(uint8_t input) {
    float n = -0.0029296875; //0 - (12/4096)
    float result = 0;
    if(input == 1) {
        result = (n * averageCVInput(input)) + cv_in1_calibrated_helper;
        return result;
    } else if(input == 2) {
        result = (n * averageCVInput(input)) + cv_in2_calibrated_helper;
    }
    return result;
}

#pragma endregion MUX

#pragma region AudioIn
void Computer::initAudioIn() {
    adc_init();
    adc_gpio_init(PIN_AUDIO_IN1);
    adc_gpio_init(PIN_AUDIO_IN2);
}

uint16_t Computer::getAudioIn(uint8_t input) {
    adc_select_input(input == 1 ? 1 : 0);
    return adc_read();
}
#pragma endregion

#pragma region AudioOut

void Computer::initAudioOut() {
    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(DAC_SPI_PORT, 20000000);
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(DAC_SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_LSB_FIRST);

    // Map SPI signals to GPIO ports    
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // Init to 0v
    setAudioOut(1, 1024);
    setAudioOut(2, 1024);
}

void Computer::setAudioOut(uint8_t output, uint16_t value) {
    uint16_t DAC_data = (output == 1 ? DAC_config_chan_A_gain : DAC_config_chan_B_gain) | ((value + CALIBRATE_DAC_OUT) & 0xffff);
    spi_write16_blocking(DAC_SPI_PORT, &DAC_data, 1);
}
#pragma endregion

#pragma region Calibration
CalibrationData Computer::readCalibration() {
    CalibrationData result;

    uint8_t *p = (uint8_t *)(XIP_BASE + CALIBRATION_FLASH_START);
    uint8_t first_byte = *p;

    // if we don't have calibration saved, use defaults
    if(first_byte != CALIBRATION_FIRST_BYTE) {
        result.cv_in1 = CALIBRATION_DEFAULT_CV_IN;
        result.cv_in2 = CALIBRATION_DEFAULT_CV_IN;
        result.cv_out1 = CALIBRATION_DEFAULT_CV_OUT;
        result.cv_out2 = CALIBRATION_DEFAULT_CV_OUT;
    }
    else
    {
        p++;
        memcpy(&result.cv_in1, p, sizeof(result.cv_in1));
        p += sizeof(result.cv_in1);
        memcpy(&result.cv_in2, p, sizeof(result.cv_in2));
        p += sizeof(result.cv_in2);
        memcpy(&result.cv_out1, p, sizeof(result.cv_out1));
        p += sizeof(result.cv_out1);
        memcpy(&result.cv_out2, p, sizeof(result.cv_out2));
    }

    return result;
}

void Computer::writeCalibration() {
    // if the settings match whats in the flash, don't save
    CalibrationData stored_data = readCalibration();
    if(calibration_data.equals(stored_data)) {
        return;
    }
    
    uint8_t buffer[FLASH_SECTOR_SIZE];
    buffer[0] = CALIBRATION_FIRST_BYTE;
    uint8_t *p = buffer + 1;
    memcpy(p, &calibration_data.cv_in1, sizeof(calibration_data.cv_in1));
    p += sizeof(calibration_data.cv_in1);
    memcpy(p, &calibration_data.cv_in2, sizeof(calibration_data.cv_in2));
    p += sizeof(calibration_data.cv_in2);
    memcpy(p, &calibration_data.cv_out1, sizeof(calibration_data.cv_out1));
    p += sizeof(calibration_data.cv_out1);
    memcpy(p, &calibration_data.cv_out2, sizeof(calibration_data.cv_out2));

    uint32_t irqs = save_and_disable_interrupts();
    flash_range_erase(CALIBRATION_FLASH_START, FLASH_SECTOR_SIZE);
    flash_range_program(CALIBRATION_FLASH_START, buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(irqs);
}

uint16_t Computer::averageCVInput(uint8_t output) {
    int16_t num_samples = 100;
    uint32_t total = 0;
    for(int i=0; i<num_samples; i++) {
        if(output == 1) {
            total += getCVIn1Value();
        } else if(output == 2) {
            total += getCVIn2Value();
        }
    }

    return total / num_samples;
}

void Computer::calibrateIfSwitchDown() {
    
    ComputerSwitchState switch_state = getSwitchState();
    if(switch_state == ComputerSwitchState::DOWN) {
        // get cv in averages with nothing connected
        uint16_t nc1 = averageCVInput(1);
        uint16_t nc2 = averageCVInput(2);

        // all leds on, wait for switch up and then down again
        setAllLEDs(true);
        while(getSwitchState() == ComputerSwitchState::DOWN);
        while(getSwitchState() != ComputerSwitchState::DOWN);

        // set ideal 0v out
        setCVOut(1, CALIBRATION_DEFAULT_CV_OUT);
        setCVOut(2, CALIBRATION_DEFAULT_CV_OUT);
        sleep_ms(1000);
        uint16_t ideal_out_zero1 = averageCVInput(1);
        uint16_t ideal_out_zero2 = averageCVInput(2);

        calibration_data.cv_in1 = nc1;
        calibration_data.cv_in2 = nc2;
        calibration_data.cv_out1 = CALIBRATION_DEFAULT_CV_OUT * ((float)ideal_out_zero1 / (float)nc1);
        calibration_data.cv_out2 = CALIBRATION_DEFAULT_CV_OUT * ((float)ideal_out_zero2 / (float)nc2);
        writeCalibration();

        setAllLEDs(false);
    }
}
#pragma endregion
