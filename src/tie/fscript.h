#ifndef TIE_FSCRIPT_H
#define TIE_FSCRIPT_H

#include <stdint.h>

/*
 * SDP (Sound Design Program) record — 62 bytes.
 * Forms a linked chain: name[0]==0 marks end of chain.
 * sound_name[0] is either an ASCII filename or a control byte
 * (state index 0-11 for transitions, 0x0C for generic terminal).
 */
typedef struct {
	char name[10];          /* SDP chain identifier */
	char sound_name[10];    /* sound file, or [0]=transition code */
	uint8_t num_dests;      /* number of destination choices (max 4) */
	uint8_t used_mask;      /* exhaustive-random tracking bitmask */
	char dest_names[4][10]; /* destination name choices */
} SdpRecord;                /* 62 bytes */

int16_t fscript_MsStartScript(void* init_data);
int16_t fscript_MsStopScript(void);
int16_t fscript_MsSetCuePoint(void);
int16_t fscript_MsRefreshScript(void);
int16_t fscript_MsSetState(int16_t new_state);
int16_t fscript_MsSetSequence(int16_t seq_id);
int16_t fscript_MsSetAttribute(int16_t attr_id, int16_t value);

/* --- Module-owned globals (consumed by fcallbk.c) ----------------------
 * iMUSE soundIDs for the active / pending tracks plus the active sequence
 * head, sampled by CbDoCallback when a play marker fires. */
extern void* currentID;
extern void* nextID;
extern void* sequenceID;
extern int32_t playingState;
extern int32_t currentState;
extern int32_t currentSequence;
extern int32_t sequencePri;

/* Music attribute slots: attributes[0] = "buildup" level driving the
 * per-channel volume bitmask tables below. */
extern int16_t attributes[2];

/* Channel-on bitmasks per buildup level (CbSetChannels indexes by the
 * current buildup value). 6-entry intro ramp + 7-entry waiting hold. */
extern uint16_t introBuildup[6];
extern uint16_t waitingBuildup[7];

#endif
