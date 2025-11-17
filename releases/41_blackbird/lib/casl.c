#include "casl.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h> // floorf

// TODO
// add sequins data type
// add random math operator
// dynamics should be available for SHAPEs (though not mutables)
// switch to a pooled memory store to enable more complex ASLs to borrow memory from cheap ones

#define SELVES_COUNT 4
static Casl* _selves[SELVES_COUNT];

static int casl_defdynamicP( Casl* self );

Casl* casl_init( int index )
{
    if(index < 0 || index >= SELVES_COUNT){ return NULL; }

    Casl* self = malloc(sizeof(Casl));
    if(!self){ printf("Casl* malloc!\n"); return NULL; }

    _selves[index] = self; // save ref for indexed lookup

    self->to_ix = 0; // To* allocation count
    self->seq_current = &self->seqs[0];
    self->seq_ix  = 0;  // counts up allocation of Sequence slots
    self->seq_select = -1; // current 'parent'
    self->dyn_ix = 0;

    self->holding = false;
    self->locked = false;

    return self;
}

static void seq_enter( Casl* self )
{
    if(self->seq_ix >= SEQ_COUNT){
        printf("ERROR: no sequences left!\n");
        return;
    }
    Sequence* s = &self->seqs[self->seq_ix];
    self->seq_current = s; // save as 'active' Sequence

    s->length = 0;
    s->pc     = 0;
    s->parent = self->seq_select;

    self->seq_select = self->seq_ix; // select the new Sequence
    self->seq_ix++; // marks the sequence as allocated
}

static void seq_exit( Casl* self )
{
    self->seq_select = self->seq_current->parent; // move up tree
    self->seq_current = &self->seqs[self->seq_select]; // save the new node
}

static void seq_append( Casl* self, To* t )
{
    Sequence* s = self->seq_current;
    if(s->length >= SEQ_LENGTH){
        printf("ERROR: no stages left!\n");
    }
    s->stage[s->length] = t; // append To* to end of Sequence
    s->length++;
}

static void parse_table( Casl* self, lua_State* L );
void casl_describe( int index, lua_State* L )
{
    if(index < 0 || index >= SELVES_COUNT){
        printf("casl_describe: invalid index %d (valid 0..%d)\n", index, SELVES_COUNT-1);
        return;
    }
    Casl* self = _selves[index];

    // deallocate everything
    self->to_ix  = 0;
    self->seq_ix = 0;
    self->seq_select = -1; // current 'parent'
    // reset all sequences
    self->seq_current = &self->seqs[0]; // first sequence
    for(int i=0; i<SEQ_COUNT; i++){ self->seqs[i].pc = 0; } // reset all program counters

    // enter first sequence
    seq_enter(self);

    parse_table(self, L);
    // seq_exit(self)? // i think we want to start inside the first Seq anyway
}

// suite of functions for unwrapping elements of Lua tables
static int ix_type( lua_State* L, int ix )
{
    lua_pushnumber(L, ix); // query first table elem
    lua_gettable(L, -2);  // get above key from table on stack
    int ix_type = lua_type(L, -1); // types from lua.h
    lua_pop(L, 1);
    return ix_type;
}
static void ix_str( char *result, lua_State* L, int ix, int length )
{
    lua_pushnumber(L, ix); // query first table elem
    lua_gettable(L, -2);  // get above key from table on stack
    const char *s = luaL_checkstring(L, -1);
    for (int i = 0; i < length; ++i) {
        result[i] = s[i];
    }
    lua_pop(L, 1);
}
static const char ix_char( lua_State* L, int ix )
{
    lua_pushnumber(L, ix); // query first table elem
    lua_gettable(L, -2);  // get above key from table on stack
    const char ix_char = luaL_checkstring(L, -1)[0]; // types from lua.h
    lua_pop(L, 1);
    return ix_char;
}
static float ix_num( lua_State* L, int ix )
{
    lua_pushnumber(L, ix); // query first table elem
    lua_gettable(L, -2);  // get above key from table on stack
    float ix_num = luaL_checknumber(L, -1); // types from lua.h
    lua_pop(L, 1);
    return ix_num;
}
static float ix_bool( lua_State* L, int ix )
{
    lua_pushnumber(L, ix); // query first table elem
    lua_gettable(L, -2);  // get above key from table on stack
    float ix_num = lua_toboolean(L, -1) ? 1.0 : 0.0;
    lua_pop(L, 1);
    return ix_num;
}
static int ix_int( lua_State* L, int ix )
{
    lua_pushnumber(L, ix); // query first table elem
    lua_gettable(L, -2);  // get above key from table on stack
    int ix_int = luaL_checkinteger(L, -1); // types from lua.h
    lua_pop(L, 1);
    return ix_int;
}

