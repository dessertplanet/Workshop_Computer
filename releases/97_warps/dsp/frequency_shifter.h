// Fixed-point single-sideband frequency shifter.
//
// Structure: a phase-difference network (two cascades of four second-order
// allpass sections whose outputs are 90 degrees apart across the audio band)
// feeds a quadrature oscillator ring modulator:
//
//   out = I * cos(wt) -/+ Q * sin(wt)     (- = shift up, + = shift down)
//
// Allpass coefficients are the classic Hilbert half-band pair (e.g. as used
// in the Warps/parasites frequency shifter and many phase-vocoder designs),
// quantised to Q14. Audio is Q12 throughout; no floats in the audio path
// (the sine table is built once at construction).

#ifndef WARPS_DSP_FREQUENCY_SHIFTER_H_
#define WARPS_DSP_FREQUENCY_SHIFTER_H_

#include <cmath>
#include <cstdint>

#include "dsp/xmod_algorithms.h"

namespace xmod {

// Second-order allpass: y[n] = a*(x[n] + y[n-2]) - x[n-2], a in Q14.
class AllpassSection
{
public:
	explicit AllpassSection(int32_t a_q14) : a_(a_q14) {}

	int32_t Process(int32_t x)
	{
		// Rounded shift to avoid the truncation DC bias (see vocoder.h).
		int32_t y = ((a_ * (x + y2_) + 8192) >> 14) - x2_;
		x2_ = x1_; x1_ = x;
		y2_ = y1_; y1_ = y;
		return y;
	}

private:
	int32_t a_;
	int32_t x1_ = 0, x2_ = 0;
	int32_t y1_ = 0, y2_ = 0;
};

class FrequencyShifter
{
public:
	FrequencyShifter()
	{
		for (int i = 0; i < kSineSize; ++i)
		{
			sine_q14_[i] = static_cast<int16_t>(
				16384.0f * sinf(2.0f * 3.14159265f * static_cast<float>(i) /
				                static_cast<float>(kSineSize)));
		}
	}

	// parameter: 0..4095. Centre (2048) = no shift; below shifts down, above
	// shifts up, with a quadratic curve out to roughly +/-1 kHz.
	int32_t Process(int32_t carrier, int32_t modulator, int32_t parameter)
	{
		int32_t x = Clip((carrier + modulator) >> 1);

		// Two allpass paths, 90 degrees apart. Path B is additionally
		// delayed one sample to complete the quadrature relationship.
		int32_t i_path = x;
		for (auto& s : path_a_) i_path = s.Process(i_path);

		int32_t q_path = x;
		for (auto& s : path_b_) q_path = s.Process(q_path);
		int32_t q_delayed = q_z1_;
		q_z1_ = q_path;

		// Quadratic bipolar shift amount.
		int32_t d = parameter - 2048;               // -2048..2047
		bool up = d >= 0;
		if (d < 0) d = -d;
		uint32_t increment = static_cast<uint32_t>(d * d * 21); // ~1kHz max
		phase_ += up ? increment : 0u - increment;

		int32_t sin_v = SineQ14(phase_);
		int32_t cos_v = SineQ14(phase_ + 0x40000000u);

		int32_t out = (i_path * cos_v - q_delayed * sin_v) >> 14;
		return Clip(out);
	}

private:
	static constexpr int kSineSize = 1024;

	int32_t SineQ14(uint32_t phase) const
	{
		// Top 10 bits index the table; next 8 bits linearly interpolate.
		uint32_t index = phase >> 22;
		int32_t frac = static_cast<int32_t>((phase >> 14) & 0xFF);
		int32_t a = sine_q14_[index];
		int32_t b = sine_q14_[(index + 1) & (kSineSize - 1)];
		return a + (((b - a) * frac) >> 8);
	}

	// Hilbert phase-difference network coefficients, Q14.
	AllpassSection path_a_[4] = {
		AllpassSection(11344),  // 0.6923878
		AllpassSection(15336),  // 0.9360654
		AllpassSection(16191),  // 0.9882295
		AllpassSection(16363),  // 0.9987488
	};
	AllpassSection path_b_[4] = {
		AllpassSection(6590),   // 0.4021921
		AllpassSection(14027),  // 0.8561711
		AllpassSection(15930),  // 0.9722910
		AllpassSection(16307),  // 0.9952885
	};

	int32_t q_z1_ = 0;
	uint32_t phase_ = 0;
	int16_t sine_q14_[kSineSize];
};

} // namespace xmod

#endif // WARPS_DSP_FREQUENCY_SHIFTER_H_
