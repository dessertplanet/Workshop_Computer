#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// Debug categories - can be selectively enabled/disabled
// #define DEBUG_LOCKFREE    1  // Lock-free queue operations
// #define DEBUG_CONTROL     1  // Control messages and voltage changes  
// #define DEBUG_BOOT        1  // Startup and system messages
// #define DEBUG_DETECT      1  // Input detection system

// Debug macros that compile to nothing when disabled
#ifdef DEBUG_LOCKFREE
    #define DEBUG_LF_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_LF_PRINT(...) ((void)0)
#endif

#ifdef DEBUG_CONTROL
    #define DEBUG_CTRL_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_CTRL_PRINT(...) ((void)0)
#endif

#ifdef DEBUG_BOOT
    #define DEBUG_BOOT_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_BOOT_PRINT(...) ((void)0)
#endif

#ifdef DEBUG_DETECT
    #define DEBUG_DETECT_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_DETECT_PRINT(...) ((void)0)
#endif

// Performance-critical paths should NEVER have debug output
// These macros are always disabled in production
#define DEBUG_AUDIO_PRINT(...) ((void)0)
#define DEBUG_TIMING_PRINT(...) ((void)0)

#endif // DEBUG_H
