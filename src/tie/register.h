#ifndef TIE_REGISTER_H
#define TIE_REGISTER_H

#include "landru/filedir.h"
#include "landru/input.h"
#include "tie/shellext.h"
#include "tie_runtime/storage/pilot_storage.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

int16_t register_Register(SceneHeadStruct* scene_head);
void register_end_View(int32_t time);
extern Directory register_directory;
extern void* register_fast_pilot_record;
int16_t register_Do_Protect_Dialog(void);

/* Clear the is_protected flag on the active pilot's FastPilotRecord.
 * Called by COMPUTER after a pilot restore. */
void register_Revive_Pilot_Info(void);

#ifdef __cplusplus
}
#endif

#endif
