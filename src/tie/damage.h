/*
 * DAMAGE — in-flight damage-report room (USER_inflightinfo page 3).
 *
 * Port of damage.c from the Watcom binary. 3 functions, straight 1:1 mapping
 * against IDA's DAMAGE_damageroom / DAMAGE_nextsystem / DAMAGE_outputsystem.
 */

#ifndef TIE_DAMAGE_H
#define TIE_DAMAGE_H

#include "tie/string_table_ids.h"
#include <stdint.h>

/*
 * System-status bitmask. One bit per subsystem, lives in CraftData::subsystem_active
 * (craft loadout) and in systemmask[] (lookup table per SystemStringId).
 */
typedef enum SubsystemFlags {
	SF_SHIELDS = 0x0001,
	SF_AUTO_EJECT = 0x0002,
	SF_TARGETING = 0x0004,
	SF_WARHEAD_LAUNCH = 0x0008,
	SF_LASERS = 0x0010,
	SF_FLIGHT_CONTROL = 0x0020,
	SF_ENGINES = 0x0040,
	SF_HYPER_DRIVE = 0x0080,
	SF_TRACTOR_BEAM = 0x0100,
	SF_COMMUNICATIONS = 0x0200,
} SubsystemFlags;

/*
 * 10-entry pointer table loaded by FEDISKIO_loadstringdata (points into
 * stringdata_buf). systemstrings[id] is the printable name of SystemStringId id.
 * Owned by damage.c per watdbg (_systemstrings, 4 bytes = single pointer).
 */
extern char** systemstrings;

/*
 * Push the damage-assessment room as a tie_core task: three groups in
 * repair-priority order (under repair -> operational -> not fitted),
 * with one selectable row whose priority can be bumped to the top via
 * Enter/Space or RMB. The task latches the navigation hint
 * (-1/0/+1) into `user_submodal_result` before pop.
 */
void damage_Push_DamageRoom_Task(void);

/*
 * Move the current selection one step forward (direction == +1) or backward
 * (direction == -1, encoded as 0xFFFF) through systems in repair-priority order. The
 * sort order is {under repair first, operational after}; navigation wraps inside a
 * group and falls through to the other group at the boundary. Does NOT
 * filter by subsystem_active -- the caller re-calls until a present one is
 * reached.
 */
uint8_t damage_nextsystem(uint16_t cur_sys, int16_t direction);

/*
 * Print one row for `system_id` at vertical position `y`:
 *   - subsystem absent (bit clear in subsystem_active) -> "N/A" (color 0x41)
 *   - under repair (health == 0)          -> "MM:SS" countdown (color 0x4A)
 *   - fully operational (health == 100)  -> "100%" (color 0x52)
 *   - partial system health             -> "NN%" (color 0x4E)
 * Name is drawn left-justified through systemstrings[id]; value right-justified.
 */
void damage_outputsystem(SystemStringId system_id, int16_t y);

#endif /* TIE_DAMAGE_H */
