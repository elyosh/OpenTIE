#include "internal/utils.h"

#include "internal/debug.h"

/*
 * Note on the linked-list functions: they treat item as a struct
 * whose first two members are `prev` (offset 0) and `next` (offset
 * 4). The DOS build of ImUtils_ListAddItem/RemoveItem used
 * untyped _DWORD arithmetic for exactly this — ImMidiPlayer,
 * ImSoundFader, ImTrigger, ImDeferCmd, ImWaveTrack all embed the
 * two fields at the start.
 */

typedef struct ListNode {
	struct ListNode* prev;
	struct ListNode* next;
} ListNode;

int ImUtils_Clamp(imuse_t* im, int v, int min, int max) {
	if (v < min)
		return min;
	if (v > max)
		return max;
	return v;
}

int ImUtils_WrapSemitones(imuse_t* im, int v, int min, int max) {
	while (v < min)
		v += 12;
	while (v > max)
		v -= 12;
	return v;
}

void ImUtils_ListAddItem(imuse_t* im, void* headPtr, void* item) {
	ListNode** head = (ListNode**)headPtr;
	ListNode* node = (ListNode*)item;
	/* Reject null or already-linked items — both prev and next must
	 * be zero on entry. The DOS build logs "ERR: list arg err" and
	 * returns; replicate with a log + early-out. */
	if (!node || node->prev || node->next) {
		ImDebug_LogMsg(im, "ERR: list arg err on ListAddItem...");
		return;
	}
	ListNode* old = *head;
	node->next = old;
	if (old)
		old->prev = node;
	node->prev = 0;
	*head = node;
}

uint32_t ImUtils_Swap32(imuse_t* im, const unsigned char* buf) {
	return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

int ImUtils_CheckHookValue(imuse_t* im, int* playerHookPtr, int trackHookId) {
	if (trackHookId) {
		if (trackHookId == *playerHookPtr) {
			*playerHookPtr = 0;
			return 0;
		}
		return -1;
	}
	/* Wildcard path: 128 on the player means 'suppress
	 * wildcards' — consume it and block. Anything else fires. */
	if (*playerHookPtr == 128) {
		*playerHookPtr = 0;
		return -1;
	}
	return 0;
}

int ImUtils_ListRemoveItem(imuse_t* im, void* headPtr, void* item) {
	ListNode** head = (ListNode**)headPtr;
	ListNode* node = (ListNode*)item;
	ListNode* cur = *head;
	if (!node || !cur) {
		ImDebug_LogMsg(im, "ERR: list arg err on ListRemoveItem...");
		return -5;
	}
	while (cur && cur != node)
		cur = cur->next;
	if (!cur) {
		ImDebug_LogMsg(im, "ERR: item not on list...");
		return -3;
	}
	if (node->next)
		node->next->prev = node->prev;
	if (node->prev)
		node->prev->next = node->next;
	else
		*head = node->next;
	node->next = 0;
	node->prev = 0;
	return 0;
}
