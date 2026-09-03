/* Per-session mutable state. Consumers use the opaque `imuse_t` type. */
#ifndef IMUSE_INTERNAL_STATE_H
#define IMUSE_INTERNAL_STATE_H

#include <stdint.h>

#include <imuse/handle.h>
#include <imuse/midi_backend.h> /* ImuseMidiBackend */

#include <imuse/commands.h> /* ImuseHost, ImuseConfig */
#include <imuse/filelist.h> /* ImuseLoadSoundFunc, ImuseOpenSoundFunc, ... */
#include <imuse/groups.h>   /* IMUSE_NUM_GROUPS */

#include "internal/dispatch.h"      /* ImWaveDispatch */
#include "internal/fades.h"         /* ImSoundFader, IM_NUM_FADERS */
#include "internal/mixer.h"         /* ImSoundFrameState, ImDigitalOutBuf */
#include "internal/players.h"       /* ImMidiPlayer */
#include "internal/seq.h"           /* ImSeqData */
#include "internal/slots.h"         /* ImPlayerParts */
#include "internal/streamer.h"      /* ImWaveStream */
#include "internal/sustain.h"       /* ImSustainedSound */
#include "internal/tracks.h"        /* ImWaveTrack */
#include "internal/triggers.h"      /* ImTrigger, ImDeferCmd, IM_NUM_TRIGGERS, IM_NUM_DEFER_CMDS */
#include "internal/wave_renderer.h" /* ImWaveRendererState */

/* Pool / table sizes that live in .c files today; promoted here so the
 * struct can embed by value. */
#define IMUSE_MAX_SOUND_ENTRIES 10
#define IMUSE_MAX_SOUND_NAME 23
#define IMUSE_SUSTAIN_POOL_SIZE 24
#define IMUSE_MIXER_MAX_MIX_COUNT 16
#define IMUSE_DISPATCH_POOL_SIZE 16

/* SoundEntry is internal to the FILELIST module but lives here so
 * struct imuse can embed the entry pool by value. The struct's
 * fields are still only touched by filelist.c. */
typedef struct ImSoundEntry {
	struct ImSoundEntry* prev;
	struct ImSoundEntry* next;
	void* handle;
	char name[IMUSE_MAX_SOUND_NAME + 1];
	int ref_count;
} ImSoundEntry;

/* ===== Per-module state structs ===== */

typedef struct ImCommandsState {
	ImuseConfig config; /* copy of host-supplied settings */
	int initialized;    /* nonzero between Init and Terminate */
	int paused;
	int32_t timerCoreAccum; /* Real host time toward the next service. */
	int32_t timer60HzAccum; /* Logical iMUSE time. */
	int32_t timer10HzAccum; /* Logical iMUSE time. */
	ImuseLogFunc logFunc; /* mirrors host logFunc */
	void* logUser;
} ImCommandsState;

typedef struct ImGroupsState {
	int volsRaw[IMUSE_NUM_GROUPS];
	int volsEff[IMUSE_NUM_GROUPS];
} ImGroupsState;

typedef struct ImFadesState {
	ImSoundFader pool[IM_NUM_FADERS];
	int on;
} ImFadesState;

typedef struct ImTriggersState {
	ImTrigger triggers[IM_NUM_TRIGGERS];
	ImDeferCmd defers[IM_NUM_DEFER_CMDS];
	int defersOn;
} ImTriggersState;

typedef struct ImMidiState {
	int lock;
	int paused;
	int debugRefreshCounter;
	ImuseMidiBackend* backend; /* NULL = MIDI silent (no synth attached) */
} ImMidiState;

typedef struct ImStreamerState {
	ImWaveStream streams[2];
	ImWaveStream* lastStreamLoaded;
} ImStreamerState;

typedef struct ImDispatchState {
	ImWaveDispatch pool[IMUSE_DISPATCH_POOL_SIZE];
	unsigned char waveChunkData[48];
	unsigned char resampleBuffer[1024];
	ImSoundFrameState mixFrameState;
} ImDispatchState;

