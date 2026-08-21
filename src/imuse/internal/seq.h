#ifndef __IMUSE_SEQ_H__
#define __IMUSE_SEQ_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Two sequencer slots pair one-to-one with MIDI players. Live, jump, and scan
 * walkers share the parser with different dispatch tables. Scan mutates slot
 * state with driver notes suppressed, then restarts the resulting live notes. */

struct ImMidiPlayer;

/* Packed sequencer time format:
 *   bits 0..15   ticks within the current beat (0..ticksPerBeat-1)
 *   bits 16..19  beat within the current measure (0..beatsPerMeasure-1)
 *   bits 20..31  measure since the chunk started (0..4095)
 *
 * ticksPerBeat is a plain int. beatsPerMeasure is stored PRE-SHIFTED
 * by 16 (see ImSeq_SetTicksPerBeat) so it can be compared directly
 * against the beat bits of a packed value in FixupSoundTick. */
typedef struct ImSeqData {
	struct ImMidiPlayer* player; /* paired player (back-ptr) */
	intptr_t soundId;            /* live sound */
	int32_t seqIndex;            /* current MTrk chunk index */
	int32_t chunkOffset;         /* payload offset inside sndData */
	int32_t nextTick;            /* packed 'advance up to' tick */
	int32_t curTick;             /* packed 'playing now' tick */
	int32_t chunkPtr;            /* cursor relative to chunkOffset */
	int32_t ticksPerBeat;
	int32_t beatsPerMeasure; /* stored << 16 */
	int32_t tickAccum;       /* 16.16 fixed-point accumulator */
	int32_t tempo;           /* µs per quarter note */
	int32_t step;            /* 16.16 ticks-per-interrupt (nominal) */
	int32_t speed;           /* fixed-point 0x80 = 1.0 */
	int32_t stepFixed;       /* step * speed >> 7 */
} ImSeqData;

/* ===== Lifecycle / save ===== */

int ImSeq_Init(imuse_t* im);
int ImSeq_Deinit(imuse_t* im);

int ImSeq_Save(imuse_t* im, void* buf, int size);
int ImSeq_Restore(imuse_t* im, void* buf);

/* Slot lookup for ImPlayers_Init: returns &seqData[playerId]. */
ImSeqData* ImSeq_GetSeqDataForPlayerId(imuse_t* im, int playerId);

/* Bind a slot to a sound and position at the first 'MTrk' chunk.
 * Initializes tempo 500000 µs/qn (= 120 BPM), speed 1.0, 480 tpb,
 * 4/4 time. */
int ImSeq_StartSound(imuse_t* im, ImSeqData* data, intptr_t soundId);

/* Per-tick driver. Called from ImPlayers_Update on every engine
 * quantum per live MIDI player. */
void ImSeq_Update(imuse_t* im, ImSeqData* seq);

/* Raise a shared flag so the currently-running walker bails. Used
 * by ImPlayers_StopPlayer when a player retires mid-walk. */
void ImSeq_SetEndOfTrack(imuse_t* im);

/* ===== Transport ===== */

int ImSeq_Jump(imuse_t* im, ImSeqData* data, int chunk, int measure, int beat, int tick, int sustain);
int ImSeq_Scan(imuse_t* im, ImSeqData* data, int chunk, int measure, int beat, int tick);

/* ===== Time controls ===== */

int ImSeq_SetSpeed(imuse_t* im, ImSeqData* seq, int speed);
int ImSeq_SetTempo(imuse_t* im, ImSeqData* seq, int tempo);
void ImSeq_SetTicksPerBeat(imuse_t* im, ImSeqData* seq, int ticksPerBeat, int beatsPerMeasure);

/* Query ImuseParam::midiChunk/Measure/Beat/Tick/Speed from
 * the packed nextTick. Returns -5 for other params. */
int ImSeq_GetTimeParam(imuse_t* im, ImSeqData* seq, int param);

/* ===== MIDI dispatch ===== */

/* Inject a channel-voice message into the running player via the
 * live dispatch table. Status must be 0x80..0xEF. */
int ImSeq_HandleMidiMsg(imuse_t* im, struct ImMidiPlayer** playerSlot, int status, int data1, int data2);

/* Raw tick delta between two packed positions sharing a measure. */
int ImSeq_MidiGetTickDelta(imuse_t* im, ImSeqData* playerData, int prevTick, int tick);

/* Walk the MIDI chunk, firing events < playerData->nextTick
 * through the caller-supplied 9-slot dispatch table. */
void ImSeq_AdvanceMidi(imuse_t* im, ImSeqData* playerData, void* sndData, void** midiCmdFunc);

/* Seek the slot to chunk[chunkId] (0-based). Loads the initial
 * delta-time, normalizes it, zeroes nextTick. */
int ImSeq_GoToChunk(imuse_t* im, ImSeqData* data, void* sndData, int chunkId);

/* Normalize a packed tick value so tick bits < ticksPerBeat and
 * beat bits < beatsPerMeasure. */
int ImSeq_FixupSoundTick(imuse_t* im, ImSeqData* data, unsigned int value);

/* MIDI aftertouch no-op: logs 'hey, quit the aftertouch' and
 * drops the event. Wired into slots 2 (poly AT) and 5 (chan AT) of
 * the live dispatch table. */
void ImSeq_MidiPressure(imuse_t* im);

/* Meta-event handler used by jump/scan modes. End-of-Track raises
 * seq_isEndOfTrack only (doesn't tear down the player). */
void ImSeq_HandleMetaJumpScan(imuse_t* im, ImSeqData* playerData, const uint8_t* chunkData);

/* ===== Parser sub-module =====
 *
 * ImParser_HandleSysEx implements LucasArts' 0xF0 0x7D protocol:
 *   sub-op 0 = marker   (payload[0] = markerId 0..127)
 *   sub-op 1 = jump-hook (hookId + packed jump target + sustain)
 *   sub-op 3 = large marker (marker=128 + payload-pointer passed
 *                           to ImTriggers_ProcessMarker)
 *
 * ImParser_HandleMetaEvent is the live-mode meta handler. EoT
 * retires the player; tempo and time-sig update the seqData slot. */

void ImParser_HandleSysEx(imuse_t* im, ImSeqData* playerData, const uint8_t* chunkData);
void ImParser_HandleMetaEvent(imuse_t* im, ImSeqData* playerData, const uint8_t* chunkData);

#endif /* __IMUSE_SEQ_H__ */
