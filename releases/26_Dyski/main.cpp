#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdint>
#include <cmath>

// ---------------------------------------------------------------------------
// Live image-analysis control card on core 0.
// The browser owns the image and analysis. The card only streams control state.
// ---------------------------------------------------------------------------

namespace
{
	constexpr uint8_t FRAME_KNOB_0 = 'K';
	constexpr uint8_t FRAME_KNOB_1 = 'N';
	constexpr uint8_t FRAME_CV_0 = 'C';
	constexpr uint8_t FRAME_CV_1 = 'V';
	constexpr uint8_t FRAME_RGB_0 = 'R';
	constexpr uint8_t FRAME_RGB_1 = 'B';
	constexpr uint8_t KNOB_FRAME_SIZE = 11;
	constexpr uint8_t CV_FRAME_SIZE = 8;
	constexpr uint32_t USB_TX_INTERVAL_MS = 40;

	// ---- Additive-synthesis constants ----
	constexpr int LUT_BITS = 10;
	constexpr int LUT_SIZE = 1 << LUT_BITS;        // 1024
	constexpr int PHASE_FRAC_BITS = 32 - LUT_BITS;  // 22
	constexpr int MAX_SYNTH_GRID = 4;                // up to 4×4 grid
	constexpr int MAX_OSCS = MAX_SYNTH_GRID * MAX_SYNTH_GRID; // 16 oscillators max
	constexpr uint32_t SYNTH_SR = 48000;
	// RGB frame: header(2) + grid_size(1) + slew(1) + cells(grid²×3) + chk(1)
	// Max frame: 4 + 25*3 + 1 = 80
	constexpr uint8_t MAX_RGB_FRAME_SIZE = 4 + MAX_OSCS * 3 + 1;

	// Tables computed once at startup
	static int16_t sine_lut[LUT_SIZE];
	static uint32_t freq_inc_table[256]; // R value → phase increment

	// Major scale quantization: maps each chromatic note to nearest major scale degree
	constexpr static uint8_t majorScale[12] = {0, 0, 2, 2, 4, 4, 5, 7, 7, 9, 9, 11};

	static void init_synth_tables()
	{
		constexpr float PI_F = 3.14159265358979f;
		for (int i = 0; i < LUT_SIZE; i++)
		{
			sine_lut[i] = static_cast<int16_t>(
				2047.0f * sinf(2.0f * PI_F * static_cast<float>(i) / LUT_SIZE));
		}
		// R 0..255 → 20 Hz – 10 kHz  (~9 octaves, exponential)
		// phase_inc = freq × 2^32 / 48000
		constexpr float INC_SCALE = 4294967296.0f / static_cast<float>(SYNTH_SR);
		constexpr float FREQ_LO = 20.0f;
		constexpr float FREQ_HI = 10000.0f;
		constexpr float OCT_RANGE = log2f(FREQ_HI / FREQ_LO); // ~8.96 octaves
		for (int i = 0; i < 256; i++)
		{
			float freq = FREQ_LO * powf(2.0f, static_cast<float>(i) * OCT_RANGE / 255.0f);
			freq_inc_table[i] = static_cast<uint32_t>(freq * INC_SCALE);
		}
	}

	static uint8_t frame_checksum(const uint8_t* bytes, uint8_t count)
	{
		uint8_t sum = 0;
		for (uint8_t i = 0; i < count; ++i)
		{
			sum ^= bytes[i];
		}
		return sum;
	}