typedef struct ImPlayersState {
	ImMidiPlayer players[2];
	ImMidiPlayer* list;
} ImPlayersState;

typedef struct ImSlotsState {
	unsigned int activeNotesP1[128];
	unsigned int activeNotesP2[128];
	unsigned int sustainedNotesP1[128];
	unsigned int sustainedNotesP2[128];
	ImPlayerParts parts[15];
	int drumPriority;
	int drumPartNoteReq;
	int drumVolume;
	int drvIgnoreNotes;
} ImSlotsState;

typedef struct ImSustainState {
	ImSustainedSound pool[IMUSE_SUSTAIN_POOL_SIZE];
	ImSustainedSound* freeList;
	ImSustainedSound* activeList;
	unsigned int curMidiNoteMask[128];
	int curNoteCount;
	unsigned int midiSustainChannelMask[16];
	int trackTicksRemaining;
	int midiTickDelta;
	int midiTrackEnd;
} ImSustainState;

typedef struct ImSeqState {
	ImSeqData data[2];
	int isEndOfTrack;
	ImSeqData soundPrevState;
	ImSeqData soundNextState;
	ImSeqData scanPrevState;
	ImSeqData scanNextState;
	int sysExLen;
} ImSeqState;

typedef struct ImTracksState {
	ImWaveTrack pool[16];
	ImWaveTrack* list;
	int waveMixCount;              /* clamped 1..16, default 4 */
	int pauseTimer;                /* 0 = running, 1..3 = paused */
	int nanosecsPerSample;         /* 45454 (22 kHz) or 90909 (11 kHz) */
	int timerDivAccum;             /* silent-mode sample-clock accumulator */
	int16_t* mixBufferOut;         /* int16 scratch supplied by the
									  internal wave renderer */
	ImDigitalOutBuf defaultOutBuf; /* used in silent mode only */
} ImTracksState;

typedef struct ImMixerState {
	int8_t normBuf[IMUSE_MIXER_MAX_MIX_COUNT * 128 * 2];
	int8_t* normalization;
	ImDigitalOutBuf* driverOutPtr;
	int16_t* outBuf;
	ImSoundFrameState addFrameState;
} ImMixerState;

typedef struct ImFilelistState {
	ImSoundEntry entries[IMUSE_MAX_SOUND_ENTRIES];
	ImSoundEntry* loadedList;
	ImSoundEntry* openedList;
	int initFlag;
	ImuseLoadSoundFunc loadFunc;
	ImuseUnloadSoundFunc unloadFunc;
	ImuseOpenSoundFunc openFunc;
	ImuseCloseSoundFunc closeFunc;
} ImFilelistState;

typedef struct ImFilesState {
	ImuseHost host;  /* copy of host callbacks captured at Init */
	int initialized; /* nonzero once ImFiles_Init succeeded */
} ImFilesState;

typedef struct ImHilevelState {
	void* waveFillFunc; /* INSANE wave-fill callback set
						   via imuse_ImInsane(2, cb) */
} ImHilevelState;

typedef struct ImWaveModuleState {
	int wvSlicingHalted;
} ImWaveModuleState;

/* ===== The handle ===== */

struct imuse {
	ImCommandsState commands;
	ImGroupsState groups;
	ImFadesState fades;
	ImTriggersState triggers;
	ImMidiState midi;
	ImStreamerState streamer;
	ImDispatchState dispatch;
	ImPlayersState players;
	ImSlotsState slots;
	ImSustainState sustain;
	ImSeqState seq;
	ImTracksState tracks;
	ImMixerState mixer;
	ImFilelistState filelist;
	ImFilesState files;
	ImHilevelState hilevel;
	ImWaveModuleState wave;
	ImWaveRendererState wave_renderer;
};

#endif /* IMUSE_INTERNAL_STATE_H */
