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
 * - Main Knob: Grain playback speed/direction (-2x to +2x, center=pause) OR pitch attenuverter when CV1 connected
 * - X Knob: Grain position spread (0=fixed delay, right=random spread)
 * - CV1: Pitch control (-5V to +5V = -2x to +2x speed) with Main knob as attenuverter
 * - Y Knob/CV2: Grain size (Y knob as attenuverter when CV2 connected)
 * - Switch: Up=Freeze Buffer, Middle=Wet, Down=Loop Mode
 * - Pulse 1 In: Triggers new grains
 * - Pulse 2 In: Forces switch down (loop mode)
 */

#define BUFF_LENGTH_SAMPLES 125000 // ~2.6 seconds at 48kHz

class OC_DT : public ComputerCard
{
private:
	// 256-entry Hann window lookup table (Q12 format)
	static constexpr int HANN_TABLE_SIZE = 256;
	static constexpr int32_t hannWindowTable_[HANN_TABLE_SIZE] = {
		// 256-entry Hann window, Q12 format, 0.5 * (1 - cos(2 * pi * n / 255)) * 4096
		0, 78, 313, 704, 1251, 1953, 2820, 3850, 5043, 6397, 7911, 9584, 11414, 13399, 15539, 17831,
		20274, 22865, 25603, 28485, 31509, 34672, 37972, 41406, 44971, 48664, 52482, 56422, 60480, 64653, 68937, 73328,
		77822, 82415, 87102, 91878, 96739, 101681, 106698, 111786, 116940, 122154, 127423, 132741, 138102, 143501, 148931, 154387,
		159862, 165351, 170847, 176344, 181836, 187316, 192778, 198216, 203623, 208993, 214319, 219595, 224814, 229970, 235056, 240067,
		244995, 249836, 254582, 259228, 263768, 268196, 272505, 276690, 280744, 284662, 288437, 292064, 295537, 298851, 302000, 304980,
		307785, 310411, 312853, 315107, 317168, 319033, 320697, 322157, 323409, 324450, 325277, 325887, 326277, 326445, 326389, 326108,
		325599, 324862, 323895, 322698, 321270, 319611, 317721, 315600, 313249, 310668, 307858, 304820, 301555, 298065, 294351, 290415,
		286259, 281885, 277295, 272492, 267478, 262256, 256828, 251198, 245368, 239342, 233123, 226715, 220121, 213346, 206393, 199267,
		191972, 184513, 176895, 169123, 161202, 153137, 144934, 136598, 128135, 119551, 110852, 102044, 93133, 84126, 75029, 65848,
		56601, 47294, 37934, 28529, 19086, 9622,  0, -9622, -19086, -28529, -37934, -47294, -56601, -65848, -75029, -84126,
		-93133, -102044, -110852, -119551, -128135, -136598, -144934, -153137, -161202, -169123, -176895, -184513, -191972, -199267, -206393, -213346,
		-220121, -226715, -233123, -239342, -245368, -251198, -256828, -262256, -267478, -272492, -277295, -281885, -286259, -290415, -294351, -298065,
		-301555, -304820, -307858, -310668, -313249, -315600, -317721, -319611, -321270, -322698, -323895, -324862, -325599, -326108, -326389, -326445,
		-326277, -325887, -325277, -324450, -323409, -322157, -320697, -319033, -317168, -315107, -312853, -310411, -307785, -304980, -302000, -298851,
		-295537, -292064, -288437, -284662, -280744, -276690, -272505, -268196, -263768, -259228, -254582, -249836, -244995, -240067, -235056, -229970
	};
	// Constants for magic numbers (Bug Fix #9)
	static const int32_t GRAIN_TRIGGER_COOLDOWN_SAMPLES = 48; // ~1ms cooldown at 48kHz
	static const int32_t SAFETY_MARGIN_SAMPLES = 1000; // ~20ms safety margin
	static const int32_t GRAIN_END_PULSE_DURATION = 200; // ~4.2ms pulse duration
	static const int32_t MAX_PULSE_HALF_PERIOD = 32768; // Maximum reasonable pulse period
	static const int32_t PULSE_COUNTER_OVERFLOW_LIMIT = 65536; // Prevent counter overflow
	static const int32_t VIRTUAL_DETENT_THRESHOLD = 12; // Center detent threshold
	static const int32_t VIRTUAL_DETENT_EDGE_THRESHOLD = 5; // Edge detent threshold
	
