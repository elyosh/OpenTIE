#include "internal/players.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/fades.h"
#include "internal/midi.h"
#include "internal/seq.h"
#include "internal/slots.h"
#include "internal/sustain.h"
#include "internal/triggers.h"
#include "internal/utils.h"
#include <imuse/commands.h>
#include <imuse/groups.h>

#include <string.h>

/* Owns the two MIDI players and maps their virtual channels to voice pairs. */

/* ===== Module-private state ===== */

/* Reverse-priority scan order for GetParam dispatch. table[k] matches,
 * tblRem = (23 - k) indexes into the switch: case 1 = soundType,
 * case 23 = midiPartPgm. Verbatim from IMUSE.ENG players_getParamCatLookup. */
static const int s_getParamCatLookup[23] = { 0x1700, 0x1600, 0x1500, 0x1400, 0x1300, 0x1200, 0x1100, 0x0F00,
											 0x0E00, 0x0D00, 0x0C00, 0x0B00, 0x0A00, 0x0900, 0x0800, 0x0700,
											 0x0600, 0x0500, 0x0400, 0x0300, 0x0200, 0x0100, 0x0000 };

/* ===== Lifecycle ===== */

int ImPlayers_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "PLAYERS module...");
	im->players.list = 0;
	for (int i = 0; i < 2; ++i) {
		ImMidiPlayer* p = &im->players.players[i];
		p->prev = 0;
		p->next = 0;
		ImSeqData* seq = ImSeq_GetSeqDataForPlayerId(im, i);
		p->seqData = seq;
		seq->player = p;
		p->soundId = 0;
	}
	return 0;
}

int ImPlayers_Deinit(imuse_t* im) {
	ImPlayers_StopAllSounds(im);
	return 0;
}

/* ===== Save / Restore ===== */

int ImPlayers_Save(imuse_t* im, void* buf, int size) {
	ImMidi_Lock(im);
	int used = ImSeq_Save(im, buf, size);
	if (used < 0) {
		ImMidi_Unlock(im);
		return used;
	}
	if ((unsigned int)(size - used) < sizeof(im->players.players)) {
		ImMidi_Unlock(im);
		return -5;
	}
	memcpy((char*)buf + used, im->players.players, sizeof(im->players.players));
	ImMidi_Unlock(im);
	return used + (int)sizeof(im->players.players);
}

int ImPlayers_Restore(imuse_t* im, void* buf) {
	/* Six-phase restore:
	 *   1. Restore seq state + memcpy players blob from buf.
	 *   2. Rewire seq↔player back-pointers; rebuild live list.
	 *   3. Reallocate parts for live players (MidiSetupParts #1).
	 *   4. Rebuild share-parts via saved sharedSoundId; second
	 *      MidiSetupParts so share pairs re-converge.
	 *   5. Save current state into buf's tail as scratch.
	 *   6. Scan each live player twice (from 1:1:0 to saved pos)
	 *      so accumulated CC state replays through the driver.
	 *   7. Re-restore from scratch to rewind the sequencer cursor. */
	ImMidi_Lock(im);
	im->players.list = 0;

	int seqLen = ImSeq_Restore(im, buf);
	memcpy(im->players.players, (char*)buf + seqLen, sizeof(im->players.players));

	for (int i = 0; i < 2; ++i) {
		ImMidiPlayer* p = &im->players.players[i];
		p->prev = 0;
		p->next = 0;
		ImSeqData* seq = ImSeq_GetSeqDataForPlayerId(im, i);
		p->seqData = seq;
		seq->player = p;
		if (p->soundId) {
			for (int c = 0; c < 16; ++c)
				p->channels[c].data = 0;
			ImUtils_ListAddItem(im, &im->players.list, p);
		}
	}

	ImPlayers_MidiSetupParts(im);

	for (ImMidiPlayer* p = im->players.list; p; p = p->next)
		p->sharedPartPlayer = ImPlayers_GetSoundPlayer(im, p->sharedSoundId);

	ImPlayers_MidiSetupParts(im);

	/* Scratch snapshot for the scan replay. 0x7000 matches the
	 * DOS budget (the 10 000-byte save region leaves enough tail). */
	int scratchLen = ImSeq_Save(im, buf, 0x7000);
	memcpy((char*)buf + scratchLen, im->players.players, sizeof(im->players.players));

	for (ImMidiPlayer* p = im->players.list; p; p = p->next) {
		int chunk = ImSeq_GetTimeParam(im, p->seqData, IMUSE_PARAM_MIDI_CHUNK);
		int measure = ImSeq_GetTimeParam(im, p->seqData, IMUSE_PARAM_MIDI_MEASURE);
		int beat = ImSeq_GetTimeParam(im, p->seqData, IMUSE_PARAM_MIDI_BEAT);
		int tick = ImSeq_GetTimeParam(im, p->seqData, IMUSE_PARAM_MIDI_TICK);
		ImSeq_Scan(im, p->seqData, chunk, 1, 1, 0);
		ImSeq_Scan(im, p->seqData, chunk, measure, beat, tick);
	}

	int finalSeqLen = ImSeq_Restore(im, buf);
	memcpy(im->players.players, (char*)buf + finalSeqLen, sizeof(im->players.players));

	ImMidi_Unlock(im);
	return seqLen + (int)sizeof(im->players.players);
}

