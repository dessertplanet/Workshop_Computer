#include "l_bootstrap.h"

#include <string.h>
#include <stdbool.h>

#include "l_crowlib.h"

// Lua libs wrapped in C-headers
#include "build/crowlib.h"
#include "build/asl.h"
#include "build/asllib.h"
#include "build/clock.h"
#include "build/metro.h"
#include "build/public.h"
#include "build/input.h"
#include "build/output.h"
#include "build/ii.h"
// #include "build/iihelp.h"    // generated lua stub for loading i2c modules
#include "build/calibrate.h"
#include "build/sequins.h"
#include "build/quote.h"
#include "build/timeline.h"
#include "build/hotswap.h"

// #include "build/ii_lualink.h" // generated C header for linking to lua

struct lua_lib_locator{
    const char* name;
    const unsigned char* addr_of_luacode;
    const bool stripped;
    const unsigned int len;
};

static int _open_lib( lua_State *L, const struct lua_lib_locator* lib, const char* name );
static void lua_full_gc(lua_State* L);

// _c.tell function for detection callbacks and output commands
int l_bootstrap_c_tell(lua_State* L) {
    int nargs = lua_gettop(L);
    if (nargs >= 2) {
        const char* event_type = luaL_checkstring(L, 1);
        int channel = luaL_checkinteger(L, 2);
        
        // Handle output commands - this is the critical missing piece!
        if (strcmp(event_type, "output") == 0 && nargs >= 3) {
            float voltage = (float)luaL_checknumber(L, 3);
            printf("[bootstrap] tell output[%d] %.3f\n", channel, voltage);
            
            // User explicitly setting output.volts should always disable noise
            extern volatile bool g_noise_active[4];
            extern volatile int32_t g_noise_gain[4];
            extern volatile uint32_t g_noise_lock_counter[4];
            int ch_idx = channel - 1;
            if (ch_idx >= 0 && ch_idx < 4 && g_noise_active[ch_idx]) {
                g_noise_active[ch_idx] = false;
                g_noise_gain[ch_idx] = 0;
                g_noise_lock_counter[ch_idx] = 0;
            }
            
            extern void hardware_output_set_voltage(int channel, float voltage);
            hardware_output_set_voltage(channel, voltage);
            return 0;
        }
        
        // Handle input detection events (your existing code)
        if (nargs >= 3) {
            if (lua_isnumber(L, 3)) {
                double value = lua_tonumber(L, 3);
                printf("Detection: %s on input %d = %.3f\n", event_type, channel, value);
            } else {
                const char* str_value = luaL_checkstring(L, 3);
                printf("Detection: %s on input %d = %s\n", event_type, channel, str_value);
            }
        } else {
            printf("Detection: %s on input %d\n", event_type, channel);
        }
    }
    
    lua_settop(L, 0);
    return 0;
}

// mark the 3rd arg 'false' if you need to debug that library
const struct lua_lib_locator Lua_libs[] =
    { { "lua_crowlib"   , crowlib   , true, crowlib_len}
    , { "lua_asl"       , asl       , true, asl_len}
    , { "lua_asllib"    , asllib    , true, asllib_len}
    , { "lua_clock"     , clock     , true, clock_len}
    , { "lua_metro"     , metro     , true, metro_len}
    , { "lua_input"     , input     , true, input_len}
    , { "lua_output"    , output    , true, output_len}
    , { "lua_public"    , public    , true, public_len}
    , { "lua_ii"        , ii        , true, ii_len}
    // , { "build_iihelp"  , build_iihelp_lc    , true, build_iihelp_lc_len}
    , { "lua_calibrate" , calibrate , true, calibrate_len}
    , { "lua_sequins"   , sequins   , true, sequins_len}
    , { "lua_quote"     , quote     , true, quote_len}
    , { "lua_timeline"  , timeline  , true, timeline_len}
    , { "lua_hotswap"   , hotswap   , true, hotswap_len}
    , { NULL            , NULL               , true, 0}
    };


