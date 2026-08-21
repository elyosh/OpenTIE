#include "internal/seq.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/files.h"
#include "internal/midi.h"
#include "internal/players.h"
#include "internal/slots.h"
#include "internal/sustain.h"
#include "internal/timer.h"
#include "internal/triggers.h"
#include "internal/utils.h"
#include <imuse/commands.h>

#include <stddef.h>
#include <string.h>

/* AdvanceMidi serves playback, jump, and scan using different dispatch tables. */

/* Forward decl — defined after the parser handlers that populate the tables. */
static const void* const s_midiCmdFunc[9];
static const void* const s_jumpMidiCmdFunc[9];
static const void* const s_scanMidiCmdFuncs[9];

/* ===== Module-private state ===== */

/* Scratch snapshots used by Jump/Scan. Module-global so a single
 * walker invocation can mutate playerNextState in place and
 * commit it back to the real slot on success. */

/* LucasArts SysEx length scratch; kept here to match the DOS
 * layout (module-global, not stack) even though it's only
 * touched by ImParser_HandleSysEx. */

/* MIDI channel-message sizes keyed by (status & 0x70) >> 4:
 *   0 NoteOff (3), 1 NoteOn (3), 2 PolyAT (3),
 *   3 CC (3), 4 PgmChange (2), 5 ChanAT (2), 6 PitchBend (3). */
static const unsigned char s_midiMsgSize[7] = { 3, 3, 3, 3, 2, 2, 3 };

/* MTrk chunk type literal for ImFiles_GotoChunk. */
static const char s_chunkTypeMTrk[5] = "MTrk";

/* ===== Init / save / restore ===== */

int ImSeq_Init(imuse_t* im) {
	memset(im->seq.data, 0, sizeof(im->seq.data));
	im->seq.isEndOfTrack = 0;
	return 0;
}

int ImSeq_Deinit(imuse_t* im) { return 0; }

ImSeqData* ImSeq_GetSeqDataForPlayerId(imuse_t* im, int playerId) {
	/* No bounds check — caller guarantees 0 or 1. */
	return &im->seq.data[playerId];
}

int ImSeq_Save(imuse_t* im, void* buf, int size) {
	if ((unsigned int)size < sizeof(im->seq.data))
		return -5;
	memcpy(buf, im->seq.data, sizeof(im->seq.data));
	return (int)sizeof(im->seq.data);
}

int ImSeq_Restore(imuse_t* im, void* buf) {
	memcpy(im->seq.data, buf, sizeof(im->seq.data));
	return (int)sizeof(im->seq.data);
}

/* ===== Lifecycle per-player ===== */

int ImSeq_StartSound(imuse_t* im, ImSeqData* data, intptr_t soundId) {
	void* sndData = ImFiles_GetSoundPtr(im, soundId);
	if (!sndData)
		return -4;

	data->soundId = soundId;
	data->tickAccum = 0;
	ImSeq_SetTempo(im, data, 500000);        /* 120 BPM */
	ImSeq_SetSpeed(im, data, 128);           /* 1.0x */
	ImSeq_SetTicksPerBeat(im, data, 480, 4); /* 480 tpqn, 4/4 */
	return ImSeq_GoToChunk(im, data, sndData, 0);
}

void ImSeq_SetEndOfTrack(imuse_t* im) { im->seq.isEndOfTrack = 1; }

/* ===== Time controls ===== */

int ImSeq_SetSpeed(imuse_t* im, ImSeqData* seq, int speed) {
	if ((unsigned int)speed > 0xFF)
		return -5;
	ImMidi_Lock(im);
	seq->speed = speed;
	seq->stepFixed = (seq->step * speed) >> 7;
	ImMidi_Unlock(im);
	return 0;
}

