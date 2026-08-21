#include "internal/dispatch.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/files.h"
#include "internal/mixer.h"
#include "internal/streamer.h"
#include "internal/tracks.h"
#include "internal/triggers.h"

#include <imuse/commands.h>

#include <string.h>

/* ===== Module-private state ===== */

/* 16-entry dispatch pool paired 1:1 with TRACKS. */

/* 48-byte staging for VOC chunk header parsing. Shared across
 * SeekToNextChunk calls (it's owned by this module, not any
 * one dispatch). */

/* Frame-state struct passed to ImMixer_audioProcessFrame.
 * Module-global for the same reason — reused across the
 * two phases of PlaySoundFrame (fade and main). */

/* ===== Accessors ===== */

ImWaveDispatch* ImDispatch_GetById(imuse_t* im, int id) {
	/* Trivial indexed accessor — no bounds check in the binary. */
	return &im->dispatch.pool[id];
}

/* ===== Save / Restore ===== */

int ImDispatch_Save(imuse_t* im, void* buf, int size) {
	/* Raw memcpy of the 768-byte pool. Pointer fields inside
	 * (sound, streamPtr, fadeBuf) are saved as addresses that
	 * will be invalid on load — RestoreStreamers fixes them. */
	if ((unsigned int)size < sizeof(im->dispatch.pool))
		return -5;
	memcpy(buf, im->dispatch.pool, sizeof(im->dispatch.pool));
	return (int)sizeof(im->dispatch.pool);
}

int ImDispatch_Restore(imuse_t* im, void* buf) {
	/* Blind memcpy: caller has already verified there's enough
	 * tail in the save buffer. */
	memcpy(im->dispatch.pool, buf, sizeof(im->dispatch.pool));
	return (int)sizeof(im->dispatch.pool);
}

int ImDispatch_RestoreStreamers(imuse_t* im) {
	/* Post-Restore fixup. For each in-use slot:
	 *   1. Zero fadeBuf unconditionally (fades don't carry
	 *      across saves).
	 *   2. If it was streaming at save time, alloc a fresh
	 *      streamer and rewind to curOffset.
	 *   3. If an active VOC loop was armed, re-arm it at
	 *      curOffset + audioRemaining (next-block boundary). */
	int nTracks = ImTracks_GetWaveMixCount(im);
	for (int i = 0; i < nTracks; ++i) {
		ImWaveDispatch* d = &im->dispatch.pool[i];
		d->fadeBuf = 0;

		if (d->sound->soundId && d->streamPtr) {
			d->streamPtr = ImStreamer_AllocSound(im, d->sound->soundId, d->bufId, 0x800);
			if (!d->streamPtr)
				ImDebug_LogMsg(im, "ERR: unable to start stream during restore...");
			ImStreamer_SetSoundToStreamFromOffset(im, d->streamPtr, d->sound->soundId, d->curOffset);
			if (d->vocLoopStartingPoint)
				ImStreamer_SetLoopFlag(im, d->streamPtr, d->audioRemaining + d->curOffset);
		}
	}
	return 0;
}

/* ===== Per-track lifecycle ===== */

int ImDispatch_SetupSound(imuse_t* im, struct ImWaveTrack* sound, int bufId) {
	ImWaveDispatch* d = sound->data;
	d->curOffset = 0;
	d->audioRemaining = 0;
	d->vocLoopStartingPoint = 0;
	d->fadeBuf = 0;
	d->wholeLoopOffset = 0;
	d->sourceRateHz = 0;
	d->resamplePhase = 0;
	d->resampleStep = 0;

	if (bufId) {
		/* Streaming mode: alloc the streamer and defer the VOC
		 * header parse to the first PlaySoundFrame that finds
		 * audioRemaining == 0. */
		d->streamPtr = ImStreamer_AllocSound(im, sound->soundId, bufId, 0x800);
		d->bufId = bufId;
		return d->streamPtr ? 0 : -1;
	}

	/* In-memory mode: parse the header now. */
	d->streamPtr = 0;
	return ImDispatch_SeekToNextChunk(im, d);
}