/* ===== Group-volume propagation ===== */

void ImPlayers_ApplyGroupVol(imuse_t* im) {
	for (ImMidiPlayer* p = im->players.list; p; p = p->next) {
		int scaled = p->volume + 1;
		p->groupVolume = (scaled * imuse_get_group_volume(im, p->group)) >> 7;
		for (int c = 0; c < 16; ++c)
			ImPlayers_HandleChannelVolumeChange(im, p, &p->channels[c]);
	}
}

/* ===== Per-tick ===== */

void ImPlayers_Update(imuse_t* im) {
	/* Cache next up-front: ImSeq_Update may call back into
	 * StopPlayer (on fatal error / end-of-track) and unlink the
	 * current node. */
	for (ImMidiPlayer* p = im->players.list; p;) {
		ImMidiPlayer* next = p->next;
		ImSeq_Update(im, p->seqData);
		p = next;
	}
}

/* ===== Sound control ===== */

int ImPlayers_StartSound(imuse_t* im, intptr_t soundId, int priority) {
	int pri = ImUtils_Clamp(im, priority, 0, 127);
	ImMidiPlayer* p = ImPlayers_GetFreePlayer(im, pri);
	if (!p)
		return -6;

	/* Reset player-wide state. marker = -1 (distinct from 0..127
	 * and the 128 "large marker"). Defaults: music group, full
	 * volume, centered pan, zero transpose/detune. */
	p->sharedPartPlayer = 0;
	p->sharedSoundId = 0;
	p->marker = -1;
	p->group = 3; /* IMUSE_GROUP_MUSIC */
	p->priority = pri;
	p->volume = 127;
	p->groupVolume = imuse_get_group_volume(im, p->group);
	p->pan = 64;
	p->detune = 0;
	p->transpose = 0;
	p->mailbox = 0;
	p->hook = 0;

	/* partPgm=128 is the "no program yet" sentinel; partStatus=0
	 * keeps the channel virtual until the first ProgramChange. */
	for (int c = 0; c < 16; ++c) {
		ImMidiOutChannel* ch = &p->channels[c];
		ch->data = 0;
		ch->partStatus = 0;
		ch->partPgm = 128;
		ch->partTrim = 127;
		ch->partPriority = 0;
		ch->priority = p->priority;
		ch->partNoteReq = 1;
		ch->partVolume = 127;
		ch->groupVolume = p->groupVolume;
		ch->partPan = 64;
		ch->modulation = 0;
		ch->sustain = 0;
		ch->pitchBend = 0;
		ch->pbRange = 2;
		ch->effectiveDetune = 0;
	}

	if (ImSeq_StartSound(im, p->seqData, soundId) != 0)
		return -1;

	/* Only after the soundfile loads do we commit the slot. A
	 * failed load leaves the slot "free" (soundId stays 0). */
	p->soundId = soundId;
	ImUtils_ListAddItem(im, &im->players.list, p);
	return 0;
}

