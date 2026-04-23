/**
 * MonomeGrid.h — Friendly C++ wrapper for monome_mext.
 *
 * Provides readable methods for common grid interaction patterns.
 * Include this instead of (or alongside) monome_mext.h in your card.
 *
 * Usage:
 *   MonomeGrid grid;          // in your card class
 *   grid.begin();             // in constructor (calls mext_init + launches core 1)
 *   grid.poll();              // at top of ProcessSample()
 *   grid.led(x, y, 15);      // set an LED
 *   if (grid.keyDown())  ...  // check for key events
 */

#ifndef MONOME_GRID_HPP
#define MONOME_GRID_HPP

#include "pico/multicore.h"
#include "bsp/board.h"
#include "tusb.h"

extern "C" {
#include "monome_mext.h"
}

class MonomeGrid
{
public:
	MonomeGrid()
	{
		clear();
	}

	/* ---- lifecycle ---- */

	/** Initialise mext and launch USB host on core 1. */
	void begin()
	{
		mext_init(MEXT_TRANSPORT_HOST, 0);
		multicore_launch_core1(usb_core);
	}

	/** True once the grid has reported its dimensions. */
	bool ready() const { return mext_grid_ready(); }

	/** True if a grid/arc is physically connected. */
	bool connected() const { return g_mext.connected; }

	/** Grid width in columns (0 before discovery). */
	uint8_t cols() const { return g_mext.grid_x; }

	/** Grid height in rows (0 before discovery). */
	uint8_t rows() const { return g_mext.grid_y; }

	/* ---- events ---- */

	/**
	 * Drain the event queue.  Call once at the top of ProcessSample().
	 * After calling, use keyDown()/keyUp()/keyHeld()/lastKey() to
	 * inspect what happened this sample.
	 */
	void poll()
	{
		key_down_this_sample = false;
		key_up_this_sample   = false;

		mext_event_t ev;
		while (mext_event_pop(&ev)) {
			if (ev.type == MEXT_EVENT_GRID_KEY) {
				last_event_x = ev.grid.x;
				last_event_y = ev.grid.y;

				if (ev.grid.z) {
					key_down_this_sample = true;
					any_key_held_count++;
					if (ev.grid.x < MEXT_MAX_GRID_X && ev.grid.y < MEXT_MAX_GRID_Y)
						held_state[ev.grid.y][ev.grid.x] = true;
				} else {
					key_up_this_sample = true;
					if (any_key_held_count > 0) any_key_held_count--;
					if (ev.grid.x < MEXT_MAX_GRID_X && ev.grid.y < MEXT_MAX_GRID_Y)
						held_state[ev.grid.y][ev.grid.x] = false;
				}
			}
		}

		submitFrameIfPossible();
	}

	/** True if any key was pressed down this sample. */
	bool keyDown() const { return key_down_this_sample; }

	/** True if any key was released this sample. */
	bool keyUp() const { return key_up_this_sample; }

	/** True if at least one key is currently held. */
	bool anyHeld() const { return any_key_held_count > 0; }

	/** True if the key at (x, y) is currently held down. */
	bool held(uint8_t x, uint8_t y) const
	{
		if (x >= MEXT_MAX_GRID_X || y >= MEXT_MAX_GRID_Y) return false;
		return held_state[y][x];
	}

	/** X coordinate of the most recent key event. */
	uint8_t lastX() const { return last_event_x; }

	/** Y coordinate of the most recent key event. */
	uint8_t lastY() const { return last_event_y; }

	/* ---- LEDs ---- */

	/** Set a single LED brightness (0–15). */
	void led(uint8_t x, uint8_t y, uint8_t level)
	{
		if (x >= MEXT_MAX_GRID_X || y >= MEXT_MAX_GRID_Y) return;
		if (level > 15) level = 15;
		frame_[y * MEXT_MAX_GRID_X + x] = level;
		frame_dirty_ = true;
	}

	/** Turn a single LED fully on. */
	void ledOn(uint8_t x, uint8_t y) { led(x, y, 15); }

