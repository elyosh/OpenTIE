#ifndef LIBIMUSE_PUBLIC_MIDI_FM4_OPL3_H
#define LIBIMUSE_PUBLIC_MIDI_FM4_OPL3_H

/* Self-contained FM4 OPL3 backend. Each iMUSE session requires its own handle. */

#include <imuse/midi_backend.h>

/* Nonzero when this libimuse build contains Nuked-OPL3-fast support. */
int imuse_fm4_opl3_backend_available(void);

/* Allocate a backend handle. Returns NULL when support is unavailable or the
 * descriptor allocation fails. Ownership transfers to imuse_create. */
ImuseMidiBackend* imuse_fm4_opl3_backend_create(void);

#endif /* LIBIMUSE_PUBLIC_MIDI_FM4_OPL3_H */