void ImDispatch_Release(imuse_t* im, struct ImWaveTrack* sound) {
	/* Detach the streamer (if any). Does NOT zero the dispatch's
	 * other fields — SetupSound will re-init them next time the
	 * slot is allocated. */
	ImWaveDispatch* d = sound->data;
	if (d->streamPtr)
		ImStreamer_ClearSoundInStream(im, d->streamPtr);
}

/* ===== Per-frame mix ===== */

static void play_variable_rate_frame(imuse_t* im, ImWaveTrack* track, ImDigitalOutBuf* outDesc) {
	ImWaveDispatch* d = track->data;
	unsigned char* out = im->dispatch.resampleBuffer;
	int outputCount = outDesc->mixSampleCount;
	if (outputCount > (int)sizeof(im->dispatch.resampleBuffer))
		outputCount = (int)sizeof(im->dispatch.resampleBuffer);

	int generated = 0;
	int terminal = 0;
	while (generated < outputCount) {
		if (!d->audioRemaining) {
			terminal = ImDispatch_SeekToNextChunk(im, d);
			if (terminal)
				break;
		}

		unsigned char* soundBase = (unsigned char*)ImFiles_GetSoundPtr(im, track->soundId);
		if (!soundBase) {
			terminal = -1;
			break;
		}
		out[generated++] = soundBase[d->curOffset];

		uint64_t phase = (uint64_t)d->resamplePhase + d->resampleStep;
		uint32_t advance = (uint32_t)(phase >> 16);
		d->resamplePhase = (uint32_t)phase & 0xffffu;
		while (advance) {
			if (!d->audioRemaining) {
				terminal = ImDispatch_SeekToNextChunk(im, d);
				if (terminal)
					break;
			}
			uint32_t count = advance < (uint32_t)d->audioRemaining ? advance : (uint32_t)d->audioRemaining;
			d->curOffset += (int32_t)count;
			d->audioRemaining -= (int32_t)count;
			advance -= count;
		}
		if (terminal)
			break;
	}

	if (generated) {
		im->dispatch.mixFrameState.buffer = out;
		im->dispatch.mixFrameState.count = generated;
		im->dispatch.mixFrameState.sourceIsHighRate = 1;
		ImMixer_audioProcessFrame(im, &im->dispatch.mixFrameState, 0, track->effVol, track->pan);
	}
	if (terminal == -1)
		ImTracks_Clear(im, track);
}

