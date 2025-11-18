#pragma once

// Central definition for the audio ISR rate used across the firmware.
// Keep everything derived from these macros so future sample-rate changes
// only require editing this file.
#define PROCESS_SAMPLE_RATE_HZ        8000.0f
#define PROCESS_SAMPLE_RATE_HZ_DOUBLE 8000.0
#define PROCESS_SAMPLE_RATE_HZ_INT    8000
#define PROCESS_SAMPLE_PERIOD_US      (1000000.0f / PROCESS_SAMPLE_RATE_HZ)
