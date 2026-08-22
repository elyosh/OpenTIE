#ifndef __REGISTER_H__
#define __REGISTER_H__

#include "landru/input.h"
#include "tie/shellext.h"
#include "tie_runtime/storage/pilot_storage.h"
#include <stdint.h>

/* PORT: shared native representation of TIE95 REGISTER_Alloc_Input_Reg_
 * String_Button (0x7C148) and TIE98 counterpart (0x471BB0). */
typedef struct {
	Input header;
	char name[44];
	int16_t is_filename_mode;
} RegStringButton;

/* PORT: runtime representation of the edition-specific original handle
 * entries (20 bytes in TIE95, 43 bytes in TIE98); it has no on-disk ABI. */
typedef struct {
	char name[TIE_PILOT_NAME_CAPACITY];
	uint8_t cur_battle;
	uint8_t lost_status;
	uint8_t rank;
	int32_t score;
} FastPilotRecord;

/* PORT: task entry replacing the original synchronous REGISTER_Register. */
void register_Push_Register_Task(SceneHeadStruct* scene_head);

/* Clear the is_protected flag on the active pilot's FastPilotRecord.
 * Called by COMPUTER after a pilot restore. */
void register_Revive_Pilot_Info(void);

/* Enable/disable the copy-protection symbol challenge shown by the
 * register scene. Disabled by default (matches retail). Non-zero
 * restores the original 29-symbol password prompt. */
void register_set_copy_protection(int enabled);

#endif
