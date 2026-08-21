#include <imuse/midi_fluidsynth.h>

#include "internal/debug.h"
#include "internal/gmidi_driver.h"
#include "internal/midi_backend.h"
#include "internal/state.h"

#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h> /* snprintf for the in-memory SoundFont mem:%llx encoding */
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FLUIDSYNTH
#include <fluidsynth.h>
#endif

/* FluidSynth General MIDI backend. The shared GMIDI driver owns MIDI policy and its 12-voice
 * allocator. This adapter owns only SoundFont loading, FluidSynth's
 * device-specific CC7 compensation, MIDI-message reception, and PCM.
 */

/* Set to 1 to dump every event entry-point call to stderr. */
#ifndef GMIDI_TRACE
#define GMIDI_TRACE 0
#endif

/* ===== Backend state =====
 *
 * The vtable lives at offset 0 so a `(FluidSynthBackend *)backend`
 * downcast is well-defined; the rest is implementation detail
 * invisible to the engine.
 *
 * The SoundFont source is one of two modes:
 *   - file mode: soundfontPath points at an owned strdup; sfBufKey
 *     is unused.
 *   - buffer mode: sfBufKey holds the borrowed buffer pointer + size
 *     so the SFLoader open() callback can resolve the load via the
 *     pointer-as-filename trick (see sfmem_open below).
 * SFMEM_LOADKEY_MAGIC and the address-encoded "filename" let us
 * cleanly distinguish a memory-mode sfload from any other path that
 * might land in the open callback. */
#define SFMEM_LOADKEY_MAGIC 0x53464D45u /* 'SFME' */

typedef struct sfmem_loadkey {
	unsigned int magic;
	const void* data;
	size_t size;
} sfmem_loadkey;

typedef struct FluidSynthBackend {
	ImuseMidiBackend vtable; /* MUST be first member */

	struct FluidMidiEvent {
		uint16_t value;
		uint8_t type;
		uint8_t channel;
		uint8_t data1;
		uint8_t data2;
	} events[4096];
	_Alignas(64) atomic_uint writeIndex;
	_Alignas(64) atomic_uint readIndex;
	atomic_bool resetRequested;
	ImGmidiDriver driver;

	/* SoundFont source — exactly one of these is populated. */
	char* soundfontPath;    /* file mode: owned strdup */
	sfmem_loadkey sfBufKey; /* buffer mode: borrowed ptr + size */

	int polyphony;
	int opened; /* 0 before open(), 1 after */

	/* Host log hooks captured at open() so backend code can report
	 * errors and traces without touching stdio. NULL = silent. */
	ImuseLogFunc logFunc;
	void* logUser;

	void* settings; /* fluid_settings_t * */
	void* synth;    /* fluid_synth_t *    */
	int sfontId;

	/* CC 7 (channel volume) compensation table. SF2 / GM has CC 7
	 * attenuating ~quadratically; the DOS engine was tuned against
	 * a flatter response. We feed FluidSynth `sqrt(i/127)*127` so
	 * its squaring restores a linear relationship between the
	 * engine's computed channel volume and perceived loudness. */
	unsigned char cc7_linearize[128];
} FluidSynthBackend;

typedef enum FluidMidiEventType {
	FLUID_EVENT_PROGRAM,
	FLUID_EVENT_NOTE_ON,
	FLUID_EVENT_NOTE_OFF,
	FLUID_EVENT_CONTROL,
	FLUID_EVENT_PITCH_BEND,
} FluidMidiEventType;

enum { FLUID_EVENT_QUEUE_MASK = 4095 };

_Static_assert((4096 & 4095) == 0, "FluidSynth event queue capacity must be a power of two");

/* Log via the backend's stored host callback. Safe to call when
 * logFunc is NULL (silent) and inside any vtable hook. */
static void be_log(FluidSynthBackend* be, ImuseLogLevel level, const char* fmt, ...) {
	if (!be || !be->logFunc)
		return;
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	be->logFunc(be->logUser, level, buf);
}