int ImPlayers_StopSound(imuse_t* im, intptr_t soundId) {
	int result = -1;
	ImMidiPlayer* p;
	while ((p = ImPlayers_GetSoundPlayer(im, soundId)) != 0) {
		ImPlayers_StopPlayer(im, p);
		result = 0;
	}
	return result;
}

int ImPlayers_StopAllSounds(imuse_t* im) {
	while (im->players.list)
		ImPlayers_StopPlayer(im, im->players.list);
	return 0;
}

intptr_t ImPlayers_FindNextSound(imuse_t* im, intptr_t soundId) {
	intptr_t smallestGreater = 0;
	for (ImMidiPlayer* p = im->players.list; p; p = p->next) {
		intptr_t cur = p->soundId;
		if ((uintptr_t)soundId < (uintptr_t)cur &&
			(!smallestGreater || (uintptr_t)smallestGreater > (uintptr_t)cur))
			smallestGreater = cur;
	}
	return smallestGreater;
}

void ImPlayers_StopPlayer(imuse_t* im, ImMidiPlayer* player) {
	/* Order matters:
	 *   1. Unlink first so updates skip the dying player.
	 *   2. Cancel fades/triggers/sustain for this soundId.
	 *   3. Signal sequencer to bail at next iteration.
	 *   4. Release all 16 channel→part bindings.
	 *   5. Clear soundId + break share-parts link atomically.
	 *   6. MidiSetupParts so freed parts get reassigned. */
	ImUtils_ListRemoveItem(im, &im->players.list, player);
	ImFades_DisableForSound(im, player->soundId, -1);
	ImTriggers_Clear(im, player->soundId, -1, -1);
	ImSustain_ClearForPlayer(im, player);
	ImSeq_SetEndOfTrack(im);

	for (int c = 0; c < 16; ++c)
		ImPlayers_ResetMidiOutChannel(im, &player->channels[c]);

	player->soundId = 0;

	ImMidiPlayer* partner = player->sharedPartPlayer;
	if (partner) {
		partner->sharedPartPlayer = 0;
		partner->sharedSoundId = 0;
	}
	ImPlayers_MidiSetupParts(im);
}

/* ===== Parameter dispatch ===== */

int ImPlayers_SetParam(imuse_t* im, intptr_t soundId, int param, int value) {
	ImMidiPlayer* p = ImPlayers_GetSoundPlayer(im, soundId);
	if (!p)
		return -4;
	int kind = param & IMUSE_PARAM_KIND_MASK;
	switch (kind) {
		case IMUSE_PARAM_SOUND_GROUP:
			if ((unsigned int)value >= 0x10u)
				return -5;
			p->group = value;
			p->groupVolume = ((p->volume + 1) * imuse_get_group_volume(im, value)) >> 7;
			for (int c = 0; c < 16; ++c)
				ImPlayers_HandleChannelVolumeChange(im, p, &p->channels[c]);
			return 0;

		case IMUSE_PARAM_SOUND_PRIORITY:
			if ((unsigned int)value > 0x7Fu)
				return -5;
			p->priority = value;
			for (int c = 0; c < 16; ++c)
				ImPlayers_HandleChannelPriorityChange(im, p, &p->channels[c]);
			ImPlayers_MidiSetupParts(im);
			return 0;

		case IMUSE_PARAM_SOUND_VOL:
			if ((unsigned int)value > 0x7Fu)
				return -5;
			p->volume = value;
			p->groupVolume = ((value + 1) * imuse_get_group_volume(im, p->group)) >> 7;
			for (int c = 0; c < 16; ++c)
				ImPlayers_HandleChannelVolumeChange(im, p, &p->channels[c]);
			return 0;

		case IMUSE_PARAM_SOUND_PAN:
			if ((unsigned int)value > 0x7Fu)
				return -5;
			p->pan = value;
			/* Composite pan: per-channel pan preserves offset from player. */
			for (int c = 0; c < 16; ++c) {
				ImMidiOutChannel* ch = &p->channels[c];
				if (ch->data) {
					int finalPan = ImUtils_Clamp(im, value + ch->partPan - 64, 0, 127);
					ImSlots_PartSetPan(im, ch->data, finalPan);
				}
			}
			return 0;

		case IMUSE_PARAM_SOUND_DETUNE:
			if (value < -9216 || value > 9216)
				return -5;
			p->detune = value;
			for (int c = 0; c < 16; ++c)
				ImPlayers_HandleChannelDetuneChange(im, p, &p->channels[c]);
			return 0;

		case IMUSE_PARAM_SOUND_TRANSPOSE:
			if (value < -12 || value > 12)
				return -5;
			/* Zero = RESET, not "add zero". Lets the game nudge
			 * transpose incrementally without tracking state. */
			p->transpose = value ? ImUtils_WrapSemitones(im, p->transpose + value, -12, 12) : 0;
			for (int c = 0; c < 16; ++c)
				ImPlayers_HandleChannelDetuneChange(im, p, &p->channels[c]);
			return 0;

		case IMUSE_PARAM_SOUND_MAILBOX:
			p->mailbox = value;
			return 0;

		case IMUSE_PARAM_MIDI_SPEED:
			return ImSeq_SetSpeed(im, p->seqData, value);

		case IMUSE_PARAM_MIDI_PART_TRIM: {
			int chanIdx = (unsigned char)param;
			if (chanIdx >= 16 || (unsigned int)value > 0x7Fu)
				return -5;
			p->channels[chanIdx].partTrim = value;
			ImPlayers_HandleChannelVolumeChange(im, p, &p->channels[chanIdx]);
			return 0;
		}

		default:
			ImDebug_LogMsg(im, "ERR: PlSetParam() couldn't set param %lu...", (unsigned long)param);
			return -5;
	}
}

