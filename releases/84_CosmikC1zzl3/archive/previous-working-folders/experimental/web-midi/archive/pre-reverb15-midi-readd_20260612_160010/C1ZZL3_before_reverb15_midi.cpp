#include "ComputerCard.h"
#include "C1ZZL3_LUT.h"
#include "hardware/sync.h"

class C1ZZL3 : public ComputerCard
{
public:

    C1ZZL3()
    {
        phase1 = 0;
        phase2 = 0;

        noise = 1;

        loadPerformanceState();
    }

    // =========================================================
    // AUDIO CALLBACK
    // =========================================================
    void ProcessSample() override
    {
        int32_t in1 = AudioIn1();
        int32_t in2 = AudioIn2();

        int32_t cv1 = CVIn1();
        int32_t cv2 = CVIn2();

        Switch mode = SwitchVal();

        int32_t main = KnobVal(Knob::Main);
        int32_t x    = KnobVal(Knob::X);
        int32_t y    = KnobVal(Knob::Y);

        Switch previousMode = lastMode;

        updateStartupEnvelopeSelect(mode);

        if (envelopeSelectMode)
        {
            updateEnvelopeSelectMode(main, mode, previousMode);
            lastMode = mode;
            return;
        }

        if (previousMode == Switch::Up && mode == Switch::Down)
            tapTuringClock();
        lastMode = mode;

        // =========================
        // PD SYNTH MODE (MID + DOWN)
        // =========================
        if (mode != Switch::Up)
        {
            bool alt = (mode == Switch::Down);

            if (!alt)
            {
                pitchControl = main;
                pdControl = x;
                waveControl = y;
            }
            else
            {
                if (previousMode != Switch::Down)
                {
                    saveHoldCanSave = (previousMode == Switch::Middle);
                    resetAltPickup(main, x, y);
                }

                updateAltControls(main, x, y);
            }

            // -------------------------
            // PITCH (octave map with hardware-tested 1V/oct input scale)
            // -------------------------
            int32_t freq = smoothPitch(pitchFrequency(pitchUnits(pitchControl, in1)));

            int32_t pd = clamp12(pdControl + (in2 << 1));
            int32_t wave = clamp12(waveControl + (cv1 << 1));

            int32_t ring =
                clamp12(osc2Ring + (alt && cv2 > 0 ? cv2 << 1 : 0));
            int32_t noiseAmt =
                clamp12(osc2Noise + (alt && cv2 < 0 ? (-cv2) << 1 : 0));

            bool pulse2Trigger = PulseIn2RisingEdge();
            if (pulse2Trigger)
            {
                if (envelopePreset != (uint8_t)EnvelopePreset::Off)
                {
                    syncOscillators();
                    triggerEnvelope();
                }
            }

            outputSynthVoice(freq, pd, wave, ring, noiseAmt);

            CVOut1(turingCv);
            CVOut2(turingModCv);
            PulseOut1(turingPulse);
            PulseOut2(turingAltPulse);

            updateSynthLEDs(alt, pd, wave);
            updateSaveGesture(alt);
        }

        // =========================
        // TURING MODE
        // =========================
        else
        {
            resetSaveGesture();

            turingLength = 2 + ((x * 14) >> 12);
            if (turingLength > 16) turingLength = 16;

            bool externalClock = PulseIn1RisingEdge();
            bool clocked = false;

            if (externalClock)
            {
                externalClockAge = 0;
                clocked = true;
            }
            else
            {
                if (externalClockAge < 96000u)
                    externalClockAge++;
                else
                    clocked = internalTuringClock(y);
            }

            if (clocked)
            {
                stepTuring(main);
                triggerTuringEnvelope();
            }

            updateTuringPulseAge();

            CVOut1(turingCv);
            CVOut2(turingModCv);

            outputTuringSynthVoice();

            PulseOut1(turingPulse);
            PulseOut2(turingAltPulse);

            updateTuringLEDs();
        }
    }

private:
    enum class EnvelopePreset : uint8_t
    {
        Off,
        Pluck,
        DoublePluck,
        Bounce,
        Bell,
        Brass,
        Strings,
        ReverseSwell,
        EvolvingDigital
    };

    struct EnvelopeStage
    {
        uint16_t level;
        uint32_t time;
    };

    struct EnvelopeProgram
    {
        EnvelopeStage amp[8];
        EnvelopeStage pd[8];
    };