#if GMIDI_TRACE
static void trace_event(FluidSynthBackend* be, int op, int isNoteOn) {
	static int s_totalEvents = 0;
	static int s_noteOns = 0;
	++s_totalEvents;
	if (isNoteOn)
		++s_noteOns;
	if (s_totalEvents <= 20 || (s_totalEvents % 200) == 0)
		be_log(be, IMUSE_LOG_TRACE, "midi_fluidsynth op=%d total=%d noteons=%d", op, s_totalEvents,
			   s_noteOns);
}
#define TRACE_EVENT(be, op, on) trace_event((be), (op), (on))
#else
#define TRACE_EVENT(be, op, on)                                                                              \
	do {                                                                                                     \
		(void)(be);                                                                                          \
		(void)(op);                                                                                          \
		(void)(on);                                                                                          \
	} while (0)
#endif

/* ===== FluidSynth wrappers (no-op when HAVE_FLUIDSYNTH unset) ===== */

#ifdef HAVE_FLUIDSYNTH

/* ===== In-memory SoundFont SFLoader =====
 *
 * FluidSynth's SFLoader callback API exposes only a `filename`
 * string to the open() hook — there is no user-data slot. We work
 * around it by encoding the address of the per-backend `sfBufKey`
 * into the filename (`mem:<hex-pointer>`) and parsing it back in
 * sfmem_open. The magic field on sfBufKey defends against the open
 * callback being invoked with a stray non-mem path.
 *
 * The reader struct returned by open() carries a cursor into the
 * borrowed buffer; FluidSynth keeps it alive until close() and
 * walks it through read/seek/tell during synth construction. */

typedef struct sfmem_reader {
	const unsigned char* base;
	size_t size;
	size_t pos;
} sfmem_reader;

static void* sfmem_open(const char* filename) {
	if (!filename || strncmp(filename, "mem:", 4) != 0)
		return NULL;
	char* end = NULL;
	unsigned long long addr = strtoull(filename + 4, &end, 16);
	if (!end || *end != '\0' || addr == 0)
		return NULL;
	sfmem_loadkey* key = (sfmem_loadkey*)(uintptr_t)addr;
	if (!key || key->magic != SFMEM_LOADKEY_MAGIC)
		return NULL;

	sfmem_reader* r = calloc(1, sizeof *r);
	if (!r)
		return NULL;
	r->base = (const unsigned char*)key->data;
	r->size = key->size;
	r->pos = 0;
	return r;
}

static int sfmem_read(void* buf, fluid_long_long_t count, void* handle) {
	sfmem_reader* r = (sfmem_reader*)handle;
	if (!r || count < 0)
		return FLUID_FAILED;
	size_t remain = r->size - r->pos;
	if ((size_t)count > remain)
		return FLUID_FAILED;
	memcpy(buf, r->base + r->pos, (size_t)count);
	r->pos += (size_t)count;
	return FLUID_OK;
}

static int sfmem_seek(void* handle, fluid_long_long_t offset, int origin) {
	sfmem_reader* r = (sfmem_reader*)handle;
	if (!r)
		return FLUID_FAILED;

	long long target;
	switch (origin) {
		case SEEK_SET:
			target = offset;
			break;
		case SEEK_CUR:
			target = (long long)r->pos + offset;
			break;
		case SEEK_END:
			target = (long long)r->size + offset;
			break;
		default:
			return FLUID_FAILED;
	}
	if (target < 0 || (unsigned long long)target > r->size)
		return FLUID_FAILED;
	r->pos = (size_t)target;
	return FLUID_OK;
}

static fluid_long_long_t sfmem_tell(void* handle) {
	sfmem_reader* r = (sfmem_reader*)handle;
	return r ? (fluid_long_long_t)r->pos : FLUID_FAILED;
}

static int sfmem_close(void* handle) {
	free(handle);
	return FLUID_OK;
}

/* Build + register the in-memory loader and ask FluidSynth to load
 * via it. The synth takes ownership of the loader on success;
 * on failure we delete it ourselves. */
