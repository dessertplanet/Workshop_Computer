#include "ComputerCard.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdint>

// ---------------------------------------------------------------------------
// Dyski: live image-analysis control card + web radio audio output.
//
// The browser owns the image, the analysis and the radio. The card:
//   - streams knob/switch state ('KN') and raw CV inputs ('CI') to the browser
//   - receives CV/pulse targets ('CV') from the image analysis
//   - receives radio audio ('AU') and plays it on Audio Out 1 and 2
//
// Core 0: USB serial I/O.  Core 1: 48 kHz ProcessSample.
// ---------------------------------------------------------------------------

namespace
{
	// Frames sent to the browser
	constexpr uint8_t FRAME_KNOB_0 = 'K';
	constexpr uint8_t FRAME_KNOB_1 = 'N';
	constexpr uint8_t FRAME_CVIN_0 = 'C';
	constexpr uint8_t FRAME_CVIN_1 = 'I';
	constexpr uint8_t KNOB_FRAME_SIZE = 11;
	constexpr uint8_t CVIN_FRAME_SIZE = 9;

	// Frames received from the browser
	constexpr uint8_t FRAME_CV_0 = 'C';
	constexpr uint8_t FRAME_CV_1 = 'V';
	constexpr uint8_t FRAME_AUDIO_0 = 'A';
	constexpr uint8_t FRAME_AUDIO_1 = 'U';
	constexpr uint8_t CV_FRAME_SIZE = 8;
	// Audio frame: 'A' 'U' count rate_lo rate_hi samples(count × int16 LE) chk
	constexpr uint32_t AUDIO_HEADER_SIZE = 5;
	constexpr uint32_t MAX_AUDIO_SAMPLES = 64;
	constexpr uint32_t MAX_RX_FRAME_SIZE = AUDIO_HEADER_SIZE + MAX_AUDIO_SAMPLES * 2 + 1; // 134

	constexpr uint32_t TX_INTERVAL_US = 10000; // control frames to the browser every 10 ms

	// ---- Radio audio ring buffer (core 0 writes, core 1 reads) ----
	constexpr uint32_t OUTPUT_RATE = 48000;
	constexpr uint32_t RING_SIZE = 8192; // power of two; ~680 ms at 12 kHz
	constexpr uint32_t RING_MASK = RING_SIZE - 1;
	// Playback holds about this much audio buffered, to ride out USB/network jitter.
	constexpr uint32_t TARGET_FILL_MS = 120;

	int16_t g_ring[RING_SIZE];
	volatile uint32_t g_ring_write = 0;     // only core 0 writes
	volatile uint32_t g_ring_read = 0;      // only core 1 writes
	volatile uint32_t g_audio_rate = 12000; // Hz of incoming radio audio
	volatile uint32_t g_underruns = 0;      // times playback ran dry (diagnostics, read via SWD)

	static uint8_t frame_checksum(const uint8_t* bytes, uint32_t count)
	{
		uint8_t sum = 0;
		for (uint32_t i = 0; i < count; ++i)
		{
			sum ^= bytes[i];
		}
		return sum;
	}

	static int16_t decode_i16_le(const uint8_t* bytes)
	{
		return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8));
	}

	static int32_t clamp_u12(int32_t value)
	{
		if (value < 0) value = 0;
		if (value > 4095) value = 4095;
		return value;
	}

	static int16_t clamp_cv(int32_t value)
	{
		if (value < -2048) value = -2048;
		if (value > 2047) value = 2047;
		return static_cast<int16_t>(value);
	}

	// Major scale quantization: maps each chromatic note to nearest major scale degree
	constexpr uint8_t kMajorScale[12] = {0, 0, 2, 2, 4, 4, 5, 7, 7, 9, 9, 11};

	struct SharedState
	{
		volatile uint16_t mod_main = 0;
		volatile uint16_t mod_x = 0;
		volatile uint16_t mod_y = 0;
		volatile uint8_t switch_value = 0;
		volatile int16_t cv_in1 = 0;
		volatile int16_t cv_in2 = 0;
		volatile uint8_t pulse_in = 0; // bit 0 = pulse in 1, bit 1 = pulse in 2
		volatile int32_t target_cv1 = 0;
		volatile int32_t target_cv2 = 0;
		volatile uint8_t pulse_flags = 0; // bit 0 = pulse1, bit 1 = pulse2
	};

	constexpr int32_t PULSE_STEP = 512; // 4096 / 8 = 8 steps across full range

	SharedState g_state;
}

