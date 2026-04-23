/**
 * mlr.c — MLR track engine v2 implementation.
 *
 * Playback: core 1 decodes ADPCM from flash → PCM ring buffer.
 *           core 0 reads PCM from ring buffer. Never touches flash.
 * Recording: core 0 encodes ADPCM into page ring.
 *            core 1 drains page ring to flash with JIT sector erase.
 * Seeking:   core 0 sets seek target, core 1 flushes ring + refills.
 */

#include "mlr.h"
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "pico/platform.h"

#ifndef XIP_BASE
#define XIP_BASE 0x10000000
#endif

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

mlr_track_t     mlr_tracks[MLR_NUM_TRACKS];
volatile int    mlr_rec_track  = -1;
volatile bool   mlr_flushing   = false;
mlr_page_ring_t mlr_page_ring;
volatile uint16_t mlr_master_level_raw = 4095;
volatile bool     mlr_master_override = false;

mlr_pattern_t   mlr_patterns[MLR_NUM_PATTERNS];
mlr_recall_t    mlr_recalls[MLR_NUM_RECALLS];
volatile bool   mlr_scene_saving = false;
volatile int    mlr_recall_active = -1;

/* Recording state */
static int      rec_track_idx     = -1;
static uint32_t rec_flash_offset;       /* next flash write offset */
static uint32_t rec_next_erase;         /* next sector to erase */
static uint32_t rec_samples;
static uint32_t rec_bytes;
static uint32_t rec_num_keyframes;
static mlr_keyframe_stereo_t rec_keyframes[MLR_MAX_KEYFRAMES];
static adpcm_state_t  rec_enc_state[MLR_NUM_CHANNELS];
static bool           rec_nybble_phase;
static uint8_t        rec_pending_byte;

/* Header write state */
static volatile bool hdr_write_pending = false;
static int           hdr_write_track   = -1;

/* Clear state */
static volatile bool clear_pending = false;
static int           clear_track   = -1;

/* Scene save state (core 1) — two-phase: erase then program pages */
static volatile bool scene_save_pending = false;
static int           scene_save_sector  = 0;   /* 0..MLR_SCENE_SECTORS-1 */
static bool          scene_sector_erased = false;
static int           scene_page_idx     = 0;   /* 0..pages_per_sector-1 */
#define SCENE_PAGES_PER_SECTOR (MLR_SECTOR_SIZE / MLR_PAGE_SIZE)  /* 16 */

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline const uint8_t *track_audio_xip(int t)
{
	return (const uint8_t *)(XIP_BASE + MLR_TRACK_OFFSET(t) + MLR_HEADER_SIZE);
}

static inline uint32_t track_audio_flash_off(int t)
{
	return MLR_TRACK_OFFSET(t) + MLR_HEADER_SIZE;
}

static inline uint32_t track_hdr_flash_off(int t)
{
	return MLR_TRACK_OFFSET(t);
}

static inline uint32_t pcm_ring_avail(mlr_pcm_ring_t *r)
{
	return r->w - r->r;
}

static inline uint32_t pcm_ring_free(mlr_pcm_ring_t *r)
{
	return MLR_RING_SAMPLES - pcm_ring_avail(r);
}

static inline uint32_t page_ring_used_pages(void)
{
	return (uint8_t)(mlr_page_ring.w - mlr_page_ring.r);
}

/* Convert speed slot to 8.8 fixed-point factor.
 * Slots are: -3=-2 oct, -2=-1 oct, -1=-5th, 0=1x,
 *            +1=+5th, +2=+1 oct, +3=+2 oct. */
static inline uint16_t speed_shift_to_frac(int8_t s)
{
	static uint16_t kSpeedFrac[7] = {
		64,   /* -2 octaves = 0.25x */
		128,  /* -1 octave  = 0.5x  */
		171,  /* -5th       ≈ 0.667x */
		256,  /* unison     = 1.0x  */
		384,  /* +5th       = 1.5x  */
		512,  /* +1 octave  = 2.0x  */
		1024, /* +2 octaves = 4.0x  */
	};
	if (s < -3) s = -3;
	if (s > 3) s = 3;
	return kSpeedFrac[s + 3];
}

/* Convert volume slot (0=loudest, 5=quietest) to 8.8 fixed-point.
 * Slot 2 = unity (0 dB).  Slots 0–1 boost above unity for
 * intentional soft clipping into the ADPCM codec. */
static inline uint16_t volume_slot_to_frac(uint8_t slot)
{
	static uint16_t kVolFrac[MLR_NUM_VOL_SLOTS] = {
		512,  /* 0: +6 dB  (boost)  */
		362,  /* 1: +3 dB  (boost)  */
		256,  /* 2: unity  (0 dB)   */
		181,  /* 3:        (-3 dB)  */
		128,  /* 4:        (-6 dB)  */
		45,   /* 5:        (-15 dB) */
	};
	if (slot >= MLR_NUM_VOL_SLOTS) slot = MLR_NUM_VOL_SLOTS - 1;
	return kVolFrac[slot];
}

static inline int16_t clamp16_interp(int32_t v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return (int16_t)v;
}

static inline int16_t linear_interp_q8(int16_t x0, int16_t x1, uint8_t frac)
{
	if (frac == 0) return x0;
	int32_t y = (int32_t)x0 + ((((int32_t)x1 - (int32_t)x0) * (int32_t)frac + 128) >> 8);
	return clamp16_interp(y);
}

static inline void reset_track_audio_state(mlr_track_t *tr)
{
	tr->speed_accum = 0;
	memset(tr->last_pcm, 0, sizeof(tr->last_pcm));
	memset(tr->interp_prev, 0, sizeof(tr->interp_prev));
	memset(tr->last_out, 0, sizeof(tr->last_out));
	memset(tr->out_hist, 0, sizeof(tr->out_hist));
	tr->out_hist_pos = 0;
	tr->declick_hist_pos = 0;
	tr->declick_count = 0;
	tr->stop_pending = false;
}

static inline void begin_track_declick(mlr_track_t *tr, bool stop_after)
{
	tr->declick_hist_pos = tr->out_hist_pos;
	tr->declick_count = MLR_DECLICK_SAMPLES;
	tr->stop_pending = stop_after;
}

static inline int16_t apply_declick_sample(const mlr_track_t *tr, int ch, int32_t target)
{
	uint32_t alpha = (uint32_t)(MLR_DECLICK_SAMPLES - tr->declick_count) + 1u; /* 1..N */
	if (alpha > MLR_DECLICK_SAMPLES) alpha = MLR_DECLICK_SAMPLES;
	uint8_t hist_idx = (uint8_t)((tr->declick_hist_pos + (uint8_t)(alpha - 1u)) & (MLR_DECLICK_SAMPLES - 1u));
	int32_t old = tr->out_hist[ch][hist_idx];
	return (int16_t)(((old * (int32_t)(MLR_DECLICK_SAMPLES - alpha)) +
	                 target * (int32_t)alpha) >> MLR_DECLICK_SHIFT);
}

static inline void push_track_output(mlr_track_t *tr, const int16_t *samples)
{
	uint8_t pos = tr->out_hist_pos;
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		tr->last_out[ch] = samples[ch];
		tr->out_hist[ch][pos] = samples[ch];
	}
	tr->out_hist_pos = (uint8_t)((pos + 1u) & (MLR_DECLICK_SAMPLES - 1u));
}

static inline void finish_pending_stop(mlr_track_t *tr)
{
	tr->playing = false;
	tr->pcm.r = tr->pcm.w;
	reset_track_audio_state(tr);
}

/* ------------------------------------------------------------------ */
/* Init — load track metadata from flash, pre-fill rings              */
/* ------------------------------------------------------------------ */