static int fs_sfload_buffer(FluidSynthBackend* be) {
	fluid_sfloader_t* loader = new_fluid_defsfloader((fluid_settings_t*)be->settings);
	if (!loader)
		return -1;

	if (fluid_sfloader_set_callbacks(loader, sfmem_open, sfmem_read, sfmem_seek, sfmem_tell, sfmem_close) !=
		FLUID_OK) {
		delete_fluid_sfloader(loader);
		return -1;
	}

	fluid_synth_add_sfloader((fluid_synth_t*)be->synth, loader);

	char fname[64];
	snprintf(fname, sizeof fname, "mem:%llx", (unsigned long long)(uintptr_t)&be->sfBufKey);

	be->sfontId = fluid_synth_sfload((fluid_synth_t*)be->synth, fname, 1 /* reset */);
	/* On failure FluidSynth retains the loader; on success the
	 * loader stays registered for the synth's lifetime and is
	 * cleaned up by delete_fluid_synth. Either way we don't free
	 * `loader` directly here. */
	return (be->sfontId >= 0) ? 0 : -1;
}

static int fs_open(FluidSynthBackend* be, int sampleRate) {
	if (sampleRate <= 0)
		sampleRate = 44100;
	int polyphony = (be->polyphony > 0) ? be->polyphony : 256;

	be->settings = new_fluid_settings();
	if (!be->settings)
		return -1;

	fluid_settings_setnum((fluid_settings_t*)be->settings, "synth.sample-rate", (double)sampleRate);
	fluid_settings_setint((fluid_settings_t*)be->settings, "synth.polyphony", polyphony);
	/* Reverb + chorus driven via Roland GS SysEx + per-channel
	 * CC91/CC93; let FluidSynth render them. */
	fluid_settings_setint((fluid_settings_t*)be->settings, "synth.reverb.active", 1);
	fluid_settings_setint((fluid_settings_t*)be->settings, "synth.chorus.active", 1);
	fluid_settings_setnum((fluid_settings_t*)be->settings, "synth.gain", 0.6);

	be->synth = new_fluid_synth((fluid_settings_t*)be->settings);
	if (!be->synth) {
		delete_fluid_settings((fluid_settings_t*)be->settings);
		be->settings = NULL;
		return -1;
	}

	/* Two SoundFont sources, validated mutually exclusive at
	 * factory time: a file path goes through FluidSynth's default
	 * loader; a memory buffer goes through the SFLoader callbacks
	 * registered above. */
	int sfErr;
	if (be->soundfontPath) {
		be->sfontId = fluid_synth_sfload((fluid_synth_t*)be->synth, be->soundfontPath, 1 /* reset */);
		sfErr = (be->sfontId < 0);
	} else {
		sfErr = (fs_sfload_buffer(be) != 0);
	}
	if (sfErr) {
		delete_fluid_synth((fluid_synth_t*)be->synth);
		be->synth = NULL;
		delete_fluid_settings((fluid_settings_t*)be->settings);
		be->settings = NULL;
		return -1;
	}
	return 0;
}

static void fs_close(FluidSynthBackend* be) {
	if (be->synth) {
		delete_fluid_synth((fluid_synth_t*)be->synth);
		be->synth = NULL;
	}
	if (be->settings) {
		delete_fluid_settings((fluid_settings_t*)be->settings);
		be->settings = NULL;
	}
	be->sfontId = -1;
}

#else /* HAVE_FLUIDSYNTH not defined -- silent build */

static int fs_open(FluidSynthBackend* be, int sampleRate) {
	(void)be;
	(void)sampleRate;
	return -1;
}
static void fs_close(FluidSynthBackend* be) { (void)be; }

#endif

/* ===== Helpers ===== */

static void init_cc7_linearize(FluidSynthBackend* be) {
	for (int i = 0; i < 128; ++i) {
		double comp = sqrt((double)i * 127.0);
		int r = (int)(comp + 0.5);
		if (r < 0)
			r = 0;
		if (r > 127)
			r = 127;
		be->cc7_linearize[i] = (unsigned char)r;
	}
}

