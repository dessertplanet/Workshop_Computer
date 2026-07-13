#include "ComputerCard.h"
#include "algos/ResoNoise.hpp"
#include "algos/RadioOhNo.hpp"
#include "algos/CrossModRingSquare.hpp"
#include "algos/CrossModRingSine.hpp"
#include "algos/ClusterSaw.hpp"
#include "algos/Atari.hpp"
#include "algos/Basurilla.hpp"
#include "algos/ArrayOnTheRocks.hpp"
#include "algos/RwalkModWave.hpp"
#include "algos/PwCluster.hpp"
#include "algos/ExistencelsPain.hpp"
#include "algos/BasuraTotal.hpp"
#include "algos/S_H.hpp"
#include "algos/SatanWorkout.hpp"
#include "algos/WhoKnows.hpp"

// Noise synthesis algorithms with CV control.
// - Main knob: algorithm selection (7 algorithms: ResoNoise, RadioOhNo, 
//   CrossModRingSquare, CrossModRingSine, ClusterSaw, ExistencesPain, Atari)
// - CV1 input: X parameter control for selected algorithm  
// - CV2 input: Y parameter control for selected algorithm

class NoiseDemo : public ComputerCard
{
public:
    NoiseDemo()
        : sampleHoldCounter(0), sampleHoldPeriod(8), heldSample(0), bitReductionShift(6)
        , cv2_current_q16(0)
        , cv2_target_q16(0)
        , cv2_step_q16(0)
        , cv2_slew_samples_left(0)
        , last_cv1_value(0)
        , have_prev_cv1(false)
        , samples_since_last_pulse(0)
        , kMain_offset(0)
        , kX_offset(0)
        , kY_offset(0)
        , switch_down_samples(0)
        , hold_reset_applied(false)
        , prev_switch_state(Switch::Middle)
        , rng_state(0xA5F1523Du)
    {
        // Freeverb removed from main.
        // Warm up all algos so internal states (e.g., reverbs/filters) settle.
        warmupAllAlgos(1024); // ~125 ms at 48 kHz
    }
    virtual void ProcessSample()
    {
        // Read controls
        int32_t main_knob_value_0_to_4095 = KnobVal(Knob::Main);
        
        // Read CV inputs (-2048 to 2047)
        int16_t cv1_raw = CVIn1();
        int16_t cv2_raw = CVIn2();

        // Handle Z switch edge/hold for randomizing/resetting knob offsets
        Switch sw_now = SwitchVal();
        if (sw_now == Switch::Down)
        {
            if (prev_switch_state != Switch::Down)
            {
                // Edge to Down: randomize offsets in 0..4095
                kMain_offset = static_cast<int32_t>(nextRand4096());
                kX_offset    = static_cast<int32_t>(nextRand4096());
                kY_offset    = static_cast<int32_t>(nextRand4096());
                switch_down_samples = 0;
                hold_reset_applied = false;
            }
            else
            {
                // Held: count samples and reset if held long enough
                if (!hold_reset_applied)
                {
                    if (++switch_down_samples >= HOLD_RESET_SAMPLES)
                    {
                        kMain_offset = 0;
                        kX_offset = 0;
                        kY_offset = 0;
                        hold_reset_applied = true;
                    }
                }
            }
        }
        else
        {
            switch_down_samples = 0;
            hold_reset_applied = false;
        }
        prev_switch_state = sw_now;
        
        // Sum CV with X/Y knobs and per-session offsets; wrap to 0..4095 (modulo)
        auto wrap4096 = [](int32_t v){ v %= 4096; if (v < 0) v += 4096; return static_cast<uint16_t>(v); };
        uint16_t kX = wrap4096(static_cast<int32_t>(cv1_raw) + KnobVal(Knob::X) + kX_offset);
        uint16_t kY = wrap4096(static_cast<int32_t>(cv2_raw) + KnobVal(Knob::Y) + kY_offset);

        // Also allow CV offset of Main knob, with wrap-around (0..4095)
        // Positive CV beyond max wraps back around
        int32_t kMain_wrapped = (main_knob_value_0_to_4095 + static_cast<int32_t>(AudioIn1()) + kMain_offset) % 4096;
        if (kMain_wrapped < 0) kMain_wrapped += 4096;

        // Dynamically select algorithm based on number of algos and knob position
        // Order: ResoNoise, RadioOhNo, CrossModRingSquare, CrossModRingSine, ClusterSaw, Basurilla, PwCluster, ArrayOnTheRocks, RwalkModWave, Atari, ExistencelsPain, BasuraTotal

        // List of algorithm lambdas for dynamic dispatch
        constexpr int num_algos = 13;
        int16_t s = 0;
        int algo_index = (kMain_wrapped * num_algos) / 4096;
        if (algo_index < 0) algo_index = 0;
        if (algo_index >= num_algos) algo_index = num_algos - 1;

        switch (algo_index)
        {
            case 0:
                s = reso.nextSample(kX, kY);
                break;
            case 1:
                s = radio.nextSample(kX, kY);
                break;
            case 2:
                s = static_cast<int16_t>(xmodring.process(kX, kY));
                break;
            case 3:
                s = static_cast<int16_t>(xmodringsine.process(kX, kY));
                break;
            case 4:
                s = static_cast<int16_t>(clustersaw.process(kX, kY));
                break;
            case 5:
                s = static_cast<int16_t>(basurilla.process(kX, kY));
                break;
            case 6:
                s = static_cast<int16_t>(pwcluster.process(kX, kY));
                break;
            case 7:
                s = static_cast<int16_t>(arrayrocks.process(kX, kY));
                break;
            case 8:
                s = static_cast<int16_t>(atari.process(kX, kY));
                break;
            case 9:
                s = static_cast<int16_t>(satanworkout.process(kX, kY));
                break;
            case 10:
                s = samplehold.nextSample(kX, kY);  
                break;
            case 11:
                s = static_cast<int16_t>(basuratotal.process(kX, kY));
                break;
            default:
                s = static_cast<int16_t>(existencels.process(kX, kY));
                break;
        }

        int32_t vca_0_to_4095 = Connected(Input::Audio2) ? (AudioIn2() + 2048) : 4095;
        if (vca_0_to_4095 < 0) vca_0_to_4095 = 0;
        if (vca_0_to_4095 > 4095) vca_0_to_4095 = 4095;
        s = static_cast<int16_t>((static_cast<int32_t>(s) * vca_0_to_4095) >> 12);

        // Engage bit/sample rate reducer when the Z switch is Up, or when PulseIn2 gate is high
        LedOn(1, PulseIn2());
        if (SwitchVal() == Switch::Up || PulseIn2())
        {
            // Sample rate reduction via sample-and-hold
            if (sampleHoldCounter == 0)
            {
                heldSample = s;
            }
            s = heldSample;
            sampleHoldCounter++;
            if (sampleHoldCounter >= sampleHoldPeriod) sampleHoldCounter = 0;

            int32_t su = static_cast<int32_t>(s) + 2048; // map to 0..4095
            if (su < 0) su = 0; else if (su > 4095) su = 4095;
            su = (su >> bitReductionShift) << bitReductionShift;
            s = static_cast<int16_t>(su - 2048);
        }

        // Advance pulse sample counter each sample
        samples_since_last_pulse++;

        // On a rising edge at PulseIn1, sample-and-hold current audio sample 's' to CV Out 1
        if (PulseIn1RisingEdge())
        {
            // Output CV1 immediately with the sampled value
            CVOut1(s);
            PulseOut1(s > 0);
            
            // Measure period naively from last pulse
            uint32_t measured = samples_since_last_pulse;
            samples_since_last_pulse = 0;
            if (measured == 0) measured = 1; // avoid divide-by-zero
            uint32_t period_for_slew = measured;

            // Set up the slew between the last and current CVOut1 values (last pulse -> this pulse)
            int16_t start_val = have_prev_cv1 ? last_cv1_value : s;
            int32_t target_val_q16 = static_cast<int32_t>(s) << 16;
            // If it's the first pulse, initialise CV2 to the same as CV1 and do not slew
            if (!have_prev_cv1)
            {
                cv2_current_q16 = target_val_q16;
                cv2_target_q16 = target_val_q16;
                cv2_step_q16 = 0;
                cv2_slew_samples_left = 0;
                have_prev_cv1 = true;
            }
            else
            {
                // Start new ramp at the previous CV1 value
                cv2_current_q16 = static_cast<int32_t>(start_val) << 16;
                cv2_target_q16 = target_val_q16;
                cv2_slew_samples_left = period_for_slew;
                if (cv2_slew_samples_left == 0)
                {
                    cv2_step_q16 = 0;
                }
                else
                {
                    cv2_step_q16 = (cv2_target_q16 - cv2_current_q16) / static_cast<int32_t>(cv2_slew_samples_left);
                }
            }

            // Update last CV1 value for next interval
            last_cv1_value = s;
        }

        // Progress CV2 slew each sample and output
        if (cv2_slew_samples_left > 0)
        {
            cv2_current_q16 += cv2_step_q16;
            cv2_slew_samples_left--;
            if (cv2_slew_samples_left == 0)
            {
                cv2_current_q16 = cv2_target_q16;
            }
        }
        int16_t cv2_out_i16 = static_cast<int16_t>(cv2_current_q16 >> 16);
        CVOut2(cv2_out_i16);

        // Drive pulse outs from current audio polarity
        PulseOut2(s > 0);

        AudioOut1(s);
        AudioOut2(s);

        // Minimal visual feedback
        for (int i = 0; i < 6; ++i) LedOff(i);
        LedOn(0, true);

    }

private:
    // Hold reset after 2.5 seconds at 48kHz
    static constexpr uint32_t HOLD_RESET_SAMPLES = 120000; // 2.5s * 48k
    // Minimal guard to avoid zero-length ramps
    static constexpr uint32_t MIN_PERIOD_SAMPLES = 1;

