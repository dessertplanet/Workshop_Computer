#pragma once

#include <stdint.h>
#include <stdbool.h>

// Metro system for crow emulator
// Based on submodules/crow/lib/metro.h and metro.c

#define MAX_NUM_METROS 8

typedef enum {
    METRO_STATUS_RUNNING,
    METRO_STATUS_STOPPED
} metro_status_t;

typedef struct {
    int ix;                     // metro index
    metro_status_t status;      // running/stopped status
    float seconds;              // period in seconds
    int32_t count;              // number of repeats. <0 is infinite
    int32_t stage;              // number of completed cycles
    uint64_t next_fire_time;    // absolute time when metro should fire next (microseconds)
} metro_t;

// Metro management
void metro_init(void);
void metro_deinit(void);

// Metro control (matching crow's API)
void metro_start(int ix);
void metro_stop(int ix);
void metro_stop_all(void);

// Metro parameters (matching crow's API)
void metro_set_time(int ix, float sec);
void metro_set_count(int ix, int count);
void metro_set_stage(int ix, int stage);

// Processing function - called from ProcessSample at 48kHz
void metro_process_events(void);

// Check if any metros need to fire
bool metro_any_pending(void);
