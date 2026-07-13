#pragma once

// Random number generator stub for RP2040 Workshop Computer
// Replaces the original crow random functionality

// Get a random float between 0.0 and 1.0
float Random_Float(void);

// Get a random integer between min and max (inclusive)
int Random_Int(int min, int max);

// Initialize random number generator with seed
void Random_Init(unsigned int seed);
