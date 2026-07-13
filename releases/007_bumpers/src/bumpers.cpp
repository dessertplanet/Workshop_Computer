
#include "ComputerCard.h"
#include <cmath>

int32_t __not_in_flash_func(rnd12)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 20;
}

uint32_t __not_in_flash_func(rnd32)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed;
}


template <uint32_t bufSize, typename storage=int16_t>
class DelayLine
{
public:
	unsigned writeInd;
	DelayLine()
	{
		writeInd=0;
		for (unsigned i=0; i<bufSize; i++)
		{
			delaybuf[i] = 0;
		}
	} 

	// delay is in 128ths of a sample
	storage ReadInterp(int32_t delay)
	{

		int32_t r = delay & 0x7F;
		
		int32_t readInd1 = (writeInd + (bufSize<<1) - (delay>>7) - 1)%bufSize;
		int32_t fromBuffer1 =  delaybuf[readInd1];
		int readInd2 = (writeInd + (bufSize<<1) - (delay>>7) - 2)%bufSize;
		int32_t fromBuffer2 =  delaybuf[readInd2];
		return (fromBuffer2*r + fromBuffer1*(128-r))>>7;
	}
	// delay is in samples
	storage ReadRaw(int32_t delay)
	{
		int32_t readInd1 = (writeInd + (bufSize<<1) - delay - 1)%bufSize;
		return  delaybuf[readInd1];
	}
	void Write(int16_t value)
	{
		delaybuf[writeInd] = value;

		writeInd++;
		if (writeInd>=bufSize) writeInd=0;
	}
private:
	storage delaybuf[bufSize];
};


template <typename T=int32_t, unsigned shift=16>
class SVFLowPass
{

protected:
	T ic1eq, ic2eq, v1rem, v2rem;
	int32_t a1, a2, a3;
public:
	SVFLowPass(float f0, float Q)
	{
		float g = tanf(M_PI * f0/48000.0f);
		float k = 1.0f/Q;
		float fa1 = (1l<<shift)/(1.0f + g*(g+k));
		a1 = fa1;
		a2 = fa1*g;
		a3 = fa1*g*g;

		ic1eq = 0;
		ic2eq = 0;
		v1rem = 0;
		v2rem = 0;
	}

	void SetFreq(float g, float k)
	{
		float fa1 = (1l<<shift)/(1.0f + g*(g+k));
		a1 = fa1;
		a2 = fa1*g;
		a3 = fa1*g*g;
	}
	
	int32_t operator()(int32_t x)
	{
		T v3 = x - ic2eq;
		T v1 = (a1*ic1eq + a2*v3 + v1rem);
		v1rem = v1 & 0x7FFF;
		v1>>=(shift-1);
		T v2 = (a2*ic1eq + a3*v3+ v2rem);
		v2rem = v2 & 0x7FFF;
		v2>>=(shift-1);
		ic1eq = v1 - ic1eq;
		ic2eq = v2 + ic2eq;
		return ic2eq;
	}
};



int pentatonic[5]={0,2,5,7,9};
class Bumpers : public ComputerCard
{
	DelayLine<100000> delay;
	SVFLowPass<> lpf;
	int32_t pulseDurationTimer[2];
	int32_t pulseSpacingTimer[2];
	int32_t pulseSpacing[2];
	int32_t cvCurrent[2], cvDest[2];
	int32_t dlLength;

	unsigned delayTime[2][10];
	// Lookup table for powers of two
	uint32_t pow2_128[128];
	
