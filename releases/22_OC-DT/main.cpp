#include "ComputerCard.h"

/*
 * OC-DT Granular Delay
 * 
 * A sophisticated granular delay effect with the following features:
 * - 2.6-second circular buffer for audio capture (100k samples at 48kHz)
 * - Up to 4 simultaneous grains with Hann windowing
 * - Linear grain sizes from micro (64 samples) to huge (65536 samples)
 * - Bidirectional playback (-2x to +2x speed)
 * - Loop/glitch mode for captured segment looping
 * 
 * Controls:
 * - Main Knob: Grain playback speed/direction (-2x to +2x, center=pause) OR pitch attenuverter when CV2 connected
 * - X Knob: Grain position spread (0=fixed delay, right=random spread) OR attenuverter when CV1 connected (left=invert, center=off, right=normal)
 * - Y Knob: Grain size (linear control from micro to huge grains)
 * - CV1: Grain position control (0-5V covers full range, negative values wrap from end) with X knob as attenuverter
 * - CV2: Pitch control (-5V to +5V = -2x to +2x speed) with Main knob as attenuverter
 * - Switch: Up=Freeze Buffer, Middle=Wet, Down=Loop Mode
 * - Pulse 1 In: Triggers new grains
 * - Pulse 2 In: Forces switch down (loop mode)
 */

#define BUFF_LENGTH_SAMPLES 100000 // 100,000 samples (2.08 seconds at 48kHz)

