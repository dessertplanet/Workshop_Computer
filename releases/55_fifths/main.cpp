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

	int8_t all_keys[13][12];

	uint32_t sampleCounter;
	uint32_t quarterNoteMs;
	uint32_t quarterNoteSamples = 12000;
	Click tap = Click(tempTap, longHold);
	bool tapping = false;
	bool switchHold = false;
	bool resync = false;
	bool pulse = false;
	bool shift_on = false;
	uint32_t tapTime = 0;
	uint32_t tapTimeLast = 0;
	int16_t counter = 0;

	int16_t vcaOut;
	int16_t vcaCV;

	int16_t quantisedNote;
	int32_t quantizedAmbigThird;

	bool looping;

	int16_t buffer[12];

	int8_t looplength = 12;
	int8_t loopindex = 0;

	int16_t lastX;
	int16_t lastY;

	int16_t mainKnob;
	int16_t vcaKnob;
	int16_t xKnob;

	int16_t pulseDuration = 100;

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

		looping = SwitchVal() == Switch::Middle;

		for (int i = 0; i < 12; i++)
		{
			buffer[i] = (rnd12() - 2048) / 4;
		}
	}

	virtual void ProcessSample()
	{

		// Switch behaviour

		Switch sw = SwitchVal();

		looping = !(sw == Switch::Up);

		if (PulseIn2())
		{
			looping = !looping;
		}

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

		if (Connected(Input::Pulse1))
		{
			pulse = PulseIn1RisingEdge();
		}
		else
		{
			pulse = sampleCounter % quarterNoteSamples == 0;
		}

		if (pulse && !counter)
		{
			counter = pulseDuration;
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

		if(switchHold && (tapTimeLast > 1000) && sw != Switch::Down)
		{
			switchHold = false;
			shift_on = false;
		}

		/////VCA
		int16_t input = AudioIn1() + 25; // DC offset for non-callibrated input. Works on Dune's Workshop System *shrug*

		mainKnob = virtualDetentedKnob(KnobVal(Knob::Main));
		vcaKnob = virtualDetentedKnob(KnobVal(Knob::Y));
		xKnob = virtualDetentedKnob(KnobVal(Knob::X));

		if (Connected(Input::Audio2))
		{
			vcaCV = virtualDetentedKnob((AudioIn2() * vcaKnob >> 12) + 2048) - 2048;
		}
		else
		{
			vcaCV = virtualDetentedKnob(KnobVal(Knob::Y));
		}

		if (Connected(Input::Audio1) && Connected(Input::Audio2))
		{
			vcaOut = input * vcaCV >> 11;
		}
		else if (Connected(Input::Audio1))
		{
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

		clip(vcaOut);

		AudioOut1(mainKnob - 2048);
		AudioOut2(vcaOut);

		/////WEIRD QUANTIZER

		if (pulse)
		{
			int8_t key_index;
			if (Connected(Input::CV2))
			{
				key_index = (virtualDetentedKnob(KnobVal(Knob::Main)) + CVIn2()) * 13 >> 12;
				if (key_index < 0)
				{
					key_index += 13;
				}
				else if (key_index > 12)
				{
					key_index -= 13;
				}
			}
			else
			{
				key_index = KnobVal(Knob::Main) * 13 >> 12;
			}

			if (Connected(Input::CV1))
			{
				looplength = (virtualDetentedKnob(KnobVal(Knob::X)) + CVIn1()) * 12 >> 12;
				if (looplength < 0)
				{
					looplength += 12;
				}
				else if (looplength > 11)
				{
					looplength -= 12;
				}
			}
			else
			{
				looplength = virtualDetentedKnob(KnobVal(Knob::X)) * 12 >> 12; // 0 - 11
			}

			looplength = looplength + 1; // 1 - 12

			int16_t quant_input;

			if (looping)
			{
				quant_input = buffer[loopindex];
			}
			else
			{
				quant_input = vcaOut;
				buffer[loopindex] = quant_input;
			}

			clip(quant_input);

			quantisedNote = quantSample(quant_input, all_keys[key_index]);
			quantizedAmbigThird = calculateAmbigThird(quantisedNote, key_index);
			CVOut1MIDINote(quantisedNote);
			CVOut2MIDINote(quantizedAmbigThird);
			loopindex = loopindex + 1;

			if (loopindex >= looplength)
			{
				loopindex = 0;
			}
		}

		if (switchHold && ((xKnob - lastX) > 0 || shift_on))
		{
			shift_on = true;
			
			pulseDuration = xKnob * (12000) >> 12;
		}

		PulseOut1(counter > 0);

		LedOn(4, counter > 0);

		lastX = xKnob;
		lastY = vcaKnob;
	}

private:
	// a slightly more complex random number generator than usual to ensure reseting Computer produces different results
	uint32_t __not_in_flash_func(rnd12)()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed ^= UniqueCardID() >> 20;
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
			tonic + whole + whole + half + whole + whole + whole};
		for (int i = 0; i < 12; i++)
		{
			scale[i] = temp[i];
		}
	}

	int8_t calculateAmbigThird(int8_t input, int8_t key_index)
	{
		int8_t octave = input / 12;

		for (int i = 0; i < 12; i++)
		{
			if (input + 3 == 12 * octave + all_keys[key_index][i] || input + 3 == 12 * (octave + 1) + all_keys[key_index][i]) // check for minor third in this or next octave
			{
				return input + 3;
			}
		};

		return input + 4; // otherwise return major third
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