int ImSeq_SetTempo(imuse_t* im, ImSeqData* seq, int tempo) {
	/* step = (480 * usec_per_interrupt / tempo) << 16, but
	 * computed carefully to avoid 32-bit overflow on very slow
	 * tempi. Shift dividend+divisor in lockstep until both fit
	 * in 16 bits, then shift-and-divide. */
	unsigned int dividend = 480u * (unsigned int)IM_USEC_PER_INT;
	unsigned int divisor = (unsigned int)tempo;
	seq->tempo = tempo;
	while ((dividend & 0xFFFF0000u) || (divisor & 0xFFFF0000u)) {
		dividend >>= 1;
		divisor >>= 1;
	}
	seq->step = (int)((dividend << 16) / divisor);
	return ImSeq_SetSpeed(im, seq, seq->speed);
}

void ImSeq_SetTicksPerBeat(imuse_t* im, ImSeqData* seq, int ticksPerBeat, int beatsPerMeasure) {
	/* beatsPerMeasure is stored pre-shifted <<16 so FixupSoundTick
	 * can compare it directly against the beat bits of a packed
	 * position without re-shifting. */
	ImMidi_Lock(im);
	seq->ticksPerBeat = ticksPerBeat;
	seq->beatsPerMeasure = beatsPerMeasure << 16;
	ImMidi_Unlock(im);
}

int ImSeq_GetTimeParam(imuse_t* im, ImSeqData* seq, int param) {
	switch (param) {
		case IMUSE_PARAM_MIDI_CHUNK:
			return seq->seqIndex + 1;
		case IMUSE_PARAM_MIDI_MEASURE:
			return (int)((unsigned int)seq->nextTick >> 20) + 1;
		case IMUSE_PARAM_MIDI_BEAT:
			return (int)(((unsigned int)seq->nextTick >> 16) & 0xFu) + 1;
		case IMUSE_PARAM_MIDI_TICK:
			return (int)(unsigned short)seq->nextTick;
		case IMUSE_PARAM_MIDI_SPEED:
			return seq->speed;
		default:
			return -5;
	}
}

/* ===== Normalization + chunk positioning ===== */

int ImSeq_FixupSoundTick(imuse_t* im, ImSeqData* data, unsigned int value) {
	/* Wrap tick bits into (0, ticksPerBeat); on each wrap,
	 * increment the beat bits (offset 16). Beat bits wrap into
	 * (0, beatsPerMeasure-shifted); on each wrap, increment the
	 * measure bits (offset 20). */
	unsigned int result = value;
	for (;;) {
		unsigned int tpb = (unsigned int)data->ticksPerBeat;
		if ((result & 0xFFFFu) < tpb)
			break;
		result = result + 0x10000u - tpb;
		for (;;) {
			unsigned int bpm = (unsigned int)data->beatsPerMeasure;
			if ((result & 0xF0000u) < bpm)
				break;
			result = result + 0x100000u - bpm;
		}
	}
	return (int)result;
}

int ImSeq_GoToChunk(imuse_t* im, ImSeqData* data, void* sndData, int chunkId) {
	/* Find the chunkId-th MTrk header; -1 if not present. */
	char* hdr = (char*)ImFiles_GotoChunk(im, sndData, s_chunkTypeMTrk, chunkId);
	if (!hdr) {
		ImDebug_LogMsg(im, "ERR: Sq couldn't find chunk %d...", chunkId + 1);
		return -1;
	}

	data->seqIndex = chunkId;
	data->chunkOffset = (int)(hdr + 8 - (char*)sndData);

	/* Decode the initial VLQ delta-time immediately after the
	 * 8-byte chunk header. */
	unsigned char* cursor = (unsigned char*)(hdr + 9);
	unsigned int dt = (unsigned int)(unsigned char)hdr[8];
	if (dt & 0x80) {
		dt &= 0x7F;
		int b;
		do {
			b = *cursor++;
			dt = (dt << 7) | (unsigned int)(b & 0x7F);
		} while (b & 0x80);
	}

	data->curTick = ImSeq_FixupSoundTick(im, data, dt);
	data->nextTick = 0;
	data->chunkPtr = (int)((char*)cursor - ((char*)sndData + data->chunkOffset));
	return 0;
}

