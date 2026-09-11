// SSI-263 Voice — a polyphonic formant speech synthesizer for the Music Thing
// Modular Workshop System Computer, built on the clean-room Casso SSI-263A
// engine, ported to a fixed-point vocoder (SsiVoice.h).
//
// See rp2040-voice-port-design.md for the full design. This build is the first
// hardware bring-up: it plays a hard-coded "Daisy Bell" score (daisy_demo.h) on
// loop so the synthesis can be heard on-device. Device-mode MIDI notes drive
// STEP pacing through that phrase; the SysEx phrase bank comes next.

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "tusb.h"
#include "usb_midi_host.h"

#include <cmath>

#include "SsiVoice.h"
#include "daisy_demo.h"

// Number of phrase slots in the bank (one per panel LED).
static constexpr int kNumSlots = 6;

// Debugger-visible audio timing counters. The 24 kHz callback has 41.67 us
// between invocations; timerawl is a low-overhead 1 MHz free-running clock.
volatile uint32_t processSampleMaxUs = 0;
volatile uint32_t processSampleOverruns = 0;
volatile uint32_t midiEventsReceived = 0;
volatile uint32_t midiEventsDropped = 0;
volatile uint32_t midiActiveVoices = 0;
volatile uint32_t midiModeActive = 0;
volatile int32_t midiStepIndex = -1;
volatile uint32_t midiStepState = 0;

static inline void RecordProcessSampleTiming(uint32_t startUs)
{
	uint32_t elapsedUs = timer_hw->timerawl - startUs;
	if (elapsedUs > processSampleMaxUs)
		processSampleMaxUs = elapsedUs;
	if (elapsedUs > 41)
		processSampleOverruns++;
}

// Boot mute, in samples: silence while the DAC settles and core 1 brings USB
// up, so the card does not click on power-up.
static constexpr int32_t kBootMute = 12000; // 0.5 s at 24 kHz

// Mode 3 is staged while powered down and latched when CTL falls. Casso's song
// then writes raw phoneme bytes (DR=0, the longest phoneme duration).
static constexpr uint8_t kModeBits = SsiVoice::kModePhonemeTransitioned;

static inline float MidiToHz(uint8_t note)
{
	return 440.0f * std::pow(2.0f, (static_cast<int>(note) - 69) / 12.0f);
}

static inline uint32_t MidiToPhaseIncrement(uint8_t note)
{
	return static_cast<uint32_t>(MidiToHz(note) * (4294967296.0 / 24000.0));
}

class VoiceCard : public ComputerCard
{
public:
	VoiceCard()
	{
		bootCounter_ = 0;
		powerState_ = Unsupported;
		isUSBMIDIHost_ = false;

		segIndex_ = 0;
		segSamplesLeft_ = 0;
		curReg0_ = 0;
		midiMode_ = false;
		midiWrite_ = 0;
		midiRead_ = 0;
		voiceAgeCounter_ = 0;
		heldNoteCount_ = 0;
		releaseVoice_ = -1;
		stepIndex_ = -1;
		stepState_ = StepState::Demo;
		stepSamplesLeft_ = 0;
		releasePending_ = false;
		for (int note = 0; note < 128; note++)
			midiPhaseIncrement_[note] = MidiToPhaseIncrement(static_cast<uint8_t>(note));
		for (int voice = 0; voice < SsiVoice::kMaxVoices; voice++)
		{
			voiceNote_[voice] = kNoNote;
			voiceChannel_[voice] = 0;
			voiceAge_[voice] = 0;
		}
		for (int segment = 0; segment < kDaisyLen; segment++)
			for (int voice = 0; voice < kDemoVoiceCount; voice++)
				daisyPhaseIncrement_[segment][voice] = MidiToPhaseIncrement(
					static_cast<uint8_t>(kDaisy[segment].note + kDemoChordSemitones[voice]));

		// The ComputerCard callback is configured for 24 kHz in ComputerCard.h.
		engine_.SetXckClock(kDaisyXckHz);
		engine_.SetSampleRate(24000);
		engine_.SetTickClock(24000);

		// Power the chip up: a benign pause phoneme with the mode bits, then
		// CTL low (bit 7 = 0) with mid articulation and full amplitude, which
		// latches the mode and starts playback. The sequencer takes over on
		// the first ProcessSample.
		engine_.WriteRegister(SsiVoice::kRegDurationPhoneme, (kModeBits << 6) | 0x00);
		engine_.WriteRegister(SsiVoice::kRegCtlArtAmp,
		                      (kDaisyArticulation << 4) | kDaisyAmplitude);
		engine_.WriteRegister(SsiVoice::kRegFilterFreq, kDaisyFilter);

		// Core 1 owns the USB stack; core 0 runs the audio ISR via Run().
		instance_ = this;
		multicore_launch_core1(Core1Entry);
	}

