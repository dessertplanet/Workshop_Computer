#include "ComputerCard.h"

#define BUFF_LENGTH_SAMPLES 125000 // 5.2 seconds at 24kHz

class OC_DT : public ComputerCard
{
public:
	OC_DT()
	{
		// Constructor
		for (int i = 0; i < BUFF_LENGTH_SAMPLES; i++)
		{
			buffer_[i] = 0;
		}
	}

	virtual void ProcessSample()
	{
		uint16_t stereoSample;
		int16_t left, right;
		

		if (PulseIn2RisingEdge())
		{
			
			stereoSample = packStereo(AudioIn1(), AudioIn2());
			buffer_[writeHead_] = stereoSample;

			writeHead_++;
			if (writeHead_ >= loopLength_)
			{
				writeHead_ = 0; // Wrap around
			}
		}
		if (PulseIn1RisingEdge())
		{
			int16_t outL = unpackStereo(buffer_[readHead_],0); // Left channel
			int16_t outR = unpackStereo(buffer_[readHead_],1); // Right channel

			AudioOut1(outL);	 // Left channel
			AudioOut2(outR); // Right channel

			readHead_++;
			if (readHead_ >= loopLength_)
			{
				readHead_ = 0; // Wrap around
			}
		}
	}

private:
	uint16_t buffer_[BUFF_LENGTH_SAMPLES]; 
	bool halftime_ = false;
	bool recPulse_ = false;
	bool playPulse_ = false;
	int32_t readHead_ = 0;		 // Read head for the buffer
	int32_t writeHead_ = 500;	 // Write head for the buffer
	int32_t loopLength_ = 50000; // Length of the loop in samples

	// RNG! Different values for each card but the same on each boot
	uint32_t __not_in_flash_func(rnd12)()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed ^= UniqueCardID() >> 20;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed >> 20;
	}

	uint16_t packStereo(int16_t left, int16_t right)
	{
		//convert two 12 bit signed values to signed 8 bit values and pack into a single 16 bit word
		int8_t left8 = static_cast<int8_t>(left >> 4);
		int8_t right8 = static_cast<int8_t>(right >> 4);
		return (static_cast<uint8_t>(left8) << 8) | static_cast<uint8_t>(right8);
	}

	int16_t unpackStereo(uint16_t stereo, int8_t index)
	{
		//unpack a 16 bit word into two signed 8 bit values and convert to signed 12 bit values (returning one of them based on index)
		if (index == 0)
		{
			int8_t left8 = (stereo >> 8) & 0xFF;
			return static_cast<int16_t>(left8) << 4;
		}
		else
		{
			int8_t right8 = stereo & 0xFF;
			return static_cast<int16_t>(right8) << 4;
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

	int8_t sign(int16_t val)
	{
		if (val > 0)
		{
			return 1;
		}
		else if (val < 0)
		{
			return -1;
		}
		else
		{
			return 0;
		}
	}
};

int main()
{
	OC_DT card;
	card.Run();
}