void mlr_init(void)
{
	memset(mlr_tracks, 0, sizeof(mlr_tracks));
	memset(&mlr_page_ring, 0, sizeof(mlr_page_ring));
	memset(mlr_patterns, 0, sizeof(mlr_patterns));
	memset(mlr_recalls, 0, sizeof(mlr_recalls));
	mlr_rec_track  = -1;
	mlr_flushing   = false;
	mlr_scene_saving = false;
	rec_track_idx  = -1;

	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		const mlr_track_header_t *hdr = (const mlr_track_header_t *)
			(XIP_BASE + MLR_TRACK_OFFSET(t));

		mlr_track_t *tr = &mlr_tracks[t];
		tr->pcm.w = 0;
		tr->pcm.r = 0;

		/* default speed/reverse for all tracks (including empty tracks) */
		tr->speed_shift = 0;
		tr->record_speed_shift = 0;
		tr->speed_frac  = 256;
		tr->speed_accum = 0;
		reset_track_audio_state(tr);
		tr->reverse     = false;
		tr->volume_slot = 2;
		tr->volume_frac = 256;
		tr->volume_target = 256;

		if (hdr->magic == MLR_MAGIC &&
		    hdr->sample_count > 0 &&
		    hdr->sample_count <= MLR_MAX_SAMPLES &&
		    hdr->num_keyframes <= MLR_MAX_KEYFRAMES) {

			tr->has_content    = true;
			tr->length_samples = hdr->sample_count;
			tr->length_bytes   = hdr->adpcm_bytes;
			tr->num_keyframes  = hdr->num_keyframes;
			tr->record_speed_shift = hdr->record_speed_shift;
			tr->playing        = false;
			tr->playhead       = 0;

			memcpy(tr->keyframes, hdr->keyframes,
			       hdr->num_keyframes * sizeof(mlr_keyframe_stereo_t));

			/* init fill state at beginning */
			tr->fill_byte_pos   = 0;
			tr->fill_high_nyb   = false;
			tr->fill_sample_pos = 0;
			for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
				tr->fill_decode[ch].predictor  = 0;
				tr->fill_decode[ch].step_index = 0;
				if (tr->num_keyframes > 0) {
					tr->fill_decode[ch].predictor  = tr->keyframes[0].ch[ch].predictor;
					tr->fill_decode[ch].step_index = tr->keyframes[0].ch[ch].step_index;
				}
			}
			tr->fill_seek_pending = false;

		}
	}

	/* load saved scenes + patterns (not track state — defaults stay) */
	mlr_scene_load();
}

/** Re-read a single track's header from flash.
 *  Call only when the track is stopped (not playing, not recording). */
void mlr_rescan_track(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;

	mlr_track_t *tr = &mlr_tracks[track];
	const mlr_track_header_t *hdr = (const mlr_track_header_t *)
		(XIP_BASE + MLR_TRACK_OFFSET(track));

	/* Reset playback state */
	tr->playing = false;
	tr->has_content = false;
	tr->pcm.w = 0;
	tr->pcm.r = 0;
	tr->speed_shift = 0;
	tr->record_speed_shift = 0;
	tr->speed_frac  = 256;
	tr->speed_accum = 0;
	reset_track_audio_state(tr);
	tr->reverse     = false;
	tr->loop_active = false;
	tr->volume_slot = 2;
	tr->volume_frac = 256;
	tr->volume_target = 256;
	tr->fill_seek_pending = false;

	if (hdr->magic == MLR_MAGIC &&
	    hdr->sample_count > 0 &&
	    hdr->sample_count <= MLR_MAX_SAMPLES &&
	    hdr->num_keyframes <= MLR_MAX_KEYFRAMES) {

		tr->has_content    = true;
		tr->length_samples = hdr->sample_count;
		tr->length_bytes   = hdr->adpcm_bytes;
		tr->num_keyframes  = hdr->num_keyframes;
		tr->record_speed_shift = hdr->record_speed_shift;
		tr->playhead       = 0;

		memcpy(tr->keyframes, hdr->keyframes,
		       hdr->num_keyframes * sizeof(mlr_keyframe_stereo_t));

		tr->fill_byte_pos   = 0;
		tr->fill_high_nyb   = false;
		tr->fill_sample_pos = 0;
		for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
			tr->fill_decode[ch].predictor  = 0;
			tr->fill_decode[ch].step_index = 0;
			if (tr->num_keyframes > 0) {
				tr->fill_decode[ch].predictor  = tr->keyframes[0].ch[ch].predictor;
				tr->fill_decode[ch].step_index = tr->keyframes[0].ch[ch].step_index;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Recording — core 0 encodes, core 1 flushes pages                   */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(mlr_start_record)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;

	mlr_track_t *tr = &mlr_tracks[track];

	/* fresh recording — stop any existing playback */
	tr->playing = false;
	tr->has_content = false;

	tr->pcm.w = 0;
	tr->pcm.r = 0;
	/* preserve speed_shift / speed_frac — recording is speed-linked */
	tr->record_speed_shift = tr->speed_shift;
	tr->speed_frac  = speed_shift_to_frac(tr->speed_shift);
	tr->speed_accum = 0;
	reset_track_audio_state(tr);
	/* preserve reverse — only affects playback, recording always forward */

	rec_track_idx       = track;
	mlr_rec_track       = track;
	rec_samples         = 0;
	rec_bytes           = 0;
	rec_num_keyframes   = 0;
	rec_flash_offset    = track_audio_flash_off(track);
	rec_next_erase      = rec_flash_offset;  /* first sector needs erase */

	mlr_page_ring.w    = 0;
	mlr_page_ring.r    = 0;
	mlr_page_ring.fill = 0;

	/* init encoder(s) */
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		rec_enc_state[ch].predictor  = 0;
		rec_enc_state[ch].step_index = 0;
	}
	rec_nybble_phase = false;
	rec_pending_byte = 0;

	/* save initial keyframe */
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		rec_keyframes[0].ch[ch].predictor  = 0;
		rec_keyframes[0].ch[ch].step_index = 0;
	}
	rec_num_keyframes = 1;
}

void __not_in_flash_func(mlr_record_sample)(int16_t sample)
{
	if (rec_track_idx < 0) return;

	/* scale 12-bit to 16-bit for better ADPCM quality */
	int16_t sample16 = sample << 4;

	uint8_t nybble = adpcm_encode(sample16, &rec_enc_state[0]);

	if (!rec_nybble_phase) {
		rec_pending_byte = nybble;
		rec_nybble_phase = true;
	} else {
		rec_pending_byte |= (nybble << 4);

		/* write byte into page ring */
		uint8_t slot = mlr_page_ring.w % MLR_PAGE_RING_COUNT;
		uint32_t fill = mlr_page_ring.fill;
		mlr_page_ring.pages[slot][fill] = rec_pending_byte;
		mlr_page_ring.fill = fill + 1;

		if (mlr_page_ring.fill >= MLR_PAGE_SIZE) {
			mlr_page_ring.fill = 0;
			__dmb();
			mlr_page_ring.w++;
		}

		rec_nybble_phase = false;
	}

	rec_samples++;

	/* save keyframe at regular intervals */
	if ((rec_samples % MLR_KEYFRAME_INTERVAL) == 0 &&
	    rec_num_keyframes < MLR_MAX_KEYFRAMES) {
		rec_keyframes[rec_num_keyframes].ch[0].predictor  = rec_enc_state[0].predictor;
		rec_keyframes[rec_num_keyframes].ch[0].step_index = rec_enc_state[0].step_index;
		rec_num_keyframes++;
	}
}

#ifdef MLR_STEREO
void __not_in_flash_func(mlr_record_sample_stereo)(int16_t left, int16_t right)
{
	if (rec_track_idx < 0) return;

	/* scale 12-bit to 16-bit for better ADPCM quality */
	int16_t l16 = left << 4;
	int16_t r16 = right << 4;

	/* Mid-Side transform for better stereo preservation in ADPCM.
	 * M = (L+R)/2, S = (L-R)/2.  Encoded as M nybble (low) + S nybble (high). */
	int16_t mid  = (int16_t)(((int32_t)l16 + (int32_t)r16) >> 1);
	int16_t side = (int16_t)(((int32_t)l16 - (int32_t)r16) >> 1);

	uint8_t m_nyb = adpcm_encode(mid,  &rec_enc_state[0]);
	uint8_t s_nyb = adpcm_encode(side, &rec_enc_state[1]);
	uint8_t byte = m_nyb | (s_nyb << 4);

	/* write byte into page ring */
	uint8_t slot = mlr_page_ring.w % MLR_PAGE_RING_COUNT;
	uint32_t fill = mlr_page_ring.fill;
	mlr_page_ring.pages[slot][fill] = byte;
	mlr_page_ring.fill = fill + 1;

	if (mlr_page_ring.fill >= MLR_PAGE_SIZE) {
		mlr_page_ring.fill = 0;
		__dmb();
		mlr_page_ring.w++;
	}

	rec_samples++;

	/* save keyframe at regular intervals */
	if ((rec_samples % MLR_KEYFRAME_INTERVAL) == 0 &&
	    rec_num_keyframes < MLR_MAX_KEYFRAMES) {
		for (int ch = 0; ch < 2; ch++) {
			rec_keyframes[rec_num_keyframes].ch[ch].predictor  = rec_enc_state[ch].predictor;
			rec_keyframes[rec_num_keyframes].ch[ch].step_index = rec_enc_state[ch].step_index;
		}
		rec_num_keyframes++;
	}
}
#endif