	// ---- Core 1: USB (device or host, chosen at power-on) ----------------

	static void Core1Entry() { instance_->USBCore(); }

	void USBCore()
	{
		// Give the USB power circuitry time to settle to its port state.
		sleep_us(150000);

		powerState_ = USBPowerState();
		// Device for UFP or unsupported (pre-2025 boards); host for DFP.
		isUSBMIDIHost_ = (powerState_ == DFP);

		if (isUSBMIDIHost_)
			tuh_init(TUH_OPT_RHPORT);
		else
			tud_init(TUD_OPT_RHPORT);

		while (true)
		{
			if (isUSBMIDIHost_)
			{
				tuh_task();
				// TODO (Phase 3): pitch + pacing from host MIDI notes.
			}
			else
			{
				tud_task();
				uint8_t packet[4];
				while (tud_midi_available())
				{
					if (!tud_midi_packet_read(packet))
						break;
					uint8_t type = packet[1] & 0xF0;
					if (type == 0x80 || type == 0x90 || type == 0xB0)
						QueueMidiEvent(packet[1], packet[2], packet[3]);
				}
			}
		}
	}

	// ---- Core 0: 24 kHz audio + panel ------------------------------------

	virtual void ProcessSample() override
	{
		uint32_t startUs = timer_hw->timerawl;
		ProcessMidiEvents();

		// Half a second of silence at power-up while the DAC settles.
		if (bootCounter_ < kBootMute)
		{
			bootCounter_++;
			AudioOut1(0);
			AudioOut2(0);
			RecordProcessSampleTiming(startUs);
			return;
		}

		if (midiMode_)
			ProcessStep();
		else
		{
			if (segSamplesLeft_ == 0)
				AdvanceSegment();
			segSamplesLeft_--;
		}

		// A phoneme whose own duration has expired is re-triggered so the
		// syllable sustains for the whole score segment.
		if (engine_.IsRequesting() && (!midiMode_ || stepState_ != StepState::Idle))
			engine_.WriteRegister(SsiVoice::kRegDurationPhoneme, curReg0_);

		int32_t sQ = engine_.GenerateSample();
		engine_.Tick(1);

		// Preserve the model's full-scale waveform without a second DAC clamp.
		int32_t out = ((sQ >> 8) * 2000) >> 16;
		if (out > 2047) out = 2047;
		if (out < -2047) out = -2047;

		AudioOut1(static_cast<int16_t>(out));
		AudioOut2(static_cast<int16_t>(out));
		RecordProcessSampleTiming(startUs);
	}

	// MIDI host device address, set by the rppicomidi mount callback below.
	static uint8_t midiDevAddr;
	static VoiceCard *instance_;

private:
	void AdvanceSegment()
	{
		int currentSegment = segIndex_;
		const DaisySeg &seg = kDaisy[currentSegment];
		segIndex_ = (segIndex_ + 1) % kDaisyLen;

		if (!midiMode_)
			for (int voice = 0; voice < kDemoVoiceCount; voice++)
				engine_.SetVoicePhaseIncrement(voice, daisyPhaseIncrement_[currentSegment][voice]);
		curReg0_ = seg.phoneme;
		engine_.WriteRegister(SsiVoice::kRegDurationPhoneme, curReg0_);
		segSamplesLeft_ = static_cast<int32_t>(seg.units) * kDaisyUnitSamples;

		for (int i = 0; i < kNumSlots; i++)
			LedOn(i, i == (segIndex_ % kNumSlots));
	}

	static constexpr uint8_t kMidiQueueSize = 16;
	static constexpr uint8_t kMidiQueueMask = kMidiQueueSize - 1;
	static constexpr uint8_t kNoNote = 0xFF;
	enum class StepState : uint8_t { Demo, Idle, Onset, Nucleus, Coda };

	void QueueMidiEvent(uint8_t status, uint8_t data1, uint8_t data2)
	{
		uint8_t next = static_cast<uint8_t>((midiWrite_ + 1) & kMidiQueueMask);
		if (next == midiRead_)
		{
			midiEventsDropped++;
			return;
		}
		midiQueue_[midiWrite_] = static_cast<uint32_t>(status)
		                            | (static_cast<uint32_t>(data1) << 8)
		                            | (static_cast<uint32_t>(data2) << 16);
		__dmb();
		midiWrite_ = next;
		midiEventsReceived++;
	}

	bool PopMidiEvent(uint32_t &event)
	{
		uint8_t read = midiRead_;
		if (read == midiWrite_)
			return false;
		__dmb();
		event = midiQueue_[read];
		__dmb();
		midiRead_ = static_cast<uint8_t>((read + 1) & kMidiQueueMask);
		return true;
	}