	static int16_t decode_i16_le(const uint8_t* bytes)
	{
		return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8));
	}

	static int32_t clamp_u16(int32_t value)
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

	struct SharedState
	{
		volatile uint16_t knob_main = 0;
		volatile uint16_t knob_x = 0;
		volatile uint16_t knob_y = 0;
		volatile uint16_t mod_main = 0;
		volatile uint16_t mod_x = 0;
		volatile uint16_t mod_y = 0;
		volatile uint8_t switch_value = 0;
		volatile int32_t target_cv1 = 0;
		volatile int32_t target_cv2 = 0;
		volatile uint8_t pulse_flags = 0; // bit 0 = pulse1, bit 1 = pulse2

		// RGB grid for additive synthesis (written by core 0, read by core 1)
		uint8_t rgb_grid[MAX_OSCS * 3] = {};
		volatile uint8_t active_osc_count = 0;
		volatile uint8_t slew_rate = 64;  // 0 = instant, 255 = very slow
		volatile bool rgb_updated = false;
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
			note = octave * 12 + majorScale[degree];
			CVOut2MIDINote(static_cast<uint8_t>(note));
		}

		// ---- Additive synthesis → Audio Out 1 ----
		if (g_state.rgb_updated)
		{
			rgb_received_ = true;
			active_oscs_ = g_state.active_osc_count;
			// slew_rate 0..255 → shift 1..16 (0 = instant, 255 = very slow)
			slew_shift_ = 1 + ((static_cast<int>(g_state.slew_rate) * 15) >> 8);
			for (int i = 0; i < active_oscs_; i++)
			{
				uint8_t r = g_state.rgb_grid[i * 3];
				uint8_t g = g_state.rgb_grid[i * 3 + 1];
				uint8_t b = g_state.rgb_grid[i * 3 + 2];
				osc_target_inc_[i] = freq_inc_table[r];
				osc_target_amp_[i] = static_cast<int32_t>(g) << 8; // 16.8 fixed point
				osc_target_off_[i] = static_cast<uint32_t>(b) << 24; // B → phase offset
			}
			// Silence any oscillators beyond the active grid
			for (int i = active_oscs_; i < MAX_OSCS; i++)
			{
				osc_target_amp_[i] = 0;
			}
			g_state.rgb_updated = false;
		}

		// Slew oscillator parameters toward targets
		for (int i = 0; i < MAX_OSCS; i++)
		{
			int32_t inc_diff = static_cast<int32_t>(osc_target_inc_[i]) - static_cast<int32_t>(osc_cur_inc_[i]);
			if (inc_diff != 0)
			{
				int32_t step = inc_diff >> slew_shift_;
				if (step == 0) step = (inc_diff > 0) ? 1 : -1;
				osc_cur_inc_[i] = static_cast<uint32_t>(static_cast<int32_t>(osc_cur_inc_[i]) + step);
			}
			int32_t amp_diff = osc_target_amp_[i] - osc_cur_amp_[i];
			if (amp_diff != 0)
			{
				int32_t step = amp_diff >> slew_shift_;
				if (step == 0) step = (amp_diff > 0) ? 1 : -1;
				osc_cur_amp_[i] += step;
			}
			int32_t off_diff = static_cast<int32_t>(osc_target_off_[i]) - static_cast<int32_t>(osc_cur_off_[i]);
			if (off_diff != 0)
			{
				int32_t step = off_diff >> slew_shift_;
				if (step == 0) step = (off_diff > 0) ? 1 : -1;
				osc_cur_off_[i] = static_cast<uint32_t>(static_cast<int32_t>(osc_cur_off_[i]) + step);
			}
		}

		int32_t mix = 0;
		if (rgb_received_ && active_oscs_ > 0)
		{
			// Audio In 1 FM: modulate all oscillator frequencies when connected
			int32_t fm_mod = 0;
			if (Connected(Input::Audio1))
			{
				// AudioIn1 is -2048..2047; scale to a phase increment offset
				// At full amplitude, shift pitch by ~1 octave (double/halve freq)
				fm_mod = AudioIn1(); // raw bipolar sample
			}

			// Additive synth from RGB grid
			for (int i = 0; i < active_oscs_; i++)
			{
				uint32_t ph = osc_phase_[i] + osc_cur_off_[i];
				int16_t  s  = sine_lut[(ph >> PHASE_FRAC_BITS) & (LUT_SIZE - 1)];
				int32_t  amp = osc_cur_amp_[i] >> 8; // back to 0-255
				mix += (static_cast<int32_t>(s) * amp) >> 8;
				// Apply FM: scale phase increment by audio input
				int32_t inc = static_cast<int32_t>(osc_cur_inc_[i]);
				inc += (inc * fm_mod) >> 7;
				osc_phase_[i] += static_cast<uint32_t>(inc > 0 ? inc : 0);
			}
			mix /= active_oscs_; // normalize by active oscillator count
		}
		else
		{
			// Debug test tone: 440 Hz sine at full volume until first RGB frame
			int16_t s = sine_lut[(test_phase_ >> PHASE_FRAC_BITS) & (LUT_SIZE - 1)];
			mix = s;
			test_phase_ += freq_inc_table[128]; // ~440 Hz
		}
		AudioOut1(static_cast<int16_t>(clamp_cv(mix)));
		AudioOut2(static_cast<int16_t>(clamp_cv(mix)));

		// Raw knob positions
		const int32_t raw_main = KnobVal(Knob::Main);
		const int32_t raw_x = KnobVal(Knob::X);
		const int32_t raw_y = KnobVal(Knob::Y);
		g_state.knob_main = static_cast<uint16_t>(raw_main);
		g_state.knob_x = static_cast<uint16_t>(raw_x);
		g_state.knob_y = static_cast<uint16_t>(raw_y);

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
			// Scale to 0..4095, attenuate by knob (0..4095)
			mod_x = clamp_u16((cv_pos * 2 * raw_x) >> 12);
		}

		if (Connected(Input::CV2))
		{
			const int32_t cv2_in = CVIn2();
			const int32_t cv_pos = (cv2_in < 0) ? 0 : cv2_in;
			mod_y = clamp_u16((cv_pos * 2 * raw_y) >> 12);
		}

		// Audio In 2 modulates main knob: base ± audio (only when connected)
		int32_t mod_main = raw_main;
		if (Connected(Input::Audio2))
		{
			const int32_t audio2 = AudioIn2(); // -2048..2047
			mod_main = clamp_u16(raw_main + audio2);
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

		// Switch state (no-op for now, but still reported to JS)
		int s = SwitchVal();
		g_state.switch_value = static_cast<uint8_t>(s);

		// Pulse outputs: 200ms stretch from flags sent by JS
		uint8_t pf = g_state.pulse_flags;
		if (pf & 0x01) { pulse1_timer_ = PULSE_DURATION; g_state.pulse_flags &= ~0x01; }
		if (pf & 0x02) { pulse2_timer_ = PULSE_DURATION; g_state.pulse_flags &= ~0x02; }
		bool p1 = pulse1_timer_ > 0;
		bool p2 = pulse2_timer_ > 0;
		if (p1) pulse1_timer_--;
		if (p2) pulse2_timer_--;
		PulseOut1(p1);
		PulseOut2(p2);

		// LEDs show output state (positive values only, like Blackbird)
		// LED 0: Audio Out 1 (synth mix level)
		int32_t audio_brightness = (mix > 0) ? mix : -mix; // absolute value
		LedBrightness(0, static_cast<uint16_t>(audio_brightness < 4096 ? audio_brightness : 4095));
		// LED 1: Audio Out 2 (same as Audio Out 1)
		LedBrightness(1, static_cast<uint16_t>(audio_brightness < 4096 ? audio_brightness : 4095));
		// LED 2: CV Out 1 (positive only)
		LedBrightness(2, static_cast<uint16_t>(current_cv1_ > 0 ? (current_cv1_ < 2048 ? current_cv1_ * 2 : 4095) : 0));
		// LED 3: CV Out 2 (positive only)
		LedBrightness(3, static_cast<uint16_t>(current_cv2_ > 0 ? (current_cv2_ < 2048 ? current_cv2_ * 2 : 4095) : 0));
		// LED 4, 5: pulse outputs
		LedOn(4, p1);
		LedOn(5, p2);
	}

