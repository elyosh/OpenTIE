/* Maps configured semantic actions to the engine's key queue or virtual
 * joystick button bits. Extended scan codes enqueue `{0, scan}`. */
#ifndef TIE_INPUT_ACTIONS_H
#define TIE_INPUT_ACTIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "aeron/input.h"

typedef enum TieInputAction {
	TIE_INPUT_ACTION_NONE = 0,

	TIE_INPUT_ACTION_FIRE_WEAPON,                   /* button 1 — fire selected weapon / warhead */
	TIE_INPUT_ACTION_TARGET_ROLL_THROTTLE_MODIFIER, /* button 2 — tap target, hold for roll / throttle */

	/* Targeting */
	TIE_INPUT_ACTION_TARGET_NEXT,                    /* 't' — cycle forward */
	TIE_INPUT_ACTION_TARGET_PREV,                    /* 'y' — cycle backward */
	TIE_INPUT_ACTION_TARGET_NEAREST_FIGHTER_OR_MINE, /* 'r' — closest enemy fighter / mine */
	TIE_INPUT_ACTION_TARGET_MY_ATTACKER,             /* 'e' — closest enemy shooting at me */
	TIE_INPUT_ACTION_TARGET_ATTACKER_CHAIN,          /* 'a' — closest attacker of current target */
	TIE_INPUT_ACTION_TARGET_NEWEST_CRAFT,            /* 'u' — newest craft in area */
	TIE_INPUT_ACTION_TARGET_CLEAR,                   /* 'o' — drop current target */
	TIE_INPUT_ACTION_AUTO_TARGET,                    /* Alt+1 — picktarget under crosshair */
	TIE_INPUT_ACTION_CYCLE_WARHEAD_VIEW,             /* F2 — select / cycle launched-warhead view */

	/* Throttle / engine */
	TIE_INPUT_ACTION_THROTTLE_UP,
	TIE_INPUT_ACTION_THROTTLE_DOWN,
	TIE_INPUT_ACTION_THROTTLE_FULL,
	TIE_INPUT_ACTION_MATCH_SPEED,

	/* Weapons */
	TIE_INPUT_ACTION_CYCLE_WEAPON_GROUP,       /* 'w' — laser / ion / etc. */
	TIE_INPUT_ACTION_CYCLE_WEAPON_FIRING_MODE, /* 'x' — cannon linking / warhead firing mode */
	TIE_INPUT_ACTION_TOGGLE_BEAM,              /* 'b' — beam weapon on/off */
	TIE_INPUT_ACTION_XFER_CANNON_TO_SHIELDS,   /* '\'' (apostrophe) */
	TIE_INPUT_ACTION_XFER_SHIELDS_TO_CANNON,   /* ';' */

	/* Shields */
	TIE_INPUT_ACTION_CYCLE_SHIELD_MODE, /* 's' — forward / rear / balanced */

	/* View */
	TIE_INPUT_ACTION_VIEW_RETURN_TO_COCKPIT,              /* F1 — return from external / warhead view */
	TIE_INPUT_ACTION_VIEW_TOGGLE_HIGH_ANGLE,              /* '0' — wing-level / 45-degree high view */
	TIE_INPUT_ACTION_VIEW_TOGGLE_EXTERNAL_CAMERA,         /* '/' or F3 — external camera on / off */
	TIE_INPUT_ACTION_VIEW_TOGGLE_EXTERNAL_CAMERA_CONTROL, /* '*' or F4 — external camera positioning */
	TIE_INPUT_ACTION_VIEW_TOGGLE_COCKPIT,                 /* '.' — cockpit on / off and recenter */

	/* Info rooms */
	TIE_INPUT_ACTION_INFO_GOALS,    /* 'g' */
	TIE_INPUT_ACTION_INFO_MAP,      /* 'm' */
	TIE_INPUT_ACTION_INFO_MESSAGES, /* 'l' */
	TIE_INPUT_ACTION_INFO_DAMAGE,   /* 'd' */
	TIE_INPUT_ACTION_INFO_HELP,     /* 'k' */

	/* Misc */
	TIE_INPUT_ACTION_HYPERSPACE,      /* 'h' */
	TIE_INPUT_ACTION_EJECT,           /* Alt+E */
	TIE_INPUT_ACTION_RECORDER_TOGGLE, /* 'c' — flight recorder */
	TIE_INPUT_ACTION_PAUSE,           /* 'p' */
	TIE_INPUT_ACTION_ESCAPE,          /* Esc — options room */

	TIE_INPUT_ACTION_CONFIRM,                 /* Space — skip / confirm pending action */
	TIE_INPUT_ACTION_END_MISSION,             /* 'q' — prompt to end the mission */
	TIE_INPUT_ACTION_CYCLE_TIME_ACCELERATION, /* Alt+T */

	TIE_INPUT_ACTION_THROTTLE_ZERO,       /* '\\' */
	TIE_INPUT_ACTION_THROTTLE_ONE_THIRD,  /* '[' */
	TIE_INPUT_ACTION_THROTTLE_TWO_THIRDS, /* ']' */
	TIE_INPUT_ACTION_TOGGLE_SLAM,         /* 'n' */

	TIE_INPUT_ACTION_CYCLE_BEAM_RECHARGE_RATE,   /* F8 */
	TIE_INPUT_ACTION_CYCLE_CANNON_RECHARGE_RATE, /* F9 */
	TIE_INPUT_ACTION_CYCLE_SHIELD_RECHARGE_RATE, /* F10 */

	TIE_INPUT_ACTION_TOGGLE_COMPONENT_TRACKING, /* 'i' */
	TIE_INPUT_ACTION_TARGET_COMPONENT_NEXT,     /* ',' */
	TIE_INPUT_ACTION_TARGET_COMPONENT_PREV,     /* '<' */
	TIE_INPUT_ACTION_TARGET_PRESET_STORE_1,     /* Shift+F5 */
	TIE_INPUT_ACTION_TARGET_PRESET_STORE_2,     /* Shift+F6 */
	TIE_INPUT_ACTION_TARGET_PRESET_STORE_3,     /* Shift+F7 */
	TIE_INPUT_ACTION_TARGET_PRESET_RECALL_1,    /* F5 */
	TIE_INPUT_ACTION_TARGET_PRESET_RECALL_2,    /* F6 */
	TIE_INPUT_ACTION_TARGET_PRESET_RECALL_3,    /* F7 */
	TIE_INPUT_ACTION_TOGGLE_THREAT_DISPLAY,     /* 'z' */

	TIE_INPUT_ACTION_ROLL_LEFT_INFO_PREVIOUS, /* Left Arrow */
	TIE_INPUT_ACTION_ROLL_RIGHT_INFO_NEXT,    /* Right Arrow */
	TIE_INPUT_ACTION_INFO_WINGMAN_COMMANDS,   /* Shift+Z */
	TIE_INPUT_ACTION_RECORDER_VIEW,           /* 'v' */

	TIE_INPUT_ACTION_COMM_ASSIGN_TARGET,    /* Shift+A */
	TIE_INPUT_ACTION_COMM_REQUEST_RESUPPLY, /* Shift+B */
	TIE_INPUT_ACTION_COMM_COVER_ME,         /* Shift+C */
	TIE_INPUT_ACTION_COMM_EVADE,            /* Shift+E */
	TIE_INPUT_ACTION_COMM_CONTINUE_MISSION, /* Shift+G */
	TIE_INPUT_ACTION_COMM_HEAD_HOME,        /* Shift+H */
	TIE_INPUT_ACTION_COMM_IGNORE_TARGET,    /* Shift+I */
	TIE_INPUT_ACTION_COMM_REPORT_ORDERS,    /* Shift+R */
	TIE_INPUT_ACTION_COMM_REINFORCEMENTS,   /* Shift+S */
	TIE_INPUT_ACTION_COMM_STOP_AND_WAIT,    /* Shift+W */

	TIE_INPUT_ACTION_VIEW_LEFT_SHOULDER,  /* Num 1 */
	TIE_INPUT_ACTION_VIEW_REAR,           /* Num 2 */
	TIE_INPUT_ACTION_VIEW_RIGHT_SHOULDER, /* Num 3 */
	TIE_INPUT_ACTION_VIEW_LEFT_WING,      /* Num 4 */
	TIE_INPUT_ACTION_VIEW_STRAIGHT_UP,    /* Num 5 */
	TIE_INPUT_ACTION_VIEW_RIGHT_WING,     /* Num 6 */
	TIE_INPUT_ACTION_VIEW_LEFT_FORWARD,   /* Num 7 */
	TIE_INPUT_ACTION_VIEW_FORWARD,        /* Num 8 */
	TIE_INPUT_ACTION_VIEW_RIGHT_FORWARD,  /* Num 9 */

	TIE_INPUT_ACTION_COUNT
} TieInputAction;

