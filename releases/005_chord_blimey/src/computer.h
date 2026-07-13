#ifndef COMPUTER_H
#define COMPUTER_H

#include "pico/stdlib.h"

#define CALIBRATION_FLASH_START     (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CALIBRATION_FIRST_BYTE      0xCC
#define CALIBRATION_DEFAULT_CV_IN   2048
#define CALIBRATION_DEFAULT_CV_OUT  1024
#define CALIBRATE_DAC_OUT   -10

#define PIN_PULSE1_IN       2
#define PIN_PULSE2_IN       3
#define PIN_VER1            5
#define PIN_VER2            6
#define PIN_VER3            7
#define PIN_PULSE1_OUT      8
#define PIN_PULSE2_OUT      9
#define PIN_LED1_OUT        10
#define PIN_SCK             18
#define PIN_MOSI            19
#define PIN_CS              21
#define PIN_CV2_OUT         22
#define PIN_CV1_OUT         23
#define PIN_MUX_LOGIC_A     24
#define PIN_MUX_LOGIC_B     25
#define PIN_AUDIO_IN2       26
#define PIN_AUDIO_IN1       27
#define PIN_MUX_OUT_X       28
#define PIN_MUX_OUT_Y       29

#define DAC_SPI_PORT            spi0
#define DAC_config_chan_A_gain  0b0001000000000000  //  A-channel, 2x, active -- NB 2x = ~3.27v max, not 4.096
#define DAC_config_chan_B_gain  0b1001000000000000  // B-channel, 1x, active

#define PIN_MASK_LEDS       0xFC00

enum class ComputerSwitchState {
    UNKNOWN = 0,
    UP = 1,
    MID = 2,
    DOWN = 3
};

struct TimedPulse {
    bool enabled = false;
    uint32_t start_time = 0;
    uint32_t length = 0;
};

struct CalibrationData {
    uint16_t cv_in1;
    uint16_t cv_in2;
    uint16_t cv_out1;
    uint16_t cv_out2;

    bool equals(CalibrationData other) {
        return cv_in1 == other.cv_in1
            && cv_in2 == other.cv_in2
            && cv_out1 == other.cv_out1
            && cv_out2 == other.cv_out2;
    }
};

class Computer {
    public:
        void init();
        void poll();

        void setAllLEDs(bool value);
        void setLEDs(uint8_t values);
        void setLED(uint8_t led, bool value);

        bool getPulse(uint8_t output);
        void setPulse(uint8_t output, bool value);
        void setPulseCallback(uint8_t input, gpio_irq_callback_t callback);
        void setTimedPulse(uint8_t output, uint32_t length_ms);
        
        void setCVOut(uint8_t output, uint16_t value);
        void setCVOutVolts(uint8_t output, float volts);

        ComputerSwitchState getSwitchState();
        uint16_t getPotXValue();
        uint16_t getPotYValue();
        uint16_t getPotZValue();
        uint16_t getCVIn1Value();
        uint16_t getCVIn2Value();
        float getCVInVolts(uint8_t input);

        uint16_t getAudioIn(uint8_t input);
        void setAudioOut(uint8_t output, uint16_t value);

        void calibrateIfSwitchDown();

    private:
        uint8_t board_version;
        uint cv_out1_slice;
        uint cv_out1_channel;
        uint cv_out2_slice;
        uint cv_out2_channel;

        float cv_out1_calibrated_helper;
        float cv_out2_calibrated_helper;
        float cv_in1_calibrated_helper;
        float cv_in2_calibrated_helper;

        CalibrationData calibration_data;

        TimedPulse timed_pulse_out[2];

        void initVersion();
        void initPulse();
        void initMux();
        void initLEDs();
        void initCVOut();
        void initAudioIn();
        void initAudioOut();

        void readVersion();
        uint16_t getMuxValue(uint8_t bank, uint8_t chan);

        void writeCalibration();
        CalibrationData readCalibration();
        uint16_t averageCVInput(uint8_t output);
};

#endif