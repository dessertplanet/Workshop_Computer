#include "crow_casl.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// Include our slopes system for envelope execution
#include "crow_slopes.h"

// Wrap Lua includes for proper C linkage
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// Global CASL instances - one per output channel
static crow_casl_t* g_casl_channels[CROW_CASL_CHANNELS] = {nullptr, nullptr, nullptr, nullptr};

// Forward declarations for internal functions
static void casl_seq_enter(crow_casl_t* self);
static void casl_seq_exit(crow_casl_t* self);
static void casl_seq_append(crow_casl_t* self, crow_casl_to_t* to);
static void casl_parse_table(crow_casl_t* self, lua_State* L);
static void casl_read_to(crow_casl_t* self, crow_casl_to_t* to, lua_State* L);
static void casl_capture_elem(crow_casl_t* self, crow_casl_elem_t* elem, lua_State* L, int ix);
static crow_casl_to_t* casl_to_alloc(crow_casl_t* self);
static crow_casl_to_t* casl_seq_advance(crow_casl_t* self);
static bool casl_seq_up(crow_casl_t* self);
static void casl_seq_down(crow_casl_t* self, int seq_ix);
static bool casl_find_control(crow_casl_t* self, crow_casl_to_control_t ctrl, bool full_search);
static crow_casl_elem_obj_t casl_resolve(crow_casl_t* self, crow_casl_elem_t* elem);

// Lua helper functions for table parsing
static int lua_ix_type(lua_State* L, int ix) {
    lua_pushnumber(L, ix);
    lua_gettable(L, -2);
    int type = lua_type(L, -1);
    lua_pop(L, 1);
    return type;
}

static void lua_ix_str(char* result, lua_State* L, int ix, int length) {
    lua_pushnumber(L, ix);
    lua_gettable(L, -2);
    const char* s = luaL_checkstring(L, -1);
    for (int i = 0; i < length && i < (int)strlen(s); ++i) {
        result[i] = s[i];
    }
    if (length > 0) result[length-1] = '\0';
    lua_pop(L, 1);
}

static char lua_ix_char(lua_State* L, int ix) {
    lua_pushnumber(L, ix);
    lua_gettable(L, -2);
    const char* s = luaL_checkstring(L, -1);
    char c = s[0];
    lua_pop(L, 1);
    return c;
}

static float lua_ix_num(lua_State* L, int ix) {
    lua_pushnumber(L, ix);
    lua_gettable(L, -2);
    float num = luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return num;
}

static bool lua_ix_bool(lua_State* L, int ix) {
    lua_pushnumber(L, ix);
    lua_gettable(L, -2);
    bool b = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return b;
}

static int lua_ix_int(lua_State* L, int ix) {
    lua_pushnumber(L, ix);
    lua_gettable(L, -2);
    int i = luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return i;
}

// Shape string conversion matching crow's API
crow_shape_t crow_casl_str_to_shape(const char* shape_str) {
    if (!shape_str) return CROW_SHAPE_Linear;
    
    if (strncmp(shape_str, "li", 2) == 0) return CROW_SHAPE_Linear;
    if (strncmp(shape_str, "si", 2) == 0) return CROW_SHAPE_Sine;
    if (strncmp(shape_str, "lo", 2) == 0) return CROW_SHAPE_Log;
    if (strncmp(shape_str, "ex", 2) == 0) return CROW_SHAPE_Expo;
    if (strncmp(shape_str, "no", 2) == 0) return CROW_SHAPE_Now;
    if (strncmp(shape_str, "wa", 2) == 0) return CROW_SHAPE_Wait;
    if (strncmp(shape_str, "ov", 2) == 0) return CROW_SHAPE_Over;
    if (strncmp(shape_str, "un", 2) == 0) return CROW_SHAPE_Under;
    if (strncmp(shape_str, "re", 2) == 0) return CROW_SHAPE_Rebound;
    
    return CROW_SHAPE_Linear; // Default
}

