/**
 * MLRws — Monome Live Remix for Workshop System Computer.
 *
 * 6 looping ADPCM tracks stored in flash.  Grid rows 1–6 control
 * tracks.  Row 0 is page navigation + pattern/recall controls.
 * Row 7 is output VU in grid mode.
 *
 * Pages (selected via row 0):
 *   CUT (col 0): playhead cutting, loop-a-section
 *   REC (col 1): per-track speed, reverse, volume, record arm
 *
 * Row 0 layout (both pages):
 *   Col 0     = CUT page
 *   Col 1     = REC page
 *   Cols 4–7  = Pattern 1–4 (timed motion recorders)
 *   Cols 9–12 = Recall 1–4 (instant snapshot slots)
 *   Col 14    = ALT modifier
 *
 * Recording: hold col 0 on a track row + switch up/down.
 *   Speed-linked — tape speed controls both record and playback rate.
 *   Records from position 0, variable length up to flash limit.
 *
 * Knob Main      = master output volume
 * Audio In 1     = record source (monitored while recording)
 * Audio Out 1    = mixed playback
 */

#include "ComputerCard.h"
#include "MonomeGrid.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "bsp/board.h"
#include "tusb.h"

extern "C" {
#include "monome_mext.h"
#include "mlr.h"
#include "device_mode.h"
}

/* ------------------------------------------------------------------ */
/* Grid LED update rate (sub-sampled from 48 kHz)                     */
/* ------------------------------------------------------------------ */
#define LED_UPDATE_INTERVAL 2400  /* ~50 ms at 48 kHz → 20 fps grid update */
#define PAT_TICK_INTERVAL     48  /* ~1 ms at 48 kHz → pattern playback resolution */
#define MAIN_CTRL_DIV        4    /* 48 kHz / 4 = 12 kHz UI/control polling */
#define GATE_HOLD_THRESHOLD 48000  /* 1 second at 48 kHz sample rate */
#define KNOB_HARD_TAKEOVER_THRESHOLD 80  /* ADC counts (0..4095) — must exceed ADC noise floor */
#define GRIDLESS_REC_HOLD_SAMPLES 96000u  /* 2 seconds at 48 kHz */
#define RECORD_REARM_DELAY_SAMPLES 24000u /* 0.5 seconds at 48 kHz */

/* ------------------------------------------------------------------ */
/* Pages                                                              */
/* ------------------------------------------------------------------ */
#define PAGE_CUT  0
#define PAGE_REC  1

enum class Mode {
	HostMLR,          /* USB host, grid connected directly */
	DeviceGridless,      /* USB device, nothing connected — gridless */
	DeviceMLR,        /* USB device, grid app connected via CDC */
	DeviceSampleMgr,  /* USB device, sample manager via CDC */
};

/* ------------------------------------------------------------------ */
/* Card                                                               */
/* ------------------------------------------------------------------ */

