#include "ComputerCard.h"
#include "click.h"
#include <cmath> // for sin

#define L 1
#define R 0

#define GATE_HIGH 2047
#define GATE_LOW 0

void tempTap();
void longHold();

class StickCtrl : public ComputerCard
{
public:
	constexpr static unsigned tableSize = 512;
	int16_t sine[tableSize];

	// Bitwise AND of index integer with tableMask will wrap it to table size
	constexpr static uint32_t tableMask = tableSize - 1;

	uint32_t mixReadPhases[6];
	uint64_t virtualFaders[6];

	uint32_t sampleCounter;

	uint32_t drummers[2] = {0, 0};

	int16_t activePulses[6] = {0, 0, 0, 0, 0, 0};
	// int16_t pulseMap[6] = {0, 2, 4, 1, 3, 5};

	constexpr static bool paradiddle[8] = {R, L, R, R, L, R, L, L};
	constexpr static bool sonClave[16] = {R, L, L, R, L, L, R, L, L, L, R, L, R, L, L, L};
	constexpr static bool sixStrokeRoll[6] = {R, L, L, R, R, L};
	constexpr static bool stickMap[6] = {L, R, L, R, L, R};

	uint32_t sixteenthNoteMs;

	uint32_t sixteenthNoteSamples = 12000;

	Click tap = Click(tempTap, longHold);

	bool tapping = false;
	bool switchHold = false;
	bool resync = false;

	bool sixteenthPulse = false;
	bool sixEightPulse = false;

	uint32_t tapTime = 0;
	uint32_t tapTimeLast = 0;

	int8_t startPhase0 = 0;
	int8_t startPhase1 = 0;

	int8_t paradiddleLength = 16;
	int8_t latinGrooveLength = 5;

	StickCtrl()
	{
		// Constructor

		for (unsigned i = 0; i < tableSize; i++)
		{
			// just shy of 2^15 * sin
			sine[i] = int16_t(32000 * sin(2 * i * M_PI / double(tableSize)));
		}

		for (int i = 0; i < 6; i++)
		{
			mixReadPhases[i] = rnd() << 16; // random phases for our one knob mixer
		}

		sampleCounter = 0;
	}

	virtual void ProcessSample()
	{
		if (SwitchVal() != Switch::Down && switchHold)
		{
			switchHold = false;
		}

		tap.Update(SwitchVal() == Switch::Down);

		if (resync)
		{
			sampleCounter = 0;
			sixteenthNoteSamples = sixteenthNoteMs * 48; // 48kHz
			resync = false;
		}

		int16_t pulseWidth = 200;

		sixteenthPulse = sampleCounter % sixteenthNoteSamples == 0;
		sixEightPulse = sampleCounter % (sixteenthNoteSamples * 3 / 4) == 0;

		if (SwitchVal() == Switch::Up)
		{
			drummers[0] = startPhase0;
			drummers[1] = startPhase1;
		}
		else
		{
			if (sixteenthPulse)
			{
				switch (paradiddle[drummers[0] % 8])
				{
				case L:
					activePulses[0] = pulseWidth;
					break;
				case R:
					activePulses[1] = pulseWidth;
					break;
				}

				switch (sonClave[drummers[0]])
				{
				case L:
					activePulses[2] = pulseWidth;
					break;
				case R:
					activePulses[3] = pulseWidth;
					break;
				}

				if (!switchHold)
				{
					drummers[0] = (drummers[0] + 1) % paradiddleLength;
				}
			}

			if (sixEightPulse)
			{
				switch (sixStrokeRoll[drummers[1] % 6])
				{
				case L:
					activePulses[4] = pulseWidth;
					break;
				case R:
					activePulses[5] = pulseWidth;
					break;
				}

				if (!switchHold)
				{
					// deliberately wacky here because I liked the five pulse pattern
					drummers[1] = (drummers[1] + 1) % latinGrooveLength;

					// if (drummers[1] >= latinGrooveLength - 1)
					// {
					// 	drummers[1] = startPhase1;
					// }
				}
			}
		}

		int16_t outputs[6] = {0, 0, 0, 0, 0, 0};

		for (int i = 0; i < 6; i++)
		{
			if (activePulses[i] > 0)
			{
				activePulses[i]--;
				outputs[i] = GATE_HIGH;
			}
			else
			{
				outputs[i] = GATE_LOW;
			}
		}

		for (int i = 0; i < 6; i++)
		{
			virtualFaders[i] = sineLookup(mixReadPhases[i] + ((uint64_t)KnobVal(Knob::Main) * 0xFFFFFFFF >> 12));
			// outputs[i] = (outputs[i] * virtualFaders[i]) >> 12;
			int brightness = outputs[i] * 8190 >> 12;
			LedBrightness(i, outputs[i] ? outputs[i] + 2048 : 0);
		}

		AudioOut1(outputs[0]);
		AudioOut2(outputs[1]);
		CVOut1(outputs[2]);
		CVOut2(outputs[3]);
		PulseOut1(outputs[4]);
		PulseOut2(outputs[5]);

		sampleCounter += 1;
		sampleCounter %= 0xFFFFFFFF;

		tapTimeLast = (pico_millis() - tapTime) % 0xFFFFFFFF;
		if (tapTimeLast > 2000 && tapping)
		{
			tapping = false;
		}
	}

private:
	// a slightly more complex random number generator than usual to ensure reseting Computer produces different results
	// (and to make it more difficult to reverse engineer) << Copilot added this wtf my code is super readable! (jokes)
	int32_t rnd()
	{
		static uint32_t lcg_seed = UniqueCardID() & 0xFFFFFFFF; // 32-bit LCG seed from unique cardID
		lcg_seed ^= (uint32_t)time_us_64();						// XOR with time to add some randomness
		lcg_seed ^= KnobVal(Knob::Main) << 20;					// XOR with main knob value to add some randomness
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed;
	}

	int16_t sineLookup(uint64_t phase)
	{
		// Lookup table pieces from Chris Johnson's sine_wave_lookup example. Thanks Chris!

		// wrap phase to 32 bits
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

		return (int16_t)out;
	}
};

// define this here so that it can be used in the click library
// There is probably a better way to do this but I don't know it
StickCtrl stCtrl;

int main()
{
	stCtrl.EnableNormalisationProbe();
	stCtrl.Run();
}

// Callbacks for click library to call based on Switch state
void tempTap()
{
	// first tap
	if (!stCtrl.tapping)
	{
		stCtrl.tapTime = pico_millis();
		stCtrl.tapping = true;
	}
	else // second tap
	{
		unsigned long sinceLast = pico_millis() - stCtrl.tapTime;
		if (sinceLast > 50 && sinceLast < 3000)
		{ // ignore bounces and forgotten taps > 2 seconds

			stCtrl.tapTime = pico_millis(); // record time ready for next tap
			stCtrl.sixteenthNoteMs = sinceLast;
			stCtrl.resync = true;
		}
	}
}

void longHold()
{
	stCtrl.switchHold = true;
}
