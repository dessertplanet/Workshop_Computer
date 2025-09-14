#include "crow_asl.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// Forward declarations
static void asl_process_dynamic_updates(crow_asl_t* asl, int block_size);

// Global ASL channels
crow_asl_t* crow_asl_channels[CROW_ASL_CHANNELS];
static bool asl_initialized = false;

// Initialize ASL system
void crow_asl_init(void) {
    if (asl_initialized) {
        return;
    }
    
    printf("Initializing crow ASL system (%d channels)...\n", CROW_ASL_CHANNELS);
    
    for (int i = 0; i < CROW_ASL_CHANNELS; i++) {
        crow_asl_channels[i] = new crow_asl_t;
        crow_asl_t* asl = crow_asl_channels[i];
        
        // Initialize basic structure
        asl->index = i;
        asl->to_ix = 0;
        asl->seq_current = nullptr;
        asl->seq_ix = 0;
        asl->seq_select = 0;
        asl->dyn_ix = 0;
        asl->holding = false;
        asl->locked = false;
        asl->done_callback = nullptr;
        
        // Initialize sequences
        for (int j = 0; j < CROW_ASL_SEQ_COUNT; j++) {
            asl->seqs[j].length = 0;
            asl->seqs[j].pc = 0;
            asl->seqs[j].parent = -1;
            for (int k = 0; k < CROW_ASL_SEQ_LENGTH; k++) {
                asl->seqs[j].stage[k] = nullptr;
            }
        }
        
        // Clear dynamics
        for (int j = 0; j < CROW_ASL_DYN_COUNT; j++) {
            asl->dynamics[j].type = CROW_ASL_ELEM_FLOAT;
            asl->dynamics[j].obj.f = 0.0f;
        }
    }
    
    asl_initialized = true;
    printf("Crow ASL system initialized\n");
}

void crow_asl_deinit(void) {
    if (!asl_initialized) {
        return;
    }
    
    for (int i = 0; i < CROW_ASL_CHANNELS; i++) {
        if (crow_asl_channels[i]) {
            delete crow_asl_channels[i];
            crow_asl_channels[i] = nullptr;
        }
    }
    
    asl_initialized = false;
}

// Get ASL channel
crow_asl_t* crow_asl_get_channel(int channel) {
    if (!asl_initialized || channel < 0 || channel >= CROW_ASL_CHANNELS) {
        return nullptr;
    }
    return crow_asl_channels[channel];
}

// Dynamic variable management
int crow_asl_def_dynamic(int channel) {
    crow_asl_t* asl = crow_asl_get_channel(channel);
    if (!asl || asl->dyn_ix >= CROW_ASL_DYN_COUNT) {
        return -1;
    }
    
    int idx = asl->dyn_ix++;
    asl->dynamics[idx].type = CROW_ASL_ELEM_FLOAT;
    asl->dynamics[idx].obj.f = 0.0f;
    
    return idx;
}

void crow_asl_clear_dynamics(int channel) {
    crow_asl_t* asl = crow_asl_get_channel(channel);
    if (!asl) {
        return;
    }
    
    asl->dyn_ix = 0;
    for (int i = 0; i < CROW_ASL_DYN_COUNT; i++) {
        asl->dynamics[i].type = CROW_ASL_ELEM_FLOAT;
        asl->dynamics[i].obj.f = 0.0f;
    }
}

void crow_asl_set_dynamic(int channel, int dynamic_ix, float val) {
    crow_asl_t* asl = crow_asl_get_channel(channel);
    if (!asl || dynamic_ix < 0 || dynamic_ix >= CROW_ASL_DYN_COUNT) {
        return;
    }
    
    asl->dynamics[dynamic_ix].type = CROW_ASL_ELEM_FLOAT;
    asl->dynamics[dynamic_ix].obj.f = val;
}

float crow_asl_get_dynamic(int channel, int dynamic_ix) {
    crow_asl_t* asl = crow_asl_get_channel(channel);
    if (!asl || dynamic_ix < 0 || dynamic_ix >= CROW_ASL_DYN_COUNT) {
        return 0.0f;
    }
    
    return asl->dynamics[dynamic_ix].obj.f;
}

