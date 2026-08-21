#include "internal/streamer.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/dispatch.h"
#include "internal/files.h"

#include <string.h>

/*
 *  - Ring layout: [bufFreeSize usable bytes | maxRead shadow zone].
 *    The producer writes into [0, bufFreeSize) with wrap; when
 *    a consumer request (Read / Peek) straddles the wrap, the
 *    producer-facing code mirrors the overlap bytes into the
 *    shadow zone so the returned pointer spans linearly.
 *
 *  - Empty-state disambiguation: FetchData never fills the last
 *    byte before readIndex (the 'freeMinusSentinel' step). So
 *    loadIndex == readIndex unambiguously means 'ring empty',
 *    never 'ring full'.
 *
 *  - Fair-round-robin: lastStreamLoaded tracks which slot
 *    FetchData wrote to last; ProcessStreams consults it to
 *    alternate refills when neither slot is starving and both
 *    are active.
 */

/* ===== Module-private state ===== */

/* ===== Forward decls ===== */

static void streamer_fetchData(imuse_t* im, ImWaveStream* stream);

/* ===== Lifecycle ===== */

int ImStreamer_Init(imuse_t* im) {
	for (int i = 0; i < 2; ++i)
		im->streamer.streams[i].soundId = 0;
	im->streamer.lastStreamLoaded = 0;
	return 0;
}

int ImStreamer_Deinit(imuse_t* im) {
	/* Static slot storage + host-owned ring buffers: no work. */
	return 0;
}

/* ===== Slot management ===== */

ImWaveStream* ImStreamer_AllocSound(imuse_t* im, intptr_t soundId, int bufId, unsigned int maxRead) {
	ImuseStreamBuffer* info = ImFiles_GetBufInfo(im, bufId);
	if (!info) {
		ImDebug_LogMsg(im, "ERR: streamer couldn't get buffer info...");
		return 0;
	}
	/* bufSize/4 > maxRead guarantees at least 4x the largest
	 * single consumer pull fits in the ring. Without this the
	 * low-water / load-size heuristics are meaningless. */
	if ((unsigned int)(info->bufSize >> 2) <= maxRead) {
		ImDebug_LogMsg(im, "ERR: maxRead too big...");
		return 0;
	}

	/* Reject if another slot already owns this bufId — two
	 * sounds can't share one host ring region. */
	for (int i = 0; i < 2; ++i) {
		if (im->streamer.streams[i].soundId && im->streamer.streams[i].bufId == bufId) {
			ImDebug_LogMsg(im, "ERR: stream bufID %lu already in use...", (unsigned long)bufId);
			return 0;
		}
	}

	/* Find a free slot. */
	ImWaveStream* slot = 0;
	for (int i = 0; i < 2; ++i) {
		if (!im->streamer.streams[i].soundId) {
			slot = &im->streamer.streams[i];
			break;
		}
	}
	if (!slot) {
		ImDebug_LogMsg(im, "ERR: no spare stream...");
		return 0;
	}

	/* Seed the slot. bufFreeSize reserves the trailing maxRead
	 * bytes as the wrap-shadow zone. */
	slot->soundId = soundId;
	slot->curOffset = 0;
	slot->endOffset = ImFiles_SeekSound(im, soundId, 0, 2 /* SEEK_END */);
	slot->bufId = bufId;
	slot->buf = (uint8_t*)info->buffer;
	slot->bufFreeSize = info->bufSize - (int)maxRead;
	slot->loadSize = info->loadSize;
	slot->lowWaterMark = info->lowWaterMark;
	slot->maxRead = (int)maxRead;
	slot->loadIndex = 0;
	slot->readIndex = 0;
	slot->paused = 0;
	slot->vocLoopFlag = 0;
	slot->vocLoopTriggerOffset = 0;
	return slot;
}

void ImStreamer_ClearSoundInStream(imuse_t* im, ImWaveStream* stream) {
	stream->soundId = 0;
	if (im->streamer.lastStreamLoaded == stream)
		im->streamer.lastStreamLoaded = 0;
}

void ImStreamer_SetSoundToStreamFromOffset(imuse_t* im, ImWaveStream* stream, intptr_t soundId, int offset) {
	stream->soundId = soundId;
	stream->curOffset = offset;
	stream->endOffset = ImFiles_SeekSound(im, soundId, 0, 2 /* SEEK_END */);
	stream->paused = 0;
	/* Clear the fairness tracker — this slot now holds
	 * different data so the last-loaded preference is stale. */
	if (im->streamer.lastStreamLoaded == stream)
		im->streamer.lastStreamLoaded = 0;
}