class LiveImageCard : public ComputerCard
{
public:
	LiveImageCard()
	{
		EnableNormalisationProbe();
	}

	virtual void ProcessSample()
	{
		const int32_t target_cv1 = g_state.target_cv1;
		const int32_t target_cv2 = g_state.target_cv2;

		current_cv1_ += (target_cv1 - current_cv1_) >> 4;
		CVOut1(clamp_cv(current_cv1_));

		// Quantize CV2 to nearest major scale note (0-127)
		current_cv2_ += (target_cv2 - current_cv2_) >> 4;
		{
			// Map -2048..2047 → MIDI note range
			int32_t note = ((current_cv2_ + 2048) * 127) >> 12;
			if (note < 0) note = 0;
			if (note > 127) note = 127;
			// Snap to major scale
			int32_t octave = note / 12;
			int32_t degree = note % 12;
			note = octave * 12 + kMajorScale[degree];
			CVOut2MIDINote(static_cast<uint8_t>(note));
		}

		// ---- Radio audio → Audio Out 1 and 2 ----
		const int32_t radio = NextRadioSample();
		AudioOut1(clamp_cv(radio));
		AudioOut2(clamp_cv(radio));

		// Raw knob positions
		const int32_t raw_main = KnobVal(Knob::Main);
		const int32_t raw_x = KnobVal(Knob::X);
		const int32_t raw_y = KnobVal(Knob::Y);

		// Raw CV/pulse inputs, reported to the browser as-is
		g_state.cv_in1 = static_cast<int16_t>(CVIn1());
		g_state.cv_in2 = static_cast<int16_t>(CVIn2());
		g_state.pulse_in = static_cast<uint8_t>((PulseIn1() ? 1 : 0) | (PulseIn2() ? 2 : 0));

		// CV modulation: knobs X/Y attenuate CV1/CV2 for position
		// CV positive range (0..2047) maps to full 0..4095 image range
		// Knob scales the CV: knob at max = full CV, knob at 0 = no CV
		// Only apply when a jack is connected; otherwise use raw knob
		int32_t mod_x = raw_x;
		int32_t mod_y = raw_y;

		if (Connected(Input::CV1))
		{
			const int32_t cv1_in = CVIn1();  // -2048..2047
			const int32_t cv_pos = (cv1_in < 0) ? 0 : cv1_in; // 0..2047
			mod_x = clamp_u12((cv_pos * 2 * raw_x) >> 12);
		}

		if (Connected(Input::CV2))
		{
			const int32_t cv2_in = CVIn2();
			const int32_t cv_pos = (cv2_in < 0) ? 0 : cv2_in;
			mod_y = clamp_u12((cv_pos * 2 * raw_y) >> 12);
		}

		// Audio In 2 modulates main knob: base ± audio (only when connected)
		int32_t mod_main = raw_main;
		if (Connected(Input::Audio2))
		{
			const int32_t audio2 = AudioIn2(); // -2048..2047
			mod_main = clamp_u12(raw_main + audio2);
		}

		// Pulse inputs: rising edge advances position, wrapping at 4096
		// Step size scaled by knob X/Y (0..4095 → 0..PULSE_STEP)
		// Reset offsets when pulse jacks are disconnected
		if (Disconnected(Input::Pulse1))
		{
			pulse_offset_x_ = 0;
		}
		else if (PulseIn1RisingEdge())
		{
			const int32_t step_x = 1 + ((PULSE_STEP * raw_x) >> 12);
			pulse_offset_x_ = (pulse_offset_x_ + step_x) % 4096;
		}

		if (Disconnected(Input::Pulse2))
		{
			pulse_offset_y_ = 0;
		}
		else if (PulseIn2RisingEdge())
		{
			const int32_t step_y = 1 + ((PULSE_STEP * raw_y) >> 12);
			pulse_offset_y_ = (pulse_offset_y_ + step_y) % 4096;
		}

		// Apply pulse offsets with wrapping
		mod_x = (mod_x + pulse_offset_x_) % 4096;
		mod_y = (mod_y + pulse_offset_y_) % 4096;

		g_state.mod_main = static_cast<uint16_t>(mod_main);
		g_state.mod_x = static_cast<uint16_t>(mod_x);
		g_state.mod_y = static_cast<uint16_t>(mod_y);

		// Switch state (reported to JS, selects analysis mode there)
		g_state.switch_value = static_cast<uint8_t>(SwitchVal());

		// Pulse outputs: stretched from flags sent by JS
		uint8_t pf = g_state.pulse_flags;
		if (pf & 0x01) { pulse1_timer_ = PULSE_DURATION; g_state.pulse_flags &= ~0x01; }
		if (pf & 0x02) { pulse2_timer_ = PULSE_DURATION; g_state.pulse_flags &= ~0x02; }
		bool p1 = pulse1_timer_ > 0;
		bool p2 = pulse2_timer_ > 0;
		if (p1) pulse1_timer_--;
		if (p2) pulse2_timer_--;
		PulseOut1(p1);
		PulseOut2(p2);

		// LEDs 0/1: radio level (peak hold with slow decay so speech is visible)
		const int32_t level = radio < 0 ? -radio : radio;
		if (level > level_) level_ = level;
		else if (level_ > 0) level_ -= (level_ >> 11) + 1;
		const uint16_t level_led = static_cast<uint16_t>(level_ * 2 < 4096 ? level_ * 2 : 4095);
		LedBrightness(0, level_led);
		LedBrightness(1, level_led);
		// LED 2: CV Out 1 (positive only)
		LedBrightness(2, static_cast<uint16_t>(current_cv1_ > 0 ? (current_cv1_ < 2048 ? current_cv1_ * 2 : 4095) : 0));
		// LED 3: CV Out 2 (positive only)
		LedBrightness(3, static_cast<uint16_t>(current_cv2_ > 0 ? (current_cv2_ < 2048 ? current_cv2_ * 2 : 4095) : 0));
		// LED 4, 5: pulse outputs
		LedOn(4, p1);
		LedOn(5, p2);
	}

private:
	// Pull the next 48 kHz output sample from the radio ring buffer.
	// Linear interpolation with a fractional read position; the step is nudged
	// by up to ±2% to hold the buffer near its target, which absorbs the clock
	// difference between the receiver, the browser and this card.
	int32_t NextRadioSample()
	{
		const uint32_t rate = g_audio_rate;
		const uint32_t write = g_ring_write;
		__dmb(); // see ring contents written before g_ring_write
		uint32_t read = g_ring_read;
		const int32_t fill = static_cast<int32_t>(write - read);
		const int32_t target = static_cast<int32_t>((rate * TARGET_FILL_MS) / 1000);

		if (priming_)
		{
			if (fill < target)
			{
				return Ramp(0, false);
			}
			priming_ = false;
			fill_avg_q8_ = fill << 8;
		}
		if (fill < 2)
		{
			priming_ = true; // underrun: go quiet until the buffer refills
			g_underruns = g_underruns + 1;
			return Ramp(0, false);
		}

		const int32_t a = g_ring[read & RING_MASK];
		const int32_t b = g_ring[(read + 1) & RING_MASK];
		// 16-bit input → 12-bit DAC range
		const int32_t sample = (a + (((b - a) * static_cast<int32_t>(frac_)) >> 16)) >> 4;

		// The fill level jumps by a whole packet each time one arrives. Smooth it
		// (~340 ms time constant) before it steers the playback rate; reacting to
		// that sawtooth directly wobbles the pitch at the packet rate (flutter).
		fill_avg_q8_ += ((fill << 8) - fill_avg_q8_) >> 14;

		// step = rate / 48000 in 16.16, adjusted by up to ±2% for buffer error
		const int32_t nominal = static_cast<int32_t>((static_cast<uint64_t>(rate) << 16) / OUTPUT_RATE);
		int32_t err = (fill_avg_q8_ >> 8) - target;
		if (err > target) err = target;
		if (err < -target) err = -target;
		const int32_t step = nominal + ((nominal / 50) * err) / (target > 0 ? target : 1);

		frac_ += static_cast<uint32_t>(step);
		read += frac_ >> 16;
		frac_ &= 0xFFFFu;
		g_ring_read = read;

		return Ramp(sample, true);
	}

