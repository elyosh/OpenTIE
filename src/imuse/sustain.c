#include "internal/sustain.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/players.h"
#include "internal/seq.h"
#include "internal/slots.h"
#include "internal/utils.h"

#define SUSTAIN_POOL_SIZE 24

/* Crossfade window (ticks): when the new sequence's Note-On
 * matches a held sustain, shorten the sustain's remaining time
 * so it releases this many ticks after the new Note-On. Short
 * enough to avoid audible double-attack; long enough that the
 * release overlaps the new note's attack. */
#define SUSTAIN_CROSSFADE_TICKS 10

/* ===== Module-private state ===== */

/* Jump-scratch state. Populated by ImSustain_Jump and read by
 * the dispatch-table handlers invoked from
 * ImSustain_ProcessSustainJump. Module-global because the
 * ImGenericCmdFunc dispatch ABI has no closure slot. */

/* MIDI channel message sizes, indexed by (status & 0x70) >> 4.
 *   0 NoteOff (3), 1 NoteOn (3), 2 PolyAT (3), 3 CC (3),
 *   4 PgmChange (2), 5 ChanAT (2), 6 PitchBend (3).
 * Matches IMUSE.ENG's c_midiMsgSize[] table byte-for-byte. */
static const unsigned char c_midiMsgSize[7] = { 3, 3, 3, 3, 2, 2, 3 };

/* ===== Forward decls for the dispatch-table handlers ===== */

static void sustain_jumpOpen_NoteOff(imuse_t* im, struct ImMidiPlayer* player, int channelId, int note,
									 int data2);
static void sustain_jumpOpen_NoteOn(imuse_t* im, struct ImMidiPlayer* player, int channelId, int note,
									int velocity);
static void sustain_jump_NoteOn(imuse_t* im, struct ImMidiPlayer* player, int channelId, int note,
								int velocity);
static void sustain_metaTrackEnd(imuse_t* im, struct ImMidiPlayer* player, const unsigned char* metaData);

/* 9-entry dispatch tables passed into ImSustain_ProcessSustainJump.
 *
 * Slot layout (indexed by (status & 0x70) >> 4 for channel msgs):
 *   [0] NoteOff, [1] NoteOn, [2] PolyAT, [3] CC, [4] PgmChange,
 *   [5] ChanAT, [6] PitchBend, [7] SysEx, [8] Meta.
 *
 * Channel handlers take (player, channelId, data1, data2).
 * SysEx / Meta handlers take (player, payloadPtr). Unused slots
 * are NULL; the walker silently skips those events (still
 * advances the byte cursor).
 *
 * Storing typed thunks in a typeless pointer array is the DOS
 * original's ABI — we keep it verbatim so the walker can stay
 * ABI-agnostic like IMUSE.ENG. */
typedef void (*ImSustainCmdFunc)(void);

static const ImSustainCmdFunc s_jumpOpenCmdFunc[9] = {
	(ImSustainCmdFunc)sustain_jumpOpen_NoteOff, (ImSustainCmdFunc)sustain_jumpOpen_NoteOn, 0, 0, 0, 0, 0, 0,
	(ImSustainCmdFunc)sustain_metaTrackEnd,
};

static const ImSustainCmdFunc s_jumpMainCmdFunc[9] = {
	0, (ImSustainCmdFunc)sustain_jump_NoteOn, 0, 0, 0, 0, 0, 0, (ImSustainCmdFunc)sustain_metaTrackEnd,
};

/* ===== Init / Deinit ===== */

static void sustain_resetPool(imuse_t* im) {
	im->sustain.freeList = 0;
	im->sustain.activeList = 0;
	for (int i = 0; i < SUSTAIN_POOL_SIZE; ++i) {
		im->sustain.pool[i].prev = 0;
		im->sustain.pool[i].next = 0;
		ImUtils_ListAddItem(im, &im->sustain.freeList, &im->sustain.pool[i]);
	}
}

int ImSustain_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "SUSTAIN module...");
	sustain_resetPool(im);
	return 0;
}

int ImSustain_Deinit(imuse_t* im) {
	/* Identical to Init without the log. The pool is static, so
	 * there's nothing to free — we just rebuild the free list
	 * from every slot, implicitly reclaiming any still-active
	 * nodes. */
	sustain_resetPool(im);
	return 0;
}

