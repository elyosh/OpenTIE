#include "internal/state.h"
#include <imuse/filelist.h>
#include <imuse/lolevel.h>

#include <string.h>

/*
 * iMUSE File List (filelist.c)
 *
 * Part of the iMUSE host library. Manages a pool of 10 SoundEntry slots
 * organized into two doubly-linked lists: im->filelist.loadedList (persistent sounds)
 * and im->filelist.openedList (streaming sounds). Each entry tracks a sound handle
 * returned by the host's load/open callbacks, a name (max 23 chars),
 * and a reference count.
 *
 * Sounds with ref_count==0 that are not currently playing are flushed
 * (freed via the host's unload/close callbacks) by ImFlushSounds.
 */

#define MAX_SOUND_ENTRIES IMUSE_MAX_SOUND_ENTRIES
#define MAX_SOUND_NAME IMUSE_MAX_SOUND_NAME

/* The local IM_PARAM_PRIORITY / IM_PARAM_VOLUME macros (legacy names
 * inherited from the DOS code) actually queried PLAY_COUNT / PEND_COUNT
 * — the values 0x100 and 0x200 in iMuseParameter. The flush sweep
 * below uses them to skip reaping a sound that is still playing or
 * has work pending. Redirected to the public IMUSE_PARAM_* names. */

/* SoundEntry struct definition lives in internal/state.h (named
 * ImSoundEntry there) so the imuse_t aggregate can embed the entry
 * pool by value. Local alias kept to minimise diffs in the body. */
typedef ImSoundEntry SoundEntry;

/* --- Internal linked list helpers --- */

static int AddToList(SoundEntry** head, SoundEntry* node) {
	if (!node || node->prev || node->next)
		return -5;
	node->next = *head;
	if (*head)
		(*head)->prev = node;
	node->prev = NULL;
	*head = node;
	return 0;
}

static int RemoveFromList(SoundEntry** head, SoundEntry* node) {
	SoundEntry* cur;

	if (!node || !*head)
		return -5;

	for (cur = *head; cur && cur != node; cur = cur->next)
		;
	if (!cur)
		return -3;

	if (node->next)
		node->next->prev = node->prev;
	if (node->prev)
		node->prev->next = node->next;
	else
		*head = node->next;

	node->prev = NULL;
	node->next = NULL;
	return 0;
}

/* --- Find a free slot in the pool --- */

static SoundEntry* FindFreeSlot(imuse_t* im) {
	for (int i = 0; i < MAX_SOUND_ENTRIES; i++) {
		if (!im->filelist.entries[i].handle)
			return &im->filelist.entries[i];
	}
	return NULL;
}

/* --- Public API --- */

int imuse_filelist_init(imuse_t* im, ImuseLoadSoundFunc loadFunc, ImuseUnloadSoundFunc unloadFunc,
						ImuseOpenSoundFunc openFunc, ImuseCloseSoundFunc closeFunc) {
	im->filelist.loadFunc = loadFunc;
	im->filelist.unloadFunc = unloadFunc;
	im->filelist.openFunc = openFunc;
	im->filelist.closeFunc = closeFunc;

	for (int i = 0; i < MAX_SOUND_ENTRIES; i++) {
		im->filelist.entries[i].prev = NULL;
		im->filelist.entries[i].next = NULL;
		im->filelist.entries[i].handle = NULL;
	}

	im->filelist.loadedList = NULL;
	im->filelist.openedList = NULL;
	im->filelist.initFlag = 1;
	return 0;
}

int imuse_ImSaveFilelist(imuse_t* im, int32_t* outBuf) {
	SoundEntry* entry;
	int count = 2; /* skip 2 dwords for loaded_count + opened_count */

	outBuf[0] = 0;
	for (entry = im->filelist.loadedList; entry; entry = entry->next) {
		outBuf[0]++;
		memcpy(&outBuf[count], entry, sizeof(SoundEntry));
		count += sizeof(SoundEntry) / 4;
	}

	outBuf[1] = 0;
	for (entry = im->filelist.openedList; entry; entry = entry->next) {
		outBuf[1]++;
		memcpy(&outBuf[count], entry, sizeof(SoundEntry));
		count += sizeof(SoundEntry) / 4;
	}

	return count * 4;
}

void imuse_ImRestoreFilelist(imuse_t* im, int32_t* buf) {
	int loaded_count = buf[0];
	int opened_count = buf[1];
	int i = 2;

	while (loaded_count--) {
		SoundEntry* saved = (SoundEntry*)&buf[i];
		i += sizeof(SoundEntry) / 4;

		if (!imuse_filelist_load(im, saved->name))
			continue;

		if (im->filelist.loadedList && !strcmp(saved->name, im->filelist.loadedList->name))
			im->filelist.loadedList->ref_count = saved->ref_count;
	}

	while (opened_count--) {
		SoundEntry* saved = (SoundEntry*)&buf[i];
		i += sizeof(SoundEntry) / 4;

		if (!imuse_filelist_open(im, saved->name))
			continue;

		if (im->filelist.openedList && !strcmp(saved->name, im->filelist.openedList->name))
			im->filelist.openedList->ref_count = saved->ref_count;
	}
}