void ImDispatch_PlaySoundFrame(imuse_t* im, struct ImWaveTrack* track, struct ImDigitalOutBuf* outDesc) {
	ImWaveDispatch* d = track->data;
	if (d->resampleStep && !d->streamPtr && !d->fadeBuf) {
		play_variable_rate_frame(im, track, outDesc);
		return;
	}

	/* Source-rate scaling. Half-rate sources deliver half as
	 * many input bytes per output frame because the mixer
	 * 2x-duplicates them. */
	int mixSampleCount = (d->sourceIsHighRate == 1) ? outDesc->mixSampleCount
													: (int)((unsigned int)outDesc->mixSampleCount >> 1);

	int remainingFade = 0;

	/* ===== Phase A: cross-fade drain =====
	 * Emit up to fadeRemaining bytes from the snapshot buffer
	 * attached by SwitchStream, at a ramp-down volume. */
	if (d->fadeBuf) {
		int fadeCount = ((unsigned int)mixSampleCount >= (unsigned int)d->fadeRemaining) ? d->fadeRemaining
																						 : mixSampleCount;
		remainingFade = fadeCount;

		im->dispatch.mixFrameState.count = fadeCount;
		im->dispatch.mixFrameState.buffer = (unsigned char*)(intptr_t)d->fadeBuf;
		im->dispatch.mixFrameState.sourceIsHighRate = d->sourceIsHighRate;

		int fadeVol = ImDispatch_UpdateFadeMixVolume(im, d, fadeCount);
		ImMixer_audioProcessFrame(im, &im->dispatch.mixFrameState, 0, fadeVol, track->pan);

		d->fadeRemaining -= fadeCount;
		d->fadeBuf += fadeCount;
		if (!d->fadeRemaining)
			d->fadeBuf = 0;
	}

	/* ===== Phase B: main mix loop ===== */
	int offset = 0;
	int samplesLeft = mixSampleCount;
	int chunkStatus = 0;

	for (;;) {
		/* Chunk advance at block boundary. Non-zero return
		 * means end-of-sound or malformed VOC — break out and
		 * let the tail teardown fire ImTracks_Clear. */
		if (!d->audioRemaining) {
			chunkStatus = ImDispatch_SeekToNextChunk(im, d);
			if (chunkStatus)
				break;
		}

		if (!samplesLeft)
			return;

		int audioRemaining =
			((unsigned int)samplesLeft >= (unsigned int)d->audioRemaining) ? d->audioRemaining : samplesLeft;
		int mixCount = audioRemaining;

		/* Source pointer acquisition. Streamed vs in-memory. */
		unsigned char* srcPtr;
		if (d->streamPtr) {
			srcPtr = ImStreamer_GetStreamBuffer(im, d->streamPtr, (unsigned int)audioRemaining);
			if (!srcPtr) {
				ImDebug_LogMsg(im, "ERR: no streamed audio...");
				if (d->fadeBuf)
					d->fadeSyncDelta += remainingFade;
				return;
			}
		} else {
			char* soundBase = (char*)ImFiles_GetSoundPtr(im, track->soundId);
			if (!soundBase) {
				ImDebug_LogMsg(im, "ERR: dispatch got NULL file addr...");
				return;
			}
			srcPtr = (unsigned char*)&soundBase[d->curOffset];
		}

		/* fadeSync compensation: skip source bytes that were
		 * consumed by the fade buffer but not by the new stream,
		 * so the two streams stay phase-aligned. */
		if (d->fadeBuf && d->fadeSyncDelta) {
			int skip = ((unsigned int)d->fadeSyncDelta >= (unsigned int)audioRemaining) ? audioRemaining
																						: d->fadeSyncDelta;
			d->fadeSyncDelta -= skip;
			mixCount = audioRemaining - skip;
			srcPtr += skip;
			d->curOffset += skip;
			d->audioRemaining -= skip;
		}

		if (mixCount) {
			im->dispatch.mixFrameState.count = mixCount;
			im->dispatch.mixFrameState.buffer = srcPtr;
			im->dispatch.mixFrameState.sourceIsHighRate = d->sourceIsHighRate;

			int mixVol = d->fadeBuf ? ImDispatch_UpdateFadeSlope(im, d) : track->effVol;
			ImMixer_audioProcessFrame(im, &im->dispatch.mixFrameState, offset, mixVol, track->pan);

			offset += mixCount;
			samplesLeft -= mixCount;
			d->curOffset += mixCount;
			d->audioRemaining -= mixCount;
		}
	}

	/* Terminal end: -1 is end-of-sound, other non-zero values
	 * are recoverable (e.g. -3 = stream starvation). Only -1
	 * tears down the track. Either way roll any remaining fade
	 * into fadeSyncDelta so the next invocation knows. */
	if (chunkStatus == -1)
		ImTracks_Clear(im, track);
	if (d->fadeBuf)
		d->fadeSyncDelta += remainingFade;
}

/* ===== VOC parser ===== */

