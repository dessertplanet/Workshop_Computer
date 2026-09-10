// SSI-263 Voice — a polyphonic formant speech synthesizer for the Music Thing
// Modular Workshop System Computer, built on the clean-room Casso SSI-263A
// engine (Ssi263.h / Ssi263.cpp).
//
// See rp2040-voice-port-design.md for the full design. This file is the
// Phase 0 scaffold: ComputerCard wiring, boot-selected USB device/host role,
// and the six-slot phrase-bank panel controls. The engine is vendored and
// linked but not yet driven from MIDI (Phases 1-3).

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

#include "Ssi263.h"

// Number of phrase slots in the bank (one per panel LED).
static constexpr int kNumSlots = 6;

// Boot mute, in samples: silence while the DAC settles and core 1 brings USB
// up, so the card does not click on power-up.
static constexpr int32_t kBootMute = 24000; // 0.5 s at 48 kHz

class VoiceCard : public ComputerCard
{
public:
	VoiceCard()
	{
		activeSlot_ = 0;
		slotArmRaw_ = -1;
		bootCounter_ = 0;

		powerState_ = Unsupported;
		isUSBMIDIHost_ = false;

		// ComputerCard runs the audio callback at a fixed 48 kHz.
		engine_.SetSampleRate(48000);

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

	// ---- Core 0: 48 kHz audio + panel ------------------------------------

	virtual void ProcessSample() override
	{
		UpdateSlotSelect();

		for (int i = 0; i < kNumSlots; i++)
			LedOn(i, i == activeSlot_);

		// TODO (Phase 1): drive engine_ from MIDI and write its output here.
		// Silent during boot mute and until the engine is wired up.
		if (bootCounter_ < kBootMute)
			bootCounter_++;

		AudioOut1(0);
		AudioOut2(0);
	}

	// MIDI host device address, set by the rppicomidi mount callback below.
	static uint8_t midiDevAddr;

private:
	// Quantize the Main knob (0..4095) into six slots, with edge hysteresis so
	// a knob parked on a boundary does not flicker between phrases.
	void UpdateSlotSelect()
	{
		constexpr int band = 4096 / kNumSlots; // ~682
		constexpr int hyst = band / 6;
		int raw = KnobVal(Main);

		int lo = activeSlot_ * band - hyst;
		int hi = (activeSlot_ + 1) * band + hyst;
		if (raw >= lo && raw < hi)
			return; // still within the active slot's band (+ hysteresis)

		int slot = raw / band;
		if (slot < 0) slot = 0;
		if (slot >= kNumSlots) slot = kNumSlots - 1;
		activeSlot_ = slot;
	}

	Ssi263 engine_;

	int activeSlot_;
	int slotArmRaw_;
	int32_t bootCounter_;

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
	VoiceCard card;
	card.EnableNormalisationProbe();
	card.Run();
}
