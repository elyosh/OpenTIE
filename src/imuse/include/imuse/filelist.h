#ifndef __IMUSE_FILELIST_H__
#define __IMUSE_FILELIST_H__

#include <stdint.h>

#include <imuse/handle.h>

/*
 * libimuse — name-keyed sound-handle reference counter.
 *
 * Hosts that load sounds by name (rather than by integer id) can
 * use the filelist to share a single handle across multiple
 * imuse_start_sound calls. The four runtime callbacks
 * (load/unload/open/close) are rebindable so a host can switch I/O
 * conventions on the fly (TIE swaps load/unload between front-end
 * and flight modes).
 *
 * Streaming-buffer descriptor returned by host->getBufInfoFunc.
 * `buffer` is the host-owned ring base; the engine reads
 * `bufSize` / `loadSize` / `lowWaterMark` to schedule refills. */
typedef struct ImuseStreamBuffer {
	void* buffer;
	int32_t bufSize;
	int32_t loadSize;
	int32_t lowWaterMark;
} ImuseStreamBuffer;

/* ===== Sound-type tags (returned by ImFiles_GetSoundType internally
 * — exposed here because some host code key off them). */
#define IMUSE_SOUND_TYPE_INVALID (-1)
#define IMUSE_SOUND_TYPE_MIDI 1 /* 'MIDI' container */
#define IMUSE_SOUND_TYPE_WAVE 2 /* 'Crea' VOC container */

/* ===== Runtime callback types =====
 *
 * load/unload — for full-load resources (the engine memcpy's the
 *               whole buffer at start time).
 * open/close  — for streamed resources (the engine pulls bytes via
 *               the host's seekFunc/readFunc on demand). */
typedef void* (*ImuseLoadSoundFunc)(const char* name);
typedef void (*ImuseUnloadSoundFunc)(void* handle);
typedef void* (*ImuseOpenSoundFunc)(const char* name);
typedef void (*ImuseCloseSoundFunc)(void* handle);

int imuse_filelist_init(imuse_t* im, ImuseLoadSoundFunc loadFunc, ImuseUnloadSoundFunc unloadFunc,
						ImuseOpenSoundFunc openFunc, ImuseCloseSoundFunc closeFunc);
void* imuse_filelist_load(imuse_t* im, const char* name);
void imuse_filelist_unload(imuse_t* im, void* handle);
void imuse_filelist_unload_all(imuse_t* im);
void* imuse_filelist_open(imuse_t* im, const char* name);
void imuse_filelist_close(imuse_t* im, void* handle);
void imuse_filelist_close_all(imuse_t* im);
void* imuse_filelist_find(imuse_t* im, const char* name);
void imuse_filelist_flush(imuse_t* im);

#endif
