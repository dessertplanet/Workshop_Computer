#include "ComputerCard.h"
#include "click.h"
#include "quantiser.h"

void tempTap();
void longHold();

class Fifths : public ComputerCard
{
public:
	// tonics on the circle of fifths starting from Gb all the way to F#,
	// always choosing an octave with a note as close as possible to the key center (0)
	constexpr static int8_t circle_of_fifths[13] = {-6, 1, -4, 3, -2, 5, 0, -5, 2, -3, 4, -1, 6};

	// half-way between a minor third (3/12) and a major third (4/12) in 1V per Octave terms
	constexpr static int ambiguous_third = 597; //this is approximately 3.5 semitones in fixed point
	int8_t all_keys[13][12];

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

		// set up scale matrix

		for (int i = 0; i < 13; i++)
		{
			populateScale(circle_of_fifths[i]);

			for (int j = 0; j < 12; j++)
			{
				all_keys[i][j] = scale[j];
			}
		}

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
			vcaCV = virtualDetentedKnob((AudioIn2() * vcaKnob >> 12) + 2048) - 2048;
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

		int8_t key_index = KnobVal(Knob::Main) * 13 >> 12;

		if (PulseIn1RisingEdge())
		{
			int16_t quantisedNote = quantSample(vcaOut, all_keys[key_index]);
			int16_t quantizedAmbigThird = quantSample(vcaOut + ambiguous_third, all_keys[key_index]);
			CVOut1MIDINote(quantisedNote);
			CVOut2MIDINote(quantizedAmbigThird);
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

	int8_t *scale = new int8_t[12];

	void populateScale(int8_t tonic)
	{
		int8_t whole = 2;
		int8_t half = 1;
		int8_t temp[12] = {
			tonic,
			tonic,
			tonic + whole,
			tonic + whole,
			tonic + whole + whole,
			tonic + whole + whole,
			tonic + whole + whole + half,
			tonic + whole + whole + half + whole,
			tonic + whole + whole + half + whole,
			tonic + whole + whole + half + whole + whole,
			tonic + whole + whole + half + whole + whole,
			tonic + whole + whole + half + whole + whole + half
		};
		for (int i = 0; i < 12; i++)
		{
			scale[i] = temp[i];
		}
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