class MLRCard : public ComputerCard
{
public:
	MLRCard()
	{
		sleep_ms(150);
		EnableNormalisationProbe();

		/* ---- Determine mode from USB power state + protocol detection ---- */
		device_detect_result_t detect_result = {};

		USBPowerState_t power_state = USBPowerState();
		if (power_state != UFP) {
			/* Host mode — grid connected directly */
			mode_ = Mode::HostMLR;
		} else {
			/* Device mode — init TUD and detect protocol */
			board_init();
			tud_init(TUD_OPT_RHPORT);
			detect_result = device_mode_detect_protocol(2000);
			switch (detect_result.protocol) {
			case DEVICE_PROTO_SAMPLE_MGR:
				mode_ = Mode::DeviceSampleMgr;
				break;
			case DEVICE_PROTO_MEXT:
				mode_ = Mode::DeviceMLR;
				break;
			default:
				mode_ = Mode::DeviceGridless;
				break;
			}
		}

		/* ---- Mode-specific startup LED animation ---- */
		switch (mode_) {
		case Mode::HostMLR: {
			/* Host MLR: pairs sweep downward {0,1} → {2,3} → {4,5} */
			for (int step = 0; step < 6; step++) {
				int row = step % 3;
				int prev_row = (step + 2) % 3;
				LedOn(prev_row * 2, false);
				LedOn(prev_row * 2 + 1, false);
				LedOn(row * 2, true);
				LedOn(row * 2 + 1, true);
				sleep_ms(150);
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		case Mode::DeviceMLR: {
			/* Device MLR: forward chase {0, 1, 3, 5, 4, 2} */
			static const uint8_t chase_path[] = {0, 1, 3, 5, 4, 2};
			const int chase_len = sizeof(chase_path);
			for (int step = 0; step < 30; step++) {
				int prev = chase_path[(step + chase_len - 1) % chase_len];
				int curr = chase_path[step % chase_len];
				LedOn(prev, false);
				LedOn(curr, true);
				sleep_ms(50);
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		case Mode::DeviceGridless: {
			/* Gridless: single LED chase 0→5 and back */
			for (int rep = 0; rep < 2; rep++) {
				for (int i = 0; i < 6; i++) {
					for (int j = 0; j < 6; j++) LedOn(j, j == i);
					sleep_ms(60);
				}
				for (int i = 4; i >= 1; i--) {
					for (int j = 0; j < 6; j++) LedOn(j, j == i);
					sleep_ms(60);
				}
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		case Mode::DeviceSampleMgr: {
			/* Sample Manager: all LEDs pulse on then off */
			for (int b = 0; b < 4096; b += 256) {
				for (int i = 0; i < 6; i++) LedBrightness(i, (uint16_t)b);
				sleep_ms(30);
			}
			for (int b = 4095; b >= 0; b -= 256) {
				for (int i = 0; i < 6; i++) LedBrightness(i, (uint16_t)(b > 0 ? b : 0));
				sleep_ms(30);
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		}

		/* ---- Common init ---- */
		led_counter     = 0;
		pat_counter     = 0;
		play_page       = PAGE_CUT;
		rec_speed_accum = 0;
		rec_limit_latched = false;
		prev_rec_track  = -1;
		mlr_master_level_raw = (uint16_t)KnobVal(Knob::Main);
		master_knob_arm_raw_ = (int)mlr_master_level_raw;
		master_knob_last_raw_ = (int)mlr_master_level_raw;
		master_knob_takeover_armed_ = false;

		s_mode_ = mode_;
		mlr_init();

		/* ---- Mode-specific subsystem init ---- */
		switch (mode_) {
		case Mode::HostMLR:
			mext_init(MEXT_TRANSPORT_HOST, 0);
			break;
		case Mode::DeviceMLR:
			mext_init(MEXT_TRANSPORT_DEVICE, 0);
			mext_connect(0);  /* CDC already connected during detection */
			if (detect_result.has_pending_byte) {
				mext_rx_feed(&detect_result.first_byte, 1);
			}
			break;
		case Mode::DeviceGridless:
			gridless_scan_tracks();
			gl_active_track_ = 0;
			mlr_cut(gl_active_track_, 0);
			gl_prev_start_col_ = 0;
			knob_hard_takeover_arm(Knob::Main);
			knob_hard_takeover_arm(Knob::X);
			knob_hard_takeover_arm(Knob::Y);
			break;
		case Mode::DeviceSampleMgr:
			device_mode_init();
			if (detect_result.has_pending_byte) {
				device_mode_inject_byte(detect_result.first_byte);
			}
			break;
		}

		multicore_launch_core1(core1_entry);
	}

	/* Core 1: USB + I/O — mode-specific loop */
	static void core1_entry()
	{
		switch (s_mode_) {
		case Mode::HostMLR:
			board_init();
			tusb_init();
			while (true) {
				mext_task();
				mlr_io_task();
			}
			break;
		case Mode::DeviceMLR:
			/* board_init + tud_init already done in constructor */
			while (true) {
				tud_task();
				mext_task();
				mlr_io_task();
			}
			break;
		case Mode::DeviceGridless:
			/* board_init + tud_init already done in constructor */
			device_mode_init();  /* pre-init so it's ready if needed */
			while (true) {
				tud_task();

				/* ---- Monitor CDC: if a sample-manager byte arrives, transition ---- */
				if (tud_cdc_n_connected(0) && tud_cdc_n_available(0) > 0) {
					uint8_t peek = 0;
					tud_cdc_n_read(0, &peek, 1);

					if (peek == 'I' || peek == 'S' ||
					    peek == 'R' || peek == 'E' || peek == 'W' || peek == 'X') {
						/* ---- Enter sample manager mode ---- */
						for (int t = 0; t < MLR_NUM_TRACKS; t++) {
							if (mlr_tracks[t].playing) {
								mlr_tracks[t].playing = false;
								mlr_tracks[t].pcm.r = mlr_tracks[t].pcm.w;
							}
						}

						s_sample_mgr_active_ = true;
						__dmb();

						device_mode_init();
						device_mode_inject_byte(peek);

						/* Run sample manager until CDC disconnects */
						while (tud_cdc_n_connected(0)) {
							tud_task();
							device_mode_task();
						}

						/* CDC disconnected — rescan tracks and resume gridless */
						for (int t = 0; t < MLR_NUM_TRACKS; t++)
							mlr_rescan_track(t);

						s_rescan_needed_ = true;
						s_sample_mgr_active_ = false;
						__dmb();
					}
					/* else: unknown byte — ignore, stay in gridless mode */
				}

				mlr_io_task();
			}
			break;
		case Mode::DeviceSampleMgr:
			/* board_init + tud_init already done in constructor */
			while (true) {
				tud_task();
				device_mode_task();
			}
			break;
		}
	}

	virtual void __not_in_flash("ProcessSample") ProcessSample() override
	{
		if (rec_start_lockout_samples_ > 0)
			rec_start_lockout_samples_--;

		/* ---- Mode dispatch ---- */
		switch (mode_) {
		case Mode::DeviceGridless:
			if (s_sample_mgr_active_) {
				/* Sample manager owns flash — output silence */
				AudioOut1(0);
#ifdef MLR_STEREO
				AudioOut2(0);
#endif
				return;
			}
			if (s_rescan_needed_) {
				/* Sample manager just disconnected — rescan tracks */
				s_rescan_needed_ = false;
				gridless_scan_tracks();
				if (gl_active_track_ < 0 || gl_active_track_ >= MLR_NUM_TRACKS)
					gl_active_track_ = 0;
				mlr_cut(gl_active_track_, 0);
				gl_prev_start_col_ = 0;
				knob_hard_takeover_arm(Knob::Main);
				knob_hard_takeover_arm(Knob::X);
				knob_hard_takeover_arm(Knob::Y);
			}
			gridless_process_sample();
			return;
		case Mode::DeviceSampleMgr:
			/* Silence — sample manager owns flash exclusively */
			AudioOut1(0);
#ifdef MLR_STEREO
			AudioOut2(0);
#endif
			return;
		case Mode::HostMLR:
		case Mode::DeviceMLR:
			break;  /* fall through to MLR processing */
		}

		ui_ctrl_div_++;
		bool run_ui_control = false;
		if (ui_ctrl_div_ >= MAIN_CTRL_DIV) {
			ui_ctrl_div_ = 0;
			run_ui_control = true;
		}
		if (mlr_flushing) run_ui_control = false;

		if (run_ui_control) {
			grid.poll();
			process_bottom_master_control();
		}

		/* ---- recording state machine: armed track + switch position ---- */
		int sw = SwitchVal();
		bool rec_modifier = (sw == Switch::Up || sw == Switch::Down);

		if (mlr_flushing) {
			if (resume_after_arm_track_ == rec_armed_track)
				resume_after_arm_track_ = -1;
			rec_armed_track = -1;  /* disarm if flushing */
			rec_gated = false;
		}

		/* Stop gated recording if switch returns to middle */
		if (rec_gated && !rec_modifier && mlr_rec_track >= 0) {
			mlr_stop_record();
			rec_speed_accum = 0;
			rec_gated = false;
		}

		/* Start recording if armed track exists and switch moves to rec position */
		if (rec_armed_track >= 0 && rec_modifier && mlr_rec_track < 0 && !rec_limit_latched && rec_start_lockout_samples_ == 0) {
			if (resume_after_arm_track_ == rec_armed_track)
				resume_after_arm_track_ = -1;
			mlr_start_record(rec_armed_track);
			rec_speed_accum = 0;
		}
		/* Stop recording if switch returns to middle or armed track changes */
		if (mlr_rec_track >= 0 && !rec_gated && (!rec_modifier || rec_armed_track != mlr_rec_track)) {
			mlr_stop_record();
			rec_speed_accum = 0;
			if (rec_armed_track >= 0 && rec_modifier && rec_armed_track != mlr_rec_track && !mlr_flushing && !rec_limit_latched && rec_start_lockout_samples_ == 0) {
				mlr_start_record(rec_armed_track);
				rec_speed_accum = 0;
			}
		}

		/* ---- stop armed track so recording starts from silence ---- */
		if (run_ui_control && rec_armed_track >= 0 && !rec_gated &&
		    mlr_tracks[rec_armed_track].playing) {
			resume_after_arm_track_ = rec_armed_track;
			mlr_stop_track(rec_armed_track);
		}

		if (run_ui_control) {
			update_record_monitor_window(sw);
		}

		/* ---- speed-linked recording ---- */
		int16_t dry_in = notch_filter_input(AudioIn1());
#ifdef MLR_STEREO
		int16_t dry_in_r = AudioIn2();
#endif
		uint8_t dry_level = (uint8_t)(KnobVal(Knob::X) >> 4);  /* 0–255 */
		if (mlr_rec_track >= 0) {
			/* scale input by dry knob and track volume before encoding */
			int32_t scaled = ((int32_t)dry_in * (int32_t)dry_level) >> 8;
			scaled = (scaled * (int32_t)mlr_tracks[mlr_rec_track].volume_frac) >> 8;
			if (scaled > 2047) scaled = 2047;
			if (scaled < -2048) scaled = -2048;
#ifdef MLR_STEREO
			int32_t scaled_r = ((int32_t)dry_in_r * (int32_t)dry_level) >> 8;
			scaled_r = (scaled_r * (int32_t)mlr_tracks[mlr_rec_track].volume_frac) >> 8;
			if (scaled_r > 2047) scaled_r = 2047;
			if (scaled_r < -2048) scaled_r = -2048;
#endif
			uint16_t spd = mlr_tracks[mlr_rec_track].speed_frac;
			rec_speed_accum += spd;
			while (rec_speed_accum >= 256) {
				rec_speed_accum -= 256;
#ifdef MLR_STEREO
				mlr_record_sample_stereo((int16_t)scaled, (int16_t)scaled_r);
#else
				mlr_record_sample((int16_t)scaled);
#endif
				if (mlr_get_rec_progress() >= MLR_MAX_SAMPLES) {
					mlr_stop_record();
					rec_speed_accum = 0;
					rec_limit_latched = true;
					rec_armed_track = -1;  /* disarm when limit reached */
					rec_gated = false;
					break;
				}
			}
		}

		/* ---- page navigation + pattern/recall buttons (row 0, always active) ---- */
		if (run_ui_control && grid.keyDown() && grid.lastY() == 0) {
			int col = grid.lastX();
			bool alt_held = grid.held(14, 0);
			if (col == 0) {
				play_page = PAGE_CUT;
			}
			else if (col == 1) {
				play_page = PAGE_REC;
				/* stop all gated tracks when leaving CUT page */
				for (int t = 0; t < MLR_NUM_TRACKS; t++) {
					if (gate_mode[t] && mlr_tracks[t].playing) {
						mlr_stop_track(t);
						dispatch_event(MLR_EVT_STOP, (uint8_t)t, 0, 0);
					}
				}
			}
			else if (col >= 4 && col <= 7) {
				/* pattern buttons */
				int p = col - 4;
				if (alt_held) {
					mlr_pattern_rec_stop(p);
					mlr_pattern_play_stop(p);
					mlr_pattern_clear(p);
					mlr_scene_save_start();
				} else if (mlr_patterns[p].state == MLR_PAT_RECORDING) {
					/* stop recording, immediately start looping */
					mlr_pattern_rec_stop(p);
					if (mlr_patterns[p].count > 0) {
						mlr_pattern_play_start(p);
						mlr_scene_save_start();
					}
				} else if (mlr_patterns[p].state == MLR_PAT_ARMED) {
					/* disarm */
					mlr_pattern_rec_stop(p);
				} else if (mlr_patterns[p].state == MLR_PAT_IDLE) {
					mlr_pattern_arm(p);
				} else if (mlr_patterns[p].state == MLR_PAT_PLAYING) {
					mlr_pattern_play_stop(p);
				} else if (mlr_patterns[p].state == MLR_PAT_STOPPED) {
					mlr_pattern_play_start(p);
				}
			} else if (col >= 9 && col <= 12) {
				/* recall/scene buttons */
				int r = col - 9;
				if (alt_held) {
					mlr_recall_clear(r);
					mlr_scene_save_start();
				} else if (mlr_recall_active == r) {
					/* pressing active scene: undo to pre-recall state */
					mlr_recall_undo_and_record();
				} else if (!mlr_recalls[r].has_data) {
					/* empty: instant snapshot */
					mlr_recall_snapshot(r);
					mlr_scene_save_start();
				} else {
					/* has data: recall it */
					mlr_recall_exec_and_record(r);
				}
			}
		}

		/* ---- page interaction (always active) ---- */
		if (run_ui_control) {
			process_gate_hold();  /* col 0/15 play/stop/gate on both pages */
			if (play_page == PAGE_REC)
				process_page_rec();
			else
				process_page_cut();
		}

		/* ---- mix and output ---- */
		int knob_main_raw = KnobVal(Knob::Main);
		if (master_knob_last_raw_ < 0) master_knob_last_raw_ = knob_main_raw;
		if (!master_knob_takeover_armed_) {
			if (knob_main_raw != master_knob_last_raw_) {
				mlr_master_level_raw = (uint16_t)knob_main_raw;
				master_knob_arm_raw_ = knob_main_raw;
				mlr_master_override = true;  /* knob moved, suppress pattern master */
			}
		} else {
			int diff = knob_main_raw - master_knob_arm_raw_;
			if (diff < 0) diff = -diff;
			if (diff > KNOB_HARD_TAKEOVER_THRESHOLD) {
				master_knob_takeover_armed_ = false;
				mlr_master_level_raw = (uint16_t)knob_main_raw;
				master_knob_arm_raw_ = knob_main_raw;
				mlr_master_override = true;  /* knob takeover, suppress pattern master */
			}
		}
		master_knob_last_raw_ = knob_main_raw;

		uint8_t master_level = (uint8_t)(mlr_master_level_raw >> 4);
#ifdef MLR_STEREO
		int16_t mix_r;
		int16_t mix = mlr_play_mix_stereo(255, &mix_r);
		int32_t outL = (int32_t)mix;
		int32_t outR = (int32_t)mix_r;
		if (dry_level > 0 && (rec_armed_track >= 0 || sw == Switch::Up)) {
			/* monitor input through ADPCM codec round-trip to filter USB noise.
			 * Apply armed/recording track volume so monitor matches what's recorded. */
			uint16_t mon_vol = 256;  /* unity default */
			int mon_track = (mlr_rec_track >= 0) ? mlr_rec_track : rec_armed_track;
			if (mon_track >= 0) mon_vol = mlr_tracks[mon_track].volume_frac;
			int32_t in_l = ((int32_t)dry_in * (int32_t)dry_level) >> 8;
			in_l = (in_l * (int32_t)mon_vol) >> 8;
			int32_t in_r = ((int32_t)dry_in_r * (int32_t)dry_level) >> 8;
			in_r = (in_r * (int32_t)mon_vol) >> 8;
			int16_t in16l = (int16_t)(in_l << 4);
			uint8_t nybL = adpcm_encode(in16l, &mon_enc_);
			int16_t cleanL = adpcm_decode(nybL, &mon_dec_);
			outL += (int32_t)(cleanL >> 4);

			int16_t in16r = (int16_t)(in_r << 4);
			uint8_t nybR = adpcm_encode(in16r, &mon_enc_r_);
			int16_t cleanR = adpcm_decode(nybR, &mon_dec_r_);
			outR += (int32_t)(cleanR >> 4);
		}
		outL = (outL * (int32_t)master_level) >> 8;
		outR = (outR * (int32_t)master_level) >> 8;
		if (outL > 2047)  outL = 2047;  if (outL < -2048) outL = -2048;
		if (outR > 2047)  outR = 2047;  if (outR < -2048) outR = -2048;
		AudioOut1((int16_t)outL);
		AudioOut2((int16_t)outR);
#else
		int16_t mix = mlr_play_mix(255);
		int32_t out = (int32_t)mix;
		if (dry_level > 0 && (rec_armed_track >= 0 || sw == Switch::Up)) {
			/* monitor input through ADPCM codec round-trip to filter USB noise.
			 * Apply armed/recording track volume so monitor matches what's recorded. */
			uint16_t mon_vol = 256;
			int mon_track = (mlr_rec_track >= 0) ? mlr_rec_track : rec_armed_track;
			if (mon_track >= 0) mon_vol = mlr_tracks[mon_track].volume_frac;
			int32_t in_scaled = ((int32_t)dry_in * (int32_t)dry_level) >> 8;
			in_scaled = (in_scaled * (int32_t)mon_vol) >> 8;
			int16_t in16 = (int16_t)(in_scaled << 4);
			uint8_t nyb = adpcm_encode(in16, &mon_enc_);
			int16_t clean = adpcm_decode(nyb, &mon_dec_);
			out += (int32_t)(clean >> 4);
		}
		out = (out * (int32_t)master_level) >> 8;
		if (out > 2047)  out = 2047;
		if (out < -2048) out = -2048;
		AudioOut1((int16_t)out);
#endif

		/* ---- tick pattern playback (~5ms resolution) ---- */
		if (!mlr_flushing) {
			pat_counter++;
			if (pat_counter >= PAT_TICK_INTERVAL) {
				pat_counter = 0;
				uint32_t now_ms = to_ms_since_boot(get_absolute_time());
				mlr_pattern_tick(now_ms);
			}
		}

		/* ---- update LEDs (sub-sampled) ---- */
		if (!mlr_flushing) {
			led_counter++;
			if (led_counter >= LED_UPDATE_INTERVAL) {
				led_counter = 0;
				rec_blink = (rec_blink + 1) & 15;
				gate_pulse++;

				if (vu_in_accum_ > vu_in_peak_) vu_in_peak_ = vu_in_accum_;
				else if (vu_in_peak_ > 24) vu_in_peak_ -= 24;
				else vu_in_peak_ = 0;

				if (vu_out_accum_ > vu_out_peak_) vu_out_peak_ = vu_out_accum_;
				else if (vu_out_peak_ > 24) vu_out_peak_ -= 24;
				else vu_out_peak_ = 0;

				vu_in_accum_ = 0;
				vu_out_accum_ = 0;

				LedBrightness(4, vu_in_peak_ > 100 ? (uint16_t)(vu_in_peak_ * 2) : 0);
				LedBrightness(2, vu_in_peak_ > 600 ? (uint16_t)((vu_in_peak_ - 600) * 3) : 0);
				LedBrightness(0, vu_in_peak_ > 1400 ? (uint16_t)((vu_in_peak_ - 1400) * 6) : 0);
				LedBrightness(5, vu_out_peak_ > 100 ? (uint16_t)(vu_out_peak_ * 2) : 0);
				LedBrightness(3, vu_out_peak_ > 600 ? (uint16_t)((vu_out_peak_ - 600) * 3) : 0);
				LedBrightness(1, vu_out_peak_ > 1400 ? (uint16_t)((vu_out_peak_ - 1400) * 6) : 0);

				if (play_page == PAGE_REC)
					update_grid_page_rec();
				else
					update_grid_page_cut();
			}
		}

		/* ---- detect flush completion: stop gated tracks from auto-playing ---- */
		if (was_flushing_ && !mlr_flushing && prev_rec_track >= 0) {
			record_history_push(prev_rec_track);
			if (gate_mode[prev_rec_track] && mlr_tracks[prev_rec_track].playing) {
				mlr_stop_track(prev_rec_track);
			}
			rec_start_lockout_samples_ = RECORD_REARM_DELAY_SAMPLES;
		}
		was_flushing_ = mlr_flushing;

		/* ---- card VU accumulation (LED writes are decimated above) ---- */
		{
			int32_t in_scaled_abs = dry_in < 0 ? -dry_in : dry_in;
			in_scaled_abs = (in_scaled_abs * (int32_t)dry_level) >> 8;  /* 0..2048 */
			if (in_scaled_abs > vu_in_accum_) vu_in_accum_ = in_scaled_abs;

#ifdef MLR_STEREO
			int32_t out_abs = outL < 0 ? -outL : outL;
			int32_t out_abs_r = outR < 0 ? -outR : outR;
			if (out_abs_r > out_abs) out_abs = out_abs_r;
#else
			int32_t out_abs = out < 0 ? -out : out;
#endif
			if (out_abs > vu_out_accum_) vu_out_accum_ = out_abs;
		}

		if (mlr_rec_track >= 0) prev_rec_track = mlr_rec_track;
	}

private:
	/** Read the switch position directly via ADC (safe before Run()/ISR).
	 *  The switch is on the analog mux at position 3 (MX_A=1, MX_B=1),
	 *  read through MUX_IO_1 (ADC input 2, GPIO 28). */
	static Switch read_switch_direct()
	{
		/* Set mux to position 3: A=1, B=1 */
		gpio_put(24, true);   /* MX_A */
		gpio_put(25, true);   /* MX_B */
		sleep_ms(2);          /* settle time for mux + RC filter */

		/* Do a single blocking ADC read on channel 2 (GPIO 28 = MUX_IO_1) */
		adc_select_input(2);
		uint16_t raw = adc_read();  /* 12-bit: 0–4095 */

		return static_cast<Switch>((raw > 1000) + (raw > 3000));
	}

	static int16_t __not_in_flash("notch_filter_input") notch_filter_input(int32_t x)
	{
		/* 12 kHz notch, matching the reverb card's hardcoded mux-noise cleanup. */
		constexpr int32_t ooa0 = 16302;
		constexpr int32_t a2oa0 = 16221;

		int32_t y = (ooa0 * (x + notch_x2_) - a2oa0 * notch_y2_) >> 14;
		notch_x2_ = notch_x1_;
		notch_x1_ = x;
		notch_y2_ = notch_y1_;
		notch_y1_ = y;

		if (y > 2047) y = 2047;
		if (y < -2048) y = -2048;
		return (int16_t)y;
	}

	MonomeGrid grid;
	uint32_t   led_counter;
	uint32_t   pat_counter;
	uint8_t    ui_ctrl_div_ = 0;
	int        play_page;
	Mode       mode_;
	uint16_t   rec_speed_accum;
	bool       rec_limit_latched;
	int        prev_rec_track = -1;  /* last track that was recording */
	uint8_t    rec_blink = 0;        /* blink counter for record indicator */
	int32_t    vu_in_peak_ = 0;      /* peak hold for input VU */
	int32_t    vu_out_peak_ = 0;     /* peak hold for output VU */
	int32_t    vu_in_accum_ = 0;     /* per-sample input VU accumulator */
	int32_t    vu_out_accum_ = 0;    /* per-sample output VU accumulator */
	int        rec_armed_track = -1; /* armed track for recording (-1 = none) */
	int        resume_after_arm_track_ = -1; /* track auto-stopped due to arm; resume on disarm */
	bool       rec_gated = false;    /* true during press-to-record (gated) */
	bool       rec_monitor_window_active_ = false;
	bool       rec_monitor_prev_playing_[MLR_NUM_TRACKS] = {};
	int8_t     rec_recent_tracks_[4] = {-1, -1, -1, -1};
	bool       rec_monitor_restore_pending_ = false;
	uint8_t    rec_monitor_restore_idx_ = 0;
	int        master_knob_arm_raw_ = -1;
	int        master_knob_last_raw_ = -1;
	bool       master_knob_takeover_armed_ = false;
	uint32_t   rec_start_lockout_samples_ = 0;
	bool       was_flushing_ = false; /* edge detect for flush completion */
	bool       gate_mode[MLR_NUM_TRACKS] = {};    /* per-track gate playback mode */
	uint32_t   col15_hold_time[MLR_NUM_TRACKS] = {};  /* sample counter for col 15 hold */
	bool       col15_armed[MLR_NUM_TRACKS] = {};  /* true while col 15 is held in REC */
	bool       gate_entered[MLR_NUM_TRACKS] = {}; /* true once hold crossed threshold */
	uint16_t   gate_pulse = 0;       /* free-running counter for gate LED pulse */
	adpcm_state_t mon_enc_ = {0, 0}; /* monitor codec encoder state */
	adpcm_state_t mon_dec_ = {0, 0}; /* monitor codec decoder state */
#ifdef MLR_STEREO
	adpcm_state_t mon_enc_r_ = {0, 0}; /* right channel monitor encoder */
	adpcm_state_t mon_dec_r_ = {0, 0}; /* right channel monitor decoder */
#endif
	inline static int32_t notch_x1_ = 0;
	inline static int32_t notch_x2_ = 0;
	inline static int32_t notch_y1_ = 0;
	inline static int32_t notch_y2_ = 0;
	inline static Mode s_mode_ = Mode::HostMLR;

	/* Cross-core volatile state for DeviceGridless ↔ DeviceSampleMgr transitions.
	 * Written by core 1, read by core 0. */
	inline static volatile bool s_sample_mgr_active_ = false;
	inline static volatile bool s_rescan_needed_ = false;

	/* ---- Gridless mode state ---- */
	int        gl_active_track_ = -1;    /* currently selected/playing track index */
	int        gl_num_populated_ = 0;    /* number of tracks with content */
	int        gl_populated_[MLR_NUM_TRACKS] = {};  /* indices of populated tracks */
	int        gl_prev_knob_track_ = -1; /* previous track from knob, for hysteresis */
	int        gl_prev_start_col_ = 0;   /* last accepted start column (0..13) */
	bool       gl_prev_switch_down_ = false; /* edge detect for switch-down reset */
	int        gl_locked_track_ = -1;     /* locked track while switch is Up */
	Switch     gl_prev_switch_mode_ = Switch::Middle;
	bool       gl_active_deadzone_mute_ = false; /* channel-mode center mute for active channel */
	uint16_t   gl_base_speed_frac_[MLR_NUM_TRACKS] = {256, 256, 256, 256, 256, 256};
	uint16_t   gl_playback_gain_frac_ = 256; /* playback-layer gain, Q8: 256 = unity */
	uint16_t   gl_radiate_frac_ = 0;      /* global-mode radiate amount, Q8: 0..256 */
	uint16_t   gl_playback_track_gain_frac_[MLR_NUM_TRACKS] = {}; /* per-track playback layer gain */
	enum class GridlessMixMode {
		HardCutSolo,
	};
	GridlessMixMode gl_mix_mode_ = GridlessMixMode::HardCutSolo;
	uint32_t   gl_led_counter_ = 0;      /* sub-sample counter for LED updates */
	uint16_t   gl_led_pulse_ = 0;        /* free-running pulse counter for LEDs */
	bool       knob_hard_takeover_armed_[3] = {false, false, false};
	int        knob_hard_takeover_anchor_[3] = {0, 0, 0};
	uint8_t    gl_ctrl_div_ = 0;              /* control-rate divider */
	uint8_t    gl_start_maint_div_ = 0;       /* all-track restart maintenance divider */
	bool       gl_pulse1_pending_ = false;    /* latched PulseIn1 edge between control ticks */
	bool       gl_pulse2_pending_ = false;    /* latched PulseIn2 edge between control ticks */
	bool       gl_prev_switch_down_sample_ = false;
	bool       gl_switch_down_rise_pending_ = false;
	bool       gl_switch_down_fall_pending_ = false;
	uint32_t   gl_switch_down_hold_samples_ = 0;
	bool       gl_record_hold_active_ = false;
	bool       gl_record_mode_ = false;
	bool       gl_record_mode_wait_release_ = false;
	bool       gl_recording_active_ = false;
	int        gl_record_track_ = -1;
	bool       gl_post_record_start_pending_ = false;
	int        gl_post_record_start_track_ = -1;
	bool       gl_audio1_mod_rearm_required_ = false;
	bool       gl_audio2_mod_rearm_required_ = false;
	bool       gl_audio1_seen_disconnect_ = false;
	bool       gl_audio2_seen_disconnect_ = false;

	int knob_index(Knob knob)
	{
		switch (knob) {
		case Knob::Main: return 0;
		case Knob::X:    return 1;
		case Knob::Y:    return 2;
		}
		return -1;
	}

	void knob_hard_takeover_arm(Knob knob)
	{
		int i = knob_index(knob);
		if (i < 0) return;
		knob_hard_takeover_armed_[i] = true;
		knob_hard_takeover_anchor_[i] = KnobVal(knob);
	}

	bool knob_hard_takeover_accept(Knob knob, int raw, int threshold = KNOB_HARD_TAKEOVER_THRESHOLD)
	{
		int i = knob_index(knob);
		if (i < 0) return true;
		if (!knob_hard_takeover_armed_[i]) return true;

		int diff = raw - knob_hard_takeover_anchor_[i];
		if (diff < 0) diff = -diff;
		if (diff <= threshold) return false;

		knob_hard_takeover_armed_[i] = false;
		return true;
	}

	/* ================================================================ */
	/* Gridless mode                                      */
	/* ================================================================ */

	/** Scan mlr_tracks[] and build the populated track list. */
	void gridless_scan_tracks()
	{
		gl_num_populated_ = 0;
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (mlr_tracks[t].has_content) {
				gl_populated_[gl_num_populated_++] = t;
			}
		}
	}

	int gridless_track_idx_from_knob(int knob_main) const
	{
		if (knob_main < 0) knob_main = 0;
		if (knob_main > 4095) knob_main = 4095;
		int track_idx = (knob_main * MLR_NUM_TRACKS) >> 12;  /* 0..5 */
		if (track_idx < 0) track_idx = 0;
		if (track_idx >= MLR_NUM_TRACKS) track_idx = MLR_NUM_TRACKS - 1;
		return track_idx;
	}

	void gridless_start_all_tracks_if_needed()
	{
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (!mlr_tracks[t].has_content) continue;
			if (!mlr_tracks[t].playing && !mlr_tracks[t].stop_pending) {
				int col = mlr_get_column(t);
				if (col < 0) col = 0;
				mlr_cut(t, col);
			}
		}
	}

	void gridless_apply_level_layers()
	{
		static const uint16_t kSlotFrac[MLR_NUM_VOL_SLOTS] = {
			512,  /* slot 0: +6 dB */
			362,  /* slot 1: +3 dB */
			256,  /* slot 2: unity */
			181,  /* slot 3: -3 dB */
			128,  /* slot 4: -6 dB */
			45,   /* slot 5: -15 dB */
		};

		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			mlr_track_t *tr = &mlr_tracks[t];
			uint8_t slot = tr->volume_slot;
			if (slot >= MLR_NUM_VOL_SLOTS) slot = MLR_NUM_VOL_SLOTS - 1;
			uint32_t level = kSlotFrac[slot];

			/* Playback layer gain is per-track (solo/radiate matrix). */
			level = (level * (uint32_t)gl_playback_track_gain_frac_[t]) >> 8;

			if (level > 1024u) level = 1024u;
			tr->volume_target = (uint16_t)level;
			if (!tr->playing)
				tr->volume_frac = tr->volume_target;
		}
	}

	void gridless_apply_mix_matrix(bool switch_middle)
	{
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			gl_playback_track_gain_frac_[t] = 0;
			mlr_tracks[t].muted = false;
		}

		if (gl_active_track_ < 0 || gl_active_track_ >= MLR_NUM_TRACKS)
			return;

		if (gl_record_mode_ || gl_recording_active_) {
			gl_playback_track_gain_frac_[gl_active_track_] = 256u;
			return;
		}

		switch (gl_mix_mode_) {
		case GridlessMixMode::HardCutSolo:
		default:
			/* Normal playback: always use radiate matrix.
			 * Record mode/recording above enforces selected-track solo. */
			uint32_t spread = (uint32_t)gl_radiate_frac_ * MLR_NUM_TRACKS;  /* 0..1792 */
			for (int t = 0; t < MLR_NUM_TRACKS; t++) {
				int d = t - gl_active_track_;
				if (d < 0) d = -d;
				uint32_t w = 0;
				if (d == 0) {
					w = 256;
				} else {
					int32_t raw = (int32_t)spread - (int32_t)(d * 256);
					if (raw > 256) raw = 256;
					if (raw < 0) raw = 0;
					w = (uint32_t)raw;
				}
				gl_playback_track_gain_frac_[t] = (uint16_t)((w * (uint32_t)gl_playback_gain_frac_) >> 8);
			}
			break;
		}
	}

	/**
	 * Gridless ProcessSample
	 *
	 * Global mode (switch middle):
	 *   Knob Main = channel select
	 * Channel mode (switch up):
	 *   Knob Main = bipolar speed control
	 *     center ~0 (muted hold), left = reverse, right = forward
	 *     edges map to ±2 octaves
	 *   Knob X = per-channel level (grid mixer style)
	 *   Knob Y = reset start position (column 0–13)
	 * Global mode (switch middle):
	 *   Knob X = playback layer gain (unity at center)
	 *   Knob Y = radiate amount (left=selected only, right=all channels)
	 * Switch Down / Pulse In 1 rising edge = reset to start position
	 * Tracks always play and loop. No recording in this mode.
	 */
	void __not_in_flash("gridless") gridless_process_sample()
	{
		/* Keep pulse outputs one-sample wide. */
		PulseOut1(false);
		PulseOut2(false);

		/* Latch external pulse edges at full sample rate; consume at control rate. */
		if (PulseIn1RisingEdge()) gl_pulse1_pending_ = true;
		if (PulseIn2RisingEdge()) gl_pulse2_pending_ = true;

		/* After a recording pass, require per-input disconnect/reconnect before
		 * re-enabling AudioIn modulation. */
		bool a1_connected = Connected(Input::Audio1);
		bool a2_connected = Connected(Input::Audio2);
		if (gl_audio1_mod_rearm_required_) {
			if (!a1_connected) gl_audio1_seen_disconnect_ = true;
			if (a1_connected && gl_audio1_seen_disconnect_) {
				gl_audio1_mod_rearm_required_ = false;
				gl_audio1_seen_disconnect_ = false;
			}
		}
		if (gl_audio2_mod_rearm_required_) {
			if (!a2_connected) gl_audio2_seen_disconnect_ = true;
			if (a2_connected && gl_audio2_seen_disconnect_) {
				gl_audio2_mod_rearm_required_ = false;
				gl_audio2_seen_disconnect_ = false;
			}
		}

		/* Latch switch-down edges at full sample rate. */
		int sw_now = SwitchVal();
		bool switch_down_now = (sw_now == Switch::Down);
		if (switch_down_now && !gl_prev_switch_down_sample_) gl_switch_down_rise_pending_ = true;
		if (!switch_down_now && gl_prev_switch_down_sample_) gl_switch_down_fall_pending_ = true;
		gl_prev_switch_down_sample_ = switch_down_now;

		/* Hold switch down 3s to enter record mode. */
		if (!gl_record_mode_ && !gl_recording_active_ && !mlr_flushing && rec_start_lockout_samples_ == 0) {
			if (switch_down_now) {
				if (gl_switch_down_hold_samples_ < GRIDLESS_REC_HOLD_SAMPLES)
					gl_switch_down_hold_samples_++;
				if (gl_switch_down_hold_samples_ >= GRIDLESS_REC_HOLD_SAMPLES) {
					gl_record_mode_ = true;
					gl_record_mode_wait_release_ = true;
					gl_record_hold_active_ = false;
					gl_switch_down_hold_samples_ = 0;
					gl_switch_down_rise_pending_ = false;
				}
			} else {
				gl_switch_down_hold_samples_ = 0;
			}
		}
		if (gl_record_mode_wait_release_ && !switch_down_now)
			gl_record_mode_wait_release_ = false;
		gl_record_hold_active_ = (!gl_record_mode_ && switch_down_now &&
			gl_switch_down_hold_samples_ > 0 && gl_switch_down_hold_samples_ < GRIDLESS_REC_HOLD_SAMPLES);

		/* In record mode (idle), switch Up cancels and returns to normal playback mode
		 * without starting any recording/overwrite. */
		if (gl_record_mode_ && !gl_recording_active_ && sw_now == Switch::Up) {
			gl_record_mode_ = false;
			gl_record_mode_wait_release_ = false;
			gl_switch_down_hold_samples_ = 0;
			gl_switch_down_rise_pending_ = false;
			gl_switch_down_fall_pending_ = false;
		}

		/* Record-mode switch behavior: next switch-down starts recording. */
		if (gl_record_mode_ && !gl_record_mode_wait_release_ && !gl_recording_active_ && !mlr_flushing && rec_start_lockout_samples_ == 0 && gl_switch_down_rise_pending_) {
			gl_switch_down_rise_pending_ = false;
			if (gl_active_track_ >= 0 && gl_active_track_ < MLR_NUM_TRACKS) {
				gl_record_track_ = gl_active_track_;
				mlr_start_record(gl_record_track_);
				rec_speed_accum = 0;
				gl_recording_active_ = true;
			}
		}

		int16_t rec_monitor_l = 0;
#ifdef MLR_STEREO
		int16_t rec_monitor_r = 0;
#endif

		/* Active recording: speed-linked write, auto-stop at max length. */
		if (gl_recording_active_ && gl_record_track_ >= 0 && gl_record_track_ < MLR_NUM_TRACKS) {
			int16_t dry_in_l = notch_filter_input(AudioIn1());
#ifdef MLR_STEREO
			int16_t dry_in_r = AudioIn2();
#endif
			uint8_t in_gain = (uint8_t)(KnobVal(Knob::X) >> 4);  /* 0..255 */

			int32_t scaled_l = ((int32_t)dry_in_l * (int32_t)in_gain) >> 8;
			if (scaled_l > 2047) scaled_l = 2047;
			if (scaled_l < -2048) scaled_l = -2048;
			rec_monitor_l = (int16_t)scaled_l;

#ifdef MLR_STEREO
			int32_t scaled_r = ((int32_t)dry_in_r * (int32_t)in_gain) >> 8;
			if (scaled_r > 2047) scaled_r = 2047;
			if (scaled_r < -2048) scaled_r = -2048;
			rec_monitor_r = (int16_t)scaled_r;
#endif

			rec_speed_accum += 256u;  /* fixed 1x record speed in gridless mode */

			bool stop_record = false;
			while (rec_speed_accum >= 256) {
				rec_speed_accum -= 256;
#ifdef MLR_STEREO
				mlr_record_sample_stereo(rec_monitor_l, rec_monitor_r);
#else
				mlr_record_sample(rec_monitor_l);
#endif
				if (mlr_get_rec_progress() >= MLR_MAX_SAMPLES) {
					stop_record = true;
					break;
				}
			}

			if (gl_switch_down_fall_pending_) {
				gl_switch_down_fall_pending_ = false;
				stop_record = true;
			}

			if (stop_record) {
				mlr_stop_record();
				rec_speed_accum = 0;
				gl_recording_active_ = false;
				gl_record_mode_ = false;
				gl_record_mode_wait_release_ = false;
				gl_audio1_mod_rearm_required_ = true;
				gl_audio2_mod_rearm_required_ = true;
				gl_audio1_seen_disconnect_ = false;
				gl_audio2_seen_disconnect_ = false;
				gl_post_record_start_pending_ = true;
				gl_post_record_start_track_ = gl_record_track_;
				gl_record_track_ = -1;
			}
		}
		if (!gl_recording_active_) {
			gl_switch_down_fall_pending_ = false;
		}

		/* Once flush completes, start looping new material from start. */
		if (gl_post_record_start_pending_ && !mlr_flushing) {
			int t = gl_post_record_start_track_;
			if (t >= 0 && t < MLR_NUM_TRACKS && mlr_tracks[t].has_content) {
				mlr_cut(t, 0);
				gl_active_track_ = t;
			}
			rec_start_lockout_samples_ = RECORD_REARM_DELAY_SAMPLES;
			gl_post_record_start_pending_ = false;
			gl_post_record_start_track_ = -1;
		}

		/* Run control path at reduced rate to save CPU in gridless mode. */
		const uint8_t kGridlessControlDiv = 4;  /* 48 kHz / 4 = 12 kHz control loop */
		gl_ctrl_div_++;
		bool run_control = false;
		if (gl_ctrl_div_ >= kGridlessControlDiv) {
			gl_ctrl_div_ = 0;
			run_control = true;
		}

		if (run_control) {
			/* Maintenance doesn't need control-rate updates. */
			gl_start_maint_div_++;
			if (gl_start_maint_div_ >= 8) {  /* ~1.5 kHz effective */
				gl_start_maint_div_ = 0;
				gridless_start_all_tracks_if_needed();
			}

			int sw = sw_now;
			bool switch_middle = (sw == Switch::Middle);
			bool switch_up = (sw == Switch::Up);
			if (gl_record_mode_ || gl_recording_active_) {
				switch_middle = true;
				switch_up = false;
			}

		/* ---- Switch mode transitions: lock track + re-arm takeover ---- */
		if (!gl_record_mode_ && !gl_recording_active_ && switch_up && gl_prev_switch_mode_ == Switch::Middle) {
			if (gl_active_track_ >= 0 && gl_active_track_ < MLR_NUM_TRACKS) {
				gl_locked_track_ = gl_active_track_;
			} else {
				gl_locked_track_ = gridless_track_idx_from_knob(KnobVal(Knob::Main));
			}
			knob_hard_takeover_arm(Knob::Main);
			knob_hard_takeover_arm(Knob::X);
			knob_hard_takeover_arm(Knob::Y);
		} else if (!gl_record_mode_ && !gl_recording_active_ && switch_middle && gl_prev_switch_mode_ == Switch::Up) {
			gl_active_deadzone_mute_ = false;
			knob_hard_takeover_arm(Knob::Main);
			knob_hard_takeover_arm(Knob::X);
			knob_hard_takeover_arm(Knob::Y);
		}

			/* ---- Track selection from Knob Main ---- */
			int knob_main = KnobVal(Knob::Main);  /* 0–4095 */
			int new_track = gl_active_track_;
			if (switch_up && gl_locked_track_ >= 0 && gl_locked_track_ < MLR_NUM_TRACKS) {
				new_track = gl_locked_track_;
			} else if (gl_active_track_ < 0 || (switch_middle && knob_hard_takeover_accept(Knob::Main, knob_main))) {
				int track_idx = gridless_track_idx_from_knob(knob_main);
				new_track = track_idx;
			}

			/* Audio In 1 modulation (if connected): channel wrap offset. */
			if (!gl_record_mode_ && !gl_recording_active_ && !gl_audio1_mod_rearm_required_ && Connected(Input::Audio1)) {
				int32_t a1 = AudioIn1();
				if (a1 < 0) a1 = -a1;
				if (a1 > 2047) a1 = 2047;
				int offset = (int)(((uint32_t)a1 * MLR_NUM_TRACKS) >> 11);  /* 0..6 */
				if (offset >= MLR_NUM_TRACKS) offset = MLR_NUM_TRACKS - 1;
				int base = (new_track >= 0 && new_track < MLR_NUM_TRACKS) ? new_track : 0;
				new_track = (base + offset) % MLR_NUM_TRACKS;
			}

			/* Pulse In 2 rising edge: advance to next channel (wrap 7->1). */
			if (gl_pulse2_pending_) {
				gl_pulse2_pending_ = false;
				int base = (new_track >= 0 && new_track < MLR_NUM_TRACKS) ? new_track : 0;
				new_track = (base + 1) % MLR_NUM_TRACKS;
				if (switch_up) gl_locked_track_ = new_track;
				knob_hard_takeover_arm(Knob::Main);
			}

			/* No channel switching during active recording. */
			if (gl_recording_active_ && gl_record_track_ >= 0) {
				new_track = gl_record_track_;
			}

			/* Track changed — switch to new track */
			if (new_track != gl_active_track_) {
				gl_active_track_ = new_track;

				/* Hard-cut selected channel from last accepted start position */
				mlr_cut(gl_active_track_, gl_prev_start_col_);

				/* Pulse out 2: track change trigger */
				PulseOut2(true);
			}

			/* ---- X knob layer controls ---- */
			int knob_x = KnobVal(Knob::X);  /* 0..4095 */
			bool cv1_connected = Connected(Input::CV1);
			int mod_x = knob_x;
			if (cv1_connected) {
				int cv1 = CVIn1(); /* -2048..2047 */
				mod_x = knob_x + cv1;
				if (mod_x < 0) mod_x = 0;
				if (mod_x > 4095) mod_x = 4095;
			}

			bool x_accept = knob_hard_takeover_accept(Knob::X, knob_x);
			if (!gl_recording_active_ && switch_up && gl_active_track_ >= 0 && (x_accept || cv1_connected)) {
			/* Channel mode: per-channel level slot, like grid mixer.
			 * Left=quietest slot 5, right=loudest slot 0. */
				int idx = (mod_x * MLR_NUM_VOL_SLOTS) >> 12;  /* 0..5 */
			if (idx >= MLR_NUM_VOL_SLOTS) idx = MLR_NUM_VOL_SLOTS - 1;
			int slot = (MLR_NUM_VOL_SLOTS - 1) - idx;
			mlr_set_volume(gl_active_track_, slot);
			} else if (!gl_recording_active_ && switch_middle && (x_accept || cv1_connected)) {
			/* Global mode: playback-layer level, unity at center. */
				if (mod_x <= 2048) {
					gl_playback_gain_frac_ = (uint16_t)(((uint32_t)mod_x * 256u) / 2048u);  /* 0..256 */
			} else {
					gl_playback_gain_frac_ = (uint16_t)(256u + (((uint32_t)(mod_x - 2048) * 256u) / 2047u));  /* 256..512 */
			}
			}

			/* ---- Y knob layer controls ---- */
			int knob_y = KnobVal(Knob::Y);  /* 0..4095 */
			bool cv2_connected = Connected(Input::CV2);
			int mod_y = knob_y;
			if (cv2_connected) {
				int cv2 = CVIn2(); /* -2048..2047 */
				mod_y = knob_y + cv2;
				if (mod_y < 0) mod_y = 0;
				if (mod_y > 4095) mod_y = 4095;
			}

			bool y_accept = knob_hard_takeover_accept(Knob::Y, knob_y);
			if (!gl_recording_active_ && switch_up && (y_accept || cv2_connected)) {
			/* Channel mode: reset start position for selected channel resets. */
				int start_col = (mod_y * MLR_GRID_COLS) >> 12;
			if (start_col >= MLR_GRID_COLS) start_col = MLR_GRID_COLS - 1;
			if (start_col < 0) start_col = 0;
			gl_prev_start_col_ = start_col;
			} else if (!gl_recording_active_ && switch_middle && (y_accept || cv2_connected)) {
			/* Global mode: radiate amount. */
				gl_radiate_frac_ = (uint16_t)(((uint32_t)mod_y * 256u) / 4095u);
			}

			/* ---- Channel mode speed from Knob Main ---- */
			if (switch_up && gl_active_track_ >= 0 && knob_hard_takeover_accept(Knob::Main, knob_main)) {
			int centered = knob_main - 2048;        /* -2048..+2047 */
			int magnitude = centered < 0 ? -centered : centered;
			const int deadzone = 64;
			const int max_mag = 2048 - deadzone;

			if (magnitude <= deadzone) {
				gl_active_deadzone_mute_ = true;
			} else {
				uint32_t speed_frac = ((uint32_t)(magnitude - deadzone) * 1024u) / (uint32_t)max_mag;
				if (speed_frac > 1024u) speed_frac = 1024u;

				if (speed_frac < 64u) {
					gl_active_deadzone_mute_ = true;
				} else {
					gl_active_deadzone_mute_ = false;
					mlr_set_reverse(gl_active_track_, centered < 0);
					gl_base_speed_frac_[gl_active_track_] = (uint16_t)speed_frac;
					mlr_set_speed_frac_nondeclick(gl_active_track_, (uint16_t)speed_frac);
				}
			}
			} else if (!switch_up) {
				gl_active_deadzone_mute_ = false;
			}

			/* Audio In 2 modulation (if connected): selected-channel speed.
			 * -6V => -2 oct (0.25x), 0V => 1x, +6V => +2 oct (4x).
			 * In this modulation mode speed is always forward. */
			if (gl_active_track_ >= 0) {
				if (!gl_record_mode_ && !gl_recording_active_ && !gl_audio2_mod_rearm_required_ && Connected(Input::Audio2)) {
					int32_t a2 = AudioIn2();
					if (a2 > 2047) a2 = 2047;
					if (a2 < -2048) a2 = -2048;
					uint16_t speed_frac;
					if (a2 >= 0) {
						speed_frac = (uint16_t)(256u + (((uint32_t)a2 * (1024u - 256u)) / 2047u));
					} else {
						speed_frac = (uint16_t)(256u - (((uint32_t)(-a2) * (256u - 64u)) / 2048u));
					}
					mlr_set_reverse(gl_active_track_, false);
					mlr_set_speed_frac_nondeclick(gl_active_track_, speed_frac);
				} else {
					mlr_set_speed_frac_nondeclick(gl_active_track_, gl_base_speed_frac_[gl_active_track_]);
				}
			}

			gridless_apply_mix_matrix(switch_middle);
			gridless_apply_level_layers();

			/* ---- Reset: Switch Down or Pulse In 1 rising edge ---- */
			bool switch_reset = gl_switch_down_rise_pending_;
			gl_switch_down_rise_pending_ = false;

			bool pulse_reset = gl_pulse1_pending_;
			gl_pulse1_pending_ = false;

			if (!gl_record_mode_ && !gl_recording_active_ && (switch_reset || pulse_reset) && gl_active_track_ >= 0) {
				mlr_cut(gl_active_track_, gl_prev_start_col_);

				/* Pulse out 1: reset trigger echo */
				PulseOut1(true);
			}

			gl_prev_switch_mode_ = static_cast<Switch>(sw);
		}

		/* ---- Mix and output ---- */
		uint8_t master_level = 255;  /* full volume — no master knob in gridless */
#ifdef MLR_STEREO
		int16_t mix_r;
		int16_t mix = mlr_play_mix_stereo(master_level, &mix_r);
		if (gl_recording_active_) {
			int32_t out_l = (int32_t)mix + (int32_t)rec_monitor_l;
			int32_t out_r = (int32_t)mix_r + (int32_t)rec_monitor_r;
			if (out_l > 2047) out_l = 2047;
			if (out_l < -2048) out_l = -2048;
			if (out_r > 2047) out_r = 2047;
			if (out_r < -2048) out_r = -2048;
			mix = (int16_t)out_l;
			mix_r = (int16_t)out_r;
		}
		AudioOut1(mix);
		AudioOut2(mix_r);
#else
		int16_t mix = mlr_play_mix(master_level);
		if (gl_recording_active_) {
			int32_t out_m = (int32_t)mix + (int32_t)rec_monitor_l;
			if (out_m > 2047) out_m = 2047;
			if (out_m < -2048) out_m = -2048;
			mix = (int16_t)out_m;
		}
		AudioOut1(mix);
#endif

		/* ---- LED update (sub-sampled) ---- */
		gl_led_counter_++;
		if (gl_led_counter_ >= LED_UPDATE_INTERVAL) {
			gl_led_counter_ = 0;
			gl_led_pulse_++;
			gridless_update_leds();
		}

	}

	/** Update the 6 card LEDs for gridless mode.
	 *  Track-to-LED mapping (zig-zag across the two columns of 3):
	 *    tr1→Led0  tr2→Led2  tr3→Led4
	 *    tr4→Led1  tr5→Led3  tr6→Led5
	 */
	static constexpr uint8_t kTrackLed[MLR_NUM_TRACKS] = {0, 2, 4, 1, 3, 5};

	void __not_in_flash("gridless") gridless_update_leds()
	{
		if (gl_active_track_ < 0 || gl_active_track_ >= MLR_NUM_TRACKS) {
			for (int i = 0; i < 6; i++) LedBrightness(i, 0);
			return;
		}

		int led = kTrackLed[gl_active_track_];
		for (int i = 0; i < 6; i++) LedBrightness(i, 0);

		if (gl_record_mode_ || gl_recording_active_) {
			bool on = gl_recording_active_ ? ((gl_led_pulse_ & 1) != 0)
			                               : ((gl_led_pulse_ & 2) != 0);
			if (!on) return;
			LedBrightness(led, 4095);
			return;
		}

		if (gl_record_hold_active_) {
			uint32_t lit = (gl_switch_down_hold_samples_ * 6u) / GRIDLESS_REC_HOLD_SAMPLES;
			if (lit > 6u) lit = 6u;
			for (int i = 0; i < 6; i++) {
				LedBrightness(i, ((uint32_t)i < lit) ? 4095 : 0);
			}
			return;
		}

		/* Show every track's effective level (volume slot × radiate × playback
		 * gain, including CV modulation).  volume_target is 0..1024 (Q8 with
		 * +6 dB headroom), already computed by gridless_apply_level_layers(). */
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			uint32_t vt = mlr_tracks[t].volume_target;  /* 0..1024 */
			uint32_t bri = (vt * 4095u) / 1024u;
			if (bri > 4095u) bri = 4095u;
			LedBrightness(kTrackLed[t], (uint16_t)bri);
		}
	}

	/* ================================================================ */
	/* Helper: build and dispatch an event (records into patterns too)  */
	/* ================================================================ */

	void __not_in_flash("dispatch_event") dispatch_event(uint8_t type, uint8_t track, int8_t a, int8_t b)
	{
		/* any manual interaction deselects the active recall,
		 * except recall events themselves (which set it). */
		if (type != MLR_EVT_RECALL)
			mlr_recall_active = -1;

		mlr_event_t e;
		e.timestamp_ms = 0;  /* filled by pattern recorder */
		e.type  = type;
		e.track = track;
		e.param_a = a;
		e.param_b = b;
		mlr_pattern_event(&e);  /* feed into recording patterns/recalls */
	}

	void process_bottom_master_control()
	{
		int row = MLR_NUM_TRACKS + 1;
		if (row > 7) return;
		if (!grid.keyDown()) return;
		if (grid.lastY() != row) return;

		int col = grid.lastX();
		if (col < 0) col = 0;
		if (col > 15) col = 15;

		int inv = 15 - col;  /* left=15, right=0 */
		uint16_t raw = (uint16_t)(((uint32_t)inv * 4095u + 7u) / 15u);
		if (raw > 4095u) raw = 4095u;
		mlr_master_level_raw = raw;
		master_knob_arm_raw_ = KnobVal(Knob::Main);
		master_knob_last_raw_ = master_knob_arm_raw_;
		master_knob_takeover_armed_ = true;
		mlr_master_override = true;  /* suppress pattern master events */

		dispatch_event(MLR_EVT_MASTER, (uint8_t)(raw >> 8), (int8_t)(raw & 0xFF), 0);
	}

	void resume_armed_track_if_needed(int track)
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return;
		if (resume_after_arm_track_ != track) return;
		resume_after_arm_track_ = -1;

		if (!mlr_tracks[track].has_content || mlr_tracks[track].playing)
			return;

		int col = mlr_get_column(track);
		if (col < 0) col = 0;
		if (col >= MLR_GRID_COLS) col = MLR_GRID_COLS - 1;
		mlr_cut(track, col);
	}

	void set_armed_track(int track)
	{
		if (track < -1 || track >= MLR_NUM_TRACKS) return;

		if (track == rec_armed_track) {
			resume_armed_track_if_needed(track);
			rec_armed_track = -1;
			return;
		}

		if (rec_armed_track >= 0)
			resume_armed_track_if_needed(rec_armed_track);

		rec_armed_track = track;
	}

	void record_history_push(int track)
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return;

		int8_t ordered[4] = {(int8_t)track, -1, -1, -1};
		int w = 1;
		for (int i = 0; i < 4 && w < 4; i++) {
			int prev = rec_recent_tracks_[i];
			if (prev < 0 || prev == track) continue;
			ordered[w++] = (int8_t)prev;
		}
		for (int i = 0; i < 4; i++)
			rec_recent_tracks_[i] = ordered[i];
	}

	bool in_recent_record_window(int track) const
	{
		for (int i = 0; i < 4; i++) {
			if (rec_recent_tracks_[i] == track) return true;
		}
		return false;
	}

	void update_record_monitor_window(int sw)
	{
		bool hold_window = (mlr_rec_track >= 0 || mlr_flushing);

		if (hold_window && !rec_monitor_window_active_) {
			rec_monitor_restore_pending_ = false;
			rec_monitor_restore_idx_ = 0;
			for (int t = 0; t < MLR_NUM_TRACKS; t++) {
				rec_monitor_prev_playing_[t] = mlr_tracks[t].playing;
				if (!in_recent_record_window(t) && mlr_tracks[t].playing)
					mlr_stop_track(t);
			}
			rec_monitor_window_active_ = true;
			return;
		}

		if (hold_window && rec_monitor_window_active_) {
			for (int t = 0; t < MLR_NUM_TRACKS; t++) {
				if (!in_recent_record_window(t) && mlr_tracks[t].playing)
					mlr_stop_track(t);
			}
			return;
		}

		if (!hold_window && rec_monitor_window_active_) {
			rec_monitor_window_active_ = false;
			rec_monitor_restore_pending_ = true;
			rec_monitor_restore_idx_ = 0;
		}

		if (rec_monitor_restore_pending_ && sw == Switch::Middle) {
			for (; rec_monitor_restore_idx_ < MLR_NUM_TRACKS; rec_monitor_restore_idx_++) {
				int t = rec_monitor_restore_idx_;
				if (!rec_monitor_prev_playing_[t])
					continue;

				rec_monitor_prev_playing_[t] = false;
				if (mlr_tracks[t].has_content && !mlr_tracks[t].playing) {
					int col = mlr_get_column(t);
					if (col < 0) col = 0;
					if (col >= MLR_GRID_COLS) col = MLR_GRID_COLS - 1;
					mlr_cut(t, col);
				}

				rec_monitor_restore_idx_++;
				break;  /* restore max one track per control tick */
			}

			if (rec_monitor_restore_idx_ >= MLR_NUM_TRACKS) {
				rec_monitor_restore_pending_ = false;
				rec_monitor_restore_idx_ = 0;
			}
		}
	}

	/* ================================================================ */
	/* Gate mode: col 15 hold detection (runs every sample, any page)  */
	/* ================================================================ */

	void __not_in_flash("process_gate_hold") process_gate_hold()
	{
		/* Arm on key-down of col 15 on any track row */
		if (grid.keyDown() && grid.lastX() == 15 && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;
			col15_armed[track] = true;
			col15_hold_time[track] = 0;
			gate_entered[track] = false;
		}

		/* Increment hold counters and enter gate mode at threshold */
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (col15_armed[t]) {
				col15_hold_time[t] += MAIN_CTRL_DIV;
				if (col15_hold_time[t] >= GATE_HOLD_THRESHOLD && !gate_entered[t]) {
					gate_entered[t] = true;
					gate_mode[t] = true;
					/* Stop the track when entering gate mode */
					if (mlr_tracks[t].playing) {
						mlr_stop_track(t);
						dispatch_event(MLR_EVT_STOP, (uint8_t)t, 0, 0);
					}
				}
			}
		}

		/* Key-up of col 15: short press action */
		if (grid.keyUp() && grid.lastX() == 15 && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;
			if (col15_armed[track] && !gate_entered[track]) {
				/* Short press */
				if (gate_mode[track]) {
					/* Disable gate mode and stop the track */
					gate_mode[track] = false;
					if (mlr_tracks[track].playing) {
						mlr_stop_track(track);
						dispatch_event(MLR_EVT_STOP, (uint8_t)track, 0, 0);
					}
				} else {
					/* Normal play/stop toggle */
					if (mlr_tracks[track].playing) {
						mlr_stop_track(track);
						dispatch_event(MLR_EVT_STOP, (uint8_t)track, 0, 0);
					} else if (mlr_tracks[track].has_content) {
						if (rec_armed_track == track) {
							rec_armed_track = -1;
							if (resume_after_arm_track_ == track) resume_after_arm_track_ = -1;
							rec_limit_latched = false;
						}
						int start_col = 0;
						if (mlr_tracks[track].loop_active)
							start_col = mlr_tracks[track].loop_col_start;
						mlr_cut(track, start_col);
						dispatch_event(MLR_EVT_START, (uint8_t)track, 0, 0);
					}
				}
			}
			col15_armed[track] = false;
			col15_hold_time[track] = 0;
			gate_entered[track] = false;
		}
	}

