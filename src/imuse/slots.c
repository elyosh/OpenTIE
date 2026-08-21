#include "internal/slots.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/midi_backend.h"
#include "internal/players.h"
#include <imuse/commands.h>

/*
 * Channel mapping: pair i → outChannelId = (i < 9) ? i : i + 1.
 * MIDI channel 9 is reserved for drums and sits outside the pool.
 *
 * Note-bank bitmaps: shared per-part-side (P1 vs P2) arrays of
 * 128 u32s, one bit per pair index. part1 of a pair points at
 * the P1 banks, part2 at the P2 banks, so a share-parts pair can
 * have the same note live in both layers without clashing.
 */

/* ===== CC codes (avoid including midi.h for these) ===== */
#define CC_MODULATION 1
#define CC_VOLUME 7
#define CC_PAN 10
#define CC_PART_NOTEREQ 17
#define CC_PART_PRIORITY 18

/* Drum channel MIDI number (between pair 8 and pair 9 in the
 * outChannelId mapping). */
#define DRUM_CHANNEL 9

/* ===== Module-private state ===== */

/* Note-bank bitmaps. [note] → bits are 1<<pair_index. Two sets:
 * P1 for part1 halves, P2 for part2 halves. Bank 1 = actively
 * sounding; bank 2 = held past release by sustain. */

/* Drum channel state cache — shared by every partStatus==0
 * channel routing through ImSlots_HandleDrumNoteOn. */

/* Installed MIDI driver entry point. NULL = silent mode — the
 * Drv* wrappers become no-ops but bookkeeping still runs so the
 * engine stays internally consistent. */

/* channelBit helper: bit = 1 << outChannelId, matching the DOS
 * c_channelMask[] lookup. */
static unsigned int channel_bit(int outChannelId) { return 1u << outChannelId; }

/* ===== Driver dispatch (forward decls, bodies below) ===== */

/* ===== Lifecycle ===== */

int ImSlots_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "SLOTS module...");

	/* MIDI backend is brought up by ImCommands_Init before this
	 * function runs (so the CC defaults emitted below reach a live
	 * synth). NULL backend = MIDI silent — the ImSlots_Drv* wrappers
	 * become no-ops via ImMidi_* dispatch. */

	im->slots.drvIgnoreNotes = 0;
	for (int n = 0; n < 128; ++n) {
		im->slots.activeNotesP1[n] = 0;
		im->slots.activeNotesP2[n] = 0;
		im->slots.sustainedNotesP1[n] = 0;
		im->slots.sustainedNotesP2[n] = 0;
	}

	for (int i = 0; i < 15; ++i) {
		ImPlayerParts* pair = &im->slots.parts[i];
		int outCh = (i < 9) ? i : i + 1;

		pair->part1.player = 0;
		pair->part1.outChannel = 0;
		pair->part1.sharedMidiChannel = &pair->part2;
		pair->part1.outChannelId = outCh;
		pair->part1.pgm = 0;
		pair->part1.priority = 0;
		pair->part1.noteReq = 0;
		pair->part1.volume = 127;
		pair->part1.pan = 64;
		pair->part1.modulation = 0;
		pair->part1.pitchBend = 0;
		pair->part1.sustain = 0;
		pair->part1.activeNotes = im->slots.activeNotesP1;
		pair->part1.sustainedNotes = im->slots.sustainedNotesP1;

		pair->part2.player = 0;
		pair->part2.outChannel = 0;
		pair->part2.sharedMidiChannel = &pair->part1;
		pair->part2.outChannelId = outCh;
		pair->part2.pgm = 0;
		pair->part2.priority = 0;
		pair->part2.noteReq = 0;
		pair->part2.volume = 127;
		pair->part2.pan = 64;
		pair->part2.modulation = 0;
		pair->part2.pitchBend = 0;
		pair->part2.sustain = 0;
		pair->part2.activeNotes = im->slots.activeNotesP2;
		pair->part2.sustainedNotes = im->slots.sustainedNotesP2;

		/* Push initial control state once per pair (both halves
		 * share outChannelId). The DOS build does this inside the
		 * part2 block; mirror that. */
		ImSlots_DrvProgramChange(im, outCh, pair->part2.pgm);
		ImSlots_DrvControlChange(im, outCh, CC_PART_PRIORITY, pair->part2.priority);
		ImSlots_DrvControlChange(im, outCh, CC_PART_NOTEREQ, pair->part2.noteReq);
		ImSlots_DrvControlChange(im, outCh, CC_VOLUME, pair->part2.volume);
		ImSlots_DrvControlChange(im, outCh, CC_PAN, pair->part2.pan);
		ImSlots_DrvControlChange(im, outCh, CC_MODULATION, pair->part2.modulation);
		ImSlots_DrvSetPitchBend(im, outCh, 2 * pair->part2.pitchBend + 0x2000);
	}

	/* Drum channel defaults + push. Not in the pair pool. */
	im->slots.drumPriority = 0;
	ImSlots_DrvControlChange(im, DRUM_CHANNEL, CC_PART_PRIORITY, 0);
	im->slots.drumPartNoteReq = 1;
	ImSlots_DrvControlChange(im, DRUM_CHANNEL, CC_PART_NOTEREQ, 1);
	im->slots.drumVolume = 127;
	ImSlots_DrvControlChange(im, DRUM_CHANNEL, CC_VOLUME, 127);

	return 0;
}