int ImDispatch_SeekToNextChunk(imuse_t* im, ImWaveDispatch* d) {
	int wholeLoopRewinds = 0;
	for (;;) {
		/* Source acquisition: streamer peek (48 bytes, fallback
		 * to 1 byte), or in-memory memcpy. */
		if (d->streamPtr) {
			unsigned char* src = ImStreamer_PeekAt(im, d->streamPtr, 0, sizeof(im->dispatch.waveChunkData));
			if (!src) {
				src = ImStreamer_PeekAt(im, d->streamPtr, 0, 1);
				if (!src)
					return -3; /* streamer starved — try again next tick */
			}
			memcpy(im->dispatch.waveChunkData, src, sizeof(im->dispatch.waveChunkData));
		} else {
			char* soundBase = (char*)ImFiles_GetSoundPtr(im, d->sound->soundId);
			if (!soundBase) {
				ImDebug_LogMsg(im, "ERR: null sound addr in SeekToNextChunk...");
				return -1;
			}
			/* Read only what the dispatch below actually consumes.
			 * The DOS build did a blind 48-byte peek and relied on
			 * Watcom heap slack to absorb the over-read at end-of-
			 * sound (where the terminator chunk is the very last
			 * byte). Per-type sizing avoids that on tight allocators:
			 *   type 0       -> 1 byte (terminator, return -1)
			 *   types 1, 4   -> 6 bytes (audio header / marker payload)
			 *   types 6, 7   -> 1 byte (loop markers; no payload read)
			 *   types >= 8   -> 24 bytes ('Crea' header signature) */
			unsigned char chunkType = (unsigned char)soundBase[d->curOffset];
			im->dispatch.waveChunkData[0] = chunkType;
			int needed = 1;
			if (chunkType == 1 || chunkType == 4)
				needed = 6;
			else if (chunkType >= 8)
				needed = 24;
			if (needed > 1)
				memcpy(im->dispatch.waveChunkData + 1, &soundBase[d->curOffset] + 1, (size_t)(needed - 1));
		}

		unsigned char chunkType = im->dispatch.waveChunkData[0];

		if (chunkType == 0) {
			if ((d->sound->startFlags & IMUSE_WAVE_START_LOOP) && d->wholeLoopOffset > 0 &&
				!wholeLoopRewinds) {
				d->curOffset = d->wholeLoopOffset;
				wholeLoopRewinds = 1;
				continue;
			}
			return -1; /* terminator */
		}
		if (chunkType == 1) {
			/* Some host loaders create their playback buffer from only the
			 * first VOC audio block. This opt-in path reproduces that behavior
			 * without modifying the shared source asset. */
			if ((d->sound->startFlags & IMUSE_WAVE_START_LOOP_FIRST_BLOCK) && d->wholeLoopOffset > 0 &&
				d->curOffset != d->wholeLoopOffset) {
				d->curOffset = d->wholeLoopOffset;
				continue;
			}
			break; /* audio block — fall through to parser */
		}

		/* Non-audio chunks (marker/loop/file-header) are
		 * consumed and we keep looping. */
		if (chunkType == 4) {
			/* Marker: 16-bit payload at offset 4. */
			int16_t markerValue = *(int16_t*)&im->dispatch.waveChunkData[4];
			ImTriggers_ProcessMarker(im, d->sound->soundId, markerValue);
			d->curOffset += 6;
		} else if (chunkType == 6) {
			/* Loop start: record position, advance. */
			d->vocLoopStartingPoint = d->curOffset;
			d->curOffset += 6;
			if (d->streamPtr)
				ImStreamer_GetStreamBuffer(im, d->streamPtr, 6);
		} else if (chunkType == 7) {
			/* Loop end: rewind to loop start. Streamer advances
			 * 1 byte past the type so its cursor doesn't re-enter
			 * this block on the next pass. */
			d->curOffset = d->vocLoopStartingPoint;
			if (d->streamPtr)
				ImStreamer_GetStreamBuffer(im, d->streamPtr, 1);
		} else if (chunkType >= 8 && im->dispatch.waveChunkData[0] == 'C' &&
				   im->dispatch.waveChunkData[1] == 'r' && im->dispatch.waveChunkData[2] == 'e' &&
				   im->dispatch.waveChunkData[3] == 'a' && im->dispatch.waveChunkData[20] == 0x1A &&
				   im->dispatch.waveChunkData[21] == 0 && im->dispatch.waveChunkData[22] == 0x0A &&
				   im->dispatch.waveChunkData[23] == 0x01) {
			/* VOC file header: 26-byte "Creative Voice File\x1A"
			 * preamble + version fields. Skip and keep parsing. */
			d->curOffset += 26;
			if (d->streamPtr)
				ImStreamer_GetStreamBuffer(im, d->streamPtr, 26);
		} else {
			ImDebug_LogMsg(im, "ERR: Illegal chunk in sound %lu...", (unsigned long)d->sound->soundId);
			return -1;
		}
	}

	/* Type 1 (audio block) parser. Size is a 24-bit little-
	 * endian value at bytes [1..3] INCLUDING the 2-byte config
	 * field at [4..5]. audioRemaining excludes the config. */
	int audioSize = ((int)im->dispatch.waveChunkData[3] << 16) | ((int)im->dispatch.waveChunkData[2] << 8) |
					(int)im->dispatch.waveChunkData[1];
	if (audioSize <= 2)
		return -1;
	if ((d->sound->startFlags & IMUSE_WAVE_START_LOOP) && !d->wholeLoopOffset)
		d->wholeLoopOffset = d->curOffset;

	/* VOC sample-rate divisor in byte 4 encodes
	 *   sampleRate = 1,000,000 / (256 - divisor).
	 * 0xC4 maps to ~16.7 kHz; at > 0xC4 the source is already
	 * at output rate (no upsampling). */
	uint32_t divisor = im->dispatch.waveChunkData[4];
	d->sourceRateHz = 1000000u / (256u - divisor);
	d->sourceIsHighRate = divisor > 0xC4;
	d->audioRemaining = audioSize - 2;
	d->curOffset += 6;

	if (d->streamPtr) {
		ImStreamer_GetStreamBuffer(im, d->streamPtr, 6);
		if (d->vocLoopStartingPoint)
			ImStreamer_SetLoopFlag(im, d->streamPtr, d->audioRemaining + d->curOffset);
	}
	return 0;
}