void* imuse_filelist_load(imuse_t* im, const char* name) {
	SoundEntry *entry, *slot;
	void* handle;
	int len;

	if (!im->filelist.initFlag || !name || !*name || !im->filelist.loadFunc || !im->filelist.unloadFunc)
		return NULL;

	/* Already loaded? Increment ref count. */
	handle = imuse_filelist_find(im, name);
	if (handle) {
		for (entry = im->filelist.loadedList; entry; entry = entry->next) {
			if (handle == entry->handle) {
				entry->ref_count++;
				return handle;
			}
		}
	}

	slot = FindFreeSlot(im);
	if (!slot)
		return NULL;

	handle = im->filelist.loadFunc(name);
	if (!handle)
		return NULL;

	len = strlen(name);
	if (len > MAX_SOUND_NAME)
		return NULL;

	strncpy(slot->name, name, MAX_SOUND_NAME);
	slot->name[MAX_SOUND_NAME] = '\0';
	slot->handle = handle;
	slot->ref_count = 1;
	AddToList(&im->filelist.loadedList, slot);
	return handle;
}

void imuse_filelist_unload(imuse_t* im, void* handle) {
	SoundEntry* entry;

	if (!im->filelist.initFlag || !im->filelist.loadFunc || !im->filelist.unloadFunc)
		return;

	for (entry = im->filelist.loadedList; entry; entry = entry->next) {
		if (handle == entry->handle) {
			if (entry->ref_count > 0)
				entry->ref_count--;
			break;
		}
	}
	imuse_filelist_flush(im);
}

void imuse_filelist_unload_all(imuse_t* im) {
	SoundEntry* entry;

	if (!im->filelist.initFlag || !im->filelist.loadFunc || !im->filelist.unloadFunc)
		return;

	for (entry = im->filelist.loadedList; entry; entry = entry->next)
		entry->ref_count = 0;

	imuse_filelist_flush(im);
}

void* imuse_filelist_open(imuse_t* im, const char* name) {
	SoundEntry *entry, *slot;
	void* handle;
	int len;

	if (!im->filelist.initFlag || !name || !*name || !im->filelist.openFunc || !im->filelist.closeFunc)
		return NULL;

	/* Already opened? Increment ref count. */
	handle = imuse_filelist_find(im, name);
	if (handle) {
		for (entry = im->filelist.openedList; entry; entry = entry->next) {
			if (handle == entry->handle) {
				entry->ref_count++;
				return handle;
			}
		}
	}

	slot = FindFreeSlot(im);
	if (!slot)
		return NULL;

	handle = im->filelist.openFunc(name);
	if (!handle)
		return NULL;

	len = strlen(name);
	if (len > MAX_SOUND_NAME)
		return NULL;

	strncpy(slot->name, name, MAX_SOUND_NAME);
	slot->name[MAX_SOUND_NAME] = '\0';
	slot->handle = handle;
	slot->ref_count = 1;
	AddToList(&im->filelist.openedList, slot);
	return handle;
}

void imuse_filelist_close(imuse_t* im, void* handle) {
	SoundEntry* entry;

	if (!im->filelist.initFlag || !im->filelist.openFunc || !im->filelist.closeFunc)
		return;

	for (entry = im->filelist.openedList; entry; entry = entry->next) {
		if (handle == entry->handle) {
			if (entry->ref_count > 0)
				entry->ref_count--;
			break;
		}
	}
	imuse_filelist_flush(im);
}

void imuse_filelist_close_all(imuse_t* im) {
	SoundEntry* entry;

	if (!im->filelist.initFlag || !im->filelist.openFunc || !im->filelist.closeFunc)
		return;

	for (entry = im->filelist.openedList; entry; entry = entry->next)
		entry->ref_count = 0;

	imuse_filelist_flush(im);
}

void* imuse_filelist_find(imuse_t* im, const char* name) {
	SoundEntry* entry;

	for (entry = im->filelist.loadedList; entry; entry = entry->next) {
		if (!strcmp(name, entry->name))
			return entry->handle;
	}
	for (entry = im->filelist.openedList; entry; entry = entry->next) {
		if (!strcmp(name, entry->name))
			return entry->handle;
	}
	return NULL;
}

void imuse_filelist_flush(imuse_t* im) {
	SoundEntry *entry, *next;

	for (entry = im->filelist.loadedList; entry; entry = next) {
		next = entry->next;
		if (!entry->ref_count &&
			!imuse_get_param(im, (intptr_t)entry->handle, IMUSE_PARAM_SOUND_PLAY_COUNT) &&
			!imuse_get_param(im, (intptr_t)entry->handle, IMUSE_PARAM_SOUND_PEND_COUNT)) {
			if (im->filelist.unloadFunc)
				im->filelist.unloadFunc(entry->handle);
			entry->handle = NULL;
			RemoveFromList(&im->filelist.loadedList, entry);
		}
	}

	for (entry = im->filelist.openedList; entry; entry = next) {
		next = entry->next;
		if (!entry->ref_count &&
			!imuse_get_param(im, (intptr_t)entry->handle, IMUSE_PARAM_SOUND_PLAY_COUNT) &&
			!imuse_get_param(im, (intptr_t)entry->handle, IMUSE_PARAM_SOUND_PEND_COUNT)) {
			if (im->filelist.closeFunc)
				im->filelist.closeFunc(entry->handle);
			entry->handle = NULL;
			RemoveFromList(&im->filelist.openedList, entry);
		}
	}
}