void __not_in_flash_func(mlr_stop_record)(void)
{
	if (rec_track_idx < 0) return;
	mlr_track_t *tr = &mlr_tracks[rec_track_idx];
	mlr_flushing = true;

	/* flush any partial nybble */
	if (rec_nybble_phase) {
		uint8_t slot = mlr_page_ring.w % MLR_PAGE_RING_COUNT;
		mlr_page_ring.pages[slot][mlr_page_ring.fill] = rec_pending_byte;
		mlr_page_ring.fill++;
		rec_nybble_phase = false;
	}

	/* flush partial page (pad with 0xFF) */
	if (mlr_page_ring.fill > 0) {
		uint8_t slot = mlr_page_ring.w % MLR_PAGE_RING_COUNT;
		memset(&mlr_page_ring.pages[slot][mlr_page_ring.fill], 0xFF,
		       MLR_PAGE_SIZE - mlr_page_ring.fill);
		mlr_page_ring.fill = 0;
		__dmb();
		mlr_page_ring.w++;
	}

	/* calculate actual ADPCM byte count */
#ifdef MLR_STEREO
	uint32_t actual_bytes = rec_samples;  /* 1 byte per stereo frame */
#else
	uint32_t actual_bytes = (rec_samples + 1) / 2;  /* 2 mono samples per byte */
#endif

	/* update track metadata — track becomes playable once header is written */
	tr->length_samples = rec_samples;
	tr->length_bytes   = actual_bytes;
	tr->num_keyframes  = rec_num_keyframes;
	memcpy(tr->keyframes, rec_keyframes,
	       rec_num_keyframes * sizeof(mlr_keyframe_stereo_t));

	hdr_write_track   = rec_track_idx;
	hdr_write_pending = true;

	rec_track_idx = -1;
	mlr_rec_track = -1;
}

/* ------------------------------------------------------------------ */
/* Playback — core 0 reads decoded PCM from ring buffers              */
/* ------------------------------------------------------------------ */

int16_t __not_in_flash_func(mlr_play_mix)(uint8_t volume)
{
	int32_t mix = 0;

	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_t *tr = &mlr_tracks[t];
		if (!tr->has_content || (!tr->playing && !tr->stop_pending)) continue;

		/* wrap boundaries */
		uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
		uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
		bool wrapped = false;

		if (!tr->stop_pending) {
			/* variable-speed sample consumption via accumulator */
			tr->speed_accum += tr->speed_frac;
			while (tr->speed_accum >= 256) {
				tr->speed_accum -= 256;
				if (pcm_ring_avail(&tr->pcm) > 0) {
					tr->interp_prev[0] = tr->last_pcm[0];
					tr->last_pcm[0] = tr->pcm.buf[(tr->pcm.r % MLR_RING_SAMPLES) * MLR_NUM_CHANNELS];
					tr->pcm.r++;
					if (tr->reverse) {
						if (tr->playhead <= wrap_start) {
							tr->playhead = wrap_end > 0 ? wrap_end - 1 : 0;
							wrapped = true;
						} else {
							tr->playhead--;
						}
					} else {
						tr->playhead++;
						if (tr->playhead >= wrap_end) {
							tr->playhead = wrap_start;
							wrapped = true;
						}
					}
				}
			}
			if (wrapped) begin_track_declick(tr, false);
		}

		/* Linear interpolation between current and next sample.
		 * Fractional phase is speed_accum in Q8 (0..255). */
		int32_t sample_out = 0;
		if (!tr->stop_pending) {
			uint32_t avail = pcm_ring_avail(&tr->pcm);
			int16_t x0 = tr->last_pcm[0];
			int16_t x1 = x0;
			if (avail > 0) {
				uint32_t idx1 = (tr->pcm.r % MLR_RING_SAMPLES) * MLR_NUM_CHANNELS;
				x1 = tr->pcm.buf[idx1];
			}

			sample_out = linear_interp_q8(x0, x1, (uint8_t)tr->speed_accum);
		}

		/* apply per-track volume (slew toward target to avoid clicks) */
		if (tr->volume_frac < tr->volume_target) {
			tr->volume_frac += 4;
			if (tr->volume_frac > tr->volume_target)
				tr->volume_frac = tr->volume_target;
		} else if (tr->volume_frac > tr->volume_target) {
			if (tr->volume_frac < 4)
				tr->volume_frac = tr->volume_target;
			else
				tr->volume_frac -= 4;
			if (tr->volume_frac < tr->volume_target)
				tr->volume_frac = tr->volume_target;
		}
		sample_out = (sample_out * (int32_t)tr->volume_frac) >> 8;
		if (tr->declick_count > 0) {
			sample_out = apply_declick_sample(tr, 0, sample_out);
			tr->declick_count--;
			if (tr->declick_count == 0 && tr->stop_pending) {
				finish_pending_stop(tr);
				sample_out = 0;
			}
		}
		int16_t out_sample[MLR_NUM_CHANNELS] = {(int16_t)sample_out};
		push_track_output(tr, out_sample);

		if (!tr->muted)
			mix += sample_out;
	}

	if (mix == 0) return 0;
	mix = (mix * (int32_t)volume) >> 8;
	if (mix > 32767) mix = 32767;
	if (mix < -32768) mix = -32768;
	return (int16_t)(mix >> 4);
}
#ifdef MLR_STEREO
int16_t __not_in_flash_func(mlr_play_mix_stereo)(uint8_t volume, int16_t *out_right)
{
	int32_t mixL = 0, mixR = 0;

	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_t *tr = &mlr_tracks[t];
		if (!tr->has_content || !tr->playing) continue;

		uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
		uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
		bool wrapped = false;

		if (!tr->stop_pending) {
			tr->speed_accum += tr->speed_frac;
			while (tr->speed_accum >= 256) {
				tr->speed_accum -= 256;
				if (pcm_ring_avail(&tr->pcm) > 0) {
					uint32_t idx = (tr->pcm.r % MLR_RING_SAMPLES) * 2;
					tr->interp_prev[0] = tr->last_pcm[0];
					tr->interp_prev[1] = tr->last_pcm[1];
					tr->last_pcm[0] = tr->pcm.buf[idx];
					tr->last_pcm[1] = tr->pcm.buf[idx + 1];
					tr->pcm.r++;
					if (tr->reverse) {
						if (tr->playhead <= wrap_start) {
							tr->playhead = wrap_end > 0 ? wrap_end - 1 : 0;
							wrapped = true;
						} else {
							tr->playhead--;
						}
					} else {
						tr->playhead++;
						if (tr->playhead >= wrap_end) {
							tr->playhead = wrap_start;
							wrapped = true;
						}
					}
				}
			}
			if (wrapped) begin_track_declick(tr, false);
		}

		int32_t sL = 0;
		int32_t sR = 0;
		if (!tr->stop_pending) {
			uint32_t avail = pcm_ring_avail(&tr->pcm);
			int16_t x0L = tr->last_pcm[0];
			int16_t x0R = tr->last_pcm[1];
			int16_t x1L = x0L, x1R = x0R;
			if (avail > 0) {
				uint32_t idx1 = (tr->pcm.r % MLR_RING_SAMPLES) * 2;
				x1L = tr->pcm.buf[idx1];
				x1R = tr->pcm.buf[idx1 + 1];
			}

			uint8_t frac = (uint8_t)tr->speed_accum;
			sL = linear_interp_q8(x0L, x1L, frac);
			sR = linear_interp_q8(x0R, x1R, frac);
		}

		/* volume slew */
		if (tr->volume_frac < tr->volume_target) {
			tr->volume_frac += 4;
			if (tr->volume_frac > tr->volume_target) tr->volume_frac = tr->volume_target;
		} else if (tr->volume_frac > tr->volume_target) {
			if (tr->volume_frac < 4) tr->volume_frac = tr->volume_target;
			else tr->volume_frac -= 4;
			if (tr->volume_frac < tr->volume_target) tr->volume_frac = tr->volume_target;
		}
		sL = (sL * (int32_t)tr->volume_frac) >> 8;
		sR = (sR * (int32_t)tr->volume_frac) >> 8;

		if (tr->declick_count > 0) {
			sL = apply_declick_sample(tr, 0, sL);
			sR = apply_declick_sample(tr, 1, sR);
			tr->declick_count--;
			if (tr->declick_count == 0 && tr->stop_pending) {
				finish_pending_stop(tr);
				sL = 0;
				sR = 0;
			}
		}
		int16_t out_sample[MLR_NUM_CHANNELS] = {(int16_t)sL, (int16_t)sR};
		push_track_output(tr, out_sample);

		if (!tr->muted) {
			mixL += sL;
			mixR += sR;
		}
	}

	mixL = (mixL * (int32_t)volume) >> 8;
	mixR = (mixR * (int32_t)volume) >> 8;
	if (mixL > 32767) mixL = 32767; if (mixL < -32768) mixL = -32768;
	if (mixR > 32767) mixR = 32767; if (mixR < -32768) mixR = -32768;
	*out_right = (int16_t)(mixR >> 4);
	return (int16_t)(mixL >> 4);
}
#endif

