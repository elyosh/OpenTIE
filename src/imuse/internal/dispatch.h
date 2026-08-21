#ifndef __IMUSE_DISPATCH_H__
#define __IMUSE_DISPATCH_H__

#include <stdint.h>

#include <imuse/handle.h>

/*
 * iMUSE engine -- DISPATCH module.
 *
 * Per-track VOC cursor + streamer hookup + crossfade state.
 * TRACKS pairs its 16 slots 1:1 with DISPATCH records at Init
 * and drives DISPATCH through three hot paths:
 *
 *   - ImDispatch_SetupSound / _Release on Start/Stop
 *   - ImDispatch_PlaySoundFrame on each Update tick
 *   - ImDispatch_SwitchStream on opcode 26 (IMUSE_CMD_SWITCH_STREAM)
 *
 * The per-tick mix path parses VOC block headers inline (via
 * ImDispatch_SeekToNextChunk) and uses the MIXER's frame-state
 * struct to hand each block to ImMixer_audioProcessFrame.
 */

struct ImWaveTrack;
struct ImWaveStream;
struct ImDigitalOutBuf;

/* 48-byte per-track cursor + fade state. */
typedef struct ImWaveDispatch {
	struct ImWaveTrack* sound;      /* back-pointer */
	int32_t sourceIsHighRate;       /* VOC byte-4 > 0xC4 */
	int32_t curOffset;              /* byte cursor */
	int32_t audioRemaining;         /* bytes left in current block */
	int32_t vocLoopStartingPoint;   /* loop re-seek target */
	struct ImWaveStream* streamPtr; /* non-NULL = streaming */
	int32_t bufId;                  /* ImuseStreamBuffer slot */
	int32_t fadeBuf;                /* crossfade scratch (pointer cast) */
	int32_t fadeRemaining;          /* bytes left in the crossfade */
	int32_t fadeSyncDelta;          /* resync skip on stream switch */
	int32_t fadeVol;                /* 8.16 fixed-point fade counter */
	int32_t fadeSlope;              /* per-sample decrement (negative) */
	int32_t wholeLoopOffset;        /* first type-1 header, or 0 when disabled */
	uint32_t sourceRateHz;          /* exact rate decoded from the VOC divisor */
	uint32_t resamplePhase;         /* 16.16 source position fraction */
	uint32_t resampleStep;          /* 16.16 source samples per mix sample */
} ImWaveDispatch;

/* Mapping: dispatch[id] is paired with tracks[id] at TRACKS Init
 * and Restore.  Never re-paired at runtime. */
ImWaveDispatch* ImDispatch_GetById(imuse_t* im, int id);

/* Per-track lifecycle. SetupSound either parses the VOC header
 * (bufId == 0, in-memory mode) or allocates a streamer slot
 * (bufId != 0). Release detaches the streamer on teardown. */
int ImDispatch_SetupSound(imuse_t* im, struct ImWaveTrack* track, int bufId);
void ImDispatch_Release(imuse_t* im, struct ImWaveTrack* track);

/* Per-tick advance: mix one frame of this track into outDesc.
 * May call back into ImTracks_Clear on end-of-sound. */
void ImDispatch_PlaySoundFrame(imuse_t* im, struct ImWaveTrack* track, struct ImDigitalOutBuf* outDesc);

/* Parse the VOC chunk header at the dispatch's current source
 * position. Updates dispatch state for the next audio block or
 * handles marker/loop/EOF. Called on audioRemaining==0. */
int ImDispatch_SeekToNextChunk(imuse_t* im, ImWaveDispatch* dispatch);

/* Streamer end-of-block loop trigger callback. Fires from
 * ImStreamer_FetchData via a function pointer armed by
 * ImStreamer_SetLoopFlag. */
void ImDispatch_VOCLoopCallback(imuse_t* im, intptr_t soundId);

/* Save/Restore of the dispatch slab. Save returns bytes
 * written (or -5 if the buffer is too small); Restore returns
 * bytes consumed. */
int ImDispatch_Save(imuse_t* im, void* buf, int size);
int ImDispatch_Restore(imuse_t* im, void* buf);

/* Post-Restore fixup: re-bind the streamer records that survived
 * the save to their dispatch slots. */
int ImDispatch_RestoreStreamers(imuse_t* im);

/* Stream crossfade — called by ImWave_SwitchStream. */
int ImDispatch_SwitchStream(imuse_t* im, intptr_t oldSoundId, intptr_t newSoundId, void* crossFadeBuffer,
							int crossFadeBufferSize, int vocLoopFlag);

/* Fade-volume updates used by PlaySoundFrame. UpdateFadeSlope
 * returns the fade-IN volume for the new stream (and seeds
 * fadeSlope on first call). UpdateFadeMixVolume returns the
 * fade-OUT volume for the old stream and advances fadeVol by
 * (remainingFade * fadeSlope). */
int ImDispatch_UpdateFadeSlope(imuse_t* im, ImWaveDispatch* dispatch);
int ImDispatch_UpdateFadeMixVolume(imuse_t* im, ImWaveDispatch* dispatch, int remainingFade);

#endif /* __IMUSE_DISPATCH_H__ */