/* ===== VOC loop-end callback (streamer fetch) ===== */

void ImDispatch_VOCLoopCallback(imuse_t* im, intptr_t soundId) {
	/* Fired by ImStreamer_FetchData when the read cursor
	 * reaches a SetLoopFlag-armed offset. Find the dispatch
	 * owning soundId; if the next chunk header is a type-7
	 * loop-end, rewind the streamer cursor to the loop start. */
	int nTracks = ImTracks_GetWaveMixCount(im);
	ImWaveDispatch* d = im->dispatch.pool;
	int i = 0;
	for (; i < nTracks; ++i, ++d) {
		if (soundId && d->sound->soundId == soundId)
			break;
	}
	if (i >= nTracks) {
		ImDebug_LogMsg(im, "ERR: bogus callback in dispatch.c...");
		return;
	}

	unsigned char* blockHead = ImStreamer_PeekAt(im, d->streamPtr, d->audioRemaining, 1);
	if (!blockHead) {
		ImDebug_LogMsg(im, "ERR: view err in loop callback...");
		return;
	}
	if (*blockHead == 7) {
		/* Step past the type byte and seek the stream to the
		 * loop-start offset; next fetch resumes the loop body. */
		ImStreamer_SetLoadIndex(im, d->streamPtr, d->audioRemaining + 1);
		ImStreamer_SetSoundToStreamFromOffset(im, d->streamPtr, d->sound->soundId, d->vocLoopStartingPoint);
	}
}

/* ===== SwitchStream ===== */