/* ------------------------------------------------------------------ */
/* Seeking                                                            */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(mlr_cut)(int track, int column)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content) return;

	uint32_t target = (uint32_t)column * tr->length_samples / MLR_GRID_COLS;
	if (target >= tr->length_samples) target = tr->length_samples - 1;

	/* signal core 1 to refill from this position */
	begin_track_declick(tr, false);
	tr->seek_target_sample = target;
	__dmb();
	tr->fill_seek_pending = true;
	tr->playing = true;

	/* flush the PCM ring so core 0 stops reading stale data */
	tr->pcm.r = tr->pcm.w;
	tr->speed_accum = 0;
	tr->playhead = target;
}

void __not_in_flash_func(mlr_cut_sample)(int track, uint32_t sample_pos)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content) return;

	if (sample_pos >= tr->length_samples)
		sample_pos = tr->length_samples - 1;

	begin_track_declick(tr, false);
	tr->seek_target_sample = sample_pos;
	__dmb();
	tr->fill_seek_pending = true;
	tr->playing = true;

	tr->pcm.r = tr->pcm.w;
	tr->speed_accum = 0;
	tr->playhead = sample_pos;
}

void __not_in_flash_func(mlr_stop_track)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->playing || tr->stop_pending) return;
	begin_track_declick(tr, true);
}

/* ------------------------------------------------------------------ */
/* Loop-a-section                                                     */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(mlr_set_loop)(int track, int col_start, int col_end)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content || tr->length_samples == 0) return;

	/* ensure col_start <= col_end */
	if (col_start > col_end) { int tmp = col_start; col_start = col_end; col_end = tmp; }
	if (col_start == col_end) return;  /* need at least 2 different columns */

	uint32_t start_s = (uint32_t)col_start * tr->length_samples / MLR_GRID_COLS;
	uint32_t end_s   = (uint32_t)(col_end + 1) * tr->length_samples / MLR_GRID_COLS;
	if (end_s > tr->length_samples) end_s = tr->length_samples;
	if (start_s >= end_s) return;

	tr->loop_start_sample = start_s;
	tr->loop_end_sample   = end_s;
	tr->loop_col_start    = col_start;
	tr->loop_col_end      = col_end;
	__dmb();
	tr->loop_active       = true;
	begin_track_declick(tr, false);

	/* ALWAYS seek core 1 fill to new playhead bounds */
	if (tr->playhead < start_s || tr->playhead >= end_s) {
		tr->playhead = start_s;
	}
	tr->seek_target_sample = tr->playhead;
	__dmb();
	tr->fill_seek_pending = true;
	tr->pcm.r = tr->pcm.w;
}

void __not_in_flash_func(mlr_clear_loop)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->loop_active) return;

	tr->loop_active = false;
	begin_track_declick(tr, false);
	
	/* Seek core 1 so it stops feeding stale loop samples */
	tr->seek_target_sample = tr->playhead;
	__dmb();
	tr->fill_seek_pending = true;
	tr->pcm.r = tr->pcm.w;
}

void __not_in_flash_func(mlr_set_speed)(int track, int speed_shift)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (speed_shift < -3) speed_shift = -3;
	if (speed_shift >  3) speed_shift =  3;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->speed_shift = (int8_t)speed_shift;
	tr->speed_frac  = speed_shift_to_frac((int8_t)speed_shift);
	tr->speed_accum = 0;
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++)
		tr->interp_prev[ch] = tr->last_pcm[ch];
	begin_track_declick(tr, false);
}

void __not_in_flash_func(mlr_set_speed_frac)(int track, uint16_t speed_frac)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (speed_frac < 64) speed_frac = 64;
	if (speed_frac > 1024) speed_frac = 1024;

	mlr_track_t *tr = &mlr_tracks[track];
	tr->speed_frac  = speed_frac;
	tr->speed_accum = 0;
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++)
		tr->interp_prev[ch] = tr->last_pcm[ch];
	begin_track_declick(tr, false);
}

void __not_in_flash_func(mlr_set_speed_frac_nondeclick)(int track, uint16_t speed_frac)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (speed_frac < 64) speed_frac = 64;
	if (speed_frac > 1024) speed_frac = 1024;

	mlr_track_t *tr = &mlr_tracks[track];
	tr->speed_frac = speed_frac;
}

void __not_in_flash_func(mlr_set_reverse)(int track, bool reverse)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (tr->reverse == reverse) return;  /* no change */
	tr->reverse = reverse;

	/* re-seek core 1 fill to current playhead in new direction */
	tr->seek_target_sample = tr->playhead;
	__dmb();
	tr->fill_seek_pending = true;
	tr->pcm.r = tr->pcm.w;  /* flush ring */
}

void __not_in_flash_func(mlr_set_volume)(int track, int slot)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (slot < 0) slot = 0;
	if (slot >= MLR_NUM_VOL_SLOTS) slot = MLR_NUM_VOL_SLOTS - 1;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->volume_slot = (uint8_t)slot;
	tr->volume_target = volume_slot_to_frac((uint8_t)slot);
	/* If not playing, apply immediately (no audio to slew) */
	if (!tr->playing)
		tr->volume_frac = tr->volume_target;
}

void __not_in_flash_func(mlr_get_loop_cols)(int track, int *col_start, int *col_end)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) { *col_start = -1; *col_end = -1; return; }
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->loop_active) { *col_start = -1; *col_end = -1; return; }
	*col_start = tr->loop_col_start;
	*col_end   = tr->loop_col_end;
}

void __not_in_flash_func(mlr_clear_track)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->playing        = false;
	tr->has_content    = false;
	tr->length_samples = 0;
	tr->length_bytes   = 0;
	tr->num_keyframes  = 0;
	tr->playhead       = 0;
	tr->loop_active    = false;
	tr->fill_byte_pos   = 0;
	tr->fill_high_nyb   = false;
	tr->fill_sample_pos = 0;
	tr->fill_seek_pending = false;
	tr->seek_target_sample = 0;
	tr->speed_shift = 0;
	tr->record_speed_shift = 0;
	tr->speed_frac  = 256;
	tr->speed_accum = 0;
	memset(tr->last_pcm, 0, sizeof(tr->last_pcm));
	memset(tr->interp_prev, 0, sizeof(tr->interp_prev));
	tr->reverse     = false;
	tr->volume_slot = 2;
	tr->volume_frac = 256;
	tr->volume_target = 256;
	tr->pcm.w = 0;
	tr->pcm.r = 0;

	/* schedule flash erase of header sector */
	clear_track   = track;
	clear_pending = true;
}

int __not_in_flash_func(mlr_get_column)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return 0;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content || tr->length_samples == 0) return 0;
	return (int)((uint32_t)tr->playhead * MLR_GRID_COLS / tr->length_samples);
}

uint32_t __not_in_flash_func(mlr_get_rec_progress)(void)
{
	return rec_samples;
}

int __not_in_flash_func(mlr_get_flush_track)(void)
{
	if (hdr_write_pending) return hdr_write_track;
	if (clear_pending) return clear_track;
	return -1;
}

/* ------------------------------------------------------------------ */
/* Event dispatch — execute a pattern/recall event                    */
/* ------------------------------------------------------------------ */

