#include "ComputerCard.h"

/*
 * OC-DT Granular Delay
 * 
 * A sophisticated granular delay effect with the following features:
 * - 5.2-second circular buffer for audio capture
 * - Up to 4 simultaneous grains with Hann windowing
 * - Variable grain sizes from micro (64 samples) to huge (65536 samples)
 * - Bidirectional playback (-2x to +2x speed)
 * - Loop/glitch mode for captured segment looping
 * 
 * Controls:
 * - Main Knob: Grain playback speed/direction
 * - X Knob/CV1: Delay distance (X knob as attenuverter when CV1 connected)
 * - Y Knob/CV2: Grain size (Y knob as attenuverter when CV2 connected)
 * - Switch: Up=Dry, Middle=Wet, Down=Loop Mode
 * - Pulse 1 In: Triggers new grains
 * - Pulse 2 In: Forces switch down (loop mode)
 * 
 * Outputs:
 * - Pulse 1 Out: Square wave at grain size intervals (perfect for feedback)
 * - Pulse 2 Out: Trigger pulses at grain intervals
 */

#define BUFF_LENGTH_SAMPLES 125000 // ~2.6 seconds at 48kHz

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
		maxActiveGrains_ = 4; // Fixed maximum - no adaptive limits
		loopMode_ = false; // Initialize loop mode
		
		// Initialize pulse output state
		pulseCounter_ = 0;
		pulseState_ = false;
		
		// Initialize all grains as inactive
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			grains_[i].active = false;
			grains_[i].readPos = 0;
			grains_[i].readFrac = 0;
			grains_[i].sampleCount = 0;
			grains_[i].startPos = 0;
			grains_[i].loopSize = 0;
			grains_[i].looping = false;
		}
	}

	virtual void ProcessSample()
	{
		uint16_t stereoSample;
		
		// Always record incoming audio (infinite tape mode)
		stereoSample = packStereo(AudioIn1(), AudioIn2());
		buffer_[writeHead_] = stereoSample;
		writeHead_++;
		if (writeHead_ >= BUFF_LENGTH_SAMPLES)
		{
			writeHead_ = 0; // Wrap around for circular buffer
		}
		
		// Calculate delay distance from knob X or CV1 (with X knob as attenuverter)
		int32_t xControlValue;
		if (Connected(Input::CV1)) {
			// CV1 connected - X knob becomes attenuverter
			int32_t cv1Val = CVIn1(); // -2048 to +2047
			int32_t xKnobVal = KnobVal(X); // 0 to 4095
			xControlValue = applyAttenuverter(cv1Val, xKnobVal);
		} else {
			// CV1 disconnected - X knob direct control
			xControlValue = KnobVal(X);
		}
		// Map control value to delay distance (1000 to 60000 samples)
		delayDistance_ = 1000 + ((xControlValue * 59000) >> 12); // Scale 0-4095 to 1000-60000
		
		// Update stretch parameters from knobs
		updateStretchParameters();
		
		// Read 3-way switch position
		Switch switchPos = SwitchVal();
		
		// Override switch position if Pulse 2 is high (acts as switch down)
		if (PulseIn2()) {
			switchPos = Switch::Down;
		}
		
		// Handle switch modes using enums
		if (switchPos == Switch::Up) { // Switch Up - Totally Dry
			// Pass input directly to output
			AudioOut1(AudioIn1());
			AudioOut2(AudioIn2());
			
			// Still allow grain triggering in background
			if (PulseIn1RisingEdge())
			{
				triggerNewGrain();
			}
		}
		else if (switchPos == Switch::Middle) { // Switch Middle - 100% Wet (current behavior)
			// Exit loop mode if we were in it
			if (loopMode_) {
				exitLoopMode();
			}
			
			// Normal granular processing
			if (PulseIn1RisingEdge())
			{
				triggerNewGrain();
			}
			
			// Generate granular output ONLY
			int16_t outL = generateStretchedSample(0); // Left channel
			int16_t outR = generateStretchedSample(1); // Right channel

			AudioOut1(outL);
			AudioOut2(outR);
		}
		else { // Switch Down - Loop/Glitch Mode
			// Enter loop mode if not already in it
			if (!loopMode_) {
				enterLoopMode();
			}
			
			// Allow grain triggering even in loop mode
			if (PulseIn1RisingEdge())
			{
				triggerNewGrain();
			}
			
			// Generate looped granular output
			int16_t outL = generateStretchedSample(0); // Left channel
			int16_t outR = generateStretchedSample(1); // Right channel

			AudioOut1(outL);
			AudioOut2(outR);
		}
		
		// Update pulse outputs
		updatePulseOutputs();
		
		// Update grain system
		updateGrains();
	}

