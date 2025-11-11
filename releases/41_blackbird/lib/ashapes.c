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
    
    self->active = true;
}

float AShaper_get_state( int index )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return 0.0; }
    AShape_t* self = &ashapers[index];

    return self->state;
}

// TODO optimization
float* AShaper_v( int     index
                , float*  out
                , int     size
                )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return out; }
    AShape_t* self = &ashapers[index]; // safe pointer

    if( !self->active ){ // shaper inactive so just return
        self->state = out[size-1]; // save latest value
        return out;
    }

    float* out2 = out;
    for( int i=0; i<size; i++ ){
        float samp = *out2 + self->offset; // apply shift for centering and transpose

        float n_samp = samp/self->scaling; // samp normalized to [0,1.0)

        float divs = floorf(n_samp);
        float phase = n_samp - divs; // [0,1.0)

        int note = (int)(phase * self->dlLen); // map phase to num of note choices
        float note_map = self->divlist[note]; // apply lookup table
        note_map /= self->modulo; // remap via num of options

        *out2++ = self->scaling * (divs + note_map);
    }
    self->state = out[size-1]; // save last value
    return out;
}

// Single-sample quantization for real-time hardware output
// CRITICAL: Place in RAM - called from shaper_v() on every block
__attribute__((section(".time_critical.AShaper_quantize_single")))
float AShaper_quantize_single( int index, float voltage )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return voltage; }
    AShape_t* self = &ashapers[index];

    if( !self->active ){ // quantization disabled
        return voltage;
    }

    // Apply the same quantization algorithm as AShaper_v
    float samp = voltage + self->offset; // apply shift for centering and transpose
    float n_samp = samp / self->scaling; // samp normalized to [0,1.0)
    
    float divs = floorf(n_samp);
    float phase = n_samp - divs; // [0,1.0)
    
    int note = (int)(phase * self->dlLen); // map phase to num of note choices
    float note_map = self->divlist[note]; // apply lookup table
    note_map /= self->modulo; // remap via num of options
    
    return self->scaling * (divs + note_map);
}