static void __not_in_flash_func(event_exec)(const mlr_event_t *e)
{
	switch (e->type) {
	case MLR_EVT_CUT:
		mlr_cut(e->track, e->param_a);
		break;
	case MLR_EVT_STOP:
		mlr_stop_track(e->track);
		break;
	case MLR_EVT_START:
		mlr_cut(e->track, 0);  /* start from beginning */
		break;
	case MLR_EVT_SPEED:
		mlr_set_speed(e->track, e->param_a);
		break;
	case MLR_EVT_REVERSE:
		mlr_set_reverse(e->track, e->param_a != 0);
		break;
	case MLR_EVT_LOOP:
		mlr_set_loop(e->track, e->param_a, e->param_b);
		break;
	case MLR_EVT_LOOP_CLR:
		mlr_clear_loop(e->track);
		break;
	case MLR_EVT_VOLUME:
		mlr_set_volume(e->track, e->param_a);
		break;
	case MLR_EVT_PAT_PLAY:
		if (e->track < MLR_NUM_PATTERNS &&
		    mlr_patterns[e->track].count > 0)
			mlr_pattern_play_start(e->track);
		break;
	case MLR_EVT_PAT_STOP:
		if (e->track < MLR_NUM_PATTERNS)
			mlr_pattern_play_stop(e->track);
		break;
	case MLR_EVT_RECALL:
		if (e->track < MLR_NUM_RECALLS)
			mlr_recall_exec(e->track);
		break;
	case MLR_EVT_RECALL_UNDO:
		mlr_recall_undo();
		break;
	case MLR_EVT_MASTER: {
		if (mlr_master_override) break;  /* suppressed until loop wrap */
		uint16_t raw = ((uint16_t)e->track << 8) | (uint8_t)e->param_a;
		if (raw > 4095) raw = 4095;
		mlr_master_level_raw = raw;
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Pattern engine — timed event recorder / looping player             */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(mlr_pattern_event)(const mlr_event_t *e)
{
	/* transition armed patterns to recording on first event */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (mlr_patterns[p].state == MLR_PAT_ARMED) {
			mlr_patterns[p].rec_start_ms = to_ms_since_boot(get_absolute_time());
			mlr_patterns[p].state = MLR_PAT_RECORDING;
		}
	}

	/* feed into all recording patterns */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (mlr_patterns[p].state != MLR_PAT_RECORDING) continue;
		if (mlr_patterns[p].count >= MLR_PATTERN_MAX_EVENTS) continue;

		mlr_pattern_t *pat = &mlr_patterns[p];
		uint32_t now = to_ms_since_boot(get_absolute_time());
		uint16_t idx = pat->count;
		pat->events[idx] = *e;
		pat->events[idx].timestamp_ms = now - pat->rec_start_ms;
		pat->count = idx + 1;
	}
}

void __not_in_flash_func(mlr_pattern_arm)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	/* disarm any other armed pattern */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (p != pat && mlr_patterns[p].state == MLR_PAT_ARMED)
			mlr_patterns[p].state = MLR_PAT_IDLE;
	}
	mlr_pattern_t *p = &mlr_patterns[pat];
	p->count = 0;
	p->play_idx = 0;
	p->loop_len_ms = 0;
	p->state = MLR_PAT_ARMED;
}

void __not_in_flash_func(mlr_pattern_rec_start)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	p->count = 0;
	p->play_idx = 0;
	p->loop_len_ms = 0;
	p->rec_start_ms = to_ms_since_boot(get_absolute_time());
	p->state = MLR_PAT_RECORDING;
}

void __not_in_flash_func(mlr_pattern_rec_stop)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	if (p->state != MLR_PAT_RECORDING && p->state != MLR_PAT_ARMED) return;

	if (p->state == MLR_PAT_ARMED) {
		/* was armed but never started — go back to idle */
		p->state = MLR_PAT_IDLE;
		return;
	}

	uint32_t now = to_ms_since_boot(get_absolute_time());
	p->loop_len_ms = now - p->rec_start_ms;
	if (p->loop_len_ms < 10) p->loop_len_ms = 10;  /* minimum loop length */

	if (p->count == 0) {
		p->state = MLR_PAT_IDLE;
	} else {
		p->state = MLR_PAT_STOPPED;
	}
}

void __not_in_flash_func(mlr_pattern_play_start)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	if (p->count == 0) return;

	p->play_idx = 0;
	p->play_start_ms = to_ms_since_boot(get_absolute_time());
	p->state = MLR_PAT_PLAYING;

	/* fire the first event immediately */
	event_exec(&p->events[0]);
	p->play_idx = 1;
}

void __not_in_flash_func(mlr_pattern_play_stop)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	if (p->state == MLR_PAT_PLAYING) {
		p->state = MLR_PAT_STOPPED;
	}
}

void __not_in_flash_func(mlr_pattern_clear)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	p->state = MLR_PAT_IDLE;
	p->count = 0;
	p->play_idx = 0;
	p->loop_len_ms = 0;
}

void __not_in_flash_func(mlr_pattern_tick)(uint32_t now_ms)
{
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		mlr_pattern_t *pat = &mlr_patterns[p];
		if (pat->state != MLR_PAT_PLAYING) continue;
		if (pat->count == 0) continue;

		uint32_t elapsed = now_ms - pat->play_start_ms;

		/* check for loop wrap */
		if (elapsed >= pat->loop_len_ms) {
			/* wrap: restart from beginning */
			pat->play_start_ms = now_ms;
			pat->play_idx = 0;
			elapsed = 0;
			mlr_master_override = false;  /* allow pattern master events again */
		}

		/* fire all events whose timestamp has been reached */
		while (pat->play_idx < pat->count &&
		       pat->events[pat->play_idx].timestamp_ms <= elapsed) {
			event_exec(&pat->events[pat->play_idx]);
			pat->play_idx++;
			pat->event_flash = 1;  /* flash for 1 LED update (~50ms) */
		}
	}
}

/* ------------------------------------------------------------------ */
/* Recall engine — instant snapshot replay                            */
/* ------------------------------------------------------------------ */

/* Undo buffer — stores pre-recall state */
static mlr_recall_t recall_undo;

/** Fill a recall struct with a snapshot of current state. */
static void snapshot_into(mlr_recall_t *r)
{
	r->count = 0;

	/* master volume */
	if (r->count < MLR_PATTERN_MAX_EVENTS) {
		uint16_t raw = mlr_master_level_raw;
		if (raw > 4095) raw = 4095;
		r->events[r->count].type = MLR_EVT_MASTER;
		r->events[r->count].track = (uint8_t)(raw >> 8);
		r->events[r->count].param_a = (int8_t)(raw & 0xFF);
		r->events[r->count].param_b = 0;
		r->events[r->count].timestamp_ms = 0;
		r->count++;
	}

	/* capture current state of all tracks */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_t *tr = &mlr_tracks[t];
		if (r->count >= MLR_PATTERN_MAX_EVENTS) break;

		/* speed */
		r->events[r->count].type = MLR_EVT_SPEED;
		r->events[r->count].track = (uint8_t)t;
		r->events[r->count].param_a = tr->speed_shift;
		r->events[r->count].param_b = 0;
		r->events[r->count].timestamp_ms = 0;
		r->count++;

		/* reverse */
		if (r->count < MLR_PATTERN_MAX_EVENTS) {
			r->events[r->count].type = MLR_EVT_REVERSE;
			r->events[r->count].track = (uint8_t)t;
			r->events[r->count].param_a = tr->reverse ? 1 : 0;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}

		/* volume */
		if (r->count < MLR_PATTERN_MAX_EVENTS) {
			r->events[r->count].type = MLR_EVT_VOLUME;
			r->events[r->count].track = (uint8_t)t;
			r->events[r->count].param_a = (int8_t)tr->volume_slot;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}

		/* loop (or clear loop) */
		if (r->count < MLR_PATTERN_MAX_EVENTS) {
			if (tr->loop_active) {
				r->events[r->count].type = MLR_EVT_LOOP;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = (int8_t)tr->loop_col_start;
				r->events[r->count].param_b = (int8_t)tr->loop_col_end;
			} else {
				r->events[r->count].type = MLR_EVT_LOOP_CLR;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = 0;
				r->events[r->count].param_b = 0;
			}
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}

		/* playing/stopped: cut to current position or stop */
		if (r->count < MLR_PATTERN_MAX_EVENTS && tr->has_content) {
			if (tr->playing) {
				int col = (int)((uint32_t)tr->playhead * MLR_GRID_COLS / tr->length_samples);
				r->events[r->count].type = MLR_EVT_CUT;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = (int8_t)col;
				r->events[r->count].param_b = 0;
			} else {
				r->events[r->count].type = MLR_EVT_STOP;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = 0;
				r->events[r->count].param_b = 0;
			}
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}
	}

	/* capture motion record lane (pattern) play state */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (r->count >= MLR_PATTERN_MAX_EVENTS) break;
		if (mlr_patterns[p].state == MLR_PAT_PLAYING) {
			r->events[r->count].type = MLR_EVT_PAT_PLAY;
			r->events[r->count].track = (uint8_t)p;
			r->events[r->count].param_a = 0;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		} else if (mlr_patterns[p].state == MLR_PAT_STOPPED ||
		           mlr_patterns[p].state == MLR_PAT_RECORDING ||
		           mlr_patterns[p].state == MLR_PAT_ARMED) {
			/* stop any playing pattern when recalling */
			r->events[r->count].type = MLR_EVT_PAT_STOP;
			r->events[r->count].track = (uint8_t)p;
			r->events[r->count].param_a = 0;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}
	}

	r->recording = false;
	r->has_data = true;
}

