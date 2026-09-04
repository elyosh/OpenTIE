#ifndef __IMUSE_COMMANDS_H__
#define __IMUSE_COMMANDS_H__

#include <stdint.h>

#include <imuse/handle.h>

/*
 * libimuse — top-level commands.
 *
 * Lifecycle (`imuse_create`/`imuse_destroy`), per-tick advance
 * (`imuse_advance`), audio mix (`imuse_mix_f32`/`imuse_mix_s16`),
 * and the per-sound control surface (`imuse_start_sound`,
 * `imuse_set_param`, …).
 *
 * Trigger/defer payload type `ImuseCmd` is also declared here
 * because it carries an `ImuseOpcode` value, and the opcode enum
 * lives below.
 */

/* ===== Command opcodes =====
 *
 * Numbering matches the DOS engine's imuseFuncPtrs[] table. Opcodes
 * >= 30 are reserved for callback function pointers carried in the
 * trigger / defer paths — see triggers.h (IM_OPCODE_MAX). */
typedef enum ImuseOpcode {
	IMUSE_CMD_INIT = 0,
	IMUSE_CMD_TERMINATE = 1,
	IMUSE_CMD_PRINTF = 2,
	IMUSE_CMD_PAUSE = 3,
	IMUSE_CMD_RESUME = 4,
	IMUSE_CMD_SAVE = 5,
	IMUSE_CMD_RESTORE = 6,
	IMUSE_CMD_SET_GROUP_VOL = 7,
	IMUSE_CMD_START_SOUND = 8,
	IMUSE_CMD_STOP_SOUND = 9,
	IMUSE_CMD_STOP_ALL_SOUNDS = 10,
	IMUSE_CMD_GET_NEXT_SOUND = 11,
	IMUSE_CMD_SET_PARAM = 12,
	IMUSE_CMD_GET_PARAM = 13,
	IMUSE_CMD_FADE_PARAM = 14,
	IMUSE_CMD_SET_HOOK = 15,
	IMUSE_CMD_GET_HOOK = 16,
	IMUSE_CMD_SET_TRIGGER = 17,
	IMUSE_CMD_CHECK_TRIGGER = 18,
	IMUSE_CMD_CLEAR_TRIGGER = 19,
	IMUSE_CMD_DEFER_CMD = 20,
	IMUSE_CMD_JUMP_MIDI = 21,
	IMUSE_CMD_SCAN_MIDI = 22,
	IMUSE_CMD_SEND_MIDI_MSG = 23,
	IMUSE_CMD_SHARE_PARTS = 24,
	IMUSE_CMD_START_STREAM = 25,
	IMUSE_CMD_SWITCH_STREAM = 26,
	IMUSE_CMD_PROCESS_STREAM = 27,
	IMUSE_CMD_QUERY_STREAM = 28,
	IMUSE_CMD_PANIC_INTERNAL = 29
} ImuseOpcode;

/* ===== iMuse parameter IDs =====
 *
 * Encodes the per-sound / per-part parameter addressed by
 * imuse_set_param / imuse_get_param / imuse_fade_param. Values
 * 0x000..0xA00 are sound-level (including wave frequency at 0x777);
 * 0xB00..0xF00 are MIDI time-related;
 * 0x1100..0x1700 are per-MIDI-part params with the channel index
 * encoded in the low byte (e.g. 0x1503 = channel-3 part-volume).
 * 0x1800 is the wave-stream flag. */