int ImPlayers_GetParam(imuse_t* im, intptr_t soundId, int param) {
	/* Walk the list; count matches along the way for soundPlayCount. */
	int liveCount = 0;
	ImMidiPlayer* p = im->players.list;
	for (;;) {
		if (!p)
			return (param == IMUSE_PARAM_SOUND_PLAY_COUNT) ? liveCount : -4;
		if (p->soundId == soundId)
			break;
		p = p->next;
	}

	int kind = param & IMUSE_PARAM_KIND_MASK;
	if (kind > 0x1700)
		return -5;

	int tblRem = 0;
	for (int i = 0; i < 23; ++i) {
		if (s_getParamCatLookup[i] == kind) {
			tblRem = 23 - i;
			break;
		}
	}

	switch (tblRem) {
		case 0:
			return -5;
		case 1:
			return -1; /* soundType — top-level handles */
		case 2: {      /* soundPlayCount — count all matches */
			int count = 1;
			for (ImMidiPlayer* q = p->next; q; q = q->next)
				if (q->soundId == soundId)
					++count;
			return count;
		}
		case 3:
			return -1; /* soundPendCount — top-level handles */
		case 4:
			return p->marker;
		case 5:
			return p->group;
		case 6:
			return p->priority;
		case 7:
			return p->volume;
		case 8:
			return p->pan;
		case 9:
			return p->detune;
		case 10:
			return p->transpose;
		case 11:
			return p->mailbox;
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
			return ImSeq_GetTimeParam(im, p->seqData, param);
		case 17:
		case 18:
		case 19:
		case 20:
		case 21:
		case 22:
		case 23: {
			int chanIdx = (unsigned char)param;
			if (chanIdx >= 16)
				return -5;
			ImMidiOutChannel* ch = &p->channels[chanIdx];
			switch (tblRem) {
				case 17:
					return ch->partTrim;
				case 18:
					return ch->partStatus != 0;
				case 19:
					return ch->partPriority;
				case 20:
					return ch->partNoteReq;
				case 21:
					return ch->partVolume;
				case 22:
					return ch->partPan;
				case 23:
					return ch->partPgm;
			}
			/* unreachable */
			return -5;
		}
	}
	return -5;
}

int ImPlayers_SetHook(imuse_t* im, intptr_t soundId, uint32_t hookId) {
	if (hookId > 128u)
		return -5;
	ImMidiPlayer* p = ImPlayers_GetSoundPlayer(im, soundId);
	if (!p)
		return -4;
	p->hook = (int)hookId;
	return 0;
}