	// ~5 ms fade in when playback (re)starts, so stream start doesn't click.
	int32_t Ramp(int32_t sample, bool playing)
	{
		if (!playing)
		{
			gain_ = 0;
			return 0;
		}
		if (gain_ < 256) ++gain_;
		return (sample * gain_) >> 8;
	}

	static constexpr int32_t PULSE_DURATION = 48000 / 50; // 20ms at 48kHz

	int32_t current_cv1_ = 0;
	int32_t current_cv2_ = 0;
	int32_t pulse_offset_x_ = 0;
	int32_t pulse_offset_y_ = 0;
	int32_t pulse1_timer_ = 0;
	int32_t pulse2_timer_ = 0;

	// Radio playback state
	uint32_t frac_ = 0;
	bool priming_ = true;
	int32_t fill_avg_q8_ = 0; // smoothed buffer fill, 24.8 fixed point
	int32_t gain_ = 0;
	int32_t level_ = 0;
};

void core1_main()
{
	static LiveImageCard card;
	card.Run();
}

void apply_cv_frame(const uint8_t* frame)
{
	g_state.target_cv1 = decode_i16_le(&frame[2]);
	g_state.target_cv2 = decode_i16_le(&frame[4]);
	g_state.pulse_flags = frame[6];
}

void apply_audio_frame(const uint8_t* frame)
{
	const uint32_t count = frame[2];
	const uint32_t rate = static_cast<uint32_t>(frame[3]) | (static_cast<uint32_t>(frame[4]) << 8);
	if (rate >= 4000 && rate <= 48000)
	{
		g_audio_rate = rate;
	}

	uint32_t write = g_ring_write;
	const uint32_t fill = write - g_ring_read;
	if (fill + count >= RING_SIZE)
	{
		return; // reader has stalled; drop rather than overwrite unread audio
	}
	for (uint32_t i = 0; i < count; ++i)
	{
		g_ring[write & RING_MASK] = decode_i16_le(&frame[AUDIO_HEADER_SIZE + i * 2]);
		++write;
	}
	__dmb(); // publish samples before the new write index
	g_ring_write = write;
}

