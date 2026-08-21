#ifndef __IMUSE_GROUPS_H__
#define __IMUSE_GROUPS_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Group zero is master. A subgroup's effective volume is
 * master * (raw + 1) >> 7, so raw 127 passes master through unchanged.
 * The getter returns effective volume; the setter's query mode returns raw. */

#define IMUSE_NUM_GROUPS 16
#define IMUSE_GROUP_MAX_VOL 127

/* Numbering matches the DOS engine's group indices. */
typedef enum ImuseGroup {
	IMUSE_GROUP_MASTER = 0,
	IMUSE_GROUP_SFX = 1,
	IMUSE_GROUP_VOICE = 2,
	IMUSE_GROUP_MUSIC = 3,
	IMUSE_GROUP_DIPPED = 4
	/* 5..15 reserved / unused in TIE */
} ImuseGroup;

/* vol == -1 queries raw volume. A valid set returns the previous raw value;
 * invalid group or volume returns -5. */
int imuse_set_group_volume(imuse_t* im, int group, int vol);

/* Read the effective (master-scaled) volume for a group. Returns -5
 * on group >= 16. */
int imuse_get_group_volume(imuse_t* im, int group);

#endif /* __IMUSE_GROUPS_H__ */