private:
	int32_t current_cv1_ = 0;
	int32_t current_cv2_ = 0;
	int32_t pulse_offset_x_ = 0;
	int32_t pulse_offset_y_ = 0;

	static constexpr int32_t PULSE_DURATION = 48000 / 50; // 20ms at 48kHz
	int32_t pulse1_timer_ = 0;
	int32_t pulse2_timer_ = 0;

	// Oscillator bank state
	uint32_t osc_phase_[MAX_OSCS] = {};
	uint32_t osc_target_inc_[MAX_OSCS] = {};
	uint32_t osc_cur_inc_[MAX_OSCS] = {};
	int32_t  osc_target_amp_[MAX_OSCS] = {};  // 16.8 fixed point
	int32_t  osc_cur_amp_[MAX_OSCS] = {};     // 16.8 fixed point
	uint32_t osc_target_off_[MAX_OSCS] = {};
	uint32_t osc_cur_off_[MAX_OSCS] = {};
	int active_oscs_ = 0;
	int slew_shift_ = 4;  // default moderate slew

	// Debug
	bool rgb_received_ = false;
	uint32_t test_phase_ = 0;
};

void core1_main()
{
	init_synth_tables();
	static LiveImageCard pt;
	pt.Run();
}

void apply_cv_frame(const uint8_t* frame)
{
	g_state.target_cv1 = decode_i16_le(&frame[2]);
	g_state.target_cv2 = decode_i16_le(&frame[4]);
	g_state.pulse_flags = frame[6];
}