int ImSlots_Deinit(imuse_t* im) {
	/* MIDI backend close is owned by ImCommands_Terminate; nothing
	 * to do here. */
	(void)im;
	return 0;
}

/* ===== Pair allocation ===== */

ImPlayerParts* ImSlots_GetFreePlayerParts(imuse_t* im) {
	/* BOTH halves must be unbound — either half being live
	 * reserves the whole pair (share-parts may later claim the
	 * other half). */
	for (int i = 0; i < 15; ++i) {
		if (!im->slots.parts[i].part1.player && !im->slots.parts[i].part2.player)
			return &im->slots.parts[i];
	}
	return 0;
}

/* ===== Note routing ===== */

void ImSlots_HandleNoteOn(imuse_t* im, ImPart* part, int note, int velocity) {
	if (!part)
		return;
	unsigned int bit = channel_bit(part->outChannelId);
	unsigned int* active = &part->activeNotes[note];

	if (*active & bit) {
		/* Retrigger of an actively sounding note: restart the
		 * driver envelope with Off+On; bit stays set in bank 1. */
		ImSlots_DrvNoteOff(im, part->outChannelId, note);
		ImSlots_DrvNoteOn(im, part->outChannelId, note, velocity);
		return;
	}

	unsigned int* held = &part->sustainedNotes[note];
	if (*held & bit) {
		/* Retrigger of a sustain-held note: promote bank 2 → bank 1
		 * (note is 'playing' again, not just ringing) and restart. */
		*held &= ~bit;
		*active |= bit;
		ImSlots_DrvNoteOff(im, part->outChannelId, note);
		ImSlots_DrvNoteOn(im, part->outChannelId, note, velocity);
		return;
	}

	/* Fresh note: set bit in bank 1, fire NoteOn. */
	*active |= bit;
	ImSlots_DrvNoteOn(im, part->outChannelId, note, velocity);
}

void ImSlots_HandleNoteOff(imuse_t* im, ImPart* part, int note) {
	if (!part)
		return;
	unsigned int bit = channel_bit(part->outChannelId);
	unsigned int* active = &part->activeNotes[note];
	if (!(*active & bit))
		return; /* silent no-op for spurious offs */

	*active &= ~bit;
	if (part->sustain) {
		/* Pedal held: park the bit in bank 2; driver keeps ringing. */
		part->sustainedNotes[note] |= bit;
	} else {
		ImSlots_DrvNoteOff(im, part->outChannelId, note);
	}
	/* Bank 2 is never touched here — only SetChannelSustain(0)
	 * and FreeMidiChannel clear it. */
}

void ImSlots_HandleDrumNoteOn(imuse_t* im, int priority, int noteReq, int volume, int note, int velocity) {
	/* Change-detect each cached state against the new settings;
	 * push to the driver only when they actually differ. Every
	 * virtual channel with partStatus==0 funnels through this
	 * one physical channel, so the cached globals hold whatever
	 * was pushed last. */
	if (priority != im->slots.drumPriority) {
		im->slots.drumPriority = priority;
		ImSlots_DrvControlChange(im, DRUM_CHANNEL, CC_PART_PRIORITY, priority);
	}
	if (noteReq != im->slots.drumPartNoteReq) {
		im->slots.drumPartNoteReq = noteReq;
		ImSlots_DrvControlChange(im, DRUM_CHANNEL, CC_PART_NOTEREQ, noteReq);
	}
	if (volume != im->slots.drumVolume) {
		im->slots.drumVolume = volume;
		ImSlots_DrvControlChange(im, DRUM_CHANNEL, CC_VOLUME, volume);
	}
	ImSlots_DrvNoteOn(im, DRUM_CHANNEL, note, velocity);
}

int ImSlots_NoteOffDrumChannel(imuse_t* im, int note) {
	/* No note-mask bookkeeping — drums are one-shot percussive
	 * voices with no sustain handling or retrigger counting. */
	ImSlots_DrvNoteOff(im, DRUM_CHANNEL, note);
	return 0;
}