    static constexpr int32_t TuringCvScale = 3072;
    static constexpr int32_t TuringCvOffset = 512;
    static constexpr int32_t TuringAudioPitchDepth = 2048;
    static constexpr uint32_t TuringClockLedSamples = 1200u;
    static constexpr int32_t PitchUnitsPerOctave = 4096;
    static constexpr int32_t MainPitchOctaves = 5;
    static constexpr int32_t PitchInputCountsPerVolt = 341;
    static constexpr int32_t MinPitchUnits = -2 * PitchUnitsPerOctave;
    static constexpr int32_t MaxPitchUnits = 7 * PitchUnitsPerOctave;
    static constexpr uint32_t C2PhaseIncrement = 5852465u;
    static constexpr uint8_t EnvelopePresetCount = 9;
    static constexpr uint32_t StartupSelectDelaySamples = 12000u;
    static constexpr uint32_t StartupSelectWindowSamples = 24000u;
    static constexpr uint32_t SaveMagic = 0x43315A33u; // C1Z3
    static constexpr uint16_t SaveVersion = 2;
    static constexpr uint32_t SaveFlashOffset =
        (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) &
        ~(FLASH_SECTOR_SIZE - 1u);
    static constexpr uint32_t SaveHoldSamples = 384000u;
    static constexpr uint32_t SaveConfirmSamples = 48000u;