private:
	uint16_t buffer_[BUFF_LENGTH_SAMPLES]; 
	int32_t writeHead_ = 0;	 // Write head for the buffer (start at 0, not 500)
	int32_t delayDistance_ = 10000; // Distance between record and playback heads

	// Time-stretching grain system
	static const int MAX_GRAINS = 4;
	struct Grain {
		int32_t readPos;        // Current read position in buffer (integer part)
		int32_t readFrac;       // Q12 fractional part for interpolation
		int32_t sampleCount;    // Samples processed in this grain
		int32_t startPos;       // Where grain started in buffer
		int32_t loopSize;       // Size of loop when in loop mode
		bool active;            // Whether grain is currently playing
		bool looping;           // Whether this grain is in loop mode
	};
	Grain grains_[MAX_GRAINS];
	
	// Time-stretching state
	int32_t stretchRatio_;      // Q12 fixed-point stretch ratio (4096 = 1.0x)
	int32_t grainPlaybackSpeed_; // Q12 fixed-point grain speed (4096 = 1.0x)
	int32_t grainSize_;         // Current grain size in samples
	int32_t maxActiveGrains_;   // Fixed maximum grain count (4)
	bool loopMode_;             // Whether we're in loop/glitch mode (switch down)
	
	// Pulse output state
	int32_t pulseCounter_;      // Counter for pulse timing
	bool pulseState_;           // Current state of pulse output
	

	// Interpolated sample reading with wraparound (Q12 fixed-point)
	int16_t __not_in_flash_func(getInterpolatedSample)(int32_t bufferPos, int32_t frac, int channel)
	{
		// Ensure buffer position is within bounds with proper modulo for negative values
		int32_t pos1 = bufferPos;
		while (pos1 >= BUFF_LENGTH_SAMPLES) pos1 -= BUFF_LENGTH_SAMPLES;
		while (pos1 < 0) pos1 += BUFF_LENGTH_SAMPLES;
		
		int32_t pos2 = pos1 + 1;
		if (pos2 >= BUFF_LENGTH_SAMPLES) pos2 = 0; // Wraparound
		
		int16_t sample1 = unpackStereo(buffer_[pos1], channel);
		int16_t sample2 = unpackStereo(buffer_[pos2], channel);
		
		// Clamp fractional part to valid range
		if (frac < 0) frac = 0;
		if (frac >= 4096) frac = 4095;
		
		// Linear interpolation in Q12: sample1 + (sample2 - sample1) * frac
		int32_t diff = sample2 - sample1;
		int32_t interpolated = sample1 + ((diff * frac) >> 12);
		
		// Clamp result to prevent overflow
		if (interpolated > 2047) interpolated = 2047;
		if (interpolated < -2048) interpolated = -2048;
		
		return (int16_t)interpolated;
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
		// SWAPPED CONTROLS:
		// Main knob now controls playback speed/direction (-2.0x to +2.0x)
		// Y knob now controls grain size (6-tier system)
		
		// Get main knob value and apply virtual detents for playback speed
		int32_t mainKnobVal = virtualDetentedKnob(KnobVal(Main));
		
		// Map main knob to grain playback speed: -1 octave to +1 octave (0.5x to 2.0x)
		// 0 -> -0.5x (-2048), 2048 -> 1.0x (4096), 4095 -> +2.0x (8192)
		if (mainKnobVal <= 2048) {
			// Left half: -0.5x to 1.0x (half speed reverse to normal forward)
			grainPlaybackSpeed_ = -2048 + ((mainKnobVal * 6144) >> 11); // -2048 to 4096
		} else {
			// Right half: 1.0x to +2.0x (normal to double speed forward)
			int32_t rightKnob = mainKnobVal - 2048; // 0 to 2047
			grainPlaybackSpeed_ = 4096 + ((rightKnob * 4096) >> 11); // 4096 to 8192
		}
		
		// Calculate Y control value from knob Y or CV2 (with Y knob as attenuverter)
		int32_t yControlValue;
		if (Connected(Input::CV2)) {
			// CV2 connected - Y knob becomes attenuverter
			int32_t cv2Val = CVIn2(); // -2048 to +2047
			int32_t yKnobVal = KnobVal(Y); // 0 to 4095
			yControlValue = applyAttenuverter(cv2Val, yKnobVal);
		} else {
			// CV2 disconnected - Y knob direct control
			yControlValue = KnobVal(Y);
		}
		
		// Apply virtual detents to the control value
		yControlValue = virtualDetentedKnob(yControlValue);
		
		// Map Y control value to stretch ratio for grain size calculation
		// We'll use this to determine grain size in the 6-tier system
		// 0 -> 0.25x (1024), 2048 -> 1.0x (4096), 4095 -> 4.0x (16384)
		if (yControlValue <= 2048) {
			// Left half: 0.25x to 1.0x
			stretchRatio_ = 1024 + ((yControlValue * 3072) >> 11); // 3072 = (4096-1024)
		} else {
			// Right half: 1.0x to 4.0x  
			int32_t rightKnob = yControlValue - 2048; // 0 to 2047
			stretchRatio_ = 4096 + ((rightKnob * 12288) >> 11); // 12288 = (16384-4096)
		}
		
		// Calculate grain size based on Y knob (stretchRatio_) - 6-tier system
		// Expanded range from micro-granular to long ambient textures
		// All grain sizes now support up to 4 simultaneous grains
		if (stretchRatio_ < 1638) { // 0.25x-0.4x - Micro grains
			grainSize_ = 64;    // ~1.3ms - extreme granular textures
		} else if (stretchRatio_ < 2867) { // 0.4x-0.7x - Tiny grains
			grainSize_ = 256;   // ~5.3ms - short bursts
		} else if (stretchRatio_ < 5325) { // 0.7x-1.3x - Small grains
			grainSize_ = 1024;  // ~21ms - percussive sounds
		} else if (stretchRatio_ < 8192) { // 1.3x-2.0x - Medium grains
			grainSize_ = 4096;  // ~85ms - musical phrases
		} else if (stretchRatio_ < 12288) { // 2.0x-3.0x - Large grains
			grainSize_ = 16384; // ~341ms - long textures
		} else { // 3.0x-4.0x - Huge grains
			grainSize_ = 65536; // ~1.36s - ambient stretches
		}
		
	}
	int16_t virtualDetentedKnob(int16_t val)
	{
		// Create virtual detents at key positions
		if (val > 4090)
		{
			val = 4095; // Far right detent
		}
		else if (val < 5)
		{
			val = 0; // Far left detent
		}

		// Center detent - important for pause/freeze at 0x speed
		if (cabs(val - 2048) < 12)
		{
			val = 2048; // Slightly larger detent zone for center (pause)
		}

		return val;
	}
	
	// Attenuverter function: applies knob as attenuverter to CV input
	int32_t __not_in_flash_func(applyAttenuverter)(int32_t cvValue, int32_t knobValue)
	{
		// cvValue: -2048 to +2047 (CV input)
		// knobValue: 0 to 4095 (knob position)
		// Returns: 0 to 4095 (scaled for use as control value)
		
		// Convert knob to attenuverter scale: 0-4095 -> -1.0 to +1.0 (Q12)
		// knob = 0 -> -4096 (full inversion)
		// knob = 2048 -> 0 (no effect, but we want unity gain)
		// knob = 4095 -> +4096 (full positive)
		
		// Map knob 0-4095 to scale factor -2.0 to +2.0 (Q12)
		int32_t scaleFactor = ((knobValue - 2048) * 4) + 4096; // -4096 to +12288, centered at 4096
		
		// Apply scaling to CV: cv * scale / 4096
		int32_t scaledCV = (cvValue * scaleFactor) >> 12;
		
		// Convert from CV range (-2048 to +2047) to knob range (0 to 4095)
		// Add offset and scale to 0-4095 range
		int32_t result = scaledCV + 2048; // Now -2048+2048 to +2047+2048 = 0 to 4095
		
		// Clamp to valid knob range
		if (result < 0) result = 0;
		if (result > 4095) result = 4095;
		
		return result;
	}
	
	void __not_in_flash_func(triggerNewGrain)()
	{
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
				break;
			}
		}
	}
	
	int32_t __not_in_flash_func(calculateGrainWeight)(int grainIndex)
	{
		Grain& grain = grains_[grainIndex];
		
		// Safety check to prevent division by zero
		if (grainSize_ <= 0)
		{
			return 4096; // Full weight if grain size is invalid
		}
		
		// Hann window: 0.5 * (1 - cos(2π * position / grainSize))
		// Position in grain: 0 to grainSize-1, normalized to 0-4095 (Q12)
		int32_t pos = (grain.sampleCount << 12) / grainSize_; // Q12 normalized position (0-4095)
		
		// Clamp position to valid range
		if (pos > 4095) pos = 4095;
		if (pos < 0) pos = 0;
		
		// Calculate cos(2π * pos) where pos is 0-4095 representing 0-1
		// Scale pos to 0-4095 representing 0-2π for fastCos
		int32_t cosArg = pos; // pos is already 0-4095
		int32_t cosVal = fastCos(cosArg); // Returns Q12 cos value
		
		// Hann window: 0.5 * (1 - cos) = 2048 - (cosVal >> 1)
		int32_t weight = 2048 - (cosVal >> 1); // Q12 format
		
		// Ensure weight is never negative or zero
		if (weight < 1) weight = 1;
		
		return weight;
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
		
		// Handle output normalization
		if (totalWeight > 0)
		{
			// Always normalize by total weight for consistent granular processing
			int32_t result = (mixedSample << 12) / totalWeight;
			
			// Clamp to prevent overflow
			if (result > 2047) result = 2047;
			if (result < -2048) result = -2048;
			
			return (int16_t)result;
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
				// Get grain playback speed
				int32_t grainSpeed = grainPlaybackSpeed_;
				
				// Handle looping grains differently
				if (grains_[i].looping)
				{
					// In loop mode, grains loop within their captured segment
					if (grainSpeed != 0)
					{
						// Advance read position with fractional tracking
						grains_[i].readFrac += grainSpeed;
						
						// Handle integer overflow from fractional part
						while (grains_[i].readFrac >= 4096) { // >= 1.0 in Q12
							grains_[i].readPos++;
							grains_[i].readFrac -= 4096;
							
							// Loop within the captured segment
							if (grains_[i].readPos >= grains_[i].startPos + grains_[i].loopSize) {
								grains_[i].readPos = grains_[i].startPos;
							}
						}
						
						// Handle negative speeds (reverse looping)
						while (grains_[i].readFrac < 0) {
							grains_[i].readPos--;
							grains_[i].readFrac += 4096;
							
							// Loop within the captured segment (reverse)
							if (grains_[i].readPos < grains_[i].startPos) {
								grains_[i].readPos = grains_[i].startPos + grains_[i].loopSize - 1;
							}
						}
					}
					// Looping grains never deactivate automatically
				}
				else
				{
					// Normal grain behavior
					// Only update grain if speed is non-zero (fixes freeze bug)
					if (grainSpeed != 0)
					{
						grains_[i].sampleCount++;
						
						// Advance read position with fractional tracking
						grains_[i].readFrac += grainSpeed;
						
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
						
						// Deactivate grain if it's finished (only when not frozen)
						if (grains_[i].sampleCount >= grainSize_)
						{
							grains_[i].active = false;
						}
					}
					// When speed is 0, grain stays frozen at current position indefinitely
				}
			}
		}
	}
	
	// Loop mode functions
	void __not_in_flash_func(enterLoopMode)()
	{
		loopMode_ = true;
		
		// Convert all currently active grains to looping mode
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				grains_[i].looping = true;
				grains_[i].loopSize = grainSize_; // Capture current grain size as loop size
				// Reset sample count to prevent immediate deactivation
				grains_[i].sampleCount = 0;
			}
		}
	}
	
	void __not_in_flash_func(exitLoopMode)()
	{
		loopMode_ = false;
		
		// Convert all looping grains back to normal mode
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active && grains_[i].looping)
			{
				grains_[i].looping = false;
				grains_[i].loopSize = 0;
				// Reset sample count for normal grain lifecycle
				grains_[i].sampleCount = 0;
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

	// Update pulse outputs
	void __not_in_flash_func(updatePulseOutputs)()
	{
		// Pulse 1: Square wave at grain size intervals for perfect grain timing
		pulseCounter_++;
		
		// Calculate half grain size for 50% duty cycle square wave
		int32_t halfGrainSize = grainSize_ >> 1;
		
		// Toggle pulse state every half grain size for square wave
		if (pulseCounter_ >= halfGrainSize)
		{
			pulseState_ = !pulseState_;
			pulseCounter_ = 0;
		}
		
		// Output the pulse state
		PulseOut1(pulseState_);
		
		// Pulse 2: Could be used for other timing functions in the future
		// For now, keep it simple - maybe output a trigger at full grain intervals
		static int32_t pulse2Counter = 0;
		static bool pulse2Triggered = false;
		
		pulse2Counter++;
		
		// Generate a short trigger pulse at the start of each grain period
		if (pulse2Counter >= grainSize_)
		{
			pulse2Triggered = true;
			pulse2Counter = 0;
		}
		
		// Make the trigger pulse short (1/16th of grain size, minimum 10 samples)
		int32_t triggerLength = (grainSize_ >> 4);
		if (triggerLength < 10) triggerLength = 10;
		
		if (pulse2Triggered)
		{
			if (pulse2Counter < triggerLength)
			{
				PulseOut2(true);
			}
			else
			{
				PulseOut2(false);
				pulse2Triggered = false;
			}
		}
		else
		{
			PulseOut2(false);
		}
	}
};

int main()
{
	OC_DT card;
	card.EnableNormalisationProbe(); // Enable CV jack detection
	card.Run();
}
