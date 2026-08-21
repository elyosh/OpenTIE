#ifndef __IMUSE_TRIGGERS_H__
#define __IMUSE_TRIGGERS_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Owns marker triggers and 60 Hz deferred commands. Opcodes below
 * IM_OPCODE_MAX dispatch commands; larger values are callback pointers. */

#define IM_NUM_TRIGGERS 16
#define IM_NUM_DEFER_CMDS 8

/* Opcode / callback dispatch boundary. Values in [0, IM_OPCODE_MAX)
 * are routed through ImCommands_ExecOpcode (the ImuseOpcode enum in
 * commands.h); values >= IM_OPCODE_MAX are treated as callback
 * function pointers cast to intptr_t. */
#define IM_OPCODE_MAX 30

/* Marker-fired trigger slot. */
typedef struct ImTrigger {
	intptr_t soundId; /* 0 = free */
	int marker;       /* 0 = any, 1..127 = byte, 128 = large */
	intptr_t opcodeOrFuncPtr;
	intptr_t args[10];
} ImTrigger;

/* Deferred-command slot. */
typedef struct ImDeferCmd {
	int counter; /* 0 = free; decremented per 60 Hz tick */
	intptr_t opcode;
	intptr_t args[10];
} ImDeferCmd;

/* Callback signature when a trigger fires with opcodeOrFuncPtr >= 30.
 * The engine passes the marker followed by all ten stored args. */
typedef void (*ImTriggerCallback)(int marker, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4,
								  intptr_t a5, intptr_t a6, intptr_t a7, intptr_t a8, intptr_t a9);

/* Callback signature when a deferred command fires with opcode >= 30.
 * No marker is passed (this is the semantic difference from the trigger
 * path, so callers that are used in both must supply distinct shims). */
typedef void (*ImDeferCallback)(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4, intptr_t a5,
								intptr_t a6, intptr_t a7, intptr_t a8, intptr_t a9);

/* ===== Lifecycle ===== */

int ImTriggers_Init(imuse_t* im);
int ImTriggers_Deinit(imuse_t* im);

/* ===== Save / Restore =====
 *
 * NOT DOS-format-compatible: the two tables are memcpy'd at their native
 * LP64 layout (ImTrigger and ImDeferCmd are pointer-width-sensitive).
 * Use ImTriggers_GetSaveSize to learn how many bytes Save will write. */
int ImTriggers_Save(imuse_t* im, void* buf, int size);
int ImTriggers_Restore(imuse_t* im, void* buf);
int ImTriggers_GetSaveSize(imuse_t* im);

/* lolevel.c collects public varargs into the fixed 10-element argument array. */

/* Register a marker-fired trigger. soundId must be non-zero.
 * Returns 0, -5 (bad soundId), or -6 (table full). */
int ImTriggers_Set(imuse_t* im, intptr_t soundId, int marker, intptr_t opcodeOrFuncPtr,
				   const intptr_t args[10]);

/* Count triggers matching (soundId, marker, opcodeOrFuncPtr). Pass -1
 * for any field to wildcard it. Returns 0..IM_NUM_TRIGGERS. */
int ImTriggers_Check(imuse_t* im, intptr_t soundId, int marker, intptr_t opcodeOrFuncPtr);

/* Free trigger slots matching (soundId, marker, opcodeOrFuncPtr).
 * Same wildcard semantics as Check. Returns 0. */
int ImTriggers_Clear(imuse_t* im, intptr_t soundId, int marker, intptr_t opcodeOrFuncPtr);

/* Fire all triggers matching (soundId, marker). Called from the MIDI
 * sysex / chunk-boundary paths. */
void ImTriggers_ProcessMarker(imuse_t* im, intptr_t soundId, int marker);

/* Schedule a deferred command. count (>0) = frames at 60 Hz until fire.
 * Returns 0, -5 (count == 0), or -6 (table full). */
int ImTriggers_DeferCommand(imuse_t* im, int count, intptr_t opcode, const intptr_t args[10]);

/* Per-tick step. Decrements every live defer slot; fires those that
 * reach 0. Called from imuse_advance's 60 Hz tier. */
void ImTriggers_Update(imuse_t* im);

/* Count pending IMUSE_CMD_START_SOUND triggers or defers targeting soundId.
 * Backs the soundPendCount (param 512) query in imuse_get_param. */
int ImTriggers_GetPendingSoundCount(imuse_t* im, intptr_t soundId);

#endif /* __IMUSE_TRIGGERS_H__ */
