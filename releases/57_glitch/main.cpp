// Glitch — clock-synced beat-repeater for the Workshop Computer
// Inspired by the Phazerville/O&C Glitch applet by Andy Jenkinson
//
// Controls:
//   Pulse In 1          : Clock input (rising edge = new beat)
//   CV In 1 > ~0V       : Freeze (stops recording, keeps looping)
//   Pulse In 2          : External gate (used in Switch MID mode)
//   Main Knob           : 5 zones → ratchet division {1,2,3,4,6}
//                         Remainder within zone → reverse probability threshold
//   Knob X              : Degradation amount (bitcrush + decimation depth)
//   Knob Y              : Degradation probability (how often degradation applies)
//   Switch UP (latch)   : Probabilistic mode — glitch if rng < prob_thresh
//   Switch MID          : External gate mode — glitch while Pulse In 2 HIGH
//   Switch DOWN (moment): Force mode — always glitch
//   Audio In 1          : Input signal
//   Audio Out 1 + 2     : Output (same signal on both channels)
//
// LEDs:
//   LED 0..4   : One lit to show current ratchet zone (left col + top-right)
//   LED 5      : Brightness = reverse probability remainder
//
// Integer math only — no floats anywhere.

#include "ComputerCard.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

// 0.5 s at 48kHz = 24000 samples = 48KB. Safe for RP2040.
static constexpr int32_t MAX_BUFFER_SIZE = 24000;

static int16_t  g_buf[MAX_BUFFER_SIZE];  // circular audio buffer
static int32_t  g_write_pos = 0;         // next write position

// ---------------------------------------------------------------------------
// RNG — 32-bit LCG, use high bits only
// ---------------------------------------------------------------------------

static uint32_t g_rng = 1;
static inline uint32_t __not_in_flash_func(rng_next)() {
    g_rng = 1664525u * g_rng + 1013904223u;
    return g_rng;
}

// ---------------------------------------------------------------------------
// Micro-fade — 64-sample linear ramp at loop boundaries to suppress clicks.
//
// amp ranges 0..63; scale factor is >>6 (divide by 64).
// Skipped for very short slices (<128 samples) where ramps would overlap.
// ---------------------------------------------------------------------------

static inline int16_t __not_in_flash_func(apply_fade)(int16_t sample,
                                                       int32_t pos,
                                                       int32_t len)
{
    if (len < 128) return sample;
    int32_t amp;
    if (pos < 64)
        amp = pos;
    else if (pos >= len - 64)
        amp = (len - pos) - 1;
    else
        amp = 63;
    return (int16_t)((sample * amp) >> 6);
}

// ---------------------------------------------------------------------------
// Glitch card
// ---------------------------------------------------------------------------

class Glitch : public ComputerCard
{
    // --- clock tracking ---
    int32_t  clock_counter    = 0;  // samples since last clock rising edge
    int32_t  master_loop_len  = 0;  // measured beat length (capped at MAX_BUFFER_SIZE)

    // --- slice state ---
    int32_t  slice_start      = 0;  // absolute buffer index where slice began
    int32_t  slice_len        = 1;  // length of one ratchet sub-slice in samples
    int32_t  slice_pos        = 0;  // playback position within current slice

    // --- glitch state ---
    bool     do_glitch        = false;  // held for full slice duration
    bool     reverse_flag     = false;  // held for full slice duration
    bool     degrade_active   = false;  // held for full slice duration

