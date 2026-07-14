#include "ComputerCard.h"

extern "C" {
#include "reverb_dsp.h"
}

#include "hardware/clocks.h"

extern "C" {
extern const int8_t sample_data[];
extern const int8_t sample_data_end[];
}

class FourThirtyThreeRooms : public ComputerCard
{
public:
    FourThirtyThreeRooms()
    {
        verb_ = reverb_create();
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        const StereoFrame dry = next_frame();

        if (verb_) {
            update_reverb_from_knobs();
            const int32_t mono = (static_cast<int32_t>(dry.left) + dry.right) << 1;
            reverb_process(verb_, mono);

            const int32_t wet_amount = shaped_wet_amount();
            const int32_t dry_amount = 4096 - wet_amount;
            int32_t wet_left = reverb_get_left(verb_) >> 2;
            int32_t wet_right = reverb_get_right(verb_) >> 2;

            int32_t left = (dry_amount * dry.left + wet_amount * wet_left) >> 12;
            int32_t right = (dry_amount * dry.right + wet_amount * wet_right) >> 12;

            AudioOut1(clamp12(left));
            AudioOut2(clamp12(right));
        } else {
            AudioOut1(dry.left);
            AudioOut2(dry.right);
        }

        update_leds();
    }

private:
    struct StereoFrame {
        int16_t left;
        int16_t right;
    };

    static constexpr uint32_t kSourceRate = FOUR33_SAMPLE_RATE;
    static constexpr uint32_t kOutputRate = 48000;
    static constexpr uint32_t kPhaseInc =
        static_cast<uint32_t>((static_cast<uint64_t>(kSourceRate) << 32u) / kOutputRate);
    static constexpr uint32_t kLedUpdateMask = 0x7ff;
    static constexpr uint32_t kControlUpdateMask = 0xff;

    reverb *verb_ = nullptr;
    uint64_t phase_ = 0;
    uint32_t sample_tick_ = 0;
    int32_t smoothed_size_ = 8192;
    int32_t smoothed_wet_ = 0;

    static uint32_t sample_frames()
    {
        return static_cast<uint32_t>(sample_data_end - sample_data) / 2u;
    }

    static int16_t clamp12(int32_t value)
    {
        if (value > 2047) {
            return 2047;
        }
        if (value < -2048) {
            return -2048;
        }
        return static_cast<int16_t>(value);
    }

    StereoFrame __not_in_flash_func(next_frame)()
    {
        const uint32_t frames = sample_frames();
        if (frames == 0) {
            return {0, 0};
        }

        const uint32_t frame_index = static_cast<uint32_t>(phase_ >> 32u);
        const uint32_t next = (frame_index + 1u == frames) ? 0u : frame_index + 1u;
        const int32_t frac = static_cast<int32_t>((phase_ >> 20u) & 0xfffu);
        const int32_t left_a = sample_data[frame_index * 2u];
        const int32_t right_a = sample_data[frame_index * 2u + 1u];
        const int32_t left_b = sample_data[next * 2u];
        const int32_t right_b = sample_data[next * 2u + 1u];

        const int32_t left = (left_a << 4) + (((left_b - left_a) * frac) >> 8);
        const int32_t right = (right_a << 4) + (((right_b - right_a) * frac) >> 8);

        phase_ += kPhaseInc;
        const uint64_t loop_len = static_cast<uint64_t>(frames) << 32u;
        if (phase_ >= loop_len) {
            phase_ -= loop_len;
        }

        ++sample_tick_;
        return {clamp12(left), clamp12(right)};
    }

    void __not_in_flash_func(update_reverb_from_knobs)()
    {
        if ((sample_tick_ & kControlUpdateMask) != 0) {
            return;
        }

        const int32_t x = knob_with_cv(Knob::X, CVIn1());
        const int32_t y = knob_with_cv(Knob::Y, CVIn2());

        // X moves from a tight room toward a long, blooming cathedral.
        const int32_t curved_x = (x * x) >> 12;
        const int32_t target_size = 4096 + ((curved_x * 61400) >> 12);
        smoothed_size_ += (target_size - smoothed_size_) >> 4;
        reverb_set_size(verb_, smoothed_size_);

        // Darker small rooms, brighter large spaces.
        const int32_t tilt = 18000 + ((x * 36000) >> 12);
        reverb_set_tilt(verb_, tilt);

        // Y is wet amount. The curve keeps the first half subtle and playable.
        const int32_t target_wet = (y >> 2) + ((3 * y * y) >> 14);
        smoothed_wet_ += (target_wet - smoothed_wet_) >> 4;
    }

    int32_t __not_in_flash_func(shaped_wet_amount)() const
    {
        if (smoothed_wet_ < 0) {
            return 0;
        }
        if (smoothed_wet_ > 4096) {
            return 4096;
        }
        return smoothed_wet_;
    }

    int32_t __not_in_flash_func(knob_with_cv)(Knob knob, int32_t cv)
    {
        // CV is bipolar. Adding twice its value lets +/- about 6V sweep the full control range.
        const int32_t combined = KnobVal(knob) + (cv << 1);
        if (combined < 0) {
            return 0;
        }
        if (combined > 4095) {
            return 4095;
        }
        return combined;
    }

    void __not_in_flash_func(update_leds)()
    {
        if ((sample_tick_ & kLedUpdateMask) != 0) {
            return;
        }

        const int32_t room = knob_with_cv(Knob::X, CVIn1());
        const int32_t wet = shaped_wet_amount();
        LedBrightness(0, 4095 - room);
        LedBrightness(2, room > 1365 ? 2048 : 0);
        LedBrightness(4, room > 2730 ? room : 0);
        LedBrightness(1, wet);
        LedBrightness(3, wet > 1365 ? wet : 0);
        LedBrightness(5, wet > 2730 ? wet : 0);
    }
};

int main()
{
    set_sys_clock_khz(144000, true);
    FourThirtyThreeRooms card;
    card.Run();
}