/* ===== Per-tick producer ===== */

int ImStreamer_ProcessStreams(imuse_t* im) {
	ImWaveStream* s0 = &im->streamer.streams[0];
	ImWaveStream* s1 = &im->streamer.streams[1];

	int s0Live = s0->soundId && !s0->paused;
	int s1Live = s1->soundId && !s1->paused;

	/* One or zero active: refill the active one (if any). */
	if (!s0Live || !s1Live) {
		if (s0Live)
			streamer_fetchData(im, s0);
		else if (s1Live)
			streamer_fetchData(im, s1);
		return 0;
	}

	/* Both active: critical = below low-water. */
	int s0Critical = ImStreamer_GetAvailableBytes(im, s0) < s0->lowWaterMark;
	int s1Critical = ImStreamer_GetAvailableBytes(im, s1) < s1->lowWaterMark;

	if (!s0Critical || !s1Critical) {
		/* At most one is critical. If neither critical,
		 * alternate via lastStreamLoaded for fairness. */
		if (!s0Critical && (s1Critical || im->streamer.lastStreamLoaded != s0))
			streamer_fetchData(im, s1);
		else
			streamer_fetchData(im, s0);
		return 0;
	}

	/* Both critical: refill both this tick, ordered by
	 * lastStreamLoaded so we don't always favour slot 0. */
	if (im->streamer.lastStreamLoaded == s0) {
		streamer_fetchData(im, s0);
		streamer_fetchData(im, s1);
	} else {
		streamer_fetchData(im, s1);
		streamer_fetchData(im, s0);
	}
	return 0;
}

/* ===== Consumer reads ===== */

int ImStreamer_GetAvailableBytes(imuse_t* im, ImWaveStream* stream) {
	/* (loadIndex - readIndex) mod bufFreeSize. loadIndex is
	 * always at least 1 byte behind readIndex when the ring is
	 * "full" (see sentinel above), so the negative-wrap case
	 * never becomes the empty case by accident. */
	int loaded = stream->loadIndex - stream->readIndex;
	if (loaded < 0)
		loaded += stream->bufFreeSize;
	return loaded;
}

uint8_t* ImStreamer_GetStreamBuffer(imuse_t* im, ImWaveStream* stream, unsigned int size) {
	if ((unsigned int)ImStreamer_GetAvailableBytes(im, stream) < size || size > (unsigned int)stream->maxRead)
		return 0;

	/* If the requested window straddles the wrap, mirror the
	 * head-of-ring overlap into the shadow zone so the caller
	 * gets a linear pointer. */
	int tailSpace = stream->bufFreeSize - stream->readIndex;
	if ((unsigned int)tailSpace < size) {
		memcpy(&stream->buf[stream->bufFreeSize], stream->buf, size - (unsigned int)tailSpace);
	}

	uint8_t* readPtr = &stream->buf[stream->readIndex];
	stream->readIndex += (int)size;
	if (stream->readIndex >= stream->bufFreeSize)
		stream->readIndex -= stream->bufFreeSize;
	return readPtr;
}

uint8_t* ImStreamer_PeekAt(imuse_t* im, ImWaveStream* stream, int offset, unsigned int size) {
	/* Peek window must fit both in what's loaded (offset..offset+size
	 * still within available) and in the shadow zone (size <=
	 * maxRead so the wrap copy below cannot overflow). */
	if ((unsigned int)(ImStreamer_GetAvailableBytes(im, stream) - offset) < size ||
		size > (unsigned int)stream->maxRead)
		return 0;

	/* Wrap the starting index. */
	unsigned int idx = (unsigned int)offset + (unsigned int)stream->readIndex;
	if (idx >= (unsigned int)stream->bufFreeSize)
		idx -= (unsigned int)stream->bufFreeSize;

	unsigned int tailSpace = (unsigned int)stream->bufFreeSize - idx;
	if (tailSpace < size) {
		memcpy(&stream->buf[stream->bufFreeSize], stream->buf, size - tailSpace);
	}
	return &stream->buf[idx];
}

int ImStreamer_SetReadIndex(imuse_t* im, ImWaveStream* stream, unsigned int offset) {
	if ((unsigned int)ImStreamer_GetAvailableBytes(im, stream) < offset)
		return -1;
	stream->readIndex += (int)offset;
	if (stream->readIndex >= stream->bufFreeSize)
		stream->readIndex -= stream->bufFreeSize;
	return 0;
}