// Evaluate an ASL element to a float value
static float crow_asl_eval_elem(crow_asl_t* asl, const crow_asl_elem_t* elem) {
    switch (elem->type) {
        case CROW_ASL_ELEM_FLOAT:
            return elem->obj.f;
            
        case CROW_ASL_ELEM_DYNAMIC:
            if (elem->obj.dyn >= 0 && elem->obj.dyn < CROW_ASL_DYN_COUNT) {
                return crow_asl_eval_elem(asl, &asl->dynamics[elem->obj.dyn]);
            }
            return 0.0f;
            
        case CROW_ASL_ELEM_SHAPE:
            // Shape elements are converted to strings/enums elsewhere
            return (float)elem->obj.shape;
            
        default:
            // TODO: Implement arithmetic operations
            return 0.0f;
    }
}

// Process a single TO statement
static void crow_asl_process_to(crow_asl_t* asl, const crow_asl_to_t* to) {
    if (!asl || !to) {
        return;
    }
    
    // Evaluate the TO parameters
    float volts = crow_asl_eval_elem(asl, &to->a);
    float time_ms = crow_asl_eval_elem(asl, &to->b);
    crow_shape_t shape = (crow_shape_t)crow_asl_eval_elem(asl, &to->c);
    
    // Clamp shape to valid range
    if (shape < CROW_SHAPE_Linear || shape > CROW_SHAPE_Rebound) {
        shape = CROW_SHAPE_Linear;
    }
    
    printf("ASL TO: ch %d -> %.3fV over %.1fms (%d)\n", 
           asl->index, volts, time_ms, (int)shape);
    
    // Execute the slope using our existing slopes system
    crow_slopes_toward(asl->index, volts, time_ms, shape, crow_asl_slope_done_callback);
}

