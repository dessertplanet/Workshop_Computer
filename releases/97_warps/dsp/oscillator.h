// Internal carrier oscillator, after the original Warps' internal carrier
// (sine / triangle / saw / pulse / filtered noise) for use when no external
// carrier is patched - here selected manually via the shape cycle.
//
// Fixed point throughout the audio path: 32-bit phase accumulator, 1024-entry
// sine table (built once at construction), semitone increment table with
// linear interpolation for continuous pitch.

#ifndef WARPS_DSP_OSCILLATOR_H_
#define WARPS_DSP_OSCILLATOR_H_

#include <cmath>
#include <cstdint>

#include "dsp/xmod_algorithms.h"

namespace xmod {

enum class OscShape : int32_t
{
	kOff = 0,
	kSine,
	kTriangle,
	kSaw,
	kPulse,
	kNoiseLp,
	kNumShapes,
};

class CarrierOscillator
{
public:
	CarrierOscillator()
	{
		for (int i = 0; i < kSineSize; ++i)
		{
			sine_q11_[i] = static_cast<int16_t>(
				2047.0f * sinf(2.0f * 3.14159265f * static_cast<float>(i) /
				               static_cast<float>(kSineSize)));
		}
		// Phase increment per MIDI note: 2^32 * f / 48000.
		for (int n = 0; n < 128; ++n)
		{
			float f = 440.0f * powf(2.0f, (static_cast<float>(n) - 69.0f) / 12.0f);
			note_increment_[n] = static_cast<uint32_t>(f * 89478.485f); // 2^32/48000
		}
	}

	void NextShape()
	{
		shape_ = static_cast<OscShape>(
			(static_cast<int32_t>(shape_) + 1) %
			static_cast<int32_t>(OscShape::kNumShapes));
	}

	OscShape shape() const { return shape_; }
	bool enabled() const { return shape_ != OscShape::kOff; }

	// pitch: 0..4095 -> MIDI notes 24..96, interpolated between semitones.
	void SetPitch(int32_t pitch)
	{
		int32_t note_q12 = (24 << 12) + pitch * 72; // Q12 note number
		int32_t note = note_q12 >> 12;
		int32_t frac = note_q12 & 4095;
		if (note > 126) { note = 126; frac = 4095; }
		uint32_t a = note_increment_[note];
		uint32_t b = note_increment_[note + 1];
		increment_ = a + static_cast<uint32_t>(
			(static_cast<uint64_t>(b - a) * static_cast<uint32_t>(frac)) >> 12);
	}

	int32_t Process()
	{
		phase_ += increment_;

		switch (shape_)
		{
		case OscShape::kSine:
			return sine_q11_[phase_ >> 22];
		case OscShape::kTriangle:
		{
			int32_t p = static_cast<int32_t>(phase_ >> 20); // 0..4095
			return (p < 2048 ? (p << 1) : ((4095 - p) << 1)) - 2048;
		}
		case OscShape::kSaw:
			return static_cast<int32_t>(phase_ >> 20) - 2048;
		case OscShape::kPulse:
			return phase_ < 0x80000000u ? 1600 : -1600;
		case OscShape::kNoiseLp:
		{
			// xorshift white noise through a pitch-tracking one-pole lowpass.
			lfsr_ ^= lfsr_ << 13;
			lfsr_ ^= lfsr_ >> 17;
			lfsr_ ^= lfsr_ << 5;
			int32_t white = static_cast<int32_t>(lfsr_ & 0xFFF) - 2048;
			// Cutoff coefficient from the phase increment (higher pitch =
			// brighter noise), Q12, clamped to keep the filter stable.
			int32_t coeff = static_cast<int32_t>(increment_ >> 19);
			if (coeff > 4096) coeff = 4096;
			if (coeff < 16) coeff = 16;
			noise_lp_ += ((white - noise_lp_) * coeff) >> 12;
			return Clip(noise_lp_ << 1);
		}
		case OscShape::kOff:
		default:
			return 0;
		}
	}

private:
	static constexpr int kSineSize = 1024;

	OscShape shape_ = OscShape::kOff;
	uint32_t phase_ = 0;
	uint32_t increment_ = 0;
	uint32_t lfsr_ = 0x12345678u;
	int32_t noise_lp_ = 0;
	int16_t sine_q11_[kSineSize];
	uint32_t note_increment_[128];
};

} // namespace xmod

#endif // WARPS_DSP_OSCILLATOR_H_
