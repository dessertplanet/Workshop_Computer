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

	uint32_t mixReadPhases[6];
	uint64_t virtualFaders[6] = { 0, 0, 0, 0, 0, 0 };

	StickControl()
	{
		//Constructor

		for (unsigned i = 0; i < tableSize; i++)
		{
			// just shy of 2^15 * sin
			sine[i] = int16_t(32000 * sin(2 * i * M_PI / double(tableSize)));
		}

		for (int i = 0; i < 6; i++)
		{
			mixReadPhases[i] = rnd() << 16; // random phases for our one knob mixer
		}
	}

	virtual void ProcessSample()
	{
		// Lookup table pieces from Chris Johnson's sine_wave_lookup example

		for (int i = 0; i < 6; i++)
		{
			virtualFaders[i] = sineLookup(mixReadPhases[i] + ((uint64_t)KnobVal(Knob::Main) * 0xFFFFFFFF >> 12));
		}

		AudioOut1(virtualFaders[0]);
		AudioOut2(virtualFaders[1]);
		CVOut1(virtualFaders[2]);
		CVOut2(virtualFaders[3]);
	}

private:
	int32_t rnd()
	{
		static uint32_t lcg_seed = UniqueCardID() & 0xFFFFFFFF; // 32-bit LCG seed from unique cardID
		lcg_seed ^= (uint32_t)time_us_64(); // XOR with time to add some randomness
		lcg_seed ^= KnobVal(Knob::Main) << 20; // XOR with main knob value to add some randomness
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed;
	}

	int16_t sineLookup(uint64_t phase)
	{
		// Lookup table pieces from Chris Johnson's sine_wave_lookup example. Thanks Chris!

		//wrap phase to 32 bits
		if (phase >= 0xFFFFFFFF)
		{
			phase -= 0xFFFFFFFF;
		}
		if (phase < 0)
		{
			phase += 0xFFFFFFFF;
		}

		uint32_t index = phase >> 23;		 // convert from 32-bit phase to 9-bit lookup table index
		int32_t r = (phase & 0x7FFFFF) >> 7; // fractional part is last 23 bits of phase, shifted to 16-bit

		// Look up this index and next index in lookup table
		int32_t s1 = sine[index];
		int32_t s2 = sine[(index + 1) & tableMask];

		// Linear interpolation of s1 and s2, using fractional part
		// Shift right by 20 bits
		// (16 bits of r, and 4 bits to reduce 16-bit signed sine table to 12-bit output)
		int32_t out = (s2 * r + s1 * (65536 - r)) >> 20;

		return (int16_t) out;
	}
};

int main()
{
	StickControl stCtrl;
	stCtrl.EnableNormalisationProbe();
	stCtrl.Run();
}
