#include <stdint.h>

#include "ComputerCard.h"
#include "breaky_audio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

class Breaky : public ComputerCard {
 public:
  Breaky() = default;

  void ProcessSample() override {
    if (boot_mute_samples_ > 0) {
      --boot_mute_samples_;
      AudioOut1(0);
      AudioOut2(0);
      for (int i = 0; i < kNumLeds; ++i) {
        LedOff(i);
      }
      return;
    }

    handle_switch_jump();
    update_playback_rate();

    const uint32_t frame = current_frame();
    const uint32_t next_frame = frame + 1u < BREAKY_FRAME_COUNT ? frame + 1u : 0u;
    const uint32_t frac = static_cast<uint32_t>(phase_q32_);

    const StereoFrame a = read_frame(frame);
    const StereoFrame b = read_frame(next_frame);
    const int16_t left = interpolate(a.left, b.left, frac);
    const int16_t right = interpolate(a.right, b.right, frac);

    AudioOut1(left);
    AudioOut2(right);

    advance_phase();

    update_leds();
  }

 private:
  static constexpr int kNumLeds = 6;
  static constexpr uint32_t kBootMuteSamples = BREAKY_SAMPLE_RATE / 10u;
  static constexpr uint32_t kLedUpdateDivider = 1024u;
  static constexpr uint32_t kKnobMax = 4095u;
  static constexpr uint32_t kTempoMinBpm = 100u;
  static constexpr uint32_t kTempoRangeBpm = 100u;
  static constexpr uint32_t kControlUpdateDivider = BREAKY_SAMPLE_RATE / 1000u;
  static constexpr uint16_t kSwitchDebounceSamples = 96u;

  struct StereoFrame {
    int16_t left;
    int16_t right;
  };

  uint64_t phase_q32_ = 0;
  uint64_t phase_inc_q32_ = 1ull << 32u;
  uint32_t led_divider_ = 0;
  uint32_t control_update_divider_ = kControlUpdateDivider;
  uint32_t boot_mute_samples_ = kBootMuteSamples;
  uint16_t switch_down_samples_ = 0;
  bool switch_jump_armed_ = true;

  static int16_t sign_extend_12(uint16_t value) {
    value &= 0x0FFFu;
    if (value & 0x0800u) {
      value |= 0xF000u;
    }
    return static_cast<int16_t>(value);
  }

  static int16_t interpolate(int16_t a, int16_t b, uint32_t frac) {
    const int32_t diff = static_cast<int32_t>(b) - static_cast<int32_t>(a);
    return static_cast<int16_t>(
        static_cast<int32_t>(a) + static_cast<int32_t>((static_cast<int64_t>(diff) * frac) >> 32u));
  }

  static StereoFrame read_frame(uint32_t frame) {
    const uint32_t byte_index = frame * 3u;
    const uint8_t b0 = breaky_audio_data[byte_index];
    const uint8_t b1 = breaky_audio_data[byte_index + 1u];
    const uint8_t b2 = breaky_audio_data[byte_index + 2u];

    const int16_t left = sign_extend_12(static_cast<uint16_t>(b0) |
                                        ((static_cast<uint16_t>(b1) & 0x0Fu)
                                         << 8u));
    const int16_t right =
        sign_extend_12(((static_cast<uint16_t>(b1) >> 4u) & 0x0Fu) |
                       (static_cast<uint16_t>(b2) << 4u));
    return {left, right};
  }

  uint32_t current_frame() const {
    return static_cast<uint32_t>(phase_q32_ >> 32u);
  }

  void advance_phase() {
    phase_q32_ += phase_inc_q32_;
    const uint64_t loop_len_q32 = static_cast<uint64_t>(BREAKY_FRAME_COUNT) << 32u;
    while (phase_q32_ >= loop_len_q32) {
      phase_q32_ -= loop_len_q32;
    }
  }

  void update_playback_rate() {
    if (control_update_divider_ < kControlUpdateDivider) {
      ++control_update_divider_;
      return;
    }
    control_update_divider_ = 0;

    const uint32_t knob = static_cast<uint32_t>(KnobVal(X));
    const uint32_t target_bpm =
        kTempoMinBpm + ((knob * kTempoRangeBpm) + (kKnobMax / 2u)) / kKnobMax;
    phase_inc_q32_ = (static_cast<uint64_t>(target_bpm) << 32u) / BREAKY_SOURCE_BPM;
  }

  void handle_switch_jump() {
    if (SwitchVal() != Down) {
      switch_down_samples_ = 0;
      switch_jump_armed_ = true;
      return;
    }

    if (switch_down_samples_ < kSwitchDebounceSamples) {
      ++switch_down_samples_;
    }

    if (switch_jump_armed_ && switch_down_samples_ >= kSwitchDebounceSamples) {
      jump_to_main_knob_position();
      switch_jump_armed_ = false;
    }
  }

  void jump_to_main_knob_position() {
    const uint32_t knob = static_cast<uint32_t>(KnobVal(Main));
    const uint32_t frame = static_cast<uint32_t>(
        (static_cast<uint64_t>(knob) * (BREAKY_FRAME_COUNT - 1u)) / kKnobMax);
    phase_q32_ = static_cast<uint64_t>(frame) << 32u;
    update_leds(true);
  }

  void update_leds(bool force = false) {
    ++led_divider_;
    if (!force && led_divider_ < kLedUpdateDivider) {
      return;
    }
    led_divider_ = 0;

    const uint32_t segment =
        static_cast<uint32_t>((static_cast<uint64_t>(current_frame()) * kNumLeds) /
                              BREAKY_FRAME_COUNT);
    for (int i = 0; i < kNumLeds; ++i) {
      LedOn(i, static_cast<uint32_t>(i) == segment);
    }
  }
};

int main() {
  set_sys_clock_khz(200000, true);
  stdio_init_all();

  Breaky card;
  card.Run();
}