uint32_t rx_index = 0; // position within the frame being received

void parse_rx_byte(uint8_t byte)
{
	static uint8_t rx_buffer[MAX_RX_FRAME_SIZE] = {};
	static uint32_t expected_size = 0;

	auto is_header_start = [](uint8_t b) { return b == FRAME_CV_0 || b == FRAME_AUDIO_0; };

	if (rx_index == 0)
	{
		if (is_header_start(byte))
		{
			rx_buffer[0] = byte;
			rx_index = 1;
		}
		return;
	}

	if (rx_index == 1)
	{
		if (rx_buffer[0] == FRAME_CV_0 && byte == FRAME_CV_1)
		{
			expected_size = CV_FRAME_SIZE;
		}
		else if (rx_buffer[0] == FRAME_AUDIO_0 && byte == FRAME_AUDIO_1)
		{
			expected_size = 0; // set once the sample count arrives
		}
		else
		{
			rx_index = 0;
			if (is_header_start(byte))
			{
				rx_buffer[0] = byte;
				rx_index = 1;
			}
			return;
		}
		rx_buffer[1] = byte;
		rx_index = 2;
		return;
	}

	rx_buffer[rx_index++] = byte;

	// Audio frames: byte 2 is the sample count, which fixes the frame length
	if (rx_index == 3 && rx_buffer[0] == FRAME_AUDIO_0)
	{
		const uint32_t count = rx_buffer[2];
		if (count < 1 || count > MAX_AUDIO_SAMPLES)
		{
			rx_index = 0;
			return;
		}
		expected_size = AUDIO_HEADER_SIZE + count * 2 + 1;
	}

	if (expected_size > 0 && rx_index == expected_size)
	{
		if (rx_buffer[expected_size - 1] == frame_checksum(rx_buffer, expected_size - 1))
		{
			if (rx_buffer[0] == FRAME_AUDIO_0)
				apply_audio_frame(rx_buffer);
			else
				apply_cv_frame(rx_buffer);
		}
		rx_index = 0;
	}
}

