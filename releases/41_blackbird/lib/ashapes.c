#include "ashapes.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

int ashaper_count = 0;
AShape_t* ashapers = NULL;

void AShaper_init( int channels )
{
    ashaper_count = channels;
    ashapers = malloc( sizeof( AShape_t ) * channels );
    if( !ashapers ){ printf("ashapers malloc failed\n"); return; }
    
    for( int j=0; j<ASHAPER_CHANNELS; j++ ){
        ashapers[j].index  = j;
        for( int d=0; d<MAX_DIV_LIST_LEN; d++ ){
            ashapers[j].divlist[d] = d; // ascending vals to 24
        }
        ashapers[j].dlLen   = 12;
        ashapers[j].modulo  = 12.0;
        ashapers[j].scaling = 1.0;
        ashapers[j].offset  = 0.0;
        ashapers[j].active  = false;  // Default to pass-through mode
        ashapers[j].state   = 0.0;
    }
}

void AShaper_unset_scale( int index )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return; }
    AShape_t* self = &ashapers[index];

    self->active = false;  // Set to pass-through mode
}

void AShaper_set_scale( int    index
                      , float* divlist
                      , int    dlLen
                      , float  modulo
                      , float  scaling
                      )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return; }
    AShape_t* self = &ashapers[index];

    // For now, just store the parameters but don't activate
    // This allows future implementation of quantization without breaking existing code
    self->dlLen = (dlLen > 24 ) ? 24 : dlLen;
    if( self->dlLen == 0 ){
        self->dlLen = 1;
        self->divlist[0] = 0.0;
        self->modulo = 1.0;
        self->scaling = scaling / modulo;
    } else {
        for( int i=0; i<(self->dlLen); i++ ){
            self->divlist[i] = divlist[i];
        }
        self->modulo = modulo;
        self->scaling = scaling;
    }

    self->offset = 0.5 * self->scaling / self->modulo;
    
    // TODO: Implement quantization later
    // For now, keep as pass-through
    // self->active = true;
    printf("AShaper_set_scale called for channel %d (quantization not implemented)\n", index + 1);
}

float AShaper_get_state( int index )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return 0.0; }
    AShape_t* self = &ashapers[index];

    return self->state;
}

// Pass-through implementation - just saves the last value for state queries
float* AShaper_v( int     index
                , float*  out
                , int     size
                )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return out; }
    AShape_t* self = &ashapers[index];

    // Always pass-through for now (quantization disabled)
    // Just save the last value for state queries
    if( size > 0 ){
        self->state = out[size-1];
    }
    
    return out;  // Pass-through unchanged
}