// Basic ASL describe (simplified version)
void crow_asl_describe(int channel, lua_State* L) {
    crow_asl_t* asl = crow_asl_get_channel(channel);
    if (!asl || !L) {
        printf("crow_asl_describe: invalid channel %d or lua state\n", channel);
        return;
    }
    
    printf("ASL describe called for channel %d\n", channel);
    
    // For now, implement a basic TO parser
    // This is a simplified version - full implementation would parse complex ASL tables
    if (lua_istable(L, -1)) {
        lua_geti(L, -1, 1); // Get first element (should be "TO")
        if (lua_isstring(L, -1)) {
            const char* cmd = lua_tostring(L, -1);
            if (strcmp(cmd, "TO") == 0) {
                // Create a simple TO command
                crow_asl_to_t* to = &asl->tos[0]; // Use first TO slot
                
                // Get voltage (index 2)
                lua_pop(L, 1);
                lua_geti(L, -1, 2);
                if (lua_isnumber(L, -1)) {
                    to->a.type = CROW_ASL_ELEM_FLOAT;
                    to->a.obj.f = lua_tonumber(L, -1);
                }
                
                // Get time (index 3)
                lua_pop(L, 1);
                lua_geti(L, -1, 3);
                if (lua_isnumber(L, -1)) {
                    to->b.type = CROW_ASL_ELEM_FLOAT;
                    to->b.obj.f = lua_tonumber(L, -1) * 1000.0f; // Convert to ms
                }
                
                // Get shape (index 4)
                lua_pop(L, 1);
                lua_geti(L, -1, 4);
                if (lua_isstring(L, -1)) {
                    const char* shape_str = lua_tostring(L, -1);
                    to->c.type = CROW_ASL_ELEM_SHAPE;
                    to->c.obj.shape = crow_str_to_shape(shape_str);
                }
                
                to->ctrl = CROW_ASL_LITERAL;
                asl->to_ix = 1; // Mark that we have one TO
                
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
}

// Execute ASL action
void crow_asl_action(int channel, int action) {
    crow_asl_t* asl = crow_asl_get_channel(channel);
    if (!asl) {
        printf("crow_asl_action: invalid channel %d\n", channel);
        return;
    }
    
    printf("ASL action called for channel %d, action %d\n", channel, action);
    
    // Simple action: execute the first TO if we have one
    if (asl->to_ix > 0) {
        crow_asl_process_to(asl, &asl->tos[0]);
    }
}

// Process ASL events (called from audio thread)
void crow_asl_process_sample(void) {
    if (!asl_initialized) {
        return;
    }
    
    // Legacy per-sample processing - kept for compatibility
    // The ASL system primarily coordinates with slopes
    // Most work is done in slope completion callbacks
}

// Vector processing for ASL system (Phase 2)
void crow_asl_process_block(float* input_blocks[4], int block_size) {
    if (!asl_initialized) {
        return;
    }
    
    // ASL system primarily coordinates envelope sequences rather than processing audio
    // Vector processing here focuses on efficient evaluation of dynamic variables
    // and bulk processing of ASL state updates
    
    // Process each ASL channel for bulk dynamic variable updates
    for (int ch = 0; ch < CROW_ASL_CHANNELS; ch++) {
        crow_asl_t* asl = crow_asl_channels[ch];
        if (!asl) continue;
        
        // Check for pending sequences that need to be triggered
        // This is more efficient than checking per-sample
        if (asl->seq_current && asl->seq_current->pc < asl->seq_current->length) {
            // Process sequence advancement at block boundaries
            crow_asl_sequence_t* seq = asl->seq_current;
            
            // Execute current stage if available
            if (seq->stage[seq->pc]) {
                crow_asl_process_to(asl, seq->stage[seq->pc]);
                seq->pc++;
                
                // Check if sequence is complete
                if (seq->pc >= seq->length) {
                    asl->seq_current = nullptr;
                    seq->pc = 0;
                }
            }
        }
        
        // Batch update dynamic variables that may have changed
        // This avoids per-sample evaluation overhead
        asl_process_dynamic_updates(asl, block_size);
    }
    
    // Future: Vector processing for ASL arithmetic operations using wrDsp
    // Example: b_add(), b_mul() for dynamic variable calculations across blocks
}

// Helper function for batch processing dynamic variable updates
static void asl_process_dynamic_updates(crow_asl_t* asl, int block_size) {
    if (!asl) return;
    
    // Process any pending dynamic variable calculations
    // This could include vector math operations for complex expressions
    
    for (int dyn_idx = 0; dyn_idx < asl->dyn_ix; dyn_idx++) {
        crow_asl_elem_t* elem = &asl->dynamics[dyn_idx];
        
        // For arithmetic operations, we could use wrDsp vector functions here
        // This is where Phase 2 vector optimization would be most beneficial
        switch (elem->type) {
            case CROW_ASL_ELEM_ADD:
            case CROW_ASL_ELEM_SUB:
            case CROW_ASL_ELEM_MUL:
            case CROW_ASL_ELEM_DIV:
                // Future: Use wrDsp vector functions for bulk arithmetic
                // b_add(), b_sub(), b_mul(), b_div() for processing arrays
                break;
                
            default:
                // Simple types don't need block processing
                break;
        }
    }
}

// Slope completion callback
void crow_asl_slope_done_callback(int channel) {
    crow_asl_t* asl = crow_asl_get_channel(channel);
    if (!asl) {
        return;
    }
    
    printf("ASL slope completed for channel %d\n", channel);
    
    // Call user-defined done callback if set
    if (asl->done_callback) {
        asl->done_callback(channel);
    }
    
    // TODO: Continue with next stage in sequence if applicable
}

// Lua binding functions
static int l_casl_describe(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert 1-based to 0-based
    crow_asl_describe(channel, L);
    return 0;
}

static int l_casl_action(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert 1-based to 0-based
    int action = luaL_optinteger(L, 2, 1);
    crow_asl_action(channel, action);
    return 0;
}

static int l_casl_defdynamic(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert 1-based to 0-based
    int dyn_id = crow_asl_def_dynamic(channel);
    lua_pushinteger(L, dyn_id);
    return 1;
}

static int l_casl_cleardynamics(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert 1-based to 0-based
    crow_asl_clear_dynamics(channel);
    return 0;
}

static int l_casl_setdynamic(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert 1-based to 0-based
    int dyn_id = luaL_checkinteger(L, 2);
    float value = luaL_checknumber(L, 3);
    crow_asl_set_dynamic(channel, dyn_id, value);
    return 0;
}

static int l_casl_getdynamic(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert 1-based to 0-based
    int dyn_id = luaL_checkinteger(L, 2);
    float value = crow_asl_get_dynamic(channel, dyn_id);
    lua_pushnumber(L, value);
    return 1;
}

// Register lua functions
void crow_asl_register_lua_functions(lua_State* L) {
    if (!L) {
        return;
    }
    
    printf("Registering ASL lua functions\n");
    
    // Register CASL functions (matching crow's naming)
    lua_register(L, "casl_describe", l_casl_describe);
    lua_register(L, "casl_action", l_casl_action);
    lua_register(L, "casl_defdynamic", l_casl_defdynamic);
    lua_register(L, "casl_cleardynamics", l_casl_cleardynamics);
    lua_register(L, "casl_setdynamic", l_casl_setdynamic);
    lua_register(L, "casl_getdynamic", l_casl_getdynamic);
}