void consume_rx_bytes()
{
	// Frames arrive as one burst, so a long gap mid-frame means the sender went
	// away (e.g. the page disconnected); drop the partial frame so the next
	// sender's first frame isn't swallowed as its tail.
	static uint32_t last_byte_us = 0;
	int c;
	while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT)
	{
		const uint32_t now = time_us_32();
		if (rx_index != 0 && now - last_byte_us > 20000)
		{
			rx_index = 0;
		}
		last_byte_us = now;
		parse_rx_byte(static_cast<uint8_t>(c));
	}
}

void write_frame(const uint8_t* frame, uint32_t size)
{
	for (uint32_t i = 0; i < size; ++i)
	{
		putchar_raw(frame[i]);
	}
}

void send_control_frames()
{
	if (!stdio_usb_connected())
	{
		return;
	}

	static uint8_t knob_sequence = 0;
	static uint8_t cv_sequence = 0;

	const uint16_t knob_main = g_state.mod_main;
	const uint16_t knob_x = g_state.mod_x;
	const uint16_t knob_y = g_state.mod_y;
	uint8_t knob[KNOB_FRAME_SIZE] = {
		FRAME_KNOB_0,
		FRAME_KNOB_1,
		static_cast<uint8_t>(knob_main & 0xFFu),
		static_cast<uint8_t>((knob_main >> 8) & 0xFFu),
		static_cast<uint8_t>(knob_x & 0xFFu),
		static_cast<uint8_t>((knob_x >> 8) & 0xFFu),
		static_cast<uint8_t>(knob_y & 0xFFu),
		static_cast<uint8_t>((knob_y >> 8) & 0xFFu),
		g_state.switch_value,
		knob_sequence++,
		0,
	};
	knob[KNOB_FRAME_SIZE - 1] = frame_checksum(knob, KNOB_FRAME_SIZE - 1);
	write_frame(knob, KNOB_FRAME_SIZE);

	const uint16_t cv1 = static_cast<uint16_t>(g_state.cv_in1);
	const uint16_t cv2 = static_cast<uint16_t>(g_state.cv_in2);
	uint8_t cvin[CVIN_FRAME_SIZE] = {
		FRAME_CVIN_0,
		FRAME_CVIN_1,
		static_cast<uint8_t>(cv1 & 0xFFu),
		static_cast<uint8_t>((cv1 >> 8) & 0xFFu),
		static_cast<uint8_t>(cv2 & 0xFFu),
		static_cast<uint8_t>((cv2 >> 8) & 0xFFu),
		g_state.pulse_in,
		cv_sequence++,
		0,
	};
	cvin[CVIN_FRAME_SIZE - 1] = frame_checksum(cvin, CVIN_FRAME_SIZE - 1);
	write_frame(cvin, CVIN_FRAME_SIZE);
}

int main()
{
	set_sys_clock_khz(200000, true);
	stdio_init_all();
	sleep_ms(1500);
	multicore_launch_core1(core1_main);

	// Poll serial input continuously (radio audio arrives at ~24 KB/s) and
	// send control frames on a fixed interval.
	uint32_t next_tx = time_us_32();
	while (true)
	{
		consume_rx_bytes();
		const uint32_t now = time_us_32();
		if (static_cast<int32_t>(now - next_tx) >= 0)
		{
			send_control_frames();
			next_tx = now + TX_INTERVAL_US;
		}
		sleep_us(200);
	}
}