static int clamp_midi(int value, int maximum) {
	if (value < 0)
		return 0;
	return value > maximum ? maximum : value;
}

static int fluidsynth_write_message(void* user, const uint8_t* bytes, size_t size) {
	FluidSynthBackend* be = user;
#ifdef HAVE_FLUIDSYNTH
	fluid_synth_t* synth = be ? (fluid_synth_t*)be->synth : NULL;
	if (!synth || !bytes || size == 0)
		return -1;
	/* GMIDI expects transport acceptance here. FluidSynth's return values
	 * describe synth state instead; NoteOff legitimately fails when the key
	 * has already been released, but the MIDI message was still accepted. */
	if (bytes[0] == 0xf0) {
		if (size < 2 || bytes[size - 1] != 0xf7)
			return -1;
		(void)fluid_synth_sysex(synth, (const char*)bytes + 1, (int)size - 2, NULL, NULL, NULL, 0);
		return 0;
	}
	const int channel = bytes[0] & 0x0f;
	switch (bytes[0] & 0xf0) {
		case 0x80:
			if (size != 3)
				return -1;
			(void)fluid_synth_noteoff(synth, channel, bytes[1]);
			return 0;
		case 0x90:
			if (size != 3)
				return -1;
			(void)fluid_synth_noteon(synth, channel, bytes[1], bytes[2]);
			return 0;
		case 0xb0:
			if (size != 3)
				return -1;
			{
				const int value = bytes[1] == 7 ? be->cc7_linearize[bytes[2]] : bytes[2];
				(void)fluid_synth_cc(synth, channel, bytes[1], value);
			}
			return 0;
		case 0xc0:
			if (size != 2)
				return -1;
			(void)fluid_synth_program_change(synth, channel, bytes[1]);
			return 0;
		case 0xe0:
			if (size != 3)
				return -1;
			(void)fluid_synth_pitch_bend(synth, channel, bytes[1] | (bytes[2] << 7));
			return 0;
		default:
			return -1;
	}
#else
	(void)be;
	(void)bytes;
	(void)size;
	return -1;
#endif
}

static void request_reset(FluidSynthBackend* be) {
	(void)atomic_exchange_explicit(&be->resetRequested, true, memory_order_acq_rel);
}

static void enqueue_event(FluidSynthBackend* be, struct FluidMidiEvent event) {
	if (!be->opened || atomic_load_explicit(&be->resetRequested, memory_order_acquire))
		return;
	const unsigned int write = atomic_load_explicit(&be->writeIndex, memory_order_relaxed);
	const unsigned int read = atomic_load_explicit(&be->readIndex, memory_order_acquire);
	if (write - read >= 4096) {
		request_reset(be);
		return;
	}
	be->events[write & FLUID_EVENT_QUEUE_MASK] = event;
	atomic_store_explicit(&be->writeIndex, write + 1, memory_order_release);
}

static void apply_event(FluidSynthBackend* be, const struct FluidMidiEvent* event) {
	switch ((FluidMidiEventType)event->type) {
		case FLUID_EVENT_PROGRAM:
			(void)im_gmidi_driver_program_change(&be->driver, event->channel, event->data1);
			break;
		case FLUID_EVENT_NOTE_ON:
			(void)im_gmidi_driver_note_on(&be->driver, event->channel, event->data1, event->data2);
			break;
		case FLUID_EVENT_NOTE_OFF:
			(void)im_gmidi_driver_note_off(&be->driver, event->channel, event->data1);
			break;
		case FLUID_EVENT_CONTROL:
			(void)im_gmidi_driver_control_change(&be->driver, event->channel, event->data1, event->data2);
			break;
		case FLUID_EVENT_PITCH_BEND:
			(void)im_gmidi_driver_pitch_bend(&be->driver, event->channel, event->value);
			break;
	}
}