	/* ================================================================ */
	/* REC page: col 7 = reverse, cols 8–14 = speed (11 = 1x),         */
	/*           col 15 = stop/start                                   */
	/* ================================================================ */

	void __not_in_flash("process_page_rec") process_page_rec()
	{
		/* --- gated recording: no track armed + switch in rec + key up on col 0 = stop --- */
		if (grid.keyUp() && grid.lastX() == 0 && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;
			if (mlr_rec_track == track && rec_gated) {
				/* gated recording stop on key release */
				mlr_stop_record();
				rec_speed_accum = 0;
				rec_gated = false;
			}
		}

		if (!grid.keyDown()) return;
		if (grid.lastY() < 1 || grid.lastY() > 7) return;

		int track  = grid.lastY() - 1;
		int column = grid.lastX();
		bool alt_held = grid.held(14, 0);

		if (column == 0) {
			if (alt_held) {
				/* alt + col 0 = clear track */
				mlr_clear_track(track);
				if (rec_armed_track == track) {
					rec_armed_track = -1;
					if (resume_after_arm_track_ == track) resume_after_arm_track_ = -1;
				}
			} else {
				int sw_now = SwitchVal();
				bool rec_pos = (sw_now == Switch::Up || sw_now == Switch::Down);
				if (rec_pos && rec_armed_track < 0 && mlr_rec_track < 0 && !mlr_flushing && !rec_limit_latched && rec_start_lockout_samples_ == 0) {
					/* gated recording: start immediately on press */
					mlr_start_record(track);
					rec_speed_accum = 0;
					rec_gated = true;
				} else if (!rec_pos || rec_armed_track >= 0) {
					/* toggle record arm for this track */
					if (rec_armed_track == track) {
						set_armed_track(-1);  /* disarm and resume if this track was auto-stopped */
						rec_limit_latched = false;
					} else {
						set_armed_track(track);  /* arm this track, resuming any previously armed track */
						rec_limit_latched = false;
					}
				}
			}
			return;
		} else if (column >= 1 && column <= 6) {
			/* volume mixer: col 1 = unity (slot 0), col 6 = quietest (slot 5) */
			int slot = column - 1;
			mlr_set_volume(track, slot);
			dispatch_event(MLR_EVT_VOLUME, (uint8_t)track, (int8_t)slot, 0);
		} else if (column == 7) {
			/* toggle reverse */
			bool new_rev = !mlr_tracks[track].reverse;
			mlr_set_reverse(track, new_rev);
			dispatch_event(MLR_EVT_REVERSE, (uint8_t)track, new_rev ? 1 : 0, 0);
		} else if (column >= 8 && column <= 14) {
			/* speed slots: -2 oct, -1 oct, -5th, 1x, +5th, +1 oct, +2 oct */
			int speed = column - 11;  /* -3 .. +3 */
			mlr_set_speed(track, speed);
			dispatch_event(MLR_EVT_SPEED, (uint8_t)track, (int8_t)speed, 0);
		}
		/* col 15 is handled by process_gate_hold() */
	}