/* ===== Active-list maintenance ===== */

void ImSustain_ClearForPlayer(imuse_t* im, struct ImMidiPlayer* player) {
	/* cur/next cache pattern: removing cur from the list
	 * invalidates cur->next mid-iteration. */
	for (ImSustainedSound* cur = im->sustain.activeList; cur;) {
		ImSustainedSound* next = cur->next;
		if (cur->midiPlayer == player) {
			ImUtils_ListRemoveItem(im, &im->sustain.activeList, cur);
			ImUtils_ListAddItem(im, &im->sustain.freeList, cur);
		}
		cur = next;
	}
}

void ImSustain_Update(imuse_t* im) {
	for (ImSustainedSound* cur = im->sustain.activeList; cur;) {
		ImSustainedSound* next = cur->next;

		/* Integrate the owning player's fixed-point tick rate.
		 * stepFixed is 16.16 ticks-per-PIT. Accumulate; move
		 * the whole-tick overflow out into curTick; keep only
		 * the fractional part in curTickFixed. */
		unsigned int newFixed =
			(unsigned int)cur->midiPlayer->seqData->stepFixed + (unsigned int)cur->curTickFixed;
		cur->curTickFixed = (int32_t)newFixed;
		cur->curTick -= (int)(newFixed >> 16);
		cur->curTickFixed &= 0xFFFF;

		if (cur->curTick < 0) {
			/* Sustain duration elapsed — emit the delayed Note-Off
			 * and return the node to the free pool. */
			ImPlayers_MidiNoteOff(im, cur->midiPlayer, cur->channelId, cur->note);
			ImUtils_ListRemoveItem(im, &im->sustain.activeList, cur);
			ImUtils_ListAddItem(im, &im->sustain.freeList, cur);
		}
		cur = next;
	}
}

/* ===== Silent-scan MIDI walker ===== */

/* Read a MIDI variable-length quantity. Returns the decoded
 * value; *cursor is advanced past the terminating byte. */
static int sustain_readVlq(const unsigned char** cursor) {
	int v = *(*cursor)++;
	if (v & 0x80) {
		v &= 0x7F;
		int b;
		do {
			b = *(*cursor)++;
			v = (v << 7) | (b & 0x7F);
		} while (b & 0x80);
	}
	return v;
}

/* Walk the sequence at (seq->chunkPtr + chunkOffset + sndData),
 * dispatching each event into midiCmdFunc[]. Matches the DOS
 * ImSustain_ProcessSustainJump — same VLQ/status decoding, same
 * 9-slot dispatch (channel 0..6, sysex 7, meta 8), same
 * accumulated sustain_midiTickDelta. */
