#include "crow_metro.h"
#include "crow_lua.h"
#include "pico/time.h"
#include <cstdio>
#include <cstring>

static metro_t metros[MAX_NUM_METROS];
static bool metro_initialized = false;

void metro_init(void) {
    if (metro_initialized) {
        return;
    }
    
    printf("Initializing metro system (%d metros)...\n", MAX_NUM_METROS);
    
    for (int i = 0; i < MAX_NUM_METROS; i++) {
        metros[i].ix = i;
        metros[i].status = METRO_STATUS_STOPPED;
        metros[i].seconds = 1.0f;
        metros[i].count = -1;  // infinite by default
        metros[i].stage = 0;
        metros[i].next_fire_time = 0;
    }
    
    metro_initialized = true;
    printf("Metro system initialized\n");
}

void metro_deinit(void) {
    if (!metro_initialized) {
        return;
    }
    
    metro_stop_all();
    metro_initialized = false;
}

void metro_start(int ix) {
    if (!metro_initialized || ix < 0 || ix >= MAX_NUM_METROS) {
        printf("metro_start: bad index %d\n", ix);
        return;
    }
    
    metro_t* m = &metros[ix];
    m->status = METRO_STATUS_RUNNING;
    
    // Schedule first fire time
    uint64_t current_time = time_us_64();
    m->next_fire_time = current_time + (uint64_t)(m->seconds * 1000000.0f);
    
    printf("Metro %d started: period=%.3fs, count=%d\n", ix, m->seconds, m->count);
}

void metro_stop(int ix) {
    if (!metro_initialized || ix < 0 || ix >= MAX_NUM_METROS) {
        printf("metro_stop: bad index %d\n", ix);
        return;
    }
    
    metro_t* m = &metros[ix];
    if (m->status == METRO_STATUS_RUNNING) {
        m->status = METRO_STATUS_STOPPED;
        printf("Metro %d stopped at stage %d\n", ix, m->stage);
    }
}

void metro_stop_all(void) {
    if (!metro_initialized) {
        return;
    }
    
    for (int i = 0; i < MAX_NUM_METROS; i++) {
        metro_stop(i);
    }
    printf("All metros stopped\n");
}

void metro_set_time(int ix, float sec) {
    if (!metro_initialized || ix < 0 || ix >= MAX_NUM_METROS) {
        printf("metro_set_time: bad index %d\n", ix);
        return;
    }
    
    // Limit to 500uS minimum to avoid crash (matching crow behavior)
    if (sec < 0.0005f) {
        sec = 0.0005f;
    }
    
    metros[ix].seconds = sec;
    printf("Metro %d time set to %.6fs\n", ix, sec);
}

void metro_set_count(int ix, int count) {
    if (!metro_initialized || ix < 0 || ix >= MAX_NUM_METROS) {
        printf("metro_set_count: bad index %d\n", ix);
        return;
    }
    
    metros[ix].count = count;
}

void metro_set_stage(int ix, int stage) {
    if (!metro_initialized || ix < 0 || ix >= MAX_NUM_METROS) {
        printf("metro_set_stage: bad index %d\n", ix);
        return;
    }
    
    metros[ix].stage = stage;
}

static void metro_bang(int ix) {
    if (!metro_initialized || ix < 0 || ix >= MAX_NUM_METROS) {
        return;
    }
    
    metro_t* m = &metros[ix];
    
    // Call lua metro handler (equivalent to L_queue_metro in crow)
    // This matches crow's behavior: metro_handler(id, stage) where both are 1-indexed
    if (g_crow_lua) {
        g_crow_lua->call_metro_handler(ix + 1, m->stage + 1);  // convert to 1-based indexing for lua
    }
    
    // Update stage counter
    m->stage++;
    if (m->stage == 0x7FFFFFFF) {
        m->stage = 0x7FFFFFFE;  // prevent overflow (matching crow behavior)
    }
    
    // Check if we should stop (finite count metros)
    if (m->count >= 0 && m->stage > m->count) {
        metro_stop(ix);
        return;
    }
    
    // Schedule next fire time if still running
    if (m->status == METRO_STATUS_RUNNING) {
        m->next_fire_time += (uint64_t)(m->seconds * 1000000.0f);
    }
}

void metro_process_events(void) {
    if (!metro_initialized) {
        return;
    }
    
    uint64_t current_time = time_us_64();
    
    // Check all running metros for firing
    for (int i = 0; i < MAX_NUM_METROS; i++) {
        metro_t* m = &metros[i];
        
        if (m->status == METRO_STATUS_RUNNING && current_time >= m->next_fire_time) {
            metro_bang(i);
        }
    }
}

bool metro_any_pending(void) {
    if (!metro_initialized) {
        return false;
    }
    
    uint64_t current_time = time_us_64();
    
    for (int i = 0; i < MAX_NUM_METROS; i++) {
        metro_t* m = &metros[i];
        if (m->status == METRO_STATUS_RUNNING && current_time >= m->next_fire_time) {
            return true;
        }
    }
    
    return false;
}
