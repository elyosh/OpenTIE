#ifndef __IMUSE_UTILS_H__
#define __IMUSE_UTILS_H__

#include <stdint.h>

#include <imuse/handle.h>

/*
 * iMUSE engine -- UTILS module.
 *
 * Small generic helpers used across the engine: integer clamp, an
 * octave-wrap for semitone values, and a doubly-linked-list pair
 * keyed on prev (offset 0) / next (offset 4) at the start of any
 * participating struct.
 *
 * These helpers are 1:1 ports of the IMUSE.ENG ImUtils_* set
 * (ImUtils_Clamp, ImUtils_WrapSemitones, ImUtils_ListAddItem,
 * ImUtils_ListRemoveItem). The list functions mutate the head
 * pointer via a pointer-to-pointer argument, matching the DOS ABI.
 */

int ImUtils_Clamp(imuse_t* im, int v, int min, int max);

/* Add / subtract 12 until v falls within [min, max]. Step is
 * hardcoded to 12 — the caller picks the span (normally [-12, 12]
 * for soundTranspose). Non-octave-aligned spans give garbage. */
int ImUtils_WrapSemitones(imuse_t* im, int v, int min, int max);

/* Head-insertion into a doubly-linked list whose nodes carry
 * prev (offset 0) and next (offset 4) pointers. head is
 * pointer-to-first-node; caller passes its address. */
void ImUtils_ListAddItem(imuse_t* im, void* head, void* item);

/* Unlink `item` from the list headed at `*head`. Returns 0 on
 * success, -3 if item wasn't on the list, -5 on argument error. */
int ImUtils_ListRemoveItem(imuse_t* im, void* head, void* item);

/* Evaluate a SysEx jump-hook against the player's armed hook.
 * Returns 0 (fire) or -1 (block); consumes (zeroes) *playerHookPtr
 * on every write path since hooks are one-shot.
 *
 *   trackHookId != 0 : fire iff *playerHookPtr == trackHookId.
 *   trackHookId == 0 : fire unless *playerHookPtr == 128
 *                      (128 is the 'suppress wildcards' sentinel). */
int ImUtils_CheckHookValue(imuse_t* im, int* playerHookPtr, int trackHookId);

/* Read a 32-bit big-endian unsigned integer from `buf`.
 * Name preserved from the binary for ABI-documentation parity,
 * though it is a misnomer — this isn't an in-place swap, it's
 * a BE-bytes → host dword decoder. Used by ImFiles_GotoChunk
 * to parse IFF-like chunk size headers. */
uint32_t ImUtils_Swap32(imuse_t* im, const unsigned char* buf);

#endif /* __IMUSE_UTILS_H__ */
