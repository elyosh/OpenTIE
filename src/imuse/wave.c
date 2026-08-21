#include "internal/wave.h"
#include "internal/state.h"

#include "internal/dispatch.h"
#include "internal/files.h"
#include "internal/streamer.h"
#include "internal/tracks.h"

/* Locking prevents callbacks from recursively entering ImWave_Update. TRACKS
 * owns pause state. */

/* Reentrancy counter. Lock increments, Unlock decrements (floored
 * at 0). Update() skips when non-zero. */

/* ===== Lifecycle ===== */

int ImWave_Init(imuse_t* im) {
	if (ImTracks_Init(im) != 0)
		return -1;
	im->wave.wvSlicingHalted = 0;
	return 0;
}

int ImWave_Deinit(imuse_t* im) { return ImTracks_Deinit(im); }

/* ===== Tick + reentrancy lock ===== */

void ImWave_Lock(imuse_t* im) { ++im->wave.wvSlicingHalted; }

void ImWave_Unlock(imuse_t* im) {
	if (im->wave.wvSlicingHalted)
		--im->wave.wvSlicingHalted;
}

void ImWave_Update(imuse_t* im) {
	if (!im->wave.wvSlicingHalted)
		ImTracks_Update(im);
}

/* ===== Pause / Resume ===== */

int ImWave_Pause(imuse_t* im) {
	ImWave_Lock(im);
	ImTracks_Pause(im);
	ImWave_Unlock(im);
	return 0;
}

int ImWave_Resume(imuse_t* im) {
	ImWave_Lock(im);
	ImTracks_Resume(im);
	ImWave_Unlock(im);
	return 0;
}

/* ===== Save / Restore ===== */

int ImWave_Save(imuse_t* im, void* buf, int size) {
	ImWave_Lock(im);
	int written = ImTracks_Save(im, buf, size);
	ImWave_Unlock(im);
	return written;
}

int ImWave_Restore(imuse_t* im, void* buf) {
	ImWave_Lock(im);
	int consumed = ImTracks_Restore(im, buf);
	ImWave_Unlock(im);
	return consumed;
}

/* ===== Group-volume propagation ===== */

void ImWave_ApplyGroupVol(imuse_t* im) {
	ImWave_Lock(im);
	ImTracks_ApplyGroupVol(im);
	ImWave_Unlock(im);
}

/* ===== Sound control ===== */

int ImWave_StartSound(imuse_t* im, intptr_t soundId, int priority, uint32_t startFlags) {
	ImWave_Lock(im);
	int r = ImTracks_StartSound(im, soundId, priority, 0, startFlags);
	ImWave_Unlock(im);
	return r;
}

int ImWave_StopSoundById(imuse_t* im, intptr_t soundId) {
	ImWave_Lock(im);
	int r = ImTracks_StopSoundById(im, soundId);
	ImWave_Unlock(im);
	return r;
}

int ImWave_StopAllSounds(imuse_t* im) {
	ImWave_Lock(im);
	int r = ImTracks_StopAllSounds(im);
	ImWave_Unlock(im);
	return r;
}

intptr_t ImWave_GetNextSound(imuse_t* im, intptr_t soundId) {
	ImWave_Lock(im);
	intptr_t r = ImTracks_GetNextSound(im, soundId);
	ImWave_Unlock(im);
	return r;
}

int ImWave_SetParam(imuse_t* im, intptr_t soundId, int param, int value) {
	ImWave_Lock(im);
	int r = ImTracks_SetParam(im, soundId, param, value);
	ImWave_Unlock(im);
	return r;
}

int ImWave_GetParam(imuse_t* im, intptr_t soundId, int param) {
	ImWave_Lock(im);
	int r = ImTracks_GetParam(im, soundId, param);
	ImWave_Unlock(im);
	return r;
}

/* Hooks: -2 = unsupported on the wave side. Args ignored. */
void ImWave_SetHook(imuse_t* im, intptr_t soundId, uint32_t hookId) {
	(void)soundId;
	(void)hookId;
}

int ImWave_GetHook(imuse_t* im, intptr_t soundId) {
	(void)soundId;
	return -2;
}

/* ===== Streaming ===== */

int ImWave_StartStream(imuse_t* im, intptr_t soundId, int priority, int bufId) {
	/* Validity check before the lock — matches the DOS ordering and
	 * avoids spurious lock churn on the rejected path. */
	if (!ImFiles_IsSoundIdValid(im, soundId))
		return -1;
	ImWave_Lock(im);
	int r = ImTracks_StartSound(im, soundId, priority, bufId, 0);
	ImWave_Unlock(im);
	return r;
}

int ImWave_SwitchStream(imuse_t* im, intptr_t oldSoundId, intptr_t newSoundId, void* crossFadeBuffer,
						int crossFadeBufferSize, int vocLoopFlag) {
	ImWave_Lock(im);
	int r = ImDispatch_SwitchStream(im, oldSoundId, newSoundId, crossFadeBuffer, crossFadeBufferSize,
									vocLoopFlag);
	ImWave_Unlock(im);
	return r;
}

int ImWave_ProcessStreams(imuse_t* im) {
	/* Not bracketed by Lock/Unlock in the DOS build — ImStreamer owns
	 * its own fencing. Preserved verbatim. */
	return ImStreamer_ProcessStreams(im);
}

intptr_t ImWave_QueryNextStream(imuse_t* im, intptr_t soundId, int* bufSize, int* lowWaterMark,
								int* available) {
	ImWave_Lock(im);
	intptr_t r = ImTracks_QueryNextStream(im, soundId, bufSize, lowWaterMark, available);
	ImWave_Unlock(im);
	return r;
}
