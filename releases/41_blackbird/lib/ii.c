// ii leader, ported from crow (monome/crow lib/ii.c, GPL-3.0). The follower
// side (crow answering as ii.self) isn't here yet.

#include "ii.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ii_c_layer.h"  // generated from lib/ii/*.lua (CMakeLists.txt)
#include "ii_i2c.h"
#include "caw.h"         // Caw_printf
#include "../lua/src/lua.h"

#define II_MAX_BROADCAST_LEN 9 // cmd byte + 4*s16 args
#define II_MAX_RECEIVE_LEN 10
#define II_QUEUE_LENGTH 16
#define II_GET 128  // cmd >= are getter requests
#define II_TT_VOLT  ((float)1638.3)
#define II_TT_iVOLT ((float)1.0/II_TT_VOLT)

typedef struct{
    uint8_t address;
    uint8_t length;
    uint8_t query_length; // 0 for broadcast, >0 is return type byte size
    uint8_t data[II_MAX_BROADCAST_LEN];
    uint8_t arg; // carried through to the reply event
    bool is_raw;
} ii_q_t;

static uint8_t type_size( ii_Type_t t );
static float decode( uint8_t* data, ii_Type_t type );
static uint8_t encode( uint8_t* dest, ii_Type_t type, float data );
static uint8_t encode_packet( uint8_t* dest, const ii_Cmd_t* c, uint8_t cmd, float* data );

// Leader queue (ring buffer). In flight: the message at the head, until the
// driver reports how it went.
static ii_q_t   l_iq[II_QUEUE_LENGTH];
static uint8_t  l_head = 0, l_count = 0;
static bool     in_flight = false;
static bool     retrying = false;
static uint8_t  follower_index = 1; // ii.address (follower mode, later)

// How each transaction ended, by ii_i2c_status_t (for tests and diagnosis)
volatile uint32_t g_ii_results[6];

static float lim_f( float v, float lo, float hi ){
    return v < lo ? lo : (v > hi ? hi : v);
}

////////////////////////
// setup

void ii_init( void ){
    ii_i2c_init();
    l_head = l_count = 0;
    in_flight = retrying = false;
}
const char* ii_list_modules( void ){
    return ii_module_list;
}
const char* ii_list_cmds( uint8_t address ){
    return ii_list_commands(address);
}
void ii_set_pullups( uint8_t state ){
    ii_i2c_set_pullups(state != 0); // the RP2040's weak internal pull-ups only
}
void ii_set_fastmode( uint8_t state ){
    ii_i2c_set_fast(state != 0);
}
uint8_t ii_get_address( void ){
    return follower_index;
}
void ii_set_address( uint8_t index ){
    follower_index = (index >= 1 && index <= 4) ? index : 1;
}


////////////////////////////////
// leader commands

static ii_q_t* queue_push( void ){
    if( l_count >= II_QUEUE_LENGTH ){ return NULL; }
    ii_q_t* q = &l_iq[(l_head + l_count) % II_QUEUE_LENGTH];
    l_count++;
    return q;
}
static void queue_pop( void ){
    if( l_count ){
        l_head = (l_head + 1) % II_QUEUE_LENGTH;
        l_count--;
    }
}

uint8_t ii_leader_enqueue( uint8_t address
                         , uint8_t cmd
                         , float*  data
                         )
{
    const ii_Cmd_t* c = ii_find_command(address, cmd);
    if( c == NULL ){ printf("ii: unknown command\n"); return 1; }
    ii_q_t* q = queue_push();
    if( q == NULL ){ Caw_printf("ii: queue full"); return 1; }

    q->is_raw = false;
    q->address = address;
    q->query_length = type_size( c->return_type );
    q->arg = data[0]; // save a copy of the first argument
    q->length = encode_packet( q->data, c, cmd, data );
    ii_pickle( &q->address, q->data, &q->length );
    return 0;
}

uint8_t ii_leader_enqueue_bytes( uint8_t  address
                               , uint8_t* data
                               , uint8_t  tx_len
                               , uint8_t  rx_len
                               )
{
    ii_q_t* q = queue_push();
    if( q == NULL ){ Caw_printf("ii: queue full"); return 1; }

    if( tx_len > II_MAX_BROADCAST_LEN ){ tx_len = II_MAX_BROADCAST_LEN; }
    if( rx_len > II_MAX_RECEIVE_LEN ){ rx_len = II_MAX_RECEIVE_LEN; }
    if( tx_len + rx_len > II_I2C_MAX_BYTES ){ rx_len = II_I2C_MAX_BYTES - tx_len; }

    q->is_raw = true;
    q->address = address;
    q->arg = tx_len > 1 ? data[1] : 0;
    q->query_length = rx_len;
    q->length = tx_len;
    memcpy( q->data, data, tx_len );
    return 0;
}

