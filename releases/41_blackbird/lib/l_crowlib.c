#include "l_crowlib.h"

#include <math.h>

#include "l_bootstrap.h" // l_bootstrap_dofile
#include "l_ii_mod.h"       // l_ii_mod_preload
#include "random.h"   // Random_Get()
#include "lib/ii.h"         // ii_*()
#include "lib/ashapes.h"    // AShaper_get_state
#include "lib/caw.h"        // Caw_printf()
// #include "lib/io.h"         // IO_GetADC() - not used in emulator
// Declare get_input_state_simple function for compatibility (implemented in main.cpp)
extern float get_input_state_simple(int channel); // returns input voltage in volts
#include "lib/events.h"     // event_t, event_post()
#include "lib/events_lockfree.h"  // Lock-free event queues
#include "lib/slopes.h"     // S_reset()
#include "ll_timers.h"       // Timer_Set_Block_Size()
#include <string.h>

#define L_CL_MIDDLEC 		(261.63f)
#define L_CL_MIDDLEC_INV 	(1.0f/L_CL_MIDDLEC)
#define L_CL_JIVOLT 		(1.0f/logf(2.f))


static int _ii_follow_reset( lua_State* L );
static int _random_arity_n( lua_State* L );
static int _tell_get_out( lua_State* L );
static int _tell_get_cv( lua_State* L );
static int _lua_void_function( lua_State* L );
static int _delay( lua_State* L );

// Forward declarations for L_handle_* functions
void L_handle_metro( event_t* e );
void L_handle_clock_resume( event_t* e );
void L_handle_clock_start( event_t* e );
void L_handle_clock_stop( event_t* e );

// function() end
// useful as a do-nothing callback
static int _lua_void_function( lua_State* L ){
	lua_settop(L, 0);
	return 0;
}

// ---- bb.priority implementation (file-scope) ----
// Behavior:
//  - bb.priority()            -> returns 'timing', 'balanced', 'accuracy', or current custom block size (int)
//  - bb.priority('timing')    -> sets size 480 (if still safe) and returns 'timing'
//  - bb.priority('balanced')  -> sets size 240 (if still safe) and returns 'balanced'
//  - bb.priority('accuracy')  -> sets size 4 (if safe) and returns 'accuracy'
//  - bb.priority(N)           -> sets size N (clamped to [1,MAX]) if safe;
//                                returns mapped string for 4/240/480 else the applied integer size
//  - After processing starts (guard active) requests are ignored; current descriptor returned.
int l_bb_priority(lua_State* L) {
    int nargs = lua_gettop(L);
    if (nargs >= 1) {
        if (lua_isnumber(L, 1)) {
            int requested = luaL_checkinteger(L, 1);
            if (requested < 1) requested = 1;
            if (requested > TIMER_BLOCK_SIZE_MAX) requested = TIMER_BLOCK_SIZE_MAX;
            (void)Timer_Set_Block_Size(requested); // deferred
        } else if (lua_isstring(L, 1)) {
            const char* requested = lua_tostring(L, 1);
            if (strcmp(requested, "accuracy") == 0) {
                (void)Timer_Set_Block_Size(4);
            } else if (strcmp(requested, "balanced") == 0) {
                (void)Timer_Set_Block_Size(240);
            } else if (strcmp(requested, "timing") == 0) {
                (void)Timer_Set_Block_Size(480);
            } else {
                // Unrecognized string: treat as 'balanced' (default)
                (void)Timer_Set_Block_Size(240);
            }
        } else {
            // Ignore other types
        }
    }

    lua_settop(L, 0);
    int current = Timer_Get_Block_Size();
    if (Timer_Block_Size_Change_Pending()) {
        // Report the target that's about to be applied
        int pending = Timer_Get_Block_Size(); // current still old; we don't expose internal pending value
        // We can't directly read pending here without extra API; keep reporting current classification
    }
    if (current == 4) {
        lua_pushstring(L, "accuracy");
    } else if (current == 240) {
        lua_pushstring(L, "balanced");
    } else if (current == 480) {
        lua_pushstring(L, "timing");
    } else {
        lua_pushinteger(L, current);
    }
    return 1;
}

