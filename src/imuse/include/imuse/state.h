#ifndef LIBIMUSE_PUBLIC_STATE_H
#define LIBIMUSE_PUBLIC_STATE_H

#include <stddef.h>
#include <stdint.h>

#include <imuse/handle.h>

/* Versioned game-save snapshot. The little-endian header contains magic,
 * format, ABI, and mixer count; incompatible headers are rejected. Transient
 * audio-bus state is not serialized, and load mutates its input as scratch. */

#ifdef __cplusplus
extern "C" {
#endif

/* 'iMSv' as four LE bytes — printable in `xxd` output for diagnostics. */
#define IMUSE_STATE_MAGIC 0x76534D69u /* 'i','M','S','v' */
#define IMUSE_STATE_FORMAT_VERSION 2u
#define IMUSE_STATE_HEADER_SIZE 16u

/* Returns the maximum byte count an `imuse_state_save` of the current
 * engine could produce. The figure is exact — the engine's state pools
 * are fixed-size — so callers can pre-allocate a buffer of exactly
 * this size without overflow. Returns 0 on `im == NULL`. */
size_t imuse_state_query_size(imuse_t* im);

/* Serialise the current engine state into `buf` (which must have at
 * least `imuse_state_query_size(im)` bytes). Returns the number of
 * bytes actually written. Returns 0 on any error (NULL inputs, buffer
 * too small).
 *
 * Safe to call from any thread provided no other thread is mutating
 * the same `imuse_t` (the engine is not internally serialised against
 * concurrent saves). */
size_t imuse_state_save(imuse_t* im, void* buf, size_t size);

/* Replace the engine's live state with the contents of `buf`. The
 * engine first calls StopAllSounds to drain anything currently
 * playing, then validates the wire-format header and stamps the
 * payload over its pools. Returns 1 on success, 0 on failure
 * (header mismatch, truncated buffer, NULL inputs).
 *
 * On success the engine resumes whatever was playing at save time.
 * On failure the engine has already drained but no state was loaded —
 * the engine is left silent.
 *
 * `buf` is mutated in place during the load (used as scratch for the
 * sequencer cursor replay). */
int imuse_state_load(imuse_t* im, void* buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LIBIMUSE_PUBLIC_STATE_H */