	/* ================================================================ */
	/* CUT page: playhead cutting + loop-a-section                     */
	/* ================================================================ */

	void __not_in_flash("process_page_cut") process_page_cut()
	{
		bool alt_held = grid.held(14, 0) || grid.held(0, 0);

		/* --- key-up on track rows --- */
		if (grid.keyUp() && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;

			/* gated recording: stop on col 0 or 15 release */
			if ((grid.lastX() == 0 || grid.lastX() == 15) && mlr_rec_track == track && rec_gated) {
				mlr_stop_record();
				rec_speed_accum = 0;
				rec_gated = false;
			}

			/* gated playback: stop when last key released */
			if (gate_mode[track] && mlr_tracks[track].playing) {
				int row = track + 1;
				bool any_held = false;
				for (int c = 0; c < 16; c++) {
					if (grid.held((uint8_t)c, (uint8_t)row)) { any_held = true; break; }
				}
				if (!any_held) {
					if (mlr_tracks[track].loop_active) {
						mlr_clear_loop(track);
						dispatch_event(MLR_EVT_LOOP_CLR, (uint8_t)track, 0, 0);
					}
					mlr_stop_track(track);
					dispatch_event(MLR_EVT_STOP, (uint8_t)track, 0, 0);
				}
			}
		}

		/* on key down on track rows */
		if (grid.keyDown() && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track  = grid.lastY() - 1;
			int column = grid.lastX();

			/* col 0 and 15 are utility keys */
			/* col 0: record functions, col 15: play/gate (handled by process_gate_hold) */
			if (column == 0) {
				if (alt_held) {
					mlr_clear_track(track);
					if (rec_armed_track == track) {
						rec_armed_track = -1;
						if (resume_after_arm_track_ == track) resume_after_arm_track_ = -1;
					}
				} else {
					int sw_now = SwitchVal();
					bool rec_pos = (sw_now == Switch::Up || sw_now == Switch::Down);
					if (rec_pos && rec_armed_track < 0 && mlr_rec_track < 0 && !mlr_flushing && !rec_limit_latched && rec_start_lockout_samples_ == 0) {
						mlr_start_record(track);
						rec_speed_accum = 0;
						rec_gated = true;
					} else if (!rec_pos || rec_armed_track >= 0) {
						if (rec_armed_track == track) {
							set_armed_track(-1);
							rec_limit_latched = false;
						} else {
							set_armed_track(track);
							rec_limit_latched = false;
						}
					}
				}
				return;
			}
			if (column == 15) {
				if (alt_held) {
					mlr_clear_track(track);
					if (rec_armed_track == track) {
						rec_armed_track = -1;
						if (resume_after_arm_track_ == track) resume_after_arm_track_ = -1;
					}
				}
				return;  /* play/stop/gate handled by process_gate_hold */
			}

			if (alt_held) {
				if (mlr_tracks[track].playing) {
					mlr_stop_track(track);
					dispatch_event(MLR_EVT_STOP, (uint8_t)track, 0, 0);
				}
				return;
			}

			/* Remap grid cols 1–14 → internal cols 0–13 */
			int cut_col = column - 1;

			if (rec_armed_track == track) {
				rec_armed_track = -1;
				if (resume_after_arm_track_ == track) resume_after_arm_track_ = -1;
				rec_limit_latched = false;
			}

			/* immediate cut — clears any active loop.
			 * For gated tracks this is monophonic: last key wins. */
			mlr_clear_loop(track);
			mlr_cut(track, cut_col);
			dispatch_event(MLR_EVT_LOOP_CLR, (uint8_t)track, 0, 0);
			dispatch_event(MLR_EVT_CUT, (uint8_t)track, (int8_t)cut_col, 0);
		}

		/* ---- loop-a-section: detect 2+ held keys per track row (cols 1–14 only) ---- */
		if (grid.keyDown() || grid.keyUp()) {
			for (int t = 0; t < MLR_NUM_TRACKS; t++) {
				int row = t + 1;
				int held_count = 0;
				int held_min = 16, held_max = -1;

				for (int c = 1; c <= 14; c++) {
					if (grid.held((uint8_t)c, (uint8_t)row)) {
						held_count++;
						int ic = c - 1;  /* internal col */
						if (ic < held_min) held_min = ic;
						if (ic > held_max) held_max = ic;
					}
				}

				if (held_count >= 2 && mlr_tracks[t].has_content) {
					mlr_set_loop(t, held_min, held_max);
					dispatch_event(MLR_EVT_LOOP, (uint8_t)t, (int8_t)held_min, (int8_t)held_max);
				}
			}
		}
	}