static void _load_lib(lua_State* L, char* filename, char* luaname){
	lua_pushfstring(L, "lua/%s.lua", filename);
    l_bootstrap_dofile(L);
    lua_setglobal(L, luaname);
    lua_settop(L, 0);
}

// called after crowlib lua file is loaded
// here we add any additional globals and such
void l_crowlib_init(lua_State* L){

    //////// create a nop function
    lua_pushcfunction(L, _lua_void_function);
    lua_setglobal(L, "nop_fn");

	//////// load all libraries
	_load_lib(L, "input", "Input");
	_load_lib(L, "output", "Output");
	_load_lib(L, "asl", "asl");
	_load_lib(L, "asllib", "asllib");
	_load_lib(L, "metro", "metro");

    // load C funcs into lua env first
    l_ii_mod_preload(L);
	_load_lib(L, "ii", "ii");

	_load_lib(L, "calibrate", "cal");
	_load_lib(L, "sequins", "sequins");
	_load_lib(L, "public", "public");
	_load_lib(L, "clock", "clock");
	_load_lib(L, "quote", "quote");
	_load_lib(L, "timeline", "timeline");
	_load_lib(L, "hotswap", "hotswap");


	//////// crow.reset
    // Create crow table if it doesn't exist
    lua_getglobal(L, "crow");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "crow");
    } else {
        lua_pop(L, 1); // pop the existing crow table
    }
    
    // Now set crow.reset
    lua_getglobal(L, "crow"); // @1
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "reset"); // set crow.reset, pops function
    
    // Set crow.init (alias for crow.reset)  
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "init"); // set crow.init, pops function
    
    lua_pop(L, 1); // pop crow table


	//////// tell
	// C.tell = tell
    // NOTE: We set up _c.tell (crow.tell) in l_bootstrap.c, not here
    // Commenting out this code that tries to use a non-existent global 'tell' function
    // lua_getglobal(L, "crow"); // @1
    // lua_getglobal(L, "tell"); // @2
    // lua_setfield(L, 1, "tell");
    // lua_settop(L, 0);


	//////// get_out & get_cv
	lua_pushcfunction(L, _tell_get_out);
	lua_setglobal(L, "get_out");
	lua_pushcfunction(L, _tell_get_cv);
	lua_setglobal(L, "get_cv");
    lua_settop(L, 0);


	//////// input

	// -- Input
	// input = {1,2}
	// for chan = 1, #input do
	//   input[chan] = Input.new( chan )
	// end
	lua_createtable(L, 2, 0); // 2 array elements
	lua_setglobal(L, "input"); // -> @0

	lua_getglobal(L, "input"); // @1
	for(int i=1; i<=2; i++){
		lua_getglobal(L, "Input"); // @2
		lua_getfield(L, 2, "new"); // Output.new @3
		lua_pushinteger(L, i); // push the key
		lua_call(L, 1, 1); // Output.new(chan) -> replace key with value -> @3
		lua_pushinteger(L, i); // push the key
		lua_rotate(L, -2, 1); // swap top 2 elements
		lua_settable(L, 1); // output[chan] = result
		lua_settop(L, 1); // discard everything except _G.output
	}
	lua_settop(L, 0);


	//////// output (asl)

	// -- Output
	// output = {1,2,3,4}
	// for chan = 1, #output do
	// 	 output[chan] = Output.new( chan )
	// end
	lua_createtable(L, 4, 0); // 4 array elements
	lua_setglobal(L, "output"); // -> @0

	lua_getglobal(L, "output"); // @1
	for(int i=1; i<=4; i++){
		lua_getglobal(L, "Output"); // @2
		lua_getfield(L, 2, "new"); // Output.new @3
		lua_pushinteger(L, i); // push the key
		lua_call(L, 1, 1); // Output.new(chan) -> replace key with value -> @3
		lua_pushinteger(L, i); // push the key
		lua_rotate(L, -2, 1); // swap top 2 elements
		lua_settable(L, 1); // output[chan] = result
		lua_settop(L, 1); // discard everything except _G.output
	}
	lua_settop(L, 0);


	// LL_get_state = get_state
    lua_getglobal(L, "get_state");
    lua_setglobal(L, "LL_get_state");
	lua_settop(L, 0);


	//////// ii follower default actions

	// install the reset function
	lua_pushcfunction(L, _ii_follow_reset);
	lua_setglobal(L, "ii_follow_reset");

	// call it to reset immediately
	lua_getglobal(L, "ii_follow_reset");
	lua_call(L, 0, 0);
	lua_settop(L, 0);


	//////// ii.pullup(true)
	ii_set_pullups(1);


	//////// RANDOM

	// hook existing math.random into math.srandom
	lua_getglobal(L, "math"); // 1
	lua_getfield(L, 1, "random"); // 2
	lua_setfield(L, 1, "srandom");
	lua_settop(L, 1); // abandon anything above _G.math
	// _G.math is still at stack position 1
	lua_getfield(L, 1, "randomseed");
	lua_setfield(L, 1, "srandomseed");
	lua_settop(L, 0);

	// set math.random to the c-func for true random
	lua_getglobal(L, "math");
	lua_pushcfunction(L, _random_arity_n);
	lua_setfield(L, -2, "random");
	lua_settop(L, 0);


	//////// DELAY
	// creates a closure, so this is just way easier
	luaL_dostring(L,"function delay(action, time, repeats)\n"
						"local r = repeats or 0\n"
					    "return clock.run(function()\n"
					            "for i=1,1+r do\n"
					                "clock.sleep(time)\n"
					                "action(i)\n"
					            "end\n"
					        "end)\n"
					"end\n");

    l_crowlib_emptyinit(L);

    //////// bb table (create if missing) and add priority controls
    lua_getglobal(L, "bb"); // @1
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "bb");
    } else {
        lua_pop(L, 1);
    }

    // bb.priority getter/setter implemented in C for validation and dynamic block size
    // Use global-static so we can declare function at file scope (embedded C forbids nested funcs)
    // Forward declare function
    extern int l_bb_priority(lua_State* L);
    // Ensure default value is set before first use
    Timer_Set_Block_Size(480);
    lua_getglobal(L, "bb"); // @1
    lua_pushcfunction(L, l_bb_priority);
    lua_setfield(L, -2, "priority"); // bb.priority
    // Initialize block size to default 'timing' explicitly
    Timer_Set_Block_Size(480);
    lua_pop(L, 1); // pop bb
}