void mlr_recall_snapshot(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	snapshot_into(&mlr_recalls[slot]);
	mlr_recall_active = slot;
}

void mlr_recall_exec(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	mlr_recall_t *r = &mlr_recalls[slot];
	if (!r->has_data) return;

	/* snapshot current state into undo buffer before applying */
	snapshot_into(&recall_undo);

	for (uint16_t i = 0; i < r->count; i++) {
		event_exec(&r->events[i]);
	}
	mlr_recall_active = slot;
}

void mlr_recall_undo(void)
{
	if (!recall_undo.has_data) return;
	for (uint16_t i = 0; i < recall_undo.count; i++) {
		event_exec(&recall_undo.events[i]);
	}
	recall_undo.has_data = false;
	mlr_recall_active = -1;
}

void mlr_recall_exec_and_record(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	mlr_recall_t *r = &mlr_recalls[slot];
	if (!r->has_data) return;

	snapshot_into(&recall_undo);

	for (uint16_t i = 0; i < r->count; i++) {
		event_exec(&r->events[i]);
		/* Feed each individual state event into motion pattern recording.
		 * Skip PAT_PLAY/PAT_STOP to avoid recursive pattern interactions. */
		if (r->events[i].type != MLR_EVT_PAT_PLAY &&
		    r->events[i].type != MLR_EVT_PAT_STOP) {
			mlr_pattern_event(&r->events[i]);
		}
	}
	mlr_recall_active = slot;
}

void mlr_recall_undo_and_record(void)
{
	if (!recall_undo.has_data) return;
	for (uint16_t i = 0; i < recall_undo.count; i++) {
		event_exec(&recall_undo.events[i]);
		if (recall_undo.events[i].type != MLR_EVT_PAT_PLAY &&
		    recall_undo.events[i].type != MLR_EVT_PAT_STOP) {
			mlr_pattern_event(&recall_undo.events[i]);
		}
	}
	recall_undo.has_data = false;
	mlr_recall_active = -1;
}

void mlr_recall_clear(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	mlr_recall_t *r = &mlr_recalls[slot];
	r->count = 0;
	r->recording = false;
	r->has_data = false;
	if (mlr_recall_active == slot)
		mlr_recall_active = -1;
}

/* ------------------------------------------------------------------ */
/* Scene persistence — save/load patterns + recalls + track state     */
/* ------------------------------------------------------------------ */

/* Scene is serialized into MLR_SCENE_SECTORS * 4096 bytes in flash.
 * Layout:
 *   [0]    uint32_t magic (MLR_SCENE_MAGIC)
 *   [4]    uint32_t data_len (bytes following, excluding CRC)
 *   [8]    mlr_track_scene_t[7]  (28 bytes)
 *   [36]   pattern_count[4] as uint16_t (8 bytes)
 *   [44]   pattern_loop_len_ms[4] as uint32_t (16 bytes)
 *   [60]   pattern events (count * 8 bytes per pattern, concatenated)
 *   [...]  recall_count[4] as uint16_t (8 bytes)
 *   [...]  recall_has_data[4] as uint8_t (4 bytes)
 *   [...]  recall events (count * 8 bytes per recall, concatenated)
 *   [end]  uint32_t CRC32 (over everything from offset 0 to here)
 */

/* Simple CRC32 (no table — small code size, only runs on save/load) */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
	crc = ~crc;
	for (uint32_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}
	return ~crc;
}

/* Shared staging buffer — used as hdr_staging during header writes
 * and scene_blob during scene save.  Never concurrent. */
static union {
	uint8_t hdr[MLR_SECTOR_SIZE];
	uint8_t scene[MLR_SCENE_SECTORS * MLR_SECTOR_SIZE];
} __attribute__((aligned(4))) staging_buf;

#define hdr_staging   (staging_buf.hdr)
#define scene_blob    (staging_buf.scene)

static uint32_t scene_serialize(void)
{
	memset(scene_blob, 0xFF, MLR_SCENE_SECTORS * MLR_SECTOR_SIZE);
	uint32_t pos = 0;

	/* magic */
	uint32_t magic = MLR_SCENE_MAGIC;
	memcpy(&scene_blob[pos], &magic, 4); pos += 4;

	/* placeholder for data_len */
	uint32_t len_pos = pos;
	pos += 4;

	/* track states */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_scene_t ts;
		ts.speed_shift    = mlr_tracks[t].speed_shift;
		ts.reverse        = mlr_tracks[t].reverse ? 1 : 0;
		ts.loop_col_start = mlr_tracks[t].loop_active ? (int8_t)mlr_tracks[t].loop_col_start : -1;
		ts.loop_col_end   = mlr_tracks[t].loop_active ? (int8_t)mlr_tracks[t].loop_col_end   : -1;
		ts.volume_slot    = mlr_tracks[t].volume_slot;
		if (t == 0) {
			uint16_t raw = mlr_master_level_raw;
			if (raw > 4095) raw = 4095;
			ts._pad[0] = (uint8_t)(raw & 0xFF);
			ts._pad[1] = (uint8_t)(raw >> 8);
			ts._pad[2] = 0xA5;
		} else {
			ts._pad[0] = 0xFF; ts._pad[1] = 0xFF; ts._pad[2] = 0xFF;
		}
		memcpy(&scene_blob[pos], &ts, sizeof(ts)); pos += sizeof(ts);
	}

	/* pattern metadata */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		uint16_t cnt = mlr_patterns[p].count;
		memcpy(&scene_blob[pos], &cnt, 2); pos += 2;
	}
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		uint32_t ll = mlr_patterns[p].loop_len_ms;
		memcpy(&scene_blob[pos], &ll, 4); pos += 4;
	}

	/* pattern events (only used slots) */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		uint32_t sz = mlr_patterns[p].count * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(&scene_blob[pos], mlr_patterns[p].events, sz);
			pos += sz;
		}
	}

	/* recall metadata */
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		uint16_t cnt = mlr_recalls[r].count;
		memcpy(&scene_blob[pos], &cnt, 2); pos += 2;
	}
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		uint8_t hd = mlr_recalls[r].has_data ? 1 : 0;
		scene_blob[pos++] = hd;
	}

	/* recall events (only used slots) */
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		uint32_t sz = mlr_recalls[r].count * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(&scene_blob[pos], mlr_recalls[r].events, sz);
			pos += sz;
		}
	}

	/* write data_len */
	uint32_t data_len = pos - 8;
	memcpy(&scene_blob[len_pos], &data_len, 4);

	/* CRC32 over everything so far */
	uint32_t crc = crc32_update(0, scene_blob, pos);
	memcpy(&scene_blob[pos], &crc, 4); pos += 4;

	return pos;
}