// A reply arrived: hand it to Lua (ii.lua: ii_LeadRx_handler -> ii.<dev>.event)
static void lead_callback( ii_q_t* q, uint8_t* rx_data )
{
    uint8_t address = q->address;
    uint8_t command = q->data[0];
    if( !q->is_raw ){ ii_unpickle( &address, &command, rx_data ); }
    ii_Type_t return_type = ii_s32T;
    const ii_Cmd_t* cmd = ii_find_command(address, command);
    if( cmd != NULL ){
        return_type = cmd->return_type;
    }
    float value = decode( rx_data, return_type );

    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    if( L == NULL ){ return; }
    lua_getglobal(L, "ii_LeadRx_handler");
    if( lua_type(L, -1) != LUA_TFUNCTION ){ lua_pop(L, 1); return; }
    lua_pushinteger(L, address);
    lua_pushinteger(L, command);
    lua_pushinteger(L, q->arg);
    lua_pushnumber(L, value);
    if( lua_pcall(L, 4, 0, 0) != LUA_OK ){
        Caw_printf("lua runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void ii_leader_process( void )
{
    uint8_t rx[II_I2C_MAX_BYTES];

    if( in_flight ){
        ii_i2c_status_t s = ii_i2c_poll(rx);
        if( s == II_I2C_BUSY ){ return; }
        in_flight = false;
        if( (unsigned)s < 6 ){ g_ii_results[s] = g_ii_results[s] + 1; }
        ii_q_t* q = &l_iq[l_head];
        switch( s ){
            case II_I2C_DONE:
                retrying = false;
                if( q->query_length ){ lead_callback( q, rx ); }
                queue_pop();
                break;
            case II_I2C_NAK: // nobody at that address: drop it quietly, as crow does
                retrying = false;
                queue_pop();
                break;
            default: // timeout or bus error: retry once, then abandon
                if( retrying ){
                    retrying = false;
                    queue_pop();
                    Caw_printf( (s == II_I2C_TIMEOUT)
                        ? "ii: bus stuck or lines low. check ii devices and connections."
                        : "ii: bus error." );
                } else {
                    retrying = true;
                }
                break;
        }
        return; // start the next one on the next pass
    }

    if( l_count == 0 || !ii_i2c_idle() ){ return; }
    ii_q_t* q = &l_iq[l_head];
    if( ii_i2c_start( q->address, q->data, q->length, q->query_length ) ){
        in_flight = true;
    }
}


/////////////////////////////////////
// ii Type Encode/Decode (as crow)

static uint8_t type_size( ii_Type_t t )
{
    switch(t){ case ii_void:  return 0;
               case ii_u8:    return 1;
               case ii_s8:    return 1;
               case ii_u16:   return 2;
               case ii_s16:   return 2;
               case ii_s16V:  return 2;
               case ii_s16ms: return 2;
               case ii_float: return 4;
               case ii_s32T:  return 4;
               default:       return 0;
    }
    return 0;
}

static float decode( uint8_t* data, ii_Type_t type )
{
    float val = 0; // return value default to zero
    uint16_t u16 = 0;
    int16_t s16;
    switch( type ){
        case ii_u8:
            val = (float)(*data++);
            break;
        case ii_s8:
            val = (float)(*(int8_t*)data++);
            break;
        case ii_u16:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            val = (float)u16;
            break;
        case ii_s16:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16;
            break;
        case ii_s16V:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16 * II_TT_iVOLT; // Scale Teletype down to float
            break;
        case ii_s16ms:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16 / 1000.0f;
            break;
        case ii_float:
            memcpy(&val, data, 4);
            break;
        case ii_s32T:
            // seconds: signed 16
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16;
            // subsecond: signed 16 V
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val += (float)s16 * II_TT_iVOLT;
            break;
        default: printf("ii_decode unmatched\n"); break;
    }
    return val;
}

static uint8_t encode( uint8_t* dest, ii_Type_t type, float data )
{
    uint8_t len = 0;
    uint8_t* d = dest;

    uint16_t u16; int16_t s16;
    switch( type ){
        case ii_u8: d[len++] = (uint8_t)lim_f(data,0.0f,255.0f);
            break;
        case ii_s8: d[len++] = (uint8_t)(int8_t)lim_f(data,-128.0f,127.0f);
            break;
        case ii_u16:
            u16 = (uint16_t)lim_f(data, 0.0f, 65535.0f);
            d[len++] = (uint8_t)(u16>>8);          // High byte first
            d[len++] = (uint8_t)(u16 & 0x00FF);    // Low byte
            break;
        case ii_s16V:
            data *= II_TT_VOLT; // Scale float up to Teletype
            // FLOWS THROUGH
        case ii_s16:
            s16 = (int16_t)lim_f(data, -32768.0f, 32767.0f);
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            break;
        case ii_s16ms:
            data *= 1000.0f;
            s16 = (int16_t)lim_f(data, -32768.0f, 32767.0f);
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            break;
        case ii_float:
            memcpy( &(d[len]), &data, 4 );
            len += 4;
            break;
        case ii_s32T:{
            int iTime = (int)data;
            float subTime = data - (float)iTime;
            if( iTime < -32768 ){
                iTime = -32768; subTime = 0.0f;
            } else if( iTime > 32767 ){
                iTime = 32767; subTime = 0.0f;
            }
            s16 = (int16_t)iTime;
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            subTime *= II_TT_VOLT; // Scale float up to Teletype
            s16 = (int16_t)subTime;
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            break;}
        default: return 0;
    }
    return len;
}

static uint8_t encode_packet( uint8_t* dest
                            , const ii_Cmd_t* c
                            , uint8_t cmd
                            , float* data
                            )
{
    uint8_t len = 0;
    dest[len++] = cmd; // first byte is command
    for( int i=0; i<(c->args); i++ ){
        len += encode( &dest[len], c->argtype[i], *data++ );
    }
    return len;
}