    struct SavedPerformanceState
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        int32_t osc2Detune;
        int32_t osc2Level;
        int32_t osc2Ring;
        int32_t osc2Noise;
        uint8_t envelopePreset;
        uint8_t reserved[3];
        uint32_t checksum;
    };

    // =========================================================
    // CZ OSCILLATOR
    // =========================================================

    inline int32_t getSine(uint32_t phase)
    {
        // 1024-entry full-cycle sine table.
        // Top 10 bits select the table index.
        // Next 10 bits interpolate between adjacent samples.
        uint32_t index = (phase >> 22) & 1023;
        uint32_t frac  = (phase >> 12) & 1023;

        int32_t a = sineLUT[index];
        int32_t b = sineLUT[(index + 1) & 1023];

        return a + (((b - a) * (int32_t)frac) >> 10);
    }
    
    inline uint16_t getCosWarp(uint32_t index)
    {
        return phaseWarpLUT[index & 1023];
    }
    
    
    // ---------------------------------------------------------
    // CZ-style smooth breakpoint warp
    // ---------------------------------------------------------
    inline uint32_t czPhaseWarp(uint32_t phase, uint32_t amount)
    {
        uint32_t p = (phase >> 20) & 4095;

        int32_t bp =
            2048 +
            (((int32_t)amount - 2048) >> 1);

        if (bp < 256) bp = 256;
        if (bp > 3840) bp = 3840;

        if (p < (uint32_t)bp)
        {
            uint32_t t = (p << 10) / bp;

            uint32_t c = getCosWarp(t);

            return (c * bp) >> 11;
        }
        else
        {
            uint32_t t =
                ((p - bp) << 10) /
                (4095 - bp);

            uint32_t c = getCosWarp(t);

            return bp +
                (((2047 - c) *
                 (4095 - bp)) >> 11);
        }
    }

    // ---------------------------------------------------------
    // OSCILLATOR
    // ---------------------------------------------------------
    void outputSynthVoice()
    {
        int32_t freq = smoothPitch(pitchFrequency(pitchUnits(pitchControl, 0)));
        int32_t pd = clamp12(pdControl);
        int32_t wave = clamp12(waveControl);

        outputSynthVoice(freq, pd, wave, osc2Ring, osc2Noise);
    }

    void outputTuringSynthVoice()
    {
        int32_t pitchOffset =
            (turingCv * TuringAudioPitchDepth * MainPitchOctaves) >> 12;
        int32_t freq = smoothPitch(
            pitchFrequency(pitchUnits(pitchControl, 0) + pitchOffset));
        int32_t pd = clamp12(pdControl);
        int32_t wave = clamp12(waveControl);

        outputSynthVoice(freq, pd, wave, osc2Ring, osc2Noise);
    }

    void outputSynthVoice(
        int32_t freq,
        int32_t pd,
        int32_t wave,
        int32_t ring,
        int32_t noiseAmt)
    {
        int32_t envelopeLevel = updateEnvelope();
        pd = applyEnvelopeToPd(pd, envelopeLevel);

        int32_t osc1 =
            oscCZ(phase1, freq, pd, wave, noiseAmt);

        int32_t freq2 =
            applyDetune(freq, osc2Detune);

        int32_t osc2 =
            oscCZ(phase2, freq2, pd, wave, noiseAmt);

        int32_t osc2Raw = osc2;
        osc2 = (osc2Raw * osc2Level) >> 12;

        int32_t ringDrive = osc2Level;
        if (ringDrive < 2048)
            ringDrive = 2048;
        int32_t ringCarrier = (osc2Raw * ringDrive) >> 12;
        int32_t ringSig = clip((osc1 * ringCarrier) >> 10);
        int32_t ringMix = (ring * 3840) >> 12;
        osc1 = mix(osc1, ringSig, ringMix);

        int32_t ampScale = envelopeAmpScale(envelopeLevel);
        ampScale = (ampScale * updateSyncFade()) >> 12;
        osc1 = (osc1 * ampScale) >> 12;
        osc2 = (osc2 * ampScale) >> 12;

        AudioOut1(clip(osc1));
        AudioOut2(clip(osc2));
    }

    inline int32_t oscCZ(
        uint32_t& phase,
        int32_t freq,
        int32_t pd,
        int32_t wave,
        int32_t noiseAmt)
    {
        phase += (uint32_t)freq;

        uint32_t renderPhase = phase;
        int32_t noisyPd = pd;

        if (noiseAmt > 0)
            applyCZNoise(renderPhase, noisyPd, noiseAmt);

        int32_t pdCurve = responseCurve(noisyPd);

        int32_t sine = getSine(renderPhase);
        int32_t target = morphWave(renderPhase, wave);

        return mix(sine, target, pdCurve);
    }

    inline void applyCZNoise(uint32_t& phase, int32_t& pd, int32_t amount)
    {
        if (++noiseHoldCounter >= 512)
        {
            noiseHoldCounter = 0;
            heldPdNoise = ((int32_t)fastNoise() - 128);
            heldPhaseNoise = ((int32_t)fastNoise() - 128);
        }

        int32_t noiseCurve = (responseCurve(amount) * 11) / 20;
        int32_t pdJitter = (heldPdNoise * noiseCurve) >> 10;
        int32_t phaseJitter = heldPhaseNoise * (noiseCurve >> 6);

        pd = clamp12(pd + pdJitter);
        phase += (uint32_t)phaseJitter;
    }

    void triggerEnvelope()
    {
        if (envelopePreset == (uint8_t)EnvelopePreset::Off)
            return;

        ampEnvelopeStage = 0;
        ampEnvelopeSample = 0;
        ampEnvelopeLevel = 0;
        ampEnvelopeStartLevel = 0;
        pdEnvelopeStage = 0;
        pdEnvelopeSample = 0;
        pdEnvelopeLevel = 0;
        pdEnvelopeStartLevel = 0;
        envelopeActive = true;
    }

    void triggerTuringEnvelope()
    {
        if (turingPulse)
            triggerEnvelope();
    }

    int32_t updateEnvelope()
    {
        if (envelopePreset == (uint8_t)EnvelopePreset::Off || !envelopeActive)
            return 0;

        EnvelopeProgram program = envelopeProgram();

        bool ampDone = updateEnvelopeRunner(
            program.amp,
            ampEnvelopeStage,
            ampEnvelopeSample,
            ampEnvelopeLevel,
            ampEnvelopeStartLevel);

        bool pdDone = updateEnvelopeRunner(
            program.pd,
            pdEnvelopeStage,
            pdEnvelopeSample,
            pdEnvelopeLevel,
            pdEnvelopeStartLevel);

        if (ampDone && pdDone)
            envelopeActive = false;

        return clamp12(ampEnvelopeLevel);
    }

    int32_t applyEnvelopeToPd(int32_t pd, int32_t level)
    {
        (void)level;
        return clamp12(pd + pdEnvelopeLevel);
    }

    int32_t envelopeAmpScale(int32_t level)
    {
        if (envelopePreset == (uint8_t)EnvelopePreset::Off)
            return 4095;

        return clamp12(level);
    }

    EnvelopeProgram envelopeProgram()
    {
        switch ((EnvelopePreset)envelopePreset)
        {
            case EnvelopePreset::Pluck:
                return {{
                    {4095, 480}, {0, 12000}, {0, 1}, {0, 1},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }, {
                    {1024, 480}, {0, 12000}, {0, 1}, {0, 1},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }};
            case EnvelopePreset::DoublePluck:
                return {{
                    {4095, 180}, {700, 4800}, {0, 2400}, {3600, 180},
                    {900, 6000}, {350, 6000}, {120, 6000}, {0, 12000}
                }, {
                    {1600, 180}, {500, 4800}, {0, 2400}, {2200, 180},
                    {700, 6000}, {300, 6000}, {120, 6000}, {0, 12000}
                }};
            case EnvelopePreset::Bounce:
                return {{
                    {4095, 120}, {1200, 3600}, {3300, 3600}, {1700, 4800},
                    {2600, 4800}, {900, 7200}, {1600, 7200}, {0, 12000}
                }, {
                    {2500, 120}, {800, 3600}, {2200, 3600}, {700, 4800},
                    {1600, 4800}, {500, 7200}, {1200, 7200}, {0, 12000}
                }};
            case EnvelopePreset::Bell:
                return {{
                    {4095, 240}, {2600, 12000}, {1200, 24000}, {0, 36000},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }, {
                    {2048, 240}, {1600, 6000}, {700, 24000}, {0, 42000},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }};
            case EnvelopePreset::Brass:
                return {{
                    {4095, 4800}, {3400, 30000}, {0, 18000}, {0, 1},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }, {
                    {1792, 4800}, {900, 30000}, {0, 18000}, {0, 1},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }};
            case EnvelopePreset::Strings:
                return {{
                    {2200, 24000}, {3600, 24000}, {3800, 48000}, {3600, 48000},
                    {3400, 48000}, {3000, 48000}, {1800, 48000}, {0, 96000}
                }, {
                    {400, 12000}, {900, 36000}, {1200, 36000}, {900, 48000},
                    {700, 48000}, {500, 48000}, {400, 48000}, {300, 96000}
                }};
            case EnvelopePreset::ReverseSwell:
                return {{
                    {200, 12000}, {900, 18000}, {1800, 18000}, {3000, 18000},
                    {4095, 12000}, {2600, 2400}, {900, 2400}, {0, 4800}
                }, {
                    {200, 12000}, {500, 18000}, {1000, 18000}, {1800, 18000},
                    {2600, 12000}, {1300, 2400}, {500, 2400}, {0, 4800}
                }};
            case EnvelopePreset::EvolvingDigital:
                return {{
                    {4095, 480}, {3900, 12000}, {3800, 12000}, {3600, 12000},
                    {3200, 18000}, {2600, 18000}, {1600, 24000}, {0, 36000}
                }, {
                    {3000, 480}, {800, 12000}, {3600, 12000}, {1200, 12000},
                    {2600, 18000}, {600, 18000}, {1800, 24000}, {0, 36000}
                }};
            case EnvelopePreset::Off:
            default:
                return {{
                    {0, 1}, {0, 1}, {0, 1}, {0, 1},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }, {
                    {0, 1}, {0, 1}, {0, 1}, {0, 1},
                    {0, 1}, {0, 1}, {0, 1}, {0, 1}
                }};
        }
    }

    bool updateEnvelopeRunner(
        const EnvelopeStage* stages,
        uint8_t& stage,
        uint32_t& sample,
        int32_t& level,
        int32_t& startLevel)
    {
        if (stage >= 8)
            return true;

        uint32_t time = stages[stage].time;
        if (time == 0)
            time = 1;

        sample++;
        int32_t target = stages[stage].level;
        level = startLevel + (((target - startLevel) * (int32_t)sample) / (int32_t)time);

        if (sample >= time)
        {
            level = target;
            stage++;
            sample = 0;
            startLevel = level;
        }

        return stage >= 8;
    }

    inline int32_t morphWave(uint32_t phase, int32_t wave)
    {
        uint32_t scaled = ((uint32_t)wave * 7u);
        uint32_t index = scaled >> 12;
        uint32_t frac = smoothStep12(scaled & 4095);

        int32_t a = czWave(phase, index);
        int32_t b = czWave(phase, index < 7 ? index + 1 : 7);

        return a + (((b - a) * (int32_t)frac) >> 12);
    }

    inline int32_t czWave(uint32_t phase, uint32_t wave)
    {
        int32_t saw = ((int32_t)(phase >> 20) & 4095) - 2048;
        int32_t square = (phase & 0x80000000u) ? 2047 : -2048;
        int32_t pulse = narrowPulseWave(phase);
        int32_t doubleSine = getSine(phase << 1);

        switch (wave)
        {
            case 0: return saw;
            case 1: return square;
            case 2: return pulse;
            case 3: return doubleSine;
            case 4: return sawPulseWave(phase);
            case 5: return resonantSawWindowWave(phase);
            case 6: return resonantTriangleWindowWave(phase);
            default: return resonantTrapezoidWindowWave(phase);
        }
    }

    inline int32_t narrowPulseWave(uint32_t phase)
    {
        uint32_t p = (phase >> 20) & 4095;

        if (p < 128)
            return 2047;

        if (p >= 2048 && p < 2176)
            return -2048;

        return getSine(phase) >> 3;
    }

    inline int32_t sawPulseWave(uint32_t phase)
    {
        uint32_t p = (phase >> 20) & 4095;

        if (p < 2304)
        {
            int32_t rise = ((int32_t)p * 4095) / 2304;
            int32_t curve = 4095 - (((4095 - rise) * (4095 - rise)) >> 12);
            return curve - 2048;
        }

        return -2048;
    }

    inline int32_t resonantSawWindowWave(uint32_t phase)
    {
        uint32_t p = (phase >> 20) & 4095;
        int32_t envelope = (int32_t)(4095 - p);
        int32_t overtone = getSine(phase * 6);
        int32_t body = (((int32_t)p - 2048) * envelope) >> 13;

        return clip(body + ((overtone * envelope) >> 12));
    }

    inline int32_t resonantTriangleWindowWave(uint32_t phase)
    {
        uint32_t p = (phase >> 20) & 4095;
        int32_t envelope =
            p < 2048 ? (int32_t)p << 1 : (int32_t)(4095 - p) << 1;
        int32_t triangle = p < 2048 ?
            ((int32_t)p << 1) - 2048 :
            6143 - ((int32_t)p << 1);
        int32_t overtone = getSine(phase * 5);
        int32_t body = (triangle * envelope) >> 14;

        return clip(body + ((overtone * envelope) >> 12));
    }

    inline int32_t resonantTrapezoidWindowWave(uint32_t phase)
    {
        uint32_t p = (phase >> 20) & 4095;
        int32_t envelope;

        if (p < 768)
            envelope = ((int32_t)p * 4095) / 768;
        else if (p < 3072)
            envelope = 4095;
        else
            envelope = ((int32_t)(4095 - p) * 4095) / 1023;

        int32_t overtone =
            (getSine(phase * 7) + (getSine(phase * 8) >> 1)) >> 1;
        int32_t body = envelope >> 4;

        return clip(body + ((overtone * envelope) >> 12));
    }

    // =========================================================
    // TURING MACHINE
    // =========================================================
    void stepTuring(int32_t knob)
    {
        uint32_t mask = (1u << turingLength) - 1u;
        uint8_t chance = turingMutationChance(knob);

        bool bit = turing & 1;
        if (fastNoise() < chance) bit ^= 1;

        turing = ((turing >> 1) | ((uint32_t)bit << (turingLength - 1))) & mask;

        turingPulse = bit;
        turingAltPulse = (turing >> 1) & 1u;
        turingPulseAge = 0;
        turingClockLedAge = 0;

        int32_t unipolar = (int32_t)((turing * 4095u) / mask);
        turingCv = clip((((unipolar - 2048) * TuringCvScale) >> 12) + TuringCvOffset);
        turingModCv = smooth(turingCv);
    }

    uint8_t turingMutationChance(int32_t knob)
    {
        int32_t distance = knob - 2048;
        if (distance < 0) distance = -distance;

        int32_t chance = 255 - (distance >> 3);
        if (chance < 0) chance = 0;
        if (chance > 255) chance = 255;

        return (uint8_t)chance;
    }

    bool internalTuringClock(int32_t speed)
    {
        int32_t moved = speed - lastClockSpeed;
        if (moved < 0) moved = -moved;
        if (moved > 64)
            useTappedClock = false;
        lastClockSpeed = speed;

        uint32_t inverseSpeed = 4095u - (uint32_t)clamp12(speed);
        uint32_t period =
            3000u +
            (((inverseSpeed * inverseSpeed) >> 12) * 57000u >> 12);
        if (period < 3000u) period = 3000u;

        if (!useTappedClock)
            turingClockPeriod = period;

        if (++turingClock >= turingClockPeriod)
        {
            turingClock = 0;
            return true;
        }

        return false;
    }

    void updateTuringPulseAge()
    {
        if (turingClockLedAge < TuringClockLedSamples)
            turingClockLedAge++;

        if ((turingPulse || turingAltPulse) && ++turingPulseAge > 1200)
        {
            turingPulse = false;
            turingAltPulse = false;
        }
    }

    void tapTuringClock()
    {
        if (turingClock > 2400u && turingClock < 96000u)
        {
            tappedClockPeriod = turingClock;
            turingClockPeriod = tappedClockPeriod;
            useTappedClock = true;
        }

        turingClock = 0;
    }

    int32_t smooth(int32_t x)
    {
        turingSmooth = (turingSmooth * 15 + x) >> 4;
        return turingSmooth;
    }

    void updateTuringLEDs()
    {
        LedBrightness(0, turing & 1 ? 4095 : 0);
        LedBrightness(1, turing & 2 ? 4095 : 0);
        LedBrightness(2, turing & 4 ? 4095 : 0);
        LedBrightness(3, PulseIn1());
        LedBrightness(4, (turingLength * 4095) >> 4);
        LedBrightness(5, turingClockLedAge < TuringClockLedSamples ? 4095 : 0);
    }

    void updateSynthLEDs(bool alt, int32_t pd, int32_t wave)
    {
        LedBrightness(0, pd);
        LedBrightness(1, wave);
        LedBrightness(2, osc2Level);
        LedBrightness(3, osc2Ring);
        LedBrightness(4, osc2Noise);
        LedBrightness(5, alt ? 4095 : 0);
    }

    void updateStartupEnvelopeSelect(Switch mode)
    {
        if (startupSelectChecked)
            return;

        if (startupSelectSamples < StartupSelectDelaySamples + StartupSelectWindowSamples)
        {
            startupSelectSamples++;
            if (startupSelectSamples >= StartupSelectDelaySamples &&
                mode == Switch::Down)
            {
                envelopeSelectMode = true;
                envelopeSelectReady = false;
            }
        }
        else
        {
            startupSelectChecked = true;
        }
    }

    void updateEnvelopeSelectMode(int32_t main, Switch mode, Switch previousMode)
    {
        AudioOut1(0);
        AudioOut2(0);
        CVOut1(0);
        CVOut2(0);
        PulseOut1(false);
        PulseOut2(false);

        uint8_t selected =
            (uint8_t)(((uint32_t)clamp12(main) * EnvelopePresetCount) >> 12);
        if (selected >= EnvelopePresetCount)
            selected = EnvelopePresetCount - 1;

        envelopePreset = selected;
        showEnvelopePresetLeds(envelopePreset);

        bool down = mode == Switch::Down;

        if (!envelopeSelectReady)
        {
            if (!down)
                envelopeSelectReady = true;

            return;
        }

        if (!down)
        {
            if (envelopeSelectHoldActive)
            {
                envelopeSelectMode = false;
                resetEnvelopeSelectHold();
            }

            return;
        }

        if (!envelopeSelectHoldActive || previousMode != Switch::Down)
        {
            envelopeSelectHoldActive = true;
            envelopeSelectHoldSamples = 0;
            envelopeSelectSaved = false;
        }

        if (envelopeSelectHoldSamples < SaveHoldSamples)
            envelopeSelectHoldSamples++;

        if (envelopeSelectHoldSamples >= SaveHoldSamples && !envelopeSelectSaved)
        {
            savePerformanceStateIfChanged();
            envelopeSelectSaved = true;
            saveConfirmSamples = SaveConfirmSamples;
        }

        updateSaveFeedback();
    }

    void showEnvelopePresetLeds(uint8_t preset)
    {
        for (uint32_t i = 0; i < 6; ++i)
            LedBrightness(i, (preset & (1u << i)) ? 4095 : 0);
    }

    void resetEnvelopeSelectHold()
    {
        envelopeSelectHoldActive = false;
        envelopeSelectHoldSamples = 0;
        envelopeSelectSaved = false;
    }

    void updateSaveGesture(bool alt)
    {
        if (!alt)
        {
            resetSaveGesture();
            updateSaveFeedback();
            return;
        }

        if (saveHoldCanSave)
        {
            if (saveHoldSamples < SaveHoldSamples)
                saveHoldSamples++;

            if (saveHoldSamples >= SaveHoldSamples && !saveCompletedThisHold)
            {
                savePerformanceStateIfChanged();
                saveCompletedThisHold = true;
                saveConfirmSamples = SaveConfirmSamples;
            }
        }

        updateSaveFeedback();
    }

    void updateSaveFeedback()
    {
        if (saveConfirmSamples == 0)
            return;

        saveConfirmSamples--;

        bool on = (saveConfirmSamples & 4096u) != 0;
        for (uint32_t i = 0; i < 6; ++i)
            LedBrightness(i, on ? 4095 : 0);
    }

    void resetSaveGesture()
    {
        saveHoldSamples = 0;
        saveCompletedThisHold = false;
        saveHoldCanSave = false;
    }

    // =========================================================
    // UTILS
    // =========================================================

    int32_t mix(int32_t a, int32_t b, int32_t amt)
    {
        return a + ((b - a) * amt >> 12);
    }

    int32_t clip(int32_t x)
    {
        if (x > 2047) return 2047;
        if (x < -2048) return -2048;
        return x;
    }

    int32_t clamp12(int32_t x)
    {
        if (x < 0) return 0;
        if (x > 4095) return 4095;
        return x;
    }

    void updateAltControls(int32_t main, int32_t x, int32_t y)
    {
        if (altMainPickedUp ||
            pickupAltControl(main, altMainEntry, clamp12(osc2Detune + 2048), altMainPickedUp))
        {
            osc2Detune = main - 2048;
            if (osc2Detune > -32 && osc2Detune < 32)
                osc2Detune = 0;

            osc2Level = osc2Detune < 0 ? -osc2Detune : osc2Detune;
            osc2Level <<= 1;
            if (osc2Level > 4095) osc2Level = 4095;
        }

        if (altXPickedUp ||
            pickupAltControl(x, altXEntry, osc2Ring, altXPickedUp))
            osc2Ring = x;

        if (altYPickedUp ||
            pickupAltControl(y, altYEntry, osc2Noise, altYPickedUp))
            osc2Noise = y;
    }

    void resetAltPickup(int32_t main, int32_t x, int32_t y)
    {
        altMainPickedUp = false;
        altXPickedUp = false;
        altYPickedUp = false;
        altMainEntry = main;
        altXEntry = x;
        altYEntry = y;
    }

    bool pickupAltControl(int32_t knob, int32_t entry, int32_t target, bool& pickedUp)
    {
        int32_t moved = knob - entry;
        if (moved < 0)
            moved = -moved;

        int32_t distance = knob - target;
        if (distance < 0)
            distance = -distance;

        if (moved >= 64 && distance <= 96)
            pickedUp = true;

        return pickedUp;
    }

    SavedPerformanceState currentPerformanceState()
    {
        SavedPerformanceState state;
        state.magic = SaveMagic;
        state.version = SaveVersion;
        state.size = sizeof(SavedPerformanceState);
        state.osc2Detune = osc2Detune;
        state.osc2Level = osc2Level;
        state.osc2Ring = osc2Ring;
        state.osc2Noise = osc2Noise;
        state.envelopePreset = envelopePreset;
        state.reserved[0] = 0;
        state.reserved[1] = 0;
        state.reserved[2] = 0;
        state.checksum = 0;
        state.checksum = checksumState(state);

        return state;
    }

    uint32_t checksumState(const SavedPerformanceState& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedPerformanceState) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }

        return checksum;
    }

    const SavedPerformanceState& flashPerformanceState()
    {
        return *reinterpret_cast<const SavedPerformanceState*>(
            XIP_BASE + SaveFlashOffset);
    }

    bool isValidSavedState(const SavedPerformanceState& state)
    {
        return
            state.magic == SaveMagic &&
            state.version == SaveVersion &&
            state.size == sizeof(SavedPerformanceState) &&
            state.checksum == checksumState(state);
    }

    bool savedStateMatches(const SavedPerformanceState& a, const SavedPerformanceState& b)
    {
        return
            a.osc2Detune == b.osc2Detune &&
            a.osc2Level == b.osc2Level &&
            a.osc2Ring == b.osc2Ring &&
            a.osc2Noise == b.osc2Noise &&
            a.envelopePreset == b.envelopePreset;
    }

    void loadPerformanceState()
    {
        const SavedPerformanceState& state = flashPerformanceState();
        if (!isValidSavedState(state))
            return;

        osc2Detune = state.osc2Detune;
        osc2Level = clamp12(state.osc2Level);
        osc2Ring = clamp12(state.osc2Ring);
        osc2Noise = clamp12(state.osc2Noise);
        envelopePreset = state.envelopePreset < EnvelopePresetCount ?
            state.envelopePreset :
            (uint8_t)EnvelopePreset::Off;
    }

    void savePerformanceStateIfChanged()
    {
        SavedPerformanceState state = currentPerformanceState();
        const SavedPerformanceState& saved = flashPerformanceState();

        if (isValidSavedState(saved) && savedStateMatches(state, saved))
            return;

        uint8_t page[FLASH_PAGE_SIZE];
        for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i)
            page[i] = 0xFF;

        const uint8_t* stateBytes = reinterpret_cast<const uint8_t*>(&state);
        for (uint32_t i = 0; i < sizeof(SavedPerformanceState); ++i)
            page[i] = stateBytes[i];

        uint32_t interrupts = save_and_disable_interrupts();
        flash_range_erase(SaveFlashOffset, FLASH_SECTOR_SIZE);
        flash_range_program(SaveFlashOffset, page, FLASH_PAGE_SIZE);
        restore_interrupts(interrupts);
    }

    int32_t applyDetune(int32_t freq, int32_t detune)
    {
        int32_t bend = detune;
        int32_t sign = 1;

        if (bend < 0)
        {
            sign = -1;
            bend = -bend;
        }

        // Small movements give fine beating; the far ends reach wide offsets.
        int32_t fine = (bend * bend) >> 11;
        int32_t offset = (freq * fine) >> 12;

        if (sign < 0)
            return freq - offset;

        return freq + offset;
    }

    void syncOscillators()
    {
        phase1 = 0;
        phase2 = 0;
        syncFadeSamples = 96;
    }

    int32_t updateSyncFade()
    {
        if (syncFadeSamples == 0)
            return 4095;

        syncFadeSamples--;
        return 4095 - ((syncFadeSamples * 4095) / 96);
    }

    uint8_t fastNoise()
    {
        noise = noise * 1664525u + 1013904223u;
        return noise >> 24;
    }

    int32_t pitchUnits(int32_t knob, int32_t pitchInput)
    {
        int32_t mainUnits =
            (clamp12(knob) * MainPitchOctaves * PitchUnitsPerOctave) / 4095;
        int32_t inputUnits =
            (pitchInput * PitchUnitsPerOctave) / PitchInputCountsPerVolt;

        return mainUnits + inputUnits;
    }

    int32_t pitchFrequency(int32_t units)
    {
        static constexpr uint32_t SemitoneRatioQ15[13] = {
            32768, 34716, 36781, 38968, 41285, 43740, 46341,
            49097, 52016, 55109, 58386, 61858, 65536
        };

        if (units < MinPitchUnits) units = MinPitchUnits;
        if (units > MaxPitchUnits) units = MaxPitchUnits;

        int32_t octaves = units / PitchUnitsPerOctave;
        int32_t octaveRemainder = units % PitchUnitsPerOctave;
        if (octaveRemainder < 0)
        {
            octaveRemainder += PitchUnitsPerOctave;
            octaves--;
        }

        int32_t semitone = (octaveRemainder * 12) / PitchUnitsPerOctave;
        int32_t semitoneStart = (semitone * PitchUnitsPerOctave) / 12;
        int32_t semitoneEnd = ((semitone + 1) * PitchUnitsPerOctave) / 12;
        int32_t frac =
            ((octaveRemainder - semitoneStart) << 15) /
            (semitoneEnd - semitoneStart);

        uint32_t ratio =
            SemitoneRatioQ15[semitone] +
            (((int32_t)SemitoneRatioQ15[semitone + 1] -
              (int32_t)SemitoneRatioQ15[semitone]) * frac >> 15);

        uint32_t freq = (uint32_t)(((uint64_t)C2PhaseIncrement * ratio) >> 15);

        if (octaves >= 0)
            freq <<= octaves;
        else
            freq >>= -octaves;

        return (int32_t)freq;
    }

    int32_t smoothPitch(int32_t target)
    {
        if (smoothedFreq == 0)
            smoothedFreq = target;

        int32_t delta = target - smoothedFreq;
        int32_t step = delta >> 9;

        if (step == 0 && delta != 0)
            step = delta > 0 ? 1 : -1;

        smoothedFreq += step;

        return smoothedFreq;
    }

    int32_t responseCurve(int32_t x)
    {
        uint32_t ux = (uint32_t)clamp12(x);

        return (ux * ux) >> 12;
    }

    uint32_t smoothStep12(uint32_t x)
    {
        uint32_t x2 = (x * x) >> 12;
        uint32_t x3 = (x2 * x) >> 12;

        return (3u * x2) - (2u * x3);
    }
