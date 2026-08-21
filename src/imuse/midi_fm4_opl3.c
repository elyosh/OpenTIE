#include <imuse/midi_fm4_opl3.h>

#include "internal/fm4_driver.h"
#include "internal/midi_backend.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FM4_OPL3

#include <opl3.h>

enum {
	FM4_EVENT_QUEUE_CAPACITY = 4096,
	FM4_EVENT_QUEUE_MASK = FM4_EVENT_QUEUE_CAPACITY - 1,
	FM4_RENDER_CHUNK_FRAMES = 512,
};

typedef enum ImFm4EventType {
	FM4_EVENT_PROGRAM,
	FM4_EVENT_NOTE_ON,
	FM4_EVENT_NOTE_OFF,
	FM4_EVENT_CONTROL,
	FM4_EVENT_PITCH_BEND,
} ImFm4EventType;

typedef struct ImFm4Event {
	uint16_t value;
	uint8_t type;
	uint8_t channel;
	uint8_t data1;
	uint8_t data2;
} ImFm4Event;

_Static_assert((FM4_EVENT_QUEUE_CAPACITY & (FM4_EVENT_QUEUE_CAPACITY - 1)) == 0,
			   "FM4 event queue capacity must be a power of two");
_Static_assert(sizeof(ImFm4Event) == 6, "FM4 event layout must remain compact");

typedef struct ImFm4Opl3Backend {
	ImuseMidiBackend vtable;
	ImFm4Event events[FM4_EVENT_QUEUE_CAPACITY];
	_Alignas(64) atomic_uint writeIndex;
	_Alignas(64) atomic_uint readIndex;
	atomic_bool resetRequested;
	atomic_uint overflowCount;
	atomic_uint resetCount;
	ImFm4Driver driver;
	opl3_chip chip;
	ImuseLogFunc logFunc;
	void* logUser;
	int opened;
} ImFm4Opl3Backend;