// Global CASL system functions
void crow_casl_init() {
    for (int i = 0; i < CROW_CASL_CHANNELS; i++) {
        if (g_casl_channels[i] != nullptr) {
            free(g_casl_channels[i]);
        }
        
        g_casl_channels[i] = (crow_casl_t*)malloc(sizeof(crow_casl_t));
        if (!g_casl_channels[i]) {
            printf("CASL malloc failed for channel %d!\n", i);
            continue;
        }
        
        crow_casl_t* self = g_casl_channels[i];
        
        // Initialize allocation counters
        self->to_ix = 0;
        self->seq_ix = 0;
        self->seq_select = -1;
        self->dyn_ix = 0;
        
        // Initialize state
        self->holding = false;
        self->locked = false;
        
        // Set current sequence to first sequence
        self->seq_current = &self->seqs[0];
        
        // Reset all sequence program counters
        for (int j = 0; j < CROW_CASL_SEQ_COUNT; j++) {
            self->seqs[j].pc = 0;
            self->seqs[j].length = 0;
            self->seqs[j].parent = -1;
        }
    }
}

void crow_casl_deinit() {
    for (int i = 0; i < CROW_CASL_CHANNELS; i++) {
        if (g_casl_channels[i] != nullptr) {
            free(g_casl_channels[i]);
            g_casl_channels[i] = nullptr;
        }
    }
}

void crow_casl_process_sample() {
    // CASL processing happens via callback system
    // The slopes system calls crow_casl_next_action when envelopes complete
    // This maintains the event-driven nature of CASL
}

// Per-channel CASL functions
void crow_casl_describe(int channel, lua_State* L) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) return;
    if (!g_casl_channels[channel]) return;
    
    crow_casl_t* self = g_casl_channels[channel];
    
    // Reset allocation counters
    self->to_ix = 0;
    self->seq_ix = 0;
    self->seq_select = -1;
    
    // Reset current sequence
    self->seq_current = &self->seqs[0];
    
    // Reset all program counters
    for (int i = 0; i < CROW_CASL_SEQ_COUNT; i++) {
        self->seqs[i].pc = 0;
        self->seqs[i].length = 0;
        self->seqs[i].parent = -1;
    }
    
    // Enter first sequence
    casl_seq_enter(self);
    
    // Parse the lua table
    casl_parse_table(self, L);
}

void crow_casl_action(int channel, int action) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) return;
    if (!g_casl_channels[channel]) return;
    
    crow_casl_t* self = g_casl_channels[channel];
    
    if (self->locked) {
        if (action == 2) { // Unlock message
            self->locked = false;
        }
        return; // Don't trigger action if locked
    }
    
    if (action == 1) { // Restart sequence
        self->seq_current = &self->seqs[0];
        for (int i = 0; i < CROW_CASL_SEQ_COUNT; i++) {
            self->seqs[i].pc = 0;
        }
        self->holding = false;
        self->locked = false;
    } else if (action == 0 && self->holding) { // Go to release if held
        if (casl_find_control(self, CROW_CASL_TO_UNHELD, false)) {
            self->holding = false;
        } else {
            printf("Couldn't find ToUnheld. Restarting.\n");
            crow_casl_action(channel, 1);
            return;
        }
    } else {
        return; // Do nothing
    }
    
    crow_casl_next_action(channel);
}

// Dynamic variable management
int crow_casl_defdynamic(int channel) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) return -1;
    if (!g_casl_channels[channel]) return -1;
    
    crow_casl_t* self = g_casl_channels[channel];
    
    if (self->dyn_ix >= CROW_CASL_DYN_COUNT) {
        printf("ERROR: No dynamic slots remain\n");
        return -1;
    }
    
    int ix = self->dyn_ix;
    self->dyn_ix++;
    return ix;
}

void crow_casl_cleardynamics(int channel) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) return;
    if (!g_casl_channels[channel]) return;
    
    g_casl_channels[channel]->dyn_ix = 0;
}

void crow_casl_setdynamic(int channel, int dynamic_ix, float val) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) return;
    if (!g_casl_channels[channel]) return;
    if (dynamic_ix < 0 || dynamic_ix >= CROW_CASL_DYN_COUNT) return;
    
    crow_casl_t* self = g_casl_channels[channel];
    self->dynamics[dynamic_ix].obj.f = val;
    self->dynamics[dynamic_ix].type = CROW_CASL_ELEM_FLOAT;
}

