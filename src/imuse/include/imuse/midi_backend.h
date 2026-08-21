#ifndef LIBIMUSE_PUBLIC_MIDI_BACKEND_H
#define LIBIMUSE_PUBLIC_MIDI_BACKEND_H

/*
 * libimuse — pluggable MIDI backend handle.
 *
 * The iMUSE engine talks to a single MIDI synth per session through
 * an opaque backend handle. The host obtains a handle from a
 * backend-specific factory (e.g. imuse_fluidsynth_backend_create
 * from <imuse/midi_fluidsynth.h>) and passes it to imuse_create as
 * the `backend` argument.
 *
 * Lifetime is library-owned: imuse_create takes ownership of the
 * handle unconditionally — including when imuse_create itself fails
 * — and frees it during imuse_destroy (or on the create-time
 * rollback path). The host MUST NOT release the handle and MUST
 * NOT pass the same handle to two concurrent sessions. Every
 * factory call must therefore be paired with an imuse_create call;
 * there is no public release entry point.
 */

typedef struct ImuseMidiBackend ImuseMidiBackend;

#endif /* LIBIMUSE_PUBLIC_MIDI_BACKEND_H */