int ImPlayers_GetHook(imuse_t* im, intptr_t soundId) {
	ImMidiPlayer* p = ImPlayers_GetSoundPlayer(im, soundId);
	return p ? p->hook : -4;
}

/* ===== Silence primitives ===== */

void ImPlayers_StopAllNotes(imuse_t* im, ImMidiPlayer* player) {
	/* Unlike ResetMidiOutChannel this keeps channel→part bindings.
	 * Used by Jump (sustain==0 path) and twice by Scan (around the
	 * silent replay). */
	for (int c = 0; c < 16; ++c) {
		ImPlayers_MidiCommand(im, player, c, 123 /* CC_ALL_NOTES_OFF */, 0);
		ImPlayers_MidiCommand(im, player, c, 1 /* CC_MODULATION   */, 0);
		ImPlayers_HandleChannelPitchBend(im, player, c, 0, 64);
	}
}

int ImPlayers_Panic(imuse_t* im) {
	/* Three passes of NoteOff through player[0] × every channel ×
	 * every note. "Hammer the bus" — ensures any stuck note gets
	 * the all-off, even if its originating player is gone. */
	for (int pass = 0; pass < 3; ++pass)
		for (int note = 0; note < 128; ++note)
			for (int ch = 0; ch < 16; ++ch)
				ImPlayers_MidiNoteOff(im, &im->players.players[0], ch, note);
	return 0;
}

/* ===== MIDI / sequencer opcodes ===== */

int ImPlayers_Jump(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick, int sustain) {
	ImMidiPlayer* p = ImPlayers_GetSoundPlayer(im, soundId);
	return p ? ImSeq_Jump(im, p->seqData, chunk, measure, beat, tick, sustain) : -4;
}

int ImPlayers_Scan(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick) {
	ImMidiPlayer* p = ImPlayers_GetSoundPlayer(im, soundId);
	return p ? ImSeq_Scan(im, p->seqData, chunk, measure, beat, tick) : -4;
}

int ImPlayers_SendMidiMsg(imuse_t* im, intptr_t soundId, int status, int data1, int data2) {
	ImMidiPlayer* p = ImPlayers_GetSoundPlayer(im, soundId);
	if (!p)
		return -4;
	/* ImSeq_HandleMidiMsg takes the address of the player slot so
	 * it can rebind mid-call (share-parts routing). seqData->player
	 * is the canonical slot. */
	return ImSeq_HandleMidiMsg(im, &p->seqData->player, status, data1, data2);
}

int ImPlayers_ShareParts(imuse_t* im, intptr_t sound1, intptr_t sound2) {
	ImMidiPlayer* p1 = ImPlayers_GetSoundPlayer(im, sound1);
	ImMidiPlayer* p2 = ImPlayers_GetSoundPlayer(im, sound2);
	if (!p1 || !p2 || p1->sharedPartPlayer || p2->sharedPartPlayer)
		return -1;
	p1->sharedPartPlayer = p2;
	p1->sharedSoundId = sound2;
	p2->sharedPartPlayer = p1;
	p2->sharedSoundId = sound1;
	ImPlayers_MidiSetupParts(im);
	return 0;
}

/* ===== MIDI event handlers (called from SEQ via HandleMidiMsg) ===== */

void ImPlayers_ProgramChange(imuse_t* im, ImMidiPlayer* player, int channelId, int program) {
	ImMidiOutChannel* ch = &player->channels[channelId];
	ch->partPgm = program;
	if (ch->partStatus) {
		if (ch->data)
			ImSlots_PartSetPgm(im, ch->data, program);
	} else {
		/* First PC on this channel activates it — rerun part
		 * allocation so the channel gets a physical slot. */
		ch->partStatus = 1;
		ImPlayers_MidiSetupParts(im);
	}
}