private:

    uint32_t phase1 = 0;
    uint32_t phase2 = 0;
    uint32_t syncFadeSamples = 0;

    uint32_t turing = 0xACE1u;
    int32_t turingSmooth = 0;
    int32_t turingCv = 0;
    int32_t turingModCv = 0;
    uint32_t turingClock = 0;
    uint32_t turingClockPeriod = 12000;
    uint32_t tappedClockPeriod = 12000;
    int32_t lastClockSpeed = 2048;
    uint32_t turingPulseAge = 0;
    uint32_t turingClockLedAge = TuringClockLedSamples;
    uint32_t turingLength = 16;
    uint32_t externalClockAge = 96000u;
    bool turingPulse = false;
    bool turingAltPulse = false;
    bool useTappedClock = false;

    uint32_t noise = 1;
    uint32_t noiseHoldCounter = 0;
    int32_t heldPdNoise = 0;
    int32_t heldPhaseNoise = 0;
    uint32_t ampEnvelopeSample = 0;
    uint32_t pdEnvelopeSample = 0;
    int32_t ampEnvelopeLevel = 0;
    int32_t pdEnvelopeLevel = 0;
    int32_t ampEnvelopeStartLevel = 0;
    int32_t pdEnvelopeStartLevel = 0;
    uint8_t ampEnvelopeStage = 8;
    uint8_t pdEnvelopeStage = 8;
    bool envelopeActive = false;
    uint8_t envelopePreset = 0;
    uint32_t startupSelectSamples = 0;
    uint32_t envelopeSelectHoldSamples = 0;
    bool startupSelectChecked = false;
    bool envelopeSelectMode = false;
    bool envelopeSelectReady = false;
    bool envelopeSelectHoldActive = false;
    bool envelopeSelectSaved = false;

    int32_t pitchControl = 2048;
    int32_t pdControl = 0;
    int32_t waveControl = 0;
    int32_t smoothedFreq = 0;
    int32_t osc2Detune = 0;
    int32_t osc2Level = 0;
    int32_t osc2Ring = 0;
    int32_t osc2Noise = 0;
    int32_t altMainEntry = 2048;
    int32_t altXEntry = 0;
    int32_t altYEntry = 0;
    bool altMainPickedUp = false;
    bool altXPickedUp = false;
    bool altYPickedUp = false;
    uint32_t saveHoldSamples = 0;
    uint32_t saveConfirmSamples = 0;
    bool saveHoldCanSave = false;
    bool saveCompletedThisHold = false;
    Switch lastMode = Switch::Middle;
};

// =========================================================
// ENTRY
// =========================================================
C1ZZL3 card;

//int main()
//{
//    card.EnableNormalisationProbe();
//    card.Run();
//}
int main()
{
    card.Run();
}