float crow_casl_getdynamic(int channel, int dynamic_ix) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) return 0.0f;
    if (!g_casl_channels[channel]) return 0.0f;
    if (dynamic_ix < 0 || dynamic_ix >= CROW_CASL_DYN_COUNT) return 0.0f;
    
    crow_casl_t* self = g_casl_channels[channel];
    
    switch (self->dynamics[dynamic_ix].type) {
        case CROW_CASL_ELEM_FLOAT:
            return self->dynamics[dynamic_ix].obj.f;
        default:
            printf("getdynamic! wrong type\n");
            return 0.0f;
    }
}

// Internal sequence management functions
static void casl_seq_enter(crow_casl_t* self) {
    if (self->seq_ix >= CROW_CASL_SEQ_COUNT) {
        printf("ERROR: No sequences left!\n");
        return;
    }
    
    crow_casl_sequence_t* s = &self->seqs[self->seq_ix];
    self->seq_current = s;
    
    s->length = 0;
    s->pc = 0;
    s->parent = self->seq_select;
    
    self->seq_select = self->seq_ix;
    self->seq_ix++;
}

static void casl_seq_exit(crow_casl_t* self) {
    self->seq_select = self->seq_current->parent;
    if (self->seq_select >= 0) {
        self->seq_current = &self->seqs[self->seq_select];
    }
}

static void casl_seq_append(crow_casl_t* self, crow_casl_to_t* to) {
    crow_casl_sequence_t* s = self->seq_current;
    if (s->length >= CROW_CASL_SEQ_LENGTH) {
        printf("ERROR: No stages left!\n");
        return;
    }
    s->stage[s->length] = to;
    s->length++;
}

static crow_casl_to_t* casl_to_alloc(crow_casl_t* self) {
    if (self->to_ix >= CROW_CASL_TO_COUNT) {
        return nullptr;
    }
    
    crow_casl_to_t* to = &self->tos[self->to_ix];
    self->to_ix++;
    return to;
}

// Callback function for when slopes complete - triggers next CASL action
static void casl_slope_callback(int channel) {
    crow_casl_next_action(channel);
}

// Core CASL execution engine
void crow_casl_next_action(int channel) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) return;
    if (!g_casl_channels[channel]) return;
    
    crow_casl_t* self = g_casl_channels[channel];
    
    while (true) { // Repeat until halt
        crow_casl_to_t* to = casl_seq_advance(self);
        
        if (to) { // To is valid
            switch (to->ctrl) {
                case CROW_CASL_TO_LITERAL: {
                    float dest = casl_resolve(self, &to->a).f;
                    float time_s = casl_resolve(self, &to->b).f;
                    crow_shape_t shape = casl_resolve(self, &to->c).shape;
                    
                    // Use our slopes system to execute the envelope
                    crow_slopes_toward(channel, dest, time_s * 1000.0f, shape, casl_slope_callback);
                    
                    if (time_s > 0.0f) {
                        return; // Wait for slope completion
                    }
                    break;
                }
                
                case CROW_CASL_TO_IF: {
                    if (casl_resolve(self, &to->a).f <= 0.0f) { // Predicate is false
                        goto stepup; // Skip to next level
                    }
                    break;
                }
                
                case CROW_CASL_TO_RECUR:
                    self->seq_current->pc = 0;
                    break;
                    
                case CROW_CASL_TO_ENTER:
                    casl_seq_down(self, to->a.obj.seq);
                    break;
                    
                case CROW_CASL_TO_HELD:
                    self->holding = true;
                    break;
                    
                case CROW_CASL_TO_WAIT:
                    return; // Halt execution
                    
                case CROW_CASL_TO_UNHELD:
                    self->holding = false;
                    break;
                    
                case CROW_CASL_TO_LOCK:
                    self->locked = true;
                    break;
                    
                case CROW_CASL_TO_OPEN:
                    self->locked = false;
                    break;
            }
        } else {
stepup:
            if (!casl_seq_up(self)) {
                // Sequence complete - could trigger lua callback here
                printf("CASL sequence complete for channel %d\n", channel);
                return;
            }
        }
    }
}

