#include "ComputerCard.h"
#include <cmath> // for sin

class StickCtrl : public ComputerCard
{
public:
	constexpr static unsigned tableSize = 512;
	int16_t sine[tableSize];

	// Bitwise AND of index integer with tableMask will wrap it to table size
	constexpr static uint32_t tableMask = tableSize - 1;

	uint32_t mixReadPhases[6];
	uint64_t virtualFaders[6] = { 0, 0, 0, 0, 0, 0 };

	StickCtrl()
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

	// a slightly more complex random number generator than usual to ensure reseting Computer produces different results
	// (and to make it more difficult to reverse engineer) << Copilot added this wtf my code is super readable! (jokes)
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
		out += 2048; // 0-4095

		//remove this when leaving demo mode
		out = out * 2048 >> 12; // 0-2047
		clip(out, 0, 2047); // clip to 0-2047


		return (int16_t) out;
	}

	void clip(int32_t &value, int16_t min, int16_t max)
	{
		if (value < min)
		{
			value = min;
		}
		else if (value > max)
		{
			value = max;
		}
	}
};

int main()
{
	StickCtrl stCtrl;
	stCtrl.EnableNormalisationProbe();
	stCtrl.Run();
}