	// Crash protection constants
	static const int32_t MAX_FRACTIONAL_ITERATIONS = 4; // Sufficient for ±2x speed with safety margin
	static const int32_t MAX_SAFE_GRAIN_SPEED = 8192; // Limit grain speed to 2x max speed

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
		// Read 3-way switch position first to determine recording behavior
		Switch switchPos = SwitchVal();
		
		// Override switch position if Pulse 2 is high (acts as switch down)
		if (PulseIn2()) {
			switchPos = Switch::Down;
		}
		
		// Always advance virtual write head for consistent delay timing
		virtualWriteHead_++;
		if (virtualWriteHead_ >= BUFF_LENGTH_SAMPLES)
		{
			virtualWriteHead_ = 0; // Wrap around for circular buffer
		}
		
		// Record incoming audio to buffer ONLY when switch is not up (not in freeze mode)
		if (switchPos != Switch::Up) {
			uint16_t stereoSample = packStereo(AudioIn1(), AudioIn2());
			buffer_[writeHead_] = stereoSample;
			writeHead_++;
			if (writeHead_ >= BUFF_LENGTH_SAMPLES)
			{
				writeHead_ = 0; // Wrap around for circular buffer
			}
			// Keep virtual write head in sync with real write head when recording
			virtualWriteHead_ = writeHead_;
		}
		// When switch is up, writeHead_ stays frozen, but virtualWriteHead_ continues advancing
		
		// X knob always controls delay time/spread directly (no CV1 interaction)
		int32_t xControlValue = KnobVal(X);
		
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
		
		// Check for grain triggering once per sample (before switch logic)
		bool shouldTriggerGrain = PulseIn1RisingEdge() && (grainTriggerCooldown_ <= 0);
		
		// Update cooldown counter
		if (grainTriggerCooldown_ > 0) {
			grainTriggerCooldown_--;
		}
		
		// If we trigger a grain, set cooldown to prevent rapid retriggering
		if (shouldTriggerGrain) {
			grainTriggerCooldown_ = GRAIN_TRIGGER_COOLDOWN_SAMPLES;
		}
		
		if (switchPos == Switch::Up) { // Switch Up - Freeze Buffer (Loop entire buffer)
			// Buffer recording is already frozen (writeHead_ not advancing)
			// Still allow grain triggering for pulse output feedback
			if (shouldTriggerGrain)
			{
				triggerNewGrain();
			}
			
			// Generate granular output from the frozen buffer
			int16_t outL = generateStretchedSample(0); // Left channel
			int16_t outR = generateStretchedSample(1); // Right channel

			AudioOut1(outL);
			AudioOut2(outR);
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
		
		// Update grain system
		updateGrains();
	}

private:
	uint16_t buffer_[BUFF_LENGTH_SAMPLES]; 
	int32_t writeHead_ = 0;	 // Write head for the buffer (start at 0, not 500)
	int32_t virtualWriteHead_ = 0; // Virtual write head that continues advancing in freeze mode
	int32_t delayDistance_ = 10000; // Distance between record and playback heads
	int32_t spreadAmount_ = 0; // Spread control: 0 = fixed delay position, 4095 = full random