int ImDispatch_SwitchStream(imuse_t* im, intptr_t oldSoundId, intptr_t newSoundId, void* crossFadeBuffer,
							int crossFadeBufferSize, int vocLoopFlag) {
	/* Locate the dispatch currently playing oldSoundId. */
	int nTracks = ImTracks_GetWaveMixCount(im);
	ImWaveDispatch* d = im->dispatch.pool;
	int i = 0;
	for (; i < nTracks; ++i, ++d) {
		if (oldSoundId && d->sound->soundId == oldSoundId && d->streamPtr)
			break;
	}
	if (i >= nTracks)
		return -1;

	/* Snapshot the OLD position — used later when vocLoopFlag
	 * is set, so the NEW stream resumes where the old one was. */
	int savedCurOffset = d->curOffset;
	int savedAudioRemaining = d->audioRemaining;

	/* Relabel + attach the crossfade scratch. fadeVol = 0x7F0000
	 * means "100% old sound" at start; fadeSlope seeded on the
	 * first UpdateFadeSlope call. */
	d->sound->soundId = newSoundId;
	d->fadeBuf = (int)(intptr_t)crossFadeBuffer;
	d->fadeRemaining = 0;
	d->fadeSyncDelta = 0;
	d->fadeVol = 0x7F0000;
	d->fadeSlope = 0;

	/* Drain loop: fill the crossfade scratch with up to
	 * crossFadeBufferSize bytes from the old stream's ring
	 * buffer, 2 KB at a time, advancing curOffset and
	 * shrinking audioRemaining as we go. */
	while ((unsigned int)d->fadeRemaining < (unsigned int)crossFadeBufferSize &&
		   ImStreamer_GetAvailableBytes(im, d->streamPtr) &&
		   (d->audioRemaining || !ImDispatch_SeekToNextChunk(im, d))) {

		int cap = (crossFadeBufferSize - d->fadeRemaining) <= d->audioRemaining
					  ? (crossFadeBufferSize - d->fadeRemaining)
					  : d->audioRemaining;
		int avail = ImStreamer_GetAvailableBytes(im, d->streamPtr);
		int chunk = (avail > cap) ? cap : avail;
		if (chunk > 2048)
			chunk = 2048;

		unsigned char* src = ImStreamer_GetStreamBuffer(im, d->streamPtr, (unsigned int)chunk);
		memcpy((char*)crossFadeBuffer + d->fadeRemaining, src, (size_t)chunk);
		d->fadeRemaining += chunk;
		d->curOffset += chunk;
		d->audioRemaining -= chunk;
	}

	/* Retire any remaining old-stream bytes from the ring. */
	int freeAfterFill = ImStreamer_GetAvailableBytes(im, d->streamPtr);
	ImStreamer_SetReadIndex(im, d->streamPtr, (unsigned int)freeAfterFill);

	/* Re-seek the streamer onto the NEW sound. vocLoopFlag
	 * selects between "resume at saved offset" (seamless loop)
	 * and "fresh start from offset 0". */
	ImStreamer_SetSoundToStreamFromOffset(im, d->streamPtr, newSoundId, vocLoopFlag ? savedCurOffset : 0);

	if (vocLoopFlag) {
		if (d->vocLoopStartingPoint)
			ImStreamer_SetLoopFlag(im, d->streamPtr, d->audioRemaining + d->curOffset);
	} else {
		ImStreamer_RemoveLoopFlag(im, d->streamPtr);
	}

	/* Restore parse cursor for vocLoopFlag path; reset for
	 * fresh-start path. */
	d->curOffset = vocLoopFlag ? savedCurOffset : 0;
	d->audioRemaining = vocLoopFlag ? savedAudioRemaining : 0;
	d->vocLoopStartingPoint = vocLoopFlag ? d->vocLoopStartingPoint : 0;
	return 0;
}

/* ===== Fade-volume updates ===== */

int ImDispatch_UpdateFadeSlope(imuse_t* im, ImWaveDispatch* d) {
	/* Fade-IN volume for the NEW stream this mix iteration.
	 * fadeVol is an 8.16 fixed-point counter: high byte = how
	 * much "old sound" remains; (127 - high_byte) = matching
	 * "new sound" ratio. */
	int fadeInVol = (d->sound->effVol * (127 - (d->fadeVol >> 16) + 1)) >> 7;

	/* First-call init: compute fadeSlope so fadeVol reaches 0
	 * by the end of the crossfade. Clamp span to ≥2 to avoid
	 * /0 and /1 over-decrements. */
	if (!d->fadeSlope) {
		unsigned int span = ((unsigned int)d->fadeRemaining <= 1u) ? 2u : (unsigned int)d->fadeRemaining;
		d->fadeSlope = -(int)(0x7F0000u / span);
	}
	return fadeInVol;
}

int ImDispatch_UpdateFadeMixVolume(imuse_t* im, ImWaveDispatch* d, int remainingFade) {
	/* Fade-OUT volume for the OLD stream this iteration. */
	int fadeOutVol = (d->sound->effVol * ((d->fadeVol >> 16) + 1)) >> 7;

	/* Advance fadeVol by (remainingFade * fadeSlope); fadeSlope
	 * is negative, so this ramps toward 0. Clamp to [0, 0x7F0000]. */
	d->fadeVol += remainingFade * d->fadeSlope;
	if (d->fadeVol < 0)
		d->fadeVol = 0;
	if (d->fadeVol > 0x7F0000)
		d->fadeVol = 0x7F0000;
	return fadeOutVol;
}
