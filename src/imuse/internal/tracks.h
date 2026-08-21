#ifndef __IMUSE_TRACKS_H__
#define __IMUSE_TRACKS_H__

#include <stdint.h>

#include <imuse/handle.h>

/*
 * iMUSE engine -- TRACKS module.
 *
 * Owns the tracks[16] pool that represents live digital-audio
 * voices and drives the per-tick mix/write pump.  Each track
 * carries a paired ImWaveDispatch (in-memory VOC cursor or
 * streamer hookup).  Tracks are selected by priority on Start;
 * when the pool is full, a lower-priority live track can be
 * preempted.
 *
 * The pool-max is ImuseConfig::waveMixCount (clamped to 1..16,
 * defaulting to 4).  Only the first waveMixCount entries are
 * ever allocated — the tail of the array is reserved but
 * unused.
 */

struct ImWaveDispatch;
struct ImuseStreamBuffer;

/* 60-byte live-voice record.  prev/next at offset 0/4 match the
 * ImUtils list convention. `data` points at the paired
 * ImWaveDispatch (pinned 1:1 at Init).  detuneTrans caches
 * (transpose<<8) + detune so fades can push it to the dispatch
 * in one field. */
typedef struct ImWaveTrack {
	struct ImWaveTrack* prev;
	struct ImWaveTrack* next;
	struct ImWaveDispatch* data; /* paired dispatch slot */
	intptr_t soundId;            /* 0 = slot free */
	int32_t marker;
	int32_t group;
	int32_t priority;    /* 0..127 */
	int32_t vol;         /* 0..127 */
	int32_t effVol;      /* cached: (groupVol * (vol+1)) >> 7 */
	int32_t pan;         /* 0..127, centre = 64 */
	int32_t detune;      /* 8.8 fixed-point semitones */
	int32_t transpose;   /* whole semitones, ±12 */
	int32_t detuneTrans; /* (transpose<<8) + detune */
	int32_t mailbox;     /* host scratchpad */
	int32_t jumpHook;    /* hook id for IMUSE_CMD_SET_HOOK */
	uint32_t startFlags; /* ImuseWaveStartFlags captured before dispatch setup */
	int32_t frequencyHz; /* explicit absolute playback rate; 0 = asset rate */
} ImWaveTrack;

/* ===== Lifecycle ===== */

int ImTracks_Init(imuse_t* im);
int ImTracks_Deinit(imuse_t* im);

/* Per-tick pump. Acquires an output frame, mixes every live
 * track through its dispatch, writes the result to the driver. */
void ImTracks_Update(imuse_t* im);

/* Freeze/unfreeze the mixer.  Pause is a counter: the first two
 * post-Pause ticks still run so the driver's pipeline drains,
 * then the mixer stops feeding frames. */
void ImTracks_Pause(imuse_t* im);
void ImTracks_Resume(imuse_t* im);

int ImTracks_Save(imuse_t* im, void* buf, int size);
int ImTracks_Restore(imuse_t* im, void* buf);

/* Re-scale every live track's effVol after a group-volume
 * change.  Called from imuse_set_group_volume via ImWave_ApplyGroupVol. */
void ImTracks_ApplyGroupVol(imuse_t* im);

/* ===== Sound control ===== */

int ImTracks_StartSound(imuse_t* im, intptr_t soundId, int priority, int bufId, uint32_t startFlags);
int ImTracks_StopSoundById(imuse_t* im, intptr_t soundId);
int ImTracks_StopAllSounds(imuse_t* im);
intptr_t ImTracks_GetNextSound(imuse_t* im, intptr_t soundId);

/* Iterator over streaming wave sounds. For each hit, fills the
 * bufSize, lowWaterMark, and available out-params via
 * ImStreamer_QueryStream and returns the next streaming soundId,
 * or 0 at end. */
intptr_t ImTracks_QueryNextStream(imuse_t* im, intptr_t soundId, int* bufSize, int* lowWaterMark,
								  int* available);

/* Tear down a live track: unlink, release dispatch, cancel
 * fades + triggers for its soundId, mark the slot free.
 * Exposed because ImDispatch_PlaySoundFrame calls back into it
 * on end-of-buffer. */
void ImTracks_Clear(imuse_t* im, ImWaveTrack* track);

int ImTracks_SetParam(imuse_t* im, intptr_t soundId, int param, int value);
int ImTracks_GetParam(imuse_t* im, intptr_t soundId, int param);

/* Allocate a free slot, preempting a lower-priority live track
 * if the pool is full. Equal-priority ties preempt (new sound
 * wins over running one). Sole caller: ImTracks_StartSound. */
ImWaveTrack* ImTracks_GetFreeTrack(imuse_t* im, int priority);

/* Accessor for DISPATCH: the pool walker in RestoreStreamers /
 * SwitchStream / VOCLoopCallback iterates [0, waveMixCount). */
int ImTracks_GetWaveMixCount(imuse_t* im);

#endif /* __IMUSE_TRACKS_H__ */
