#ifndef LIBIMUSE_INTERNAL_GROUPS_H
#define LIBIMUSE_INTERNAL_GROUPS_H

#include <imuse/handle.h>

/*
 * Internal lifecycle hooks for the GROUPS module. The public
 * surface (imuse_set_group_volume / imuse_get_group_volume) lives
 * in <imuse/groups.h>; Init/Deinit are called only from
 * ImCommands_Init / ImCommands_Terminate.
 */

int ImGroups_Init(imuse_t* im);
int ImGroups_Deinit(imuse_t* im);

#endif /* LIBIMUSE_INTERNAL_GROUPS_H */
