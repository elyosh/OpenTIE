#include "internal/triggers.h"
#include "internal/state.h"

#include "internal/commands.h" /* ImCommands_ExecOpcode (replay path) */
#include "internal/debug.h"
#include <imuse/commands.h>

#include <string.h>

/* Trigger save data uses native structs and is only compatible within a build.
 * Opcodes at or above 30 store a callback address directly in intptr_t. */

/* ===== Module state ===== */

/* Self-healing "any defer live" fast-path flag. Update zeroes it at
 * entry, then re-raises it if any slot still has a non-zero counter —
 * so when all defers drain, Update becomes a no-op until a new
 * DeferCommand arrives. */

/* ===== Lifecycle ===== */

int ImTriggers_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "TRIGGERS module...");

	/* Only clear the free-slot sentinels (soundId / counter). The rest
	 * of each slot stays garbage until a Set / DeferCommand writes it,
	 * matching the original engine's laziness. */
	for (int i = 0; i < IM_NUM_TRIGGERS; ++i)
		im->triggers.triggers[i].soundId = 0;
	for (int i = 0; i < IM_NUM_DEFER_CMDS; ++i)
		im->triggers.defers[i].counter = 0;

	im->triggers.defersOn = 0;
	return 0;
}

int ImTriggers_Deinit(imuse_t* im) {
	/* Same wipe as Init: free every slot, reset the fast-path flag. */
	for (int i = 0; i < IM_NUM_TRIGGERS; ++i)
		im->triggers.triggers[i].soundId = 0;
	for (int i = 0; i < IM_NUM_DEFER_CMDS; ++i)
		im->triggers.defers[i].counter = 0;

	im->triggers.defersOn = 0;
	return 0;
}

/* ===== Save / Restore ===== */

int ImTriggers_GetSaveSize(imuse_t* im) {
	return (int)(sizeof(im->triggers.triggers) + sizeof(im->triggers.defers));
}

int ImTriggers_Save(imuse_t* im, void* buf, int size) {
	int needed = ImTriggers_GetSaveSize(im);
	if (size < needed)
		return -5;
	memcpy(buf, im->triggers.triggers, sizeof(im->triggers.triggers));
	memcpy((char*)buf + sizeof(im->triggers.triggers), im->triggers.defers, sizeof(im->triggers.defers));
	return needed;
}

int ImTriggers_Restore(imuse_t* im, void* buf) {
	memcpy(im->triggers.triggers, buf, sizeof(im->triggers.triggers));
	memcpy(im->triggers.defers, (char*)buf + sizeof(im->triggers.triggers), sizeof(im->triggers.defers));
	/* Unconditionally raise the flag; Update's self-healing step will
	 * clear it on the next tick if no slot is actually live. The
	 * original matched this behaviour to keep Restore path simple. */
	im->triggers.defersOn = 1;
	return ImTriggers_GetSaveSize(im);
}

/* ===== Marker-fired im->triggers.triggers ===== */

int ImTriggers_Set(imuse_t* im, intptr_t soundId, int marker, intptr_t opcodeOrFuncPtr,
				   const intptr_t args[10]) {
	if (soundId == 0)
		return -5;

	/* Linear scan for the first free slot. 16 slots, so O(n) is fine. */
	for (int i = 0; i < IM_NUM_TRIGGERS; ++i) {
		ImTrigger* slot = &im->triggers.triggers[i];
		if (slot->soundId != 0)
			continue;
		slot->soundId = soundId;
		slot->marker = marker;
		slot->opcodeOrFuncPtr = opcodeOrFuncPtr;
		memcpy(slot->args, args, sizeof(slot->args));
		return 0;
	}

	ImDebug_LogMsg(im, "ERR: tr unable to alloc trigger...");
	return -6;
}

/* Shared predicate for Check / Clear wildcard matching. -1 on any
 * field means "don't care"; otherwise require an exact match. A slot
 * with soundId == 0 is free and never matches. */
static int trigger_matches(const ImTrigger* slot, intptr_t soundId, int marker, intptr_t opcodeOrFuncPtr) {
	if (slot->soundId == 0)
		return 0;
	if (soundId != -1 && soundId != slot->soundId)
		return 0;
	if (marker != -1 && marker != slot->marker)
		return 0;
	if (opcodeOrFuncPtr != -1 && opcodeOrFuncPtr != slot->opcodeOrFuncPtr)
		return 0;
	return 1;
}

int ImTriggers_Check(imuse_t* im, intptr_t soundId, int marker, intptr_t opcodeOrFuncPtr) {
	int n = 0;
	for (int i = 0; i < IM_NUM_TRIGGERS; ++i) {
		if (trigger_matches(&im->triggers.triggers[i], soundId, marker, opcodeOrFuncPtr))
			++n;
	}
	return n;
}

int ImTriggers_Clear(imuse_t* im, intptr_t soundId, int marker, intptr_t opcodeOrFuncPtr) {
	/* Free matching slots by zeroing soundId (the free sentinel).
	 * Everything else is left untouched — a subsequent Set will
	 * overwrite the stale fields. */
	for (int i = 0; i < IM_NUM_TRIGGERS; ++i) {
		if (trigger_matches(&im->triggers.triggers[i], soundId, marker, opcodeOrFuncPtr))
			im->triggers.triggers[i].soundId = 0;
	}
	return 0;
}