static void drain_events(FluidSynthBackend* be) {
	if (atomic_load_explicit(&be->resetRequested, memory_order_acquire)) {
		const unsigned int write = atomic_load_explicit(&be->writeIndex, memory_order_acquire);
		atomic_store_explicit(&be->readIndex, write, memory_order_release);
		im_gmidi_driver_deinit(&be->driver);
		im_gmidi_driver_init(&be->driver, fluidsynth_write_message, be);
		atomic_store_explicit(&be->resetRequested, false, memory_order_release);
		be_log(be, IMUSE_LOG_WARN, "midi_fluidsynth: event queue overflow; resetting synth");
		return;
	}
	unsigned int read = atomic_load_explicit(&be->readIndex, memory_order_relaxed);
	const unsigned int snapshot = atomic_load_explicit(&be->writeIndex, memory_order_acquire);
	while (read != snapshot) {
		apply_event(be, &be->events[read & FLUID_EVENT_QUEUE_MASK]);
		++read;
	}
	atomic_store_explicit(&be->readIndex, read, memory_order_release);
}

/* ===== Vtable hooks ===== */

static int vt_open(ImuseMidiBackend* self, int sampleRate, ImuseLogFunc logFunc, void* logUser) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	be->logFunc = logFunc;
	be->logUser = logUser;
	if (be->opened)
		return 0;

	init_cc7_linearize(be);

	/* Sanity: factory already validated exactly one source, but
	 * defend against a corrupted backend struct. */
	if (!be->soundfontPath && be->sfBufKey.magic != SFMEM_LOADKEY_MAGIC) {
		be_log(be, IMUSE_LOG_ERROR, "midi_fluidsynth: no soundfont source");
		return -1;
	}
	if (fs_open(be, sampleRate) != 0) {
		if (be->soundfontPath)
			be_log(be, IMUSE_LOG_ERROR, "midi_fluidsynth: FluidSynth open failed (sf='%s')",
				   be->soundfontPath);
		else
			be_log(be, IMUSE_LOG_ERROR, "midi_fluidsynth: FluidSynth open failed (sf=mem %zu bytes)",
				   be->sfBufKey.size);
		return -1;
	}
	atomic_store_explicit(&be->writeIndex, 0, memory_order_relaxed);
	atomic_store_explicit(&be->readIndex, 0, memory_order_relaxed);
	atomic_store_explicit(&be->resetRequested, false, memory_order_relaxed);
	im_gmidi_driver_init(&be->driver, fluidsynth_write_message, be);
	be->opened = 1;
	return 0;
}

static int vt_close(ImuseMidiBackend* self) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	if (!be->opened)
		return 0;
	im_gmidi_driver_deinit(&be->driver);
	fs_close(be);
	be->opened = 0;
	return 0;
}

static void vt_release(ImuseMidiBackend* self) {
	if (!self)
		return;
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	/* Defensive: if the host called release without going through
	 * imuse_destroy, close the synth first. Idempotent if already
	 * closed. */
	if (be->opened)
		vt_close(self);
	free(be->soundfontPath);
	free(be);
}

static void vt_program_change(ImuseMidiBackend* self, int channel, int program) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	TRACE_EVENT(be, 4, 0);
	if ((unsigned int)channel >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(be, (struct FluidMidiEvent) {
						  .type = FLUID_EVENT_PROGRAM,
						  .channel = (uint8_t)channel,
						  .data1 = (uint8_t)clamp_midi(program, 127),
					  });
}

static void vt_note_on(ImuseMidiBackend* self, int channel, int note, int velocity) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	TRACE_EVENT(be, 5, 1);
	if ((unsigned int)channel >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(be, (struct FluidMidiEvent) {
						  .type = FLUID_EVENT_NOTE_ON,
						  .channel = (uint8_t)channel,
						  .data1 = (uint8_t)clamp_midi(note, 127),
						  .data2 = (uint8_t)clamp_midi(velocity, 127),
					  });
}

static void vt_note_off(ImuseMidiBackend* self, int channel, int note) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	TRACE_EVENT(be, 6, 0);
	if ((unsigned int)channel >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(be, (struct FluidMidiEvent) {
						  .type = FLUID_EVENT_NOTE_OFF,
						  .channel = (uint8_t)channel,
						  .data1 = (uint8_t)clamp_midi(note, 127),
					  });
}

