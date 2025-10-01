#include "ComputerCard.h"
#include <cstdint>
#include "dsp/wavefolder_int.h"

// Integer wavefolder inspired by Warps' ALGORITHM_FOLD, rewritten for RP2040
// - No floating point in the audio callback
// - Uses Q1.15 fixed-point internally
// Two-parameter behavior (warps/dsp/modulator.cc):
//   sum = x1 + x2 + 0.25 * x1 * x2;
//   sum *= (0.02 + p1);
//   sum += p2;
//   y = LUT_bipolar_fold(sum)  [here approximated with integer reflection folding]

namespace {

using namespace cc_dsp;

} // namespace

class FoldInt : public ComputerCard {
private:
    int algo_step_offset = 0;  // Offset added to knob position by Pulse 1
    int last_knob_algo = -1;   // Track knob position to detect changes
    int16_t last_output = 0;   // For octave down tracking
    bool last_sign = false;    // Track zero crossings
    int16_t octave_output = 0; // Octave down output
    int octave_counter = 0;    // Sample counter for octave division
    
    // Frequency tracking for input 1
    bool last_sign_in1 = false;
    int period_counter_in1 = 0;
    int measured_period_in1 = 48000;  // Default to 1Hz
    int32_t saw_phase_in1 = 0;
    
    // Frequency tracking for input 2
    bool last_sign_in2 = false;
    int period_counter_in2 = 0;
    int measured_period_in2 = 48000;  // Default to 1Hz
    int32_t saw_phase_in2 = 0;

public:
    virtual void ProcessSample() {
        // Read audio inputs (-2048..2047)
        int16_t a1_12 = AudioIn1();
        int16_t a2_12 = AudioIn2();

        // Frequency tracking for Input 1 via zero-crossing detection
        bool current_sign_in1 = (a1_12 >= 0);
        if (current_sign_in1 && !last_sign_in1) {
            // Rising zero crossing detected - one period complete
            if (period_counter_in1 > 10 && period_counter_in1 < 4800) {
                // Valid range: ~10Hz to 4.8kHz
                measured_period_in1 = period_counter_in1;
            }
            period_counter_in1 = 0;
        }
        period_counter_in1++;
        last_sign_in1 = current_sign_in1;
        
        // Generate sawtooth at tracked frequency for Input 1
        int32_t increment_in1 = (1 << 30) / measured_period_in1;  // Phase increment per sample
        saw_phase_in1 += increment_in1;
        if (saw_phase_in1 >= (1 << 30)) saw_phase_in1 -= (1 << 30);
        int16_t saw_out1 = static_cast<int16_t>((saw_phase_in1 >> 19) - 1024);  // -1024 to 1023
        CVOut1(saw_out1 << 1);  // Scale to -2048 to 2046
        
        // Frequency tracking for Input 2 via zero-crossing detection
        bool current_sign_in2 = (a2_12 >= 0);
        if (current_sign_in2 && !last_sign_in2) {
            // Rising zero crossing detected - one period complete
            if (period_counter_in2 > 10 && period_counter_in2 < 4800) {
                // Valid range: ~10Hz to 4.8kHz
                measured_period_in2 = period_counter_in2;
            }
            period_counter_in2 = 0;
        }
        period_counter_in2++;
        last_sign_in2 = current_sign_in2;
        
        // Generate sawtooth at tracked frequency for Input 2
        int32_t increment_in2 = (1 << 30) / measured_period_in2;  // Phase increment per sample
        saw_phase_in2 += increment_in2;
        if (saw_phase_in2 >= (1 << 30)) saw_phase_in2 -= (1 << 30);
        int16_t saw_out2 = static_cast<int16_t>((saw_phase_in2 >> 19) - 1024);  // -1024 to 1023
        CVOut2(saw_out2 << 1);  // Scale to -2048 to 2046

        // Swap inputs based on switch position
        if (SwitchVal() == Switch::Up) {
            int16_t temp = a1_12;
            a1_12 = a2_12;
            a2_12 = temp;
        }

        // Convert to Q15
        int32_t x1_q15 = audio12_to_q15(a1_12);
        int32_t x2_q15 = audio12_to_q15(a2_12);

        // Read CV inputs (-2048..2047)
        int16_t cv1 = CVIn1();
        int16_t cv2 = CVIn2();

        // Map p1 <- Knob X + CV1 offset (0..4095) to Q15 (0..~1.0)
        int32_t p1_knob = static_cast<int32_t>(KnobVal(Knob::X)) + cv1;
        if (p1_knob < 0) p1_knob = 0;
        if (p1_knob > 4095) p1_knob = 4095;
        int32_t p1_q15 = knob_to_q15(static_cast<uint16_t>(p1_knob));
        
        // Map p2 <- Knob Y + CV2 offset to unipolar Q15 (0..~1.0)
        int32_t p2_knob = static_cast<int32_t>(KnobVal(Knob::Y)) + cv2;
        if (p2_knob < 0) p2_knob = 0;
        if (p2_knob > 4095) p2_knob = 4095;
        int32_t p2_q15 = knob_to_q15(static_cast<uint16_t>(p2_knob));

        // Get algorithm from Main knob
        const int num_algos = static_cast<int>(Algorithm::Count);
        uint32_t main_knob = KnobVal(Knob::Main);
        const uint32_t segment = 4096u / static_cast<uint32_t>(num_algos ? num_algos : 1);
        int knob_algo = segment ? static_cast<int>(main_knob / segment) : 0;
        if (knob_algo >= num_algos) knob_algo = num_algos - 1;
        
        // If knob moved, reset step offset
        if (knob_algo != last_knob_algo) {
            algo_step_offset = 0;
            last_knob_algo = knob_algo;
        }
        
        // Pulse 1 rising edge: step to next algorithm
        if (PulseIn1RisingEdge()) {
            algo_step_offset++;
        }
        
        // Calculate final algorithm: knob position + step offset
        int algo_index = (knob_algo + algo_step_offset) % num_algos;
        Algorithm algo = static_cast<Algorithm>(algo_index);

        // Apply selected algorithm
        int32_t y_q15 = process_algorithm_q15(algo, x1_q15, x2_q15, p1_q15, p2_q15);

        // Convert back to 12-bit for output
        int16_t y12 = q15_to_audio12(y_q15);
        
        // Audio Out 1: processed signal
        AudioOut1(y12);
        
        // Audio Out 2: octave down using simple frequency division
        // Detect zero crossings and hold every other cycle
        bool current_sign = (y12 >= 0);
        if (current_sign != last_sign) {
            // Zero crossing detected
            octave_counter++;
            if (octave_counter >= 2) {
                // Every 2nd zero crossing, update octave output
                octave_output = last_output;
                octave_counter = 0;
            }
        }
        last_sign = current_sign;
        last_output = y12;
        AudioOut2(octave_output);

        // Visual: show p1 (X + CV1) on LED 1, p2 (Y + CV2) on LED 3
        LedBrightness(1, static_cast<uint16_t>(p1_knob));
        LedBrightness(3, static_cast<uint16_t>(p2_knob));
        
        // LED 2: show step offset (bright when stepped away from knob position)
        if (algo_step_offset > 0) {
            // Brightness increases with step count
            uint16_t brightness = (algo_step_offset * 512) % 4096;
            if (brightness > 4095) brightness = 4095;
            LedBrightness(2, brightness);
        } else {
            LedOff(2);
        }
    }
};

int main() {
    set_sys_clock_khz(225000, true);
    FoldInt app;
    app.Run();
}