	/* ================================================================ */
	/* Grid display: shared row 0 pattern/recall LEDs                  */
	/* ================================================================ */

	void __not_in_flash("draw_row0_nav") draw_row0_nav()
	{
		/* pattern buttons: cols 4–7 (brighter at rest than recall keys) */
		static uint8_t pat_blink = 0;
		pat_blink++;
		for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
			int b = 4;  /* idle: brighter than recall keys */
			switch (mlr_patterns[p].state) {
			case MLR_PAT_ARMED:
				/* slow flash: toggle every 4 ticks (~200ms on/off) */
				b = (pat_blink & 4) ? 12 : 0;
				break;
			case MLR_PAT_RECORDING:
				/* fast flash: toggle every 2 ticks (~100ms on/off) */
				b = (pat_blink & 2) ? 15 : 0;
				break;
			case MLR_PAT_PLAYING:   b = 15; break;
			case MLR_PAT_STOPPED:   b = 6;  break;
			default:                b = 2;  break;
			}
			grid.led(p + 4, 0, b);
		}

		/* recall buttons: cols 9–12 */
		for (int r = 0; r < MLR_NUM_RECALLS; r++) {
			int b = 2;
			if (mlr_recall_active == r)           b = 15;  /* selected */
			else if (mlr_recalls[r].has_data)     b = 5;   /* has data */
			grid.led(r + 9, 0, b);
		}

