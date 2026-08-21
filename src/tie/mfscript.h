#ifndef __MFSCRIPT_H__
#define __MFSCRIPT_H__

#include <stdint.h>

/* ChangeRef — describes a single music transition operation */
typedef struct {
	int16_t target; /* target state or sequence ID */
	int16_t opcode; /* 1=xfade, 2=jumpMrk, 4=xfade+reset, 5=resume, 6=jumpMrk+hook2, 7=jumpOnBeat+hook2 */
	int16_t arg1;
	int16_t arg2;
	int16_t arg3;
	int16_t arg4;
} ChangeRef;

/* CueRef — a cue point within a sequence */
typedef struct {
	intptr_t sound;      /* sound handle (populated at runtime, pointer-width) */
	int16_t nameIndex;   /* index into soundNames[] */
	ChangeRef cueChange; /* transition to execute at this cue */
} CueRef;

/* StateRef — a music state with its transitions */
typedef struct {
	intptr_t sound;            /* sound handle (populated at runtime, pointer-width) */
	int16_t nameIndex;         /* index into soundNames[] */
	ChangeRef stateChanges[7]; /* transitions to other states */
	ChangeRef seqChanges[2];   /* transitions when entering/leaving sequences */
} StateRef;

int16_t mfscript_MfStartScript(void* idp);
int16_t mfscript_MfStopScript(void);
int16_t mfscript_MfRefreshScript(void);
int16_t mfscript_MfSetState(int16_t state);
int16_t mfscript_MfSetSequence(int16_t sequence);
int16_t mfscript_MfSetCuePoint(int16_t cuePoint);
int16_t mfscript_MfSetAttribute(int16_t number, int16_t val);

#endif