void l_crowlib_emptyinit(lua_State* L){
    //////// set init() to a NOP
    lua_getglobal(L, "nop_fn");
    lua_setglobal(L, "init");
}


int l_crowlib_crow_reset( lua_State* L ){
    S_reset();

    // Ensure bb.priority still exists after any user manipulations
    lua_getglobal(L, "bb"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "priority"); // @2
        if(lua_isnil(L, 2)){
            lua_pop(L, 2); // pop nil and bb
            lua_getglobal(L, "bb"); // @1
            lua_pushcfunction(L, l_bb_priority);
            lua_setfield(L, -2, "priority");
            lua_pop(L, 1);
        } else {
            lua_settop(L, 0); // priority exists
        }
    } else {
        lua_settop(L, 0);
    }

    lua_getglobal(L, "input"); // @1
for(int i=1; i<=2; i++){
        lua_settop(L, 1); // _G.input is TOS @1
lua_pushinteger(L, i); // @2
lua_gettable(L, 1); // replace @2 with: input[n]

        // input[n].mode = 'none'
        lua_pushstring(L, "none"); // @3
        lua_setfield(L, 2, "mode"); // pops 'none' -> @2

        // input[n].reset_events(input[n]) -- aka void method call
        lua_getfield(L, 2, "reset_events"); // @3
        lua_pushvalue(L, 2); // @4 copy of input[n]
        lua_call(L, 1, 0);
}
    lua_settop(L, 0);

    lua_getglobal(L, "output"); // @1
	for(int i=1; i<=4; i++){
        lua_settop(L, 1); // _G.output is TOS @1
		lua_pushinteger(L, i); // @2
		lua_gettable(L, 1); // replace @2 with: output[n]

        // output[n].slew = 0
        lua_pushnumber(L, 0.0); // @3
        lua_setfield(L, 2, "slew"); // pops 'slew' -> @2
        // output[n].volts = 0
        lua_pushnumber(L, 0.0); // @3
        lua_setfield(L, 2, "volts"); // pops 'volts' -> @2
        // output[n].scale('none')
        lua_getfield(L, 2, "scale");
        lua_pushstring(L, "none");
        lua_call(L, 1, 0);
        // output[n].done = function() end
        lua_getglobal(L, "nop_fn"); // @3
        lua_setfield(L, 2, "done"); // pops nop_fn -> @2
        // output[n]:clock('none')
        lua_getfield(L, 2, "clock"); // @3
        lua_pushvalue(L, 2); // @4 copy of output[n]
        lua_pushstring(L, "none");
        lua_call(L, 2, 0);

        // output[n].reset_events(output[n]) -- aka void method call
        lua_getfield(L, 2, "reset_events"); // @3
        lua_pushvalue(L, 2); // @4 copy of output[n]
        lua_call(L, 1, 0);
	}
	lua_settop(L, 0);

    // ii.reset_events(ii.self) - only if ii exists
    lua_getglobal(L, "ii"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "reset_events"); // @2
        if(!lua_isnil(L, 2)){
            lua_getfield(L, 1, "self"); // @3
            lua_call(L, 1, 0);
        }
    }
    lua_settop(L, 0);

    // ii_follow_reset() -- resets forwarding to output libs (only if exists)
    lua_getglobal(L, "ii_follow_reset");
    if(!lua_isnil(L, 1)){
        lua_call(L, 0, 0);
    }
    lua_settop(L, 0);

    // metro.free_all() - only if metro exists
    lua_getglobal(L, "metro"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "free_all");
        if(!lua_isnil(L, 2)){
            lua_call(L, 0, 0);
        }
    }
    lua_settop(L, 0);

    // if public then public.clear() end
    lua_getglobal(L, "public"); // @1
    if(!lua_isnil(L, 1)){ // if public is not nil
    	lua_getfield(L, 1, "clear");
    	if(!lua_isnil(L, 2)){
            lua_call(L, 0, 0);
        }
    }
    lua_settop(L, 0);

    // clock.cleanup() - only if clock exists
    lua_getglobal(L, "clock"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "cleanup");
        if(!lua_isnil(L, 2)){
            lua_call(L, 0, 0);
        }
    }
    lua_settop(L, 0);

    // hotswap.cleanup() - only if hotswap exists
    lua_getglobal(L, "hotswap"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "cleanup");
        if(!lua_isnil(L, 2)){
            lua_call(L, 0, 0);
        }
    }
    lua_settop(L, 0);

    // bb.pulseout[1]:low() and bb.pulseout[2]:low() - reset pulse outputs to low
    lua_getglobal(L, "bb"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "pulseout"); // @2
        if(!lua_isnil(L, 2)){
            for(int i = 1; i <= 2; i++){
                lua_pushinteger(L, i); // @3
                lua_gettable(L, 2); // @3 = bb.pulseout[i]
                if(!lua_isnil(L, 3)){
                    lua_getfield(L, 3, "low"); // @4 = bb.pulseout[i].low
                    if(!lua_isnil(L, 4)){
                        lua_pushvalue(L, 3); // @5 = self (bb.pulseout[i])
                        lua_call(L, 1, 0); // bb.pulseout[i]:low()
                    }
                }
                lua_settop(L, 2); // pop back to bb.pulseout
            }
        }
    }
    lua_settop(L, 0);

    return 0;
}