/* ===== Slot release ===== */

void ImSlots_FreeMidiChannel(imuse_t* im, ImPart* part) {
	if (!part)
		return;
	unsigned int bit = channel_bit(part->outChannelId);
	part->sustain = 0;

	/* Walk every note; drain bank 1 first, bank 2 second. A note
	 * can't legally be in both banks at once so `else if` is safe. */
	for (int note = 0; note < 128; ++note) {
		if (part->activeNotes[note] & bit) {
			ImSlots_DrvNoteOff(im, part->outChannelId, note);
			part->activeNotes[note] &= ~bit;
		} else if (part->sustainedNotes[note] & bit) {
			ImSlots_DrvNoteOff(im, part->outChannelId, note);
			part->sustainedNotes[note] &= ~bit;
		}
	}
}

void ImSlots_SetChannelSustain(imuse_t* im, ImPart* part, int sustain) {
	if (!part)
		return;
	part->sustain = sustain;
	if (sustain)
		return; /* pressing: latent, nothing to do now */

	/* Release: drain bank 2. Each bank-2 bit already had its
	 * composer-side Note-Off processed; the pedal was the only
	 * thing keeping them ringing. Emit driver Note-Offs now. */
	unsigned int bit = channel_bit(part->outChannelId);
	for (int note = 0; note < 128; ++note) {
		unsigned int* held = &part->sustainedNotes[note];
		if (*held & bit) {
			*held &= ~bit;
			ImSlots_DrvNoteOff(im, part->outChannelId, note);
		}
	}
}

/* ===== Sustain helpers ===== */

/* Tiny popcount for 16-bit masks (16 pair-indices, MSB set). */
static int popcount_u32(unsigned int v) {
	int n = 0;
	while (v) {
		n += (int)(v & 1u);
		v >>= 1;
	}
	return n;
}

void ImSlots_GetPlayerNotes(imuse_t* im, struct ImMidiPlayer* player, unsigned int* notesMask,
							int* notesCount) {
	*notesCount = 0;

	/* Build the per-side channel-bit masks first, then AND with
	 * each note's bitmap. This keeps the per-note loop body cheap. */
	unsigned int p1Mask = 0;
	for (int i = 0; i < 15; ++i) {
		if (im->slots.parts[i].part1.player == player)
			p1Mask |= channel_bit(im->slots.parts[i].part1.outChannelId);
	}
	for (int n = 0; n < 128; ++n) {
		unsigned int bits = p1Mask & im->slots.activeNotesP1[n];
		notesMask[n] = bits;
		*notesCount += popcount_u32(bits);
	}

	unsigned int p2Mask = 0;
	for (int i = 0; i < 15; ++i) {
		if (im->slots.parts[i].part2.player == player)
			p2Mask |= channel_bit(im->slots.parts[i].part2.outChannelId);
	}
	for (int n = 0; n < 128; ++n) {
		unsigned int bits = p2Mask & im->slots.activeNotesP2[n];
		notesMask[n] |= bits;
		*notesCount += popcount_u32(bits);
	}

	/* Sustained bank is NOT consulted — only actively sounding
	 * notes count. Sustained-past-release is a driver ring-out,
	 * not 'playing' from the sequencer's view. */
}

void ImSlots_RestartPlayerNotes(imuse_t* im, struct ImMidiPlayer* player) {
	/* Walk bits LSB-up; pair index increments with each shift.
	 * When pairIdx reaches 9 we shift an extra time to skip the
	 * drum-channel bit (which is always 0 but takes a position in
	 * the bitmask to match the outChannelId mapping). */
	for (int note = 0; note < 128; ++note) {
		unsigned int p1Bits = im->slots.sustainedNotesP1[note] | im->slots.activeNotesP1[note];
		if (p1Bits) {
			int pairIdx = 0;
			while (p1Bits) {
				if ((p1Bits & 1u) && im->slots.parts[pairIdx].part1.player == player) {
					/* Velocity hardcoded to 64 — original did not
					 * preserve per-note velocity in the bitmap. */
					ImSlots_DrvNoteOn(im, im->slots.parts[pairIdx].part1.outChannelId, note, 64);
				}
				p1Bits >>= 1;
				if (++pairIdx == 9)
					p1Bits >>= 1;
			}
		}

		unsigned int p2Bits = im->slots.sustainedNotesP2[note] | im->slots.activeNotesP2[note];
		if (p2Bits) {
			int pairIdx = 0;
			while (p2Bits) {
				if ((p2Bits & 1u) && im->slots.parts[pairIdx].part2.player == player) {
					ImSlots_DrvNoteOn(im, im->slots.parts[pairIdx].part2.outChannelId, note, 64);
				}
				p2Bits >>= 1;
				if (++pairIdx == 9)
					p2Bits >>= 1;
			}
		}
	}
}