void mlr_scene_load(void)
{
	const uint8_t *flash = (const uint8_t *)(XIP_BASE + MLR_SCENE_OFFSET);

	/* check magic */
	uint32_t magic;
	memcpy(&magic, flash, 4);
	if (magic != MLR_SCENE_MAGIC) return;

	uint32_t data_len;
	memcpy(&data_len, flash + 4, 4);
	uint32_t total = 8 + data_len + 4;  /* header + data + CRC */
	if (total > MLR_SCENE_SECTORS * MLR_SECTOR_SIZE) return;

	/* verify CRC */
	uint32_t crc_calc = crc32_update(0, flash, 8 + data_len);
	uint32_t crc_stored;
	memcpy(&crc_stored, flash + 8 + data_len, 4);
	if (crc_calc != crc_stored) return;

	/* deserialize */
	uint32_t pos = 8;

	/* track states — read but don't restore speed/volume/reverse (reset to defaults) */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_scene_t ts;
		memcpy(&ts, flash + pos, sizeof(ts)); pos += sizeof(ts);

		if (t == 0 && ts._pad[2] == 0xA5) {
			uint16_t raw = (uint16_t)ts._pad[0] | ((uint16_t)ts._pad[1] << 8);
			if (raw > 4095) raw = 4095;
			mlr_master_level_raw = raw;
		}

		/* only restore loop boundaries */
		if (ts.loop_col_start >= 0 && ts.loop_col_end >= 0 &&
		    mlr_tracks[t].has_content) {
			mlr_set_loop(t, ts.loop_col_start, ts.loop_col_end);
		}
	}

	/* pattern metadata */
	uint16_t pat_counts[MLR_NUM_PATTERNS];
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		memcpy(&pat_counts[p], flash + pos, 2); pos += 2;
		if (pat_counts[p] > MLR_PATTERN_MAX_EVENTS)
			pat_counts[p] = MLR_PATTERN_MAX_EVENTS;
	}
	uint32_t pat_loop_lens[MLR_NUM_PATTERNS];
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		memcpy(&pat_loop_lens[p], flash + pos, 4); pos += 4;
	}

	/* pattern events */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		mlr_patterns[p].count = pat_counts[p];
		mlr_patterns[p].loop_len_ms = pat_loop_lens[p];
		mlr_patterns[p].play_idx = 0;
		mlr_patterns[p].state = (pat_counts[p] > 0) ? MLR_PAT_STOPPED : MLR_PAT_IDLE;
		uint32_t sz = pat_counts[p] * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(mlr_patterns[p].events, flash + pos, sz);
			pos += sz;
		}
	}

	/* recall metadata */
	uint16_t rec_counts[MLR_NUM_RECALLS];
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		memcpy(&rec_counts[r], flash + pos, 2); pos += 2;
		if (rec_counts[r] > MLR_PATTERN_MAX_EVENTS)
			rec_counts[r] = MLR_PATTERN_MAX_EVENTS;
	}
	uint8_t rec_has_data[MLR_NUM_RECALLS];
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		rec_has_data[r] = flash[pos++];
	}

	/* recall events */
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		mlr_recalls[r].count = rec_counts[r];
		mlr_recalls[r].has_data = rec_has_data[r] != 0;
		mlr_recalls[r].recording = false;
		uint32_t sz = rec_counts[r] * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(mlr_recalls[r].events, flash + pos, sz);
			pos += sz;
		}
	}
}

void mlr_scene_save_start(void)
{
	if (scene_save_pending || mlr_scene_saving) return;

	/* serialize into RAM blob (core 0 context — fast) */
	scene_serialize();

	/* kick off the async write on core 1 */
	scene_save_sector = 0;
	scene_sector_erased = false;
	scene_page_idx = 0;
	mlr_scene_saving = true;
	__dmb();
	scene_save_pending = true;
}

/* ------------------------------------------------------------------ */
/* Core 1 — ring fill, page flush, erase-ahead, header write          */
/* ------------------------------------------------------------------ */

/** Seek fill state to an arbitrary sample position using keyframes. */
static void seek_fill_to(mlr_track_t *tr, int t, uint32_t target)
{
	uint32_t kf_idx = target / MLR_KEYFRAME_INTERVAL;
	if (kf_idx >= tr->num_keyframes && tr->num_keyframes > 0)
		kf_idx = tr->num_keyframes - 1;

	uint32_t kf_sample = kf_idx * MLR_KEYFRAME_INTERVAL;
	if (tr->num_keyframes > 0) {
		for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
			tr->fill_decode[ch].predictor  = tr->keyframes[kf_idx].ch[ch].predictor;
			tr->fill_decode[ch].step_index = tr->keyframes[kf_idx].ch[ch].step_index;
		}
	} else {
		for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
			tr->fill_decode[ch].predictor  = 0;
			tr->fill_decode[ch].step_index = 0;
		}
		kf_sample = 0;
	}
#ifdef MLR_STEREO
	tr->fill_byte_pos   = kf_sample;  /* 1 byte per stereo frame */
	tr->fill_high_nyb   = false;      /* not used in stereo */
#else
	tr->fill_byte_pos   = kf_sample / 2;
	tr->fill_high_nyb   = (kf_sample & 1) != 0;
#endif
	tr->fill_sample_pos = kf_sample;

	/* decode forward to target (discard samples) */
	const uint8_t *flash_data = track_audio_xip(t);
	while (tr->fill_sample_pos < target) {
		uint8_t byte   = flash_data[tr->fill_byte_pos];
#ifdef MLR_STEREO
		adpcm_decode(byte & 0x0F, &tr->fill_decode[0]);
		adpcm_decode(byte >> 4,   &tr->fill_decode[1]);
		tr->fill_byte_pos++;
#else
		uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
		adpcm_decode(nybble, &tr->fill_decode[0]);
		if (tr->fill_high_nyb) {
			tr->fill_byte_pos++;
			tr->fill_high_nyb = false;
		} else {
			tr->fill_high_nyb = true;
		}
#endif
		tr->fill_sample_pos++;
	}
}

/** Decode ADPCM from flash XIP and fill a track's PCM ring (forward). */
static void fill_pcm_ring_forward(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];

	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t free_samples = pcm_ring_free(&tr->pcm);

	/* fill up to half the ring per call to stay responsive */
	uint32_t to_fill = free_samples;
	if (to_fill > MLR_RING_SAMPLES / 2) to_fill = MLR_RING_SAMPLES / 2;

	/* determine wrap boundaries (loop-a-section or full track) */
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;

	for (uint32_t i = 0; i < to_fill; i++) {
		if (tr->fill_seek_pending) break;

		if (tr->fill_sample_pos >= wrap_end) {
			/* wrap: seek back to loop/track start */
			seek_fill_to(tr, t, wrap_start);
		}

		uint8_t byte   = flash_data[tr->fill_byte_pos];
#ifdef MLR_STEREO
		int16_t mid  = adpcm_decode(byte & 0x0F, &tr->fill_decode[0]);
		int16_t side = adpcm_decode(byte >> 4,   &tr->fill_decode[1]);
		/* M/S → L/R reconstruction */
		int16_t sL = (int16_t)(mid + side);
		int16_t sR = (int16_t)(mid - side);
		uint32_t idx = (tr->pcm.w % MLR_RING_SAMPLES) * 2;
		tr->pcm.buf[idx]     = sL;
		tr->pcm.buf[idx + 1] = sR;
		tr->fill_byte_pos++;
#else
		uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
		int16_t sample = adpcm_decode(nybble, &tr->fill_decode[0]);

		tr->pcm.buf[(tr->pcm.w % MLR_RING_SAMPLES) * MLR_NUM_CHANNELS] = sample;

		if (tr->fill_high_nyb) {
			tr->fill_byte_pos++;
			tr->fill_high_nyb = false;
		} else {
			tr->fill_high_nyb = true;
		}
#endif
		__dmb();
		tr->pcm.w++;

		tr->fill_sample_pos++;
	}
}

/**
 * Fill a track's PCM ring with reversed audio.
 * Decode forward in chunks from keyframes, then write reversed to ring.
 */
static int16_t rev_decode_tmp[MLR_KEYFRAME_INTERVAL * MLR_NUM_CHANNELS];  /* shared temp, core 1 only */