/////// static declarations

// Just Intonation calculators
// included in lualink.c as global lua functions

static int justvolts(lua_State* L, float mul);

int l_crowlib_justvolts(lua_State* L){
	return justvolts(L, 1.f);
}

int l_crowlib_just12(lua_State *L){
	return justvolts(L, 12.f);
}

int l_crowlib_hztovolts(lua_State *L){
	// assume numbers, not tables
	float retval = 0.f;
	switch(lua_gettop(L)){
		case 1: // use default middleC reference
			// note we 
			retval = log2f(luaL_checknumber(L, 1) * L_CL_MIDDLEC_INV);
			break;
		case 2: // use provided reference
			retval = log2f(luaL_checknumber(L, 1)/luaL_checknumber(L, 2));
			break;
		default:
			lua_pushliteral(L, "need 1 or 2 args");
			lua_error(L);
			break;
	}
    lua_settop(L, 0);
	lua_pushnumber(L, retval);
	return 1;
}

static int justvolts(lua_State* L, float mul){
	// apply optional offset
	float offset = 0.f;
	switch(lua_gettop(L)){
		case 1: break;
		case 2: {offset = log2f(luaL_checknumber(L, 2))*mul;} break;
		default:
			lua_pushliteral(L, "need 1 or 2 args");
			lua_error(L);
			break;
	}

	// now do the conversion
	int nresults = 0;
	switch(lua_type(L, 1)){
		case LUA_TNUMBER:{
			float result = log2f(lua_tonumber(L, 1))*mul + offset;
			lua_settop(L, 0);
			lua_pushnumber(L, result);
			nresults = 1;
			break;}
		case LUA_TTABLE:{
			// get length of table to convert
			lua_len(L, 1);
			int telems = lua_tonumber(L, -1);
			lua_pop(L, 1);

			// build the new table in C (a copy)
			float newtab[telems+1]; // bottom element is unused
			for(int i=1; i<=telems; i++){
				lua_geti(L, 1, i);
				newtab[i] = log2f(luaL_checknumber(L, -1))*mul + offset;
				lua_pop(L, 1); // pops the number from the stack
			}

			// push the C table into the lua table
			lua_settop(L, 0);
			lua_createtable(L, telems, 0);
			for(int i=1; i<=telems; i++){
				lua_pushnumber(L, newtab[i]);
				lua_seti(L, 1, i);
			}
			nresults = 1;
			break;}
		default:
			lua_pushliteral(L, "unknown voltage type");
			lua_error(L);
			break;
	}
	return nresults;
}

