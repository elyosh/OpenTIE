/*
 * Action layer implementation. See actions.h for the rationale.
 */
#include "tie_runtime/input/actions.h"

#include "tie_runtime/input/input.h"

#include <stdint.h>
#include <string.h>

/* ---------------- Action catalog ---------------- */

typedef enum TieInputActionKind {
	TIE_INPUT_ACTION_KIND_NONE = 0,
	TIE_INPUT_ACTION_KIND_BUTTON_BIT,     /* param = bit index (0..3) */
	TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, /* param = ASCII byte / collapsed UserKey */
	TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN,  /* param = DOS scan code (engine sees scan+128) */
} TieInputActionKind;

typedef struct TieInputActionDef {
	const char* name;
	const char* display_name;
	TieInputActionCategory category;
	TieInputActionKind kind;
	uint16_t param;
} TieInputActionDef;

/* Action -> (kind, param) table. Indexed by TieInputAction. */
static const TieInputActionDef g_action_defs[TIE_INPUT_ACTION_COUNT] = {
	[TIE_INPUT_ACTION_NONE] = { "none", "None", TIE_INPUT_ACTION_CATEGORY_SYSTEM, TIE_INPUT_ACTION_KIND_NONE,
								0 },

	[TIE_INPUT_ACTION_FIRE_WEAPON] = { "fire_weapon", "Fire Weapon / Warhead",
									   TIE_INPUT_ACTION_CATEGORY_WEAPONS, TIE_INPUT_ACTION_KIND_BUTTON_BIT,
									   0 },
	[TIE_INPUT_ACTION_TARGET_ROLL_THROTTLE_MODIFIER] = { "target_roll_throttle_modifier",
														 "Target / Roll-Throttle Modifier",
														 TIE_INPUT_ACTION_CATEGORY_TARGETING,
														 TIE_INPUT_ACTION_KIND_BUTTON_BIT, 1 },

	/* Targeting */
	[TIE_INPUT_ACTION_TARGET_NEXT] = { "target_next", "Next Target", TIE_INPUT_ACTION_CATEGORY_TARGETING,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 't' },
	[TIE_INPUT_ACTION_TARGET_PREV] = { "target_prev", "Previous Target", TIE_INPUT_ACTION_CATEGORY_TARGETING,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'y' },
	[TIE_INPUT_ACTION_TARGET_NEAREST_FIGHTER_OR_MINE] = { "target_nearest_fighter_or_mine",
														  "Nearest Enemy Fighter / Mine",
														  TIE_INPUT_ACTION_CATEGORY_TARGETING,
														  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'r' },
	[TIE_INPUT_ACTION_TARGET_MY_ATTACKER] = { "target_my_attacker", "My Attacker",
											  TIE_INPUT_ACTION_CATEGORY_TARGETING,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'e' },
	[TIE_INPUT_ACTION_TARGET_ATTACKER_CHAIN] = { "target_attacker_chain", "Target's Attacker",
												 TIE_INPUT_ACTION_CATEGORY_TARGETING,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'a' },
	[TIE_INPUT_ACTION_TARGET_NEWEST_CRAFT] = { "target_newest_craft", "Newest Craft",
											   TIE_INPUT_ACTION_CATEGORY_TARGETING,
											   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'u' },
	[TIE_INPUT_ACTION_TARGET_CLEAR] = { "target_clear", "Clear Target", TIE_INPUT_ACTION_CATEGORY_TARGETING,
										TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'o' },
	[TIE_INPUT_ACTION_AUTO_TARGET] = { "auto_target", "Target Under Crosshair",
									   TIE_INPUT_ACTION_CATEGORY_TARGETING,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x78 /* Alt+1 */ },
	[TIE_INPUT_ACTION_CYCLE_WARHEAD_VIEW] = { "cycle_warhead_view", "Cycle Warhead View",
											  TIE_INPUT_ACTION_CATEGORY_VIEW,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x3C /* F2 */ },

	/* Throttle / engine */
	[TIE_INPUT_ACTION_THROTTLE_UP] = { "throttle_up", "Increase Throttle", TIE_INPUT_ACTION_CATEGORY_THROTTLE,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '=' },
	[TIE_INPUT_ACTION_THROTTLE_DOWN] = { "throttle_down", "Decrease Throttle",
										 TIE_INPUT_ACTION_CATEGORY_THROTTLE,
										 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '-' },
	[TIE_INPUT_ACTION_THROTTLE_FULL] = { "throttle_full", "Full Throttle", TIE_INPUT_ACTION_CATEGORY_THROTTLE,
										 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 8 /* BS */ },
	[TIE_INPUT_ACTION_MATCH_SPEED] = { "match_speed", "Match Target Speed",
									   TIE_INPUT_ACTION_CATEGORY_THROTTLE,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 13 /* CR */ },

	/* Weapons */
	[TIE_INPUT_ACTION_CYCLE_WEAPON_GROUP] = { "cycle_weapon_group", "Cycle Weapon Group",
											  TIE_INPUT_ACTION_CATEGORY_WEAPONS,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'w' },
	[TIE_INPUT_ACTION_CYCLE_WEAPON_FIRING_MODE] = { "cycle_weapon_firing_mode", "Cycle Weapon Firing Mode",
													TIE_INPUT_ACTION_CATEGORY_WEAPONS,
													TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'x' },
	[TIE_INPUT_ACTION_TOGGLE_BEAM] = { "toggle_beam", "Toggle Beam Weapon", TIE_INPUT_ACTION_CATEGORY_WEAPONS,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'b' },
	[TIE_INPUT_ACTION_XFER_CANNON_TO_SHIELDS] = { "xfer_cannon_to_shields", "Cannon Energy to Shields",
												  TIE_INPUT_ACTION_CATEGORY_WEAPONS,
												  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '\'' },
	[TIE_INPUT_ACTION_XFER_SHIELDS_TO_CANNON] = { "xfer_shields_to_cannon", "Shield Energy to Cannons",
												  TIE_INPUT_ACTION_CATEGORY_WEAPONS,
												  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, ';' },

	/* Shields */
	[TIE_INPUT_ACTION_CYCLE_SHIELD_MODE] = { "cycle_shield_mode", "Cycle Shield Direction",
											 TIE_INPUT_ACTION_CATEGORY_WEAPONS,
											 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 's' },

	/* View */
	[TIE_INPUT_ACTION_VIEW_RETURN_TO_COCKPIT] = { "view_return_to_cockpit", "Return to Cockpit",
												  TIE_INPUT_ACTION_CATEGORY_VIEW,
												  TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x3B /* F1 */ },
	[TIE_INPUT_ACTION_VIEW_TOGGLE_HIGH_ANGLE] = { "view_toggle_high_angle", "Toggle High-Angle View",
												  TIE_INPUT_ACTION_CATEGORY_VIEW,
												  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '0' },
	[TIE_INPUT_ACTION_VIEW_TOGGLE_EXTERNAL_CAMERA] = { "view_toggle_external_camera",
													   "Toggle External Camera",
													   TIE_INPUT_ACTION_CATEGORY_VIEW,
													   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '/' },
	[TIE_INPUT_ACTION_VIEW_TOGGLE_EXTERNAL_CAMERA_CONTROL] = { "view_toggle_external_camera_control",
															   "Toggle External Camera Control",
															   TIE_INPUT_ACTION_CATEGORY_VIEW,
															   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '*' },
	[TIE_INPUT_ACTION_VIEW_TOGGLE_COCKPIT] = { "view_toggle_cockpit", "Toggle Cockpit",
											   TIE_INPUT_ACTION_CATEGORY_VIEW,
											   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '.' },

	/* Info rooms */
	[TIE_INPUT_ACTION_INFO_GOALS] = { "info_goals", "Mission Goals", TIE_INPUT_ACTION_CATEGORY_INFORMATION,
									  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'g' },
	[TIE_INPUT_ACTION_INFO_MAP] = { "info_map", "Map", TIE_INPUT_ACTION_CATEGORY_INFORMATION,
									TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'm' },
	[TIE_INPUT_ACTION_INFO_MESSAGES] = { "info_messages", "Messages", TIE_INPUT_ACTION_CATEGORY_INFORMATION,
										 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'l' },
	[TIE_INPUT_ACTION_INFO_DAMAGE] = { "info_damage", "Damage Report", TIE_INPUT_ACTION_CATEGORY_INFORMATION,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'd' },
	[TIE_INPUT_ACTION_INFO_HELP] = { "info_help", "Help", TIE_INPUT_ACTION_CATEGORY_INFORMATION,
									 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'k' },

	/* Misc */
	[TIE_INPUT_ACTION_HYPERSPACE] = { "hyperspace", "Engage Hyperspace", TIE_INPUT_ACTION_CATEGORY_SYSTEM,
									  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'h' },
	[TIE_INPUT_ACTION_EJECT] = { "eject", "Eject", TIE_INPUT_ACTION_CATEGORY_SYSTEM,
								 TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x12 /* Alt+E */ },
	[TIE_INPUT_ACTION_RECORDER_TOGGLE] = { "recorder_toggle", "Toggle Flight Recorder",
										   TIE_INPUT_ACTION_CATEGORY_SYSTEM,
										   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'c' },
	[TIE_INPUT_ACTION_PAUSE] = { "pause", "Pause", TIE_INPUT_ACTION_CATEGORY_SYSTEM,
								 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'p' },
	[TIE_INPUT_ACTION_ESCAPE] = { "escape", "Options / Back", TIE_INPUT_ACTION_CATEGORY_SYSTEM,
								  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 27 },

	[TIE_INPUT_ACTION_CONFIRM] = { "confirm", "Confirm Order / Skip", TIE_INPUT_ACTION_CATEGORY_SYSTEM,
								   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, ' ' },
	[TIE_INPUT_ACTION_END_MISSION] = { "end_mission", "End Mission", TIE_INPUT_ACTION_CATEGORY_SYSTEM,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'q' },
	[TIE_INPUT_ACTION_CYCLE_TIME_ACCELERATION] = { "cycle_time_acceleration", "Cycle Time Acceleration",
												   TIE_INPUT_ACTION_CATEGORY_SYSTEM,
												   TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x14 /* Alt+T */ },

	[TIE_INPUT_ACTION_THROTTLE_ZERO] = { "throttle_zero", "Zero Throttle", TIE_INPUT_ACTION_CATEGORY_THROTTLE,
										 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '\\' },
	[TIE_INPUT_ACTION_THROTTLE_ONE_THIRD] = { "throttle_one_third", "One-Third Throttle",
											  TIE_INPUT_ACTION_CATEGORY_THROTTLE,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '[' },
	[TIE_INPUT_ACTION_THROTTLE_TWO_THIRDS] = { "throttle_two_thirds", "Two-Thirds Throttle",
											   TIE_INPUT_ACTION_CATEGORY_THROTTLE,
											   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, ']' },
	[TIE_INPUT_ACTION_TOGGLE_SLAM] = { "toggle_slam", "Toggle SLAM / Overdrive",
									   TIE_INPUT_ACTION_CATEGORY_THROTTLE,
									   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'n' },

	[TIE_INPUT_ACTION_CYCLE_BEAM_RECHARGE_RATE] = { "cycle_beam_recharge_rate", "Cycle Beam Recharge Rate",
													TIE_INPUT_ACTION_CATEGORY_WEAPONS,
													TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x42 /* F8 */ },
	[TIE_INPUT_ACTION_CYCLE_CANNON_RECHARGE_RATE] = { "cycle_cannon_recharge_rate",
													  "Cycle Laser / Cannon Recharge Rate",
													  TIE_INPUT_ACTION_CATEGORY_WEAPONS,
													  TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x43 /* F9 */ },
	[TIE_INPUT_ACTION_CYCLE_SHIELD_RECHARGE_RATE] = { "cycle_shield_recharge_rate",
													  "Cycle Shield Recharge Rate",
													  TIE_INPUT_ACTION_CATEGORY_WEAPONS,
													  TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x44 /* F10 */ },

	[TIE_INPUT_ACTION_TOGGLE_COMPONENT_TRACKING] = { "toggle_component_tracking", "Toggle Component Tracking",
													 TIE_INPUT_ACTION_CATEGORY_TARGETING,
													 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'i' },
	[TIE_INPUT_ACTION_TARGET_COMPONENT_NEXT] = { "target_component_next", "Next Target Component",
												 TIE_INPUT_ACTION_CATEGORY_TARGETING,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, ',' },
	[TIE_INPUT_ACTION_TARGET_COMPONENT_PREV] = { "target_component_prev", "Previous Target Component",
												 TIE_INPUT_ACTION_CATEGORY_TARGETING,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '<' },
	[TIE_INPUT_ACTION_TARGET_PRESET_STORE_1] = { "target_preset_store_1", "Store Target Preset 1",
												 TIE_INPUT_ACTION_CATEGORY_TARGETING,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x58 /* Shift+F5 */ },
	[TIE_INPUT_ACTION_TARGET_PRESET_STORE_2] = { "target_preset_store_2", "Store Target Preset 2",
												 TIE_INPUT_ACTION_CATEGORY_TARGETING,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x59 /* Shift+F6 */ },
	[TIE_INPUT_ACTION_TARGET_PRESET_STORE_3] = { "target_preset_store_3", "Store Target Preset 3",
												 TIE_INPUT_ACTION_CATEGORY_TARGETING,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x5A /* Shift+F7 */ },
	[TIE_INPUT_ACTION_TARGET_PRESET_RECALL_1] = { "target_preset_recall_1", "Recall Target Preset 1",
												  TIE_INPUT_ACTION_CATEGORY_TARGETING,
												  TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x3F /* F5 */ },
	[TIE_INPUT_ACTION_TARGET_PRESET_RECALL_2] = { "target_preset_recall_2", "Recall Target Preset 2",
												  TIE_INPUT_ACTION_CATEGORY_TARGETING,
												  TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x40 /* F6 */ },
	[TIE_INPUT_ACTION_TARGET_PRESET_RECALL_3] = { "target_preset_recall_3", "Recall Target Preset 3",
												  TIE_INPUT_ACTION_CATEGORY_TARGETING,
												  TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x41 /* F7 */ },
	[TIE_INPUT_ACTION_TOGGLE_THREAT_DISPLAY] = { "toggle_threat_display", "Toggle Threat Display",
												 TIE_INPUT_ACTION_CATEGORY_TARGETING,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'z' },

	[TIE_INPUT_ACTION_ROLL_LEFT_INFO_PREVIOUS] = { "roll_left_info_previous", "Roll Left / Previous Info",
												   TIE_INPUT_ACTION_CATEGORY_VIEW,
												   TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN,
												   0x4B /* Left Arrow */ },
	[TIE_INPUT_ACTION_ROLL_RIGHT_INFO_NEXT] = { "roll_right_info_next", "Roll Right / Next Info",
												TIE_INPUT_ACTION_CATEGORY_VIEW,
												TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN, 0x4D /* Right Arrow */ },
	[TIE_INPUT_ACTION_INFO_WINGMAN_COMMANDS] = { "info_wingman_commands", "Wingman Commands",
												 TIE_INPUT_ACTION_CATEGORY_INFORMATION,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'Z' },
	[TIE_INPUT_ACTION_RECORDER_VIEW] = { "recorder_view", "View Current Recording",
										 TIE_INPUT_ACTION_CATEGORY_INFORMATION,
										 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'v' },

	[TIE_INPUT_ACTION_COMM_ASSIGN_TARGET] = { "comm_assign_target", "Assign Target to Wingman",
											  TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'A' },
	[TIE_INPUT_ACTION_COMM_REQUEST_RESUPPLY] = { "comm_request_resupply", "Request Resupply",
												 TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'B' },
	[TIE_INPUT_ACTION_COMM_COVER_ME] = { "comm_cover_me", "Order Wingman to Cover Me",
										 TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
										 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'C' },
	[TIE_INPUT_ACTION_COMM_EVADE] = { "comm_evade", "Order Target to Evade",
									  TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
									  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'E' },
	[TIE_INPUT_ACTION_COMM_CONTINUE_MISSION] = { "comm_continue_mission", "Continue Mission",
												 TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
												 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'G' },
	[TIE_INPUT_ACTION_COMM_HEAD_HOME] = { "comm_head_home", "Order Target to Head Home",
										  TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
										  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'H' },
	[TIE_INPUT_ACTION_COMM_IGNORE_TARGET] = { "comm_ignore_target", "Ignore Current Target",
											  TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'I' },
	[TIE_INPUT_ACTION_COMM_REPORT_ORDERS] = { "comm_report_orders", "Report Current Orders",
											  TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'R' },
	[TIE_INPUT_ACTION_COMM_REINFORCEMENTS] = { "comm_reinforcements", "Request Reinforcements",
											   TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
											   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'S' },
	[TIE_INPUT_ACTION_COMM_STOP_AND_WAIT] = { "comm_stop_and_wait", "Stop and Wait",
											  TIE_INPUT_ACTION_CATEGORY_COMMUNICATIONS,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, 'W' },

	[TIE_INPUT_ACTION_VIEW_LEFT_SHOULDER] = { "view_left_shoulder", "Left Shoulder View",
											  TIE_INPUT_ACTION_CATEGORY_VIEW,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '1' },
	[TIE_INPUT_ACTION_VIEW_REAR] = { "view_rear", "Rear View", TIE_INPUT_ACTION_CATEGORY_VIEW,
									 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '2' },
	[TIE_INPUT_ACTION_VIEW_RIGHT_SHOULDER] = { "view_right_shoulder", "Right Shoulder View",
											   TIE_INPUT_ACTION_CATEGORY_VIEW,
											   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '3' },
	[TIE_INPUT_ACTION_VIEW_LEFT_WING] = { "view_left_wing", "Left Wing View", TIE_INPUT_ACTION_CATEGORY_VIEW,
										  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '4' },
	[TIE_INPUT_ACTION_VIEW_STRAIGHT_UP] = { "view_straight_up", "Straight Up View",
											TIE_INPUT_ACTION_CATEGORY_VIEW,
											TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '5' },
	[TIE_INPUT_ACTION_VIEW_RIGHT_WING] = { "view_right_wing", "Right Wing View",
										   TIE_INPUT_ACTION_CATEGORY_VIEW,
										   TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '6' },
	[TIE_INPUT_ACTION_VIEW_LEFT_FORWARD] = { "view_left_forward", "Left Forward View",
											 TIE_INPUT_ACTION_CATEGORY_VIEW,
											 TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '7' },
	[TIE_INPUT_ACTION_VIEW_FORWARD] = { "view_forward", "Forward View", TIE_INPUT_ACTION_CATEGORY_VIEW,
										TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '8' },
	[TIE_INPUT_ACTION_VIEW_RIGHT_FORWARD] = { "view_right_forward", "Right Forward View",
											  TIE_INPUT_ACTION_CATEGORY_VIEW,
											  TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII, '9' },
};