void apply_rgb_frame(const uint8_t* frame)
{
	uint8_t grid = frame[2];
	uint8_t slew = frame[3];
	int count = grid * grid;
	// Zero full grid, then copy active cells
	for (int i = 0; i < MAX_OSCS * 3; i++)
		g_state.rgb_grid[i] = 0;
	for (int i = 0; i < count * 3; i++)
		g_state.rgb_grid[i] = frame[4 + i];
	g_state.active_osc_count = static_cast<uint8_t>(count);
	g_state.slew_rate = slew;
	g_state.rgb_updated = true;
}

void parse_rx_byte(uint8_t byte)
{
	static uint8_t rx_index = 0;
	static uint8_t rx_buffer[MAX_RGB_FRAME_SIZE] = {};  // sized for largest frame
	static uint8_t expected_size = 0;
	static bool is_rgb = false;

	if (rx_index == 0)
	{
		if (byte == FRAME_CV_0 || byte == FRAME_RGB_0)
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
			rx_buffer[1] = byte;
			expected_size = CV_FRAME_SIZE;
			is_rgb = false;
			rx_index = 2;
		}
		else if (rx_buffer[0] == FRAME_RGB_0 && byte == FRAME_RGB_1)
		{
			rx_buffer[1] = byte;
			expected_size = 0; // will be set when grid_size byte arrives
			is_rgb = true;
			rx_index = 2;
		}
		else
		{
			rx_index = 0;
			if (byte == FRAME_CV_0 || byte == FRAME_RGB_0)
			{
				rx_buffer[0] = byte;
				rx_index = 1;
			}
		}
		return;
	}

	rx_buffer[rx_index++] = byte;

	// For RGB frames, byte 2 is grid_size — compute expected frame length
	if (rx_index == 3 && is_rgb)
	{
		uint8_t grid = rx_buffer[2];
		if (grid < 1 || grid > MAX_SYNTH_GRID)
		{
			rx_index = 0;
			return;
		}
		expected_size = 4 + grid * grid * 3 + 1;
	}

	if (expected_size > 0 && rx_index == expected_size)
	{
		if (rx_buffer[expected_size - 1] == frame_checksum(rx_buffer, expected_size - 1))
		{
			if (!is_rgb)
				apply_cv_frame(rx_buffer);
			else
				apply_rgb_frame(rx_buffer);
		}
		rx_index = 0;
	}
}

void consume_rx_bytes()
{
	int c;
	while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT)
	{
		parse_rx_byte(static_cast<uint8_t>(c));
	}
}

void send_knob_frame()
{
	if (!stdio_usb_connected())
	{
		return;
	}

	static uint8_t tx_sequence = 0;
	const uint16_t knob_main = g_state.mod_main;
	const uint16_t knob_x = g_state.mod_x;
	const uint16_t knob_y = g_state.mod_y;
	const uint8_t switch_value = g_state.switch_value;
	uint8_t frame[KNOB_FRAME_SIZE] = {
		FRAME_KNOB_0,
		FRAME_KNOB_1,
		static_cast<uint8_t>(knob_main & 0xFFu),
		static_cast<uint8_t>((knob_main >> 8) & 0xFFu),
		static_cast<uint8_t>(knob_x & 0xFFu),
		static_cast<uint8_t>((knob_x >> 8) & 0xFFu),
		static_cast<uint8_t>(knob_y & 0xFFu),
		static_cast<uint8_t>((knob_y >> 8) & 0xFFu),
		switch_value,
		tx_sequence++,
		0,
	};
	frame[KNOB_FRAME_SIZE - 1] = frame_checksum(frame, KNOB_FRAME_SIZE - 1);

	for (uint8_t byte : frame)
	{
		putchar_raw(byte);
	}
}

int main()
{
	set_sys_clock_khz(200000, true);
	stdio_init_all();
	sleep_ms(1500);
	multicore_launch_core1(core1_main);

	while (true)
	{
		consume_rx_bytes();
		send_knob_frame();
		sleep_ms(USB_TX_INTERVAL_MS);
	}
}

  