/// true random

static int _random_arity_n( lua_State* L )
{
    int nargs = lua_gettop(L);
    switch(nargs){
        case 0:{
            float r = Random_Float();
            lua_settop(L, 0);
            lua_pushnumber(L, r);
            break;}
        case 1:{
            int r = Random_Int(1, luaL_checknumber(L, 1));
            lua_settop(L, 0);
            lua_pushinteger(L, r);
            break;}
        default:{
            int r = Random_Int(luaL_checknumber(L, 1)
                              ,luaL_checknumber(L, 2));
            lua_settop(L, 0);
            lua_pushinteger(L, r);
            break;}
    }
    return 1;
}

// ii follower default actions

// function(chan,val) output[chan].volts = val end
static int _ii_self_volts( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float val = luaL_checknumber(L, 2);
	lua_settop(L, 0);
	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2
	lua_pushnumber(L, val); // 3
	lua_setfield(L, 2, "volts");
	lua_settop(L, 0);
	return 0;
}

// function(chan,val) output[chan].volts = val end
static int _ii_self_slew( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float slew = luaL_checknumber(L, 2);
	lua_settop(L, 0);
	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2
	lua_pushnumber(L, slew); // 3
	lua_setfield(L, 2, "slew");
	lua_settop(L, 0);
	return 0;
}

// function() crow.reset() end
static int _ii_self_reset( lua_State* L ){
	lua_getglobal(L, "crow"); // 1
	lua_getfield(L, 1, "reset");
	lua_call(L, 0, 0);
	lua_settop(L, 0);
	return 0;
}

