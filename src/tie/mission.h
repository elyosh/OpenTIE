#ifndef __MISSION_H__
#define __MISSION_H__

#include <stdint.h>

/* Runtime mission state — 1030 bytes.
 * NOT the .TIE file header (EMissionStruct, 450 bytes) — that is loaded
 * into _missionheader. This struct is the flight engine's runtime state,
 * populated by SHIPEXT_Mission_Enter and the flight engine during gameplay.
 *
 * Field names verified against FEDISKIO_updatepilotrecord, PANEL_update*,
 * COLLIDE_*, SCORE_*, and TALK/MAP debrief functions. */
#pragma pack(push, 1)
typedef struct RUNTIME_MissionStateStruct {
	/* Training-craft selector enum. 0 = combat mission (no training
	 * craft selected); nonzero = a CraftType enum value identifying
	 * the training ship (5=TIE Fighter, 6=TIE Interceptor, 7=TIE
	 * Bomber, 8=TIE Advanced, 9=TIE Defender, 12=TIE Defender Mk II,
	 * 16=Assault Gunboat). Used throughout the engine as the boolean
	 * "training mission active" predicate. */
	uint8_t train_craft_type;      /* +0x000 */
	uint8_t train_craft_type_src;  /* +0x001: source value set by Mission_Enter from the ship switch; copied
									  into train_craft_type at mission start. */
	uint8_t train_level;           /* +0x002: training level (set by Mission_Enter) */
	uint8_t field_3;               /* +0x003 */
	int32_t mission_score;         /* +0x004: calculated mission score */
	uint8_t _gap_08[2];            /* +0x008 */
	int16_t train_gates_passed;    /* +0x00A */
	uint8_t _gap_0c[2];            /* +0x00C */
	int16_t train_gates_remaining; /* +0x00E */
	int16_t train_targets;         /* +0x010 */
	int16_t train_bonus;           /* +0x012 */
	uint8_t kills_by_type[69];     /* +0x014: per-species kill count */
	uint8_t captures_by_type[69];  /* +0x059: per-species capture count */
	uint8_t kills_losses[6][69];   /* +0x09E: per-species kill/loss breakdown */
	uint8_t _pad_23c[6];           /* +0x23C */
	uint8_t training_badge_earned; /* +0x242 */
	uint8_t _pad_243;              /* +0x243 */
	uint8_t mission_new_rank;      /* +0x244: new rank value on promotion */
	uint8_t mission_medal;         /* +0x245: nonzero = received medal */
	uint8_t mission_secret_medal;  /* +0x246: new secret order rank on advancement */
	uint8_t mission_mode;          /* +0x247: 1=training, 4=battle */
	uint8_t
		player_status; /* +0x248: 0=dead, 1=captured, 2=rescued, 3=mission ended (Q+space/timer/sim exit) */
	uint8_t end_flag;  /* +0x249 */
	uint8_t primary_complete;         /* +0x24A: 1 = primary objective complete */
	uint8_t secondary_complete;       /* +0x24B: 1 = secondary objective complete */
	uint8_t bonus_complete;           /* +0x24C: 1 = bonus objective complete */
	uint8_t primary_fg[48];           /* +0x24D */
	uint8_t secondary_fg[48];         /* +0x27D */
	uint8_t bonus_fg[48];             /* +0x2AD */
	uint8_t primary_global;           /* +0x2DD */
	uint8_t secondary_global;         /* +0x2DE */
	uint8_t bonus_global;             /* +0x2DF */
	uint8_t penalty_flag;             /* +0x2E0: nonzero = score penalty */
	uint8_t _pad_2e1[2];              /* +0x2E1 */
	uint8_t difficulty;               /* +0x2E3: 0=easy, 1=medium, 2=hard */
	uint8_t radiomsg_triggered[16];   /* +0x2E4 */
	uint8_t radiomsg_countdown[16];   /* +0x2F4 */
	uint8_t mission_linked_data[256]; /* +0x304: battle mission linked data */
	uint8_t beam_used;                /* +0x404 */
	uint8_t torp_used;                /* +0x405 */
} RUNTIME_MissionState;               /* 1030 bytes (0x406) */
#pragma pack(pop)

#endif