static void fill_pcm_ring_reverse(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];

	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t free_samples = pcm_ring_free(&tr->pcm);
	uint32_t to_fill = free_samples;
	if (to_fill > MLR_RING_SAMPLES / 2) to_fill = MLR_RING_SAMPLES / 2;

	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;

	while (to_fill > 0) {
		if (tr->fill_seek_pending) break;

		/* wrap: if fill_sample_pos is at or below wrap_start, jump to end */
		if (tr->fill_sample_pos <= wrap_start) {
			tr->fill_sample_pos = wrap_end;
		}

		/* how many samples backwards from fill_sample_pos in this batch */
		uint32_t avail = tr->fill_sample_pos - wrap_start;
		uint32_t chunk = to_fill < avail ? to_fill : avail;
		if (chunk > MLR_KEYFRAME_INTERVAL) chunk = MLR_KEYFRAME_INTERVAL;
		if (chunk == 0) break;

		/* decode forward from (fill_sample_pos - chunk) for chunk samples */
		uint32_t decode_start = tr->fill_sample_pos - chunk;
		seek_fill_to(tr, t, decode_start);

		for (uint32_t i = 0; i < chunk; i++) {
			uint8_t byte   = flash_data[tr->fill_byte_pos];
#ifdef MLR_STEREO
			int16_t mid  = adpcm_decode(byte & 0x0F, &tr->fill_decode[0]);
			int16_t side = adpcm_decode(byte >> 4,   &tr->fill_decode[1]);
			/* M/S → L/R reconstruction before storing in temp buffer */
			rev_decode_tmp[i * 2]     = (int16_t)(mid + side);
			rev_decode_tmp[i * 2 + 1] = (int16_t)(mid - side);
			tr->fill_byte_pos++;
#else
			uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
			rev_decode_tmp[i] = adpcm_decode(nybble, &tr->fill_decode[0]);
			if (tr->fill_high_nyb) {
				tr->fill_byte_pos++;
				tr->fill_high_nyb = false;
			} else {
				tr->fill_high_nyb = true;
			}
#endif
		}

		/* write into ring in reverse order */
		for (uint32_t i = 0; i < chunk; i++) {
#ifdef MLR_STEREO
			uint32_t src = (chunk - 1 - i) * 2;
			uint32_t dst = (tr->pcm.w % MLR_RING_SAMPLES) * 2;
			tr->pcm.buf[dst]     = rev_decode_tmp[src];
			tr->pcm.buf[dst + 1] = rev_decode_tmp[src + 1];
#else
			tr->pcm.buf[(tr->pcm.w % MLR_RING_SAMPLES) * MLR_NUM_CHANNELS] = rev_decode_tmp[chunk - 1 - i];
#endif
			__dmb();
			tr->pcm.w++;
		}

		tr->fill_sample_pos = decode_start;
		to_fill -= chunk;
	}
}

/** Dispatch to forward or reverse ring fill. */
static void fill_pcm_ring(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];
	if (!tr->has_content || !tr->playing) return;

	if (tr->reverse)
		fill_pcm_ring_reverse(t);
	else
		fill_pcm_ring_forward(t);
}

/** Handle a pending seek: restore decoder state from keyframe, refill. */
static void handle_seek(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];
	uint32_t target = tr->seek_target_sample;

	seek_fill_to(tr, t, target);
	tr->fill_seek_pending = false;

	/* now fill the ring from this position */
	fill_pcm_ring(t);
}

/** Drain one page from the recording page ring to flash. */
static void flush_rec_page(void)
{
	if (mlr_page_ring.r == mlr_page_ring.w) return;  /* nothing to flush */

	uint8_t slot = mlr_page_ring.r % MLR_PAGE_RING_COUNT;

	/* erase-ahead: if we've reached the next sector boundary, erase it */
	if (rec_flash_offset >= rec_next_erase) {
		flash_range_erase(rec_next_erase, MLR_SECTOR_SIZE);
		rec_next_erase += MLR_SECTOR_SIZE;
	}

	/* write the page */
	flash_range_program(rec_flash_offset, mlr_page_ring.pages[slot], MLR_PAGE_SIZE);
	rec_flash_offset += MLR_PAGE_SIZE;

	__dmb();
	mlr_page_ring.r++;
}

/** Write the track header after recording stops. */

static void write_track_header(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];

	/* build header in static buffer (too large for core 1 stack) */
	memset(hdr_staging, 0xFF, MLR_SECTOR_SIZE);

	mlr_track_header_t *hdr = (mlr_track_header_t *)hdr_staging;
	hdr->magic               = MLR_MAGIC;
	hdr->sample_count        = tr->length_samples;
	hdr->adpcm_bytes         = tr->length_bytes;
	hdr->num_keyframes       = tr->num_keyframes;
	hdr->record_speed_shift  = tr->record_speed_shift;
	memcpy(hdr->keyframes, tr->keyframes,
	       tr->num_keyframes * sizeof(mlr_keyframe_stereo_t));

	uint32_t hdr_off = track_hdr_flash_off(t);
	flash_range_erase(hdr_off, MLR_SECTOR_SIZE);
	flash_range_program(hdr_off, hdr_staging, MLR_SECTOR_SIZE);

	/* mark track as playable */
	tr->has_content     = true;
	tr->playing         = true;
	tr->playhead        = 0;
	tr->fill_byte_pos   = 0;
	tr->fill_high_nyb   = false;
	tr->fill_sample_pos = 0;
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		tr->fill_decode[ch].predictor  = 0;
		tr->fill_decode[ch].step_index = 0;
		if (tr->num_keyframes > 0) {
			tr->fill_decode[ch].predictor  = tr->keyframes[0].ch[ch].predictor;
			tr->fill_decode[ch].step_index = tr->keyframes[0].ch[ch].step_index;
		}
	}
	tr->pcm.w = 0;
	tr->pcm.r = 0;
	tr->speed_frac = speed_shift_to_frac(tr->speed_shift);
	tr->speed_accum = 0;
	reset_track_audio_state(tr);
	/* preserve reverse — only affects playback, recording always forward */
}

static int fill_rr = 0;  /* round-robin index for ring refill */

void mlr_io_task(void)
{
	/* 1. Handle pending seeks (always scan all — seeks are rare + cheap) */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		if (mlr_tracks[t].fill_seek_pending) {
			handle_seek(t);
		}
	}

	/* 2. Drain recording pages first so record writes are not starved by
	 *    playback refill work when many tracks are running. */
	if (rec_track_idx >= 0 || mlr_page_ring.r != mlr_page_ring.w) {
		flush_rec_page();

		/* If backlog is growing, drain a few extra pages this tick. */
		uint32_t used = page_ring_used_pages();
		if (used > (MLR_PAGE_RING_COUNT / 2)) {
			int extra_budget = 3; /* up to 4 pages total this call */
			while (extra_budget-- > 0 && mlr_page_ring.r != mlr_page_ring.w) {
				flush_rec_page();
				if (page_ring_used_pages() <= (MLR_PAGE_RING_COUNT / 2))
					break;
			}
		}
	}

	/* 3. Refill ONE playing track's ring (round-robin).
	 *    Spreading work across calls keeps USB/mext responsive,
	 *    especially with reverse playback's seek_fill_to overhead. */
	for (int i = 0; i < MLR_NUM_TRACKS; i++) {
		int t = (fill_rr + i) % MLR_NUM_TRACKS;
		if (mlr_tracks[t].has_content && mlr_tracks[t].playing) {
			fill_pcm_ring(t);
			fill_rr = (t + 1) % MLR_NUM_TRACKS;
			break;
		}
	}

	/* 4. Handle pending clear (quick — one sector erase) */
	if (clear_pending) {
		flash_range_erase(track_hdr_flash_off(clear_track), MLR_SECTOR_SIZE);
		clear_pending = false;
	}

	/* 5. Write header after recording stops and pages are drained */
	if (hdr_write_pending && mlr_page_ring.r == mlr_page_ring.w) {
		write_track_header(hdr_write_track);
		hdr_write_pending = false;
		mlr_flushing = false;
	}

	/* 6. No header write pending: flushing ends once page ring is drained */
	if (!hdr_write_pending && mlr_flushing && rec_track_idx < 0 &&
	    mlr_page_ring.r == mlr_page_ring.w) {
		mlr_flushing = false;
	}

	/* 7. Scene save — interleaved: erase one sector, then program one
	 *    256-byte page per io_task call.  Ring refills happen between
	 *    each page write, keeping audio fed even at high speeds. */
	if (scene_save_pending && !hdr_write_pending && !clear_pending &&
	    rec_track_idx < 0 && mlr_page_ring.r == mlr_page_ring.w) {
		uint32_t sector_off = MLR_SCENE_OFFSET + (uint32_t)scene_save_sector * MLR_SECTOR_SIZE;

		if (!scene_sector_erased) {
			/* phase 1: erase the sector (~45ms) */
			flash_range_erase(sector_off, MLR_SECTOR_SIZE);
			scene_sector_erased = true;
			scene_page_idx = 0;
		} else {
			/* phase 2: program one 256-byte page (~0.7ms) */
			uint32_t blob_off = (uint32_t)scene_save_sector * MLR_SECTOR_SIZE
			                  + (uint32_t)scene_page_idx * MLR_PAGE_SIZE;
			flash_range_program(sector_off + (uint32_t)scene_page_idx * MLR_PAGE_SIZE,
				&scene_blob[blob_off], MLR_PAGE_SIZE);
			scene_page_idx++;

			if (scene_page_idx >= SCENE_PAGES_PER_SECTOR) {
				/* sector complete — advance to next */
				scene_save_sector++;
				scene_sector_erased = false;
				if (scene_save_sector >= MLR_SCENE_SECTORS) {
					scene_save_pending = false;
					mlr_scene_saving = false;
				}
			}
		}
	}
}