typedef enum ImuseParam {
	IMUSE_PARAM_SOUND_TYPE = 0x000,
	IMUSE_PARAM_SOUND_PLAY_COUNT = 0x100,
	IMUSE_PARAM_SOUND_PEND_COUNT = 0x200,
	IMUSE_PARAM_SOUND_MARKER = 0x300,
	IMUSE_PARAM_SOUND_GROUP = 0x400,
	IMUSE_PARAM_SOUND_PRIORITY = 0x500,
	IMUSE_PARAM_SOUND_VOL = 0x600,
	IMUSE_PARAM_SOUND_PAN = 0x700,
	IMUSE_PARAM_SOUND_FREQUENCY = 0x777,
	IMUSE_PARAM_SOUND_DETUNE = 0x800,
	IMUSE_PARAM_SOUND_TRANSPOSE = 0x900,
	IMUSE_PARAM_SOUND_MAILBOX = 0xA00,
	IMUSE_PARAM_MIDI_CHUNK = 0xB00,
	IMUSE_PARAM_MIDI_MEASURE = 0xC00,
	IMUSE_PARAM_MIDI_BEAT = 0xD00,
	IMUSE_PARAM_MIDI_TICK = 0xE00,
	IMUSE_PARAM_MIDI_SPEED = 0xF00,
	IMUSE_PARAM_MIDI_PART_TRIM = 0x1100,
	IMUSE_PARAM_MIDI_PART_STATUS = 0x1200,
	IMUSE_PARAM_MIDI_PART_PRIORITY = 0x1300,
	IMUSE_PARAM_MIDI_PART_NOTE_REQ = 0x1400,
	IMUSE_PARAM_MIDI_PART_VOL = 0x1500,
	IMUSE_PARAM_MIDI_PART_PAN = 0x1600,
	IMUSE_PARAM_MIDI_PART_PGM = 0x1700,
	IMUSE_PARAM_WAVE_STREAM_FLAG = 0x1800
} ImuseParam;

typedef enum ImuseWaveStartFlags {
	IMUSE_WAVE_START_NONE = 0,
	IMUSE_WAVE_START_LOOP = 1u << 0,
	/* Port extension for loaders that expose only the first VOC audio block. */
	IMUSE_WAVE_START_LOOP_FIRST_BLOCK = 1u << 1,
} ImuseWaveStartFlags;

/* Mask isolating the param "kind" (high bits) from the per-channel
 * index carried in the low byte of the 0x1100..0x1700 range. */
#define IMUSE_PARAM_KIND_MASK 0xFF00

/* ===== Host-supplied callbacks =====
 *
 * soundId is typed as intptr_t so the host can pass either a small
 * numeric id or a pointer-width resource handle. The DOS build got
 * this for free (int == void * on 32-bit); on x64 it must be
 * explicit or the host's pointer-valued handles will truncate. */

/* Look up a sound buffer by soundId. Returns NULL if unloaded. */
typedef void* (*ImuseGetSoundPtrFunc)(intptr_t soundId);

/* ===== Log callback =====
 *
 * Library code never writes to stdio directly. All diagnostics flow
 * through a host-supplied log callback (or are silently dropped if
 * the host doesn't install one). The library pre-formats messages
 * into a stack buffer with vsnprintf — the host receives a finished
 * NUL-terminated string and a level tag.
 *
 * The callback is called from whatever thread emitted the message
 * (game thread for control events, audio thread for render-side
 * traces). A host that routes to a single sink should serialise
 * inside its own implementation. */
typedef enum ImuseLogLevel {
	IMUSE_LOG_TRACE = 0, /* high-volume per-event trace */
	IMUSE_LOG_INFO = 1,  /* lifecycle messages */
	IMUSE_LOG_WARN = 2,  /* recoverable misconfiguration */
	IMUSE_LOG_ERROR = 3, /* fatal — caller path will return error */
} ImuseLogLevel;

typedef void (*ImuseLogFunc)(void* user, ImuseLogLevel level, const char* msg);

/* Streamed-sound I/O callbacks (wired through to the host's CD /
 * disk I/O).
 *
 * seekFunc    — POSIX-like lseek: returns the new absolute offset.
 *               Called with whence=SEEK_END (2) + offset=0 to query
 *               total length.
 * readFunc    — blocking read: copies `size` bytes from soundId at
 *               the host cursor into `buf`. Returns bytes actually
 *               read.
 * getBufInfo  — returns the ImuseStreamBuffer descriptor for a
 *               given host-supplied stream-buffer id (1, 2, ...). */
struct ImuseStreamBuffer; /* defined in filelist.h */

