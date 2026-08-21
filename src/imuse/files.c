#include "internal/files.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/midi.h"
#include "internal/utils.h"
#include "internal/wave.h"
#include <imuse/commands.h>

#include <stddef.h>

/* soundId accepts both numeric IDs and pointer-valued host handles. GotoChunk
 * selects the zero-based matching chunk; mismatches do not consume the index. */

/* BE dword reads for the IFF-like chunk walker below go through
 * ImUtils_Swap32(im, despite the misleading binary-side name,
 * that function is a BE-bytes → host u32 decoder, not an
 * in-place swap). */
#define read_be32(buf) ImUtils_Swap32(im, buf)

/* ===== Lifecycle ===== */

int ImFiles_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "FILES module...");
	/* Host callbacks were already copied into im->files.host by
	 * ImCommands_Init. getSoundPtrFunc is the only required one;
	 * the streamer callbacks are optional. */
	if (!im->files.host.getSoundPtrFunc)
		return -1;
	im->files.initialized = 1;
	return 0;
}

int ImFiles_Deinit(imuse_t* im) {
	im->files.initialized = 0;
	return 0;
}

/* ===== Host-callback wrappers ===== */

int ImFiles_IsSoundIdValid(imuse_t* im, intptr_t soundId) {
	(void)im;
	/* Reject 0 (universal "no sound" sentinel) and -1 (wildcard / invalid
	 * marker used by a few trigger / defer opcodes). */
	return soundId != 0 && soundId != -1;
}

void* ImFiles_GetSoundPtr(imuse_t* im, intptr_t soundId) {
	if (ImFiles_IsSoundIdValid(im, soundId) && im->files.host.getSoundPtrFunc) {
		return im->files.host.getSoundPtrFunc(soundId);
	}
	ImDebug_LogMsg(im, "ERR: soundAddrFunc failure in files.c...");
	return NULL;
}

int ImFiles_GetSoundType(imuse_t* im, intptr_t soundId) {
	/* Walk the active-MIDI list first, then the active-wave list. Each
	 * backend's GetNextSound enumerates its live sounds in ascending
	 * soundId order, terminating with 0.
	 *
	 * Note that the original does NOT reset the iterator between the two
	 * loops — but by construction the iterator IS 0 when the first loop
	 * exits (it only exits when GetNextSound returns 0), so this is
	 * equivalent to restarting at 0. Mirror that faithfully. */
	intptr_t iter = 0;
	for (;;) {
		intptr_t next = ImMidi_GetNextSound(im, iter);
		iter = next;
		if (!next)
			break;
		if (next == soundId)
			return IMUSE_SOUND_TYPE_MIDI;
	}
	for (;;) {
		intptr_t next = ImWave_GetNextSound(im, iter);
		iter = next;
		if (!next)
			break;
		if (next == soundId)
			return IMUSE_SOUND_TYPE_WAVE;
	}
	return IMUSE_SOUND_TYPE_INVALID;
}

int ImFiles_SeekSound(imuse_t* im, intptr_t soundId, int offset, int whence) {
	if (ImFiles_IsSoundIdValid(im, soundId) && im->files.host.seekFunc) {
		return im->files.host.seekFunc(soundId, offset, whence);
	}
	ImDebug_LogMsg(im, "ERR: seekFunc failure in files.c...");
	return 0;
}

int ImFiles_ReadSound(imuse_t* im, intptr_t soundId, void* buf, int size) {
	if (ImFiles_IsSoundIdValid(im, soundId) && im->files.host.readFunc) {
		return im->files.host.readFunc(soundId, buf, size);
	}
	ImDebug_LogMsg(im, "ERR: readFunc failure in files.c...");
	return 0;
}

ImuseStreamBuffer* ImFiles_GetBufInfo(imuse_t* im, int bufId) {
	/* bufId is 1-based; 0 is the invalid sentinel. Original also
	 * short-circuits on bufId == 0 before touching the callback. */
	if (bufId && im->files.host.getBufInfoFunc) {
		return im->files.host.getBufInfoFunc(bufId);
	}
	ImDebug_LogMsg(im, "ERR: bufInfoFunc failure in files.c...");
	return NULL;
}

/* ===== IFF-like chunk walker ===== */

void* ImFiles_GotoChunk(imuse_t* im, void* buf, const char* chunkType, int chunkId) {
	if (!buf)
		return NULL;

	unsigned char* base = (unsigned char*)buf;
	uint32_t total = read_be32(base + 4);
	unsigned char* chunks = base + 8;

	/* Walk chunks in order. For each:
	 *   - Compare 4-byte type tag.
	 *   - If match and chunkId counter has reached 0, return this chunk's
	 *     type pointer (caller reads payload at ret+8).
	 *   - Otherwise skip past (size field + payload) and continue. */
	uint32_t cursor = 0;
	while (cursor < total) {
		int mismatch = 0;
		for (int i = 0; i < 4; ++i) {
			if (chunks[cursor + i] != (unsigned char)chunkType[i]) {
				mismatch = 1;
			}
		}
		cursor += 4;

		if (!mismatch) {
			if (chunkId == 0)
				return &chunks[cursor - 4];
			--chunkId;
		}

		uint32_t payloadSize = read_be32(&chunks[cursor]);
		cursor += payloadSize + 4;
	}
	return NULL;
}
