#pragma once

#include <stdbool.h>
#include "slopes.h" // For q16_t type

#define MAX_DIV_LIST_LEN 24
#define ASHAPER_CHANNELS 4

typedef struct{
    int    index;
    float  divlist[MAX_DIV_LIST_LEN];
    int    dlLen;
    float  modulo;
    float  scaling;
    float  offset;
    bool   active;
    float  state;
    
    // Q16 versions for fast quantization without conversions
    q16_t  divlist_q16[MAX_DIV_LIST_LEN];
    q16_t  modulo_q16;
    q16_t  scaling_q16;
    q16_t  offset_q16;
} AShape_t;

void AShaper_init( int channels );

void AShaper_unset_scale( int index );
void AShaper_set_scale( int    index
                      , float* divlist
                      , int    dlLen
                      , float  modulo
                      , float  scaling
                      );
float AShaper_get_state( int index );

float* AShaper_v( int     index
                , float*  out
                , int     size
                );

float AShaper_quantize_single( int index, float voltage );
q16_t AShaper_quantize_single_q16( int index, q16_t voltage_q16 );