typedef int (*ImuseSeekFunc)(intptr_t soundId, int offset, int whence);
typedef int (*ImuseReadFunc)(intptr_t soundId, void* buf, int size);
typedef struct ImuseStreamBuffer* (*ImuseGetBufInfoFunc)(int bufId);

/* INSANE wave-fill callback. The host is asked to fill `buf` with
 * `size` bytes of wave data on demand. Set via
 * imuse_set_wave_fill_cb; NULL = no INSANE streaming. */
typedef void (*ImuseWaveFillFunc)(void* user, void* buf, int size);

struct ImuseMidiBackend; /* opaque; see <imuse/midi_backend.h> */

/* ===== Init-time host configuration =====
 *
 * The init surface is split in two:
 *   - ImuseHost   — host-supplied callbacks (where data comes from,
 *                   where logs go). Pure pointers, no settings.
 *   - ImuseConfig — engine settings (sample rate, tick period, mix
 *                   pool size). Plain values, no callbacks.
 *
 * Both structs are read once at imuse_create time and copied by
 * value into the session. The host does not need to keep them
 * alive past the call — they may live on the caller's stack. The
 * MIDI backend handle is passed as a third argument; libimuse
 * takes ownership unconditionally (releases it on init failure
 * too). */
typedef struct ImuseHost {
	ImuseGetSoundPtrFunc getSoundPtrFunc; /* required: resolve soundId → buffer */
	ImuseSeekFunc seekFunc;               /* optional: streaming I/O */
	ImuseReadFunc readFunc;               /* optional: streaming I/O */
	ImuseGetBufInfoFunc getBufInfoFunc;   /* optional: streaming I/O */
	ImuseLogFunc logFunc;                 /* optional: NULL → silent */
	void* logUser;                        /* opaque cookie for logFunc */
} ImuseHost;

typedef enum ImuseWaveOutputFilter {
	IMUSE_WAVE_OUTPUT_FILTER_NONE = 0,
	IMUSE_WAVE_OUTPUT_FILTER_SB16,
} ImuseWaveOutputFilter;

typedef struct ImuseConfig {
	int32_t outputSampleRate;               /* rate consumed by imuse_mix_*; also
											   drives the MIDI backend synthesis
											   rate. 0 → 44100. Should be an
											   integer multiple of the wave
											   engine's rate (22050 with
											   waveSpeed=1) so the renderer can
											   upsample without a fractional-
											   rate resampler. */
	int32_t waveSpeed;                      /* 0 = ~11 kHz, 1 = ~22 kHz */
	int32_t waveMixCount;                   /* 1..16; default 4 if invalid */
	ImuseWaveOutputFilter waveOutputFilter; /* applies only to digital wave output */
} ImuseConfig;

/* ===== Trigger / defer payload =====
 *
 * `imuse_set_trigger` and `imuse_defer_command` consume an
 * `ImuseCmd`. The opcode is either an `ImuseOpcode` value (numeric)
 * or a function pointer (cast to `intptr_t`); the engine's
 * trigger-replay path treats values >= 30 as a callback fn-ptr and
 * lower values as opcode entries in its dispatch table. The
 * `args[10]` are passed verbatim to the replayed handler; only the
 * leading slots the handler consumes are read. */
typedef struct ImuseCmd {
	intptr_t opcode;
	intptr_t args[10];
} ImuseCmd;

/* ===== Lifecycle ===== */

/* Allocate a fresh iMUSE session.
 *
 *   host    — required. Read once and copied by value. May live on
 *             the caller's stack. NULL → returns NULL.
 *   cfg     — required. Read once and copied by value. NULL → NULL.
 *   backend — MIDI backend handle from an imuse_*_backend_create
 *             factory. NULL → MIDI silent. Ownership transfers to
 *             libimuse unconditionally — on init failure the
 *             backend is released. */
imuse_t* imuse_create(const ImuseHost* host, const ImuseConfig* cfg, struct ImuseMidiBackend* backend);

/* Tear down a session created by imuse_create. Frees the handle.
 * Returns 0 on success. */
