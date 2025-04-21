#include "ComputerCard.h"
#include <cmath> // for sin

class StickControl : public ComputerCard
{
public:
   
	constexpr static unsigned tableSize = 512;
	int16_t sine[tableSize];

	// Bitwise AND of index integer with tableMask will wrap it to table size
	constexpr static uint32_t tableMask = tableSize - 1;

	// Sine wave phase (0-2^32 gives 0-2pi phase range)
	uint32_t phase;
	
	StickControl()
	{
		// Initialise phase of sine wave to 0
		phase = 0;
		
		for (unsigned i=0; i<tableSize; i++)
		{
			// just shy of 2^15 * sin
			sine[i] = int16_t(32000*sin(2*i*M_PI/double(tableSize)));
		}

	}

    virtual void ProcessSample()
    {
		//Lookup table pieces from Chris Johnson's sine_wave_lookup example

        uint32_t index = phase >> 23; // convert from 32-bit phase to 9-bit lookup table index
		int32_t r = (phase & 0x7FFFFF) >> 7; // fractional part is last 23 bits of phase, shifted to 16-bit 

		// Look up this index and next index in lookup table
		int32_t s1 = sine[index];
		int32_t s2 = sine[(index+1) & tableMask];

		// Linear interpolation of s1 and s2, using fractional part
		// Shift right by 20 bits
		// (16 bits of r, and 4 bits to reduce 16-bit signed sine table to 12-bit output)
		int32_t out = (s2 * r + s1 * (65536 - r)) >> 20;
    }

private:

    // 32-bit random number generator
    uint32_t rand()
    {
        static uint32_t lcg_seed = 1;
        lcg_seed = 1664525 * lcg_seed + 1013904223;
        return lcg_seed;
    }
};

int main()
{
    StickControl stCtrl;
    stCtrl.EnableNormalisationProbe();
    stCtrl.Run();
}
