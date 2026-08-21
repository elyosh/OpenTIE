#ifndef __IMUSE_STREAMER_H__
#define __IMUSE_STREAMER_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Two ring-buffered stream slots use host-owned storage. A maxRead-sized
 * shadow beyond each ring exposes wrapped reads as linear spans. VOC loop
 * triggers notify the dispatcher when the producer crosses their file offset. */

struct ImuseStreamBuffer;

/* 56-byte ring-buffered stream slot. */
typedef struct ImWaveStream {
	intptr_t soundId;             /* 0 = slot free */
	int32_t curOffset;            /* producer cursor in the source file */
	int32_t endOffset;            /* total source file length */
	int32_t bufId;                /* ImuseStreamBuffer index */
	uint8_t* buf;                 /* ring + shadow base pointer */
	int32_t bufFreeSize;          /* usable ring size (bufSize - maxRead) */
	int32_t loadSize;             /* bytes per FetchData refill target */
	int32_t lowWaterMark;         /* ProcessStreams critical threshold */
	int32_t maxRead;              /* biggest single consumer pull */
	int32_t loadIndex;            /* producer cursor in the ring */
	int32_t readIndex;            /* consumer cursor in the ring */
	int32_t paused;               /* 1 = source exhausted or error */
	int32_t vocLoopFlag;          /* 1 = loop callback armed */
	int32_t vocLoopTriggerOffset; /* source-file offset that fires it */
} ImWaveStream;

/* ===== Lifecycle ===== */

int ImStreamer_Init(imuse_t* im);
int ImStreamer_Deinit(imuse_t* im);

/* ===== Slot management ===== */

ImWaveStream* ImStreamer_AllocSound(imuse_t* im, intptr_t soundId, int bufId, unsigned int maxRead);

/* Unbind on track teardown. Does NOT clear the ring — a later
 * AllocSound will reset the cursors when reusing the slot. */
void ImStreamer_ClearSoundInStream(imuse_t* im, ImWaveStream* stream);

/* Rebind the stream slot onto a new sound + offset. Caller
 * handles any read/load cursor reconciliation beforehand. */
void ImStreamer_SetSoundToStreamFromOffset(imuse_t* im, ImWaveStream* stream, intptr_t soundId, int offset);

/* ===== Per-tick producer ===== */

/* Fair-round-robin refill across both slots. Fills near-
 * starving slots first; on ties, alternates via lastStreamLoaded. */
int ImStreamer_ProcessStreams(imuse_t* im);

/* ===== Consumer reads ===== */

/* Fetch `size` linear bytes starting at readIndex and advance
 * readIndex past them. Returns NULL if less than `size` is
 * available or if size > maxRead. */
uint8_t* ImStreamer_GetStreamBuffer(imuse_t* im, ImWaveStream* stream, unsigned int size);

/* Non-advancing peek at `size` bytes starting (readIndex +
 * offset). Returns NULL on the same conditions as
 * GetStreamBuffer. */
uint8_t* ImStreamer_PeekAt(imuse_t* im, ImWaveStream* stream, int offset, unsigned int size);

/* Bytes loaded-but-not-yet-consumed in the ring. */
int ImStreamer_GetAvailableBytes(imuse_t* im, ImWaveStream* stream);

/* Advance readIndex / loadIndex by an offset (no byte copy).
 * Returns 0 on success, -1 if offset > available. */
int ImStreamer_SetReadIndex(imuse_t* im, ImWaveStream* stream, unsigned int offset);
int ImStreamer_SetLoadIndex(imuse_t* im, ImWaveStream* stream, unsigned int offset);

/* ===== VOC loop triggers ===== */

void ImStreamer_SetLoopFlag(imuse_t* im, ImWaveStream* stream, int offset);
void ImStreamer_RemoveLoopFlag(imuse_t* im, ImWaveStream* stream);

/* ===== IMUSE_CMD_QUERY_STREAM reporter ===== */

void ImStreamer_QueryStream(imuse_t* im, ImWaveStream* stream, int* bufSize, int* lowWaterMark,
							int* available);

#endif /* __IMUSE_STREAMER_H__ */