static To* to_alloc( Casl* self )
{
    if(self->to_ix >= TO_COUNT){ return NULL; }
    To* t = &self->tos[self->to_ix]; // get next To
    self->to_ix++; // mark as allocated
    return t;
}

static void read_to( Casl* self, To* t, lua_State* L );
static void capture_elem( Casl* self, Elem* e, lua_State* L, int ix );
static void parse_table( Casl* self, lua_State* L )
{
    switch( ix_type(L, 1) ){ // types from lua.h

        case LUA_TSTRING: { // TO, RECUR
            To* t = to_alloc(self);
            if(t == NULL){
                printf("ERROR: not enough To slots left\n");
                return;
            }
            seq_append(self, t);
            switch( ix_char(L, 1) ){
                case 'T': read_to(self, t, L); break; // standard To
                case 'R':{ t->ctrl = ToRecur; break; } // Recur ctrlflow
                case 'I':{ // If ctrlflow
                    capture_elem(self, &(t->a), L, 2); // capture predicate from ix[2] to t->a
                    t->ctrl = ToIf;
                    break; }
                case 'H':{ t->ctrl = ToHeld; break; }
                case 'W':{ t->ctrl = ToWait; break; }
                case 'U':{ t->ctrl = ToUnheld; break; }
                case 'L':{ t->ctrl = ToLock; break; }
                case 'O':{ t->ctrl = ToOpen; break; }
                default:
                    printf("ERROR char not found\n");
                    break;
            }
            break;}

        case LUA_TTABLE:{ // NEST
            To* t = to_alloc(self);
            seq_append(self, t);
            t->ctrl = ToEnter; // mark as entering a sub-Seq
            seq_enter(self);
            t->a.obj.seq = self->seq_select; // pass this To* to seq_enter, and do this line in there
            int seq_len = lua_rawlen(L, -1);
            for( int i=1; i<=seq_len; i++ ){ // Lua is 1-based
                lua_pushnumber(L, i); // grab the next elem
                lua_gettable(L, -2); // push that inner-table to the stack
                parse_table(self, L); // RECUR
                lua_pop(L, 1); // pops inner-table
            }
            seq_exit(self);
            break;}

        default:
            printf("ERROR unhandled parse type\n");
            printf("ERROR ASL unhandled type. Do you have a function in your ASL? Replace it with dyn.\n");
            break;
    }
}

static void read_to( Casl* self, To* t, lua_State* L )
{
    capture_elem(self, &(t->a), L, 2);
    capture_elem(self, &(t->b), L, 3);
    capture_elem(self, &(t->c), L, 4);
    t->ctrl = ToLiteral;
}

static void allocating_capture( Casl* self, Elem* e, lua_State* L, ElemT t, int count )
{
    e->type = t;
    for(int i=0; i<count; i++){
        int var = casl_defdynamicP(self); // allocate a dynamic to hold op. FIXME 0 is asl id
        e->obj.var[i] = var; // capture dynamic ref
        capture_elem(self, &self->dynamics[var], L, i+2); // recur to capture operand
    }
}

