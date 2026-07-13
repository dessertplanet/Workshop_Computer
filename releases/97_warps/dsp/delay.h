// Delay-line based modes: a shared fixed-point delay line with fractional
// (Hermite) reads, used by two zones:
//
//   EchoDelay     - classic delay: timbre = time, zone fraction = feedback.
//   StereoDoppler - 2D position panner (after parasites' doppler): timbre =
//                   left/right position, zone fraction = distance. Produces
//                   a true stereo pair (main = left, aux = right) via
//                   inter-aural time difference, per-side gain, and a
//                   distance delay whose slow slewing produces pitch bends.
//
// The buffers make these objects large; they must live in static storage.

#ifndef WARPS_DSP_DELAY_H_
#define WARPS_DSP_DELAY_H_

#include <cstdint>

#include "dsp/xmod_algorithms.h"

namespace xmod {

// Power-of-two circular buffer of Q12 samples with Q8 fractional reads.
template <int32_t kSizeLog2>
class DelayLine
{
public:
	static constexpr int32_t kSize = 1 << kSizeLog2;
	static constexpr int32_t kMask = kSize - 1;

	void Write(int32_t sample)
	{
		buffer_[write_] = static_cast<int16_t>(Clip(sample));
		write_ = (write_ + 1) & kMask;
	}

	// delay_q8: delay behind the write head, in Q8 samples. 4-point Hermite
	// interpolation for clean pitch bends while the delay slews.
	int32_t ReadHermite(int32_t delay_q8) const
	{
		int32_t delay_int = delay_q8 >> 8;
		int32_t t = delay_q8 & 0xFF; // Q8 fraction

		int32_t i1 = (write_ - 1 - delay_int) & kMask;
		int32_t xm1 = buffer_[(i1 + 1) & kMask]; // one sample newer
		int32_t x0 = buffer_[i1];
		int32_t x1 = buffer_[(i1 - 1) & kMask];
		int32_t x2 = buffer_[(i1 - 2) & kMask];

		// Catmull-Rom in Q8 steps (values Q12, well within 32 bits).
		int32_t c = (x1 - xm1) >> 1;
		int32_t v = x0 - x1;
		int32_t w = c + v;
		int32_t a = w + v + ((x2 - x0) >> 1);
		int32_t b_neg = w + a;
		int32_t y = ((((((a * t) >> 8) - b_neg) * t) >> 8) + c);
		y = ((y * t) >> 8) + x0;
		return y;
	}

private:
	int16_t buffer_[kSize] = {};
	int32_t write_ = 0;
};

// Classic delay. timbre = delay time (~1ms..340ms), fraction = feedback
// (0..~95%), 50/50 dry/wet mix.
class EchoDelay
{
public:
	int32_t Process(int32_t carrier, int32_t modulator,
	                int32_t time, int32_t feedback)
	{
		int32_t dry = Clip((carrier + modulator) >> 1);

		// Target delay in Q8 samples: 48..~16350. 1019 ~= range/4095 in Q8.
		int32_t target_q8 = (48 << 8) + time * 1019;
		// Light smoothing to avoid zipper noise (fast enough to feel
		// immediate, unlike the doppler's deliberate slow slew).
		delay_q8_ += (target_q8 - delay_q8_) >> 6;

		int32_t wet = line_.ReadHermite(delay_q8_);

		// Feedback 0..~0.95 in Q12.
		int32_t fb = (feedback * 3891) >> 12;
		line_.Write(dry + ((wet * fb) >> 12));

		return Clip((dry + wet) >> 1);
	}

private:
	DelayLine<14> line_; // 16384 samples, ~0.34s
	int32_t delay_q8_ = 48 << 8;
};

// 2D doppler panner. timbre = position (0 = hard left .. 4095 = hard right),
// fraction = distance (near..far: longer delay, quieter, wider pitch bends
// when moved). Main output = left ear, aux output = right ear.
class StereoDoppler
{
public:
	// Returns the left channel; right is available via aux() afterwards.
	int32_t Process(int32_t carrier, int32_t modulator,
	                int32_t position, int32_t distance)
	{
		int32_t dry = Clip((carrier + modulator) >> 1);
		line_.Write(dry);

		// Distance delay: 24..~9600 samples (0.5ms..200ms), Q8.
		int32_t base_q8 = (24 << 8) + distance * 600;

		// ITD: up to ~0.7ms (34 samples) between ears, Q8.
		int32_t pan = position - 2048;              // -2048..2047
		int32_t itd_q8 = (pan * (34 << 8)) >> 11;   // -34..+34 samples

		// Slow slew -> doppler pitch bends as position/distance move.
		int32_t target_l = base_q8 + (itd_q8 > 0 ? itd_q8 : 0);
		int32_t target_r = base_q8 + (itd_q8 < 0 ? -itd_q8 : 0);
		delay_l_q8_ += (target_l - delay_l_q8_) >> 11;
		delay_r_q8_ += (target_r - delay_r_q8_) >> 11;

		// Per-side gain: constant-power-ish pan plus distance attenuation.
		int32_t gl = 2048 - (pan >> 1);             // 1024..3071
		int32_t gr = 2048 + (pan >> 1);
		int32_t att = 4096 - ((distance * 2867) >> 12); // 1.0 .. ~0.3
		gl = (gl * att) >> 12;
		gr = (gr * att) >> 12;

		int32_t left = (line_.ReadHermite(delay_l_q8_) * gl) >> 11;
		aux_ = Clip((line_.ReadHermite(delay_r_q8_) * gr) >> 11);
		return Clip(left);
	}

	int32_t aux() const { return aux_; }

private:
	DelayLine<14> line_;
	int32_t delay_l_q8_ = 24 << 8;
	int32_t delay_r_q8_ = 24 << 8;
	int32_t aux_ = 0;
};

} // namespace xmod

#endif // WARPS_DSP_DELAY_H_
