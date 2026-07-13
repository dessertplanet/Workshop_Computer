#include "random.h"

#include <stdlib.h>
#include <time.h>
#include <stdio.h>

// Random number generator implementation for RP2040 Workshop Computer
// Uses standard C library functions for simplicity

static int random_initialized = 0;

void Random_Init(unsigned int seed) {
    srand(seed);
    random_initialized = 1;
    printf("Random: Initialized with seed %u\n", seed);
}

float Random_Float(void) {
    if (!random_initialized) {
        // Initialize with current time if not already initialized
        Random_Init((unsigned int)time(NULL));
    }
    
    return (float)rand() / (float)RAND_MAX;
}

int Random_Int(int min, int max) {
    if (!random_initialized) {
        // Initialize with current time if not already initialized
        Random_Init((unsigned int)time(NULL));
    }
    
    if (min > max) {
        int temp = min;
        min = max;
        max = temp;
    }
    
    return min + (rand() % (max - min + 1));
}