static void vt_control_change(ImuseMidiBackend* self, int channel, unsigned int ctrl, int value) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	TRACE_EVENT(be, 7, 0);
	if ((unsigned int)channel >= IM_GMIDI_CHANNEL_COUNT || ctrl > 127)
		return;
	enqueue_event(be, (struct FluidMidiEvent) {
						  .type = FLUID_EVENT_CONTROL,
						  .channel = (uint8_t)channel,
						  .data1 = (uint8_t)ctrl,
						  .data2 = (uint8_t)clamp_midi(value, 127),
					  });
}

static void vt_pitch_bend(ImuseMidiBackend* self, int channel, int wire14bit) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	TRACE_EVENT(be, 8, 0);
	if ((unsigned int)channel >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(be, (struct FluidMidiEvent) {
						  .value = (uint16_t)clamp_midi(wire14bit, 0x3fff),
						  .type = FLUID_EVENT_PITCH_BEND,
						  .channel = (uint8_t)channel,
					  });
}

static void vt_render_float(ImuseMidiBackend* self, float* buf, int frames) {
	FluidSynthBackend* be = (FluidSynthBackend*)self;
	if (!buf || frames <= 0)
		return;
	drain_events(be);
#ifdef HAVE_FLUIDSYNTH
	if (!be->synth) {
		memset(buf, 0, (size_t)frames * 2u * sizeof(float));
		return;
	}
	/* Interleaved stereo: left at buf[0,2,4,...], right at buf[1,3,5,...]. */
	fluid_synth_write_float((fluid_synth_t*)be->synth, frames, buf, 0, 2, buf, 1, 2);
#else
	(void)be;
	memset(buf, 0, (size_t)frames * 2u * sizeof(float));
#endif
}

/* ===== Factory ===== */

int imuse_fluidsynth_backend_available(void) {
#ifdef HAVE_FLUIDSYNTH
	return 1;
#else
	return 0;
#endif
}

int imuse_fluidsynth_soundfont_is_valid(const char* path) {
#ifdef HAVE_FLUIDSYNTH
	return path && path[0] && fluid_is_soundfont(path);
#else
	(void)path;
	return 0;
#endif
}

ImuseMidiBackend* imuse_fluidsynth_backend_create(const ImuseFluidSynthConfig* cfg) {
	if (!cfg)
		return NULL;

	/* Validate the SoundFont source: exactly one of {path, buffer}. */
	int hasPath = cfg->soundfontPath != NULL;
	int hasBuf = cfg->soundfontData != NULL;
	if (hasPath == hasBuf) /* both set or neither set */
		return NULL;
	if (hasBuf && cfg->soundfontSize == 0)
		return NULL;

	FluidSynthBackend* be = calloc(1, sizeof *be);
	if (!be)
		return NULL;

	if (hasPath) {
		/* strdup so the host can free its copy of the path immediately. */
		size_t plen = strlen(cfg->soundfontPath);
		be->soundfontPath = (char*)malloc(plen + 1);
		if (!be->soundfontPath) {
			free(be);
			return NULL;
		}
		memcpy(be->soundfontPath, cfg->soundfontPath, plen + 1);
	} else {
		/* Borrow the host's buffer. The magic guards the loader's
		 * open() callback against a stray non-mem path landing in
		 * the dispatcher. */
		be->sfBufKey.magic = SFMEM_LOADKEY_MAGIC;
		be->sfBufKey.data = cfg->soundfontData;
		be->sfBufKey.size = cfg->soundfontSize;
	}

	be->polyphony = cfg->polyphony;
	be->sfontId = -1;

	be->vtable.open = vt_open;
	be->vtable.close = vt_close;
	be->vtable.release = vt_release;
	be->vtable.program_change = vt_program_change;
	be->vtable.note_on = vt_note_on;
	be->vtable.note_off = vt_note_off;
	be->vtable.control_change = vt_control_change;
	be->vtable.pitch_bend = vt_pitch_bend;
	be->vtable.render_float = vt_render_float;

	return &be->vtable;
}