// REFACTOR to have it return the Elem (and copy it) rather than passing a pointer
static void capture_elem( Casl* self, Elem* e, lua_State* L, int ix )
{
    switch( ix_type(L, ix) ){ // type of table elem
        case LUA_TNUMBER:{
            float f = ix_num(L, ix);
            e->obj.q = FLOAT_TO_Q16(f);  // Convert float to Q16 at Lua boundary
            e->type = ElemT_Fixed;
            break;}

        case LUA_TBOOLEAN:{
            float f = ix_bool(L, ix);
            e->obj.q = FLOAT_TO_Q16(f);  // Convert bool (as float) to Q16
            e->type = ElemT_Fixed;
            break;}

        case LUA_TSTRING:{
            char s[2];
            ix_str(s, L, ix, 2);
            e->obj.shape = S_str_to_shape( s );
            e->type = ElemT_Shape;
            break;}

        case LUA_TTABLE: // handle behavioural-types
            lua_pushnumber(L, ix); // push ix of To
            lua_gettable(L, -2);  // unwrap To[ix]
            char index = ix_char(L, 1); // parse on first char at ix[1]
            switch( index ){ // parse on first char at ix[1]
                case 'D':{ // DYNAMIC
                    e->obj.dyn = ix_int(L, 2); // grab dyn_ix at ix[2]
                    e->type = ElemT_Dynamic;
                    break;}
                case 'M': // MUTABLE
                    allocating_capture(self, e, L, ElemT_Mutable, 1); break;
                case 'N':{// NAMED MUTABLE. combination of dynamic & mutable for live update
                    e->obj.var[0] = ix_int(L, 2); // grab dyn_ix at ix[2]
                    e->type = ElemT_Mutable;
                    break;}
                case '~': allocating_capture(self, e, L, ElemT_Negate, 1); break;
                case '+': allocating_capture(self, e, L, ElemT_Add, 2); break;
                case '-': allocating_capture(self, e, L, ElemT_Sub, 2); break;
                case '*': allocating_capture(self, e, L, ElemT_Mul, 2); break;
                case '/': allocating_capture(self, e, L, ElemT_Div, 2); break;
                case '%': allocating_capture(self, e, L, ElemT_Mod, 2); break;
                case '#': allocating_capture(self, e, L, ElemT_Mutate, 1); break;

                default:
                    printf("ERROR composite To char '%c'not found\n",index);
                    break;
            }
            lua_pop(L, 1);
            break;

        default:
            printf("ERROR unknown To type\n");
            break;
    }
}

///////////////////////////////
// Runtime

static To* seq_advance( Casl* self )
{
    Sequence* s = self->seq_current;
    To* t = NULL;
    if( s->pc < s->length ){ // stages remain
        t = s->stage[s->pc]; // get next stage
        s->pc++; // select following stage
    }
    return t;
}

// return true if there is an upval
static bool seq_up( Casl* self )
{
    if( self->seq_current->parent < 0 ){ return false; } // nothing left to do

    self->seq_current->pc = 0; // RESET PC so it runs more than once
    // TODO this will prob change when it comes to nesting conditionals?
    // TODO or at least when it comes to behavioural data

    self->seq_select = self->seq_current->parent;
    self->seq_current = &self->seqs[self->seq_select];
    return true;
}

static void seq_down( Casl* self, int s_ix )
{
    self->seq_select = s_ix;
    self->seq_current = &self->seqs[self->seq_select];
}

static void next_action( int index );
static bool find_control( Casl* self, ToControl ctrl, bool full_search );
static ElemO resolve( Casl* self, Elem* e );

