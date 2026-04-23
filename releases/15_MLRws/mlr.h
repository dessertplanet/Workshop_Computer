/**
 * mlr.h — MLR track engine v2: ring-buffer streaming playback + JIT flash.
 *
 * Playback always reads from per-track RAM ring buffers (never flash).
 * Core 1 refills rings from flash XIP and handles recording writes.
 * Recording streams ADPCM pages to flash just-in-time with sector
 * erase-ahead, so there is no upfront delay and no flush on stop.
 *
 * Core 0: encode samples, decode from ring buffers (RAM only).
 * Core 1: refill rings from flash, erase/write during recording.
 */

#ifndef MLR_H
#define MLR_H

#include <stdint.h>
#include <stdbool.h>
#include "adpcm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define MLR_NUM_TRACKS         6
#define MLR_GRID_COLS          14

/* Number of audio channels per track */
#ifdef MLR_STEREO
#define MLR_NUM_CHANNELS       2
#else
#define MLR_NUM_CHANNELS       1
#endif

/* Flash layout — 2 MB total.
 * Firmware reserve and track size are set from CMake so the build can
 * validate the flash partition and fail if the program grows too large. */
#ifndef MLR_FIRMWARE_RESERVE
#define MLR_FIRMWARE_RESERVE   (160 * 1024)
#endif
#ifndef MLR_TRACK_FLASH_SIZE
#define MLR_TRACK_FLASH_SIZE   (312 * 1024)
#endif
#define MLR_HEADER_SIZE        4096               /* first sector = header+kf */
#define MLR_AUDIO_SIZE         (MLR_TRACK_FLASH_SIZE - MLR_HEADER_SIZE)
#define MLR_TRACK_OFFSET(t)    (MLR_FIRMWARE_RESERVE + (t) * MLR_TRACK_FLASH_SIZE)

/* ADPCM: 2 nybbles per byte.  In stereo, each byte = 1 L nybble + 1 R nybble
 * so one byte = one stereo frame.  In mono, one byte = 2 mono samples. */
#ifdef MLR_STEREO
#define MLR_MAX_SAMPLES        (MLR_AUDIO_SIZE)       /* one frame per byte */
#else
#define MLR_MAX_SAMPLES        (MLR_AUDIO_SIZE * 2)   /* two samples per byte */
#endif

/* Keyframes every N sample-frames for instant seeking */
#define MLR_KEYFRAME_INTERVAL  1024
#define MLR_MAX_KEYFRAMES      ((MLR_MAX_SAMPLES / MLR_KEYFRAME_INTERVAL) + 1)

/* Per-track playback ring buffer (decoded PCM, consumed by core 0) */
/* Per-track playback ring buffer (decoded PCM, consumed by core 0) */
#ifdef MLR_STEREO
#define MLR_RING_SAMPLES       3584  /* frames (~75ms at 48kHz stereo) */
#else
#define MLR_RING_SAMPLES       7168  /* frames (~149ms at 48kHz mono) */
#endif
#define MLR_DECLICK_SHIFT      5
#define MLR_DECLICK_SAMPLES    (1u << MLR_DECLICK_SHIFT)  /* 32-sample crossfade */

/* Recording page ring (ADPCM pages, core 0 fills, core 1 writes) */
#define MLR_PAGE_SIZE          256
#define MLR_PAGE_RING_COUNT    32

/* Flash sector size */
#ifndef MLR_SECTOR_SIZE
#define MLR_SECTOR_SIZE        4096
#endif

/* Track header magic — different for mono vs stereo to prevent cross-reads.
 * v4/v5: added record_speed_shift field (shifts keyframe offset by 4). */
#ifdef MLR_STEREO
#define MLR_MAGIC              0x4D4C5235  /* 'MLR5' — stereo M/S ADPCM v2 */
#else
#define MLR_MAGIC              0x4D4C5234  /* 'MLR4' — mono ADPCM v2 */
#endif

/* ------------------------------------------------------------------ */
/* Pattern / recall / scene constants                                 */
/* ------------------------------------------------------------------ */

#define MLR_NUM_PATTERNS       4
#define MLR_NUM_RECALLS        4
#define MLR_PATTERN_MAX_EVENTS 64

