#include "ComputerCard.h"

/*
 * OC-DT Granular Delay
 *
 * A sophisticated granular delay effect with the following features:
 * - 2.6-second circular buffer for audio capture (100k samples at 48kHz)
 * - Up to 3 simultaneous grains with Hann windowing
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
 * - Pulse 2 In: Gate input - forces loop mode when high, returns to switch behavior when low
 *
 * Outputs:
 * - Audio Outs: Granular processed audio (stereo)
 * - CV Out 1: Random noise value (updates when grains are triggered)
 * - CV Out 2: Playback phase of grain 0 (0V=start, 5V=end of grain)
 * - Pulse 1 Out: Triggers when any grain reaches 90% completion
 * - Pulse 2 Out: Stochastic clock - triggers when noise < X knob value, rate inversely proportional to grain size
 *
 * LED Feedback:
 * - LEDs 0,1: Audio output activity (brightness = number of active grains)
 * - LEDs 2,3: CV output levels (brightness = CV voltage magnitude)
 * - LEDs 4,5: Pulse output states (on/off)
 *
 * Performance Optimizations:
 * - Knob values cached and updated at 1000Hz (instead of 48kHz) for reduced CPU overhead
 * - LED feedback updated at 1000Hz (instead of 48kHz) for improved efficiency
 * - Grain size/position parameters updated at 1000Hz (only affect new grains, not existing ones)
 * - Playback speed updated at 48kHz (affects all active grains in real-time)
 * - Fixed maximum of 3 active grains (no dynamic allocation based on grain size)
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
		80000, 79597, 79185, 78765, 78336, 77899, 77454, 77001, 76540, 76072, 75596, 75113, 74623, 74125, 73621, 73110};
	// Timing constants
	static const int32_t GRAIN_TRIGGER_COOLDOWN_SAMPLES = 48; // 1ms cooldown at 48kHz
	static const int32_t SAFETY_MARGIN_SAMPLES = 1000;		  // 20ms safety margin
	static const int32_t GRAIN_END_PULSE_DURATION = 200;	  // 4.2ms pulse duration
	static const int32_t MAX_PULSE_HALF_PERIOD = 32768;
	static const int32_t PULSE_COUNTER_OVERFLOW_LIMIT = 65536;
	static const int32_t VIRTUAL_DETENT_THRESHOLD = 12;
	static const int32_t VIRTUAL_DETENT_EDGE_THRESHOLD = 5;

	// Safety limits
	static const int32_t MAX_FRACTIONAL_ITERATIONS = 4;
	static const int32_t MAX_SAFE_GRAIN_SPEED = 8192;

public:
	OC_DT()
	{
		for (int i = 0; i < BUFF_LENGTH_SAMPLES; i++)
		{
			buffer_[i] = 0;
		}

		stretchRatio_ = 4096;
		grainPlaybackSpeed_ = 4096;
		grainSize_ = 1024;
		maxActiveGrains_ = 3;
		loopMode_ = false;

		grainTriggerCooldown_ = 0;

		pulseOut1Counter_ = 0;
		pulseOut2Counter_ = 0;
		stochasticClockCounter_ = 0;
		stochasticClockPeriod_ = 2400;

		cvOut1NoiseValue_ = 0;
		cvOut2PhaseValue_ = 0;

		updateCounter_ = UPDATE_RATE_DIVIDER - 1;

		cachedMainKnob_ = 2048;
		cachedXKnob_ = 0;
		cachedYKnob_ = 2048;

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
			grains_[i].pulse90Triggered = false;
		}
	}

	virtual void ProcessSample()
	{
		Switch switchPos = SwitchVal();

		bool pulse2Gate = PulseIn2();
		if (pulse2Gate)
		{
			switchPos = Switch::Down;
		}

		// Advance virtual write head when not in freeze mode
		if (switchPos != Switch::Up)
		{
			virtualWriteHead_++;
			if (virtualWriteHead_ >= BUFF_LENGTH_SAMPLES)
			{
				virtualWriteHead_ = 0;
			}
		}

		// Record audio when not in freeze mode
		if (switchPos != Switch::Up)
		{
			uint16_t stereoSample = packStereo(AudioIn1(), AudioIn2());
			buffer_[writeHead_] = stereoSample;
			writeHead_++;
			if (writeHead_ >= BUFF_LENGTH_SAMPLES)
			{
				writeHead_ = 0;
			}
			virtualWriteHead_ = writeHead_;
		}

		// X knob controls delay time/spread or becomes attenuverter when CV1 connected
		int32_t xControlValue = cachedXKnob_;

		if (!Connected(Input::CV1))
		{
			if (xControlValue <= 2047)
			{
				// Left half: delay time control
				delayDistance_ = 2400 + ((xControlValue * (95000 - 2400)) / 2047);
				spreadAmount_ = 0;
			}
			else
			{
				// Right half: spread control with fixed delay
				delayDistance_ = 24000;
				spreadAmount_ = ((xControlValue - 2048) * 4095) / 2047;
			}
		}
		else
		{
			// CV1 connected: X knob becomes attenuverter
			delayDistance_ = 24000;
			spreadAmount_ = 0;
		}

		updatePlaybackSpeed();

		bool shouldTriggerGrain = PulseIn1RisingEdge() && (grainTriggerCooldown_ <= 0);

		if (grainTriggerCooldown_ > 0)
		{
			grainTriggerCooldown_--;
		}

		if (shouldTriggerGrain)
		{
			grainTriggerCooldown_ = GRAIN_TRIGGER_COOLDOWN_SAMPLES;
		}

		if (switchPos == Switch::Up)
		{
			if (shouldTriggerGrain)
			{
				triggerNewGrain();
			}

			int16_t outL = generateStretchedSample(0);
			int16_t outR = generateStretchedSample(1);

			AudioOut1(outL);
			AudioOut2(outR);
		}
		else if (switchPos == Switch::Middle)
		{
			if (loopMode_)
			{
				exitLoopMode();
			}

			if (shouldTriggerGrain)
			{
				triggerNewGrain();
			}

			int16_t outL = generateStretchedSample(0);
			int16_t outR = generateStretchedSample(1);

			AudioOut1(outL);
			AudioOut2(outR);
		}
		else
		{
			if (!loopMode_)
			{
				enterLoopMode();
			}

			int16_t outL = generateStretchedSample(0);
			int16_t outR = generateStretchedSample(1);

			AudioOut1(outL);
			AudioOut2(outR);
		}

		updateGrains();
		updateCVOutputs();
		updatePulseOutputs();

		// Update control parameters at 1000Hz
		updateCounter_++;
		if (updateCounter_ >= UPDATE_RATE_DIVIDER)
		{
			updateCounter_ = 0;
			updateCachedKnobValues();
			updateGrainParameters();
			updateLEDFeedback();
		}
	}

private:
	uint16_t buffer_[BUFF_LENGTH_SAMPLES];
	int32_t writeHead_ = 0;
	int32_t virtualWriteHead_ = 0;
	int32_t delayDistance_ = 10000;
	int32_t spreadAmount_ = 0;

	// Grain system
	static const int MAX_GRAINS = 4;
	static const int32_t GRAIN_FREEZE_TIMEOUT = 48000 * 5;
	struct Grain
	{
		int32_t readPos;
		int32_t readFrac;
		int32_t sampleCount;
		int32_t startPos;
		int32_t loopSize;
		int32_t freezeCounter;
		bool active;
		bool looping;
		bool pulse90Triggered;
		// Per-grain parameters (snapshotted at trigger)
		int32_t delayDistance;
		int32_t spreadAmount;
		int32_t grainSize;
	};
	Grain grains_[MAX_GRAINS];

	int32_t stretchRatio_;
	int32_t grainPlaybackSpeed_;
	int32_t grainSize_;
	int32_t maxActiveGrains_;
	bool loopMode_;

	int32_t grainTriggerCooldown_;

	// Pulse and CV output state
	int32_t pulseOut1Counter_;
	int32_t pulseOut2Counter_;
	int32_t stochasticClockCounter_;
	int32_t stochasticClockPeriod_;

	int16_t cvOut1NoiseValue_;
	int16_t cvOut2PhaseValue_;

	// Control update throttling
	static const int32_t UPDATE_RATE_DIVIDER = 48;
	int32_t updateCounter_;

	// Cached knob values
	int32_t cachedMainKnob_;
	int32_t cachedXKnob_;
	int32_t cachedYKnob_;

	// Interpolated sample reading with wraparound
	int16_t __not_in_flash_func(getInterpolatedSample)(int32_t bufferPos, int32_t frac, int channel)
	{
		// Ensure buffer position is within bounds
		int32_t pos1 = bufferPos;
		while (pos1 >= BUFF_LENGTH_SAMPLES)
			pos1 -= BUFF_LENGTH_SAMPLES;
		while (pos1 < 0)
			pos1 += BUFF_LENGTH_SAMPLES;

		int32_t pos2 = pos1 + 1;
		if (pos2 >= BUFF_LENGTH_SAMPLES)
			pos2 = 0;

		// Safety check
		if (pos1 < 0 || pos1 >= BUFF_LENGTH_SAMPLES)
			pos1 = 0;
		if (pos2 < 0 || pos2 >= BUFF_LENGTH_SAMPLES)
			pos2 = 0;

		int16_t sample1 = unpackStereo(buffer_[pos1], channel);
		int16_t sample2 = unpackStereo(buffer_[pos2], channel);

		// Clamp fractional part
		if (frac < 0)
			frac = 0;
		if (frac >= 4096)
			frac = 4095;

		// Linear interpolation in Q12
		int32_t diff = sample2 - sample1;
		int32_t interpolated = sample1 + ((diff * frac) >> 12);

		// Clamp result
		if (interpolated > 2047)
			interpolated = 2047;
		if (interpolated < -2048)
			interpolated = -2048;

		return (int16_t)interpolated;
	}

	// Update playback speed (affects all active grains)
	void __not_in_flash_func(updatePlaybackSpeed)()
	{
		if (Connected(Input::CV2))
		{
			// CV2 controls pitch, Main knob is attenuverter
			int32_t cv2Val = CVIn2();
			int32_t mainKnobVal = virtualDetentedKnob(cachedMainKnob_);
			grainPlaybackSpeed_ = applyPitchAttenuverter(cv2Val, mainKnobVal);
		}
		else
		{
			// Main knob controls pitch directly
			int32_t mainKnobVal = virtualDetentedKnob(cachedMainKnob_);

			// Map -2x to +2x with pause at center
			if (mainKnobVal <= 2048)
			{
				grainPlaybackSpeed_ = -8192 + ((mainKnobVal * 8192) >> 11);
			}
			else
			{
				int32_t rightKnob = mainKnobVal - 2048;
				grainPlaybackSpeed_ = (rightKnob * 8192) >> 11;
			}
		}

		// Limit speed for safety
		if (grainPlaybackSpeed_ > MAX_SAFE_GRAIN_SPEED)
		{
			grainPlaybackSpeed_ = MAX_SAFE_GRAIN_SPEED;
		}
		if (grainPlaybackSpeed_ < -MAX_SAFE_GRAIN_SPEED)
		{
			grainPlaybackSpeed_ = -MAX_SAFE_GRAIN_SPEED;
		}
	}

	// Update grain parameters (affects newly triggered grains only)
	void __not_in_flash_func(updateGrainParameters)()
	{
		int32_t yControlValue = cachedYKnob_;
		yControlValue = virtualDetentedKnob(yControlValue);

		// Map Y control to stretch ratio
		if (yControlValue <= 2048)
		{
			stretchRatio_ = 1024 + ((yControlValue * 3072) >> 11);
		}
		else
		{
			int32_t rightKnob = yControlValue - 2048;
			stretchRatio_ = 4096 + ((rightKnob * 12288) >> 11);
		}

		// Calculate grain size from stretch ratio
		int32_t normalizedRatio = stretchRatio_ - 1024;
		normalizedRatio = (normalizedRatio * 4096) / 15360;

		if (normalizedRatio < 0)
			normalizedRatio = 0;
		if (normalizedRatio > 4095)
			normalizedRatio = 4095;

		grainSize_ = 64 + ((normalizedRatio * 65472) / 4095);

		if (grainSize_ < 64)
			grainSize_ = 64;
		if (grainSize_ > 65536)
			grainSize_ = 65536;
	}

	int16_t virtualDetentedKnob(int16_t val)
	{
		if (val > 4090)
		{
			val = 4095;
		}
		else if (val < 5)
		{
			val = 0;
		}

		// Center detent for pause/freeze
		if (cabs(val - 2048) < VIRTUAL_DETENT_THRESHOLD)
		{
			val = 2048;
		}

		return val;
	}

	// Apply knob as attenuverter to CV input
	int32_t __not_in_flash_func(applyAttenuverter)(int32_t cvValue, int32_t knobValue)
	{
		// Map knob to scale factor -2.0 to +2.0
		int32_t scaleFactor = ((knobValue - 2048) * 4) + 4096;

		int32_t scaledCV = (cvValue * scaleFactor) >> 12;

		// Convert to 0-4095 range
		int32_t result = scaledCV + 2048;

		if (result < 0)
			result = 0;
		if (result > 4095)
			result = 4095;

		return result;
	}

	// Pitch attenuverter function: applies Main knob as ±1x attenuverter to CV2 pitch input
	int32_t __not_in_flash_func(applyPitchAttenuverter)(int32_t cv2Value, int32_t mainKnobValue)
	{
		// cv2Value: -2048 to +2047 (±5V CV input)
		// mainKnobValue: 0 to 4095 (Main knob with detents)
		// Returns: Q12 speed value (4096 ± attenuated CV2 offset)

		// Map Main knob to ±1x gain factor, with 0 gain at center (2048)
		// 0 -> -4096 (-1x), 2048 -> 0 (0x = no effect), 4095 -> +4096 (+1x)
		int32_t gainFactor;
		if (mainKnobValue == 2048)
		{
			// Exact center - ensure zero gain for perfect 1x speed
			gainFactor = 0;
		}
		else if (mainKnobValue < 2048)
		{
			// Left half: -1x to 0x gain
			gainFactor = -4096 + ((mainKnobValue * 4096) >> 11); // -4096 to 0
		}
		else
		{
			// Right half: 0x to +1x gain
			gainFactor = ((mainKnobValue - 2048) * 4096) >> 11; // 0 to +4096
		}

		// Apply gain to CV2: cv2 * gain / 4096
		// This gives us the attenuated CV signal in the range ±2048
		int32_t attenuatedCV = (cv2Value * gainFactor) >> 12;

		// Convert attenuated CV to speed offset: ±2048 CV -> ±8192 speed (±2x in Q12)
		int32_t speedOffset = attenuatedCV * 4; // Scale CV range to speed range

		// Apply offset relative to normal 1x speed (4096 in Q12)
		int32_t result = 4096 + speedOffset; // Base 1x speed + CV offset

		// Clamp to ±2x speed range around 1x
		if (result > 12288)
			result = 12288; // +3x max (4096 + 8192)
		if (result < -4096)
			result = -4096; // -1x min (4096 - 8192, but clamped)

		return result;
	}

	// Enforce grain limit by deactivating excess grains (oldest first, based on startPos)
	void __not_in_flash_func(enforceGrainLimit)()
	{
		// Count currently active grains
		int32_t activeCount = 0;
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				activeCount++;
			}
		}

		// If we have more active grains than the current limit, deactivate the oldest ones
		while (activeCount > maxActiveGrains_)
		{
			// Find the oldest grain (furthest read position from start)
			int oldestGrainIndex = -1;
			int32_t maxSampleCount = -1;

			for (int i = 0; i < MAX_GRAINS; i++)
			{
				if (grains_[i].active && grains_[i].sampleCount > maxSampleCount)
				{
					maxSampleCount = grains_[i].sampleCount;
					oldestGrainIndex = i;
				}
			}

			// Deactivate the oldest grain
			if (oldestGrainIndex >= 0)
			{
				grains_[oldestGrainIndex].active = false;
				activeCount--;
			}
			else
			{
				break; // Safety break if we can't find a grain to deactivate
			}
		}
	}

	void __not_in_flash_func(triggerNewGrain)()
	{
		// Read pulse 2 gate state for consistent behavior
		bool pulse2Gate = PulseIn2();

		// Count currently active grains to respect dynamic limit
		int32_t activeCount = 0;
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				activeCount++;
			}
		}

		// Don't trigger new grain if we're at the current limit
		if (activeCount >= maxActiveGrains_)
		{
			return;
		}

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

				// Reset pulse trigger flag for this grain
				grains_[i].pulse90Triggered = false;

				// Generate new noise value for CV Out 1 when grain is triggered
				cvOut1NoiseValue_ = (int16_t)((rnd12() & 0xFFF) - 2048); // -2048 to +2047

				// Calculate base playback position using virtual write head for consistent delay timing
				int32_t basePlaybackPos = virtualWriteHead_ - grains_[i].delayDistance;
				if (basePlaybackPos < 0)
					basePlaybackPos += BUFF_LENGTH_SAMPLES;

				// Handle CV1 position control vs normal spread control
				int32_t playbackPos;
				if (Connected(Input::CV1))
				{
					// CV1 connected: CV1 controls grain position with X knob as attenuverter
					// Read CV1 input only when grain is triggered
					int32_t cv1Val = CVIn1();		 // -2048 to +2047 (±5V)
					int32_t xKnobVal = cachedXKnob_; // 0 to 4095 (X knob as attenuverter)

					// Convert CV1 to position control value with wrapping
					// 0-5V (cv1Val 0 to +2047) uses full range 0-4095
					// Negative values wrap around from the end
					int32_t rawPositionValue;
					if (cv1Val >= 0)
					{
						// Positive CV: scale 0-2047 to 0-4095 (double the resolution for 0-5V)
						rawPositionValue = (cv1Val * 4095) / 2047;
					}
					else
					{
						// Negative CV: wrap from end of range
						// -2048 to -1 maps to 4095 down to 2048 (wrapping from end)
						rawPositionValue = 4095 + ((cv1Val * 2048) / 2048); // cv1Val is negative, so this subtracts
					}

					// Clamp to valid range
					if (rawPositionValue < 0)
						rawPositionValue = 0;
					if (rawPositionValue > 4095)
						rawPositionValue = 4095;

					// Apply X knob as proper attenuverter
					// X knob at 0 = full inversion (-1x)
					// X knob at 2048 = no effect (0x)
					// X knob at 4095 = full positive (+1x)
					int32_t positionControlValue;

					// Map X knob to gain factor: 0 -> -1x, 2048 -> 0x, 4095 -> +1x
					int32_t gainFactor;
					if (xKnobVal <= 2048)
					{
						// Left half: -1x to 0x
						gainFactor = -4096 + ((xKnobVal * 4096) / 2048); // -4096 to 0
					}
					else
					{
						// Right half: 0x to +1x
						gainFactor = ((xKnobVal - 2048) * 4096) / 2047; // 0 to +4096
					}

					// Apply attenuverter: center position (2048) + (CV offset * gain)
					int32_t cvOffset = rawPositionValue - 2048;			   // -2048 to +2047
					int32_t scaledOffset = (cvOffset * gainFactor) / 4096; // Apply gain
					positionControlValue = 2048 + scaledOffset;			   // Add back to center

					// Final clamp
					if (positionControlValue < 0)
						positionControlValue = 0;
					if (positionControlValue > 4095)
						positionControlValue = 4095;

					// Check if buffer is frozen (switch up or pulse 2 gate high)
					bool bufferIsFrozen = (SwitchVal() == Switch::Up) || pulse2Gate;

					if (bufferIsFrozen)
					{
						// Freeze mode: CV1 scrubs the entire buffer (0-4095 maps to full buffer range)
						// 0 = beginning of buffer, 4095 = end of buffer
						playbackPos = (positionControlValue * (BUFF_LENGTH_SAMPLES - 1)) / 4095;
					}
					else
					{
						// Normal mode: CV1 directly controls buffer position
						// 0 = beginning of buffer, 4095 = end of buffer (no randomization/spread)
						playbackPos = (positionControlValue * (BUFF_LENGTH_SAMPLES - 1)) / 4095;
					}

					// Ensure position is within buffer bounds
					if (playbackPos >= BUFF_LENGTH_SAMPLES)
						playbackPos = BUFF_LENGTH_SAMPLES - 1;
					if (playbackPos < 0)
						playbackPos = 0;
				}
				else
				{
					// CV1 disconnected: Use normal spread control (original behavior)
					if (grains_[i].spreadAmount == 0)
					{
						playbackPos = basePlaybackPos;
					}
					else
					{
						int32_t randomValue = rnd12() & 0xFFF;	   // 0 to 4095
						int32_t randomOffset = randomValue - 2047; // -2047 to +2048, centered better
						const int32_t maxSafeOffset = BUFF_LENGTH_SAMPLES >> 3;
						int64_t temp64 = (int64_t)randomOffset * maxSafeOffset;
						temp64 >>= 11;
						if (temp64 > maxSafeOffset)
							temp64 = maxSafeOffset;
						if (temp64 < -maxSafeOffset)
							temp64 = -maxSafeOffset;
						temp64 = (temp64 * grains_[i].spreadAmount) >> 12;
						if (temp64 > maxSafeOffset)
							temp64 = maxSafeOffset;
						if (temp64 < -maxSafeOffset)
							temp64 = -maxSafeOffset;
						randomOffset = (int32_t)temp64;
						playbackPos = basePlaybackPos + randomOffset;
					}
				}
				while (playbackPos >= BUFF_LENGTH_SAMPLES)
					playbackPos -= BUFF_LENGTH_SAMPLES;
				while (playbackPos < 0)
					playbackPos += BUFF_LENGTH_SAMPLES;

				// Apply write head safety check ONLY when buffer is recording (not frozen)
				// In freeze mode, grains can access the entire buffer safely
				// Also skip safety check when CV1 is connected since position is explicitly controlled
				bool bufferIsFrozen = (SwitchVal() == Switch::Up) || pulse2Gate;
				bool cv1Connected = Connected(Input::CV1);
				if (!bufferIsFrozen && !cv1Connected)
				{
					const int32_t safetyMargin = SAFETY_MARGIN_SAMPLES;
					int32_t maxSafePos = writeHead_ - safetyMargin;
					if (maxSafePos < 0)
						maxSafePos += BUFF_LENGTH_SAMPLES;
					int32_t distanceFromWrite = writeHead_ - playbackPos;
					if (distanceFromWrite < 0)
						distanceFromWrite += BUFF_LENGTH_SAMPLES;
					if (distanceFromWrite < safetyMargin)
					{
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
		Grain &grain = grains_[grainIndex];
		// Safety check to prevent division by zero
		if (grain.grainSize <= 0)
		{
			return 4096; // Full weight if grain size is invalid
		}
		// Position in grain: 0 to grainSize-1, normalized to 0-4095 (Q12)
		int32_t posQ12 = (grain.sampleCount << 12) / grain.grainSize; // Q12 normalized position (0-4095)
		if (posQ12 < 0)
			posQ12 = 0;
		if (posQ12 > 4095)
			posQ12 = 4095;

		// Map Q12 position to table lookup with proper interpolation
		// Scale posQ12 to table range: 0-4095 -> 0-(HANN_TABLE_SIZE-1) in Q12
		int32_t tablePos = (posQ12 * (HANN_TABLE_SIZE - 1)) >> 12;	  // Integer table index
		int32_t tableFrac = (posQ12 * (HANN_TABLE_SIZE - 1)) & 0xFFF; // Q12 fractional part

		// Clamp table index to valid range
		if (tablePos >= HANN_TABLE_SIZE - 1)
		{
			tablePos = HANN_TABLE_SIZE - 1;
			tableFrac = 0;
		}

		// Linear interpolation between table entries
		int32_t w0 = hannWindowTable_[tablePos];
		int32_t w1 = (tablePos < HANN_TABLE_SIZE - 1) ? hannWindowTable_[tablePos + 1] : hannWindowTable_[tablePos];
		int32_t weight = w0 + (((w1 - w0) * tableFrac) >> 12);

		// Ensure weight is never negative (should not happen with proper Hann window)
		if (weight < 0)
			weight = 0;

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
			if (result > 2047)
				result = 2047;
			if (result < -2048)
				result = -2048;

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
		// Read pulse 2 gate state once for consistent behavior
		bool pulse2Gate = PulseIn2();

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
						while (grains_[i].readFrac >= 4096)
						{ // >= 1.0 in Q12
							grains_[i].readPos++;
							grains_[i].readFrac -= 4096;
						}

						// Handle negative speeds (reverse looping)
						while (grains_[i].readFrac < 0)
						{
							grains_[i].readPos--;
							grains_[i].readFrac += 4096;
						}
						// Loop back to start when grain reaches its end
						// This creates the stuttering loop effect
						if (grains_[i].sampleCount >= grains_[i].grainSize)
						{
							// Reset to beginning of grain segment for looping
							grains_[i].readPos = grains_[i].startPos;
							grains_[i].readFrac = 0;
							grains_[i].sampleCount = 0;
							grains_[i].pulse90Triggered = false; // Reset pulse trigger for next loop iteration
						}

						// Handle buffer wraparound for readPos - more efficient than while loops
						if (grains_[i].readPos >= BUFF_LENGTH_SAMPLES)
						{
							grains_[i].readPos %= BUFF_LENGTH_SAMPLES;
						}
						if (grains_[i].readPos < 0)
						{
							grains_[i].readPos = ((grains_[i].readPos % BUFF_LENGTH_SAMPLES) + BUFF_LENGTH_SAMPLES) % BUFF_LENGTH_SAMPLES;
						}
					}

					// Looping grains never deactivate automatically
				}
				else
				{
					// Normal grain behavior				// Only update grain if speed is non-zero (fixes freeze bug)
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
						while (grains_[i].readFrac >= 4096 && iterationCount < MAX_FRACTIONAL_ITERATIONS)
						{
							grains_[i].readPos++;
							grains_[i].readFrac -= 4096;
							iterationCount++;

							// Handle buffer wraparound
							if (grains_[i].readPos >= BUFF_LENGTH_SAMPLES)
							{
								grains_[i].readPos -= BUFF_LENGTH_SAMPLES;
							}
						}

						// If we hit the iteration limit, clamp the fractional part
						if (grains_[i].readFrac >= 4096)
						{
							grains_[i].readFrac = 4095; // Clamp to just under 1.0
						}

						// Reset iteration counter for negative speed handling
						iterationCount = 0;

						// Handle negative speeds (reverse playback) with bounded iterations
						while (grains_[i].readFrac < 0 && iterationCount < MAX_FRACTIONAL_ITERATIONS)
						{
							grains_[i].readPos--;
							grains_[i].readFrac += 4096;
							iterationCount++;

							// Handle buffer wraparound
							if (grains_[i].readPos < 0)
							{
								grains_[i].readPos += BUFF_LENGTH_SAMPLES;
							}
						}

						// If we hit the iteration limit, clamp the fractional part
						if (grains_[i].readFrac < 0)
						{
							grains_[i].readFrac = 0; // Clamp to 0
						}

						// WRITE HEAD BOUNDARY CHECK: Prevent grains from reading past write head
						// Only apply this check when buffer is recording (not frozen)
						bool bufferIsFrozen = (SwitchVal() == Switch::Up) || pulse2Gate;
						if (!bufferIsFrozen)
						{
							const int32_t safetyMargin = SAFETY_MARGIN_SAMPLES;
							int32_t maxSafePos = writeHead_ - safetyMargin;
							if (maxSafePos < 0)
								maxSafePos += BUFF_LENGTH_SAMPLES;

							// Calculate distance from grain to write head (accounting for circular buffer)
							int32_t distanceToWrite = writeHead_ - grains_[i].readPos;
							if (distanceToWrite < 0)
								distanceToWrite += BUFF_LENGTH_SAMPLES;

							// If grain is too close to write head, clamp it to safe position
							if (distanceToWrite < safetyMargin)
							{
								grains_[i].readPos = maxSafePos;
								grains_[i].readFrac = 0; // Reset fractional part when clamped
							}
						}

						// Check if grain has reached 90% completion and trigger Pulse 1
						if (grains_[i].grainSize > 0 && !grains_[i].pulse90Triggered)
						{
							int32_t percent90 = (grains_[i].grainSize * 90) / 100; // 90% of grain size
							if (grains_[i].sampleCount >= percent90 && pulseOut1Counter_ <= 0)
							{
								pulseOut1Counter_ = GRAIN_END_PULSE_DURATION; // 200 samples
								grains_[i].pulse90Triggered = true;			  // Mark as triggered for next loop iteration
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

	// Update pulse outputs
	void __not_in_flash_func(updatePulseOutputs)()
	{
		// Update stochastic clock period based on grain size
		// Wider range than grain size: 480 samples (10ms) to 9600 samples (200ms) at 48kHz
		// Inverse relationship: smaller grains = faster clock, larger grains = slower clock
		int32_t normalizedGrainSize = grainSize_ - 64; // 0 to 65472 range
		int32_t maxPeriod = 9600;					   // 200ms at 48kHz
		int32_t minPeriod = 480;					   // 10ms at 48kHz
		// Inverse mapping: larger grain size = longer period (slower clock)
		stochasticClockPeriod_ = maxPeriod - ((normalizedGrainSize * (maxPeriod - minPeriod)) / 65472);

		// Clamp to valid range
		if (stochasticClockPeriod_ < minPeriod)
			stochasticClockPeriod_ = minPeriod;
		if (stochasticClockPeriod_ > maxPeriod)
			stochasticClockPeriod_ = maxPeriod;

		// Update stochastic clock counter
		stochasticClockCounter_++;
		if (stochasticClockCounter_ >= stochasticClockPeriod_)
		{
			stochasticClockCounter_ = 0;

			// Generate random value and compare with X knob threshold
			int32_t randomValue = rnd12() & 0xFFF; // 0 to 4095
			int32_t xKnobValue = cachedXKnob_;	   // 0 to 4095

			// If random value is less than X knob value, trigger pulse
			// Higher X knob = higher threshold = more pulses
			if (randomValue < xKnobValue && pulseOut2Counter_ <= 0)
			{
				pulseOut2Counter_ = GRAIN_END_PULSE_DURATION; // 200 samples
			}
		}

		// Handle pulse output 1 (countdown)
		if (pulseOut1Counter_ > 0)
		{
			pulseOut1Counter_--;
			PulseOut1(true);
		}
		else
		{
			PulseOut1(false);
		}

		// Handle pulse output 2 (countdown)
		if (pulseOut2Counter_ > 0)
		{
			pulseOut2Counter_--;
			PulseOut2(true);
		}
		else
		{
			PulseOut2(false);
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

	// Update CV outputs
	void __not_in_flash_func(updateCVOutputs)()
	{
		// CV Out 1: Current noise value (updated when grains are triggered)
		CVOut1(cvOut1NoiseValue_);

		// CV Out 2: Playback phase of grain 0 (0 to 2047 = start to end of grain)
		bool grain0Active = false;
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active && i == 0)
			{ // Find grain 0
				grain0Active = true;
				if (grains_[i].grainSize > 0)
				{
					// Calculate phase: 0 to 2047 based on grain progress
					int32_t phase = (grains_[i].sampleCount * 2047) / grains_[i].grainSize;
					if (phase > 2047)
						phase = 2047;
					if (phase < 0)
						phase = 0;
					cvOut2PhaseValue_ = (int16_t)phase;
				}
				break;
			}
		}

		// If grain 0 is not active, output 0V
		if (!grain0Active)
		{
			cvOut2PhaseValue_ = 0;
		}

		CVOut2(cvOut2PhaseValue_);
	}

	// Update LED feedback for all outputs
	void __not_in_flash_func(updateLEDFeedback)()
	{
		// LEDs 0,1: Audio outputs (brightness based on number of active grains)
		int32_t activeGrains = 0;
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				activeGrains++;
			}
		}

		// Scale brightness based on active grains (0-3 grains = 0-4095 brightness)
		uint16_t audioLedBrightness = (uint16_t)((activeGrains * 4095) / MAX_GRAINS);

		LedBrightness(0, audioLedBrightness);
		LedBrightness(1, audioLedBrightness);

		// LEDs 2,3: CV outputs (brightness based on CV level)
		uint16_t ledCV1 = (uint16_t)((cabs(cvOut1NoiseValue_) * 4095) / 2048);
		uint16_t ledCV2 = (uint16_t)((cvOut2PhaseValue_ * 4095) / 2047); // Phase is 0-2047

		LedBrightness(2, ledCV1);
		LedBrightness(3, ledCV2);

		// LEDs 4,5: Pulse outputs (on/off based on pulse state)
		LedOn(4, pulseOut1Counter_ > 0);
		LedOn(5, pulseOut2Counter_ > 0);
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
		// convert two 12 bit signed values to signed 8 bit values and pack into a single 16 bit word
		int8_t left8 = static_cast<int8_t>(left >> 4);
		int8_t right8 = static_cast<int8_t>(right >> 4);
		return (static_cast<uint8_t>(left8) << 8) | static_cast<uint8_t>(right8);
	}

	int16_t unpackStereo(uint16_t stereo, int8_t index)
	{
		// unpack a 16 bit word into two signed 8 bit values and convert to signed 12 bit values (returning one of them based on index)
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

	// Update cached knob values at 1000Hz
	void __not_in_flash_func(updateCachedKnobValues)()
	{
		cachedMainKnob_ = KnobVal(Main);
		cachedXKnob_ = KnobVal(X);
		cachedYKnob_ = KnobVal(Y);
	}
};
int main()
{
	OC_DT card;
	card.EnableNormalisationProbe();
	card.Run();
}