void casl_action( int index, int action )
{
    if(index < 0 || index >= SELVES_COUNT){
        printf("casl_action: invalid index %d (valid 0..%d)\n", index, SELVES_COUNT-1);
        return;
    }
    Casl* self = _selves[index];

    if( self->locked ){ // can't apply action until unlocked
        if( action == 2 ){ self->locked = false; } // 'unlock' message received
        return; // doesn't trigger action
    }
    if( action == 1){ // restart sequence
        self->seq_current = &self->seqs[0]; // first sequence
        for(int i=0; i<SEQ_COUNT; i++){ self->seqs[i].pc = 0; } // reset all program counters
        self->holding = false;
        self->locked = false;
    } else if( action == 0 && self->holding ){ // goto release if held
        if( find_control(self, ToUnheld, false) ){
            self->holding = false;
        } else {
            printf("couldn't find ToWait. restarting\n");
            casl_action(index, 1);
            return;
        }
    } else {
        printf("do nothing\n");
        return;
    }
    next_action(index);
}

static void next_action( int index )
{
    if(index < 0 || index >= SELVES_COUNT){ return; }
    Casl* self = _selves[index];

    while(true){ // repeat until halt
        To* t = seq_advance(self);
        if(t){ // To is valid
            switch(t->ctrl){
                case ToLiteral:{
                    q16_t volts_q16 = resolve(self, &t->a).q;     // Q16.16 volts
                    q16_t seconds_q16 = resolve(self, &t->b).q;   // Q16.16 seconds
                    // Convert seconds to milliseconds: seconds * 1000
                    q16_t ms_q16 = Q16_MUL(seconds_q16, FLOAT_TO_Q16(1000.0));
                    
                    S_toward_q16( index              // Use Q16 API directly
                                , volts_q16
                                , ms_q16
                                , resolve(self, &t->c).shape
                                , (ms_q16 > 0) ? &next_action : NULL // callback only if there's a time delay
                                );
                    if(ms_q16 > 0){ return; } // wait for DSP callback before proceeding
                    break;}

                case ToIf:{
                    if( resolve(self, &t->a).q <= 0 ){ // Q16 comparison (0 is still 0 in Q16)
                        goto stepup; // WARNING! ensuring only 1 path to asl_done event
                    }
                    break;}

                case ToRecur:{  self->seq_current->pc = 0;    break;}
                case ToEnter:   seq_down(self, t->a.obj.seq); break;
                case ToHeld:{   self->holding = true;         break;}
                case ToWait:    /* halt execution */    return;
                case ToUnheld:{ self->holding = false;        break;} // this is never executed, but here for reference
                case ToLock:{   self->locked = true;          break;}
                case ToOpen:{   self->locked = false;         break;}
            }
        } else {
stepup:
            if( !seq_up(self) ){ // To invalid. Jump up. return if nothing left to do
                // ASL sequence completed - trigger "done" callback via event system
                // This is safe with the mailbox inter-core communication system
                extern void L_queue_asl_done(int channel);
                L_queue_asl_done(index);
                return;
            }
        }
    }
}

static bool find_control( Casl* self, ToControl ctrl, bool full_search )
{
    To* t = seq_advance(self);
    if(t){ // To is valid
        if(t->ctrl == ctrl){ return true; } // FOUND IT
        switch(t->ctrl){ // else handle navigation
            case ToEnter:
                if(full_search){ seq_down(self, t->a.obj.seq); }
                return find_control(self, ctrl, full_search);
            case ToIf:
                if( !full_search ){ seq_up(self); } // skip If contents
                // FALLS THROUGH
            default:
                return find_control(self, ctrl, full_search);
        }
    } else if( seq_up(self) ){ // end of list. Jump up the retstk
        return find_control(self, ctrl, full_search); // recur
    }
    return false;
}