    // --- frozen state ---
    bool     frozen           = false;

public:
    Glitch() {
        memset(g_buf, 0, sizeof(g_buf));
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        // ----------------------------------------------------------------
        // 1. Read raw knob values
        // ----------------------------------------------------------------
        const int32_t knob_main = KnobVal(Knob::Main);  // 0..4095
        const int32_t knob_x    = KnobVal(Knob::X);     // 0..4095
        const int32_t knob_y    = KnobVal(Knob::Y);     // 0..4095

        // ----------------------------------------------------------------
        // 2. Big Knob zone decode
        //    5 zones of width 819. Zone selects ratchet; remainder is
        //    the hidden reverse-probability threshold (scaled 0..4095).
        // ----------------------------------------------------------------
        static constexpr int32_t kRatchetDivs[5] = {1, 2, 3, 4, 6};
        static constexpr int32_t kZoneWidth = 819;  // floor(4095/5)

        int32_t zone = knob_main / kZoneWidth;
        if (zone > 4) zone = 4;
        const int32_t remainder    = knob_main % kZoneWidth;
        const int32_t prob_thresh  = (remainder * 4095) / 818;  // 0..4095
        const int32_t ratchet      = kRatchetDivs[zone];

        // ----------------------------------------------------------------
        // 3. Degradation parameters from Knob X
        //    crush_bits 0..7  — how many LSBs to mask (0 = bypass)
        //    dec_factor 1..16 — sample-hold step size  (1 = bypass)
        // ----------------------------------------------------------------
        const int32_t crush_bits = knob_x >> 9;          // 0..7
        const int32_t dec_factor = 1 + (knob_x >> 8);   // 1..16

        // ----------------------------------------------------------------
        // 4. Freeze: CV In 1 as comparator (>2047 ≈ above 0V)
        // ----------------------------------------------------------------
        frozen = (CVIn1() > 2047);

        // ----------------------------------------------------------------
        // 5. Clock edge — measure beat, compute slice geometry
        // ----------------------------------------------------------------
        clock_counter++;

        if (PulseIn1RisingEdge()) {
            // Measure and cap
            master_loop_len = clock_counter;
            if (master_loop_len > MAX_BUFFER_SIZE)
                master_loop_len = MAX_BUFFER_SIZE;
            clock_counter = 0;

            // Slice length for this ratchet setting.
            // Divide only here (once per beat), never in the per-sample hot path.
            slice_len = master_loop_len / ratchet;
            if (slice_len < 1) slice_len = 1;

            // Slice start: reach back master_loop_len samples from current write pos.
            // +MAX_BUFFER_SIZE before modulo ensures no negative result.
            slice_start = (g_write_pos - master_loop_len + MAX_BUFFER_SIZE)
                          % MAX_BUFFER_SIZE;

            // Decide glitch and reverse for the first sub-slice of this beat.
            slice_pos   = 0;
            do_glitch   = eval_glitch(prob_thresh, knob_y);
            reverse_flag = ((rng_next() >> 20) < (uint32_t)prob_thresh);
            degrade_active = eval_degrade(knob_y);
        }

        // ----------------------------------------------------------------
        // 6. Write to buffer (unless frozen)
        // ----------------------------------------------------------------
        if (!frozen) {
            g_buf[g_write_pos] = AudioIn1();
            g_write_pos = (g_write_pos + 1) % MAX_BUFFER_SIZE;
        }

        // ----------------------------------------------------------------
        // 7. Advance slice position; re-roll at sub-slice boundary
        // ----------------------------------------------------------------
        if (master_loop_len > 0) {
            slice_pos++;
            if (slice_pos >= slice_len) {
                slice_pos    = 0;
                do_glitch    = eval_glitch(prob_thresh, knob_y);
                reverse_flag = ((rng_next() >> 20) < (uint32_t)prob_thresh);
                degrade_active = eval_degrade(knob_y);
            }
        }

        // ----------------------------------------------------------------
        // 8. Compute output sample
        // ----------------------------------------------------------------
        int16_t out;

        if (do_glitch && master_loop_len > 0) {
            // -- Compute effective read position (decimation) --
            int32_t eff_p = slice_pos;
            if (dec_factor > 1 && degrade_active)
                eff_p = (eff_p / dec_factor) * dec_factor;

            // -- Reverse --
            if (reverse_flag)
                eff_p = (slice_len - 1) - eff_p;
            if (eff_p < 0) eff_p = 0;

            // -- Absolute read pointer into circular buffer --
            const int32_t read_ptr = (slice_start + eff_p) % MAX_BUFFER_SIZE;
            int16_t sample = g_buf[read_ptr];

            // -- Bitcrush --
            // (sample >> bits) << bits zeroes the low 'bits' bits.
            // e.g. crush_bits=4 reduces 12-bit audio to 8-bit resolution.
            if (crush_bits > 0 && degrade_active)
                sample = (int16_t)((sample >> crush_bits) << crush_bits);

            // -- Click suppression at loop boundaries --
            sample = apply_fade(sample, slice_pos, slice_len);

            out = sample;
        } else {
            // Pass-through: not glitching, output live input directly
            out = AudioIn1();
        }

        // ----------------------------------------------------------------
        // 9. Audio output — same signal on both channels
        // ----------------------------------------------------------------
        AudioOut1(out);
        AudioOut2(out);

        // ----------------------------------------------------------------
        // 10. LEDs
        //     LEDs 0..4: one lit per zone (top-left, mid-left, bot-left, top-right, mid-right)
        //     LED 5 (bot-right): brightness = prob_thresh (reverse probability)
        // ----------------------------------------------------------------
        for (int i = 0; i < 5; i++)
            LedOn(i, i == zone);
        LedBrightness(5, (uint16_t)prob_thresh);
    }

private:
    // Evaluate whether to glitch this slice, based on current switch state.
    // Called once per slice boundary — NOT every sample.
    bool __not_in_flash_func(eval_glitch)(int32_t prob_thresh, int32_t /*knob_y*/) {
        switch (SwitchVal()) {
            case Switch::Up:
                // Probabilistic: roll 12-bit random, compare to threshold
                return ((rng_next() >> 20) < (uint32_t)prob_thresh);
            case Switch::Middle:
                // External gate: Pulse In 2 HIGH = glitch
                return PulseIn2();
            case Switch::Down:
                // Force: always glitch
                return true;
            default:
                return false;
        }
    }

    // Evaluate whether degradation (bitcrush/decimate) applies this slice.
    // Knob Y sets the probability threshold (0 = never, 4095 = always).
    bool __not_in_flash_func(eval_degrade)(int32_t knob_y) {
        // rng_next() >> 20 is a 12-bit value (0..4095)
        // degrade if random < knob_y
        return ((rng_next() >> 20) < (uint32_t)knob_y);
    }
};

// ---------------------------------------------------------------------------
// Global instance — must be global (not in main) to keep large buffer out of
// the 4KB core-0 stack. See ComputerCard NOTES.md §Heap, stack and overflow.
// ---------------------------------------------------------------------------
Glitch glitch_card;

int main()
{
    // 144MHz = 3 × 48kHz — alias-free CV PWM (ComputerCard 0.3.x requirement)
    set_sys_clock_khz(144000, true);
    glitch_card.Run();
}
