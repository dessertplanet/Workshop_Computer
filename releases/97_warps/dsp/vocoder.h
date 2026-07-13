// Fixed-point channel vocoder with spectral freeze, modelled on the Warps
// vocoder but restructured for per-sample integer processing on the RP2040.
//
// Structure: 16 bandpass biquads (~100 Hz .. 8 kHz, log-spaced) analyze both
// the modulator and the carrier at full rate. A per-band envelope follower
// tracks the modulator; each carrier band is scaled by its envelope and the
// bands are summed.
//
// Freeze ("capture"): when the release parameter is fully clockwise, the
// followers' attack and decay both go to zero (exactly as in the original's
// EnvelopeFollower::set_freeze), holding the modulator's spectral envelope
// as a static filter response applied to the carrier.
//
// Biquad coefficients are computed once with floats at construction time;
// the audio path is integer-only.

#ifndef WARPS_DSP_VOCODER_H_
#define WARPS_DSP_VOCODER_H_

#include <cmath>
#include <cstdint>

#include "dsp/xmod_algorithms.h"

namespace xmod {

// Bandpass biquad (RBJ constant-0dB-peak), coefficients Q14, state Q12.
class BandpassBiquad
{
public:
	void Configure(float centre_hz, float q, float sample_rate)
	{
		float w0 = 2.0f * 3.14159265f * centre_hz / sample_rate;
		float alpha = sinf(w0) / (2.0f * q);
		float a0 = 1.0f + alpha;
		b0_ = static_cast<int32_t>(16384.0f * (alpha / a0));
		a1_ = static_cast<int32_t>(16384.0f * (-2.0f * cosf(w0) / a0));
		a2_ = static_cast<int32_t>(16384.0f * ((1.0f - alpha) / a0));
	}

	int32_t Process(int32_t x)
	{
		// y = b0*x - b0*x2 - a1*y1 - a2*y2  (b1 = 0, b2 = -b0)
		// Rounded shift: plain >>14 truncates toward -inf, and that -0.5LSB
		// bias is integrated by the resonant feedback into a huge DC offset
		// that pins the output at the clip rail.
		int32_t y = (b0_ * (x - x2_) - a1_ * y1_ - a2_ * y2_ + 8192) >> 14;
		x2_ = x1_; x1_ = x;
		y2_ = y1_; y1_ = y;
		return y;
	}

private:
	int32_t b0_ = 0, a1_ = 0, a2_ = 0;
	int32_t x1_ = 0, x2_ = 0;
	int32_t y1_ = 0, y2_ = 0;
};

class Vocoder
{
public:
	Vocoder()
	{
		// 16 log-spaced bands, ~100 Hz to ~8 kHz: ratio = (80)^(1/15) ~ 1.34.
		float f = 100.0f;
		for (int i = 0; i < kNumBands; ++i)
		{
			// Q=1.5: wide enough bands to overlap and keep output level up.
			modulator_bands_[i].Configure(f, 1.5f, 48000.0f);
			carrier_bands_[i].Configure(f, 1.5f, 48000.0f);

			// Base decay coefficient scales with band frequency (higher
			// bands follow faster), Q12 relative to a mid release setting.
			float decay = f / 48000.0f;
			if (decay > 0.25f) decay = 0.25f;
			base_decay_q12_[i] = static_cast<int32_t>(4096.0f * decay);

			f *= 1.3389f;
		}
	}

	// formant_shift: 0..4095, centre (2048) = neutral. Slides the captured
	//                envelope down/up the bands by up to +/-4 bands.
	// release:       0..4095 (position within the vocoder zone) = release
	//                time, short..long.
	// freeze:        holds the spectral envelope (capture mode); driven by
	//                the switch, not a knob end-stop, so it is always
	//                reachable and always reversible.
	int32_t Process(int32_t carrier, int32_t modulator,
	                int32_t formant_shift, int32_t release, bool freeze)
	{
		// Release scale: longer release -> slower envelopes. Q12, 4096..~64.
		int32_t release_scale = 4096 - ((release * 4032) >> 12);

		for (int i = 0; i < kNumBands; ++i)
		{
			int32_t mod_band = modulator_bands_[i].Process(modulator);
			band_level_[i] = mod_band < 0 ? -mod_band : mod_band;
		}

		if (!freeze)
		{
			for (int i = 0; i < kNumBands; ++i)
			{
				// One-pole follower: attack = 4x decay, like the original's
				// 2.0/0.5 asymmetry.
				int32_t decay_q12 = (base_decay_q12_[i] * release_scale) >> 12;
				if (decay_q12 < 1) decay_q12 = 1;
				int32_t error = (band_level_[i] << 2) - envelope_[i]; // gain 4x
				int32_t coeff = error > 0 ? decay_q12 << 2 : decay_q12;
				if (coeff > 4096) coeff = 4096;
				envelope_[i] += (error * coeff) >> 12;
			}
		}

		// Formant shift: band i reads the envelope at position i + shift,
		// interpolated, +/-4 bands full range (bands are ~5 semitones apart).
		int32_t shift_q12 = (formant_shift - 2048) << 3; // +/-4.0 in Q12

		int32_t out = 0;
		for (int i = 0; i < kNumBands; ++i)
		{
			int32_t pos = (i << 12) + shift_q12;
			int32_t env;
			if (pos <= 0)
			{
				env = envelope_[0];
			}
			else if (pos >= (kNumBands - 1) << 12)
			{
				env = envelope_[kNumBands - 1];
			}
			else
			{
				int32_t idx = pos >> 12;
				int32_t frac = pos & 4095;
				env = envelope_[idx] +
				      (((envelope_[idx + 1] - envelope_[idx]) * frac) >> 12);
			}
			int32_t car_band = carrier_bands_[i].Process(carrier);
			out += (car_band * env) >> 12;
		}

		// Soft limit rather than hard clip, standing in for the original's
		// output limiter.
		return SoftLimit(out >> 1);
	}

private:
	static constexpr int kNumBands = 16;

	BandpassBiquad modulator_bands_[kNumBands];
	BandpassBiquad carrier_bands_[kNumBands];
	int32_t envelope_[kNumBands] = {};
	int32_t band_level_[kNumBands] = {};
	int32_t base_decay_q12_[kNumBands] = {};
};

} // namespace xmod

#endif // WARPS_DSP_VOCODER_H_