	void ProcessMidiEvents()
	{
		uint32_t event;
		for (int count = 0; count < 2 && PopMidiEvent(event); count++)
		{
			uint8_t status = static_cast<uint8_t>(event);
			uint8_t note = static_cast<uint8_t>(event >> 8) & 0x7F;
			uint8_t velocity = static_cast<uint8_t>(event >> 16) & 0x7F;
			uint8_t type = status & 0xF0;
			uint8_t channel = status & 0x0F;
			if (type == 0x90 && velocity != 0)
				NoteOn(channel, note);
			else if (type == 0x80 || type == 0x90)
				NoteOff(channel, note);
			else if (type == 0xB0 && (note == 120 || note == 123))
				AllNotesOff();
		}
	}

	void EnterMidiMode()
	{
		if (midiMode_)
			return;
		midiMode_ = true;
		midiModeActive = 1;
		for (int voice = 0; voice < SsiVoice::kMaxVoices; voice++)
		{
			engine_.SetVoiceActive(voice, false);
			voiceNote_[voice] = kNoNote;
		}
		engine_.SetOutputGate(false);
		stepState_ = StepState::Idle;
		midiStepState = static_cast<uint32_t>(stepState_);
	}

	void NoteOn(uint8_t channel, uint8_t note)
	{
		EnterMidiMode();
		bool freshOnset = heldNoteCount_ == 0;
		if (freshOnset && releaseVoice_ >= 0)
		{
			engine_.SetVoiceActive(releaseVoice_, false);
			releaseVoice_ = -1;
		}
		int selected = -1;
		bool existing = false;
		for (int voice = 0; voice < SsiVoice::kMaxVoices; voice++)
		{
			if (voiceNote_[voice] == note && voiceChannel_[voice] == channel)
			{
				selected = voice;
				existing = true;
				break;
			}
			if (selected < 0 && voiceNote_[voice] == kNoNote)
				selected = voice;
		}
		if (selected < 0)
		{
			selected = 0;
			for (int voice = 1; voice < SsiVoice::kMaxVoices; voice++)
				if (voiceAge_[voice] < voiceAge_[selected])
					selected = voice;
		}

		voiceNote_[selected] = note;
		voiceChannel_[selected] = channel;
		voiceAge_[selected] = ++voiceAgeCounter_;
		engine_.SetVoicePhaseIncrement(selected, midiPhaseIncrement_[note]);
		if (!existing && heldNoteCount_ < SsiVoice::kMaxVoices)
			heldNoteCount_++;
		midiActiveVoices = heldNoteCount_;
		if (freshOnset)
			StartNextStep();
	}

	void NoteOff(uint8_t channel, uint8_t note)
	{
		if (!midiMode_)
			return;
		int releasedVoice = -1;
		for (int voice = 0; voice < SsiVoice::kMaxVoices; voice++)
		{
			if (voiceNote_[voice] == note && voiceChannel_[voice] == channel)
			{
				voiceNote_[voice] = kNoNote;
				releasedVoice = voice;
				if (heldNoteCount_ > 0)
					heldNoteCount_--;
				break;
			}
		}
		if (releasedVoice < 0)
			return;
		if (heldNoteCount_ > 0)
			engine_.SetVoiceActive(releasedVoice, false);
		else
		{
			releaseVoice_ = releasedVoice;
			BeginStepRelease();
		}
		midiActiveVoices = heldNoteCount_;
	}

	void AllNotesOff()
	{
		if (!midiMode_ || heldNoteCount_ == 0)
			return;
		releaseVoice_ = -1;
		for (int voice = 0; voice < SsiVoice::kMaxVoices; voice++)
		{
			if (voiceNote_[voice] != kNoNote && releaseVoice_ < 0)
				releaseVoice_ = voice;
			else
				engine_.SetVoiceActive(voice, false);
			voiceNote_[voice] = kNoNote;
		}
		heldNoteCount_ = 0;
		midiActiveVoices = 0;
		BeginStepRelease();
	}

	void WriteStepPhoneme(uint8_t phoneme)
	{
		curReg0_ = phoneme;
		engine_.WriteRegister(SsiVoice::kRegDurationPhoneme, curReg0_);
	}

	void StartNextStep()
	{
		stepIndex_ = (stepIndex_ + 1) % kDaisyStepCount;
		midiStepIndex = stepIndex_;
		releasePending_ = false;
		engine_.SetOutputGate(true);
		const DaisyStep &step = kDaisySteps[stepIndex_];
		if (step.onset != kNoPhoneme)
		{
			WriteStepPhoneme(step.onset);
			stepSamplesLeft_ = static_cast<int32_t>(step.onsetUnits) * kDaisyUnitSamples;
			stepState_ = StepState::Onset;
		}
		else
		{
			StartNucleus();
		}
		midiStepState = static_cast<uint32_t>(stepState_);
		for (int i = 0; i < kNumSlots; i++)
			LedOn(i, i == (stepIndex_ % kNumSlots));
	}