// function(chan,ms,volts,pol) output[chan](pulse(ms,volts,pol)) end
static int _ii_self_pulse( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float ms = luaL_checknumber(L, 2);
	float volts = luaL_checknumber(L, 3);
	float pol = luaL_checknumber(L, 4);
	lua_settop(L, 0);

	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2

	lua_getglobal(L, "pulse"); // 3
	lua_pushnumber(L, ms);
	lua_pushnumber(L, volts);
	lua_pushnumber(L, pol);
	lua_call(L, 3, 1); // calls 'ramp' and leaves asl table @3
	lua_call(L, 1, 0); // calls output[chan]({asl-table})
	lua_settop(L, 0);
	return 0;
}

// function(chan,atk,rel,volts) output[chan](ar(atk,rel,volts)) end
static int _ii_self_ar( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float atk = luaL_checknumber(L, 2);
	float rel = luaL_checknumber(L, 3);
	float volts = luaL_checknumber(L, 4);
	lua_settop(L, 0);

	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2

	lua_getglobal(L, "ar"); // 3
	lua_pushnumber(L, atk);
	lua_pushnumber(L, rel);
	lua_pushnumber(L, volts);
	lua_call(L, 3, 1); // calls 'ar' and leaves asl table @3
	lua_call(L, 1, 0); // calls output[chan]({asl-table})
	lua_settop(L, 0);
	return 0;
}


// -- convert freq to seconds where freq==0 is 1Hz
// function(chan,freq,level,skew) output[chan](ramp(math.pow(2,-freq),skew,level)) end
static int _ii_self_lfo( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float freq = luaL_checknumber(L, 2);
	float level = luaL_checknumber(L, 3);
	float skew = luaL_checknumber(L, 4);
	lua_settop(L, 0);

	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2

	lua_getglobal(L, "ramp"); // 3
	lua_pushnumber(L, powf(2.0, -freq));
	lua_pushnumber(L, skew);
	lua_pushnumber(L, level);
	lua_call(L, 3, 1); // calls 'ramp' and leaves asl table @3
	lua_call(L, 1, 0); // calls output[chan]({asl-table})
	lua_settop(L, 0);
	return 0;
}

static int _ii_follow_reset( lua_State* L ){
	lua_getglobal(L, "ii"); // @1
	lua_getfield(L, 1, "self"); // @2

	lua_pushcfunction(L, _ii_self_volts); // @3
	lua_setfield(L, 2, "volts");
	lua_pushcfunction(L, _ii_self_slew);
	lua_setfield(L, 2, "slew");
	lua_pushcfunction(L, _ii_self_reset);
	lua_setfield(L, 2, "reset");
	lua_pushcfunction(L, _ii_self_pulse);
	lua_setfield(L, 2, "pulse");
	lua_pushcfunction(L, _ii_self_ar);
	lua_setfield(L, 2, "ar");
	lua_pushcfunction(L, _ii_self_lfo);
	lua_setfield(L, 2, "lfo");

	lua_settop(L, 0);
	return 0;
}


// C.tell( 'output', channel, get_state( channel ))
static int _tell_get_out( lua_State* L ){
	int chan = luaL_checknumber(L, -1);
    Caw_printf( "^^output(%i,%f)", chan, (double)AShaper_get_state(chan-1));
    lua_settop(L, 0);
    return 0;
}

// C.tell( 'stream', channel, io_get_input( channel ))
static int _tell_get_cv( lua_State* L ){
int chan = luaL_checknumber(L, -1);
    Caw_printf( "^^stream(%i,%f)", chan, get_input_state_simple(chan-1));
    lua_settop(L, 0);
    return 0;
}

// Lock-free metro queuing function - replaces old mutex-based version
void L_queue_metro( int id, int state )
{
    // Try lock-free queue first (much faster, never blocks Core 1)
    if (metro_lockfree_post(id, state)) {
        return; // Success - event queued without blocking
    }
    
    // Lock-free queue full - fallback to mutex queue (rare case)
    printf("Warning: Lock-free metro queue full, using fallback\n");
    event_t e = { .handler = L_handle_metro
                , .index.i = id
                , .data.i  = state
                };
    event_post(&e);
}