// Sequence traversal functions
static crow_casl_to_t* casl_seq_advance(crow_casl_t* self) {
    crow_casl_sequence_t* s = self->seq_current;
    crow_casl_to_t* to = nullptr;
    
    if (s->pc < s->length) {
        to = s->stage[s->pc];
        s->pc++;
    }
    
    return to;
}

static bool casl_seq_up(crow_casl_t* self) {
    if (self->seq_current->parent < 0) {
        return false; // Nothing left to do
    }
    
    self->seq_current->pc = 0; // Reset PC for repeat execution
    
    self->seq_select = self->seq_current->parent;
    self->seq_current = &self->seqs[self->seq_select];
    
    return true;
}

static void casl_seq_down(crow_casl_t* self, int seq_ix) {
    self->seq_select = seq_ix;
    self->seq_current = &self->seqs[self->seq_select];
}

static bool casl_find_control(crow_casl_t* self, crow_casl_to_control_t ctrl, bool full_search) {
    crow_casl_to_t* to = casl_seq_advance(self);
    
    if (to) {
        if (to->ctrl == ctrl) {
            return true; // Found it
        }
        
        switch (to->ctrl) {
            case CROW_CASL_TO_ENTER:
                if (full_search) {
                    casl_seq_down(self, to->a.obj.seq);
                }
                return casl_find_control(self, ctrl, full_search);
                
            case CROW_CASL_TO_IF:
                if (!full_search) {
                    casl_seq_up(self); // Skip If contents
                }
                // Falls through
                
            default:
                return casl_find_control(self, ctrl, full_search);
        }
    } else if (casl_seq_up(self)) {
        return casl_find_control(self, ctrl, full_search);
    }
    
    return false;
}

// Element resolution with support for arithmetic operations
static volatile uint16_t resolving_mutable = CROW_CASL_DYN_COUNT; // Out of range initially

static crow_casl_elem_obj_t casl_resolve_recursive(crow_casl_t* self, crow_casl_elem_t* elem) {
    switch (elem->type) {
        case CROW_CASL_ELEM_FLOAT:
            return elem->obj;
            
        case CROW_CASL_ELEM_SHAPE:
            return elem->obj;
            
        case CROW_CASL_ELEM_DYNAMIC:
            return casl_resolve_recursive(self, &self->dynamics[elem->obj.dyn]);
            
        case CROW_CASL_ELEM_MUTABLE:
            resolving_mutable = elem->obj.var[0];
            return casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]);
            
        case CROW_CASL_ELEM_NEGATE: {
            crow_casl_elem_obj_t result;
            result.f = -casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]).f;
            return result;
        }
        
        case CROW_CASL_ELEM_ADD: {
            crow_casl_elem_obj_t result;
            result.f = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]).f +
                      casl_resolve_recursive(self, &self->dynamics[elem->obj.var[1]]).f;
            return result;
        }
        
        case CROW_CASL_ELEM_SUB: {
            crow_casl_elem_obj_t result;
            result.f = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]).f -
                      casl_resolve_recursive(self, &self->dynamics[elem->obj.var[1]]).f;
            return result;
        }
        
        case CROW_CASL_ELEM_MUL: {
            crow_casl_elem_obj_t result;
            result.f = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]).f *
                      casl_resolve_recursive(self, &self->dynamics[elem->obj.var[1]]).f;
            return result;
        }
        
        case CROW_CASL_ELEM_DIV: {
            crow_casl_elem_obj_t result;
            float divisor = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[1]]).f;
            if (divisor != 0.0f) {
                result.f = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]).f / divisor;
            } else {
                result.f = 0.0f;
            }
            return result;
        }
        
        case CROW_CASL_ELEM_MOD: {
            crow_casl_elem_obj_t result;
            float val = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]).f;
            float wrap = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[1]]).f;
            if (wrap != 0.0f) {
                float mul = floorf(val / wrap);
                result.f = val - (wrap * mul);
            } else {
                result.f = val;
            }
            return result;
        }
        
        case CROW_CASL_ELEM_MUTATE: {
            crow_casl_elem_obj_t mutated = casl_resolve_recursive(self, &self->dynamics[elem->obj.var[0]]);
            if (resolving_mutable < CROW_CASL_DYN_COUNT) {
                self->dynamics[resolving_mutable].obj = mutated;
                resolving_mutable = CROW_CASL_DYN_COUNT; // Mutation resolved
            }
            return mutated;
        }
        
        default: {
            crow_casl_elem_obj_t result;
            result.f = 0.0f;
            return result;
        }
    }
}

