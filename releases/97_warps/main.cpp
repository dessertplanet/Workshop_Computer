// Warps for the Music Thing Workshop System Computer.
//
// A port of Mutable Instruments' "parasites" Warps meta-modulator to the
// RP2040 / ComputerCard. The DSP (warps/, stmlib/) is vendored unchanged from
// the original firmware; this file is the hardware glue.
//
// Signal flow (external-carrier / classic Warps mode):
//   Audio In 1  -> carrier   (Modulator channel 0, left)
//   Audio In 2  -> modulator (Modulator channel 1, right)
//   Audio Out 1 <- main output   (cross-modulated signal)
//   Audio Out 2 <- aux output    (carrier + modulator sum)
//
// Controls:
//   Main knob -> modulation_algorithm (0..8): sweeps the meta-algorithm
//                (xfade -> fold -> ring mods -> xor -> comparator -> vocoder)
//   Knob X    -> modulation_parameter (timbre / algorithm depth)
//   Knob Y    -> channel drive (input gain / saturation, both channels)
//   Switch    -> reserved for feature-mode selection (see NOTE below)
//
// NOTE on real-time performance: Warps was designed for an STM32F4 with a
// hardware FPU at 168 MHz. The RP2040 (Cortex-M0+) has NO FPU, so every float
// op is software-emulated. Expect the heavier modes (vocoder, frequency
// shifter) to be too slow for 48 kHz. Start with the cheap cross-mod
// algorithms and profile before enabling the rest. See README.md.

#include "ComputerCard.h"

#include "warps/dsp/modulator.h"

namespace {

// Block size fed to the Modulator. Must be <= warps::kMaxBlockSize (96).
// Smaller = lower latency but more per-block overhead.
constexpr size_t kBlock = 32;

// ComputerCard audio is 12-bit signed (-2048..2047). Warps works in 16-bit
// signed frames. Shift by 4 to convert between the two.
constexpr int kAudioShift = 4;

} // namespace

class Warps : public ComputerCard {
public:
    Warps() {
        modulator_.Init(48000.0f);
        // FEATURE_MODE_META is the default set by Init(); it is the classic
        // Warps behaviour where modulation_algorithm sweeps across algorithms.
        modulator_.set_feature_mode(warps::FEATURE_MODE_META);

        for (size_t i = 0; i < kBlock; ++i) {
            in_block_[i].l = in_block_[i].r = 0;
            out_block_[i].l = out_block_[i].r = 0;
        }
    }

    virtual void ProcessSample() override {
        // Emit the already-computed output sample for this slot.
        AudioOut1(static_cast<int16_t>(out_block_[idx_].l >> kAudioShift));
        AudioOut2(static_cast<int16_t>(out_block_[idx_].r >> kAudioShift));

        // Capture this sample's inputs into the block being filled.
        in_block_[idx_].l = static_cast<int16_t>(AudioIn1() << kAudioShift); // carrier
        in_block_[idx_].r = static_cast<int16_t>(AudioIn2() << kAudioShift); // modulator

        ++idx_;
        if (idx_ >= kBlock) {
            idx_ = 0;
            UpdateParameters();
            modulator_.Process(in_block_, out_block_, kBlock);
        }

        UpdateLeds();
    }

private:
    void UpdateParameters() {
        warps::Parameters* p = modulator_.mutable_parameters();

        // Main knob -> algorithm sweep. Keep strictly below 8.0 so the integer
        // algorithm-table index stays in range.
        float algo = (static_cast<float>(KnobVal(Knob::Main)) / 4095.0f) * 7.999f;
        p->modulation_algorithm = algo;
        p->raw_algorithm = algo;
        p->raw_algorithm_pot = algo;
        p->raw_algorithm_cv = 0.0f;

        // Knob X -> timbre / modulation parameter (0..1).
        p->modulation_parameter = static_cast<float>(KnobVal(Knob::X)) / 4095.0f;

        // Knob Y -> per-channel drive (0..1). A small floor keeps some signal
        // flowing even with the knob down.
        float drive = static_cast<float>(KnobVal(Knob::Y)) / 4095.0f;
        p->channel_drive[0] = drive;
        p->channel_drive[1] = drive;
        p->raw_level[0] = drive;
        p->raw_level[1] = drive;

        // External carrier (use Audio In 1 as the carrier, not the internal
        // oscillator). 0 = external.
        p->carrier_shape = 0;
        p->note = 48.0f;
    }

    void UpdateLeds() {
        // Show algorithm position on LED 0, timbre on 2, drive on 4.
        LedBrightness(0, KnobVal(Knob::Main));
        LedBrightness(2, KnobVal(Knob::X));
        LedBrightness(4, KnobVal(Knob::Y));
    }

    warps::Modulator modulator_;
    warps::ShortFrame in_block_[kBlock];
    warps::ShortFrame out_block_[kBlock];
    size_t idx_ = 0;
};

int main() {
    // Overclock to give the FPU-less M0+ a fighting chance at the float DSP.
    set_sys_clock_khz(225000, true);
    Warps card;
    card.Run();
}
