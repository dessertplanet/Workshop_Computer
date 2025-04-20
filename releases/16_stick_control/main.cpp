#include "ComputerCard.h"
#include <algorithm>

class StickControl : public ComputerCard
{
public:
    uint32_t sharedPhase;     // Shared phase accumulator
    uint32_t phaseOffsets[6]; // Phase offsets for 6 triangle waves

    StickControl()
    {
        // Initialize shared phase
        sharedPhase = 0;

        // Generate random phase offsets
        GenerateRandomPhaseOffsets();
    }

    virtual void ProcessSample()
    {
        int32_t outputs[6]; // Array to store triangle wave values

        // Calculate triangle wave values for each phase offset
        for (int i = 0; i < 6; i++)
        {
            uint32_t phase = sharedPhase + phaseOffsets[i];
            uint32_t normalizedPhase = phase >> 17; // Scale 32-bit phase to 15 bits

            // Generate triangle wave: 0 to 2^15 to 0
            int32_t value = (normalizedPhase < 32768) ? normalizedPhase : (65535 - normalizedPhase);

            // Scale to 12-bit signed output (-2048 to 2047)
            outputs[i] = (value >> 3) - 2048;
        }

        // Output the first two triangle waves to AudioOut1 and AudioOut2
        AudioOut1(outputs[0]);
        AudioOut2(outputs[1]);

        // Increment shared phase for 440Hz triangle wave
        sharedPhase += 39370534;
    }

private:
    void GenerateRandomPhaseOffsets()
    {
        // Generate random phase offsets
        for (int i = 0; i < 6; i++)
        {
            phaseOffsets[i] = rand() % 0xFFFFFFFF; // Random 32-bit value
        }

        // Sort the offsets to ensure ascending order
        std::sort(phaseOffsets, phaseOffsets + 6);

        // Enforce minimum separation between offsets
        const uint32_t minSeparation = 0x20000000; // Minimum separation (1/8 of 2^32)
		
        for (int i = 1; i < 6; i++)
        {
            if (phaseOffsets[i] - phaseOffsets[i - 1] < minSeparation)
            {
                phaseOffsets[i] = phaseOffsets[i - 1] + minSeparation;
            }
        }

        // Wrap around the last offset if it exceeds the 32-bit range
        if (phaseOffsets[5] >= 0xFFFFFFFF)
        {
            phaseOffsets[5] -= 0xFFFFFFFF;
        }
    }

    // 32-bit random number generator
    uint32_t rand()
    {
        static uint32_t lcg_seed = 1;
        lcg_seed = 1664525 * lcg_seed + 1013904223;
        return lcg_seed;
    }
};

int main()
{
    StickControl stCtrl;
    stCtrl.EnableNormalisationProbe();
    stCtrl.Run();
}
