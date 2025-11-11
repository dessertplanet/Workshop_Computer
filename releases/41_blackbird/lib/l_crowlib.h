#pragma once

#include "lua.h" // Fixed path for our Lua build
#include "lauxlib.h"
#include "lualib.h"
#include "events_lockfree.h"  // For metro_event_lockfree_t type

// initialize the default crow environment variables & data structures
void l_crowlib_init(lua_State* L);

// destroys user init() function and replaces it with a void fn
void l_crowlib_emptyinit(lua_State* L);

// execute crow.reset() which reverts state of all modules to default
int l_crowlib_crow_reset( lua_State* L );

int l_crowlib_justvolts(lua_State *L);
int l_crowlib_just12(lua_State *L);
int l_crowlib_hztovolts(lua_State *L);

// L_queue_* functions for event posting
void L_queue_metro( int id, int state );
void L_queue_clock_resume( int coro_id );
void L_queue_clock_start( void );
void L_queue_clock_stop( void );

// Lock-free event handler functions
void L_handle_metro_lockfree( metro_event_lockfree_t* event );
void L_handle_input_lockfree( input_event_lockfree_t* event );

// Expose bb.priority binding so other C code (main.cpp) can attach it
int l_bb_priority(lua_State* L);