/* ===== MIDI walker =====
 *
 * Reads raw bytes from (sndData + chunkOffset + chunkPtr),
 * dispatches events whose accumulated tick < nextTick via the
 * caller-supplied 9-slot table, and commits the final cursor
 * back to chunkPtr / curTick on return.
 */

/* Read a MIDI VLQ. *cursor is advanced past the terminator. */
static unsigned int seq_readVlq(const unsigned char** cursor) {
	unsigned int v = *(*cursor)++;
	if (v & 0x80) {
		v &= 0x7F;
		int b;
		do {
			b = *(*cursor)++;
			v = (v << 7) | (unsigned int)(b & 0x7F);
		} while (b & 0x80);
	}
	return v;
}

void ImSeq_AdvanceMidi(imuse_t* im, ImSeqData* playerData, void* sndData, void** midiCmdFunc) {
	im->seq.isEndOfTrack = 0;
	unsigned int curTick = (unsigned int)playerData->curTick;
	unsigned int nextTick = (unsigned int)playerData->nextTick;
	unsigned char* evPtr = (unsigned char*)sndData + playerData->chunkOffset + playerData->chunkPtr;

	while (curTick < nextTick) {
		unsigned char status = *evPtr;
		int evLen;

		if (status < 0xF0 && (status & 0x80)) {
			/* Channel-voice: (status & 0x70) >> 4 is the handler
			 * index. Handlers use 5-arg calling convention:
			 * (im, player, channel, data1, data2). */
			int idx = (status & 0x70) >> 4;
			void* slot = midiCmdFunc[idx];
			if (slot) {
				((void (*)(imuse_t*, struct ImMidiPlayer*, int, int, int))slot)(
					im, playerData->player, status & 0x0F, evPtr[1], evPtr[2]);
			}
			evLen = s_midiMsgSize[idx];
			evPtr += evLen;
		} else if (status == 0xF0 || status == 0xFF) {
			/* SysEx (slot 7) or Meta (slot 8). SysEx dispatch
			 * gets a pointer to the 0xF0 status byte; Meta
			 * dispatch gets a pointer to the byte after the
			 * 0xFF (the meta-type). Afterwards advance over the
			 * VLQ-prefixed payload. */
			int metaIdx;
			if (status == 0xF0) {
				metaIdx = 7;
			} else {
				metaIdx = 8;
				++evPtr;
			}
			void* slot = midiCmdFunc[metaIdx];
			if (slot)
				((void (*)(imuse_t*, ImSeqData*, const unsigned char*))slot)(im, playerData, evPtr);

			/* Meta handler may have reloaded/moved the sound —
			 * resolve sndData again. If the sound disappeared,
			 * the player is dead; bail. */
			ptrdiff_t evOffset = (char*)evPtr - (char*)sndData;
			sndData = ImFiles_GetSoundPtr(im, playerData->player->soundId);
			if (!sndData) {
				ImDebug_LogMsg(im, "ERR: sq int handler got null addr...");
				return;
			}
			evPtr = (unsigned char*)sndData + evOffset;

			/* Payload length: VLQ. Advance cursor past it. */
			unsigned char* lenCursor = evPtr + 1;
			unsigned int payloadLen = *lenCursor++;
			if (payloadLen & 0x80) {
				payloadLen &= 0x7F;
				int b;
				do {
					b = *lenCursor++;
					payloadLen = (payloadLen << 7) | (unsigned int)(b & 0x7F);
				} while (b & 0x80);
			}
			evPtr = lenCursor;
			evLen = (int)payloadLen;
			evPtr += evLen;
			evLen = 0; /* evLen already consumed below */
		} else {
			/* Data byte where status was expected — iMuse files
			 * never use running status, so this is fatal. */
			ImDebug_LogMsg(im, "ERROR:sq unknown msg type 0x%x...", (unsigned int)status);
			ImPlayers_StopPlayer(im, playerData->player);
			return;
		}
		(void)evLen; /* silence unused-variable warn */

		/* End-of-track set by the meta handler breaks the walk
		 * cleanly — re-check before advancing to the next event. */
		if (im->seq.isEndOfTrack)
			return;

		/* Delta-time to the next event (VLQ). */
		unsigned int dt = seq_readVlq((const unsigned char**)&evPtr);
		curTick += dt;
		if ((curTick & 0xFFFFu) >= (unsigned int)playerData->ticksPerBeat)
			curTick = (unsigned int)ImSeq_FixupSoundTick(im, playerData, curTick);
	}

	playerData->curTick = (int)curTick;
	playerData->chunkPtr = (int)((char*)evPtr - ((char*)sndData + playerData->chunkOffset));
}