    // Run each algorithm for a number of samples to allow internal DSP to settle
    inline void warmupAllAlgos(int samplesPerAlgo)
    {
        if (samplesPerAlgo <= 0) return;
        const uint16_t x_q12 = 2048;
        const uint16_t y_q12 = 2048;

        // Helpers to avoid code repetition
        auto clamp4095 = [](int v){ if(v<0) return 0; if(v>4095) return 4095; return v; };
        const int kx = clamp4095(2048);
        const int ky = clamp4095(2048);

        for (int i = 0; i < samplesPerAlgo; ++i) reso.nextSample(x_q12, y_q12);
        for (int i = 0; i < samplesPerAlgo; ++i) radio.nextSample(x_q12, y_q12);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)xmodring.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)xmodringsine.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)clustersaw.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)basurilla.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)pwcluster.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)arrayrocks.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)atari.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)basuratotal.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)samplehold.nextSample(x_q12, y_q12);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)satanworkout.process(kx, ky);
        for (int i = 0; i < samplesPerAlgo; ++i) (void)existencels.process(kx, ky);
    }

    ResoNoiseAlgo reso;
    RadioOhNoAlgo radio;
    CrossModRingSquare xmodring;
    CrossModRingSine xmodringsine;
    ClusterSaw clustersaw;
    Basurilla basurilla;
    PwCluster pwcluster;
    ArrayOnTheRocks arrayrocks;
    Atari atari;
    ExistencelsPain existencels;
    BasuraTotalAlgo basuratotal;
    SampleHoldReverbAlgo samplehold;
    SatanWorkoutAlgo satanworkout;

    // Crusher state
    int sampleHoldCounter;
    int sampleHoldPeriod;      // e.g., 8 -> 48k/8 = 6kHz effective
    int16_t heldSample;
    uint8_t bitReductionShift; // 4 -> 12-4 = 8-bit effective

    // CV2 slew state
    int32_t cv2_current_q16;
    int32_t cv2_target_q16;
    int32_t cv2_step_q16;
    uint32_t cv2_slew_samples_left;
    int16_t last_cv1_value;
    bool have_prev_cv1;
    uint32_t samples_since_last_pulse;
    
    // Randomized knob offsets (0..4095)
    int32_t kMain_offset;
    int32_t kX_offset;
    int32_t kY_offset;
    
    // Switch hold detection
    uint32_t switch_down_samples;
    bool hold_reset_applied;
    Switch prev_switch_state;
    
    // Simple LCG RNG for offsets
    uint32_t rng_state;
    inline uint16_t nextRand4096()
    {
        // LCG parameters (Numerical Recipes)
        rng_state = 1664525u * rng_state + 1013904223u;
        return static_cast<uint16_t>((rng_state >> 16) & 0x0FFF); // 0..4095
    }
    
};

int main()
{ 
	set_sys_clock_khz(225000, true);
    NoiseDemo demo;
    // Enable jack-detection (normalisation probe) so Connected/Disconnected works
    demo.EnableNormalisationProbe();
    demo.Run();
}