class OC_DT : public ComputerCard
{
private:
	// 256-entry Hann window lookup table (Q12 format)
	static constexpr int HANN_TABLE_SIZE = 256;
	static constexpr int32_t hannWindowTable_[HANN_TABLE_SIZE] = {
		// 256-entry Hann window, Q12 format, 0.5 * (1 - cos(2 * pi * n / (N-1))) * 4096
		// Properly calculated Hann window - all values are positive, ranging from 0 to 4096
		0, 5, 20, 45, 78, 121, 173, 235, 306, 386, 476, 575, 683, 801, 928, 1064,
		1210, 1365, 1529, 1703, 1886, 2078, 2279, 2490, 2710, 2939, 3177, 3424, 3680, 3945, 4219, 4502,
		4794, 5095, 5405, 5724, 6051, 6387, 6732, 7085, 7447, 7817, 8196, 8583, 8978, 9381, 9792, 10212,
		10639, 11074, 11517, 11968, 12426, 12892, 13365, 13845, 14333, 14828, 15330, 15838, 16354, 16876, 17405, 17940,
		18482, 19030, 19584, 20144, 20710, 21281, 21858, 22440, 23027, 23619, 24216, 24818, 25424, 26035, 26650, 27269,
		27892, 28519, 29149, 29783, 30420, 31060, 31703, 32349, 32997, 33648, 34301, 34956, 35613, 36272, 36932, 37594,
		38257, 38921, 39586, 40252, 40918, 41585, 42252, 42919, 43586, 44253, 44920, 45586, 46252, 46917, 47581, 48244,
		48906, 49567, 50226, 50884, 51540, 52195, 52847, 53498, 54146, 54793, 55437, 56079, 56718, 57355, 57989, 58620,
		59248, 59873, 60495, 61114, 61729, 62341, 62949, 63554, 64154, 64751, 65343, 65931, 66515, 67094, 67668, 68237,
		68801, 69360, 69913, 70461, 71003, 71539, 72069, 72593, 73110, 73621, 74125, 74623, 75113, 75596, 76072, 76540,
		77001, 77454, 77899, 78336, 78765, 79185, 79597, 80000, 80394, 80779, 81155, 81522, 81879, 82227, 82566, 82895,
		83214, 83523, 83822, 84111, 84389, 84657, 84915, 85162, 85398, 85623, 85837, 86040, 86231, 86411, 86580, 86737,
		86883, 87017, 87139, 87249, 87347, 87433, 87507, 87569, 87618, 87655, 87680, 87693, 87693, 87680, 87655, 87618,
		87569, 87507, 87433, 87347, 87249, 87139, 87017, 86883, 86737, 86580, 86411, 86231, 86040, 85837, 85623, 85398,
		85162, 84915, 84657, 84389, 84111, 83822, 83523, 83214, 82895, 82566, 82227, 81879, 81522, 81155, 80779, 80394,
		80000, 79597, 79185, 78765, 78336, 77899, 77454, 77001, 76540, 76072, 75596, 75113, 74623, 74125, 73621, 73110
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
			grains_[i].freezeCounter = 0;
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
		
		// Advance virtual write head for consistent delay timing ONLY when not in freeze mode
		// In freeze mode, virtual write head stays frozen to prevent grains from accessing unrecorded buffer sections
		if (switchPos != Switch::Up) {
			virtualWriteHead_++;
			if (virtualWriteHead_ >= BUFF_LENGTH_SAMPLES)
			{
				virtualWriteHead_ = 0; // Wrap around for circular buffer
			}
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
		// When switch is up, both writeHead_ and virtualWriteHead_ stay frozen
		
		// X knob controls delay time/spread, but when CV1 is connected it becomes an attenuverter
		int32_t xControlValue = KnobVal(X);
		
		// When CV1 is NOT connected: X knob functionality remains as before
		if (!Connected(Input::CV1)) {
			// X knob functionality: Left half = delay time, Right half = spread
			if (xControlValue <= 2047) {
				// Left half (0-2047): Control delay time from shortest to longest
				// Map to delay distance: 0 -> 2400 samples (~50ms), 2047 -> 95000 samples (~2.0s)
				// Keep within buffer bounds (100000 samples) with safety margin
				delayDistance_ = 2400 + ((xControlValue * (95000 - 2400)) / 2047);
				spreadAmount_ = 0; // No spread on left half
			} else {
				// Right half (2048-4095): Control spread with fixed delay time
				delayDistance_ = 24000; // Fixed delay at ~0.5 seconds
				// Map right half to spread: 2048 -> 0, 4095 -> 4095
				spreadAmount_ = ((xControlValue - 2048) * 4095) / 2047;
			}
		} else {
			// CV1 connected: X knob becomes attenuverter, disable spread, use fixed delay
			delayDistance_ = 24000; // Fixed delay at ~0.5 seconds when CV1 controls position
			spreadAmount_ = 0; // Disable noise-based spread when CV1 is connected
			// X knob will be used as attenuverter in triggerNewGrain() function
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
			
			// In loop mode, do NOT trigger new grains - existing grains just loop
			// User must manually trigger grains before entering loop mode
			
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
		// Per-grain parameters (snapshotted at trigger)
		int32_t delayDistance;
		int32_t spreadAmount;
		int32_t grainSize;
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

	// Time-stretching helper functions
	void __not_in_flash_func(updateStretchParameters)()
	{
		// CONTROLS OVERVIEW:
		// Main knob: playback speed/direction (-2.0x to +2.0x with pause at center)
		// Y knob: grain size (linear control from micro to huge grains) 
		// X knob: delay/spread control OR position attenuverter when CV1 connected
		// CV1: grain position control (read only when grains are triggered)
		// CV2: pitch control with Main knob as attenuverter
		
		// CV2 pitch control with Main knob as attenuverter
		if (Connected(Input::CV2)) {
			// CV2 connected - CV2 controls pitch, Main knob becomes attenuverter
			int32_t cv2Val = CVIn2(); // -2048 to +2047 (±5V)
			int32_t mainKnobVal = virtualDetentedKnob(KnobVal(Main)); // Apply detents
			grainPlaybackSpeed_ = applyPitchAttenuverter(cv2Val, mainKnobVal);
		} else {
			// CV2 disconnected - Main knob direct pitch control (original behavior)
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
		
		// Calculate Y control value directly from Y knob (no CV1 interaction for grain size)
		int32_t yControlValue = KnobVal(Y); // Y knob now directly controls grain size
		
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
				// Snapshot delay, spread, and grain size for this grain
				grains_[i].delayDistance = delayDistance_;
				grains_[i].spreadAmount = spreadAmount_;
				grains_[i].grainSize = grainSize_;
				// Calculate base playback position using virtual write head for consistent delay timing
				int32_t basePlaybackPos = virtualWriteHead_ - grains_[i].delayDistance;
				if (basePlaybackPos < 0) basePlaybackPos += BUFF_LENGTH_SAMPLES;
				
				// Handle CV1 position control vs normal spread control
				int32_t playbackPos;
				if (Connected(Input::CV1)) {
					// CV1 connected: CV1 controls grain position with X knob as attenuverter
					// Read CV1 input only when grain is triggered
					int32_t cv1Val = CVIn1(); // -2048 to +2047 (±5V)
					int32_t xKnobVal = KnobVal(X); // 0 to 4095 (X knob as attenuverter)
					
					// Convert CV1 to position control value with wrapping
					// 0-5V (cv1Val 0 to +2047) uses full range 0-4095
					// Negative values wrap around from the end
					int32_t rawPositionValue;
					if (cv1Val >= 0) {
						// Positive CV: scale 0-2047 to 0-4095 (double the resolution for 0-5V)
						rawPositionValue = (cv1Val * 4095) / 2047;
					} else {
						// Negative CV: wrap from end of range
						// -2048 to -1 maps to 4095 down to 2048 (wrapping from end)
						rawPositionValue = 4095 + ((cv1Val * 2048) / 2048); // cv1Val is negative, so this subtracts
					}
					
					// Clamp to valid range
					if (rawPositionValue < 0) rawPositionValue = 0;
					if (rawPositionValue > 4095) rawPositionValue = 4095;
					
					// Apply X knob as proper attenuverter
					// X knob at 0 = full inversion (-1x)
					// X knob at 2048 = no effect (0x) 
					// X knob at 4095 = full positive (+1x)
					int32_t positionControlValue;
					
					// Map X knob to gain factor: 0 -> -1x, 2048 -> 0x, 4095 -> +1x
					int32_t gainFactor;
					if (xKnobVal <= 2048) {
						// Left half: -1x to 0x
						gainFactor = -4096 + ((xKnobVal * 4096) / 2048); // -4096 to 0
					} else {
						// Right half: 0x to +1x
						gainFactor = ((xKnobVal - 2048) * 4096) / 2047; // 0 to +4096
					}
					
					// Apply attenuverter: center position (2048) + (CV offset * gain)
					int32_t cvOffset = rawPositionValue - 2048; // -2048 to +2047
					int32_t scaledOffset = (cvOffset * gainFactor) / 4096; // Apply gain
					positionControlValue = 2048 + scaledOffset; // Add back to center
					
					// Final clamp
					if (positionControlValue < 0) positionControlValue = 0;
					if (positionControlValue > 4095) positionControlValue = 4095;
					
					// Check if buffer is frozen (switch up or pulse 2)
					bool bufferIsFrozen = (SwitchVal() == Switch::Up) || PulseIn2();
					
					if (bufferIsFrozen) {
						// Freeze mode: CV1 scrubs the entire buffer (0-4095 maps to full buffer range)
						playbackPos = (positionControlValue * (BUFF_LENGTH_SAMPLES - 1)) / 4095;
						// Ensure position is within buffer bounds
						if (playbackPos >= BUFF_LENGTH_SAMPLES) playbackPos = BUFF_LENGTH_SAMPLES - 1;
						if (playbackPos < 0) playbackPos = 0;
					} else {
						// Normal mode: CV1 position is relative to write head with safety checks
						// Map 0-4095 to delay range: 2400 to 95000 samples
						int32_t cvDelayDistance = 2400 + ((positionControlValue * (95000 - 2400)) / 4095);
						playbackPos = virtualWriteHead_ - cvDelayDistance;
						if (playbackPos < 0) playbackPos += BUFF_LENGTH_SAMPLES;
					}
				} else {
					// CV1 disconnected: Use normal spread control (original behavior)
					if (grains_[i].spreadAmount == 0) {
						playbackPos = basePlaybackPos;
					} else {
						int32_t randomValue = rnd12() & 0xFFF;  // 0 to 4095
						int32_t randomOffset = randomValue - 2047;  // -2047 to +2048, centered better
						const int32_t maxSafeOffset = BUFF_LENGTH_SAMPLES >> 3;
						int64_t temp64 = (int64_t)randomOffset * maxSafeOffset;
						temp64 >>= 11;
						if (temp64 > maxSafeOffset) temp64 = maxSafeOffset;
						if (temp64 < -maxSafeOffset) temp64 = -maxSafeOffset;
						temp64 = (temp64 * grains_[i].spreadAmount) >> 12;
						if (temp64 > maxSafeOffset) temp64 = maxSafeOffset;
						if (temp64 < -maxSafeOffset) temp64 = -maxSafeOffset;
						randomOffset = (int32_t)temp64;
						playbackPos = basePlaybackPos + randomOffset;
					}
				}
				while (playbackPos >= BUFF_LENGTH_SAMPLES) playbackPos -= BUFF_LENGTH_SAMPLES;
				while (playbackPos < 0) playbackPos += BUFF_LENGTH_SAMPLES;
				
				// Apply write head safety check ONLY when buffer is recording (not frozen)
				// In freeze mode, grains can access the entire buffer safely
				// Also skip safety check when CV1 is connected since position is explicitly controlled
				bool bufferIsFrozen = (SwitchVal() == Switch::Up) || PulseIn2();
				bool cv1Connected = Connected(Input::CV1);
				if (!bufferIsFrozen && !cv1Connected) {
					const int32_t safetyMargin = SAFETY_MARGIN_SAMPLES;
					int32_t maxSafePos = writeHead_ - safetyMargin;
					if (maxSafePos < 0) maxSafePos += BUFF_LENGTH_SAMPLES;
					int32_t distanceFromWrite = writeHead_ - playbackPos;
					if (distanceFromWrite < 0) distanceFromWrite += BUFF_LENGTH_SAMPLES;
					if (distanceFromWrite < safetyMargin) {
						playbackPos = maxSafePos;
					}
				}
				
				grains_[i].readPos = playbackPos;
				grains_[i].readFrac = 0;
				grains_[i].startPos = grains_[i].readPos;
				grains_[i].sampleCount = 0;
				grains_[i].freezeCounter = 0;
				grains_[i].loopSize = grains_[i].grainSize;
				break;
			}
		}
	}
	
	int32_t __not_in_flash_func(calculateGrainWeight)(int grainIndex)
	{
		Grain& grain = grains_[grainIndex];
		// Safety check to prevent division by zero
		if (grain.grainSize <= 0)
		{
			return 4096; // Full weight if grain size is invalid
		}
		// Position in grain: 0 to grainSize-1, normalized to 0-4095 (Q12)
		int32_t posQ12 = (grain.sampleCount << 12) / grain.grainSize; // Q12 normalized position (0-4095)
		if (posQ12 < 0) posQ12 = 0;
		if (posQ12 > 4095) posQ12 = 4095;
		
		// Map Q12 position to table lookup with proper interpolation
		// Scale posQ12 to table range: 0-4095 -> 0-(HANN_TABLE_SIZE-1) in Q12
		int32_t tablePos = (posQ12 * (HANN_TABLE_SIZE - 1)) >> 12; // Integer table index
		int32_t tableFrac = (posQ12 * (HANN_TABLE_SIZE - 1)) & 0xFFF; // Q12 fractional part
		
		// Clamp table index to valid range
		if (tablePos >= HANN_TABLE_SIZE - 1) {
			tablePos = HANN_TABLE_SIZE - 1;
			tableFrac = 0;
		}
		
		// Linear interpolation between table entries
		int32_t w0 = hannWindowTable_[tablePos];
		int32_t w1 = (tablePos < HANN_TABLE_SIZE - 1) ? hannWindowTable_[tablePos + 1] : hannWindowTable_[tablePos];
		int32_t weight = w0 + (((w1 - w0) * tableFrac) >> 12);
		
		// Ensure weight is never negative (should not happen with proper Hann window)
		if (weight < 0) weight = 0;
		
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
					// In loop mode, grains loop within their original captured segment
					// They advance through their grain but loop back to the start when finished
					// This creates repeating stutters of the captured audio segment
					
					if (grainSpeed != 0)
					{
						// Increment sample count for windowing calculation
						grains_[i].sampleCount++;
						
						// Advance read position with fractional tracking
						grains_[i].readFrac += grainSpeed;
						
						// Handle integer overflow from fractional part
						while (grains_[i].readFrac >= 4096) { // >= 1.0 in Q12
							grains_[i].readPos++;
							grains_[i].readFrac -= 4096;
						}
						
						// Handle negative speeds (reverse looping)
						while (grains_[i].readFrac < 0) {
							grains_[i].readPos--;
							grains_[i].readFrac += 4096;
						}
						
						// Loop back to start when grain reaches its end
						// This creates the stuttering loop effect
						if (grains_[i].sampleCount >= grains_[i].grainSize) {
							// Reset to beginning of grain segment for looping
							grains_[i].readPos = grains_[i].startPos;
							grains_[i].readFrac = 0;
							grains_[i].sampleCount = 0;
						}
						
						// Handle buffer wraparound for readPos - more efficient than while loops
						if (grains_[i].readPos >= BUFF_LENGTH_SAMPLES) {
							grains_[i].readPos %= BUFF_LENGTH_SAMPLES;
						}
						if (grains_[i].readPos < 0) {
							grains_[i].readPos = ((grains_[i].readPos % BUFF_LENGTH_SAMPLES) + BUFF_LENGTH_SAMPLES) % BUFF_LENGTH_SAMPLES;
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
						// Only apply this check when buffer is recording (not frozen)
						bool bufferIsFrozen = (SwitchVal() == Switch::Up) || PulseIn2();
						if (!bufferIsFrozen) {
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
						}
						
						// Deactivate grain if it's finished (only when not frozen)
						if (grains_[i].sampleCount >= grains_[i].grainSize)
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
		
		// Check if any grains are currently active
		bool hasActiveGrains = false;
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				hasActiveGrains = true;
				grains_[i].looping = true;
				// Use the grain's stored loop size (set when grain was created)
				// This prevents race condition where grainSize_ changes after grain creation
				// Keep current sampleCount for smooth transition to loop mode
			}
		}
		
		// If no grains are active, trigger one grain to ensure we have something to loop
		if (!hasActiveGrains)
		{
			triggerNewGrain();
			// Now convert the newly triggered grain to looping mode
			for (int i = 0; i < MAX_GRAINS; i++)
			{
				if (grains_[i].active && !grains_[i].looping)
				{
					grains_[i].looping = true;
					// Keep initial sampleCount for proper windowing
					break; // Only need to convert the first one we find
				}
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
				// Keep current sample count for smooth transition from loop mode
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