	/** Turn a single LED off. */
	void ledOff(uint8_t x, uint8_t y) { led(x, y, 0); }

	/** Toggle an LED between off and full brightness. */
	void ledToggle(uint8_t x, uint8_t y)
	{
		if (x >= MEXT_MAX_GRID_X || y >= MEXT_MAX_GRID_Y) return;
		uint8_t cur = frame_[y * MEXT_MAX_GRID_X + x];
		led(x, y, cur ? 0 : 15);
	}

	/** Set LED on/off based on a boolean. */
	void ledSet(uint8_t x, uint8_t y, bool on)
	{
		led(x, y, on ? 15 : 0);
	}

	/** Set all LEDs to a level (0–15). */
	void all(uint8_t level)
	{
		if (level > 15) level = 15;
		for (int i = 0; i < MEXT_MAX_GRID_X * MEXT_MAX_GRID_Y; i++)
			frame_[i] = level;
		frame_dirty_ = true;
	}

	/** Turn all LEDs off. */
	void clear() { all(0); }

	/** Fill a rectangular region with a level. */
	void fill(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t level)
	{
		for (uint8_t y = y0; y <= y1 && y < MEXT_MAX_GRID_Y; y++)
			for (uint8_t x = x0; x <= x1 && x < MEXT_MAX_GRID_X; x++)
				led(x, y, level);
	}

	/** Fill a row with a level. */
	void row(uint8_t y, uint8_t level)
	{
		uint8_t w = cols() > 0 ? cols() : MEXT_MAX_GRID_X;
		for (uint8_t x = 0; x < w; x++)
			led(x, y, level);
	}

	/** Fill a column with a level. */
	void col(uint8_t x, uint8_t level)
	{
		uint8_t h = rows() > 0 ? rows() : MEXT_MAX_GRID_Y;
		for (uint8_t y = 0; y < h; y++)
			led(x, y, level);
	}

	/** Set global LED intensity (0–15). */
	void intensity(uint8_t level) { mext_grid_led_intensity(level); }

	/** Read back the current LED level at (x, y). */
	uint8_t ledGet(uint8_t x, uint8_t y) const
	{
		if (x >= MEXT_MAX_GRID_X || y >= MEXT_MAX_GRID_Y) return 0;
		return frame_[y * MEXT_MAX_GRID_X + x];
	}

	/** Light LEDs under all currently held keys. */
	void showHeld(uint8_t level = 15)
	{
		if (!ready()) return;
		for (uint8_t y = 0; y < rows(); y++)
			for (uint8_t x = 0; x < cols(); x++)
				if (held_state[y][x])
					led(x, y, level);
	}

	/* ---- direct buffer access (advanced) ---- */

	/** Raw pointer to the LED buffer (MEXT_MAX_GRID_X * MEXT_MAX_GRID_Y). */
	uint8_t *ledBuffer()
	{
		frame_dirty_ = true;
		return frame_;
	}

	/** Mark all quadrants dirty (call after directly writing ledBuffer). */
	void markDirty()
	{
		frame_dirty_ = true;
		submitFrameIfPossible();
	}

private:
	void submitFrameIfPossible()
	{
		if (!frame_dirty_)
			return;
		if (mext_grid_frame_submit(frame_))
			frame_dirty_ = false;
	}

	static void usb_core()
	{
		board_init();
		tusb_init();
		while (true) {
			mext_task();
		}
	}

	bool    key_down_this_sample = false;
	bool    key_up_this_sample   = false;
	uint8_t last_event_x = 0;
	uint8_t last_event_y = 0;
	uint8_t any_key_held_count = 0;
	bool    held_state[MEXT_MAX_GRID_Y][MEXT_MAX_GRID_X] = {};
	uint8_t frame_[MEXT_MAX_GRID_X * MEXT_MAX_GRID_Y] = {};
	bool    frame_dirty_ = false;
};

#endif /* MONOME_GRID_HPP */
