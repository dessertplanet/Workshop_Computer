#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "crow_slopes.h"

// lua libs for ASL integration
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#define CROW_ASL_CHANNELS 4
#define CROW_ASL_TO_COUNT 16
#define CROW_ASL_SEQ_COUNT 8
#define CROW_ASL_SEQ_LENGTH 8
#define CROW_ASL_DYN_COUNT 40

// ASL Control types (matching crow's ToControl enum)
typedef enum {
    CROW_ASL_LITERAL = 0,
    CROW_ASL_RECUR,
    CROW_ASL_IF,
    CROW_ASL_ENTER,
    CROW_ASL_HELD,
    CROW_ASL_WAIT,
    CROW_ASL_UNHELD,
    CROW_ASL_LOCK,
    CROW_ASL_OPEN
} crow_asl_control_t;

// Element types for ASL expressions
typedef enum {
    CROW_ASL_ELEM_FLOAT = 0,
    CROW_ASL_ELEM_SHAPE,
    CROW_ASL_ELEM_DYNAMIC,
    CROW_ASL_ELEM_MUTABLE,
    // Arithmetic ops
    CROW_ASL_ELEM_NEGATE,
    CROW_ASL_ELEM_ADD,
    CROW_ASL_ELEM_SUB,
    CROW_ASL_ELEM_MUL,
    CROW_ASL_ELEM_DIV,
    CROW_ASL_ELEM_MOD,
    CROW_ASL_ELEM_MUTATE
} crow_asl_elem_type_t;

// Element object union (matching crow's ElemO)
typedef union {
    float f;
    int dyn;                    // Index into dynamics array
    uint16_t var[2];           // 2 indexes into dynamic table
    int seq;                   // Reference to sequence object
    crow_shape_t shape;
} crow_asl_elem_obj_t;

// Element structure (matching crow's Elem)
typedef struct {
    crow_asl_elem_obj_t obj;
    crow_asl_elem_type_t type;
} crow_asl_elem_t;

// ASL "To" structure (matching crow's To)
typedef struct {
    crow_asl_elem_t a;         // Voltage/destination
    crow_asl_elem_t b;         // Time
    crow_asl_elem_t c;         // Shape
    crow_asl_control_t ctrl;   // Control type
} crow_asl_to_t;

// ASL Sequence structure (matching crow's Sequence)
typedef struct {
    crow_asl_to_t* stage[CROW_ASL_SEQ_LENGTH];
    int length;
    int pc;                    // Program counter
    int parent;                // Parent sequence index
} crow_asl_sequence_t;

// Main ASL structure (matching crow's Casl)
typedef struct {
    int index;                 // Channel index (0-3)
    
    // To stages
    crow_asl_to_t tos[CROW_ASL_TO_COUNT];
    int to_ix;
    
    // Sequences
    crow_asl_sequence_t* seq_current;
    crow_asl_sequence_t seqs[CROW_ASL_SEQ_COUNT];
    int seq_ix;
    int seq_select;
    
    // Dynamic variables
    crow_asl_elem_t dynamics[CROW_ASL_DYN_COUNT];
    int dyn_ix;
    
    // State flags
    bool holding;
    bool locked;
    
    // Completion callback
    void (*done_callback)(int channel);
} crow_asl_t;

// Global ASL system
extern crow_asl_t* crow_asl_channels[CROW_ASL_CHANNELS];

// Core ASL API
void crow_asl_init(void);
void crow_asl_deinit(void);

// ASL control functions
crow_asl_t* crow_asl_get_channel(int channel);
void crow_asl_describe(int channel, lua_State* L);
void crow_asl_action(int channel, int action);

// Dynamic variable management
int crow_asl_def_dynamic(int channel);
void crow_asl_clear_dynamics(int channel);
void crow_asl_set_dynamic(int channel, int dynamic_ix, float val);
float crow_asl_get_dynamic(int channel, int dynamic_ix);

// Lua bindings
void crow_asl_register_lua_functions(lua_State* L);

// Processing
void crow_asl_process_sample(void);
void crow_asl_process_block(float* input_blocks[4], int block_size);

// Completion callback for slopes integration
void crow_asl_slope_done_callback(int channel);