typedef enum TieInputActionCategory {
	TIE_INPUT_ACTION_CATEGORY_WEAPONS = 0,
	TIE_INPUT_ACTION_CATEGORY_TARGETING,
	TIE_INPUT_ACTION_CATEGORY_THROTTLE,
	TIE_INPUT_ACTION_CATEGORY_VIEW,
	TIE_INPUT_ACTION_CATEGORY_INFORMATION,
	TIE_INPUT_ACTION_CATEGORY_SYSTEM,
	TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
	TIE_INPUT_ACTION_CATEGORY_COUNT
} TieInputActionCategory;

typedef struct TieKeyboardBindings {
	TieInputAction keyboard[AERON_KEY_COUNT];
} TieKeyboardBindings;

TieInputAction TieInputActions_FromName(const char* name);
const char* TieInputActions_ToName(TieInputAction action);
const char* TieInputActions_DisplayName(TieInputAction action);
TieInputActionCategory TieInputActions_Category(TieInputAction action);
const char* TieInputActions_CategoryName(TieInputActionCategory category);

void TieInputActions_InstallKeyboard(const TieKeyboardBindings* bindings);
void TieInputActions_DispatchController(TieInputAction action, bool pressed);
bool TieInputActions_DispatchKeyboard(int scancode, bool pressed);

extern uint16_t TieInputActions_VirtualButtons;

#endif