void ImPlayers_MidiNoteOn(imuse_t* im, ImMidiPlayer* player, int channelId, int note, int velocity) {
	ImMidiOutChannel* ch = &player->channels[channelId];
	if (!velocity) {
		/* MIDI running-status: NoteOn vel=0 is NoteOff. */
		ImPlayers_MidiNoteOff(im, player, channelId, note);
		return;
	}
	if (ch->partStatus) {
		if (ch->data)
			ImSlots_HandleNoteOn(im, ch->data, note, velocity);
	} else {
		/* Virtual channel — shared drum out-channel (MIDI ch 9). */
		ImSlots_HandleDrumNoteOn(im, ch->priority, ch->partNoteReq, ch->groupVolume, note, velocity);
	}
}

void ImPlayers_MidiNoteOff(imuse_t* im, ImMidiPlayer* player, int channelId, int note) {
	ImMidiOutChannel* ch = &player->channels[channelId];
	if (ch->partStatus) {
		if (ch->data)
			ImSlots_HandleNoteOff(im, ch->data, note);
	} else {
		ImSlots_NoteOffDrumChannel(im, note);
	}
}

void ImPlayers_MidiCommand(imuse_t* im, ImMidiPlayer* player, int channelIndex, int cc, int value) {
	ImMidiOutChannel* ch = &player->channels[channelIndex];
	switch (cc) {
		case 1: /* CC_MODULATION */
			ch->modulation = value;
			if (ch->data)
				ImSlots_PartSetModulation(im, ch->data, value);
			return;
		case 7: /* CC_VOLUME */
			ch->partVolume = value;
			ImPlayers_HandleChannelVolumeChange(im, player, ch);
			return;
		case 10: /* CC_PAN */
			ch->partPan = value;
			if (ch->data) {
				int clamped = ImUtils_Clamp(im, player->pan + value - 64, 0, 127);
				ImSlots_PartSetPan(im, ch->data, clamped);
			}
			return;
		case 16: /* CC_PBRANGE */
			if ((unsigned int)value > 0xCu)
				return;
			ch->pbRange = value;
			/* Re-center pitch bend to (0, 64) after range change. */
			ImPlayers_HandleChannelPitchBend(im, player, channelIndex, 0, 64);
			return;
		case 17: /* CC_PART_NOTEREQ */
			ch->partNoteReq = value;
			if (ch->data)
				ImSlots_PartSetNoteReq(im, ch->data, value);
			return;
		case 18: /* CC_PART_PRIORITY */
			ch->partPriority = value;
			ImPlayers_HandleChannelPriorityChange(im, player, ch);
			ImPlayers_MidiSetupParts(im);
			return;
		case 64: /* CC_SUSTAIN */
			ch->sustain = value;
			if (ch->data)
				ImSlots_SetChannelSustain(im, ch->data, value);
			return;
		case 123: /* CC_ALL_NOTES_OFF */
			ch->sustain = 0;
			if (ch->data)
				ImSlots_FreeMidiChannel(im, ch->data);
			return;
		default:
			return;
	}
}

void ImPlayers_HandleChannelPitchBend(imuse_t* im, ImMidiPlayer* player, int channelIndex, int data1,
									  int data2) {
	ImMidiOutChannel* ch = &player->channels[channelIndex];
	int signedBend = (data1 | (data2 << 7)) - 0x2000;
	if (ch->pbRange) {
		/* Conventional pitch bend: scale by range, store as detune
		 * in 8.8 fixed-point. */
		ch->pitchBend = (ch->pbRange * signedBend) >> 5;
		ImPlayers_HandleChannelDetuneChange(im, player, ch);
	} else {
		/* pbRange==0 — pitch-bend wheel drives volume instead. */
		ch->pitchBend = signedBend >> 6;
		ImPlayers_HandleChannelVolumeChange(im, player, ch);
	}
}

/* ===== Voice allocator ===== */

ImMidiPlayer* ImPlayers_GetFreePlayer(imuse_t* im, int priority) {
	for (int i = 0; i < 2; ++i)
		if (!im->players.players[i].soundId)
			return &im->players.players[i];

	ImDebug_LogMsg(im, "ERR: no spare player slot, will try to preempt...");

	/* '>=' means ties go to the LIFO-latest so newer sounds
	 * don't get preempted before older ones at equal priority. */
	int lowest = 127;
	ImMidiPlayer* victim = 0;
	for (ImMidiPlayer* p = im->players.list; p; p = p->next) {
		if (lowest >= p->priority) {
			lowest = p->priority;
			victim = p;
		}
	}
	if (!victim || lowest > priority)
		return 0;
	ImPlayers_StopPlayer(im, victim);
	return victim;
}

