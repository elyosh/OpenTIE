#ifndef __IMUSE_FILES_H__
#define __IMUSE_FILES_H__

#include <stdint.h>

#include <imuse/handle.h>

/*
 * iMUSE engine -- FILES module.
 *
 * Host I/O bridge. Wraps the four callback fields that the host fills
 * into ImuseHost (getSoundPtrFunc, seekFunc, readFunc, getBufInfoFunc)
 * and adds an IFF-like chunk walker (ImFiles_GotoChunk) plus a couple of
 * helpers the rest of the engine calls into (IsSoundIdValid,
 * GetSoundType).
 *
 * The original DOS build returned -1 for "unknown sound" and 1 / 2 for
 * MIDI / VOC; those codes are exposed as macros. Callback signatures
 * here match the iMUSE ABI (soundId as u32, offset as int, etc.), which
 * means the `int`-fits-a-pointer trick the DOS binary used in
 * ImuseStreamBuffer.buffer is fixed to a proper void* below.
 */

/* ImuseStreamBuffer + IMUSE_SOUND_TYPE_* are the public API now;
 * pull them in from <imuse/filelist.h> rather than redefining. */
#include <imuse/filelist.h>

/* ===== Lifecycle ===== */

int ImFiles_Init(imuse_t* im);
int ImFiles_Deinit(imuse_t* im);

/* ===== Host-callback wrappers ===== */

/* Validity test used by every host-callback gate. Rejects 0 (free-slot
 * sentinel across the engine) and -1 (wildcard / invalid sentinel used
 * in a handful of trigger / defer APIs). The DOS build rejected the
 * whole top-16 range; with intptr_t soundIds that range is unreachable
 * by any real pointer so the simpler two-sentinel check suffices. */
int ImFiles_IsSoundIdValid(imuse_t* im, intptr_t soundId);

/* Resolve a loaded sound's in-memory buffer via the host's
 * getSoundPtrFunc. Logs and returns NULL on invalid id / missing
 * callback / host returning NULL. */
void* ImFiles_GetSoundPtr(imuse_t* im, intptr_t soundId);

/* Walk the active-sound enumeration in MIDI then WAVE order and return
 * the IM_SOUND_TYPE_* that the soundId is playing as. Returns
 * IMUSE_SOUND_TYPE_INVALID if the id isn't live. */
int ImFiles_GetSoundType(imuse_t* im, intptr_t soundId);

/* POSIX-style seek on a streamed sound. Returns the resulting absolute
 * offset, or 0 on any host-side failure (with a debug log). */
int ImFiles_SeekSound(imuse_t* im, intptr_t soundId, int offset, int whence);

/* Read size bytes from the host cursor for soundId into buf. Returns
 * bytes actually read, or 0 on any host-side failure. */
int ImFiles_ReadSound(imuse_t* im, intptr_t soundId, void* buf, int size);

/* Look up the host-supplied ImuseStreamBuffer descriptor for the given
 * stream-buffer id (1-based). Returns NULL on invalid id / missing
 * callback. */
ImuseStreamBuffer* ImFiles_GetBufInfo(imuse_t* im, int bufId);

/* ===== IFF-like chunk walker ===== */

/* Find the (chunkId+1)th occurrence of `chunkType` (a 4-byte ASCII tag,
 * e.g. "MTrk") inside an IMUSE-MIDI container at `buf`. Container layout:
 *   [0..3]  outer magic (e.g. 'MIDI')
 *   [4..7]  total chunk-block size (big-endian u32)
 *   [8..]   concatenated chunks:
 *              [0..3]  chunk type (4 ASCII)
 *              [4..7]  chunk size (big-endian u32)
 *              [8..]   chunk payload
 * Returns a pointer to the matching chunk's type byte (payload at +8).
 * NULL buf or overrun returns NULL. */
void* ImFiles_GotoChunk(imuse_t* im, void* buf, const char* chunkType, int chunkId);

#endif /* __IMUSE_FILES_H__ */