static int clamp_int(int value, int minimum, int maximum) {
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

static void opl_write(void* user, uint16_t reg, uint8_t value) {
	OPL3_WriteReg((opl3_chip*)user, reg, value);
}

static void reset_synth(ImFm4Opl3Backend* backend, int sampleRate) {
	OPL3_Reset(&backend->chip, (uint32_t)sampleRate);
	im_fm4_driver_init(&backend->driver, opl_write, &backend->chip);
}

static void request_reset(ImFm4Opl3Backend* backend) {
	bool wasRequested = atomic_exchange_explicit(&backend->resetRequested, true, memory_order_acq_rel);
	if (wasRequested)
		return;
	atomic_fetch_add_explicit(&backend->overflowCount, 1u, memory_order_relaxed);
	if (backend->logFunc)
		backend->logFunc(backend->logUser, IMUSE_LOG_WARN,
						 "midi_fm4_opl3: event queue overflow; resetting synth");
}

static void enqueue_event(ImFm4Opl3Backend* backend, ImFm4Event event) {
	if (!backend->opened || atomic_load_explicit(&backend->resetRequested, memory_order_acquire))
		return;
	unsigned int write = atomic_load_explicit(&backend->writeIndex, memory_order_relaxed);
	unsigned int read = atomic_load_explicit(&backend->readIndex, memory_order_acquire);
	if (write - read >= FM4_EVENT_QUEUE_CAPACITY) {
		request_reset(backend);
		return;
	}
	backend->events[write & FM4_EVENT_QUEUE_MASK] = event;
	atomic_store_explicit(&backend->writeIndex, write + 1u, memory_order_release);
}

static void apply_event(ImFm4Opl3Backend* backend, const ImFm4Event* event) {
	switch ((ImFm4EventType)event->type) {
		case FM4_EVENT_PROGRAM:
			im_fm4_driver_program_change(&backend->driver, event->channel, event->data1);
			break;
		case FM4_EVENT_NOTE_ON:
			(void)im_fm4_driver_note_on(&backend->driver, event->channel, event->data1, event->data2);
			break;
		case FM4_EVENT_NOTE_OFF:
			(void)im_fm4_driver_note_off(&backend->driver, event->channel, event->data1);
			break;
		case FM4_EVENT_CONTROL:
			im_fm4_driver_control_change(&backend->driver, event->channel, event->data1, event->data2);
			break;
		case FM4_EVENT_PITCH_BEND:
			im_fm4_driver_pitch_bend(&backend->driver, event->channel, event->value);
			break;
	}
}

static void drain_events(ImFm4Opl3Backend* backend, int sampleRate) {
	if (atomic_load_explicit(&backend->resetRequested, memory_order_acquire)) {
		unsigned int write = atomic_load_explicit(&backend->writeIndex, memory_order_acquire);
		atomic_store_explicit(&backend->readIndex, write, memory_order_release);
		reset_synth(backend, sampleRate);
		atomic_fetch_add_explicit(&backend->resetCount, 1u, memory_order_relaxed);
		atomic_store_explicit(&backend->resetRequested, false, memory_order_release);
		return;
	}

	unsigned int read = atomic_load_explicit(&backend->readIndex, memory_order_relaxed);
	unsigned int snapshot = atomic_load_explicit(&backend->writeIndex, memory_order_acquire);
	while (read != snapshot) {
		apply_event(backend, &backend->events[read & FM4_EVENT_QUEUE_MASK]);
		++read;
	}
	atomic_store_explicit(&backend->readIndex, read, memory_order_release);
}

static int vt_open(ImuseMidiBackend* self, int sampleRate, ImuseLogFunc logFunc, void* logUser) {
	ImFm4Opl3Backend* backend = (ImFm4Opl3Backend*)self;
	if (backend->opened)
		return 0;
	if (sampleRate <= 0)
		return -1;
	backend->logFunc = logFunc;
	backend->logUser = logUser;
	atomic_store_explicit(&backend->writeIndex, 0u, memory_order_relaxed);
	atomic_store_explicit(&backend->readIndex, 0u, memory_order_relaxed);
	atomic_store_explicit(&backend->resetRequested, false, memory_order_relaxed);
	atomic_store_explicit(&backend->overflowCount, 0u, memory_order_relaxed);
	atomic_store_explicit(&backend->resetCount, 0u, memory_order_relaxed);
	reset_synth(backend, sampleRate);
	backend->opened = sampleRate;
	return 0;
}

static int vt_close(ImuseMidiBackend* self) {
	ImFm4Opl3Backend* backend = (ImFm4Opl3Backend*)self;
	if (backend->opened)
		im_fm4_driver_deinit(&backend->driver);
	backend->opened = 0;
	return 0;
}

static void vt_release(ImuseMidiBackend* self) {
	if (!self)
		return;
	ImFm4Opl3Backend* backend = (ImFm4Opl3Backend*)self;
	if (backend->opened)
		(void)vt_close(self);
	free(backend);
}

static void vt_program_change(ImuseMidiBackend* self, int channel, int program) {
	if (channel < 0 || channel >= IM_FM4_CHANNEL_COUNT)
		return;
	ImFm4Event event = {
		.type = FM4_EVENT_PROGRAM,
		.channel = (uint8_t)channel,
		.data1 = (uint8_t)clamp_int(program, 0, 127),
	};
	enqueue_event((ImFm4Opl3Backend*)self, event);
}

static void vt_note_on(ImuseMidiBackend* self, int channel, int note, int velocity) {
	if (channel < 0 || channel >= IM_FM4_CHANNEL_COUNT)
		return;
	ImFm4Event event = {
		.type = FM4_EVENT_NOTE_ON,
		.channel = (uint8_t)channel,
		.data1 = (uint8_t)clamp_int(note, 0, 127),
		.data2 = (uint8_t)clamp_int(velocity, 0, 127),
	};
	enqueue_event((ImFm4Opl3Backend*)self, event);
}

static void vt_note_off(ImuseMidiBackend* self, int channel, int note) {
	if (channel < 0 || channel >= IM_FM4_CHANNEL_COUNT)
		return;
	ImFm4Event event = {
		.type = FM4_EVENT_NOTE_OFF,
		.channel = (uint8_t)channel,
		.data1 = (uint8_t)clamp_int(note, 0, 127),
	};
	enqueue_event((ImFm4Opl3Backend*)self, event);
}

static void vt_control_change(ImuseMidiBackend* self, int channel, unsigned int controller, int value) {
	if (channel < 0 || channel >= IM_FM4_CHANNEL_COUNT || controller > 127u)
		return;
	ImFm4Event event = {
		.type = FM4_EVENT_CONTROL,
		.channel = (uint8_t)channel,
		.data1 = (uint8_t)controller,
		.data2 = (uint8_t)clamp_int(value, 0, 127),
	};
	enqueue_event((ImFm4Opl3Backend*)self, event);
}

static void vt_pitch_bend(ImuseMidiBackend* self, int channel, int pitchBend14) {
	if (channel < 0 || channel >= IM_FM4_CHANNEL_COUNT)
		return;
	ImFm4Event event = {
		.value = (uint16_t)clamp_int(pitchBend14, 0, 0x3fff),
		.type = FM4_EVENT_PITCH_BEND,
		.channel = (uint8_t)channel,
	};
	enqueue_event((ImFm4Opl3Backend*)self, event);
}

static void vt_render_float(ImuseMidiBackend* self, float* buffer, int frames) {
	ImFm4Opl3Backend* backend = (ImFm4Opl3Backend*)self;
	if (!buffer || frames <= 0)
		return;
	if (!backend->opened) {
		memset(buffer, 0, (size_t)frames * 2u * sizeof(float));
		return;
	}

	drain_events(backend, backend->opened);
	int16_t scratch[FM4_RENDER_CHUNK_FRAMES * 2];
	int written = 0;
	while (written < frames) {
		int chunk = frames - written;
		if (chunk > FM4_RENDER_CHUNK_FRAMES)
			chunk = FM4_RENDER_CHUNK_FRAMES;
		OPL3_GenerateStream(&backend->chip, scratch, (uint32_t)chunk);
		float* destination = buffer + (size_t)written * 2u;
		for (int sample = 0; sample < chunk * 2; ++sample)
			destination[sample] = (float)scratch[sample] / 32768.0f;
		written += chunk;
	}
}

int imuse_fm4_opl3_backend_available(void) { return 1; }

ImuseMidiBackend* imuse_fm4_opl3_backend_create(void) {
	ImFm4Opl3Backend* backend = calloc(1, sizeof *backend);
	if (!backend)
		return NULL;
	backend->vtable.open = vt_open;
	backend->vtable.close = vt_close;
	backend->vtable.release = vt_release;
	backend->vtable.program_change = vt_program_change;
	backend->vtable.note_on = vt_note_on;
	backend->vtable.note_off = vt_note_off;
	backend->vtable.control_change = vt_control_change;
	backend->vtable.pitch_bend = vt_pitch_bend;
	backend->vtable.render_float = vt_render_float;
	return &backend->vtable;
}

#else

int imuse_fm4_opl3_backend_available(void) { return 0; }

ImuseMidiBackend* imuse_fm4_opl3_backend_create(void) { return NULL; }

#endif