ImMidiPlayer* ImPlayers_GetSoundPlayer(imuse_t* im, intptr_t soundId) {
	for (ImMidiPlayer* p = im->players.list; p; p = p->next)
		if (p->soundId == soundId)
			return p;
	return 0;
}

static int channel_is_audible(const ImMidiPlayer* player, const ImMidiOutChannel* ch) {
	return player->groupVolume && ch->partTrim && ch->partVolume;
}

void ImPlayers_MidiSetupParts(imuse_t* im) {
	/* Voice-allocation pass. Idempotent; re-entered after every
	 * event that might change which channels want parts or the
	 * share-parts topology.
	 *
	 * Per iteration:
	 *   - Walk all (player,ch). For share pairs, reconcile silent
	 *     side with partner's physical voice.
	 *   - Track two candidates: outChannel (highest-priority
	 *     audible channel with no part — needs one) and chan
	 *     (lowest-priority channel with a part — potential steal
	 *     victim).
	 *   - If no outChannel: done.
	 *   - Else grab a free pair, or steal from chan when
	 *     outChannel strictly outranks (tie: resolved by
	 *     lastUpdateIsChan + player mismatch). */
	for (;;) {
		ImMidiPlayer* outChanPlayer = 0;
		ImMidiPlayer* chanPlayer = 0;
		ImMidiOutChannel* outChannel = 0;
		ImMidiOutChannel* chan = 0;
		int lastUpdateIsChan = 0;

		for (ImMidiPlayer* player = im->players.list; player; player = player->next) {
			for (int chIdx = 0; chIdx < 16; ++chIdx) {
				ImMidiOutChannel* iterCh = &player->channels[chIdx];
				ImMidiOutChannel* partnerCh =
					player->sharedPartPlayer ? &player->sharedPartPlayer->channels[chIdx] : 0;

				/* Share-parts reconciliation — silent side inherits
				 * its partner's physical voice via the sibling
				 * ImPart. This is what makes share-parts crossfades
				 * seamless. */
				if (partnerCh) {
					if (iterCh->data && !partnerCh->data && partnerCh->partStatus &&
						channel_is_audible(player->sharedPartPlayer, partnerCh)) {
						ImPlayers_AssignMidiChannel(im, player->sharedPartPlayer, partnerCh,
													iterCh->data->sharedMidiChannel);
					} else if (!iterCh->data && partnerCh->data && iterCh->partStatus &&
							   channel_is_audible(player, iterCh)) {
						ImPlayers_AssignMidiChannel(im, player, iterCh, partnerCh->data->sharedMidiChannel);
					}
				}

				/* Candidate tracking. Skip "with part" channels
				 * whose share partner holds a higher-priority part
				 * — don't break a share pair. */
				if (iterCh->data) {
					int partnerLocks = partnerCh && partnerCh->data && partnerCh->priority > iterCh->priority;
					if (!partnerLocks && (!chan || iterCh->priority <= chan->priority)) {
						chanPlayer = player;
						chan = iterCh;
						lastUpdateIsChan = 1;
					}
				} else if (iterCh->partStatus && channel_is_audible(player, iterCh)) {
					if (!outChannel || iterCh->priority > outChannel->priority) {
						outChanPlayer = player;
						outChannel = iterCh;
						lastUpdateIsChan = 0;
					}
				}
			}
		}

		if (!outChannel)
			return;

		ImPlayerParts* freePair = ImSlots_GetFreePlayerParts(im);
		if (freePair) {
			ImPlayers_AssignMidiChannel(im, outChanPlayer, outChannel, &freePair->part1);
			continue;
		}

		/* No free pair — try to steal `chan`. */
		int chanPri = chan ? chan->priority : 0;
		if (!chan || outChannel->priority <= chanPri) {
			if (!chan || chanPri != outChannel->priority)
				return;
			if (!lastUpdateIsChan)
				return;
			if (outChanPlayer == chanPlayer)
				return;
		}

		ImPart* stolen = chan->data;
		ImPlayers_ResetMidiOutChannel(im, chan);
		/* Reset the share-pair sibling too so both sides of a
		 * pair get cleaned up consistently. */
		ImPart* sibling = stolen->sharedMidiChannel;
		if (sibling && sibling->outChannel)
			ImPlayers_ResetMidiOutChannel(im, sibling->outChannel);
		ImPlayers_AssignMidiChannel(im, outChanPlayer, outChannel, stolen);
	}
}

