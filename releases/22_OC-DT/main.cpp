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
		
		// Initialize time-stretching system
		stretchRatio_ = 4096; // 1.0x stretch ratio (Q12 fixed-point)
		grainPlaybackSpeed_ = 4096; // 1.0x grain speed (Q12 fixed-point)
		grainSize_ = 1024;
		grainOverlap_ = 256;   // 25% of grain size
		hopSize_ = 768;        // grainSize_ - grainOverlap_
		outputSampleCount_ = 0;
		maxActiveGrains_ = 2;
		neutralMode_ = true;   // Start in neutral mode
		
		// Initialize high pass filter state
		hpState_[0] = 0;
		hpState_[1] = 0;
		
		// Initialize all grains as inactive
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			grains_[i].active = false;
			grains_[i].readPos = 0;
			grains_[i].readFrac = 0;
			grains_[i].sampleCount = 0;
			grains_[i].startPos = 0;
			grains_[i].continuous = false;
		}
	}

	virtual void ProcessSample()
	{
		uint16_t stereoSample;
		int16_t left, right;
		
		// Apply high pass filter to audio inputs
		int16_t filteredL = highPassFilter(AudioIn1(), 0);
		int16_t filteredR = highPassFilter(AudioIn2(), 1);
		
		// Always record incoming audio (infinite tape mode)
		stereoSample = packStereo(filteredL, filteredR);
		buffer_[writeHead_] = stereoSample;
		writeHead_++;
		if (writeHead_ >= BUFF_LENGTH_SAMPLES)
		{
			writeHead_ = 0; // Wrap around for circular buffer
		}
		
		// Calculate delay distance from knob X
		int32_t knobXVal = KnobVal(X);
		// Map knob X to delay distance (1000 to 60000 samples)
		delayDistance_ = 1000 + ((knobXVal * 59000) >> 12); // Scale 0-4095 to 1000-60000
		
		// Update stretch parameters from knobs
		updateStretchParameters();
		
		// Pulse 1 triggers new grains
		if (PulseIn1RisingEdge())
		{
			triggerNewGrain();
		}
		
		// Generate granular output
		int16_t outL = generateStretchedSample(0); // Left channel
		int16_t outR = generateStretchedSample(1); // Right channel

		AudioOut1(outL);
		AudioOut2(outR);
		
		// Update grain system
		updateGrains();
	}

