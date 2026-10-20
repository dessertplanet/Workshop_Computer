#pragma once

// ii leader on I2C0 (the UART pads, see COMPUTERCARD_II_ON_UART_PINS in
// ComputerCard.h). Non-blocking: a transaction (write, then optionally a
// read) fits in the controller's 16-entry FIFOs, so it's loaded in one go
// and the main loop polls for the end. Nothing here ever waits on the bus.
// Core 0 only.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define II_I2C_MAX_BYTES 16  // write + read bytes per transaction (FIFO depth)

typedef enum {
    II_I2C_IDLE,     // nothing in flight: ii_i2c_start() can go
    II_I2C_BUSY,     // in flight
    II_I2C_DONE,     // finished; any read bytes are in the buffer passed to poll
    II_I2C_NAK,      // nobody answered at that address (or a data byte wasn't acked)
    II_I2C_TIMEOUT,  // didn't finish in time (stuck bus, or a follower holding SCL): aborted
    II_I2C_ERROR,    // other abort (arbitration lost, ...)
} ii_i2c_status_t;

void ii_i2c_init(void);

// Start a transaction to 7-bit `address`: write `tx_len` bytes, then read
// `rx_len` (0 for none). Returns false if busy or the lengths don't fit.
bool ii_i2c_start(uint8_t address, const uint8_t* tx, uint8_t tx_len, uint8_t rx_len);

// Advance the transaction in flight. DONE/NAK/TIMEOUT/ERROR are each returned
// once, then the driver is IDLE again. On DONE, read bytes go to `rx`.
ii_i2c_status_t ii_i2c_poll(uint8_t* rx);

bool ii_i2c_idle(void);

void ii_i2c_set_fast(bool fast);      // 400kHz, else 100kHz (crow's default)
void ii_i2c_set_pullups(bool on);     // the RP2040's weak internal pull-ups

#ifdef __cplusplus
}
#endif
