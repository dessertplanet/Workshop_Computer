// Warps-style cross-modulator for the Music Thing Workshop System Computer.
//
// Fixed-point rewrite of the basic Warps algorithms for the FPU-less RP2040.
// The original Mutable Instruments float DSP (warps/, stmlib/) is vendored on
// disk for reference but not built; see dsp/ for the integer implementation.
//
// Signal flow:
//   Audio In 1  -> carrier
//   Audio In 2  -> modulator
//   Audio Out 1 <- cross-modulated signal (DC-blocked)
//   Audio Out 2 <- dry sum of both inputs (aux, as on original Warps);
//                  in the stereo doppler zone, the RIGHT channel instead
//   CV In 1     -> adds to Main knob (algorithm)
//   CV In 2     -> adds to Knob X (timbre)
//
// Controls:
//   Main knob -> algorithm sweep across 15 zones, crossfaded at boundaries:
//                xfade -> fold -> ring mod (analog) -> ring mod (digital)
//                -> ring mod morph -> xor -> comparator -> comparator8
//                -> comparator+chebyschev -> chebyschev -> bitcrusher
//                -> frequency shifter -> echo delay -> stereo doppler
//                -> vocoder
//   Knob X    -> timbre, matching the original's modulation parameter per
//                algorithm: xfade position, fold amount, ring-mod gain,
//                xor blend, comparator morphs, chebyschev order (1..16),
//                bitcrusher operator (sum/or/xor/shift), frequency shift,
//                delay time, doppler position (L..R), vocoder FORMANT
//                SHIFT (centre = neutral).
//                Secondary parameters ride the Main knob's position WITHIN
//                a zone (as in the original META sweep): fold bias,
//                ring-morph character, bitcrusher quantization depth,
//                delay feedback, doppler distance, vocoder release time.
//   Switch    -> vocoder freeze (spectral capture): up = frozen (latched),
//                down = frozen while held, middle = live tracking.
//   Knob Y    -> input drive (10% floor so signal always flows)
//
// LEDs:
//   0/1/2/3 -> current algorithm zone (binary count, 0..14; LED 3 is the
//              high bit)
//   4       -> latched ON if any sample ever exceeded the 20us budget
//   5       -> CPU headroom (bright = idle, dark = fully loaded)

#include "ComputerCard.h"

#include "hardware/vreg.h"
#include "pico/time.h"

#include "dsp/cpu_meter.h"
#include "dsp/xmod_engine.h"

// Debug taps, readable over the debugger without halting for long.
// Peak absolute effect output (decays slowly) and raw input peaks.
volatile int32_t g_debug_out_peak = 0;
volatile int32_t g_debug_in1_peak = 0;
volatile int32_t g_debug_in2_peak = 0;

class Warps : public ComputerCard
{
public:
	virtual void ProcessSample() override
	{
		meter_.BeginSample();

		// CV inputs add to the knobs (CV is bipolar, roughly -2048..2047).
		int32_t algo = Clamp04095(KnobVal(Knob::Main) + CVIn1());
		int32_t timbre = Clamp04095(KnobVal(Knob::X) + CVIn2());
		engine_.SetControls(algo, timbre, KnobVal(Knob::Y));
		// Switch: middle = normal, up = freeze latched, down = momentary
		// (freeze while held).
		engine_.SetFreeze(SwitchVal() != Switch::Middle);

		int32_t carrier = AudioIn1();
		int32_t modulator = AudioIn2();

		int32_t out = DcBlock(engine_.Process(carrier, modulator));
		AudioOut1(static_cast<int16_t>(out));
		// Out 2: dry sum, except in the stereo doppler zone where it is the
		// right channel of the stereo pair.
		int32_t aux = engine_.has_stereo_aux()
			? engine_.aux()
			: xmod::Clip((carrier + modulator) >> 1);
		AudioOut2(static_cast<int16_t>(aux));

		// Peak-hold debug taps with slow decay (~1s time constant).
		int32_t a = out < 0 ? -out : out;
		if (a > g_debug_out_peak) g_debug_out_peak = a;
		else g_debug_out_peak -= g_debug_out_peak >> 16;
		a = carrier < 0 ? -carrier : carrier;
		if (a > g_debug_in1_peak) g_debug_in1_peak = a;
		else g_debug_in1_peak -= g_debug_in1_peak >> 16;
		a = modulator < 0 ? -modulator : modulator;
		if (a > g_debug_in2_peak) g_debug_in2_peak = a;
		else g_debug_in2_peak -= g_debug_in2_peak >> 16;

		meter_.EndSample();
		UpdateLeds();
	}

private:
	static int32_t Clamp04095(int32_t v)
	{
		if (v < 0) return 0;
		if (v > 4095) return 4095;
		return v;
	}

	// One-pole DC blocker (~7Hz): several algorithms (fold, comparators,
	// frozen vocoder) can produce sustained DC which would otherwise go
	// straight to the DC-coupled outputs.
	int32_t DcBlock(int32_t x)
	{
		int32_t y = x - dc_x1_ + dc_y1_ - (dc_y1_ >> 10);
		dc_x1_ = x;
		dc_y1_ = y;
		return xmod::Clip(y);
	}

	int32_t dc_x1_ = 0;
	int32_t dc_y1_ = 0;

	void UpdateLeds()
	{
		int32_t zone = engine_.zone();
		LedOn(0, zone & 1);
		LedOn(1, zone & 2);
		LedOn(2, zone & 4);
		LedOn(3, zone & 8);
		LedOn(4, meter_.Overrun());
		LedBrightness(5, static_cast<uint16_t>(meter_.Headroom()));
	}

	xmod::Engine engine_;
	xmod::CpuMeter meter_{20}; // 1/48kHz ~= 20.8us; 20 is a safe budget
};

int main()
{
	// Multiple of 48MHz for clean ADC timing (per ComputerCard guidance).
	// 192MHz gives the 16-band vocoder comfortable headroom at 48kHz.
	// NOTE: 240MHz does NOT work on this hardware, with or without a vreg
	// boost to 1.25V - the ComputerCard audio engine fails to run (tested
	// both; without vreg the loop ran with dead outputs, with vreg
	// ProcessSample never executed). Do not raise without re-testing audio.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    set_sys_clock_khz(200000, true);
	// Static: the engine owns a ~32KB delay buffer, far too big for the
	// 2KB core-0 stack.
	static Warps card;
	card.Run();
}
