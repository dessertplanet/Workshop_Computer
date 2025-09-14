#pragma once

#ifndef CROW_CASL_H
#define CROW_CASL_H

#include <stdbool.h>
#include <stdint.h>

// Lua integration for table parsing - wrap for proper C linkage
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// Our existing slopes system integration
#include "crow_slopes.h"

// CASL Constants - matching crow's architecture
#define CROW_CASL_TO_COUNT   16   // 28 bytes each
#define CROW_CASL_SEQ_COUNT  8    // 16 bytes each  
#define CROW_CASL_SEQ_LENGTH 8    // 4 bytes each
#define CROW_CASL_DYN_COUNT  40   // 8 bytes each
#define CROW_CASL_CHANNELS   4    // One CASL instance per output

// Control flow types for To statements
typedef enum {
    CROW_CASL_TO_LITERAL,
    CROW_CASL_TO_RECUR,
    CROW_CASL_TO_IF,
    CROW_CASL_TO_ENTER,
    CROW_CASL_TO_HELD,
    CROW_CASL_TO_WAIT,
    CROW_CASL_TO_UNHELD,
    CROW_CASL_TO_LOCK,
    CROW_CASL_TO_OPEN
} crow_casl_to_control_t;

// Element object union - holds different data types
typedef union {
    float f;                    // Float value
    int dyn;                    // Index into dynamics array
    uint16_t var[2];            // 2 indexes into dynamic table for operations
    int seq;                    // Reference to a Sequence object
    crow_shape_t shape;   // Shape function for envelopes
} crow_casl_elem_obj_t;

// Element types for behavioral expressions
typedef enum {
    CROW_CASL_ELEM_FLOAT,
    CROW_CASL_ELEM_SHAPE,
    CROW_CASL_ELEM_DYNAMIC,
    CROW_CASL_ELEM_MUTABLE,
    // Arithmetic operations
    CROW_CASL_ELEM_NEGATE,
    CROW_CASL_ELEM_ADD,
    CROW_CASL_ELEM_SUB,
    CROW_CASL_ELEM_MUL,
    CROW_CASL_ELEM_DIV,
    CROW_CASL_ELEM_MOD,
    CROW_CASL_ELEM_MUTATE
} crow_casl_elem_type_t;

// Element structure - combines type and value
typedef struct {
    crow_casl_elem_obj_t obj;
    crow_casl_elem_type_t type;
} crow_casl_elem_t;

// To statement - basic building block of CASL sequences
typedef struct {
    crow_casl_elem_t a;              // First parameter (usually destination)
    crow_casl_elem_t b;              // Second parameter (usually time)
    crow_casl_elem_t c;              // Third parameter (usually shape)
    crow_casl_to_control_t ctrl;     // Control flow type
} crow_casl_to_t;

// Sequence structure - contains ordered To statements
typedef struct {
    crow_casl_to_t* stage[CROW_CASL_SEQ_LENGTH];  // Array of To statement pointers
    int length;                                    // Number of stages in sequence
    int pc;                                        // Program counter
    int parent;                                    // Parent sequence index (-1 if root)
} crow_casl_sequence_t;

// Main CASL structure - one per output channel
typedef struct {
    // To statement allocation pool
    crow_casl_to_t tos[CROW_CASL_TO_COUNT];
    int to_ix;                                     // Next available To slot

    // Sequence management
    crow_casl_sequence_t* seq_current;            // Currently executing sequence
    crow_casl_sequence_t seqs[CROW_CASL_SEQ_COUNT];
    int seq_ix;                                    // Next available sequence slot
    int seq_select;                                // Current sequence index

    // Dynamic variables
    crow_casl_elem_t dynamics[CROW_CASL_DYN_COUNT];
    int dyn_ix;                                    // Next available dynamic slot

    // State flags
    bool holding;                                  // True when in held state
    bool locked;                                   // True when locked from actions
} crow_casl_t;

// Global CASL system functions
void crow_casl_init();
void crow_casl_deinit();
void crow_casl_process_sample();

// Per-channel CASL functions
void crow_casl_describe(int channel, lua_State* L);
void crow_casl_action(int channel, int action);

// Dynamic variable management
int crow_casl_defdynamic(int channel);
void crow_casl_cleardynamics(int channel);
void crow_casl_setdynamic(int channel, int dynamic_ix, float val);
float crow_casl_getdynamic(int channel, int dynamic_ix);

// Internal helper functions (exposed for testing)
crow_casl_elem_obj_t crow_casl_resolve_elem(int channel, crow_casl_elem_t* elem);
void crow_casl_next_action(int channel);

// Lua binding functions
extern "C" int l_casl_describe(lua_State* L);
extern "C" int l_casl_action(lua_State* L);
extern "C" int l_casl_defdynamic(lua_State* L);
extern "C" int l_casl_cleardynamics(lua_State* L);
extern "C" int l_casl_setdynamic(lua_State* L);
extern "C" int l_casl_getdynamic(lua_State* L);

// Shape string conversion (matches crow's API)
crow_shape_t crow_casl_str_to_shape(const char* shape_str);

#endif // CROW_CASL_H
