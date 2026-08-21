#ifndef __IMUSE_LOLEVEL_H__
#define __IMUSE_LOLEVEL_H__

#include <stdint.h>

#include <imuse/commands.h> /* ImuseCmd */
#include <imuse/handle.h>

/*
 * libimuse — low-level command surface.
 *
 * The set/check/clear-trigger and defer-command entry points sit
 * here because they consume an `ImuseCmd` (declared in commands.h)
 * and are conceptually one tier below the per-sound start/stop
 * surface.
 *
 * The MIDI-control entries (jump, scan, send, share-parts) and the
 * VOC-stream entries also live here — they're niche enough that a
 * fresh consumer doesn't see them on first read of commands.h.
 *
 * Lifecycle: see <imuse/commands.h> for imuse_create / imuse_destroy.
 */

/* ===== Triggers + deferred commands =====
 *
 * `cmd->opcode` is either an `ImuseOpcode` value (numeric, dispatched
 * by the engine through its internal opcode table) or a function
 * pointer cast to intptr_t (treated as a callback when the value is
 * >= 30 / above IM_OPCODE_MAX). `cmd->args[0..9]` are passed
 * verbatim to the replayed handler; only the leading slots the
 * handler consumes are read. Pass 0 for unused slots.
 *
 * The engine stores `args` as 64-bit slots, so callers must pass
 * pointer-valued data through `(intptr_t)` not `(int)` to avoid
 * sign-extending and truncating LP64 pointers. */
int imuse_set_trigger(imuse_t* im, intptr_t soundId, int marker, const ImuseCmd* cmd);
int imuse_check_trigger(imuse_t* im, intptr_t soundId, int marker, intptr_t opcode);
int imuse_clear_trigger(imuse_t* im, intptr_t soundId, int marker, intptr_t opcode);
int imuse_defer_command(imuse_t* im, int tick_count, const ImuseCmd* cmd);

/* ===== MIDI control =====
 *
 * `imuse_midi_jump` repositions the sequencer cursor to (chunk,
 * measure, beat, tick); `imuse_midi_scan` does the same but
 * accumulates state by replaying intermediate events through the
 * synth (used post-restore). `sustain` controls whether held notes
 * are dropped or kept. `imuse_send_midi` injects a raw MIDI message;
 * `imuse_share_parts` re-routes one sound's MIDI parts onto another
 * sound's allocations (music transitions). */
int imuse_midi_jump(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick, int sustain);
int imuse_midi_scan(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick);
int imuse_send_midi(imuse_t* im, intptr_t soundId, int status, int d1, int d2);
int imuse_share_parts(imuse_t* im, intptr_t srcSoundId, intptr_t dstSoundId);

/* VOC streaming through the host seek/read/buffer callbacks. */
int imuse_stream_start(imuse_t* im, intptr_t soundId, int priority, int bufId);
int imuse_stream_switch(imuse_t* im, intptr_t oldSoundId, intptr_t newSoundId, void* crossFadeBuffer,
						int crossFadeBufferSize, int vocLoopFlag);
int imuse_stream_process(imuse_t* im);
intptr_t imuse_stream_next(imuse_t* im, intptr_t soundId, int* bufSize, int* lowWaterMark, int* available);

#endif
