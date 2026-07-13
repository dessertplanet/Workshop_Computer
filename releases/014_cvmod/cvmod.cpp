
#include "ComputerCard.h"

class CVMod : public ComputerCard
{
	// Buffer for recording
	constexpr static unsigned maxLoopSize = 96000;
	int16_t buffer[maxLoopSize];

	// Lookup table for powers of two
	uint32_t pow2_128[128];

	
	// Recording loop
	uint32_t loopSize, loopIndex;

	uint32_t playbackPhase[4];
	uint32_t recordPhase;

	int32_t timeKnob;
	
	int sampleCount;

	int pulseFlashCounter;

	bool resetTrigger, nextFunctionTrigger;

	int function;

	uint32_t lastExtraRecordingIndex;
	
	// Return integer part of 2^(in/4096) (approximate)
	// 7-bit lookup table (128 entries), 5-bit (32-step) linear interpolation between steps
	uint32_t Pow2(uint32_t in)
	{
		uint32_t oct = in >> 12; // floor(in/4096)

		// Table index and value of lookup
		uint32_t ind1 = (in & 0xFE0) >> 5;  // 0xFE0 = 111111100000
		uint32_t val1 = pow2_128[ind1];

		// Next lookup
		uint32_t ind2 = (ind1 + 1) & 0x7F; //
		uint32_t val2 = pow2_128[ind2];
		if (ind2 == 0) val2 <<= 1; // interpolating between end of one octave and the start of another

		// Linearly interpolate with 32 steps
		uint32_t subsample = in & 0x1F;
		uint32_t val = val1*(32-subsample) + val2*subsample;

		val >>= 31 - oct;

		return val;
	}

	int32_t PhaseAdvance(int32_t i, int32_t speedKnob, uint32_t loopIncrement)
	{
		// loopIncrement = 2^32 / loopSize is at most 22.5 bits
		// loopIncrement = 2^32 / 96000 at minimum
		// Pow2 is at most 10+(1900*3*2)/4096 = 12.78 bits
		// loopIncrement * 2^(speedKnob*(2*i-3)*k)
		// loopIncrement * (2^(10 + speedKnob*(2*i-3)*2)) >> 10
		// ((loopIncrement>>5) * (2^(10 + speedKnob*(2*i-3)*2)) >> 5

		return ((loopIncrement>>5) * Pow2(10*4096 + speedKnob*2*(2*i-3))) >> 5;
	}

	// No more than 5 functions!
	enum FuncType {Ramp=0, Saw=1, Tri=2, Sin=3, Steps=4, nFuncTypes};

	
	uint8_t funcLeds[5] = {1, 3, 2, 6, 4};
	uint32_t PhaseFunc(FuncType func, uint32_t phase)
	{
		switch(func)
		{
		case Ramp:
			return phase;

		case Saw:
			return -phase;

		case Tri:
			if (phase < 0x80000000) return phase;
			else return 0xFFFFFFFF - phase;

		case Sin:
			if (phase < 0x40000000) return (phase>>15)*(phase>>15);
			else if (phase > 0xC0000000) return ((0xFFFFFFFF-phase)>>15)*((0xFFFFFFFF-phase)>>15);
			else if (phase < 0x80000000) return 0x80000000-((0x80000000-phase)>>15)*((0x80000000-phase)>>15);
			else return 0x80000000-((phase-0x80000000)>>15)*((phase-0x80000000)>>15);

		case Steps:
			return phase&0xF0000000;
			
		default:
			return 0;
		}
	}
	
	// High 24 bits of position are sample index
	// Low 8 bits are sub-sample (8-bit linear interpolation)
	int32_t ReadBuffer(uint32_t position)
	{
		int32_t r = position & 0xFF;
		position >>= 8;
		uint32_t position2 = position+1;
		if (position2 >= loopSize)
		{
			position2 -= loopSize;
		}

		return (buffer[position]*(256 - r) + buffer[position2]*r) >> 8;
	}
	
	
public:
	CVMod()
	{

		for (unsigned i=0; i<maxLoopSize; i++)
		{
			buffer[i] = 0;
		}
		sampleCount = 0;

		float f = 67108864; // 2^26
		for (int i=0; i<128; i++)
		{
			pow2_128[i] = f;
			f *= 1.005429901112803f; // 2^(1/128)
		}

		pulseFlashCounter = 0;
		timeKnob = 0;
		
		recordPhase = 0;
		for (int i=0; i<4; i++)
		{
			playbackPhase[i] = 0;
		}

		resetTrigger = false;
		nextFunctionTrigger = false;
		function = 0;
		lastExtraRecordingIndex = 0;
	}
	