/* ===== Trigger fire ===== */

/* Fire one marker trigger. The soundId is cleared BEFORE the callback /
 * command runs so the trigger is one-shot AND re-entrant-safe (the
 * handler can legitimately Set a new trigger on the same slot). */
static void exec_trigger(imuse_t* im, ImTrigger* trigger, int marker) {
	intptr_t opcode = trigger->opcodeOrFuncPtr;
	intptr_t args[10];
	memcpy(args, trigger->args, sizeof(args));
	trigger->soundId = 0;

	if (opcode >= IM_OPCODE_MAX) {
		/* Callback path: pass marker + all 10 args through the typed
		 * function pointer cast. Callback signature must match
		 * ImTriggerCallback. */
		ImTriggerCallback cb = (ImTriggerCallback)opcode;
		cb(marker, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
	} else {
		ImCommands_ExecOpcode(im, (int)opcode, args);
	}
}

void ImTriggers_ProcessMarker(imuse_t* im, intptr_t soundId, int marker) {
	/* Walk every trigger; fire each whose slot->marker matches the
	 * incoming marker event. Match rules (reproduced from the DOS
	 * engine):
	 *   slot->marker == 0                         any marker fires
	 *   1..127 + marker in that range + equal     normal byte marker
	 *   128    + marker >= 128                    "large marker" (sysex
	 *                                              sub-op 3 path; marker
	 *                                              is a pointer value)
	 *
	 * Each matching slot fires exactly once per call — exec_trigger
	 * clears soundId on entry so subsequent matches in the same walk
	 * don't re-fire it.
	 */
	for (int i = 0; i < IM_NUM_TRIGGERS; ++i) {
		ImTrigger* slot = &im->triggers.triggers[i];
		if (slot->soundId != soundId)
			continue;
		int slotMarker = slot->marker;
		int match = (slotMarker == 0) || ((unsigned)marker < 128 && marker == slotMarker) ||
					((unsigned)marker >= 128 && slotMarker == 128);
		if (match)
			exec_trigger(im, slot, marker);
	}
}

/* ===== Deferred commands ===== */

int ImTriggers_DeferCommand(imuse_t* im, int count, intptr_t opcode, const intptr_t args[10]) {
	if (count == 0)
		return -5;

	for (int i = 0; i < IM_NUM_DEFER_CMDS; ++i) {
		ImDeferCmd* slot = &im->triggers.defers[i];
		if (slot->counter != 0)
			continue;
		slot->opcode = opcode;
		memcpy(slot->args, args, sizeof(slot->args));
		slot->counter = count;
		im->triggers.defersOn = 1;
		return 0;
	}

	ImDebug_LogMsg(im, "ERR: tr unable to alloc deferred cmd...");
	return -6;
}

/* Fire one deferred command. Unlike ExecTrigger, the slot's counter is
 * already 0 (the Update caller pre-decremented it to zero so the slot
 * is already "recycled"). Callback path passes args only, with NO
 * marker prefix — client-side callbacks used from both trigger and
 * defer paths must supply distinct shims. */
static void exec_defer(imuse_t* im, ImDeferCmd* cmd) {
	intptr_t opcode = cmd->opcode;

	if (opcode >= IM_OPCODE_MAX) {
		ImDeferCallback cb = (ImDeferCallback)opcode;
		cb(cmd->args[0], cmd->args[1], cmd->args[2], cmd->args[3], cmd->args[4], cmd->args[5], cmd->args[6],
		   cmd->args[7], cmd->args[8], cmd->args[9]);
	} else {
		ImCommands_ExecOpcode(im, (int)opcode, cmd->args);
	}
}

void ImTriggers_Update(imuse_t* im) {
	if (!im->triggers.defersOn)
		return;

	/* Self-healing: clear the flag, walk every slot, re-raise the flag
	 * only if at least one slot is still counting down post-this-tick.
	 * When all defers drain we stop doing this scan every tick until a
	 * new DeferCommand arrives. */
	im->triggers.defersOn = 0;
	for (int i = 0; i < IM_NUM_DEFER_CMDS; ++i) {
		ImDeferCmd* slot = &im->triggers.defers[i];
		if (slot->counter == 0)
			continue;
		im->triggers.defersOn = 1;
		if (--slot->counter == 0)
			exec_defer(im, slot);
	}
}

/* ===== Pending-sound query ===== */

int ImTriggers_GetPendingSoundCount(imuse_t* im, intptr_t soundId) {
	/* Count trigger + defer slots that are scheduled to start this
	 * specific soundId. "Scheduled to start" = stored opcode is
	 * IMUSE_CMD_START_SOUND AND args[0] (the soundId arg) matches. Used by
	 * the game to avoid double-queueing the same sound. */
	int n = 0;
	for (int i = 0; i < IM_NUM_TRIGGERS; ++i) {
		const ImTrigger* t = &im->triggers.triggers[i];
		if (t->soundId != 0 && t->opcodeOrFuncPtr == IMUSE_CMD_START_SOUND && t->args[0] == soundId)
			++n;
	}
	for (int i = 0; i < IM_NUM_DEFER_CMDS; ++i) {
		const ImDeferCmd* d = &im->triggers.defers[i];
		if (d->counter != 0 && d->opcode == IMUSE_CMD_START_SOUND && d->args[0] == soundId)
			++n;
	}
	return n;
}