private:
	uint16_t buffer_[BUFF_LENGTH_SAMPLES]; 
	bool halftime_ = false;
	bool recPulse_ = false;
	bool playPulse_ = false;
	bool recording_ = false;     // Whether currently recording
	int32_t readHead_ = 0;		 // Read head for the buffer
	int32_t writeHead_ = 500;	 // Write head for the buffer
	int32_t loopLength_ = 50000; // Length of the loop in samples
	int32_t recordedLength_ = 0; // Length of recorded audio in samples
	int32_t delayDistance_ = 10000; // Distance between record and playback heads

	// Time-stretching grain system
	static const int MAX_GRAINS = 4;
	struct Grain {
		int32_t readPos;        // Current read position in buffer (integer part)
		int32_t readFrac;       // Q12 fractional part for interpolation
		int32_t sampleCount;    // Samples processed in this grain
		int32_t startPos;       // Where grain started in buffer
		bool active;            // Whether grain is currently playing
		bool continuous;        // Whether this is a continuous grain (neutral mode)
	};
	Grain grains_[MAX_GRAINS];
	
	// Time-stretching state
	int32_t stretchRatio_;      // Q12 fixed-point stretch ratio (4096 = 1.0x)
	int32_t grainPlaybackSpeed_; // Q12 fixed-point grain speed (4096 = 1.0x)
	int32_t grainSize_;         // Current grain size in samples
	int32_t grainOverlap_;      // Overlap size (25% of grain size)
	int32_t hopSize_;           // Distance between grain starts
	int32_t outputSampleCount_; // Total samples output
	int32_t maxActiveGrains_;   // Adaptive grain count
	bool neutralMode_;          // Whether in neutral delay mode (1.0x/1.0x)
	
	// Neutral mode detection threshold (Q12)
	static const int32_t NEUTRAL_THRESHOLD = 64; // ~0.015625 in Q12
	
	// Input high pass filter state (Q12)
	int32_t hpState_[2];        // State for L/R channels
	static const int32_t HP_COEFF = 4063; // ~0.993 in Q12 (40Hz @ 48kHz)

	// High pass filter function (Q12 fixed-point)
	int16_t __not_in_flash_func(highPassFilter)(int16_t input, int channel)
	{
		// Simple 1-pole high pass filter: y[n] = a * (y[n-1] + x[n] - x[n-1])
		// where a = HP_COEFF (~0.993 for 40Hz @ 48kHz)
		int32_t inputQ12 = input << 12; // Convert to Q12
		int32_t output = ((hpState_[channel] * HP_COEFF) >> 12) + inputQ12 - hpState_[channel];
		hpState_[channel] = inputQ12; // Store current input for next iteration
		return output >> 12; // Convert back to int16
	}

	// Interpolated sample reading with wraparound (Q12 fixed-point)
	int16_t __not_in_flash_func(getInterpolatedSample)(int32_t bufferPos, int32_t frac, int channel)
	{
		// Ensure buffer position is within bounds
		int32_t pos1 = bufferPos;
		if (pos1 >= BUFF_LENGTH_SAMPLES) pos1 -= BUFF_LENGTH_SAMPLES;
		if (pos1 < 0) pos1 += BUFF_LENGTH_SAMPLES;
		
		int32_t pos2 = pos1 + 1;
		if (pos2 >= BUFF_LENGTH_SAMPLES) pos2 = 0; // Wraparound
		
		int16_t sample1 = unpackStereo(buffer_[pos1], channel);
		int16_t sample2 = unpackStereo(buffer_[pos2], channel);
		
		// Linear interpolation in Q12: sample1 + (sample2 - sample1) * frac
		return sample1 + (((sample2 - sample1) * frac) >> 12);
	}

	// Fast cosine approximation for Hann windowing (Q12 fixed-point)
	int32_t __not_in_flash_func(fastCos)(int32_t x)
	{
		// x is 0-4095 representing 0-2π in Q12
		// Simple polynomial approximation: cos(x) ≈ 1 - x²/2 + x⁴/24
		// For better performance, use a simpler approximation or lookup table
		
		// Normalize x to -π to π range (Q12)
		x = x - 2048; // Now -2048 to 2047 representing -π to π
		
		// Simple parabolic approximation: cos(x) ≈ 1 - 2*x²/π²
		// where x is normalized to -1 to 1
		int32_t x_norm = (x * 2048) >> 11; // Normalize to Q12 -1 to 1
		int32_t x_sq = (x_norm * x_norm) >> 12; // x² in Q12
		
		return 4096 - ((x_sq * 8192) >> 12); // 1 - 2*x² in Q12
	}

	// Time-stretching helper functions
	void __not_in_flash_func(updateStretchParameters)()
	{
		// Get knob value and apply virtual detents
		int32_t knobVal = virtualDetentedKnob(KnobVal(Main));
		
		// Map main knob with virtual detents to stretch ratio
		// 0 -> 0.25x (1024), 2048 -> 1.0x (4096), 4095 -> 4.0x (16384)
		if (knobVal <= 2048) {
			// Left half: 0.25x to 1.0x
			stretchRatio_ = 1024 + ((knobVal * 3072) >> 11); // 3072 = (4096-1024)
		} else {
			// Right half: 1.0x to 4.0x  
			int32_t rightKnob = knobVal - 2048; // 0 to 2047
			stretchRatio_ = 4096 + ((rightKnob * 12288) >> 11); // 12288 = (16384-4096)
		}
		
		// Get Y knob value and apply virtual detents for grain playback speed
		int32_t yKnobVal = virtualDetentedKnob(KnobVal(Y));
		
		// Map Y knob to grain playback speed: -1 octave to +1 octave
		// 0 -> 0.5x (2048), 2048 -> 1.0x (4096), 4095 -> 2.0x (8192)
		if (yKnobVal <= 2048) {
			// Left half: 0.5x to 1.0x
			grainPlaybackSpeed_ = 2048 + ((yKnobVal * 2048) >> 11); // 2048 = (4096-2048)
		} else {
			// Right half: 1.0x to 2.0x
			int32_t rightKnob = yKnobVal - 2048; // 0 to 2047
			grainPlaybackSpeed_ = 4096 + ((rightKnob * 4096) >> 11); // 4096 = (8192-4096)
		}
		
		// Calculate grain size based on stretch ratio (256 to 2048 samples)
		if (stretchRatio_ < 2048) { // < 0.5x
			grainSize_ = 256;
			maxActiveGrains_ = 2;
		} else if (stretchRatio_ < 8192) { // 0.5x to 2.0x
			grainSize_ = 1024;
			maxActiveGrains_ = 3;
		} else { // > 2.0x
			grainSize_ = 2048;
			maxActiveGrains_ = 4;
		}
		
		grainOverlap_ = grainSize_ >> 2; // 25% overlap
		hopSize_ = grainSize_ - grainOverlap_;
	}
	int16_t virtualDetentedKnob(int16_t val)
	{
		// Create virtual detents at key positions
		if (val > 4079)
		{
			val = 4095; // Far right detent (4.0x)
		}
		else if (val < 16)
		{
			val = 0; // Far left detent (0.25x)
		}

		// Center detent at 1.0x speed
		if (cabs(val - 2048) < 16)
		{
			val = 2048;
		}

		return val;
	}
	
	void __not_in_flash_func(triggerNewGrain)()
	{
		// Detect neutral mode: both stretch and speed near 1.0x
		bool isNeutral = (cabs(stretchRatio_ - 4096) < NEUTRAL_THRESHOLD) && 
		                 (cabs(grainPlaybackSpeed_ - 4096) < NEUTRAL_THRESHOLD);
		
		if (isNeutral && neutralMode_)
		{
			// Neutral mode: stop any existing continuous grain and start new one
			for (int i = 0; i < MAX_GRAINS; i++)
			{
				if (grains_[i].active && grains_[i].continuous)
				{
					grains_[i].active = false; // Stop previous continuous grain
				}
			}
		}
		
		// Find an inactive grain slot
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (!grains_[i].active)
			{
				grains_[i].active = true;
				// Calculate playback position: writeHead - delayDistance
				int32_t playbackPos = writeHead_ - delayDistance_;
				if (playbackPos < 0) playbackPos += BUFF_LENGTH_SAMPLES; // Handle wraparound
				grains_[i].readPos = playbackPos;
				grains_[i].readFrac = 0; // Initialize fractional part
				grains_[i].startPos = grains_[i].readPos;
				grains_[i].sampleCount = 0;
				grains_[i].continuous = isNeutral; // Mark as continuous if in neutral mode
				break;
			}
		}
		
		// Update neutral mode state
		neutralMode_ = isNeutral;
	}
	
	int32_t __not_in_flash_func(calculateGrainWeight)(int grainIndex)
	{
		Grain& grain = grains_[grainIndex];
		
		// In neutral mode with continuous grains, use constant weight
		if (grain.continuous && neutralMode_)
		{
			return 4096; // Full weight (1.0 in Q12)
		}
		
		// Hann window: 0.5 * (1 - cos(2π * position / grainSize))
		// Position in grain: 0 to grainSize-1, normalized to 0-4095 (Q12)
		int32_t pos = (grain.sampleCount << 12) / grainSize_; // Q12 normalized position (0-4095)
		
		// Calculate cos(2π * pos) where pos is 0-4095 representing 0-1
		// Scale pos to 0-4095 representing 0-2π for fastCos
		int32_t cosArg = pos; // pos is already 0-4095
		int32_t cosVal = fastCos(cosArg); // Returns Q12 cos value
		
		// Hann window: 0.5 * (1 - cos) = 2048 - (cosVal >> 1)
		return 2048 - (cosVal >> 1); // Q12 format
	}
	
	int16_t __not_in_flash_func(generateStretchedSample)(int channel)
	{
		int32_t mixedSample = 0;
		int32_t totalWeight = 0;
		
		// Mix all active grains
		for (int i = 0; i < maxActiveGrains_; i++)
		{
			if (grains_[i].active)
			{
				// Get interpolated sample from buffer with wraparound
				int16_t grainSample = getInterpolatedSample(grains_[i].readPos, grains_[i].readFrac, channel);
				int32_t weight = calculateGrainWeight(i);
				
				mixedSample += (grainSample * weight) >> 12; // Q12 format
				totalWeight += weight;
			}
		}
		
		// Normalize output
		if (totalWeight > 0)
		{
			return (mixedSample * 4095) / totalWeight; // Q12 format
		}
		else
		{
			return 0;
		}
	}
	
	void __not_in_flash_func(updateGrains)()
	{
		// Update all active grains
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				grains_[i].sampleCount++;
				
				// Combined speed: stretch ratio * grain playback speed (Q12 * Q12 = Q24, shift to Q12)
				int32_t combinedSpeed = (stretchRatio_ * grainPlaybackSpeed_) >> 12;
				
				// Advance read position with fractional tracking
				grains_[i].readFrac += combinedSpeed;
				
				// Handle integer overflow from fractional part
				while (grains_[i].readFrac >= 4096) { // >= 1.0 in Q12
					grains_[i].readPos++;
					grains_[i].readFrac -= 4096;
					
					// Handle buffer wraparound
					if (grains_[i].readPos >= BUFF_LENGTH_SAMPLES) {
						grains_[i].readPos -= BUFF_LENGTH_SAMPLES;
					}
				}
				
				// Handle negative speeds (reverse playback)
				while (grains_[i].readFrac < 0) {
					grains_[i].readPos--;
					grains_[i].readFrac += 4096;
					
					// Handle buffer wraparound
					if (grains_[i].readPos < 0) {
						grains_[i].readPos += BUFF_LENGTH_SAMPLES;
					}
				}
				
				// Deactivate grain if it's finished (but not continuous grains)
				if (!grains_[i].continuous && grains_[i].sampleCount >= grainSize_)
				{
					grains_[i].active = false;
				}
			}
		}
	}

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