/* Event types stored in pattern/recall event structs */
#define MLR_EVT_CUT            1
#define MLR_EVT_STOP           2
#define MLR_EVT_START          3
#define MLR_EVT_SPEED          4
#define MLR_EVT_REVERSE        5
#define MLR_EVT_LOOP           6
#define MLR_EVT_LOOP_CLR       7
#define MLR_EVT_VOLUME         8
#define MLR_EVT_PAT_PLAY      9   /* param: pattern index in track field */
#define MLR_EVT_PAT_STOP     10   /* param: pattern index in track field */
#define MLR_EVT_RECALL       11   /* param: recall slot in track field */
#define MLR_EVT_RECALL_UNDO  12   /* undo last recall */
#define MLR_EVT_MASTER       13   /* master volume: raw=((track<<8)|param_a), 0..4095 */

/* Volume slots: 6 levels (cols 1–6 on REC page) */
#define MLR_NUM_VOL_SLOTS      6

/* Pattern states */
#define MLR_PAT_IDLE           0
#define MLR_PAT_RECORDING      1
#define MLR_PAT_PLAYING        2
#define MLR_PAT_STOPPED        3   /* has data, not playing */
#define MLR_PAT_ARMED          4   /* waiting for first event to start recording */

/* Scene flash layout — stored after the track area */
#define MLR_SCENE_OFFSET       (MLR_FIRMWARE_RESERVE + MLR_NUM_TRACKS * MLR_TRACK_FLASH_SIZE)
#define MLR_SCENE_MAGIC        0x4D4C5332  /* 'MLS2' */
#ifndef MLR_SCENE_SECTORS
#define MLR_SCENE_SECTORS      3
#endif



/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
	int16_t predictor;
	int8_t  step_index;
	uint8_t _pad;
} mlr_keyframe_t;

/* Per-channel keyframe for stereo */
typedef struct {
	mlr_keyframe_t ch[MLR_NUM_CHANNELS];
} mlr_keyframe_stereo_t;

/* Stored in flash (first sector of each track) */
typedef struct {
	uint32_t              magic;
	uint32_t              sample_count;
	uint32_t              adpcm_bytes;
	uint32_t              num_keyframes;
	int8_t                record_speed_shift;  /* speed at time of recording */
	uint8_t               _hdr_pad[3];
	mlr_keyframe_stereo_t keyframes[MLR_MAX_KEYFRAMES];
} mlr_track_header_t;

#if defined(__cplusplus)
static_assert(sizeof(mlr_track_header_t) <= MLR_HEADER_SIZE,
	"mlr_track_header_t exceeds MLR_HEADER_SIZE; reduce track size or header contents");
#else
_Static_assert(sizeof(mlr_track_header_t) <= MLR_HEADER_SIZE,
	"mlr_track_header_t exceeds MLR_HEADER_SIZE; reduce track size or header contents");
#endif

/* ---- Pattern / recall event (8 bytes, tightly packed) ---- */
typedef struct {
	uint32_t       timestamp_ms;   /* ms since pattern record started   */
	uint8_t        type;           /* MLR_EVT_*                         */
	uint8_t        track;          /* 0–6                               */
	int8_t         param_a;        /* column (cut), speed_shift, loop_start, reverse */
	int8_t         param_b;        /* loop_end (only for MLR_EVT_LOOP)  */
} mlr_event_t;

/* ---- Timed pattern recorder / player ---- */
typedef struct {
	mlr_event_t    events[MLR_PATTERN_MAX_EVENTS];
	uint16_t       count;          /* number of recorded events          */
	uint16_t       play_idx;       /* next event to fire during playback */
	uint32_t       loop_len_ms;    /* total loop duration in ms          */
	uint32_t       rec_start_ms;   /* absolute time when recording began */
	uint32_t       play_start_ms;  /* absolute time when playback began  */
	uint8_t        state;          /* MLR_PAT_*                          */
	uint8_t        event_flash;    /* decremented by UI; >0 = darken LED */
} mlr_pattern_t;