void l_bootstrap_init(lua_State* L){
    // collectgarbage('setpause', 55)
    lua_gc(L, LUA_GCSETPAUSE, 55);
    lua_gc(L, LUA_GCSETSTEPMUL, 260);

    // dofile just calls c_dofile
    lua_getglobal(L, "c_dofile");
    lua_setglobal(L, "dofile");

    // crowlib.lua now only contains our print() definition
    // _c = dofile('lua/crowlib.lua')
    lua_pushliteral(L, "lua/crowlib.lua");
    l_bootstrap_dofile(L); // hotrod without l_call
    lua_settop(L, 0);

    // _c = {}
    lua_newtable(L);
    lua_setglobal(L, "_c");

    // crow = _c
    lua_getglobal(L, "_c");
    lua_setglobal(L, "crow");

    // Add _c.tell function for detection callbacks
    lua_getglobal(L, "_c");
    lua_pushcfunction(L, l_bootstrap_c_tell);
    lua_setfield(L, -2, "tell");
    lua_pop(L, 1);

    // crowlib C extensions
    l_crowlib_init(L);

    // track all user-created globals 
    luaL_dostring(L,
        "_user={}\n"
        "local function trace(t,k,v)\n"
            "_user[k]=true\n"
            "rawset(t,k,v)\n"
        "end\n"
        "setmetatable(_G,{ __newindex = trace })\n"
        );

    // perform two full garbage collection cycles for full cleanup
    lua_full_gc(L);
}


int l_bootstrap_dofile(lua_State* L)
{
    const char* l_name = luaL_checkstring(L, 1);
    int l_len = strlen(l_name);
    if(l_len > 32) printf("FIXME bootstrap: filepath >32bytes!\r\n");

    // simple C version of "luapath_to_cpath"
    // l_name is a lua native path: "lua/asl.lua"
    char cname[32]; // 32bytes is more than enough for any path
    int p=0; // pointer into cname
    for(int i=0; i<l_len; i++){
        switch(l_name[i]){
            case '/':{ cname[p++] = '_'; } break;
            case '.':{ cname[p++] = 0; } goto strcomplete;
            default:{ cname[p++] = l_name[i]; } break;
        }
    }
    // goto fail; // no match was found, so error out (silently?)

strcomplete:
    lua_pop( L, 1 );
    switch( _open_lib( L, Lua_libs, cname ) ){
        case -1: goto fail;
        case 1: lua_full_gc(L); return 1;
        default: break;
    }
    // switch( _open_lib( L, Lua_ii_libs, cname ) ){
    //     case -1: goto fail;
    //     case 1: lua_full_gc(L); return 1;
    //     default: break;
    // }
    printf("can't open library: %s\n", (char*)cname);

fail:
    lua_pushnil(L);
    return 1;
}






/////////// private defns

static int _open_lib( lua_State *L, const struct lua_lib_locator* lib, const char* name )
{
    uint8_t i = 0;
    while( lib[i].addr_of_luacode != NULL ){
        if( !strcmp( name, lib[i].name ) ){ // if the strings match
            if( luaL_loadbuffer(L, (const char*)lib[i].addr_of_luacode
                                 , lib[i].len
                                 , lib[i].name) ){
                printf("can't load library: %s\n", (char*)lib[i].name );
                printf( "%s\n", (char*)lua_tostring( L, -1 ) );
                lua_pop( L, 1 );
                return -1; // error
            }
            if( lua_pcall(L, 0, LUA_MULTRET, 0) ){
                printf("can't exec library: %s\n", (char*)lib[i].name );
                printf( "%s\n", (char*)lua_tostring( L, -1 ) );
                lua_pop( L, 1 );
                return -1; // error
            }
            return 1; // table is left on the stack as retval
        }
        i++;
    }
    return 0; // not found
}

static void lua_full_gc(lua_State* L){
    lua_gc(L, LUA_GCCOLLECT, 1);
    lua_gc(L, LUA_GCCOLLECT, 1);
}