int ImSeq_MidiGetTickDelta(imuse_t* im, ImSeqData* playerData, int prevTick, int tick) {
	/* Linearise each packed position as
	 *   total = bpm_shifted * tpb * measure + tpb * beat + tick
	 * NOTE: beatsPerMeasure stored pre-shifted <<16, so the
	 * measure coefficient is 65536× too large for a true
	 * ticks-per-measure figure. Valid only when both positions
	 * share a measure (sustain bridge never crosses one).
	 * Arithmetic is unsigned to match the binary's 32-bit
	 * modular wraparound (asm uses shr + imul on unsigned
	 * bitfields). With realistic inputs the measure coefficient
	 * reaches ~1.3e8, so a signed multiply by a non-trivial
	 * measure index would overflow INT32_MAX → UB. The final
	 * subtraction's bit pattern is reinterpreted as a signed
	 * delta on return. */
	unsigned int tpb = (unsigned int)playerData->ticksPerBeat;
	unsigned int tpm = (unsigned int)playerData->beatsPerMeasure * tpb;

	unsigned int pTick = (unsigned int)prevTick;
	unsigned int nTick = (unsigned int)tick;
	unsigned int prevTotal =
		((pTick & 0xF0000u) >> 16) * tpb + tpm * (pTick >> 20) + (unsigned int)(unsigned short)pTick;
	unsigned int currTotal =
		((nTick & 0xF0000u) >> 16) * tpb + tpm * (nTick >> 20) + (unsigned int)(unsigned short)nTick;
	return (int)(prevTotal - currTotal);
}

/* ===== Per-tick driver ===== */

void ImSeq_Update(imuse_t* im, ImSeqData* seq) {
	/* Advance the fixed-point accumulator; whole-tick overflow
	 * extends nextTick into the future. */
	unsigned int newAccum = (unsigned int)seq->stepFixed + (unsigned int)seq->tickAccum;
	seq->tickAccum = (int)newAccum;
	seq->nextTick += (int)(newAccum >> 16);
	seq->tickAccum &= 0xFFFF;

	if ((unsigned int)(unsigned short)seq->nextTick >= (unsigned int)seq->ticksPerBeat)
		seq->nextTick = ImSeq_FixupSoundTick(im, seq, (unsigned int)seq->nextTick);

	/* Replay events now due. */
	if ((unsigned int)seq->curTick < (unsigned int)seq->nextTick) {
		void* sndData = ImFiles_GetSoundPtr(im, seq->soundId);
		if (sndData) {
			ImSeq_AdvanceMidi(im, seq, sndData, (void**)s_midiCmdFunc);
		} else {
			/* Sound evicted mid-playback — stop the player. */
			ImDebug_LogMsg(im, "ERR: sq int handler got null addr...");
			ImPlayers_StopPlayer(im, seq->player);
		}
	}
}

/* ===== Transport: Jump / Scan =====
 *
 * Both copy the slot into a Prev/Next scratch pair, pack the
 * target position, seek the Next cursor to (chunk, tick), and
 * run ImSeq_AdvanceMidi over a dispatch table. Jump uses the
 * 'silence-voices, carry-sustain' table; Scan mutes the driver
 * and replays voices so the hardware state matches the target
 * position.
 */

