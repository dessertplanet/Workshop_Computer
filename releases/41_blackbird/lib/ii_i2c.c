#include "ii_i2c.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"

#define II_I2C i2c0
#define II_SDA_PIN 0
#define II_SCL_PIN 1
#define II_BAUD_SLOW 100000
#define II_BAUD_FAST 400000

// A transaction's time limit: its bits at the bus rate (plus start/stop),
// with room for followers that stretch the clock, and at least 5ms
#define II_TIMEOUT_MIN_US 5000u

static ii_i2c_status_t g_status = II_I2C_IDLE;
static uint8_t g_rx_len = 0;
static uint32_t g_start_us = 0;
static uint32_t g_timeout_us = 0;
static uint32_t g_baud = II_BAUD_SLOW;

void ii_i2c_init(void) {
    // ComputerCard's constructor has already initialised I2C0 (for the
    // EEPROM) and moved it to the ii pins; just make sure of the rate
    g_baud = II_BAUD_SLOW;
    i2c_set_baudrate(II_I2C, g_baud);
    g_status = II_I2C_IDLE;
}

bool ii_i2c_idle(void) {
    return g_status == II_I2C_IDLE;
}

void ii_i2c_set_fast(bool fast) {
    g_baud = fast ? II_BAUD_FAST : II_BAUD_SLOW;
    if (g_status == II_I2C_IDLE) {
        i2c_set_baudrate(II_I2C, g_baud);  // (otherwise applied by the next start)
    }
}

void ii_i2c_set_pullups(bool on) {
    gpio_set_pulls(II_SDA_PIN, on, false);
    gpio_set_pulls(II_SCL_PIN, on, false);
}

// Free a bus a follower is holding (SDA stuck low mid-byte): clock SCL by hand
// until it lets go, then send a STOP. As crow does on I2C errors.
static void bus_recover(void) {
    gpio_set_function(II_SDA_PIN, GPIO_FUNC_SIO);
    gpio_set_function(II_SCL_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(II_SDA_PIN, GPIO_IN);
    for (int i = 0; i < 9 && !gpio_get(II_SDA_PIN); i++) {
        // open drain: drive low, or release to the pull-ups
        gpio_set_dir(II_SCL_PIN, GPIO_OUT); gpio_put(II_SCL_PIN, 0); busy_wait_us(5);
        gpio_set_dir(II_SCL_PIN, GPIO_IN);                           busy_wait_us(5);
    }
    // STOP: SDA low -> high while SCL is high
    gpio_set_dir(II_SDA_PIN, GPIO_OUT); gpio_put(II_SDA_PIN, 0); busy_wait_us(5);
    gpio_set_dir(II_SCL_PIN, GPIO_IN);                           busy_wait_us(5);
    gpio_set_dir(II_SDA_PIN, GPIO_IN);                           busy_wait_us(5);
    gpio_set_function(II_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(II_SCL_PIN, GPIO_FUNC_I2C);
}

bool ii_i2c_start(uint8_t address, const uint8_t* tx, uint8_t tx_len, uint8_t rx_len) {
    if (g_status != II_I2C_IDLE) return false;
    if (tx_len + rx_len == 0 || tx_len + rx_len > II_I2C_MAX_BYTES) return false;

    i2c_hw_t* hw = i2c_get_hw(II_I2C);
    if (hw->status & I2C_IC_STATUS_ACTIVITY_BITS) return false;  // still finishing the last one

    // The target address can only change while the controller is disabled
    hw->enable = 0;
    hw->tar = address;
    hw->enable = I2C_IC_ENABLE_ENABLE_BITS;
    (void)hw->clr_tx_abrt;
    (void)hw->clr_stop_det;
    while (hw->rxflr) (void)hw->data_cmd;  // stale read bytes

    // Load the whole transaction: the writes, then a read command per byte
    // (restart before the first), STOP on the very last. If the FIFO ever ran
    // dry before STOP, the controller would hold SCL low, not end early.
    for (uint8_t i = 0; i < tx_len; i++) {
        bool last = (i + 1 == tx_len) && rx_len == 0;
        hw->data_cmd = tx[i] | (last ? I2C_IC_DATA_CMD_STOP_BITS : 0);
    }
    for (uint8_t i = 0; i < rx_len; i++) {
        hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS
                     | ((i == 0 && tx_len) ? I2C_IC_DATA_CMD_RESTART_BITS : 0)
                     | ((i + 1 == rx_len) ? I2C_IC_DATA_CMD_STOP_BITS : 0);
    }

    uint32_t bits = (uint32_t)(tx_len + rx_len + 1) * 9 + 4;
    g_timeout_us = bits * 1000000u / g_baud * 4;
    if (g_timeout_us < II_TIMEOUT_MIN_US) g_timeout_us = II_TIMEOUT_MIN_US;
    g_rx_len = rx_len;
    g_start_us = time_us_32();
    g_status = II_I2C_BUSY;
    return true;
}

ii_i2c_status_t ii_i2c_poll(uint8_t* rx) {
    if (g_status != II_I2C_BUSY) {
        ii_i2c_status_t s = g_status;
        g_status = II_I2C_IDLE;  // report a result once
        return s;
    }
    i2c_hw_t* hw = i2c_get_hw(II_I2C);
    uint32_t raw = hw->raw_intr_stat;

    if (raw & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
        uint32_t source = hw->tx_abrt_source;
        (void)hw->clr_tx_abrt;  // the controller has flushed the FIFO and sent STOP
        (void)hw->clr_stop_det;
        g_status = II_I2C_IDLE;
        if (source & (I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS | I2C_IC_TX_ABRT_SOURCE_ABRT_TXDATA_NOACK_BITS)) {
            return II_I2C_NAK;
        }
        return II_I2C_ERROR;
    }

    if (raw & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) {
        (void)hw->clr_stop_det;
        g_status = II_I2C_IDLE;
        if (hw->rxflr < g_rx_len) return II_I2C_ERROR;
        for (uint8_t i = 0; i < g_rx_len; i++) {
            uint8_t b = (uint8_t)hw->data_cmd;
            if (rx) rx[i] = b;
        }
        return II_I2C_DONE;
    }

    if ((time_us_32() - g_start_us) > g_timeout_us) {
        // Abort (sends STOP if it can), then free the bus if a follower still holds it
        hw->enable = I2C_IC_ENABLE_ENABLE_BITS | I2C_IC_ENABLE_ABORT_BITS;
        uint32_t t0 = time_us_32();
        while ((hw->enable & I2C_IC_ENABLE_ABORT_BITS) && (time_us_32() - t0) < 1000) {}
        (void)hw->clr_tx_abrt;
        (void)hw->clr_stop_det;
        hw->enable = 0;
        bus_recover();
        i2c_init(II_I2C, g_baud);  // reset: the pin toggling may have looked like another leader
        g_status = II_I2C_IDLE;
        return II_I2C_TIMEOUT;
    }
    return II_I2C_BUSY;
}