int ImStreamer_SetLoadIndex(imuse_t* im, ImWaveStream* stream, unsigned int offset) {
	/* Interpreted as "set loadIndex to readIndex + offset (mod
	 * ring)". Only VOCLoopCallback uses this to step one byte
	 * past the VOC type-7 marker. */
	if ((unsigned int)ImStreamer_GetAvailableBytes(im, stream) < offset)
		return -1;
	stream->loadIndex = (int)offset + stream->readIndex;
	if (stream->loadIndex >= stream->bufFreeSize)
		stream->loadIndex -= stream->bufFreeSize;
	return 0;
}

/* ===== VOC loop triggers ===== */

void ImStreamer_SetLoopFlag(imuse_t* im, ImWaveStream* stream, int offset) {
	stream->vocLoopFlag = 1;
	stream->vocLoopTriggerOffset = offset;
}

void ImStreamer_RemoveLoopFlag(imuse_t* im, ImWaveStream* stream) { stream->vocLoopFlag = 0; }

/* ===== IMUSE_CMD_QUERY_STREAM reporter ===== */

void ImStreamer_QueryStream(imuse_t* im, ImWaveStream* stream, int* bufSize, int* lowWaterMark,
							int* available) {
	*bufSize = stream->bufFreeSize;
	/* Paused streams can't starve, so report no effective
	 * low-water threshold — the refill scheduler will skip
	 * them regardless of what the underlying value is. */
	*lowWaterMark = stream->paused ? 0 : stream->lowWaterMark;
	*available = ImStreamer_GetAvailableBytes(im, stream);
}

/* ===== Producer ===== */

static void streamer_fetchData(imuse_t* im, ImWaveStream* stream) {
	/* Step 1: free space between loadIndex (producer) and
	 * readIndex (consumer), mod ring. One sentinel byte
	 * distinguishes "full" from "empty": loadIndex never
	 * catches up to readIndex from behind. */
	int freeToWrite = stream->readIndex - stream->loadIndex;
	if (freeToWrite <= 0)
		freeToWrite += stream->bufFreeSize;
	unsigned int freeMinusSentinel = (unsigned int)(freeToWrite - 1);

	/* Step 2: clip the per-tick target. */
	int loadTarget =
		((unsigned int)stream->loadSize >= freeMinusSentinel) ? (int)freeMinusSentinel : stream->loadSize;

	/* Step 3: clip against end-of-source. If the source is
	 * already exhausted, pause the stream; the loop below will
	 * no-op (remaining <= 0). */
	int streamRemaining = stream->endOffset - stream->curOffset;
	int remaining = (loadTarget >= streamRemaining) ? streamRemaining : loadTarget;
	if (streamRemaining <= 0)
		stream->paused = 1;

	/* Step 4: break into linear chunks bounded by the ring
	 * tail so one read call never has to wrap. */
	while (remaining > 0) {
		int tailSpace = stream->bufFreeSize - stream->loadIndex;
		int chunkSize = (remaining >= tailSpace) ? tailSpace : remaining;

		if (ImFiles_SeekSound(im, stream->soundId, stream->curOffset, 0 /* SEEK_SET */) !=
			stream->curOffset) {
			ImDebug_LogMsg(im, "ERR: invalid seek...");
			stream->paused = 1;
			return;
		}

		int bytesRead = ImFiles_ReadSound(im, stream->soundId, &stream->buf[stream->loadIndex], chunkSize);
		remaining -= bytesRead;
		stream->curOffset += bytesRead;
		im->streamer.lastStreamLoaded = stream;
		stream->loadIndex += bytesRead;
		if (stream->loadIndex >= stream->bufFreeSize)
			stream->loadIndex -= stream->bufFreeSize;

		/* VOC loop-end trigger: DISPATCH arranged for a
		 * callback when the source cursor crosses a specific
		 * offset. Fire once and disarm. */
		if (stream->vocLoopFlag &&
			(unsigned int)stream->curOffset >= (unsigned int)stream->vocLoopTriggerOffset) {
			ImDispatch_VOCLoopCallback(im, stream->soundId);
			stream->vocLoopFlag = 0;
		}

		if (bytesRead != chunkSize) {
			ImDebug_LogMsg(im, "ERR: unable to load correct amount (req=%lu,act=%lu)...",
						   (unsigned long)chunkSize, (unsigned long)bytesRead);
			im->streamer.lastStreamLoaded = 0;
			return;
		}
	}
}