	virtual void ProcessSample()
	{
		if ((SwitchChanged() && SwitchVal()==Down) || PulseIn1RisingEdge())
			resetTrigger = true;

		if (SwitchVal() == Up && SwitchChanged())
		{
			nextFunctionTrigger = true;
		}
		
		// Only process samples at 12kHz - discard three out of four samples
		sampleCount++;
		if (sampleCount == 4)
		{
			sampleCount = 0;
		}
		else
		{
			return;
		}

		/// From this point on, called at 12kHz


		////////////////////////////////////////
		// Collect knob & switch values
		
		int32_t phaseKnob = KnobVal(Y) + CVIn2();
		if (phaseKnob < 0) phaseKnob = 0;
		if (phaseKnob > 4095) phaseKnob = 4095;


		int32_t timeKnobRaw = KnobVal(X) + CVIn1();
		
		// Loop time knob X has heavy hysteresis to avoid glitching
		if (timeKnobRaw > timeKnob + 15 || timeKnobRaw < timeKnob - 15)
		{
			timeKnob = timeKnobRaw;
		}
		if (timeKnob < 0) timeKnob = 0;
		if (timeKnob > 4095) timeKnob = 4095;

		
		int32_t speedKnob = KnobVal(Main) - 2048;
		
		if (speedKnob > 100) speedKnob -= 100;
		else if (speedKnob < -100) speedKnob += 100;
		else speedKnob = 0;

		speedKnob += AudioIn2();
				
		if (speedKnob > 1900) speedKnob = 1900;
		if (speedKnob < -1900) speedKnob = -1900;

		LedOn(1, speedKnob == 0);
		

		if (nextFunctionTrigger)
		{
			function++;
			if (function >= nFuncTypes) function = 0;
			nextFunctionTrigger = 0;
		}
		
		
		// Maximum loop size should be (less than) 96000 = 8s
		// Minimum loop size should be ~ 62.5ms
		loopSize = Pow2(39125 + timeKnob*7);
		if (loopSize >= maxLoopSize) loopSize = maxLoopSize-1;
		
		loopIndex++;
		if (loopIndex >= loopSize)
		{
			loopIndex = 0;

			pulseFlashCounter = 200;
		}

		uint32_t loopIncrement = 0xFFFFFFFF/loopSize;
		recordPhase = loopIndex * loopIncrement;

		
		////////////////////////////////////////
		// Record values to buffer
		
		int32_t recordedValue = 0;
		if (Connected(Audio1))
		{
			recordedValue = AudioIn1();
		}
		else
		{
			// convert from 32-bit unsigned to signed 12-bit 
			recordedValue = (recordPhase>>20) - 2048; 
		}
		
		buffer[loopIndex] = recordedValue;

		//	if (loopIndex+loopSize < maxLoopSize)
		//	buffer[loopIndex+loopSize] = recordedValue;

		uint32_t extraRecordingIndex = loopIndex + loopSize;
		if (extraRecordingIndex < maxLoopSize)
		{
			if (extraRecordingIndex > lastExtraRecordingIndex)
			{
				uint32_t maxvals = 1500;
				uint32_t loopEnd = extraRecordingIndex;

				if (lastExtraRecordingIndex + maxvals < loopEnd)
					loopEnd = lastExtraRecordingIndex + maxvals;
				
			    for (uint32_t i = lastExtraRecordingIndex; i<loopEnd; i++)
				{
					buffer[i] = recordedValue;
				}
			}
			else
			{
			   	buffer[extraRecordingIndex] = recordedValue;
			}

			lastExtraRecordingIndex = extraRecordingIndex;
		}

		////////////////////////////////////////
		// Now, the movement of the output playback heads

		for (int i=0; i<4; i++)
		{
			playbackPhase[i] += PhaseAdvance(i, speedKnob, loopIncrement);
		}

		if (resetTrigger)
		{
			for (int i=0; i<4; i++)
			{
				playbackPhase[i] = recordPhase;
			}
			resetTrigger = false;
		}
		

		// Get final position of output playback heads and convert into buffer positions
		uint32_t pos[4];
		for (int i=0; i<4; i++)
		{
			uint32_t finalPhase = PhaseFunc((FuncType) function, playbackPhase[i] - phaseKnob*i*262144);
			pos[i] = (uint64_t(finalPhase) * loopSize) >> 24;
		}
		
		//uint32_t readPhase = recordPhase - phaseKnob*524288;
		//uint32_t readBufferPos = (uint64_t(readPhase) * loopSize) >> 24;


		AudioOut1(ReadBuffer(pos[0]));
		AudioOut2(ReadBuffer(pos[1]));
		CVOut1(ReadBuffer(pos[2]));
		CVOut2(ReadBuffer(pos[3]));

//		AudioOut1(recordedValue);
//		AudioOut2(ReadBuffer(readBufferPos));

/*		
		AudioOut1(KnobVal(Main)-2048);
		AudioOut2(KnobVal(X)-2048);
		CVOut1(KnobVal(Y)-2048);
		CVOut2(SwitchVal()==Down ? 2047:0);
*/


		if (pulseFlashCounter > 0)
		{
			LedOn(5);
			pulseFlashCounter--;
		}
		else
		{
			LedOff(5);
		}

		LedOn(0, funcLeds[function]&1);
		LedOn(2, funcLeds[function]&2);
		LedOn(4, funcLeds[function]&4);
	}
};


int main()
{
	set_sys_clock_khz(192000, true);
	
	CVMod cvm;
	cvm.EnableNormalisationProbe();
	cvm.Run();
}
