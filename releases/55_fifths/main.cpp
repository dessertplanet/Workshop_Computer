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
		int16_t vcaCV;

		if (Connected(Input::Audio2))
		{
			vcaCV = virtualDetentedKnob(AudioIn2() * vcaKnob >> 12) - 2048;
		}
		else
		{
			vcaCV = virtualDetentedKnob(vcaKnob) - 2048;
		}

		if (Connected(Input::Audio1) && Connected(Input::Audio2))
		{
			input = AudioIn1();
			vcaOut = input * vcaCV >> 11;
		}
		else if (Connected(Input::Audio1))
		{
			input = AudioIn1();
			vcaOut = input * vcaKnob >> 12;
		}
		else if (Connected(Input::Audio2))
		{
			input = rnd12() - 2048;
			vcaOut = input * vcaCV >> 11;
		}
		else
		{
			input = rnd12() - 2048;
			vcaOut = input * vcaKnob >> 12;
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
	uint32_t __not_in_flash_func(rnd12)()
	{
		static uint32_t lcg_seed = 1;
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
