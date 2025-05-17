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
	bool pulse = false;
	uint32_t tapTime = 0;
	uint32_t tapTimeLast = 0;
	int16_t counter = 0;

	Fifths()
	{
		// Constructor

		sampleCounter = 0;
	}

	virtual void ProcessSample()
	{

		////TIMING
		tap.Update(SwitchVal() == Switch::Down);

		sampleCounter += 1;
		sampleCounter %= 0xFFFFFFFF;

		if (resync)
		{
			resync = false;
			sampleCounter = 0;
			counter = 0;
		}

		pulse = sampleCounter % quarterNoteSamples == 0;

		if (pulse && !counter)
		{
			counter = 100;
		}

		if (counter)
		{
			counter--;
		}

		tapTimeLast = (pico_millis() - tapTime) % 0xFFFFFFFF;

		if (tapTimeLast > 2000 && tapping)
		{
			tapping = false;
		}

		/////VCA
		int16_t input;

		int16_t mainKnob = virtualDetentedKnob(KnobVal(Knob::Main));
		int16_t vcaKnob = virtualDetentedKnob(KnobVal(Knob::Y));
		int16_t vcaOut;
		int16_t vcaCV = virtualDetentedKnob(AudioIn2() * vcaKnob >> 12);


		if (Connected(Input::Audio1) && Connected(Input::Audio2))
		{
			input = AudioIn1();
			vcaOut = input * vcaCV >> 11;
		} else if (Connected(Input::Audio1))
		{
			input = AudioIn1();
			vcaOut = input * vcaKnob >> 12;
		} else if (Connected(Input::Audio2))
		{
			input = rnd() - 2048;
			vcaOut = input * vcaCV >> 11;
		} else 
		{
			input = rnd() - 2048;
			vcaOut = input * vcaKnob >> 11;
		}

		AudioOut1(mainKnob - 2048);
		AudioOut2(vcaOut);

		/////WEIRD QUANTIZER

		uint8_t major[7] = {0, 2, 4, 5, 7, 9, 11};

		if (PulseIn1RisingEdge())
		{
			int16_t quantisedNote = quantSample(vcaOut, major);
			CVOut1MIDINote(quantisedNote);
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
		{ // Ignore bounces and forgotten taps > 3 seconds
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