static int seq_commonSetupAndValidate(imuse_t* im, ImSeqData* data, int chunk, int measure, int beat,
									  unsigned int tick, void** sndDataOut, unsigned int* targetOut,
									  ImSeqData* prev, ImSeqData* next) {
	void* sndData = ImFiles_GetSoundPtr(im, data->soundId);
	if (!sndData)
		return -4;
	if ((unsigned int)(measure - 1) >= 0x3E8u)
		return -5;
	unsigned int beatIdx = (unsigned int)(beat - 1);
	if (beatIdx >= 0xCu || tick >= 0x1E0u)
		return -5;

	memcpy(prev, data, sizeof(*prev));
	memcpy(next, data, sizeof(*next));

	unsigned int packed = tick + (beatIdx << 16) + (unsigned int)(measure - 1) * 0x100000u;
	unsigned int target = (unsigned int)ImSeq_FixupSoundTick(im, data, packed);

	int chunkIdx = chunk - 1;
	if ((chunkIdx != next->seqIndex || target < (unsigned int)next->nextTick) &&
		ImSeq_GoToChunk(im, next, sndData, chunkIdx))
		return -1;

	*sndDataOut = sndData;
	*targetOut = target;
	return 0;
}

int ImSeq_Jump(imuse_t* im, ImSeqData* data, int chunk, int measure, int beat, int tick, int sustain) {
	ImMidi_Lock(im);

	void* sndData;
	unsigned int target;
	int r = seq_commonSetupAndValidate(im, data, chunk, measure, beat, (unsigned int)tick, &sndData, &target,
									   &im->seq.soundPrevState, &im->seq.soundNextState);
	if (r == -4 || r == -5) {
		ImMidi_Unlock(im);
		return r;
	}
	if (r == -1) {
		ImDebug_LogMsg(im, "ERR: Sq jump to invalid pos...");
		ImMidi_Unlock(im);
		return -1;
	}

	im->seq.soundNextState.nextTick = (int)target;
	ImSeq_AdvanceMidi(im, &im->seq.soundNextState, sndData, (void**)s_jumpMidiCmdFunc);
	if (im->seq.isEndOfTrack) {
		ImDebug_LogMsg(im, "ERR: Sq jump to invalid pos...");
		ImMidi_Unlock(im);
		return -1;
	}

	if (sustain) {
		/* Zero modulation/sustain/pitch-bend on every channel
		 * so the bridge starts from a clean controller state,
		 * then hand off to SUSTAIN. It scans both sequencer
		 * positions, ports any notes still ringing at the jump
		 * point into sustain-pool nodes, and lines them up
		 * against Note-Offs in the destination sequence. */
		for (int ch = 0; ch < 16; ++ch) {
			ImPlayers_MidiCommand(im, data->player, ch, /*CC_SUSTAIN*/ 64, 0);
			ImPlayers_MidiCommand(im, data->player, ch, /*CC_MODULATION*/ 1, 0);
			ImPlayers_HandleChannelPitchBend(im, data->player, ch, 0, 64);
		}
		ImSustain_Jump(im, data->player, sndData, &im->seq.soundPrevState, &im->seq.soundNextState);
	} else {
		ImPlayers_StopAllNotes(im, data->player);
	}

	memcpy(data, &im->seq.soundNextState, sizeof(*data));
	im->seq.isEndOfTrack = 1; /* break any outer walker */
	ImMidi_Unlock(im);
	return 0;
}