	// Time-stretching grain system
	static const int MAX_GRAINS = 4;
	static const int32_t GRAIN_FREEZE_TIMEOUT = 48000 * 5; // 5 seconds at 48kHz
	struct Grain {
		int32_t readPos;        // Current read position in buffer (integer part)
		int32_t readFrac;       // Q12 fractional part for interpolation
		int32_t sampleCount;    // Samples processed in this grain
		int32_t startPos;       // Where grain started in buffer
		int32_t loopSize;       // Size of loop when in loop mode
		int32_t freezeCounter;  // Counter for frozen grain timeout
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
	
	int32_t grainTriggerCooldown_; // Cooldown to prevent rapid retriggering
	

	// Interpolated sample reading with wraparound (Q12 fixed-point)
	int16_t __not_in_flash_func(getInterpolatedSample)(int32_t bufferPos, int32_t frac, int channel)
	{
		// Ensure buffer position is within bounds with proper modulo for negative values
		int32_t pos1 = bufferPos;
		while (pos1 >= BUFF_LENGTH_SAMPLES) pos1 -= BUFF_LENGTH_SAMPLES;
		while (pos1 < 0) pos1 += BUFF_LENGTH_SAMPLES;
		
		// Calculate pos2 with proper wraparound handling
		int32_t pos2 = pos1 + 1;
		if (pos2 >= BUFF_LENGTH_SAMPLES) pos2 = 0; // Forward wraparound
		
		// Additional safety check: ensure both positions are valid
		if (pos1 < 0 || pos1 >= BUFF_LENGTH_SAMPLES) pos1 = 0;
		if (pos2 < 0 || pos2 >= BUFF_LENGTH_SAMPLES) pos2 = 0;
		
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
		
		// CV1 pitch control with Main knob as attenuverter
		if (Connected(Input::CV1)) {
			// CV1 connected - CV1 controls pitch, Main knob becomes attenuverter
			int32_t cv1Val = CVIn1(); // -2048 to +2047 (±5V)
			int32_t mainKnobVal = virtualDetentedKnob(KnobVal(Main)); // Apply detents
			grainPlaybackSpeed_ = applyPitchAttenuverter(cv1Val, mainKnobVal);
		} else {
			// CV1 disconnected - Main knob direct pitch control (original behavior)
			int32_t mainKnobVal = virtualDetentedKnob(KnobVal(Main));
			
			// Map main knob to grain playback speed: -2x to +2x with pause at center
			// 0 -> -2x (-8192), 2048 -> 0x (0 = paused), 4095 -> +2x (8192)
			if (mainKnobVal <= 2048) {
				// Left half: -2x to 0x (full reverse to paused)
				// -2x in Q12 = -8192
				grainPlaybackSpeed_ = -8192 + ((mainKnobVal * 8192) >> 11); // -8192 to 0
			} else {
				// Right half: 0x to +2x (paused to full forward)
				// +2x in Q12 = 8192
				int32_t rightKnob = mainKnobVal - 2048; // 0 to 2047
				grainPlaybackSpeed_ = (rightKnob * 8192) >> 11; // 0 to 8192
			}
		}
		
		// CRASH PROTECTION: Limit grain speed to prevent overflow and runaway loops
		if (grainPlaybackSpeed_ > MAX_SAFE_GRAIN_SPEED) {
			grainPlaybackSpeed_ = MAX_SAFE_GRAIN_SPEED;
		}
		if (grainPlaybackSpeed_ < -MAX_SAFE_GRAIN_SPEED) {
			grainPlaybackSpeed_ = -MAX_SAFE_GRAIN_SPEED;
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
		if (cabs(val - 2048) < VIRTUAL_DETENT_THRESHOLD)
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
	
	// Pitch attenuverter function: applies Main knob as ±1x attenuverter to CV1 pitch input
	int32_t __not_in_flash_func(applyPitchAttenuverter)(int32_t cv1Value, int32_t mainKnobValue)
	{
		// cv1Value: -2048 to +2047 (±5V CV input)
		// mainKnobValue: 0 to 4095 (Main knob with detents)
		// Returns: -8192 to +8192 (±2x speed in Q12)
		
		// Map Main knob to ±1x gain factor
		// 0 -> -4096 (-1x), 2048 -> +4096 (+1x), 4095 -> +4096 (+1x)
		int32_t gainFactor;
		if (mainKnobValue <= 2048) {
			// Left half: -1x to +1x gain
			gainFactor = -4096 + ((mainKnobValue * 8192) >> 11); // -4096 to +4096
		} else {
			// Right half: +1x gain (no amplification beyond 1x)
			gainFactor = 4096;
		}
		
		// Apply gain to CV1: cv1 * gain / 4096
		int32_t scaledCV = (cv1Value * gainFactor) >> 12;
		
		// Scale to ±2x speed range: scaledCV * 2
		int32_t result = scaledCV * 2;
		
		// Clamp to ±2x speed range
		if (result > 8192) result = 8192;   // +2x max
		if (result < -8192) result = -8192; // -2x max
		
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
				
				// Calculate base playback position using virtual write head for consistent delay timing
				// In freeze mode, virtualWriteHead_ continues advancing while writeHead_ stays frozen
				int32_t basePlaybackPos = virtualWriteHead_ - delayDistance_;
				if (basePlaybackPos < 0) basePlaybackPos += BUFF_LENGTH_SAMPLES;
				
				// Apply spread control with overflow protection
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
					
					// Scale the offset with overflow protection
					// Limit maximum offset to 1/8 of buffer (more conservative) to prevent overflow
					const int32_t maxSafeOffset = BUFF_LENGTH_SAMPLES >> 3; // 1/8 buffer size
					
					// First scaling: randomOffset * maxSafeOffset with overflow check
					int64_t temp64 = (int64_t)randomOffset * maxSafeOffset;
					temp64 >>= 11; // Scale by spread (>> 11 = / 2048)
					
					// Clamp to prevent overflow
					if (temp64 > maxSafeOffset) temp64 = maxSafeOffset;
					if (temp64 < -maxSafeOffset) temp64 = -maxSafeOffset;
					
					// Second scaling: apply spread amount with overflow check
					temp64 = (temp64 * spreadAmount_) >> 12;
					
					// Final clamp to safe range
					if (temp64 > maxSafeOffset) temp64 = maxSafeOffset;
					if (temp64 < -maxSafeOffset) temp64 = -maxSafeOffset;
					
					randomOffset = (int32_t)temp64;
					
					// Apply offset to base position with bounds checking
					playbackPos = basePlaybackPos + randomOffset;
				}
				
				// Ensure position is within buffer bounds and never past write head
				while (playbackPos >= BUFF_LENGTH_SAMPLES) playbackPos -= BUFF_LENGTH_SAMPLES;
				while (playbackPos < 0) playbackPos += BUFF_LENGTH_SAMPLES;
				
				// Safety check: ensure grain never reads past write head (allow small safety margin)
				const int32_t safetyMargin = SAFETY_MARGIN_SAMPLES;
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
				grains_[i].freezeCounter = 0; // Initialize freeze counter
				grains_[i].loopSize = grainSize_; // Store current grain size for potential loop mode
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
		// Position in grain: 0 to grainSize-1, normalized to 0-4095 (Q12)
		int32_t posQ12 = (grain.sampleCount << 12) / grainSize_; // Q12 normalized position (0-4095)
		if (posQ12 < 0) posQ12 = 0;
		if (posQ12 > 4095) posQ12 = 4095;
		// Map Q12 position to 0-255 table index
		int idx = (posQ12 * (HANN_TABLE_SIZE - 1)) >> 12; // 0-255
		// Linear interpolation for extra smoothness
		int nextIdx = (idx < HANN_TABLE_SIZE - 1) ? idx + 1 : idx;
		int frac = (posQ12 * (HANN_TABLE_SIZE - 1)) & 0xFFF; // Q12 frac
		int32_t w0 = hannWindowTable_[idx];
		int32_t w1 = hannWindowTable_[nextIdx];
		int32_t weight = w0 + (((w1 - w0) * frac) >> 12);
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
		
		// Handle output normalization with division by zero protection
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
			// No active grains or zero total weight - return silence
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
						
						// Store original position for boundary checking
						int32_t originalPos = grains_[i].readPos;
						int32_t originalFrac = grains_[i].readFrac;
						
						// Advance read position with fractional tracking
						grains_[i].readFrac += grainSpeed;
						
						// CRASH PROTECTION: Bounded iteration limits to prevent runaway loops
						int32_t iterationCount = 0;
						
						// Handle integer overflow from fractional part with bounded iterations
						while (grains_[i].readFrac >= 4096 && iterationCount < MAX_FRACTIONAL_ITERATIONS) {
							grains_[i].readPos++;
							grains_[i].readFrac -= 4096;
							iterationCount++;
							
							// Handle buffer wraparound
							if (grains_[i].readPos >= BUFF_LENGTH_SAMPLES) {
								grains_[i].readPos -= BUFF_LENGTH_SAMPLES;
							}
						}
						
						// If we hit the iteration limit, clamp the fractional part
						if (grains_[i].readFrac >= 4096) {
							grains_[i].readFrac = 4095; // Clamp to just under 1.0
						}
						
						// Reset iteration counter for negative speed handling
						iterationCount = 0;
						
						// Handle negative speeds (reverse playback) with bounded iterations
						while (grains_[i].readFrac < 0 && iterationCount < MAX_FRACTIONAL_ITERATIONS) {
							grains_[i].readPos--;
							grains_[i].readFrac += 4096;
							iterationCount++;
							
							// Handle buffer wraparound
							if (grains_[i].readPos < 0) {
								grains_[i].readPos += BUFF_LENGTH_SAMPLES;
							}
						}
						
						// If we hit the iteration limit, clamp the fractional part
						if (grains_[i].readFrac < 0) {
							grains_[i].readFrac = 0; // Clamp to 0
						}
						
						// WRITE HEAD BOUNDARY CHECK: Prevent grains from reading past write head
						const int32_t safetyMargin = SAFETY_MARGIN_SAMPLES;
						int32_t maxSafePos = writeHead_ - safetyMargin;
						if (maxSafePos < 0) maxSafePos += BUFF_LENGTH_SAMPLES;
						
						// Calculate distance from grain to write head (accounting for circular buffer)
						int32_t distanceToWrite = writeHead_ - grains_[i].readPos;
						if (distanceToWrite < 0) distanceToWrite += BUFF_LENGTH_SAMPLES;
						
						// If grain is too close to write head, clamp it to safe position
						if (distanceToWrite < safetyMargin) {
							grains_[i].readPos = maxSafePos;
							grains_[i].readFrac = 0; // Reset fractional part when clamped
						}
						
						// Deactivate grain if it's finished (only when not frozen)
						if (grains_[i].sampleCount >= grainSize_)
						{
							grains_[i].active = false;
						}
					}
					else
					{
						// When speed is 0, grain is frozen - increment freeze counter
						// BUT: Don't apply freeze timeout to looping grains (they're supposed to stay active)
						if (!grains_[i].looping)
						{
							grains_[i].freezeCounter++;
							
							// If grain has been frozen too long, deactivate it to prevent stuck grains
							if (grains_[i].freezeCounter >= GRAIN_FREEZE_TIMEOUT)
							{
								grains_[i].active = false;
							}
						}
						// Looping grains stay frozen at their current position when speed = 0
					}
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
				// Use the grain's stored loop size (set when grain was created)
				// This prevents race condition where grainSize_ changes after grain creation
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

};

int main()
{
	OC_DT card;
	card.EnableNormalisationProbe(); // Enable CV jack detection
	card.Run();
}
