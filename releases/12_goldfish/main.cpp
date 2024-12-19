#include "ComputerCard.h"
#include <cmath>

/// Goldfish

class Goldfish : public ComputerCard
{
public:
	virtual void ProcessSample()
	{

		int16_t amp1 = static_cast<int16_t>((KnobVal(Knob::X) << 15) / 4095); // Convert to Q15 format
		int16_t amp2 = static_cast<int16_t>((KnobVal(Knob::Y) << 15) / 4095); // Convert to Q15 format

		int16_t cvMix = 0;
		int16_t thing1 = 0;
		int16_t thing2 = 0;
		bool clockPulse = false;
		bool effectiveSampleRate = false;

		int16_t rand = static_cast<int16_t>(LFrnd());

		if (Connected(Input::CV1) && Connected(Input::CV2))
		{
			thing1 = static_cast<int16_t>((CVIn1() * amp1) >> 15);
			thing2 = static_cast<int16_t>((CVIn2() * amp2) >> 15);
		}
		else if (Connected(Input::CV1))
		{
			thing1 = static_cast<int16_t>((CVIn1() * amp1) >> 15);
			thing2 = static_cast<int16_t>((2048 * (amp2 - (1 << 14))) >> 15); // 0.5 in Q15 format is 1 << 14
		}
		else if (Connected(Input::CV2))
		{
			thing1 = static_cast<int16_t>((amp1 * rand) >> 15);
			thing2 = static_cast<int16_t>((CVIn2() * amp2) >> 15);
		}
		else
		{
			thing1 = static_cast<int16_t>((amp1 * rand) >> 15);
			thing2 = static_cast<int16_t>((2048 * (amp2 - (1 << 14))) >> 15); // 0.5 in Q15 format is 1 << 14
		};

		cvMix = static_cast<int16_t>(((thing1 * (4095 - KnobVal(Knob::Main))) + (thing2 * KnobVal(Knob::Main))) >> 12);

		CVOut1(cvMix);

		clockRate = ((4095 - KnobVal(Knob::Y)) << 3) + (((1 << 15) - amp2) * 24000 >> 15) + 50; // 1 in Q15 format is 1 << 15

		clock++;
		if (clock > clockRate)
		{
			clock = 0;
			PulseOut1(true);
			LedOn(4, true);
			pulseTimer1 = 200;
			clockPulse = true;
		};

		if (cvMix > rand && (clockPulse || PulseIn1RisingEdge()))
		{
			PulseOut2(true);
			pulseTimer2 = 200;
			LedOn(5, true);
		};

		if (Connected(Input::Pulse1))
		{
			// connected
			if (PulseIn1RisingEdge())
			{
				CVOut2(cvMix);
				pulseTimer1 = 200;
			};
		}
		else
		{
			// not connected
			if (clockPulse)
			{
				CVOut2(cvMix);
			}
		}

		// If a pulse is ongoing, keep counting until it ends
		if (pulseTimer1)
		{
			pulseTimer1--;
			if (pulseTimer1 == 0) // pulse ends
			{
				PulseOut1(false);
				LedOff(4);
			}
		};

		// If a pulse is ongoing, keep counting until it ends
		if (pulseTimer2)
		{
			pulseTimer2--;
			if (pulseTimer2 == 0) // pulse ends
			{
				PulseOut2(false);
				LedOff(5);
			}
		};

		// Delay line

		downsampleCounter--;
		if (downsampleCounter == 0)
		{
			downsampleCounter = 4;
			effectiveSampleRate = true;
		}
			// Filtered knob value

		if (effectiveSampleRate)
		{
			// Filtered knob value

			int16_t filteredKnobVal = (KnobVal(Knob::Main) * 819 + lastFilteredKnobVal * 31949) >> 15; // 0.025 and 0.975 in Q15 format
			lastFilteredKnobVal = filteredKnobVal;

		int16_t delaySamples = (static_cast<int>(filteredKnobVal) * bufferSize >> 11);

			audioBufferL[writeIndexL] = AudioIn1();
			audioBufferR[writeIndexR] = AudioIn2();

			readIndexL = (writeIndexL - delaySamples + bufferSize) % bufferSize;
			readIndexR = (writeIndexR - delaySamples + bufferSize) % bufferSize;

			AudioOut1(audioBufferL[readIndexL]);
			AudioOut2(audioBufferR[readIndexR]);

			writeIndexL = (writeIndexL + 1) % bufferSize;
			writeIndexR = (writeIndexR + 1) % bufferSize;
		}

		// PROCESS SAMPLE
	};

private:
	int pulseTimer1 = 200;
	int pulseTimer2;
	int clockRate;
	int clock = 0;
	int downsampleCounter = 4;

	static const int bufferSize = 48000;
	int16_t audioBufferL[bufferSize] = {0};
	int16_t audioBufferR[bufferSize] = {0};
	int writeIndexL = 0;
	int writeIndexR = 0;
	int readIndexL = 0;
	int readIndexR = 0;
	int16_t lastFilteredKnobVal = 0;
	// Fixed-point constant
	const uint16_t FIXED_POINT_SCALE = 32768; // 2^15 for Q15 format

	// random number generator
	int32_t LFrnd()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed >> 21;
	};

	// Function to scale value from 0-4095 to 0-FIXED_POINT_SCALE
	int16_t scaleValue(int16_t input_value)
	{
		return static_cast<int16_t>((input_value * FIXED_POINT_SCALE) / 4095);
	}
};

int main()
{
	Goldfish gf;
	gf.EnableNormalisationProbe();
	gf.Run();
}