int ImSeq_Scan(imuse_t* im, ImSeqData* data, int chunk, int measure, int beat, int tick) {
	ImMidi_Lock(im);

	void* sndData;
	unsigned int target;
	int r = seq_commonSetupAndValidate(im, data, chunk, measure, beat, (unsigned int)tick, &sndData, &target,
									   &im->seq.scanPrevState, &im->seq.scanNextState);
	if (r == -4 || r == -5) {
		ImMidi_Unlock(im);
		return r;
	}
	if (r == -1) {
		ImDebug_LogMsg(im, "ERR: Sq scan to invalid pos...");
		ImMidi_Unlock(im);
		return -1;
	}

	/* Silence notes, mute the driver, replay voice events to
	 * rebuild hardware state, un-mute, re-trigger held notes. */
	ImPlayers_StopAllNotes(im, data->player);
	ImSlots_SetDrvIgnoreNotes(im);
	im->seq.scanNextState.nextTick = (int)target;
	ImSeq_AdvanceMidi(im, &im->seq.scanNextState, sndData, (void**)s_scanMidiCmdFuncs);

	if (im->seq.isEndOfTrack) {
		ImDebug_LogMsg(im, "ERR: Sq scan to invalid pos...");
		ImPlayers_StopAllNotes(im, data->player);
		ImSlots_ClearDrvIgnoreNotes(im);
		ImMidi_Unlock(im);
		return -1;
	}
	ImSlots_ClearDrvIgnoreNotes(im);
	ImSlots_RestartPlayerNotes(im, data->player);

	memcpy(data, &im->seq.scanNextState, sizeof(*data));
	im->seq.isEndOfTrack = 1;
	ImMidi_Unlock(im);
	return 0;
}

/* ===== MIDI handler dispatch tables ===== */

int ImSeq_HandleMidiMsg(imuse_t* im, struct ImMidiPlayer** playerSlot, int status, int data1, int data2) {
	/* Channel-voice messages only: 0x80..0xEF. System-common
	 * (0xF0+) and data bytes (< 0x80) are rejected. */
	if ((unsigned int)status >= 0xF0u || !(status & 0x80))
		return -5;
	int idx = (status & 0x70) >> 4;
	void* slot = (void*)s_midiCmdFunc[idx];
	if (slot) {
		((void (*)(imuse_t*, struct ImMidiPlayer*, int, int, int))slot)(im, *playerSlot, status & 0x0F, data1,
																		data2);
	}
	return 0;
}

/* ===== Small handlers (MidiPressure / meta jump-scan) ===== */

void ImSeq_MidiPressure(imuse_t* im) {
	/* Aftertouch is dropped — iMuse doesn't implement it. */
	ImDebug_LogMsg(im, "ERROR: hey, quit the aftertouch...");
}

void ImSeq_HandleMetaJumpScan(imuse_t* im, ImSeqData* playerData, const uint8_t* chunkData) {
	/* Scan/jump-mode meta handler. Only EoT/tempo/time-sig
	 * matter here — text, markers, key-sig, etc. are ignored. */
	uint8_t metaType = chunkData[0];
	if (metaType == 0x2F) {
		im->seq.isEndOfTrack = 1;
	} else if (metaType == 0x51) {
		/* Payload is 3 bytes big-endian µs/qn at chunkData[2..4]. */
		int tempo = (chunkData[2] << 16) | (chunkData[3] << 8) | chunkData[4];
		ImSeq_SetTempo(im, playerData, tempo);
	} else if (metaType == 0x58) {
		/* Numerator at [2], denominator power at [3]. */
		int numerator = chunkData[2];
		int denomPower = chunkData[3];
		ImSeq_SetTicksPerBeat(im, playerData, 960 >> (denomPower - 1), numerator);
	}
}

/* ===== LucasArts SysEx parser ===== */