static void sustain_processJump(imuse_t* im, struct ImSeqData* seq, const void* sndData,
								const ImSustainCmdFunc* midiCmdFunc, struct ImMidiPlayer* player) {
	im->sustain.midiTrackEnd = 0;

	/* chunkPtr + chunkOffset are byte offsets inside the sound
	 * buffer pointed at by sndData. */
	const unsigned char* evPtr = (const unsigned char*)sndData + seq->chunkPtr + seq->chunkOffset;

	im->sustain.midiTickDelta = ImSeq_MidiGetTickDelta(im, seq, seq->curTick, seq->nextTick);

	while (!im->sustain.midiTrackEnd) {
		unsigned char status = *evPtr;
		int evLen;

		if (status < 0xF0 && (status & 0x80)) {
			/* Channel message: (status & 0x70) >> 4 = cmdIdx. */
			int cmdIdx = (status & 0x70) >> 4;
			ImSustainCmdFunc slot = midiCmdFunc[cmdIdx];
			if (slot) {
				/* Channel handlers are (im, player, chanId, d1, d2). */
				((void (*)(imuse_t*, struct ImMidiPlayer*, int, int, int))slot)(im, player, status & 0x0F,
																				evPtr[1], evPtr[2]);
			}
			evLen = c_midiMsgSize[cmdIdx];
			evPtr += evLen;
		} else if (status == 0xF0) {
			/* SysEx: [0xF0][VLQ len][body]. Dispatch with a
			 * pointer to the 0xF0 (DOS behaviour) then advance
			 * past the status byte, the VLQ length, and the body. */
			ImSustainCmdFunc slot = midiCmdFunc[7];
			if (slot)
				((void (*)(imuse_t*, struct ImMidiPlayer*, const unsigned char*))slot)(im, player, evPtr);
			++evPtr;
			int payloadLen = sustain_readVlq(&evPtr);
			evPtr += payloadLen;
		} else if (status == 0xFF) {
			/* Meta: [0xFF][type][VLQ len][body]. Step past the
			 * 0xFF, dispatch with a pointer to the type byte,
			 * then advance past type + length + body. */
			++evPtr;
			ImSustainCmdFunc slot = midiCmdFunc[8];
			if (slot)
				((void (*)(imuse_t*, struct ImMidiPlayer*, const unsigned char*))slot)(im, player, evPtr);
			++evPtr;
			int payloadLen = sustain_readVlq(&evPtr);
			evPtr += payloadLen;
		} else {
			ImDebug_LogMsg(im, "ERROR:su unknown msg type 0x%x...", (unsigned int)status);
			return;
		}

		/* Termination check before reading dt: the event we just
		 * processed (end-of-track meta, or the final Note-Off in
		 * the Open pass) may have set im->sustain.midiTrackEnd. The DOS
		 * Watcom build always ran the dt read and let the
		 * top-of-loop check exit on the next pass; that worked
		 * because heap slack made the dt byte readable. On tight
		 * allocators it overruns the buffer, so bail here. */
		if (im->sustain.midiTrackEnd)
			break;

		/* Delta-time to the next event. */
		int dt = sustain_readVlq(&evPtr);
		im->sustain.midiTickDelta += dt;
	}
}

/* ===== Dispatch handlers ===== */

static void sustain_jumpOpen_NoteOff(imuse_t* im, struct ImMidiPlayer* player, int channelId, int note,
									 int data2) {
	(void)data2;
	unsigned int noteBits = im->sustain.curMidiNoteMask[note];
	unsigned int chanBit = im->sustain.midiSustainChannelMask[channelId];
	if (!(chanBit & noteBits))
		return; /* not a note we're tracking */

	/* Consume the bit from the live mask. */
	im->sustain.curMidiNoteMask[note] = noteBits & ~chanBit;
	if (--im->sustain.curNoteCount == 0)
		im->sustain.midiTrackEnd = 1; /* everything accounted for */

	/* Allocate a node from the free pool; on empty pool log and
	 * silently drop (ImSustain_Jump's force-kill fallback will
	 * catch the note). */
	ImSustainedSound* node = im->sustain.freeList;
	if (!node) {
		ImDebug_LogMsg(im, "ERROR:su unable to alloc Sustain...");
		return;
	}
	ImUtils_ListRemoveItem(im, &im->sustain.freeList, node);
	ImUtils_ListAddItem(im, &im->sustain.activeList, node);
	node->midiPlayer = player;
	node->note = note;
	node->channelId = channelId;
	node->curTick = im->sustain.midiTickDelta;
	node->curTickFixed = 0;
}

static void sustain_jumpOpen_NoteOn(imuse_t* im, struct ImMidiPlayer* player, int channelId, int note,
									int velocity) {
	/* MIDI convention: NoteOn vel=0 is a NoteOff. Real NoteOns
	 * during the Open pass don't affect the bridge (we only care
	 * about closing already-sounding notes). */
	if (!velocity)
		sustain_jumpOpen_NoteOff(im, player, channelId, note, 0);
}

static void sustain_jump_NoteOn(imuse_t* im, struct ImMidiPlayer* player, int channelId, int note,
								int velocity) {
	(void)velocity;

	/* Past the longest sustain: nothing more can match, stop. */
	if (im->sustain.midiTickDelta > im->sustain.trackTicksRemaining) {
		im->sustain.midiTrackEnd = 1;
		return;
	}

	unsigned int chanBit = im->sustain.midiSustainChannelMask[channelId];
	if (!(im->sustain.curMidiNoteMask[note] & chanBit))
		return; /* new note, no sustain obligation */

	/* Consume the bit — we've handled it. */
	im->sustain.curMidiNoteMask[note] &= ~chanBit;

	/* Find the node tracking this (player, note, channel) and
	 * trim its curTick to crossfade around the new NoteOn.
	 * Nodes whose curTick is already < (tickDelta - 10) end
	 * before the new NoteOn and don't need shortening. */
	int target = im->sustain.midiTickDelta - SUSTAIN_CROSSFADE_TICKS;
	for (ImSustainedSound* node = im->sustain.activeList; node; node = node->next) {
		if (node->midiPlayer == player && node->note == note && node->channelId == channelId &&
			node->curTick >= target) {
			node->curTick = target;
			return;
		}
	}
}

