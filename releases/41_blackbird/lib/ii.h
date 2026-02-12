#pragma once

#include <stdint.h>

// Inter-IC (I2C) communication stubs for RP2040

typedef enum {
    ii_mode_none = 0,
    ii_mode_leader_tx,
    ii_mode_leader_rx,
    ii_mode_follower
} ii_mode_t;

void ii_init(void);
void ii_deinit(void);

void ii_set_address(uint8_t address);
uint8_t ii_get_address(void);

void ii_leader_tx(uint8_t address, uint8_t* data, uint8_t count);
void ii_leader_rx(uint8_t address, uint8_t count);

void ii_follower_start(uint8_t address);
void ii_follower_stop(void);

void ii_process_leader(void);
void ii_process_follower(void);

uint8_t ii_tx_now(uint8_t address, uint8_t* data, uint8_t count);
uint8_t ii_rx_now(uint8_t address, uint8_t* data, uint8_t count);

// Additional functions for crow compatibility (stubs for RP2040)
const char* ii_list_modules(void);
const char* ii_list_cmds(uint8_t address);
void ii_set_pullups(uint8_t state);
uint8_t ii_leader_enqueue(uint8_t address, uint8_t cmd, float* data);
uint8_t ii_leader_enqueue_bytes(uint8_t address, uint8_t* data, uint8_t len, uint8_t rx_len);
