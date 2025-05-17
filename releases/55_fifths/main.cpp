#include "ComputerCard.h"
#include "click.h"
#include "quantiser.h"

void tempTap();
void longHold();

class Fifths : public ComputerCard
{
public:
	uint32_t sampleCounter;

	uint32_t quarterNoteMs;

	uint32_t quarterNoteSamples = 12000;

	Click tap = Click(tempTap, longHold);

	bool tapping = false;
	bool switchHold = false;
	bool resync = false;

	uint32_t tapTime = 0;
	uint32_t tapTimeLast = 0;

	Fifths()
	{
		// Constructor

		sampleCounter = 0;
	}

	virtual void ProcessSample()
	{
		tap.Update(SwitchVal() == Switch::Down);

		if (resync)
		{
			sampleCounter = 0;
		}

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
	int32_t rnd()
	{
		static uint32_t lcg_seed = UniqueCardID() & 0xFFFFFFFF; // 32-bit LCG seed from unique cardID
		lcg_seed ^= (uint32_t)time_us_64();						// XOR with time to add some randomness
		lcg_seed ^= KnobVal(Knob::Main) << 20;					// XOR with main knob value to add some randomness
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed;
	}
};

// define this here so that it can be used in the click library
// There is probably a better way to do this but I don't know it
Fifths card;

int main()
{
	card.EnableNormalisationProbe();
	card.Run();
}

// Callbacks for click library to call based on Switch state
void tempTap()
{
	// first tap
	if (!card.tapping)
	{
		card.tapTime = pico_millis();
		card.tapping = true;
	}
	else // second tap
	{
		unsigned long sinceLast = pico_millis() - card.tapTime;
		if (sinceLast > 50 && sinceLast < 3000)
		{ // ignore bounces and forgotten taps > 2 seconds

			card.tapTime = pico_millis(); // record time ready for next tap
			card.quarterNoteMs = sinceLast;
			card.resync = true;
		}
	}
}

void longHold()
{
	card.switchHold = true;
}
