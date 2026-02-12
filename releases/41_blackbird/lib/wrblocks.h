#pragma once

#include <stdint.h>

// Essential wrDsp functions for slopes system
// Copied from submodules/wrDsp for local use

float* b_cp( float* dest, float src, int size ); // memset
float* b_cp_v( float* dest, float* src, int size );
float* b_add( float* io, float add, int size );
float* b_sub( float* io, float sub, int size );
float* b_mul( float* io, float mul, int size );
float* b_accum_v( float* dest, float* src, int size ); // += *src

typedef float (b_fn_t)( float in );
float* b_map( b_fn_t fn, float* io, int size );
