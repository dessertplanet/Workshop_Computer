// SSI-263 Voice — a polyphonic formant speech synthesizer for the Music Thing
// Modular Workshop System Computer, built on the clean-room Casso SSI-263A
// engine, ported to a fixed-point vocoder (SsiVoice.h).
//
// See rp2040-voice-port-design.md for the full design. This build is the first
// hardware bring-up: it plays a hard-coded "Daisy Bell" score (daisy_demo.h) on
// loop so the synthesis can be heard on-device. MIDI note/pacing and the SysEx
// phrase bank come next (Phase 3+); the USB role detection is already wired.

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

// Duration/mode bits written into the phoneme register: mode 2 keeps A/R
// active so a phoneme that expires can be re-triggered to sustain the syllable.
static constexpr uint8_t kDurBits = SsiVoice::kModePhonemeImmediate;

static inline float MidiToHz(uint8_t note)
{
	return 440.0f * std::pow(2.0f, (static_cast<int>(note) - 69) / 12.0f);
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

		// The ComputerCard callback is configured for 24 kHz in ComputerCard.h.
		engine_.SetSampleRate(24000);
		engine_.SetTickClock(24000);

		// Power the chip up: a benign pause phoneme with the mode bits, then
		// CTL low (bit 7 = 0) with mid articulation and full amplitude, which
		// latches the mode and starts playback. The sequencer takes over on
		// the first ProcessSample.
		engine_.WriteRegister(SsiVoice::kRegDurationPhoneme, (kDurBits << 6) | 0x00);
		engine_.WriteRegister(SsiVoice::kRegCtlArtAmp, (4 << 4) | 0x0F);

		// Core 1 owns the USB stack; core 0 runs the audio ISR via Run().
		multicore_launch_core1(Core1Entry);
	}

	// ---- Core 1: USB (device or host, chosen at power-on) ----------------

	static void Core1Entry() { static_cast<VoiceCard *>(ThisPtr())->USBCore(); }

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
				uint8_t packet[64];
				while (tud_midi_available())
				{
					tud_midi_stream_read(packet, sizeof(packet));
					// TODO (Phase 3): parse note-on/off (pitch + pacing) and
					// SysEx (phrase-bank load / mode).
				}
			}
		}
	}

	// ---- Core 0: 24 kHz audio + panel ------------------------------------

	virtual void ProcessSample() override
	{
		uint32_t startUs = timer_hw->timerawl;

		// Half a second of silence at power-up while the DAC settles.
		if (bootCounter_ < kBootMute)
		{
			bootCounter_++;
			AudioOut1(0);
			AudioOut2(0);
			RecordProcessSampleTiming(startUs);
			return;
		}

		if (segSamplesLeft_ == 0)
			AdvanceSegment();
		segSamplesLeft_--;

		// A phoneme whose own duration has expired is re-triggered so the
		// syllable sustains for the whole score segment.
		if (engine_.IsRequesting())
			engine_.WriteRegister(SsiVoice::kRegDurationPhoneme, curReg0_);

		int32_t sQ = engine_.GenerateSample();
		engine_.Tick(1);

		// Single-voice peaks reach ~0.5; 3200 uses the range with headroom.
		int32_t out = sQ >> 13;
		if (out > 2047) out = 2047;
		if (out < -2047) out = -2047;

		// Simple visual: one LED walks with the syllable.
		for (int i = 0; i < kNumSlots; i++)
			LedOn(i, i == (segIndex_ % kNumSlots));

		AudioOut1(static_cast<int16_t>(out));
		AudioOut2(static_cast<int16_t>(out));
		RecordProcessSampleTiming(startUs);
	}

	// MIDI host device address, set by the rppicomidi mount callback below.
	static uint8_t midiDevAddr;

private:
	void AdvanceSegment()
	{
		const DaisySeg &seg = kDaisy[segIndex_];
		segIndex_ = (segIndex_ + 1) % kDaisyLen;

		engine_.SetVoicePitch(0, MidiToHz(seg.note));
		curReg0_ = static_cast<uint8_t>((kDurBits << 6) | seg.phoneme);
		engine_.WriteRegister(SsiVoice::kRegDurationPhoneme, curReg0_);
		segSamplesLeft_ = static_cast<int32_t>(seg.durMs) * 24; // ms -> 24 kHz samples
	}

	SsiVoice engine_;

	int32_t bootCounter_;

	int      segIndex_;
	int32_t  segSamplesLeft_;
	uint8_t  curReg0_;

	volatile USBPowerState_t powerState_;
	bool isUSBMIDIHost_;
};

uint8_t VoiceCard::midiDevAddr = 0;

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