static crow_casl_elem_obj_t casl_resolve(crow_casl_t* self, crow_casl_elem_t* elem) {
    resolving_mutable = CROW_CASL_DYN_COUNT; // Reset
    crow_casl_elem_obj_t result = casl_resolve_recursive(self, elem);
    
    if (resolving_mutable < CROW_CASL_DYN_COUNT) {
        self->dynamics[resolving_mutable].obj = result;
    }
    
    return result;
}

// Exposed resolve function for testing
crow_casl_elem_obj_t crow_casl_resolve_elem(int channel, crow_casl_elem_t* elem) {
    if (channel < 0 || channel >= CROW_CASL_CHANNELS) {
        crow_casl_elem_obj_t result;
        result.f = 0.0f;
        return result;
    }
    if (!g_casl_channels[channel]) {
        crow_casl_elem_obj_t result;
        result.f = 0.0f;
        return result;
    }
    
    return casl_resolve(g_casl_channels[channel], elem);
}

// Table parsing implementation - this is the complex part that translates lua tables to CASL sequences
static void casl_parse_table(crow_casl_t* self, lua_State* L) {
    switch (lua_ix_type(L, 1)) {
        case LUA_TSTRING: { // TO, RECUR, IF, HELD, WAIT, etc.
            crow_casl_to_t* to = casl_to_alloc(self);
            if (!to) {
                printf("ERROR: Not enough To slots left\n");
                return;
            }
            
            casl_seq_append(self, to);
            
            char cmd = lua_ix_char(L, 1);
            switch (cmd) {
                case 'T': // Standard To statement
                    casl_read_to(self, to, L);
                    break;
                case 'R': // Recur
                    to->ctrl = CROW_CASL_TO_RECUR;
                    break;
                case 'I': // If
                    casl_capture_elem(self, &to->a, L, 2);
                    to->ctrl = CROW_CASL_TO_IF;
                    break;
                case 'H': // Held
                    to->ctrl = CROW_CASL_TO_HELD;
                    break;
                case 'W': // Wait
                    to->ctrl = CROW_CASL_TO_WAIT;
                    break;
                case 'U': // Unheld
                    to->ctrl = CROW_CASL_TO_UNHELD;
                    break;
                case 'L': // Lock
                    to->ctrl = CROW_CASL_TO_LOCK;
                    break;
                case 'O': // Open
                    to->ctrl = CROW_CASL_TO_OPEN;
                    break;
                default:
                    printf("ERROR: Unknown command char '%c'\n", cmd);
                    break;
            }
            break;
        }
        
        case LUA_TTABLE: { // Nested sequence
            crow_casl_to_t* to = casl_to_alloc(self);
            if (!to) {
                printf("ERROR: Not enough To slots left\n");
                return;
            }
            
            casl_seq_append(self, to);
            to->ctrl = CROW_CASL_TO_ENTER;
            
            casl_seq_enter(self);
            to->a.obj.seq = self->seq_select;
            
            int seq_len = lua_rawlen(L, -1);
            for (int i = 1; i <= seq_len; i++) { // Lua is 1-based
                lua_pushnumber(L, i);
                lua_gettable(L, -2);
                casl_parse_table(self, L); // Recursive call
                lua_pop(L, 1);
            }
            
            casl_seq_exit(self);
            break;
        }
        
        default:
            printf("ERROR: Unhandled parse type\n");
            break;
    }
}

static void casl_read_to(crow_casl_t* self, crow_casl_to_t* to, lua_State* L) {
    casl_capture_elem(self, &to->a, L, 2);
    casl_capture_elem(self, &to->b, L, 3);
    casl_capture_elem(self, &to->c, L, 4);
    to->ctrl = CROW_CASL_TO_LITERAL;
}