int imuse_destroy(imuse_t* im);

/* ===== Per-tick advance ===== */

/* Drive the engine. Call once per host frame with the elapsed
 * microseconds since the previous call. Cadence-independent:
 * the engine accumulates the delta and fires its internal
 * sequencer + wave + fade + trigger + duck tiers off the
 * accumulated time, so MIDI tempo and audio pacing stay correct
 * at any host call rate.
 *
 * A long stall (debugger, alt-tab, slow scene load) is bounded:
 * a single advance call advances each tier by at most a small
 * fixed amount — the elapsed delta is capped before accumulation.
 * Time beyond the cap is dropped, never deferred to a later
 * call. */
void imuse_advance(imuse_t* im, int32_t usec_elapsed);

/* Set the fraction of music-group volume retained by dipped music while a
 * voice is active. The factor is fixed-point /128: 0 mutes, 128 disables the
 * reduction, and 47 matches the original. */
int imuse_set_music_ducking_factor(imuse_t* im, int factor);

/* ===== Audio mix =====
 *
 * Render `frames` stereo frames at the configured outputSampleRate
 * into `buf` (interleaved L,R,L,R,...). The library overwrites
 * `buf` with the MIDI render then additively mixes the wave stream
 * on top — caller need not zero `buf` first.
 *
 * Two typed entry points, one per public sample format:
 *   - imuse_mix_f32 emits float in [-1, +1].
 *   - imuse_mix_s16 emits saturating int16 in [-32768, +32767].
 *
 * Both are safe to call from a thread distinct from the one driving
 * imuse_advance. Synthesized MIDI backends and the wave path transfer events
 * through SPSC queues. */
void imuse_mix_f32(imuse_t* im, float* buf, int frames);
void imuse_mix_s16(imuse_t* im, int16_t* buf, int frames);

/* ===== Pause / resume =====
 *
 * Reentrant: paired calls compose. The current depth counter is
 * the return value (negative if more resumes than pauses). */
int imuse_pause(imuse_t* im);
int imuse_resume(imuse_t* im);

/* ===== Sound control =====
 *
 * StartSound resolves `soundId` via `host->getSoundPtrFunc` and
 * dispatches to MIDI or wave by inspecting the buffer's first 4
 * bytes (`MIDI` / `Crea`). Returns 0 on success, negative on error
 * (NULL buffer, no free slot, etc.). `priority` is 0..127. */
int imuse_start_sound(imuse_t* im, intptr_t soundId, int priority);

/* Start an in-memory Creative VOC with wave-specific behavior. Unknown
 * flags and non-VOC assets are rejected. */
int imuse_start_wave(imuse_t* im, intptr_t soundId, int priority, uint32_t flags);
int imuse_stop_sound(imuse_t* im, intptr_t soundId);
int imuse_stop_all_sounds(imuse_t* im);

/* Walk live sounds in ascending soundId order. Pass 0 to start,
 * iterate while non-zero. */
intptr_t imuse_next_sound(imuse_t* im, intptr_t soundId);

int imuse_set_param(imuse_t* im, intptr_t soundId, int param, int value);
int imuse_get_param(imuse_t* im, intptr_t soundId, int param);

/* Ramp a SetParam-style parameter to `target` over `time` 60 Hz
 * ticks. `param` must be one of the fade-eligible ImuseParam
 * values (volume / pan / detune / transpose / per-channel volume /
 * pan / trim). */
int imuse_fade_param(imuse_t* im, intptr_t soundId, int param, int target, int time);

void imuse_set_hook(imuse_t* im, intptr_t soundId, uint32_t hookId);
int imuse_get_hook(imuse_t* im, intptr_t soundId);

/* ===== Wave-fill callback (INSANE cutscene streaming) =====
 *
 * Single setter; pass NULL to disarm. The callback is invoked from
 * the audio thread when the wave path needs to refill a streaming
 * cutscene buffer. */
void imuse_set_wave_fill_cb(imuse_t* im, ImuseWaveFillFunc cb);

#endif /* __IMUSE_COMMANDS_H__ */