	void StartNucleus()
	{
		WriteStepPhoneme(kDaisySteps[stepIndex_].nucleus);
		stepState_ = StepState::Nucleus;
		midiStepState = static_cast<uint32_t>(stepState_);
	}

	void BeginStepRelease()
	{
		if (stepState_ == StepState::Onset)
		{
			releasePending_ = true;
			return;
		}
		StartCoda();
	}

	void StartCoda()
	{
		const DaisyStep &step = kDaisySteps[stepIndex_];
		if (step.coda == kNoPhoneme)
		{
			FinishStep();
			return;
		}
		WriteStepPhoneme(step.coda);
		stepSamplesLeft_ = static_cast<int32_t>(step.codaUnits) * kDaisyUnitSamples;
		stepState_ = StepState::Coda;
		midiStepState = static_cast<uint32_t>(stepState_);
	}

	void FinishStep()
	{
		engine_.SetOutputGate(false);
		if (releaseVoice_ >= 0)
		{
			engine_.SetVoiceActive(releaseVoice_, false);
			releaseVoice_ = -1;
		}
		stepState_ = StepState::Idle;
		midiStepState = static_cast<uint32_t>(stepState_);
	}

	void ProcessStep()
	{
		if (stepState_ != StepState::Onset && stepState_ != StepState::Coda)
			return;
		if (stepSamplesLeft_ > 0)
			stepSamplesLeft_--;
		if (stepSamplesLeft_ != 0)
			return;

		if (stepState_ == StepState::Onset)
		{
			if (heldNoteCount_ > 0 && !releasePending_)
				StartNucleus();
			else
				StartCoda();
		}
		else
		{
			FinishStep();
		}
	}

	SsiVoice engine_;

	int32_t bootCounter_;

	int      segIndex_;
	int32_t  segSamplesLeft_;
	uint8_t  curReg0_;
	uint32_t daisyPhaseIncrement_[kDaisyLen][kDemoVoiceCount];
	uint32_t midiPhaseIncrement_[128];
	uint8_t  voiceNote_[SsiVoice::kMaxVoices];
	uint8_t  voiceChannel_[SsiVoice::kMaxVoices];
	uint32_t voiceAge_[SsiVoice::kMaxVoices];
	uint32_t voiceAgeCounter_;
	uint32_t heldNoteCount_;
	int      releaseVoice_;
	bool     midiMode_;
	int      stepIndex_;
	StepState stepState_;
	int32_t  stepSamplesLeft_;
	bool     releasePending_;

	volatile uint32_t midiQueue_[kMidiQueueSize] = {};
	volatile uint8_t midiWrite_;
	volatile uint8_t midiRead_;

	volatile USBPowerState_t powerState_;
	bool isUSBMIDIHost_;
};

uint8_t VoiceCard::midiDevAddr = 0;
VoiceCard *VoiceCard::instance_ = nullptr;

// ---- rppicomidi/usb_midi_host callbacks (host mode) ----------------------

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
                       uint8_t num_cables_rx, uint16_t num_cables_tx)
{
	(void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;
	if (VoiceCard::midiDevAddr == 0)
		VoiceCard::midiDevAddr = dev_addr;
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	(void)instance;
	if (dev_addr == VoiceCard::midiDevAddr)
		VoiceCard::midiDevAddr = 0;
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
	if (VoiceCard::midiDevAddr != dev_addr || num_packets == 0)
		return;

	uint8_t cable_num;
	uint8_t buffer[48];
	while (tuh_midi_stream_read(dev_addr, &cable_num, buffer, sizeof(buffer)) > 0)
	{
		// TODO (Phase 3): parse note-on/off for pitch + pacing.
	}
}

void tuh_midi_tx_cb(uint8_t dev_addr) { (void)dev_addr; }

// ---- Entry point ---------------------------------------------------------

int main()
{
	// Overclock to 192 MHz (default voltage) for DSP headroom. 192 = 48 x 4, an
	// integer multiple of the 48 MHz audio reference, so audio clock division
	// stays exact (no jitter); 200 MHz would not divide cleanly. Must run before
	// the card configures PWM/SPI. Same value goldfish/MLRws use with ComputerCard 0.3.0.
	set_sys_clock_khz(192000, true);

	// static: keep the card (with its ~16 KB cos LUT) off main()'s stack so it
	// cannot collide with core 1's stack near the top of RAM.
	static VoiceCard card;
	card.EnableNormalisationProbe();
	card.Run();
}