TieInputAction TieInputActions_FromName(const char* name) {
	if (!name)
		return TIE_INPUT_ACTION_NONE;
	for (int i = 1; i < TIE_INPUT_ACTION_COUNT; ++i) {
		if (!strcmp(g_action_defs[i].name, name))
			return (TieInputAction)i;
	}
	return TIE_INPUT_ACTION_NONE;
}

const char* TieInputActions_ToName(TieInputAction a) {
	if (a <= TIE_INPUT_ACTION_NONE || a >= TIE_INPUT_ACTION_COUNT)
		return "<none>";
	return g_action_defs[a].name;
}

const char* TieInputActions_DisplayName(TieInputAction action) {
	if (action <= TIE_INPUT_ACTION_NONE || action >= TIE_INPUT_ACTION_COUNT)
		return "None";
	return g_action_defs[action].display_name;
}

TieInputActionCategory TieInputActions_Category(TieInputAction action) {
	if (action <= TIE_INPUT_ACTION_NONE || action >= TIE_INPUT_ACTION_COUNT)
		return TIE_INPUT_ACTION_CATEGORY_SYSTEM;
	return g_action_defs[action].category;
}

const char* TieInputActions_CategoryName(TieInputActionCategory category) {
	static const char* const names[TIE_INPUT_ACTION_CATEGORY_COUNT] = {
		"Weapons", "Targeting", "Throttle", "View", "Information", "System", "Communications"
	};
	if (category < 0 || category >= TIE_INPUT_ACTION_CATEGORY_COUNT)
		return "System";
	return names[category];
}