// resolves behavioural types to a literal value
static volatile uint16_t resolving_mutable; // tmp global var for cheaper recursive fn
#define RESOLVE_VAR_Q16(self, e, n) _resolve(self, &self->dynamics[e->obj.var[n]] ).q
static ElemO _resolve( Casl* self, Elem* e )
{
    switch( e->type ){
        case ElemT_Dynamic: return _resolve(self, &self->dynamics[e->obj.dyn] );
        case ElemT_Mutable:{
            resolving_mutable = e->obj.var[0];
            return _resolve(self, &self->dynamics[e->obj.var[0]] );}
        case ElemT_Negate: return (ElemO){.q = -RESOLVE_VAR_Q16(self,e,0)};
        case ElemT_Add: return (ElemO){.q = RESOLVE_VAR_Q16(self,e,0) + RESOLVE_VAR_Q16(self,e,1)};
        case ElemT_Sub: return (ElemO){.q = RESOLVE_VAR_Q16(self,e,0) - RESOLVE_VAR_Q16(self,e,1)};
        case ElemT_Mul: return (ElemO){.q = Q16_MUL(RESOLVE_VAR_Q16(self,e,0), RESOLVE_VAR_Q16(self,e,1))};
        case ElemT_Div: return (ElemO){.q = Q16_DIV(RESOLVE_VAR_Q16(self,e,0), RESOLVE_VAR_Q16(self,e,1))};
        case ElemT_Mod:{
            // Q16.16 modulo operation
            q16_t val_q16  = RESOLVE_VAR_Q16(self,e,0);
            q16_t wrap_q16 = RESOLVE_VAR_Q16(self,e,1);
            // Division: val / wrap, result in Q16
            q16_t div_result = Q16_DIV(val_q16, wrap_q16);
            // Floor: extract integer part by shifting down and back up
            q16_t floored = (div_result >> Q16_SHIFT) << Q16_SHIFT;
            // Multiply: floor * wrap
            q16_t mul_result = Q16_MUL(wrap_q16, floored);
            return (ElemO){.q = val_q16 - mul_result};}
        case ElemT_Mutate:{
            ElemO mutated = (ElemO){.q = RESOLVE_VAR_Q16(self,e,0)};
            if(resolving_mutable < DYN_COUNT){
                self->dynamics[resolving_mutable].obj = mutated; // update value
                resolving_mutable = DYN_COUNT; // mutation resolved!
            }
            return mutated;} // return the resultant value
        default: return e->obj;
    }
}
#undef RESOLVE_VAR_Q16

// wrap _resolve with mutable resolution
static ElemO resolve( Casl* self, Elem* e )
{
    resolving_mutable = DYN_COUNT; // out of range
    ElemO eo = _resolve(self, e);
    if(resolving_mutable < DYN_COUNT){
        self->dynamics[resolving_mutable].obj = eo; // update value
    }
    return eo;
}

////////////////////////////////////
// Dynamic Variables
// allows concurrent access of registered vars from C and Lua

int casl_defdynamic( int index )
{
    if(index < 0 || index >= SELVES_COUNT){ return -1; }
    return casl_defdynamicP(_selves[index]);
}

int casl_defdynamicP( Casl* self )
{
    if(self->dyn_ix >= DYN_COUNT){
        printf("ERROR: no dynamic slots remain\n");
        return -1;
    }
    int ix = self->dyn_ix; self->dyn_ix++;
    return ix;
}

void casl_cleardynamics( int index )
{
    if(index < 0 || index >= SELVES_COUNT){ return; }
    Casl* self = _selves[index];

    self->dyn_ix = 0;
}

void casl_setdynamic( int index, int dynamic_ix, float val )
{
    if(index < 0 || index >= SELVES_COUNT){ return; }
    Casl* self = _selves[index];

    self->dynamics[dynamic_ix].obj.q = FLOAT_TO_Q16(val);  // Convert float to Q16
    self->dynamics[dynamic_ix].type  = ElemT_Fixed;
}

float casl_getdynamic( int index, int dynamic_ix )
{
    if(index < 0 || index >= SELVES_COUNT){ return 0.0; }
    Casl* self = _selves[index];

    switch(self->dynamics[dynamic_ix].type){
        case ElemT_Fixed:
            return Q16_TO_FLOAT(self->dynamics[dynamic_ix].obj.q);  // Convert Q16 to float
        default:
            printf("getdynamic! wrong type\n");
            return 0.0;
    }
}
