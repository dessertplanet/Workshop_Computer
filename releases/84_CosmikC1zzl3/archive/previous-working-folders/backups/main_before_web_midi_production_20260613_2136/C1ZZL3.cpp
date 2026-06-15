#include "ComputerCard.h"
#include "C1ZZL3_LUT.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

static constexpr uint8_t WebMidiManufacturer = 0x7Du;
static constexpr uint8_t WebMidiId[4] = {0x43u, 0x31u, 0x5Au, 0x33u}; // C1Z3
static constexpr uint8_t WebMidiCommandPreview = 0x01u;
static constexpr uint8_t WebMidiCommandSave = 0x02u;
static constexpr uint8_t WebMidiCommandSettings = 0x03u;
static constexpr uint8_t WebMidiCommandSaveSettings = 0x04u;
static constexpr uint32_t WebMidiEnvelopePayloadLength = 97u;
static constexpr uint32_t WebMidiSettingsPayloadLength = 7u;

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

    bool ShouldBootUsbHost()
    {
        return USBPowerState() == USBPowerState_t::DFP;
    }

    void ProcessUsbMidiByte(uint8_t byte)
    {
        if (byte == 0xF0u)
        {
            sysexReceiving = true;
            sysexLength = 0;
            sysexOverflow = false;
            return;
        }

        if (!sysexReceiving)
        {
            processMidiVoiceByte(byte);
            return;
        }

        if (byte == 0xF7u)
        {
            sysexReceiving = false;
            if (!sysexOverflow)
                handleWebMidiSysex();
            return;
        }

        if (sysexLength < sizeof(sysexBuffer))
            sysexBuffer[sysexLength++] = byte;
        else
            sysexOverflow = true;
    }

    // =========================================================
    // AUDIO CALLBACK
    // =========================================================
    void ProcessSample() override
    {
        applyPendingWebEnvelope();
        applyPendingMidiNote();

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
            updateWebMidiFeedback();
            lastMode = mode;
            return;
        }

        if (!startupSelectChecked && mode == Switch::Down)
        {
            resetAltPickup(main, x, y);
            resetSaveGesture();
            lastMode = mode;
            updateWebMidiFeedback();
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
                if (previousMode != Switch::Middle)
                    resetSynthPickup(main, x, y);

                updateSynthControls(main, x, y);
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
            int32_t freq = smoothPitch(pitchFrequency(currentPitchUnits(pitchControl, in1)));

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

            if (previousMode != Switch::Up)
                resetTuringPickup(main, x, y);

            updateTuringControls(main, x, y);

            uint32_t previousTuringLength = turingLength;
            turingLength = 2 + ((turingLengthControl * 14) >> 12);
            if (turingLength > 16) turingLength = 16;
            if (turingLength != previousTuringLength)
                turingLengthDisplaySamples = WebMidiFeedbackSamples;

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
                    clocked = internalTuringClock(turingClockSpeedControl);
            }

            if (clocked)
            {
                stepTuring(turingMutationControl);
                triggerTuringEnvelope();
                queueTuringMidiNote();
            }

            updateTuringPulseAge();

            CVOut1(turingCv);
            CVOut2(turingModCv);

            outputTuringSynthVoice();

            PulseOut1(turingPulse);
            PulseOut2(turingAltPulse);

            updateTuringLEDs();
        }

        updateWebMidiFeedback();
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
        EvolvingDigital,
        Custom1,
        Custom2,
        Custom3,
        Custom4,
        Custom5,
        Custom6,
        Custom7,
        Custom8
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
    static constexpr uint8_t FactoryEnvelopePresetCount = 9;
    static constexpr uint8_t CustomEnvelopePresetCount = 8;
    static constexpr uint8_t FirstCustomEnvelopePreset = (uint8_t)EnvelopePreset::Custom1;
    static constexpr uint8_t EnvelopePresetCount =
        FactoryEnvelopePresetCount + CustomEnvelopePresetCount;
    static constexpr uint32_t StartupSelectDelaySamples = 12000u;
    static constexpr uint32_t StartupSelectWindowSamples = 24000u;
    static constexpr uint32_t WebMidiFeedbackSamples = 24000u;
    static constexpr uint32_t SaveMagic = 0x43315A33u; // C1Z3
    static constexpr uint16_t SaveVersion = 5;
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
        uint8_t customEnvelopePersist[CustomEnvelopePresetCount];
        uint8_t midiInChannel;
        uint8_t midiOutChannel;
        uint8_t midiFlags;
        uint8_t reserved[3];
        EnvelopeProgram customEnvelopePrograms[CustomEnvelopePresetCount];
        uint32_t checksum;
    };

    static_assert(sizeof(SavedPerformanceState) <= FLASH_SECTOR_SIZE,
        "SavedPerformanceState must fit in one flash sector");

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
        wave = safeWaveControl(wave);

        int32_t osc1 =
            oscCZ(phase1, freq, pd, wave, noiseAmt);

        int32_t osc2Raw = 0;
        int32_t osc2 = 0;

        if (osc2Level > 0 || ring > 0)
        {
            int32_t freq2 =
                applyDetune(freq, osc2Detune);

            osc2Raw = oscCZ(phase2, freq2, pd, wave, noiseAmt);
            osc2 = (osc2Raw * osc2Level) >> 12;
        }

        if (ring > 0)
        {
            int32_t ringDrive = osc2Level;
            if (ringDrive < 2048)
                ringDrive = 2048;
            int32_t ringCarrier = (osc2Raw * ringDrive) >> 12;
            int32_t ringSig = clip((osc1 * ringCarrier) >> 10);
            int32_t ringMix = (ring * 3840) >> 12;
            osc1 = mix(osc1, ringSig, ringMix);
        }

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
        if (pdCurve == 0)
            return sine;

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

        int32_t noiseCurve = (responseCurve(amount) * 563) >> 10;
        int32_t pdJitter = (heldPdNoise * noiseCurve) >> 10;
        int32_t phaseJitter = heldPhaseNoise * (noiseCurve >> 6);

        pd = clamp12(pd + pdJitter);
        phase += (uint32_t)phaseJitter;
    }

    int32_t safeWaveControl(int32_t wave)
    {
        wave = clamp12(wave);

        if (wave > 3984)
            return 3984;

        return wave;
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
        {
            envelopeActive = false;
            if (midiNoteReleased)
                midiNoteActive = false;
        }

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
        uint8_t preset = envelopePreset;
        if (preset >= FirstCustomEnvelopePreset && preset < EnvelopePresetCount)
        {
            uint8_t customSlot = preset - FirstCustomEnvelopePreset;
            if (customEnvelopeValid[customSlot])
                return customEnvelopePrograms[customSlot];
        }

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

    void processMidiVoiceByte(uint8_t byte)
    {
        if (byte >= 0xF8u)
            return;

        if (byte & 0x80u)
        {
            midiRunningStatus = byte;
            midiDataCount = 0;
            return;
        }

        uint8_t type = midiRunningStatus & 0xF0u;
        if (type != 0x80u && type != 0x90u && type != 0xE0u)
            return;

        midiData[midiDataCount++] = byte & 0x7Fu;
        if (midiDataCount < 2u)
            return;

        midiDataCount = 0;
        uint8_t channel = midiRunningStatus & 0x0Fu;
        if (channel != midiInChannel)
            return;

        if (type == 0x90u && midiData[1] > 0)
        {
            pendingMidiNote = midiData[0];
            pendingMidiVelocity = midiData[1];
            pendingMidiNoteOn = true;
            return;
        }

        if (type == 0x80u || (type == 0x90u && midiData[1] == 0))
        {
            if (midiData[0] == midiNote)
            {
                if (envelopePreset == (uint8_t)EnvelopePreset::Off || !envelopeActive)
                    midiNoteActive = false;
                else
                    midiNoteReleased = true;
            }
            return;
        }

        if (type == 0xE0u)
        {
            int32_t bend = ((int32_t)midiData[1] << 7) | midiData[0];
            midiPitchBend = bend - 8192;
        }
    }

    void applyPendingMidiNote()
    {
        if (!pendingMidiNoteOn)
            return;

        pendingMidiNoteOn = false;
        midiNote = pendingMidiNote;
        midiVelocity = pendingMidiVelocity;
        midiNoteActive = true;
        midiNoteReleased = false;

        if (envelopePreset != (uint8_t)EnvelopePreset::Off)
        {
            syncOscillators();
            triggerEnvelope();
        }
    }

    int32_t currentPitchUnits(int32_t knob, int32_t pitchInput)
    {
        if (!midiNoteActive)
            return pitchUnits(knob, pitchInput);

        int32_t units = midiNotePitchUnits(midiNote);
        units += (midiPitchBend * PitchUnitsPerOctave) / (8192 * 6);
        units += (pitchInput * PitchUnitsPerOctave) / PitchInputCountsPerVolt;
        return units;
    }

    int32_t midiNotePitchUnits(uint8_t note)
    {
        return ((int32_t)note - 36) * PitchUnitsPerOctave / 12;
    }

    void queueTuringMidiNote()
    {
        if (!turingMidiOutEnabled)
            return;

        pendingMidiOutNote = 36u + (uint8_t)(((uint32_t)clamp12(turingCv) * 48u) >> 12);
        pendingMidiOutVelocity = turingPulse ? 100u : 48u;
        if (activeMidiOutNote < 128u)
            pendingMidiOutOff = true;
        pendingMidiOutOn = true;
        turingMidiGateSamples = TuringClockLedSamples;
    }

public:
    bool TakePendingMidiOut(uint8_t& channel, uint8_t& note, uint8_t& velocity, bool& on)
    {
        if (pendingMidiOutOff)
        {
            pendingMidiOutOff = false;
            channel = midiOutChannel;
            note = activeMidiOutNote;
            velocity = 0;
            on = false;
            bool valid = activeMidiOutNote < 128u;
            activeMidiOutNote = 255u;
            return valid;
        }

        if (pendingMidiOutOn)
        {
            pendingMidiOutOn = false;
            channel = midiOutChannel;
            note = pendingMidiOutNote;
            velocity = pendingMidiOutVelocity;
            activeMidiOutNote = note;
            on = true;
            return true;
        }

        return false;
    }

private:
    void handleWebMidiSysex()
    {
        if (sysexLength < 6u)
        {
            flashWebMidiRejected();
            return;
        }

        if (sysexBuffer[0] != WebMidiManufacturer)
        {
            flashWebMidiRejected();
            return;
        }

        for (uint32_t i = 0; i < 4u; ++i)
        {
            if (sysexBuffer[1u + i] != WebMidiId[i])
            {
                flashWebMidiRejected();
                return;
            }
        }

        uint8_t command = sysexBuffer[5];
        if (command == WebMidiCommandPreview || command == WebMidiCommandSave)
        {
            handleWebMidiEnvelope(command);
            return;
        }

        if (command == WebMidiCommandSettings || command == WebMidiCommandSaveSettings)
        {
            handleWebMidiSettings(command);
            return;
        }

        flashWebMidiRejected();
    }

    void handleWebMidiEnvelope(uint8_t command)
    {
        if (sysexLength != WebMidiEnvelopePayloadLength + 6u)
        {
            flashWebMidiRejected();
            return;
        }

        uint8_t customSlot = sysexBuffer[6];
        if (customSlot >= CustomEnvelopePresetCount)
        {
            flashWebMidiRejected();
            return;
        }

        EnvelopeProgram decoded;
        uint32_t offset = 6u + 1u + 16u;

        if (!decodeWebMidiStages(decoded.amp, offset))
        {
            flashWebMidiRejected();
            return;
        }

        if (!hasAudibleAmpStage(decoded.amp))
        {
            flashWebMidiRejected();
            return;
        }

        if (!decodeWebMidiStages(decoded.pd, offset))
        {
            flashWebMidiRejected();
            return;
        }

        customEnvelopePrograms[customSlot] = decoded;
        customEnvelopeValid[customSlot] = true;
        pendingWebEnvelopeSlot = customSlot;
        pendingWebEnvelopeShouldSave = command == WebMidiCommandSave;

        if (pendingWebEnvelopeShouldSave)
        {
            customEnvelopePersist[customSlot] = true;
            savePerformanceStateIfChanged();
        }

        pendingWebEnvelopeReady = true;
        flashWebMidiAccepted();
    }

    void handleWebMidiSettings(uint8_t command)
    {
        if (sysexLength != WebMidiSettingsPayloadLength + 6u)
        {
            flashWebMidiRejected();
            return;
        }

        uint32_t ring =
            ((uint32_t)sysexBuffer[6]) |
            ((uint32_t)sysexBuffer[7] << 7);
        uint32_t noiseAmt =
            ((uint32_t)sysexBuffer[8]) |
            ((uint32_t)sysexBuffer[9] << 7);
        uint8_t inChannel = sysexBuffer[10];
        uint8_t outChannel = sysexBuffer[11];
        uint8_t flags = sysexBuffer[12];

        if (ring > 4095u || noiseAmt > 4095u || inChannel > 15u || outChannel > 15u)
        {
            flashWebMidiRejected();
            return;
        }

        osc2Ring = ring;
        osc2Noise = noiseAmt;
        midiInChannel = inChannel;
        midiOutChannel = outChannel;
        turingMidiOutEnabled = (flags & 1u) != 0;

        if (command == WebMidiCommandSaveSettings)
            savePerformanceStateIfChanged();

        flashWebMidiAccepted();
    }

    bool decodeWebMidiStages(EnvelopeStage* stages, uint32_t& offset)
    {
        for (uint32_t i = 0; i < 8u; ++i)
        {
            uint32_t level =
                ((uint32_t)sysexBuffer[offset]) |
                ((uint32_t)sysexBuffer[offset + 1u] << 7);
            offset += 2u;

            uint32_t time =
                ((uint32_t)sysexBuffer[offset]) |
                ((uint32_t)sysexBuffer[offset + 1u] << 7) |
                ((uint32_t)sysexBuffer[offset + 2u] << 14);
            offset += 3u;

            if (level > 4095u)
                return false;

            if (time == 0u)
                time = 1u;

            stages[i].level = (uint16_t)level;
            stages[i].time = time;
        }

        return true;
    }

    bool hasAudibleAmpStage(const EnvelopeStage* stages)
    {
        for (uint32_t i = 0; i < 8u; ++i)
        {
            if (stages[i].level > 0)
                return true;
        }

        return false;
    }

    void applyPendingWebEnvelope()
    {
        if (!pendingWebEnvelopeReady)
            return;

        uint8_t customSlot = pendingWebEnvelopeSlot;
        if (customSlot >= CustomEnvelopePresetCount)
            customSlot = 0;

        pendingWebEnvelopeReady = false;
        envelopePreset = FirstCustomEnvelopePreset + customSlot;
        triggerEnvelope();

        if (pendingWebEnvelopeShouldSave)
        {
            customEnvelopePersist[customSlot] = true;
            pendingWebEnvelopeShouldSave = false;
        }
        else
        {
            customEnvelopePersist[customSlot] = false;
        }

    }

    void flashWebMidiAccepted()
    {
        webMidiFeedbackKind = 1;
        webMidiFeedbackSamples = WebMidiFeedbackSamples;
    }

    void flashWebMidiRejected()
    {
        webMidiFeedbackKind = 2;
        webMidiFeedbackSamples = WebMidiFeedbackSamples;
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
        switch (wave)
        {
            case 0: return ((int32_t)(phase >> 20) & 4095) - 2048;
            case 1: return (phase & 0x80000000u) ? 2047 : -2048;
            case 2: return narrowPulseWave(phase);
            case 3: return getSine(phase << 1);
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

        if (turingMidiGateSamples > 0 && --turingMidiGateSamples == 0)
            pendingMidiOutOff = true;

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
        if (turingLengthDisplaySamples > 0)
        {
            turingLengthDisplaySamples--;
            for (uint32_t i = 0; i < 6; ++i)
                LedBrightness(i, (turingLength & (1u << i)) ? 4095 : 0);
            return;
        }

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

    void updateWebMidiFeedback()
    {
        if (webMidiFeedbackSamples == 0)
            return;

        uint32_t age = WebMidiFeedbackSamples - webMidiFeedbackSamples;
        webMidiFeedbackSamples--;

        if (webMidiFeedbackKind == 1)
        {
            bool on = (age & 2048u) < 1536u;
            for (uint32_t i = 0; i < 6; ++i)
                LedBrightness(i, on ? 4095 : 0);
            return;
        }

        bool phase = (age & 2048u) != 0;
        for (uint32_t i = 0; i < 6; ++i)
        {
            bool even = (i & 1u) == 0;
            LedBrightness(i, even == phase ? 4095 : 0);
        }
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

    void updateSynthControls(int32_t main, int32_t x, int32_t y)
    {
        if (synthMainPickedUp ||
            pickupControl(main, synthMainEntry, pitchControl, synthMainPickedUp))
            pitchControl = main;

        if (synthXPickedUp ||
            pickupControl(x, synthXEntry, pdControl, synthXPickedUp))
            pdControl = x;

        if (synthYPickedUp ||
            pickupControl(y, synthYEntry, waveControl, synthYPickedUp))
            waveControl = y;
    }

    void resetSynthPickup(int32_t main, int32_t x, int32_t y)
    {
        synthMainPickedUp = false;
        synthXPickedUp = false;
        synthYPickedUp = false;
        synthMainEntry = main;
        synthXEntry = x;
        synthYEntry = y;
    }

    void updateTuringControls(int32_t main, int32_t x, int32_t y)
    {
        if (turingMainPickedUp ||
            pickupControl(main, turingMainEntry, turingMutationControl, turingMainPickedUp))
            turingMutationControl = main;

        if (turingXPickedUp ||
            pickupControl(x, turingXEntry, turingLengthControl, turingXPickedUp))
            turingLengthControl = x;

        if (turingYPickedUp ||
            pickupControl(y, turingYEntry, turingClockSpeedControl, turingYPickedUp))
            turingClockSpeedControl = y;
    }

    void resetTuringPickup(int32_t main, int32_t x, int32_t y)
    {
        turingMainPickedUp = false;
        turingXPickedUp = false;
        turingYPickedUp = false;
        turingMainEntry = main;
        turingXEntry = x;
        turingYEntry = y;
    }

    bool pickupControl(int32_t knob, int32_t entry, int32_t target, bool& pickedUp)
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
        state.envelopePreset = envelopePreset < FactoryEnvelopePresetCount ?
            envelopePreset :
            (uint8_t)EnvelopePreset::Off;
        state.midiInChannel = midiInChannel;
        state.midiOutChannel = midiOutChannel;
        state.midiFlags = turingMidiOutEnabled ? 1u : 0u;
        for (uint32_t i = 0; i < CustomEnvelopePresetCount; ++i)
        {
            state.customEnvelopePersist[i] = customEnvelopePersist[i] ? 1 : 0;
            state.customEnvelopePrograms[i] = customEnvelopePrograms[i];
        }
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
        bool performanceMatches =
            a.osc2Detune == b.osc2Detune &&
            a.osc2Level == b.osc2Level &&
            a.osc2Ring == b.osc2Ring &&
            a.osc2Noise == b.osc2Noise &&
            a.envelopePreset == b.envelopePreset &&
            a.midiInChannel == b.midiInChannel &&
            a.midiOutChannel == b.midiOutChannel &&
            a.midiFlags == b.midiFlags;

        if (!performanceMatches)
            return false;

        for (uint32_t i = 0; i < CustomEnvelopePresetCount; ++i)
        {
            if (a.customEnvelopePersist[i] != b.customEnvelopePersist[i])
                return false;

            if (!envelopeProgramsMatch(a.customEnvelopePrograms[i], b.customEnvelopePrograms[i]))
                return false;
        }

        return true;
    }

    bool envelopeProgramsMatch(const EnvelopeProgram& a, const EnvelopeProgram& b)
    {
        for (uint32_t i = 0; i < 8u; ++i)
        {
            if (a.amp[i].level != b.amp[i].level || a.amp[i].time != b.amp[i].time)
                return false;

            if (a.pd[i].level != b.pd[i].level || a.pd[i].time != b.pd[i].time)
                return false;
        }

        return true;
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
        envelopePreset = state.envelopePreset < FactoryEnvelopePresetCount ?
            state.envelopePreset :
            (uint8_t)EnvelopePreset::Off;
        midiInChannel = state.midiInChannel < 16u ? state.midiInChannel : 0u;
        midiOutChannel = state.midiOutChannel < 16u ? state.midiOutChannel : 0u;
        turingMidiOutEnabled = (state.midiFlags & 1u) != 0;

        for (uint32_t i = 0; i < CustomEnvelopePresetCount; ++i)
        {
            customEnvelopePersist[i] = state.customEnvelopePersist[i] != 0;
            customEnvelopeValid[i] = customEnvelopePersist[i];
            if (customEnvelopePersist[i])
                customEnvelopePrograms[i] = state.customEnvelopePrograms[i];
        }
    }

    void savePerformanceStateIfChanged()
    {
        SavedPerformanceState state = currentPerformanceState();
        const SavedPerformanceState& saved = flashPerformanceState();

        if (isValidSavedState(saved) && savedStateMatches(state, saved))
            return;

        uint8_t page[FLASH_SECTOR_SIZE];
        for (uint32_t i = 0; i < FLASH_SECTOR_SIZE; ++i)
            page[i] = 0xFF;

        const uint8_t* stateBytes = reinterpret_cast<const uint8_t*>(&state);
        for (uint32_t i = 0; i < sizeof(SavedPerformanceState); ++i)
            page[i] = stateBytes[i];

        uint32_t interrupts = save_and_disable_interrupts();
        flash_range_erase(SaveFlashOffset, FLASH_SECTOR_SIZE);
        flash_range_program(SaveFlashOffset, page, FLASH_SECTOR_SIZE);
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
    uint32_t turingLengthDisplaySamples = 0;
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
    volatile uint32_t webMidiFeedbackSamples = 0;
    volatile uint8_t webMidiFeedbackKind = 0;
    EnvelopeProgram customEnvelopePrograms[CustomEnvelopePresetCount] = {};
    bool customEnvelopeValid[CustomEnvelopePresetCount] = {};
    bool customEnvelopePersist[CustomEnvelopePresetCount] = {};
    volatile uint8_t pendingWebEnvelopeSlot = 0;
    volatile bool pendingWebEnvelopeShouldSave = false;
    volatile bool pendingWebEnvelopeReady = false;
    uint8_t sysexBuffer[128] = {};
    uint32_t sysexLength = 0;
    bool sysexReceiving = false;
    bool sysexOverflow = false;
    uint8_t midiRunningStatus = 0;
    uint8_t midiData[2] = {};
    uint8_t midiDataCount = 0;
    volatile uint8_t pendingMidiNote = 60;
    volatile uint8_t pendingMidiVelocity = 100;
    volatile bool pendingMidiNoteOn = false;
    uint8_t midiNote = 60;
    uint8_t midiVelocity = 100;
    int32_t midiPitchBend = 0;
    bool midiNoteActive = false;
    bool midiNoteReleased = false;
    uint8_t midiInChannel = 0;
    uint8_t midiOutChannel = 0;
    bool turingMidiOutEnabled = false;
    volatile uint8_t pendingMidiOutNote = 60;
    volatile uint8_t pendingMidiOutVelocity = 100;
    volatile bool pendingMidiOutOn = false;
    volatile bool pendingMidiOutOff = false;
    uint8_t activeMidiOutNote = 255;
    uint32_t turingMidiGateSamples = 0;

    int32_t pitchControl = 2048;
    int32_t pdControl = 0;
    int32_t waveControl = 0;
    int32_t turingMutationControl = 2048;
    int32_t turingLengthControl = 4095;
    int32_t turingClockSpeedControl = 2048;
    int32_t smoothedFreq = 0;
    int32_t osc2Detune = 0;
    int32_t osc2Level = 0;
    int32_t osc2Ring = 0;
    int32_t osc2Noise = 0;
    int32_t synthMainEntry = 2048;
    int32_t synthXEntry = 0;
    int32_t synthYEntry = 0;
    int32_t turingMainEntry = 2048;
    int32_t turingXEntry = 4095;
    int32_t turingYEntry = 2048;
    int32_t altMainEntry = 2048;
    int32_t altXEntry = 0;
    int32_t altYEntry = 0;
    bool synthMainPickedUp = true;
    bool synthXPickedUp = true;
    bool synthYPickedUp = true;
    bool turingMainPickedUp = true;
    bool turingXPickedUp = true;
    bool turingYPickedUp = true;
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
static volatile uint8_t hostMidiDeviceAddress = 0;
static volatile int8_t hostMidiOutputCable = -1;

extern "C" void tuh_midi_mount_cb(
    uint8_t dev_addr,
    uint8_t in_ep,
    uint8_t out_ep,
    uint8_t num_cables_rx,
    uint16_t num_cables_tx)
{
    (void)in_ep;
    (void)out_ep;
    (void)num_cables_rx;

    if (hostMidiDeviceAddress == 0)
    {
        hostMidiDeviceAddress = dev_addr;
        hostMidiOutputCable = num_cables_tx > 0 ? (int8_t)(num_cables_tx - 1u) : -1;
    }
}

extern "C" void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;

    if (dev_addr == hostMidiDeviceAddress)
    {
        hostMidiDeviceAddress = 0;
        hostMidiOutputCable = -1;
    }
}

extern "C" void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != hostMidiDeviceAddress || num_packets == 0)
        return;

    uint8_t cable = 0;
    uint8_t bytes[128];
    while (true)
    {
        uint32_t count = tuh_midi_stream_read(dev_addr, &cable, bytes, sizeof(bytes));
        if (count == 0)
            break;

        for (uint32_t i = 0; i < count; ++i)
            card.ProcessUsbMidiByte(bytes[i]);
    }
}

extern "C" void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

static void sendPendingMidiOutToDevice()
{
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    bool on;
    if (!card.TakePendingMidiOut(channel, note, velocity, on))
        return;

    uint8_t message[3] = {
        (uint8_t)((on ? 0x90u : 0x80u) | (channel & 0x0Fu)),
        (uint8_t)(note & 0x7Fu),
        (uint8_t)(velocity & 0x7Fu)
    };
    tud_midi_stream_write(0, message, sizeof(message));
}

static void sendPendingMidiOutToHostDevice()
{
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    bool on;
    if (!card.TakePendingMidiOut(channel, note, velocity, on))
        return;

    if (hostMidiDeviceAddress == 0 || hostMidiOutputCable < 0)
        return;

    uint8_t message[3] = {
        (uint8_t)((on ? 0x90u : 0x80u) | (channel & 0x0Fu)),
        (uint8_t)(note & 0x7Fu),
        (uint8_t)(velocity & 0x7Fu)
    };
    tuh_midi_stream_write(
        hostMidiDeviceAddress,
        (uint8_t)hostMidiOutputCable,
        message,
        sizeof(message));
    tuh_midi_stream_flush(hostMidiDeviceAddress);
}

void usbMidiWorker()
{
    sleep_ms(100);
    bool hostMode = card.ShouldBootUsbHost();

    if (hostMode)
        tuh_init(0);
    else
        tud_init(0);

    while (true)
    {
        if (hostMode)
        {
            tuh_task();
            sendPendingMidiOutToHostDevice();
        }
        else
        {
            tud_task();

            uint8_t bytes[64];
            uint32_t count = tud_midi_stream_read(bytes, sizeof(bytes));
            for (uint32_t i = 0; i < count; ++i)
                card.ProcessUsbMidiByte(bytes[i]);

            sendPendingMidiOutToDevice();
        }
    }
}

//int main()
//{
//    card.EnableNormalisationProbe();
//    card.Run();
//}
int main()
{
    multicore_launch_core1(usbMidiWorker);
    card.Run();
}
