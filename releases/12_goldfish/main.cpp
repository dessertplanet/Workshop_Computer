#include "ComputerCard.h"
#include <cmath>

/// Goldfish

class Goldfish : public ComputerCard
{
public:
	virtual void ProcessSample()
	{
			
			
			float amp1 = KnobVal(Knob::X) / 4095.0f;
			float amp2 = KnobVal(Knob::Y) / 4095.0f;

			int cvMix = 0;
			int thing1 = 0;
			int thing2 = 0;
			bool clockPulse = false;
			bool randPulse = false;

			int32_t rand = LFrnd();

			if (rand > (3000-KnobVal(Knob::X))) randPulse = true;

			if(Connected(Input::CV1) && Connected(Input::CV2))
			{
				thing1 = int16_t(CVIn1()*amp1);
				thing2 = int16_t(CVIn2()*amp2);
			}
			else if(Connected(Input::CV1))
			{
				thing1 = int16_t(CVIn1()*amp1);
				thing2 = int16_t(2048 * (amp2 - 0.5));
			}
			else if(Connected(Input::CV2))
			{
				thing1 = int16_t(amp1 * rand);
				thing2 = int16_t(CVIn2()*amp2);
			} else {
				thing1 = int16_t(amp1 * rand);
				thing2 = int16_t(2048 *(amp2-0.5));
			};

			cvMix = (thing1 * (4095 - KnobVal(Knob::Main)) + thing2 * KnobVal(Knob::Main)) >> 12;

			

			CVOut1(cvMix);

			clockRate = (4095 - KnobVal(Knob::Y)) << 3;
			clockRate += (1 - amp2) * 24000 + 50;
			

			clock++;
			if (clock>clockRate)
			{
				clock = 0;
				PulseOut1(true);
				LedOn(1, true);
				pulseTimer1 = 200;
				clockPulse = true;
			};

			if(Connected(Input::Pulse1))
			{
				if(PulseIn1RisingEdge())
				{
					CVOut2(cvMix);
					LedOn(0, true);
					PulseOut1(true);
					pulseTimer1 = 200;
					if(randPulse)
					{
						PulseOut2(true);
						pulseTimer2 = 200;
					}
				};
			}
			else
			{
				if(clockPulse)
				{
					CVOut2(cvMix);
					if(randPulse)
					{
						PulseOut2(true);
						pulseTimer2 = 200;
					}
				}
			};


			// If a pulse is ongoing, keep counting until it ends
			if (pulseTimer1)
			{
				pulseTimer1--;
				if (pulseTimer1==0) // pulse ends
				{
					PulseOut1(false);
					LedOn(1, false);
					LedOn(0, false);
				}
			};

			// If a pulse is ongoing, keep counting until it ends
			if (pulseTimer2)
			{
				pulseTimer2--;
				if (pulseTimer2==0) // pulse ends
				{
					PulseOut2(false);
				}
			};

		//PROCESS SAMPLE
	};

	

private:

	int pulseTimer1 = 200;
	int pulseTimer2 = 0;
	int clockRate;
	int clock = 0;


	// random number generator
	int32_t LFrnd()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed >> 21;
	}
};


int main()
{
	Goldfish gf;
	gf.EnableNormalisationProbe();
	gf.Run();
}

  