/* ---------------- Published bindings ---------------- */

static TieKeyboardBindings g_bindings;
static bool g_keyboard_held[AERON_KEY_COUNT];
static uint16_t g_action_hold_count[TIE_INPUT_ACTION_COUNT];

uint16_t TieInputActions_VirtualButtons;

static void TieActions_ApplyAction(TieInputAction action, bool pressed) {
	const TieInputActionDef* definition;
	if (action <= TIE_INPUT_ACTION_NONE || action >= TIE_INPUT_ACTION_COUNT)
		return;
	definition = &g_action_defs[action];
	switch (definition->kind) {
		case TIE_INPUT_ACTION_KIND_BUTTON_BIT: {
			uint16_t bit = (uint16_t)(1u << (definition->param & 0xF));
			if (pressed) {
				if (g_action_hold_count[action] != UINT16_MAX)
					++g_action_hold_count[action];
				TieInputActions_VirtualButtons |= bit;
			} else if (g_action_hold_count[action] > 0 && --g_action_hold_count[action] == 0) {
				TieInputActions_VirtualButtons &= (uint16_t)~bit;
			}
			break;
		}
		case TIE_INPUT_ACTION_KIND_KEYPRESS_ASCII:
			if (pressed)
				TieInput_EnqueueKey((int16_t)definition->param);
			break;
		case TIE_INPUT_ACTION_KIND_KEYPRESS_SCAN:
			if (pressed) {
				TieInput_EnqueueKey(0);
				TieInput_EnqueueKey((int16_t)(definition->param & 0xFF));
			}
			break;
		case TIE_INPUT_ACTION_KIND_NONE:
		default:
			break;
	}
}

static void TieActions_Dispatch(TieInputAction action, bool* held, bool pressed) {
	if (*held == pressed)
		return;
	*held = pressed;
	TieActions_ApplyAction(action, pressed);
}

void TieInputActions_InstallKeyboard(const TieKeyboardBindings* bindings) {
	memset(&g_bindings, 0, sizeof g_bindings);
	memset(g_keyboard_held, 0, sizeof g_keyboard_held);
	memset(g_action_hold_count, 0, sizeof g_action_hold_count);
	TieInputActions_VirtualButtons = 0;
	if (bindings)
		g_bindings = *bindings;
}

void TieInputActions_DispatchController(TieInputAction action, bool pressed) {
	TieActions_ApplyAction(action, pressed);
}

bool TieInputActions_DispatchKeyboard(int scancode, bool pressed) {
	TieInputAction action;
	if (scancode < 0 || scancode >= AERON_KEY_COUNT)
		return false;
	action = g_bindings.keyboard[scancode];
	if (action == TIE_INPUT_ACTION_NONE)
		return false;
	TieActions_Dispatch(action, &g_keyboard_held[scancode], pressed);
	return true;
}