/* ===== Silent-replay gate ===== */

void ImSlots_SetDrvIgnoreNotes(imuse_t* im) { im->slots.drvIgnoreNotes = 1; }
void ImSlots_ClearDrvIgnoreNotes(imuse_t* im) { im->slots.drvIgnoreNotes = 0; }

/* ===== Per-part setters =====
 *
 * Template for all of them:
 *   - null `part` → no-op
 *   - value unchanged → no-op (saves redundant MIDI wire traffic)
 *   - mirror to part->sharedMidiChannel (keeps the pair sibling
 *     in sync so the silent half sees the current state when it
 *     later takes over)
 *   - push to driver
 */

void ImSlots_PartSetPgm(imuse_t* im, ImPart* part, int pgm) {
	if (!part || pgm == part->pgm)
		return;
	part->sharedMidiChannel->pgm = pgm;
	part->pgm = pgm;
	ImSlots_DrvProgramChange(im, part->outChannelId, pgm);
}

void ImSlots_PartSetPriority(imuse_t* im, ImPart* part, int priority) {
	if (!part || priority == part->priority)
		return;
	part->sharedMidiChannel->priority = priority;
	part->priority = priority;
	ImSlots_DrvControlChange(im, part->outChannelId, CC_PART_PRIORITY, priority);
}

void ImSlots_PartSetNoteReq(imuse_t* im, ImPart* part, int noteReq) {
	if (!part || noteReq == part->noteReq)
		return;
	part->sharedMidiChannel->noteReq = noteReq;
	part->noteReq = noteReq;
	ImSlots_DrvControlChange(im, part->outChannelId, CC_PART_NOTEREQ, noteReq);
}

void ImSlots_PartSetVolume(imuse_t* im, ImPart* part, int volume) {
	if (!part || volume == part->volume)
		return;
	part->sharedMidiChannel->volume = volume;
	part->volume = volume;
	ImSlots_DrvControlChange(im, part->outChannelId, CC_VOLUME, volume);
}

void ImSlots_PartSetPan(imuse_t* im, ImPart* part, int pan) {
	if (!part || pan == part->pan)
		return;
	part->sharedMidiChannel->pan = pan;
	part->pan = pan;
	ImSlots_DrvControlChange(im, part->outChannelId, CC_PAN, pan);
}

void ImSlots_PartSetModulation(imuse_t* im, ImPart* part, int modulation) {
	if (!part || modulation == part->modulation)
		return;
	part->sharedMidiChannel->modulation = modulation;
	part->modulation = modulation;
	ImSlots_DrvControlChange(im, part->outChannelId, CC_MODULATION, modulation);
}

void ImSlots_PartSetPitchBend(imuse_t* im, ImPart* part, int pitchBend) {
	if (!part || pitchBend == part->pitchBend)
		return;
	part->sharedMidiChannel->pitchBend = pitchBend;
	part->pitchBend = pitchBend;
	/* Encode to 14-bit MIDI wire: stretch ×2, recenter at 0x2000. */
	ImSlots_DrvSetPitchBend(im, part->outChannelId, 2 * pitchBend + 0x2000);
}

/* ===== Driver dispatch wrappers =====
 *
 * Forward to the configured MIDI backend. NoteOn/NoteOff honor
 * im->slots.drvIgnoreNotes so silent-replay scans skip audible output while
 * bookkeeping runs.
 */

void ImSlots_DrvProgramChange(imuse_t* im, int outChannel, int pgm) {
	ImMidi_ProgramChange(im, outChannel, pgm);
}

void ImSlots_DrvNoteOn(imuse_t* im, int outChannel, int note, int velocity) {
	if (im->slots.drvIgnoreNotes)
		return;
	ImMidi_NoteOn(im, outChannel, note, velocity);
}

void ImSlots_DrvNoteOff(imuse_t* im, int outChannel, int note) {
	if (im->slots.drvIgnoreNotes)
		return;
	ImMidi_NoteOff(im, outChannel, note);
}

void ImSlots_DrvControlChange(imuse_t* im, int outChannel, int ctrl, int value) {
	ImMidi_ControlChange(im, outChannel, (unsigned int)ctrl, value);
}

void ImSlots_DrvSetPitchBend(imuse_t* im, int outChannel, int pitchBendWire) {
	ImMidi_PitchBend(im, outChannel, pitchBendWire);
}

/* The DOS debug overlay is unsupported. */
void ImSlots_Debug(imuse_t* im) {}