void ImPlayers_AssignMidiChannel(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* outChannel,
								 ImPart* part) {
	outChannel->data = part;
	part->player = player;
	part->outChannel = outChannel;
	/* Push the channel's current state into the physical slot so
	 * the new binding is audible immediately. Uses groupVolume
	 * (post-scaled) and effectiveDetune (folded), not the raw
	 * per-channel values. */
	ImSlots_PartSetPgm(im, part, outChannel->partPgm);
	ImSlots_PartSetPriority(im, part, outChannel->priority);
	ImSlots_PartSetNoteReq(im, part, outChannel->partNoteReq);
	ImSlots_PartSetVolume(im, part, outChannel->groupVolume);
	ImSlots_PartSetPan(im, part, outChannel->partPan);
	ImSlots_PartSetModulation(im, part, outChannel->modulation);
	ImSlots_PartSetPitchBend(im, part, outChannel->effectiveDetune);
	ImSlots_SetChannelSustain(im, part, outChannel->sustain);
}

void ImPlayers_ResetMidiOutChannel(imuse_t* im, ImMidiOutChannel* channel) {
	ImPart* part = channel->data;
	if (!part)
		return;
	ImSlots_FreeMidiChannel(im, part);
	part->player = 0;
	part->outChannel = 0;
	channel->data = 0;
}

void ImPlayers_HandleChannelPriorityChange(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* channel) {
	int eff = player->priority + channel->partPriority;
	channel->priority = eff;
	if (channel->data)
		ImSlots_PartSetPriority(im, channel->data, eff);
}

void ImPlayers_HandleChannelVolumeChange(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* channel) {
	int trimPlus1;
	int modulatedVol;
	if (channel->pbRange) {
		/* Conventional: pitch-bend doesn't touch volume. */
		trimPlus1 = channel->partTrim + 1;
		modulatedVol = channel->partVolume;
	} else {
		/* pbRange==0 reuses pitch-bend as a volume modulator.
		 * Ratio-preserving: bend at max positive pushes volume to
		 * 127, bend at max negative pushes to 0. */
		int headroom = (channel->pitchBend >= 0) ? (127 - channel->partVolume) : channel->partVolume;
		modulatedVol = channel->partVolume + ((channel->pitchBend * (headroom + 1)) >> 7);
		trimPlus1 = channel->partTrim + 1;
	}
	channel->groupVolume = (unsigned int)(player->groupVolume * trimPlus1 * (modulatedVol + 1)) >> 14;

	if (player->groupVolume && channel->partTrim && channel->partVolume) {
		if (channel->data) {
			/* Audible + bound: push the new value. */
			ImSlots_PartSetVolume(im, channel->data, channel->groupVolume);
			return;
		}
		/* Audible + unbound: fall through to MidiSetupParts. */
	} else {
		/* Silent: release slot if bound; otherwise done. */
		if (!channel->data)
			return;
		ImPlayers_ResetMidiOutChannel(im, channel);
	}
	ImPlayers_MidiSetupParts(im);
}

void ImPlayers_HandleChannelDetuneChange(imuse_t* im, ImMidiPlayer* player, ImMidiOutChannel* channel) {
	/* transpose is whole semitones (<<8 matches pitchBend's 8.8
	 * fixed-point); detune is already 8.8. The slot converts to a
	 * 14-bit MIDI pitch-bend via (2 * value + 0x2000). */
	int eff = (player->transpose << 8) + channel->pitchBend + player->detune;
	channel->effectiveDetune = eff;
	if (channel->data)
		ImSlots_PartSetPitchBend(im, channel->data, eff);
}

/* The DOS debug overlay is unsupported. */
void ImPlayers_Debug(imuse_t* im) {}
