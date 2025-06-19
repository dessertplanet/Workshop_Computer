#include "ComputerCard.h"
#include "click.h"

void tempTap();
void longHold();

class StickCtrl : public ComputerCard
{
public:
	// Timing variables for tap tempo are public so that the callback functions can access them
	bool tapping = false;
	bool switchHold = false;
	bool resync = false;
	bool pulse = false;
	uint32_t quarterNoteSamples = 12000;
	uint32_t tapTime = 0;

	StickCtrl()
	{
		// Constructor
		sampleCounter_ = 0;
	}

	virtual void ProcessSample() override
	{
		// audio loop runs at 24kHz
		if (halftime_)
		{
			halftime_ = !halftime_;
			
		}

		//counters and timing run at 48kHz
		pulse = sampleCounter_ % tempo_samples_ == 0;

			if (pulse)
			{
				L_counter_ = pulseWidth_;
				R_counter_ = pulseWidth_;
			}

			// iterate sample counter
			sampleCounter_++;
			if ((sampleCounter_ + quarterNoteSamples) == 0xFFFFFFFF)
			{
				sampleCounter_ = 0;
			}

			// decrement counters
			if (L_counter_)
			{
				L_counter_--;
			}

			if (R_counter_)
			{
				R_counter_--;
			}

			// render pulse outputs

			PulseOut1(L_counter_);
			PulseOut2(R_counter_);

			// display outputs on "screen"
			LedOn(4, L_counter_);
			LedOn(5, R_counter_);
	}

	void record()
	{
	}

private:
	int16_t pulseWidth_ = 200;		// in samples
	int16_t tempo_samples_ = 12000; // in samples
	int16_t L_counter_ = 0;
	int16_t R_counter_ = 0;
	uint32_t sampleCounter_;
	Click tap_ = Click(tempTap, longHold);
	bool halftime_ = true;

	constexpr static int bufSize_ = 48000; // 2 seconds at 24kHz
	int16_t sampleL[bufSize_];
	int16_t sampleR[bufSize_];

		// RNG! Different values for each card but the same on each boot
		uint32_t
		__not_in_flash_func(rnd12)()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed ^= UniqueCardID() >> 20;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed >> 20;
	}

	int16_t virtualDetentedKnob(int16_t val)
	{
		if (val > 4079)
		{
			val = 4095;
		}
		else if (val < 16)
		{
			val = 0;
		}

		if (cabs(val - 2048) < 16)
		{
			val = 2048;
		}

		return val;
	}

	int32_t cabs(int32_t a)
	{
		return (a > 0) ? a : -a;
	}

	void clip(int16_t &val)
	{
		if (val > 2047)
		{
			val = 2047;
		}
		else if (val < -2048)
		{
			val = -2048;
		}
	}
};

// define this here so that it can be used in the click library
// There is probably a better way to do this but I don't know it
StickCtrl card;

int main()
{
	card.EnableNormalisationProbe();
	card.Run();
}

// Callbacks for click library to call based on Switch state
void tempTap()
{
	uint32_t currentTime = pico_millis();

	if (!card.tapping)
	{
		card.tapTime = currentTime;
		card.tapping = true;
	}
	else
	{
		uint32_t sinceLast = (currentTime - card.tapTime) & 0xFFFFFFFF; // Handle overflow

		if (sinceLast > 20 && sinceLast < 3000)
		{								// Ignore bounces and forgotten taps > 3 seconds
			card.tapTime = currentTime; // Record time ready for next tap
			card.quarterNoteSamples = sinceLast * 48;
			card.resync = true;
			card.pulse = true;
		}
	}
}

void longHold()
{
	card.switchHold = true;
}
