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
    handle_left_cv_jump();
    update_external_clock();
    update_playback_rate();
    update_timestretch();

    const StereoFrame output =
        timestretch_active_ ? render_stretched_sample() : render_direct_sample();

    AudioOut1(output.left);
    AudioOut2(output.right);

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
  static constexpr uint32_t kExternalClockMinIntervalSamples = BREAKY_SAMPLE_RATE / 1000u;
  static constexpr uint32_t kExternalClockMinTimeoutSamples = BREAKY_SAMPLE_RATE / 2u;
  static constexpr uint32_t kExternalClockTimeoutMultiplier = 4u;
  static constexpr uint32_t kPulseFlashSamples = BREAKY_SAMPLE_RATE / 20u;
  static constexpr uint32_t kExternalClockPpqn = 2u;
  static constexpr uint16_t kSwitchDebounceSamples = 96u;
  static constexpr uint32_t kCvJumpUpdateDivider = BREAKY_SAMPLE_RATE / 1000u;
  static constexpr uint8_t kCvJumpConnectTicks = 32u;
  static constexpr uint8_t kCvJumpConfirmTicks = 2u;
  static constexpr int16_t kCvJumpThreshold = 64;
  static constexpr int16_t kCvMin = -2048;
  static constexpr int16_t kCvMax = 2047;
  static constexpr uint32_t kStretchQ8One = 256u;
  static constexpr uint32_t kStretchQ8Bypass = (11u * kStretchQ8One + 5u) / 10u;
  static constexpr uint32_t kStretchQ8Max = 10u * kStretchQ8One;
  static constexpr uint32_t kGrainLengthSamples = 2048u;
  static constexpr uint32_t kGrainHopSamples = 1024u;
  static constexpr uint32_t kGrainHopShift = 10u;

  struct StereoFrame {
    int16_t left;
    int16_t right;
  };

  struct Grain {
    uint64_t start_phase_q32;
    uint64_t phase_inc_q32;
    uint16_t age;
  };

  uint64_t phase_q32_ = 0;
  uint64_t phase_inc_q32_ = 1ull << 32u;
  uint64_t timestretch_source_inc_q32_ = 1ull << 32u;
  uint32_t led_divider_ = 0;
  uint32_t control_update_divider_ = kControlUpdateDivider;
  uint32_t stretch_update_divider_ = kControlUpdateDivider;
  uint32_t boot_mute_samples_ = kBootMuteSamples;
  uint32_t clock_age_samples_ = 0;
  uint32_t clock_timeout_samples_ = kExternalClockMinTimeoutSamples;
  uint32_t pulse_flash_samples_ = 0;
  uint32_t cv_jump_update_divider_ = kCvJumpUpdateDivider;
  uint32_t stretch_q8_ = kStretchQ8One;
  uint16_t switch_down_samples_ = 0;
  uint8_t cv_jump_connected_ticks_ = 0;
  uint8_t cv_jump_change_ticks_ = 0;
  int16_t last_accepted_cv_ = 0;
  Grain grains_[2] = {{0, 1ull << 32u, 0}, {0, 1ull << 32u, kGrainHopSamples}};
  bool clock_seen_once_ = false;
  bool external_clock_active_ = false;
  bool switch_jump_armed_ = true;
  bool cv_jump_tracking_ = false;
  bool timestretch_active_ = false;
  bool grains_initialized_ = false;

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

  static int16_t clamp_cv(int32_t value) {
    if (value < kCvMin) {
      return kCvMin;
    }
    if (value > kCvMax) {
      return kCvMax;
    }
    return static_cast<int16_t>(value);
  }

  static uint64_t loop_len_q32() {
    return static_cast<uint64_t>(BREAKY_FRAME_COUNT) << 32u;
  }

  static uint64_t wrap_phase(uint64_t phase) {
    const uint64_t loop_len = loop_len_q32();
    while (phase >= loop_len) {
      phase -= loop_len;
    }
    return phase;
  }

  static uint64_t subtract_phase(uint64_t phase, uint64_t amount) {
    const uint64_t loop_len = loop_len_q32();
    amount %= loop_len;
    if (phase >= amount) {
      return phase - amount;
    }
    return loop_len - (amount - phase);
  }

  static uint32_t grain_window(uint16_t age) {
    if (age >= kGrainLengthSamples) {
      return 0;
    }
    if (age <= kGrainHopSamples) {
      return age;
    }
    return kGrainLengthSamples - age;
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

  static StereoFrame read_interpolated_phase(uint64_t phase_q32) {
    phase_q32 = wrap_phase(phase_q32);
    const uint32_t frame = static_cast<uint32_t>(phase_q32 >> 32u);
    const uint32_t next_frame = frame + 1u < BREAKY_FRAME_COUNT ? frame + 1u : 0u;
    const uint32_t frac = static_cast<uint32_t>(phase_q32);

    const StereoFrame a = read_frame(frame);
    const StereoFrame b = read_frame(next_frame);
    const int16_t left = interpolate(a.left, b.left, frac);
    const int16_t right = interpolate(a.right, b.right, frac);
    return {left, right};
  }

  uint32_t current_frame() const {
    return static_cast<uint32_t>(phase_q32_ >> 32u);
  }

  void advance_phase() {
    advance_phase_by(phase_inc_q32_);
  }

  void advance_phase_by(uint64_t increment_q32) {
    phase_q32_ = wrap_phase(phase_q32_ + increment_q32);
  }

  void update_playback_rate() {
    if (external_clock_active_) {
      control_update_divider_ = kControlUpdateDivider;
      return;
    }

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

  void update_timestretch() {
    if (stretch_update_divider_ < kControlUpdateDivider) {
      ++stretch_update_divider_;
      return;
    }
    stretch_update_divider_ = 0;

    const uint32_t knob = static_cast<uint32_t>(KnobVal(Y));
    const bool was_active = timestretch_active_;
    const uint32_t target_stretch_q8 = stretch_from_y_knob_q8(knob);
    if (target_stretch_q8 < kStretchQ8Bypass) {
      timestretch_active_ = false;
      stretch_q8_ = kStretchQ8One;
      timestretch_source_inc_q32_ = phase_inc_q32_;
      if (was_active) {
        invalidate_timestretch_grains();
      }
      return;
    }

    timestretch_active_ = true;
    stretch_q8_ = target_stretch_q8;
    refresh_timestretch_source_inc();
    if (!was_active) {
      invalidate_timestretch_grains();
    }
  }

  static uint32_t stretch_from_y_knob_q8(uint32_t knob) {
    const uint64_t eased_knob = static_cast<uint64_t>(knob) * knob * knob;
    const uint64_t eased_max = static_cast<uint64_t>(kKnobMax) * kKnobMax * kKnobMax;
    const uint64_t range = kStretchQ8Max - kStretchQ8One;
    return static_cast<uint32_t>(
        kStretchQ8One + ((eased_knob * range) + (eased_max / 2u)) / eased_max);
  }

  void refresh_timestretch_source_inc() {
    timestretch_source_inc_q32_ = (phase_inc_q32_ << 8u) / stretch_q8_;
  }

  void update_external_clock() {
    if (clock_age_samples_ < UINT32_MAX) {
      ++clock_age_samples_;
    }
    if (pulse_flash_samples_ > 0) {
      --pulse_flash_samples_;
    }

    if (PulseIn1RisingEdge()) {
      pulse_flash_samples_ = kPulseFlashSamples;

      const uint32_t interval = clock_age_samples_;
      clock_age_samples_ = 0;

      if (clock_seen_once_ && interval >= kExternalClockMinIntervalSamples) {
        set_external_clock_interval(interval);
      }
      clock_seen_once_ = true;
    }

    if (external_clock_active_ && clock_age_samples_ > clock_timeout_samples_) {
      external_clock_active_ = false;
      clock_seen_once_ = false;
      control_update_divider_ = kControlUpdateDivider;
      update_leds(true);
    }
  }

  void set_external_clock_interval(uint32_t interval) {
    const uint64_t numerator =
        static_cast<uint64_t>(60u * BREAKY_SAMPLE_RATE / kExternalClockPpqn)
        << 32u;
    phase_inc_q32_ =
        numerator / (static_cast<uint64_t>(interval) * BREAKY_SOURCE_BPM);
    if (timestretch_active_) {
      refresh_timestretch_source_inc();
    }

    clock_timeout_samples_ = interval * kExternalClockTimeoutMultiplier;
    if (clock_timeout_samples_ < kExternalClockMinTimeoutSamples) {
      clock_timeout_samples_ = kExternalClockMinTimeoutSamples;
    }
    external_clock_active_ = true;
    update_leds(true);
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

  void handle_left_cv_jump() {
    if (cv_jump_update_divider_ < kCvJumpUpdateDivider) {
      ++cv_jump_update_divider_;
      return;
    }
    cv_jump_update_divider_ = 0;

    if (Disconnected(Input::CV1)) {
      reset_left_cv_jump();
      return;
    }

    const int16_t cv = clamp_cv(CVIn1());
    if (cv_jump_connected_ticks_ < kCvJumpConnectTicks) {
      ++cv_jump_connected_ticks_;
      if (cv_jump_connected_ticks_ >= kCvJumpConnectTicks) {
        last_accepted_cv_ = cv;
        cv_jump_change_ticks_ = 0;
        cv_jump_tracking_ = true;
      }
      return;
    }

    if (!cv_jump_tracking_) {
      last_accepted_cv_ = cv;
      cv_jump_change_ticks_ = 0;
      cv_jump_tracking_ = true;
      return;
    }

    const int32_t diff = static_cast<int32_t>(cv) - static_cast<int32_t>(last_accepted_cv_);
    const int32_t abs_diff = diff < 0 ? -diff : diff;
    if (abs_diff < kCvJumpThreshold) {
      cv_jump_change_ticks_ = 0;
      return;
    }

    if (cv_jump_change_ticks_ < kCvJumpConfirmTicks) {
      ++cv_jump_change_ticks_;
    }

    if (cv_jump_change_ticks_ >= kCvJumpConfirmTicks) {
      jump_to_cv_position(cv);
      last_accepted_cv_ = cv;
      cv_jump_change_ticks_ = 0;
    }
  }

  void reset_left_cv_jump() {
    cv_jump_connected_ticks_ = 0;
    cv_jump_change_ticks_ = 0;
    cv_jump_tracking_ = false;
  }

  void jump_to_main_knob_position() {
    const uint32_t knob = static_cast<uint32_t>(KnobVal(Main));
    const uint32_t frame = static_cast<uint32_t>(
        (static_cast<uint64_t>(knob) * (BREAKY_FRAME_COUNT - 1u)) / kKnobMax);
    phase_q32_ = static_cast<uint64_t>(frame) << 32u;
    invalidate_timestretch_grains();
    update_leds(true);
  }

  void jump_to_cv_position(int16_t cv) {
    const uint32_t normalized_cv = static_cast<uint32_t>(cv - kCvMin);
    const uint32_t frame = static_cast<uint32_t>(
        (static_cast<uint64_t>(normalized_cv) * (BREAKY_FRAME_COUNT - 1u)) / kKnobMax);
    phase_q32_ = static_cast<uint64_t>(frame) << 32u;
    invalidate_timestretch_grains();
    update_leds(true);
  }

  StereoFrame render_direct_sample() {
    invalidate_timestretch_grains();
    const StereoFrame output = read_interpolated_phase(phase_q32_);
    advance_phase();
    return output;
  }

  StereoFrame render_stretched_sample() {
    if (!grains_initialized_) {
      initialize_timestretch_grains();
    }

    int32_t left = 0;
    int32_t right = 0;
    accumulate_grain(grains_[0], left, right);
    accumulate_grain(grains_[1], left, right);

    advance_phase_by(timestretch_source_inc_q32_);
    advance_grain(grains_[0]);
    advance_grain(grains_[1]);

    return {static_cast<int16_t>(left >> kGrainHopShift),
            static_cast<int16_t>(right >> kGrainHopShift)};
  }

  void initialize_timestretch_grains() {
    const uint64_t previous_grain_offset =
        static_cast<uint64_t>(kGrainHopSamples) * phase_inc_q32_;
    grains_[0] = {phase_q32_, phase_inc_q32_, 0};
    grains_[1] = {subtract_phase(phase_q32_, previous_grain_offset),
                  phase_inc_q32_, static_cast<uint16_t>(kGrainHopSamples)};
    grains_initialized_ = true;
  }

  void invalidate_timestretch_grains() {
    grains_initialized_ = false;
  }

  void accumulate_grain(const Grain& grain, int32_t& left, int32_t& right) const {
    const uint32_t weight = grain_window(grain.age);
    if (weight == 0) {
      return;
    }

    const uint64_t grain_phase =
        grain.start_phase_q32 +
        (static_cast<uint64_t>(grain.age) * grain.phase_inc_q32);
    const StereoFrame sample = read_interpolated_phase(grain_phase);
    left += static_cast<int32_t>(sample.left) * static_cast<int32_t>(weight);
    right += static_cast<int32_t>(sample.right) * static_cast<int32_t>(weight);
  }

  void advance_grain(Grain& grain) {
    ++grain.age;
    if (grain.age < kGrainLengthSamples) {
      return;
    }

    grain.start_phase_q32 = phase_q32_;
    grain.phase_inc_q32 = phase_inc_q32_;
    grain.age = 0;
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
    if (pulse_flash_samples_ > 0) {
      for (int i = 0; i < kNumLeds; ++i) {
        LedOn(i, true);
      }
      return;
    }

    if (external_clock_active_) {
      const uint32_t inner_segment =
          static_cast<uint32_t>((static_cast<uint64_t>(current_frame()) * 4u) /
                                BREAKY_FRAME_COUNT);
      LedOn(0, true);
      LedOn(5, true);
      for (int i = 1; i < 5; ++i) {
        LedOn(i, static_cast<uint32_t>(i - 1) == inner_segment);
      }
      return;
    }

    for (int i = 0; i < kNumLeds; ++i) {
      LedOn(i, static_cast<uint32_t>(i) == segment);
    }
  }
};

int main() {
  set_sys_clock_khz(200000, true);
  stdio_init_all();

  Breaky card;
  card.EnableNormalisationProbe();
  card.Run();
}