/* ---- Recall (snapshot — instant replay, no timestamps) ---- */
typedef struct {
	mlr_event_t    events[MLR_PATTERN_MAX_EVENTS];
	uint16_t       count;
	bool           recording;
	bool           has_data;
} mlr_recall_t;

/* ---- Per-track saved state (for scene persistence) ---- */
typedef struct {
	int8_t         speed_shift;
	uint8_t        reverse;        /* bool stored as byte for alignment */
	int8_t         loop_col_start; /* -1 = no loop */
	int8_t         loop_col_end;
	uint8_t        volume_slot;    /* 0 = unity (col 1), 5 = quietest (col 6) */
	uint8_t        _pad[3];        /* align to 8 bytes */
} mlr_track_scene_t;

/* Playback ring — decoded PCM, core 1 fills, core 0 reads */
typedef struct {
	int16_t  buf[MLR_RING_SAMPLES * MLR_NUM_CHANNELS];
	volatile uint32_t w;   /* write index in frames (core 1) */
	volatile uint32_t r;   /* read index in frames (core 0) */
} mlr_pcm_ring_t;

/* Recording page ring — ADPCM pages, core 0 fills, core 1 flushes */
typedef struct {
	uint8_t  pages[MLR_PAGE_RING_COUNT][MLR_PAGE_SIZE];
	volatile uint8_t w;    /* write slot (core 0) */
	volatile uint8_t r;    /* read slot (core 1) */
	uint32_t fill;         /* bytes in current page */
} mlr_page_ring_t;

/* Per-track state */
typedef struct {
	/* metadata (from flash header) */
	uint32_t       length_samples;
	uint32_t       length_bytes;
	bool           has_content;

	/* playback */
	bool           playing;
	uint32_t       playhead;           /* current sample in track */

	/* PCM ring (core 0 reads, core 1 writes) */
	mlr_pcm_ring_t pcm;

	/* core 1 decode state for ring fill */
	uint32_t       fill_byte_pos;      /* ADPCM byte offset in flash */
	bool           fill_high_nyb;
	adpcm_state_t  fill_decode[MLR_NUM_CHANNELS];
	uint32_t       fill_sample_pos;    /* frame position of fill head */
	volatile bool  fill_seek_pending;  /* set by cut, core 1 refills */

	/* seek target (set by core 0, consumed by core 1) */
	volatile uint32_t seek_target_sample;

	/* loop-a-section: sub-loop boundaries (set by core 0) */
	bool           loop_active;
	uint32_t       loop_start_sample;   /* first sample of sub-loop */
	uint32_t       loop_end_sample;     /* one past last sample     */
	int            loop_col_start;      /* grid column (inclusive)  */
	int            loop_col_end;        /* grid column (inclusive)  */

	/* speed & reverse (set by core 0) */
	int8_t         speed_shift;         /* -3..+3: musical speed slot    */
	int8_t         record_speed_shift;  /* speed when track was recorded */
	uint16_t       speed_frac;          /* fixed 8.8: effective playback */
	uint16_t       speed_accum;         /* fractional accumulator        */
	int16_t        last_pcm[MLR_NUM_CHANNELS];  /* held sample for sub-1x speeds */
	int16_t        interp_prev[MLR_NUM_CHANNELS]; /* previous sample for interp */
	int16_t        last_out[MLR_NUM_CHANNELS];   /* last post-volume output sample */
	int16_t        out_hist[MLR_NUM_CHANNELS][MLR_DECLICK_SAMPLES]; /* recent output for overlap xfade */
	uint8_t        out_hist_pos;        /* next history slot to write */
	uint8_t        declick_hist_pos;    /* frozen history read start for active xfade */
	uint8_t        declick_count;       /* remaining crossfade samples */
	bool           stop_pending;        /* ramp to zero before hard stop */
	bool           reverse;             /* true = play backwards         */
	bool           muted;               /* true = skip mix, keep position */

	/* per-track volume (set by core 0) */
	uint8_t        volume_slot;         /* 0..5: mixer column index       */
	uint16_t       volume_frac;         /* fixed 8.8: current (slewed)    */
	uint16_t       volume_target;       /* fixed 8.8: target from slot    */

	/* keyframes (copied to RAM at boot) */
	uint32_t          num_keyframes;
	mlr_keyframe_stereo_t keyframes[MLR_MAX_KEYFRAMES];
} mlr_track_t;

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

