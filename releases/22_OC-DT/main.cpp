#include "ComputerCard.h"

/*
 * OC-DT Granular Delay
 * 
 * A sophisticated granular delay effect with the following features:
 * - 5.2-second circular buffer for audio capture
 * - Up to 4 simultaneous grains with Hann windowing
 * - Linear grain sizes from micro (64 samples) to huge (65536 samples)
 * - Bidirectional playback (-2x to +2x speed)
 * - Loop/glitch mode for captured segment looping
 * 
 * Controls:
 * - Main Knob: Grain playback speed/direction (-2x to +2x, center=pause)
 * - X Knob/CV1: Grain position spread (0=fixed delay, right=random spread)
 * - Y Knob/CV2: Grain size (Y knob as attenuverter when CV2 connected)
 * - Switch: Up=Dry, Middle=Wet, Down=Loop Mode
 * - Pulse 1 In: Triggers new grains
 * - Pulse 2 In: Forces switch down (loop mode)
 * 
 * Outputs:
 * - Pulse 1 Out: Square wave with Y knob rate control (24kHz to grain-rate, left=fast)
 * - Pulse 2 Out: Quick pulse whenever any grain ends
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
		grainEndTrigger_ = false;
		grainEndCounter_ = 0;
		grainTriggerCooldown_ = 0;
		
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
		
		// Calculate X control value from knob X or CV1 (with X knob as attenuverter)
		int32_t xControlValue;
		if (Connected(Input::CV1)) {
			// CV1 connected - X knob becomes attenuverter
			int32_t cv1Val = CVIn1(); // -2048 to +2047
			int32_t xKnobVal = KnobVal(X); // 0 to 4095
			xControlValue = applyAttenuverter(cv1Val, xKnobVal);
		} else {
			// CV1 disconnected - X knob direct control (no virtual detents)
			xControlValue = KnobVal(X);
		}
		
		// X knob functionality: Left half = delay time, Right half = spread
		if (xControlValue <= 2047) {
			// Left half (0-2047): Control delay time from shortest to longest
			// Map to delay distance: 0 -> 2400 samples (~50ms), 2047 -> 120000 samples (~2.5s)
			delayDistance_ = 2400 + ((xControlValue * (120000 - 2400)) / 2047);
			spreadAmount_ = 0; // No spread on left half
		} else {
			// Right half (2048-4095): Control spread with fixed delay time
			delayDistance_ = 24000; // Fixed delay at ~0.5 seconds
			// Map right half to spread: 2048 -> 0, 4095 -> 4095
			spreadAmount_ = ((xControlValue - 2048) * 4095) / 2047;
		}
		
		// Update stretch parameters from knobs
		updateStretchParameters();
		
		// Read 3-way switch position
		Switch switchPos = SwitchVal();
		
		// Override switch position if Pulse 2 is high (acts as switch down)
		if (PulseIn2()) {
			switchPos = Switch::Down;
		}
		
		// Handle switch modes using enums
		
		// Check for grain triggering once per sample (before switch logic)
		bool shouldTriggerGrain = PulseIn1RisingEdge() && (grainTriggerCooldown_ <= 0);
		
		// Update cooldown counter
		if (grainTriggerCooldown_ > 0) {
			grainTriggerCooldown_--;
		}
		
		// If we trigger a grain, set cooldown to prevent rapid retriggering
		if (shouldTriggerGrain) {
			grainTriggerCooldown_ = 48; // ~1ms cooldown at 48kHz
		}
		
		if (switchPos == Switch::Up) { // Switch Up - Totally Dry
			// Pass input directly to output
			AudioOut1(AudioIn1());
			AudioOut2(AudioIn2());
			
			// Still allow grain triggering in background for pulse output feedback
			if (shouldTriggerGrain)
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
			if (shouldTriggerGrain)
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
			if (shouldTriggerGrain)
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
	int32_t spreadAmount_ = 0; // Spread control: 0 = fixed delay position, 4095 = full random

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
	bool grainEndTrigger_;      // Trigger for grain end pulses
	int32_t grainEndCounter_;   // Counter for grain end pulse duration
	int32_t grainTriggerCooldown_; // Cooldown to prevent rapid retriggering
	

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
		// Main knob now controls playback speed/direction (-2.0x to +2.0x with pause at center)
		// Y knob now controls grain size (linear system)
		
		// Get main knob value and apply virtual detents for playback speed
		int32_t mainKnobVal = virtualDetentedKnob(KnobVal(Main));
		
		// Map main knob to grain playback speed: -2.0x to +2.0x with pause at center
		// 0 -> -2.0x (-8192), 2048 -> 0x (0 = paused), 4095 -> +2.0x (8192)
		if (mainKnobVal <= 2048) {
			// Left half: -2.0x to 0x (reverse full speed to paused)
			grainPlaybackSpeed_ = -8192 + ((mainKnobVal * 8192) >> 11); // -8192 to 0
		} else {
			// Right half: 0x to +2.0x (paused to forward full speed)
			int32_t rightKnob = mainKnobVal - 2048; // 0 to 2047
			grainPlaybackSpeed_ = (rightKnob * 8192) >> 11; // 0 to 8192
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
		
		// Calculate grain size based on Y knob (stretchRatio_) - smooth linear control
		// Linear mapping for predictable grain size range using Q12 integer arithmetic
		// Range: 64 samples (~1.3ms) to 65536 samples (~1.36s)
		
		// Normalize stretchRatio_ (1024-16384) to 0-4095 range (Q12)
		int32_t normalizedRatio = stretchRatio_ - 1024; // Now 0 to 15360
		normalizedRatio = (normalizedRatio * 4096) / 15360; // Scale to 0-4095 (Q12)
		
		// Clamp to valid range
		if (normalizedRatio < 0) normalizedRatio = 0;
		if (normalizedRatio > 4095) normalizedRatio = 4095;
		
		// Linear mapping from 64 to 65536 samples
		// grainSize = 64 + (normalizedRatio * (65536 - 64)) / 4095
		grainSize_ = 64 + ((normalizedRatio * 65472) / 4095); // 65472 = 65536 - 64
		
		// Safety clamp
		if (grainSize_ < 64) grainSize_ = 64;
		if (grainSize_ > 65536) grainSize_ = 65536;
		
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
				
				// Calculate base playback position: writeHead - delayDistance
				int32_t basePlaybackPos = writeHead_ - delayDistance_;
				if (basePlaybackPos < 0) basePlaybackPos += BUFF_LENGTH_SAMPLES;
				
				// Apply spread control
				int32_t playbackPos;
				if (spreadAmount_ == 0) {
					// Spread = 0: Use exact fixed delay position (normal time-stretching)
					playbackPos = basePlaybackPos;
				} else {
					// Spread > 0: Add controlled randomness around the base position
					// Generate random value from 0 to 4095
					int32_t randomValue = rnd12() & 0xFFF; // Ensure 12-bit range
					
					// Convert to -2048 to +2047 range for bidirectional offset
					int32_t randomOffset = randomValue - 2048;
					
					// Scale the offset by spread amount and limit to safe buffer portion
					// Limit maximum offset to 1/4 of buffer to prevent read-past-write issues
					int32_t maxOffset = BUFF_LENGTH_SAMPLES >> 2; // 1/4 buffer size
					randomOffset = (randomOffset * maxOffset) >> 11; // Scale by spread (>> 11 = / 2048)
					randomOffset = (randomOffset * spreadAmount_) >> 12; // Apply spread amount
					
					// Apply offset to base position
					playbackPos = basePlaybackPos + randomOffset;
				}
				
				// Ensure position is within buffer bounds and never past write head
				while (playbackPos >= BUFF_LENGTH_SAMPLES) playbackPos -= BUFF_LENGTH_SAMPLES;
				while (playbackPos < 0) playbackPos += BUFF_LENGTH_SAMPLES;
				
				// Safety check: ensure grain never reads past write head (allow small safety margin)
				int32_t safetyMargin = 1000; // ~20ms safety margin
				int32_t maxSafePos = writeHead_ - safetyMargin;
				if (maxSafePos < 0) maxSafePos += BUFF_LENGTH_SAMPLES;
				
				// If playback position is too close to write head, wrap it around safely
				int32_t distanceFromWrite = writeHead_ - playbackPos;
				if (distanceFromWrite < 0) distanceFromWrite += BUFF_LENGTH_SAMPLES;
				if (distanceFromWrite < safetyMargin) {
					playbackPos = maxSafePos;
				}
				
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
							// Trigger grain end pulse on Pulse 2 output
							grainEndTrigger_ = true;
							grainEndCounter_ = 0;
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
		// Pulse 1: Square wave with Y knob controlling rate from grain-based to 24kHz
		// Calculate the rate based on current grain size (slowest) up to 24kHz (fastest)
		
		// Get Y control value (same as used for grain size)
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
		
		// Map Y control to pulse rate (FLIPPED):
		// yControlValue = 0 -> 24kHz period (1 sample, fastest)
		// yControlValue = 4095 -> use grain size period (slowest)
		
		int32_t pulseHalfPeriod;
		if (yControlValue <= 2048) {
			// Left half: 24kHz to ~1kHz
			// At 0: 1 sample (24kHz at 48kHz)
			// At 2048: 24 samples (1kHz)
			pulseHalfPeriod = 1 + ((yControlValue * 23) / 2048); // 1 up to 24
		} else {
			// Right half: ~1kHz to grain-based rate
			// At 2048: 24 samples (1kHz)
			// At 4095: use grain size / 2 (slowest)
			int32_t rightKnob = yControlValue - 2048; // 0 to 2047
			int32_t grainHalfPeriod = grainSize_ >> 1;
			int32_t fastHalfPeriod = 24; // 1kHz
			
			// Ensure we don't have negative values in interpolation
			if (grainHalfPeriod < fastHalfPeriod) {
				pulseHalfPeriod = grainHalfPeriod;
			} else {
				// Linear interpolation from fast to slow
				pulseHalfPeriod = fastHalfPeriod + ((grainHalfPeriod - fastHalfPeriod) * rightKnob) / 2047;
			}
		}
		
		// Safety clamps to prevent lockup
		if (pulseHalfPeriod < 1) pulseHalfPeriod = 1;
		if (pulseHalfPeriod > 32768) pulseHalfPeriod = 32768; // Max reasonable period
		
		// Generate square wave
		pulseCounter_++;
		if (pulseCounter_ >= pulseHalfPeriod) {
			pulseState_ = !pulseState_;
			pulseCounter_ = 0;
		}
		
		// Output the pulse state
		PulseOut1(pulseState_);
		
		// Light up LED 4 when Pulse 1 is high
		LedOn(4, pulseState_);
		
		// Pulse 2: Quick pulse whenever any grain ends
		bool pulse2Output = false;
		
		if (grainEndTrigger_) {
			// Generate a longer trigger pulse (200 samples = ~4.2ms at 48kHz)
			if (grainEndCounter_ < 200) {
				pulse2Output = true;
				grainEndCounter_++;
			} else {
				// End of trigger pulse
				grainEndTrigger_ = false;
				grainEndCounter_ = 0;
			}
		}
		
		PulseOut2(pulse2Output);
		
		// Light up LED 5 when Pulse 2 is high
		LedOn(5, pulse2Output);
	}
};

int main()
{
	OC_DT card;
	card.EnableNormalisationProbe(); // Enable CV jack detection
	card.Run();
}