void ImParser_HandleSysEx(imuse_t* im, ImSeqData* playerData, const uint8_t* chunkData) {
	/* Frame: [0]=0xF0, [1]=len, [2]=0x7D (LA mfg), [3]=subOp,
	 * [4..]=payload. Off-format frames are silently dropped. */
	if (chunkData[0] != 0xF0)
		return;
	im->seq.sysExLen = chunkData[1];
	if ((unsigned int)im->seq.sysExLen > 0x7Fu)
		return;
	if (chunkData[2] != 0x7D)
		return;

	const uint8_t* payload = chunkData + 4;
	uint8_t subOp = chunkData[3];

	if (subOp == 0) {
		/* Marker: payload[0] is markerId (0..127). */
		int markerId = payload[0];
		playerData->player->marker = markerId;
		ImTriggers_ProcessMarker(im, playerData->player->soundId, markerId);
	} else if (subOp == 1) {
		/* Jump hook. payload: [0]=trackHookId, then
		 * [chunk][measHi][measLo][beat][tickHi][tickLo][sustain].
		 * 14-bit values packed big-endian as (hi<<7)|lo. */
		if (!ImUtils_CheckHookValue(im, &playerData->player->hook, payload[0])) {
			const uint8_t* j = chunkData + 5;
			ImSeq_Jump(im, playerData, j[0], /* chunk */
					   j[2] | (j[1] << 7),   /* measure */
					   j[3],                 /* beat */
					   (j[4] << 7) | j[5],   /* tick */
					   j[6]);                /* sustain */
		}
	} else if (subOp == 3) {
		/* Large-marker: marker flag = 128 and the payload
		 * pointer is what ImTriggers_ProcessMarker sees. On
		 * LP64 that address is always > 127 (needed by the
		 * ProcessMarker dispatch). */
		playerData->player->marker = 128;
		ImTriggers_ProcessMarker(im, playerData->player->soundId, (intptr_t)payload);
	} else {
		ImDebug_LogMsg(im, "ERROR:mp got bad sysex msg...");
	}
}

void ImParser_HandleMetaEvent(imuse_t* im, ImSeqData* playerData, const uint8_t* chunkData) {
	/* Live-mode meta handler. Unlike the jump/scan variant,
	 * 0x2F here retires the player in full. */
	uint8_t metaType = chunkData[0];
	if (metaType == 0x2F) {
		ImPlayers_StopPlayer(im, playerData->player);
	} else if (metaType == 0x51) {
		int tempo = (chunkData[2] << 16) | (chunkData[3] << 8) | chunkData[4];
		ImSeq_SetTempo(im, playerData, tempo);
	} else if (metaType == 0x58) {
		int numerator = chunkData[2];
		int denomPower = chunkData[3];
		ImSeq_SetTicksPerBeat(im, playerData, 960 >> (denomPower - 1), numerator);
	}
}

/* ===== Dispatch tables =====
 *
 * 9 slots each, indexed [0..6] for channel-voice (status class)
 * and [7]/[8] for SysEx/Meta. NULL slots = silently skip that
 * event class.
 *
 * Channel-voice slots match the DOS wire encoding:
 *   0 NoteOff, 1 NoteOn, 2 PolyAT (no-op),
 *   3 ControlChange, 4 ProgramChange, 5 ChanAT (no-op),
 *   6 PitchBend.
 */

static const void* const s_midiCmdFunc[9] = {
	(const void*)ImPlayers_MidiNoteOff,
	(const void*)ImPlayers_MidiNoteOn,
	(const void*)ImSeq_MidiPressure,
	(const void*)ImPlayers_MidiCommand,
	(const void*)ImPlayers_ProgramChange,
	(const void*)ImSeq_MidiPressure,
	(const void*)ImPlayers_HandleChannelPitchBend,
	(const void*)ImParser_HandleSysEx,
	(const void*)ImParser_HandleMetaEvent,
};

static const void* const s_jumpMidiCmdFunc[9] = {
	0, 0, 0, 0, 0, 0, 0, 0, (const void*)ImSeq_HandleMetaJumpScan,
};

static const void* const s_scanMidiCmdFuncs[9] = {
	(const void*)ImPlayers_MidiNoteOff,
	(const void*)ImPlayers_MidiNoteOn,
	0,
	(const void*)ImPlayers_MidiCommand,
	(const void*)ImPlayers_ProgramChange,
	0,
	(const void*)ImPlayers_HandleChannelPitchBend,
	0,
	(const void*)ImSeq_HandleMetaJumpScan,
};
