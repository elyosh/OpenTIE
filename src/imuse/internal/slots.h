#ifndef __IMUSE_SLOTS_H__
#define __IMUSE_SLOTS_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Owns paired MIDI voice slots and active/sustained note bitmaps. Pair index
 * maps to the same MIDI channel except above channel 9, which is reserved for drums. */

struct ImMidiPlayer;
struct ImMidiOutChannel;

/* ===== Types ===== */

/* Physical voice slot. Owned in pairs (ImPlayerParts) so a
 * share-parts handoff can bind two player channels to the same
 * physical pair. sharedMidiChannel is the sibling back-pointer
 * inside a pair (part1.shared == &part2, part2.shared == &part1).
 *
 * activeNotes / sustainedNotes are pointers into the module-
 * global 128-entry u32 arrays (one per note number, bit = pair
 * index). part1 points at the P1 banks, part2 at P2. */
typedef struct ImPart {
	struct ImMidiPlayer* player;         /* owning player */
	struct ImMidiOutChannel* outChannel; /* owning channel */
	struct ImPart* sharedMidiChannel;    /* sibling in the pair */
	int32_t outChannelId;                /* MIDI channel 0..15 */
	int32_t pgm;                         /* program number */
	int32_t priority;                    /* effective priority */
	int32_t noteReq;                     /* poly-request */
	int32_t volume;                      /* effective volume */
	int32_t pan;                         /* 0..127 */
	int32_t modulation;                  /* 0..127 */
	int32_t pitchBend;                   /* engine-internal (signed) */
	int32_t sustain;                     /* 0/1 pedal held */
	unsigned int* activeNotes;           /* 128-entry bank 1 bitmap */
	unsigned int* sustainedNotes;        /* 128-entry bank 2 bitmap */
} ImPart;

typedef struct ImPlayerParts {
	ImPart part1;
	ImPart part2;
} ImPlayerParts;

/* ===== Lifecycle ===== */

int ImSlots_Init(imuse_t* im);
int ImSlots_Deinit(imuse_t* im);
void ImSlots_Debug(imuse_t* im);

/* ===== Pair allocation + slot release ===== */

/* Linear scan for the first voice pair whose BOTH halves are
 * unused (part1.player == NULL && part2.player == NULL).
 * Returns NULL when the pool is full. PLAYERS' MidiSetupParts
 * takes the returned pair and binds &pair->part1. */
ImPlayerParts* ImSlots_GetFreePlayerParts(imuse_t* im);

/* Silence every note currently tracked on this slot (both
 * bank-1 actives and bank-2 sustained), zero the sustain flag.
 * Does NOT clear the back-links — callers handle that. */
void ImSlots_FreeMidiChannel(imuse_t* im, ImPart* part);

/* ===== Per-part state setters ===== */
/*
 * Each setter is null-safe + change-detect + mirror-to-sibling +
 * driver-push. Skipping a driver call when the value is unchanged
 * avoids redundant MIDI wire traffic on hot paths (volume is
 * recomputed on every group-vol change).
 */

void ImSlots_PartSetPgm(imuse_t* im, ImPart* part, int pgm);
void ImSlots_PartSetPriority(imuse_t* im, ImPart* part, int priority);
void ImSlots_PartSetNoteReq(imuse_t* im, ImPart* part, int noteReq);
void ImSlots_PartSetVolume(imuse_t* im, ImPart* part, int volume);
void ImSlots_PartSetPan(imuse_t* im, ImPart* part, int pan);
void ImSlots_PartSetModulation(imuse_t* im, ImPart* part, int modulation);

/* pitchBend is the engine-internal signed value; the driver is
 * called with the 14-bit wire-format (2 * value + 0x2000). */
void ImSlots_PartSetPitchBend(imuse_t* im, ImPart* part, int pitchBend);

/* Sustain-pedal transitions: store the new value; on release
 * (to 0), drain the bank-2 sustained notes and emit NoteOffs. */
void ImSlots_SetChannelSustain(imuse_t* im, ImPart* part, int sustain);

/* ===== Note routing ===== */

/* Emit driver NoteOn on the slot's physical channel, updating
 * the bank-1/bank-2 bitmaps (retrigger of an active note gets
 * a Note-Off + Note-On; retrigger of a sustained note promotes
 * it from bank 2 to bank 1). Null `part` is a silent no-op. */
void ImSlots_HandleNoteOn(imuse_t* im, ImPart* part, int note, int velocity);

/* Emit driver NoteOff — UNLESS the sustain pedal is held, in
 * which case the bit moves from bank 1 to bank 2 and the note
 * keeps ringing until sustain release. */
void ImSlots_HandleNoteOff(imuse_t* im, ImPart* part, int note);

/* Drum out-channel (MIDI ch 9): shared by every player's
 * partStatus==0 virtual channels, so the three cached state
 * globals are change-detected against the last push before the
 * NoteOn. No note-mask bookkeeping (drums are one-shot). */
void ImSlots_HandleDrumNoteOn(imuse_t* im, int priority, int noteReq, int volume, int note, int velocity);
int ImSlots_NoteOffDrumChannel(imuse_t* im, int note);

/* ===== Sustain-module helpers ===== */

/* Snapshot every actively-sounding note on slots owned by
 * `player` into notesMask[0..127] (1 bit per pair-index after
 * the channel-9 gap adjustment). *notesCount receives the total.
 * Bank-2 sustained notes are excluded — only actively playing
 * counts. Used by ImSustain_Jump to carry notes across seq jumps. */
void ImSlots_GetPlayerNotes(imuse_t* im, struct ImMidiPlayer* player, unsigned int* notesMask,
							int* notesCount);

/* Re-emit driver NoteOns (velocity=64) for every tracked note
 * (bank 1 OR bank 2) on slots owned by `player`. Used by
 * ImSeq_Scan after a silent-replay rebuild. */
void ImSlots_RestartPlayerNotes(imuse_t* im, struct ImMidiPlayer* player);

/* ===== Silent-replay gate ===== */
/*
 * DrvNoteOn/DrvNoteOff honor the slots_drvIgnoreNotes flag so a
 * scan or jump-scan can rebuild the sequencer's state without
 * audible retriggers. Book-keeping (bitmap writes) runs as
 * normal; only the driver calls are suppressed.
 */
void ImSlots_SetDrvIgnoreNotes(imuse_t* im);
void ImSlots_ClearDrvIgnoreNotes(imuse_t* im);

/* ===== Driver wrappers =====
 *
 * Thin facades over midiDriverCallPtr, used both internally
 * (state setters, note routing) and by other modules that need
 * to talk directly to the driver (ImSustain_Jump sends raw
 * NoteOns/Offs, ImParser uses DrvControlChange for sysex replay,
 * etc.).
 */
void ImSlots_DrvProgramChange(imuse_t* im, int outChannel, int pgm);
void ImSlots_DrvNoteOn(imuse_t* im, int outChannel, int note, int velocity);
void ImSlots_DrvNoteOff(imuse_t* im, int outChannel, int note);
void ImSlots_DrvControlChange(imuse_t* im, int outChannel, int ctrl, int value);
void ImSlots_DrvSetPitchBend(imuse_t* im, int outChannel, int pitchBendWire);

#endif /* __IMUSE_SLOTS_H__ */