// Forward declarations for output batching (defined in main.cpp)
extern void output_batch_begin(void);
extern void output_batch_flush(void);

// New lock-free metro handler - processes events from lock-free queue
void L_handle_metro_lockfree( metro_event_lockfree_t* event )
{
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    
    if (!L) {
        printf("L_handle_metro_lockfree: no Lua state available\n");
        return;
    }
    
    // ===============================================
    // OPTIMIZATION 2: Enable batching for metro callbacks
    // ===============================================
    output_batch_begin();
    
    int metro_id = event->metro_id;
    int stage = event->stage;
    
    // Call the global metro_handler function in Lua like real crow
    lua_getglobal(L, "metro_handler");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, metro_id);  // First argument: metro ID
        lua_pushinteger(L, stage);     // Second argument: stage/count
        
        // Protected call to prevent crashes
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("metro_handler error: %s\n", error ? error : "unknown");
            lua_pop(L, 1);
        }
    } else {
        // metro_handler not defined - this is normal if no metros are active
        lua_pop(L, 1);
    }
    
    // ===============================================
    // OPTIMIZATION 2: Flush batched outputs
    // ===============================================
    output_batch_flush();
}

void L_queue_clock_resume( int coro_id )
{
    event_t e = { .handler = L_handle_clock_resume
                , .index.i = coro_id
                };
    event_post(&e);
}

void L_queue_clock_start( void )
{
    event_t e = { .handler = L_handle_clock_start };
    event_post(&e);
}

void L_queue_clock_stop( void )
{
    event_t e = { .handler = L_handle_clock_stop };
    event_post(&e);
}

// Handler functions - L_handle_metro calls Lua metro_handler function
void L_handle_metro( event_t* e )
{
    // This function is called from the event system when a timer fires
    // It needs to call the Lua metro_handler function safely
    
    extern lua_State* get_lua_state(void); // Forward declaration
    lua_State* L = get_lua_state();
    
    if (!L) {
        printf("L_handle_metro: no Lua state available\n");
        return;
    }
    
    int metro_id = e->index.i;
    int stage = e->data.i;
    
    // Call the global metro_handler function in Lua like real crow
    lua_getglobal(L, "metro_handler");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, metro_id);  // First argument: metro ID
        lua_pushinteger(L, stage);     // Second argument: stage/count
        
        // Protected call to prevent crashes
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("metro_handler error: %s\n", error ? error : "unknown");
            lua_pop(L, 1);
        }
    } else {
        // metro_handler not defined - this is normal if no metros are active
        lua_pop(L, 1);
    }
}

void L_handle_clock_resume( event_t* e )
{
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    
    if (!L) {
        printf("L_handle_clock_resume: no Lua state available\n");
        return;
    }
    
    int coro_id = e->index.i;
    
    // Call the global clock_resume_handler function in Lua
    lua_getglobal(L, "clock_resume_handler");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, coro_id);  // Pass coroutine ID
        
        // Protected call to prevent crashes
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("clock_resume_handler error: %s\n", error ? error : "unknown");
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}

void L_handle_clock_start( event_t* e )
{
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    
    if (!L) {
        printf("L_handle_clock_start: no Lua state available\n");
        return;
    }
    
    // Call the global clock_start_handler function in Lua
    lua_getglobal(L, "clock_start_handler");
    if (lua_isfunction(L, -1)) {
        // Protected call to prevent crashes
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("clock_start_handler error: %s\n", error ? error : "unknown");
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}

void L_handle_clock_stop( event_t* e )
{
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    
    if (!L) {
        printf("L_handle_clock_stop: no Lua state available\n");
        return;
    }
    
    // Call the global clock_stop_handler function in Lua
    lua_getglobal(L, "clock_stop_handler");
    if (lua_isfunction(L, -1)) {
        // Protected call to prevent crashes
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("clock_stop_handler error: %s\n", error ? error : "unknown");
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}