extern mlr_track_t     mlr_tracks[MLR_NUM_TRACKS];
extern volatile int    mlr_rec_track;
extern volatile bool   mlr_flushing;  /* true while header being written */
extern mlr_page_ring_t mlr_page_ring;
extern volatile uint16_t mlr_master_level_raw; /* 0..4095 master level (grid/pattern/scene controlled) */
extern volatile bool     mlr_master_override;   /* true = suppress pattern master events until loop wrap */

extern mlr_pattern_t   mlr_patterns[MLR_NUM_PATTERNS];
extern mlr_recall_t    mlr_recalls[MLR_NUM_RECALLS];
extern volatile bool   mlr_scene_saving;  /* true during flash scene write */
extern volatile int    mlr_recall_active;  /* currently selected recall (-1=none) */

/* ------------------------------------------------------------------ */
/* Core 0 API                                                         */
/* ------------------------------------------------------------------ */

void     mlr_init(void);
void     mlr_rescan_track(int track);  /* re-read header from flash (call only when track is stopped) */
void     mlr_start_record(int track);
void     mlr_record_sample(int16_t sample);  /* mono: single sample */
#ifdef MLR_STEREO
void     mlr_record_sample_stereo(int16_t left, int16_t right);
int16_t  mlr_play_mix_stereo(uint8_t volume, int16_t *out_right);
#endif
void     mlr_stop_record(void);
int16_t  mlr_play_mix(uint8_t volume);
void     mlr_cut(int track, int column);
void     mlr_cut_sample(int track, uint32_t sample_pos);
void     mlr_stop_track(int track);
void     mlr_clear_track(int track);
void     mlr_set_loop(int track, int col_start, int col_end);
void     mlr_clear_loop(int track);
void     mlr_set_speed(int track, int speed_shift);
void     mlr_set_speed_frac(int track, uint16_t speed_frac); /* fixed 8.8, clamped to 64..1024 */
void     mlr_set_speed_frac_nondeclick(int track, uint16_t speed_frac); /* fixed 8.8, clamped to 64..1024 */
void     mlr_set_reverse(int track, bool reverse);
void     mlr_set_volume(int track, int slot);
int      mlr_get_column(int track);
void     mlr_get_loop_cols(int track, int *col_start, int *col_end);
uint32_t mlr_get_rec_progress(void);
int      mlr_get_flush_track(void);

/* ---- Pattern engine (core 0) ---- */
void     mlr_pattern_event(const mlr_event_t *e);  /* record into active patterns + recalls */
void     mlr_pattern_arm(int pat);                  /* arm: wait for first event to start recording */
void     mlr_pattern_rec_start(int pat);
void     mlr_pattern_rec_stop(int pat);
void     mlr_pattern_play_start(int pat);
void     mlr_pattern_play_stop(int pat);
void     mlr_pattern_clear(int pat);
void     mlr_pattern_tick(uint32_t now_ms);         /* call every LED update (~50ms) */

/* ---- Recall engine (core 0) ---- */
void     mlr_recall_snapshot(int slot);  /* instant capture of all track state */
void     mlr_recall_exec(int slot);
void     mlr_recall_exec_and_record(int slot);  /* exec + feed events into pattern recorder */
void     mlr_recall_undo(void);          /* restore state from before last recall */
void     mlr_recall_undo_and_record(void);      /* undo + feed events into pattern recorder */
void     mlr_recall_clear(int slot);

/* ---- Scene persistence ---- */
void     mlr_scene_load(void);      /* called from mlr_init() */
void     mlr_scene_save_start(void);/* triggers async save on core 1 */

/* ------------------------------------------------------------------ */
/* Core 1 API                                                         */
/* ------------------------------------------------------------------ */

/** Call frequently from core 1 loop. Refills playback rings,
 *  flushes recording pages, handles erase-ahead. */
void mlr_io_task(void);

#ifdef __cplusplus
}
#endif

#endif /* MLR_H */