		/* ALT */
		grid.led(14, 0, grid.held(14, 0) ? 12 : 2);
	}

	/* Grid bottom row (row 7 when 6 tracks): master gradient bar.
	 * Lowest level appears at the right, filling toward the left. */
	void __not_in_flash("draw_bottom_vu_row") draw_bottom_vu_row()
	{
		int row = MLR_NUM_TRACKS + 1;
		if (row > 7) return;

		uint8_t cell[16] = {};

		/* Master volume from latched master value (0..4095) as gradient bar right->left.
		 * Full scale reaches col 0. */
		uint32_t master_raw = (uint32_t)mlr_master_level_raw;
		uint32_t master_segs = ((master_raw * 16u) + 4094u) / 4095u;  /* ceil */
		if (master_segs > 16u) master_segs = 16u;
		for (uint32_t i = 0; i < master_segs; i++) {
			int col = 15 - (int)i;
			uint8_t g = (uint8_t)(2u + (i * 10u) / 15u);  /* smooth 2..12 ramp */
			cell[col] = g;
		}

		for (int col = 0; col < 16; col++) {
			grid.led(col, row, cell[col]);
		}
	}

	/* ================================================================ */
	/* Grid display: REC page                                          */
	/* ================================================================ */

	void __not_in_flash("update_grid_page_rec") update_grid_page_rec()
	{
		if (!grid.ready()) return;
		grid.clear();

		bool recording = (mlr_rec_track >= 0);
		bool rec_pos = (SwitchVal() == Switch::Up || SwitchVal() == Switch::Down);
		bool gated_rec_ready = rec_pos && rec_armed_track < 0;

		/* row 0: navigation — REC highlighted */
		grid.led(0, 0,  4);  /* CUT = dim */
		grid.led(1, 0, 15);  /* REC = active */
		draw_row0_nav();

		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			int row = t + 1;

			bool has = mlr_tracks[t].has_content;
			bool armed = (rec_armed_track == t);
			bool actively_rec = (recording && t == mlr_rec_track);

			/* col 0: record arm/status indicator */
			uint8_t col0_bright;
			if (actively_rec) {
				col0_bright = (rec_blink & 2) ? 15 : 4;
			} else if (armed) {
				col0_bright = (rec_blink & 8) ? 12 : 3;
			} else if (gated_rec_ready) {
				col0_bright = (rec_blink & 4) ? 10 : 3;
			} else {
				col0_bright = has ? 6 : 3;
			}
			grid.led(0, row, col0_bright);

			/* col 15: play/stop/gate indicator */
			uint8_t col15_bright;
			if (gate_mode[t]) {
				col15_bright = (gate_pulse & 8) ? 12 : 4;
				if (mlr_tracks[t].playing) col15_bright = 15;
			} else {
				col15_bright = mlr_tracks[t].playing ? 15 : (has ? 6 : 3);
			}
			grid.led(15, row, col15_bright);

			/* record progress: cols 1–6 (overlays mixer during recording) */
			if (actively_rec) {
				uint32_t progress = mlr_get_rec_progress() * 6 / MLR_MAX_SAMPLES;
				if (progress > 6) progress = 6;
				for (uint32_t c = 0; c < progress; c++)
					grid.led(c + 1, row, 12);
				if (progress < 6)
					grid.led(progress + 1, row, (rec_blink & 2) ? 8 : 0);
			} else if (has || armed) {
				/* volume mixer: cols 1–6, slot 2 (col 3) = unity */
				static uint8_t vol_gradient[6] = { 12, 9, 7, 5, 3, 1 };
				uint8_t cur_slot = mlr_tracks[t].volume_slot;
				for (int c = 0; c < 6; c++) {
					if ((uint8_t)c == cur_slot)
						grid.led(c + 1, row, 14);  /* active level */
					else if ((uint8_t)c > cur_slot)
						grid.led(c + 1, row, vol_gradient[c]);
					/* cols above current level stay off */
				}
			}

			grid.led(7,  row, mlr_tracks[t].reverse ? 12 : 4);

			/* cols 8–14: speed selection (col 11 = 1x) — show for all tracks */
			int speed_col = mlr_tracks[t].speed_shift + 11;
			if (speed_col >= 8 && speed_col <= 14)
				grid.led(speed_col, row, 14);
			if (speed_col != 11)
				grid.led(11, row, 3);  /* dim 1x indicator when not selected */
		}
		draw_bottom_vu_row();
		grid.markDirty();
	}

	/* ================================================================ */
	/* Grid display: CUT page                                          */
	/* ================================================================ */

	void __not_in_flash("update_grid_page_cut") update_grid_page_cut()
	{
		if (!grid.ready()) return;
		grid.clear();

		bool recording = (mlr_rec_track >= 0);
		bool rec_pos_cut = (SwitchVal() == Switch::Up || SwitchVal() == Switch::Down);
		bool gated_rec_ready_cut = rec_pos_cut && rec_armed_track < 0;

		/* row 0: navigation — CUT highlighted */
		grid.led(0, 0, 15);  /* CUT = active */
		grid.led(1, 0,  4);  /* REC = dim */
		draw_row0_nav();

		/* rows 1–6: playhead chase (cols 1–14) */
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			int row = t + 1;
			bool has = mlr_tracks[t].has_content;
			bool armed = (rec_armed_track == t);
			bool actively_rec = (recording && t == mlr_rec_track);

			/* col 0: record arm/status indicator */
			uint8_t col0_bright;
			if (actively_rec) {
				col0_bright = (rec_blink & 2) ? 15 : 4;
			} else if (armed) {
				col0_bright = (rec_blink & 8) ? 12 : 3;
			} else if (gated_rec_ready_cut) {
				col0_bright = (rec_blink & 4) ? 10 : 3;
			} else {
				col0_bright = has ? 6 : 3;
			}
			grid.led(0, row, col0_bright);

			/* col 15: play/stop/gate indicator */
			uint8_t col15_bright;
			if (gate_mode[t]) {
				col15_bright = (gate_pulse & 8) ? 12 : 4;
				if (mlr_tracks[t].playing) col15_bright = 15;
			} else {
				col15_bright = mlr_tracks[t].playing ? 15 : (has ? 6 : 3);
			}
			grid.led(15, row, col15_bright);

			/* recording track: progress bar on cols 1–14 */
			if (actively_rec) {
				uint32_t progress = mlr_get_rec_progress() * 14 / MLR_MAX_SAMPLES;
				if (progress > 14) progress = 14;
				for (uint32_t c = 0; c < progress; c++)
					grid.led(c + 1, row, 12);
				if (progress < 14)
					grid.led(progress + 1, row, (rec_blink & 2) ? 8 : 0);
				continue;
			}

			/* show loop zone dim even when stopped (only if sub-loop, not full track) */
			int loop_s = -1, loop_e = -1;
			mlr_get_loop_cols(t, &loop_s, &loop_e);
			if (loop_s >= 0 && !(loop_s == 0 && loop_e == MLR_GRID_COLS - 1)) {
				for (int c = loop_s; c <= loop_e; c++)
					grid.led(c + 1, row, 4);  /* +1 offset */
			}

			/* only the bright playhead depends on active playback */
			if (!has || !mlr_tracks[t].playing) continue;

			int col = mlr_get_column(t);  /* 0–13 internal */
			grid.led(col + 1, row, 15);  /* +1 offset */
		}
		draw_bottom_vu_row();
		grid.markDirty();
	}

	/* ---- grid display: flushing to flash ---- */
	void __not_in_flash("update_grid_flushing") update_grid_flushing()
	{
		if (!grid.ready()) return;
		grid.clear();

		/* saving animation: sweep across cols 1–14 */
		static uint8_t sweep = 0;
		sweep = (sweep + 1) % 14;
		int row = (mlr_get_flush_track() >= 0) ? mlr_get_flush_track() + 1 : 1;
		for (int c = 0; c < 14; c++) {
			int dist = (c - sweep);
			if (dist < 0) dist = -dist;
			int bright = 15 - dist * 2;
			if (bright < 0) bright = 0;
			grid.led(c + 1, row, bright);
		}
		draw_bottom_vu_row();
		grid.markDirty();
	}
};

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main()
{
	vreg_set_voltage(VREG_VOLTAGE_1_20);
	sleep_ms(10);
	set_sys_clock_khz(200000, true);

	/* DFP: we're the host → normal grid/audio mode */
	MLRCard card;
	card.Run();
}
