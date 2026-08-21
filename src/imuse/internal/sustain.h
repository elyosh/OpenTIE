#ifndef __IMUSE_SUSTAIN_H__
#define __IMUSE_SUSTAIN_H__

#include <stdint.h>

#include <imuse/handle.h>

/*
 * iMUSE engine -- SUSTAIN module.
 *
 * Carries notes across MIDI sequencer jumps so a jump that lands
 * mid-phrase doesn't cut sustained notes. Implements the 'jump
 * bridge' from US5315057A: before ImSeq_Jump runs, the sustain
 * subsystem snapshots the live notes and silently scans both the
 * old and new sequences to pair up each ringing note with its
 * future Note-Off — either from the old sequence (give the note
 * its composed tail) or from a 10-tick cross-fade onto the new
 * sequence's next occurrence of the same note.
 *
 * Pool: 24 ImSustainedSound nodes on a free list + an active
 * list. ImSustain_Update ticks the active list each PIT tick,
 * firing the delayed Note-Off when curTick falls below zero.
 */

struct ImMidiPlayer;
struct ImSeqData;

/* 28-byte pool entry. prev/next at offset 0/4 match the
 * ImUtils_List convention. curTickFixed is a 16.16 accumulator
 * per-player stepFixed integrates into, overflowing into
 * curTick so the PIT rate is decoupled from the tempo. */
typedef struct ImSustainedSound {
	struct ImSustainedSound* prev;
	struct ImSustainedSound* next;
	struct ImMidiPlayer* midiPlayer;
	int32_t note;
	int32_t channelId;
	int32_t curTick;
	int32_t curTickFixed;
} ImSustainedSound;

int ImSustain_Init(imuse_t* im);
int ImSustain_Deinit(imuse_t* im);

/* Per-tick. Decrement curTickFixed + curTick; release expired. */
void ImSustain_Update(imuse_t* im);

/* Release all sustain nodes owned by `player`. Called from
 * ImPlayers_StopPlayer so teardown doesn't leave dangling
 * midiPlayer back-pointers. Doesn't emit driver Note-Offs —
 * the caller already silenced the physical parts. */
void ImSustain_ClearForPlayer(imuse_t* im, struct ImMidiPlayer* player);

/* Called by ImSeq_Jump. Walks the old sequence for Note-Offs
 * that close the still-sounding notes, then the new sequence to
 * fold their releases into a 10-tick crossfade around the new
 * occurrences. `sndData` is the base pointer of the player's
 * sound-buffer data (returned by ImFiles_GetSoundPtr); the
 * scanners read raw MIDI bytes from
 *   (const uint8_t *)sndData + seq->chunkPtr + seq->chunkOffset.
 * The DOS prototype typed this as `int`; we take it as
 * `const void *` so LP64 hosts don't truncate the address. */
void ImSustain_Jump(imuse_t* im, struct ImMidiPlayer* player, const void* sndData,
					struct ImSeqData* playerPrevData, struct ImSeqData* playerNextData);

#endif /* __IMUSE_SUSTAIN_H__ */