	constexpr static int32_t minSpacing = 1000;

	
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


public:
	Bumpers() : lpf(50, 1)
	{
		// Set up powers-of-two table
		float f = 67108864; // 2^26
		for (int i=0; i<128; i++)
		{
			pow2_128[i] = f;
			f *= 1.005429901112803f; // 2^(1/128)
		}

		// Zero out variables
		for (unsigned i=0; i<2; i++)
		{
			pulseSpacing[i] = 0;
			pulseDurationTimer[i]=0;
			pulseSpacingTimer[i]=0;
			cvCurrent[i] = 0;
			cvDest[i] = 0;
		}
		dlLength = 0;

		int val = 20000, lastval = 0;
		delayTime[0][0] = val;
		delayTime[1][0] = val/2;
		for (int i=1; i<10; i++)
		{
			val = (val*4)/5;
			delayTime[0][i] = delayTime[0][i-1]+val;
			delayTime[1][i] = (delayTime[0][i]+delayTime[0][i-1])/2;
		}
	}

	
	virtual void ProcessSample()
	{
		Switch s = SwitchVal();
		bool manualPulse = SwitchChanged() && s == Down;
		for (unsigned i=0; i<2; i++)
		{
			int32_t decrement = (KnobVal(Knob(Knob::X + i)) + CVIn(i));
			pulseSpacingTimer[i]-= Pow2(16384+8192 + (decrement*7));
			
			if (pulseSpacingTimer[i] <= 0 || manualPulse || PulseInRisingEdge(i))
			{
				pulseSpacing[i] = (pulseSpacing[i]*(64+(KnobVal(Main)>>6)))>>7;
				if ((pulseSpacing[i] < minSpacing && s != Middle) || manualPulse || PulseInRisingEdge(i))
				{
					pulseSpacing[i] = (2048)<<12;

					
					// Pitch signal is on CV out 1 (on channel 1), and is on audio out 2 (on channel 2) if no delay
					if (i==0)
					{
						int r = rnd12()>>8;
						int oct = r/5;
						int note = r%5;
						CVOutMIDINote(i, 50+(oct*12)+pentatonic[note]);
					}
					else if (Disconnected(Audio1)) AudioOut(i, rnd12()-2048);

					// Channel 1 does random point A to point B CV out
					if (i==0)
					{
						cvCurrent[i]=cvDest[i];
						cvDest[i]=rnd12()-2048;
					} // channel 2 does regular 0-6V CV out
					else
					{
						cvCurrent[i] = 0;
						cvDest[i] = 2047;
					}
				}
			
				pulseSpacingTimer[i] = pulseSpacing[i];
				pulseDurationTimer[i] =1+(pulseSpacing[i]>>17);
				//	pulseDurationTimer[i] = (pulseDurationTimer[i]*pulseDurationTimer[i])>>8;
				int32_t cvStep = cvCurrent[i] + (((cvDest[i]-cvCurrent[i])*(2048-(pulseSpacing[i]>>12)))>>11);
				if (cvStep<-2048) cvStep=-2048;
				if (cvStep>2047) cvStep=2047;

				// CV signal is on CV out 2 (on channel 2), and is on audio out 1 (on channel 1) if no delay
				if (i==1) CVOut(i, cvStep);
				else if (Disconnected(Audio1)) AudioOut(i, cvStep);
			}
		
			if (pulseDurationTimer[i]>0 && pulseSpacing[i] >= minSpacing)
			{
				LedOn(i);
				PulseOut(i, true);
				pulseDurationTimer[i]--;
			}
			else
			{
				PulseOut(i, false);
				LedOff(i);
			}
		}

		if (Connected(Audio1))
		{

			int32_t dl2;
			if (Connected(Audio2))
			{
				int32_t uf=(AudioIn2()+2048)>>1;
				//		dlLength = (dlLength*7 + uf)>>3;;//(255*dlLength + 16*()>>8;
				//	dlLength = (dlLength*255 + uf*64)>>8;//(255*dlLength + 16*()>>8;
				dl2=lpf(uf*16);
			}
			else
			{
				dl2=32768;//dlLength = 2047;
			}
//			dl2 = dlLength>>6;
			
			delay.Write(AudioIn1());
			int32_t mul = 1024;
			int32_t outputl = AudioIn1()*mul, outputr = AudioIn1()*mul;
			for (int i=0; i<10; i++)
			{
				mul>>=1;
				outputl += delay.ReadInterp((delayTime[0][i]*dl2)>>8)*mul;
				outputr += delay.ReadInterp((delayTime[1][i]*dl2)>>8)*mul;
			}
			AudioOut1(outputl>>11);
			AudioOut2(outputr>>11);
		}
	}
};


int main()
{
	set_sys_clock_khz(192000, true);
	
	Bumpers bumpers;
	bumpers.EnableNormalisationProbe();
	bumpers.Run();
}