static void sustain_metaTrackEnd(imuse_t* im, struct ImMidiPlayer* player, const unsigned char* metaData) {
	(void)player;
	/* Meta 0x2F = end-of-track. Halt the scanner; other meta
	 * types are irrelevant to the sustain bridge. */
	if (*metaData == 0x2F)
		im->sustain.midiTrackEnd = 1;
}

/* ===== Jump entry point ===== */

void ImSustain_Jump(imuse_t* im, struct ImMidiPlayer* player, const void* sndData,
					struct ImSeqData* playerPrevData, struct ImSeqData* playerNextData) {
	/* Phase 1: per-channel mask from the player's virtual
	 * channels. A channel with no bound physical part can't have
	 * sounding notes, so its mask stays zero. */
	for (int ch = 0; ch < 16; ++ch) {
		struct ImPart* part = player->channels[ch].data;
		im->sustain.midiSustainChannelMask[ch] = part ? (1u << part->outChannelId) : 0u;
	}

	/* Phase 2: snapshot currently-sounding notes. */
	ImSlots_GetPlayerNotes(im, player, im->sustain.curMidiNoteMask, &im->sustain.curNoteCount);

	/* Phase 3: already-tracked notes don't need new sustain
	 * entries. Clear each matching bit from the live mask. */
	for (ImSustainedSound* cur = im->sustain.activeList; cur && im->sustain.curNoteCount; cur = cur->next) {
		if (cur->midiPlayer != player)
			continue;
		unsigned int chanBit = im->sustain.midiSustainChannelMask[cur->channelId];
		unsigned int noteBits = im->sustain.curMidiNoteMask[cur->note];
		if (chanBit & noteBits) {
			im->sustain.curMidiNoteMask[cur->note] = noteBits & ~chanBit;
			--im->sustain.curNoteCount;
		}
	}

	/* Phase 4: Walk the OLD sequence for Note-Offs that close
	 * the remaining uncovered notes. Each match creates a
	 * sustain node with curTick = ticks-until-NoteOff. */
	if (im->sustain.curNoteCount) {
		sustain_processJump(im, playerPrevData, sndData, s_jumpOpenCmdFunc, player);

		/* Phase 5: if any notes STILL uncovered (source sequence
		 * missing Note-Offs), log and force-kill them. */
		if (im->sustain.curNoteCount) {
			ImDebug_LogMsg(im, "ERR: su couldn't find all note-offs...");
			for (int note = 0; note < 128; ++note) {
				if (!im->sustain.curMidiNoteMask[note])
					continue;
				for (int ch = 0; ch < 16; ++ch) {
					if (im->sustain.curMidiNoteMask[note] & im->sustain.midiSustainChannelMask[ch]) {
						ImDebug_LogMsg(im, "missing note %d on chan %d...", note, ch + 1);
						ImPlayers_MidiNoteOff(im, player, ch, note);
					}
				}
			}
		}
	}

	/* Phase 6: remember how far ImSustain_Update / the main scan
	 * need to look — the longest outstanding sustain. */
	im->sustain.trackTicksRemaining = 0;
	for (ImSustainedSound* node = im->sustain.activeList; node; node = node->next) {
		if (node->curTick > im->sustain.trackTicksRemaining)
			im->sustain.trackTicksRemaining = node->curTick;
	}

	/* Phase 7: re-snapshot (the pre-jump scan may have moved
	 * notes between banks) and walk the NEW sequence for
	 * Note-Ons that correspond to held notes → crossfade trim. */
	ImSlots_GetPlayerNotes(im, player, im->sustain.curMidiNoteMask, &im->sustain.curNoteCount);
	sustain_processJump(im, playerNextData, sndData, s_jumpMainCmdFunc, player);
}