static void casl_allocating_capture(crow_casl_t* self, crow_casl_elem_t* elem, lua_State* L, crow_casl_elem_type_t type, int count) {
    elem->type = type;
    for (int i = 0; i < count; i++) {
        int var = crow_casl_defdynamic(self->seq_select >= 0 ? self->seq_select : 0); // Use channel 0 as fallback
        elem->obj.var[i] = var;
        casl_capture_elem(self, &self->dynamics[var], L, i + 2);
    }
}

static void casl_capture_elem(crow_casl_t* self, crow_casl_elem_t* elem, lua_State* L, int ix) {
    switch (lua_ix_type(L, ix)) {
        case LUA_TNUMBER:
            elem->obj.f = lua_ix_num(L, ix);
            elem->type = CROW_CASL_ELEM_FLOAT;
            break;
            
        case LUA_TBOOLEAN:
            elem->obj.f = lua_ix_bool(L, ix) ? 1.0f : 0.0f;
            elem->type = CROW_CASL_ELEM_FLOAT;
            break;
            
        case LUA_TSTRING: {
            char s[8];
            lua_ix_str(s, L, ix, sizeof(s));
            elem->obj.shape = crow_casl_str_to_shape(s);
            elem->type = CROW_CASL_ELEM_SHAPE;
            break;
        }
        
        case LUA_TTABLE: {
            lua_pushnumber(L, ix);
            lua_gettable(L, -2);
            
            char index = lua_ix_char(L, 1);
            switch (index) {
                case 'D': // Dynamic
                    elem->obj.dyn = lua_ix_int(L, 2);
                    elem->type = CROW_CASL_ELEM_DYNAMIC;
                    break;
                case 'M': // Mutable
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_MUTABLE, 1);
                    break;
                case 'N': // Named mutable
                    elem->obj.var[0] = lua_ix_int(L, 2);
                    elem->type = CROW_CASL_ELEM_MUTABLE;
                    break;
                case '~': // Negate
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_NEGATE, 1);
                    break;
                case '+': // Add
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_ADD, 2);
                    break;
                case '-': // Subtract
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_SUB, 2);
                    break;
                case '*': // Multiply
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_MUL, 2);
                    break;
                case '/': // Divide
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_DIV, 2);
                    break;
                case '%': // Modulo
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_MOD, 2);
                    break;
                case '#': // Mutate
                    casl_allocating_capture(self, elem, L, CROW_CASL_ELEM_MUTATE, 1);
                    break;
                default:
                    printf("ERROR: Unknown composite To char '%c'\n", index);
                    break;
            }
            
            lua_pop(L, 1);
            break;
        }
            
        default:
            printf("ERROR: Unknown To type\n");
            elem->obj.f = 0.0f;
            elem->type = CROW_CASL_ELEM_FLOAT;
            break;
    }
}

// Lua binding functions
extern "C" int l_casl_describe(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    
    if (lua_gettop(L) < 2) {
        luaL_error(L, "casl_describe requires table argument");
        return 0;
    }
    
    crow_casl_describe(channel, L);
    return 0;
}

extern "C" int l_casl_action(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    int action = luaL_checkinteger(L, 2);
    
    crow_casl_action(channel, action);
    return 0;
}

extern "C" int l_casl_defdynamic(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    int result = crow_casl_defdynamic(channel);
    lua_pushinteger(L, result);
    return 1;
}

extern "C" int l_casl_cleardynamics(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    crow_casl_cleardynamics(channel);
    return 0;
}

extern "C" int l_casl_setdynamic(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    int dynamic_ix = luaL_checkinteger(L, 2);
    float val = luaL_checknumber(L, 3);
    
    crow_casl_setdynamic(channel, dynamic_ix, val);
    return 0;
}

extern "C" int l_casl_getdynamic(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    int dynamic_ix = luaL_checkinteger(L, 2);
    
    float result = crow_casl_getdynamic(channel, dynamic_ix);
    lua_pushnumber(L, result);
    return 1;
}
