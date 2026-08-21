#include "internal/midi_backend.h"
#include "internal/state.h"

#include <string.h>

/*
 * libimuse — MIDI backend dispatch.
 *
 * Engine code (slots.c, mix.c) calls the ImMidi_* helpers below;
 * each fans out through the per-session backend vtable stored at
 * im->midi.backend. NULL backend or NULL vtable slot = silent
 * no-op (or silence for render_float).
 */

void ImMidi_ProgramChange(imuse_t* im, int channel, int program) {
	if (im && im->midi.backend && im->midi.backend->program_change)
		im->midi.backend->program_change(im->midi.backend, channel, program);
}

void ImMidi_NoteOn(imuse_t* im, int channel, int note, int velocity) {
	if (im && im->midi.backend && im->midi.backend->note_on)
		im->midi.backend->note_on(im->midi.backend, channel, note, velocity);
}

void ImMidi_NoteOff(imuse_t* im, int channel, int note) {
	if (im && im->midi.backend && im->midi.backend->note_off)
		im->midi.backend->note_off(im->midi.backend, channel, note);
}

void ImMidi_ControlChange(imuse_t* im, int channel, unsigned int ctrl, int value) {
	if (im && im->midi.backend && im->midi.backend->control_change)
		im->midi.backend->control_change(im->midi.backend, channel, ctrl, value);
}

void ImMidi_PitchBend(imuse_t* im, int channel, int wire14bit) {
	if (im && im->midi.backend && im->midi.backend->pitch_bend)
		im->midi.backend->pitch_bend(im->midi.backend, channel, wire14bit);
}

void ImMidi_RenderFloat(imuse_t* im, float* buf, int frames) {
	if (!buf || frames <= 0)
		return;
	if (!im || !im->midi.backend || !im->midi.backend->render_float) {
		memset(buf, 0, (size_t)frames * 2u * sizeof(float));
		return;
	}
	im->midi.backend->render_float(im->midi.backend, buf, frames);
}

/* Internal release helper. Called by imuse_create rollback and
 * imuse_destroy — never exposed publicly. The vtable owns its own
 * free logic; this is a NULL-safe thin forwarder. */
void ImMidi_BackendRelease(ImuseMidiBackend* backend) {
	if (backend && backend->release)
		backend->release(backend);
}
