#ifndef __IMUSE_PLAYERS_H__
#define __IMUSE_PLAYERS_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Two live MIDI players, each with a sequencer cursor and 16 virtual channels.
 * Channels claim physical SLOTS voices lazily. */

struct ImPart;
struct ImSeqData;

/* Per-channel MIDI state living inside ImMidiPlayer.channels[16].
 *
 * data is a back-pointer to the currently-bound physical voice slot
 * (or NULL for a virtual channel). partStatus flips 0→1 on the
 * first ProgramChange and gates lazy part-allocation; see
 * ImPlayers_ProgramChange for the activation rule.
 *
 * groupVolume caches the pre-scaled per-channel volume so the
 * allocator doesn't have to recompute on every tick. Similarly,
 * effectiveDetune = (transpose<<8) + pitchBend + detune so
 * AssignMidiChannel can push one value to the slot. */
typedef struct ImMidiOutChannel {
	struct ImPart* data;     /* bound physical part, or NULL */
	int32_t partStatus;      /* 0 = virtual, 1 = wants a part */
	int32_t partPgm;         /* last program-change value */
	int32_t partTrim;        /* channel trim (SetParam midiPartTrim) */
	int32_t partPriority;    /* per-channel priority offset */
	int32_t priority;        /* cached = player.priority + partPriority */
	int32_t partNoteReq;     /* channel poly-request count */
	int32_t partVolume;      /* CC 7 value */
	int32_t groupVolume;     /* cached effective volume */
	int32_t partPan;         /* CC 10 value */
	int32_t modulation;      /* CC 1 value */
	int32_t sustain;         /* CC 64 value */
	int32_t pitchBend;       /* raw signed pitch-bend */
	int32_t pbRange;         /* CC 16 value; 0 repurposes bend as vol */
	int32_t effectiveDetune; /* cached composite for the slot */
} ImMidiOutChannel;

/* 1024-byte MIDI player: 64 bytes of header + 16x60B channels. */
typedef struct ImMidiPlayer {
	struct ImMidiPlayer* prev; /* ListAddItem wiring */
	struct ImMidiPlayer* next;
	struct ImSeqData* seqData; /* paired at Init, never rewired */
	struct ImMidiPlayer* sharedPartPlayer;
	intptr_t sharedSoundId;        /* partner id (survives save/load) */
	intptr_t soundId;              /* 0 = slot free */
	int32_t marker;                /* last fired trigger marker (-1 none) */
	int32_t group;                 /* ImuseGroup */
	int32_t priority;              /* 0..127 */
	int32_t volume;                /* 0..127 */
	int32_t groupVolume;           /* cached = (volume+1)*effGroupVol>>7 */
	int32_t pan;                   /* 0..127, center = 64 */
	int32_t detune;                /* fine pitch offset */
	int32_t transpose;             /* -12..+12 semitones */
	int32_t mailbox;               /* game-side scratchpad */
	int32_t hook;                  /* sequencer hook id */
	ImMidiOutChannel channels[16]; /* 16 MIDI channels */
} ImMidiPlayer;

/* ===== Lifecycle ===== */

int ImPlayers_Init(imuse_t* im);
int ImPlayers_Deinit(imuse_t* im);

/* Per-tick advance: walks the live list and steps each player's
 * sequencer cursor via ImSeq_Update. */
void ImPlayers_Update(imuse_t* im);

/* Render a one-screen debug overlay of the live player table. */
void ImPlayers_Debug(imuse_t* im);

int ImPlayers_Save(imuse_t* im, void* buf, int size);
int ImPlayers_Restore(imuse_t* im, void* buf);

/* Re-propagate current groupEffVols through every live player's
 * groupVolume + per-channel derived vol. Called by ImMidi_ApplyGroupVol. */
void ImPlayers_ApplyGroupVol(imuse_t* im);

/* ===== Sound control ===== */

int ImPlayers_StartSound(imuse_t* im, intptr_t soundId, int priority);
int ImPlayers_StopSound(imuse_t* im, intptr_t soundId);
int ImPlayers_StopAllSounds(imuse_t* im);
intptr_t ImPlayers_FindNextSound(imuse_t* im, intptr_t soundId);

/* Central teardown: wildcard-cancels fades/triggers/sustain for the
 * player's soundId, resets all 16 channels, unlinks from the live
 * list, and re-runs MidiSetupParts so freed parts get reassigned. */
void ImPlayers_StopPlayer(imuse_t* im, ImMidiPlayer* player);

/* Silence all 16 channels (CC 123 + modulation=0 + pitch-bend
 * center) WITHOUT freeing part assignments. Used by Jump/Scan when
 * sustain==0 and by the scan replay. */
void ImPlayers_StopAllNotes(imuse_t* im, ImMidiPlayer* player);

/* Brute-force NoteOff on every (channel,note) through player[0].
 * Three passes — matches the DOS "hammer the bus" panic. */
int ImPlayers_Panic(imuse_t* im);

/* ===== Parameter dispatch (opcode 12/13 handlers) ===== */

int ImPlayers_SetParam(imuse_t* im, intptr_t soundId, int param, int value);
int ImPlayers_GetParam(imuse_t* im, intptr_t soundId, int param);

int ImPlayers_SetHook(imuse_t* im, intptr_t soundId, uint32_t hookId);
int ImPlayers_GetHook(imuse_t* im, intptr_t soundId);

/* ===== MIDI / sequencer opcodes ===== */

int ImPlayers_Jump(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick, int sustain);
int ImPlayers_Scan(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick);
int ImPlayers_SendMidiMsg(imuse_t* im, intptr_t soundId, int status, int data1, int data2);

/* Pair two live sounds so they share physical voice slots for a
 * seamless crossfade. Rejected if either already participates in a
 * share pair; torn down bidirectionally at StopPlayer. */
int ImPlayers_ShareParts(imuse_t* im, intptr_t sound1, intptr_t sound2);

/* ===== MIDI event handlers (called by SEQ) ===== */

void ImPlayers_ProgramChange(imuse_t* im, ImMidiPlayer* player, int channelId, int program);
void ImPlayers_MidiNoteOn(imuse_t* im, ImMidiPlayer* player, int channelId, int note, int velocity);
void ImPlayers_MidiNoteOff(imuse_t* im, ImMidiPlayer* player, int channelId, int note);
void ImPlayers_MidiCommand(imuse_t* im, ImMidiPlayer* player, int channelIndex, int cc, int value);
void ImPlayers_HandleChannelPitchBend(imuse_t* im, ImMidiPlayer* player, int channelIndex, int data1,
									  int data2);

/* ===== Voice allocator + support ===== */

ImMidiPlayer* ImPlayers_GetFreePlayer(imuse_t* im, int priority);
ImMidiPlayer* ImPlayers_GetSoundPlayer(imuse_t* im, intptr_t soundId);

/* Recompute the voice-to-part mapping. Idempotent; called after any
 * event that changes channel priority, audibility, or share-parts
 * topology. */
void ImPlayers_MidiSetupParts(imuse_t* im);

void ImPlayers_AssignMidiChannel(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* outChannel,
								 struct ImPart* part);
void ImPlayers_ResetMidiOutChannel(imuse_t* im, ImMidiOutChannel* channel);

/* Cached-field recomputation after an input changed. Idempotent;
 * called from SetParam paths and the MIDI CC handler. */
void ImPlayers_HandleChannelPriorityChange(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* channel);
void ImPlayers_HandleChannelVolumeChange(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* channel);
void ImPlayers_HandleChannelDetuneChange(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* channel);

#endif /* __IMUSE_PLAYERS_H__ */
