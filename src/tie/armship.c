#include "tie/armship.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/armship_task.h"
#endif
#include "tie/player.h"
#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"

#include "landru/actcust.h"
#include "landru/actor.h"
#include "landru/cursor.h"
#include "landru/error.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/inpcall.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <stdint.h>
#include <string.h>

/* ======================================================================
 * Static data
 * ====================================================================== */

/* Torpedo type → actor state mapping (indexed by torp_used - 1) */
// GLOBAL: TIE95 0xce730
// GLOBAL: TIE98 0x4df190
static const int16_t torp_state[7] = { 2, 3, 4, 0, 5, 1, 6 };

// GLOBAL: TIE95 0xce73e
// GLOBAL: TIE98 0x4df1a0
static const char armship_str[] = "launch.lfd";

/* ======================================================================
 * Static BSS globals
 * ====================================================================== */

// GLOBAL: TIE95 0xf59a8
// GLOBAL: TIE98 0x4fa4a8
static Input* button_input[6];
// GLOBAL: TIE95 0xf59c0
// GLOBAL: TIE98 0x4fa4a0
static Input* world_input;
// GLOBAL: TIE95 0xf59c4
// GLOBAL: TIE98 0x4fa490
static Actor* torp_name_actor;
// GLOBAL: TIE95 0xf59c8
// GLOBAL: TIE98 0x4fa494
static Actor* beam_name_actor;
// GLOBAL: TIE95 0xf59cc
// GLOBAL: TIE98 0x4fa498
static Film* armship_film;
// GLOBAL: TIE95 0xf59d0
// GLOBAL: TIE98 0x4fa49c
ResFile* armship_file;
// GLOBAL: TIE95 0xf59d4
// GLOBAL: TIE98 0x4fa4a4
static Input* info_input;

static void armship_end_ArmShip_View(int32_t refresh);
static int16_t armship_film_ArmShip_Callback(Film* film, FilmObject* film_obj);
static int16_t armship_iupdate_ArmShip(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
									   uint8_t right, int16_t x, int16_t y);
static void armship_iuser_ArmShip(Input* input, int32_t time);
static void armship_iuser_Arm_Info(Input* input, int32_t time);
static void armship_idraw_Arm_Info(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void armship_user_ArmShip(Actor* actor, int32_t time);
static int16_t armship_draw_ArmShip(Actor* actor, Rect* frame, Rect* clip_r, int16_t x, int16_t y,
									int16_t refresh);

/* ======================================================================
 * armship_ArmShip — main entry point
 * ====================================================================== */

// FUNCTION: TIE95 0x6EB48
// FUNCTION: TIE98 0x402680
int armship_ArmShip(SceneHeadStruct* scene_head) {
#ifdef TIE_MODERN
	ResFile* launch_resource = NULL;
#else
	ResFile* launch_resource;
#endif
	Rect frame;
	char name[32];
	int16_t i;

	xio_Set_Mouse_Position(44, 166);
	armship_file = shellext_Open_Empire_Resource(armship_str);
#ifdef TIE_MODERN
	if (!armship_file) {
		TieArmShip_RunView(launch_resource, false);
		return 0;
	}
#endif
	launch_resource = shipext_Open_Launch_Resource();
#ifdef TIE_MODERN
	if (!launch_resource) {
		TieArmShip_RunView(launch_resource, false);
		return 0;
	}
#endif

	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();

	xrect_Set_Rect(&frame, 0, 0, 320, 200);
	shipext_Get_Weapon_Select_Name(name);
	armship_film = xfilm_Res_Callback_Film(name, &frame, 0, 0, 0, armship_film_ArmShip_Callback);
#ifdef TIE_MODERN
	if (!armship_film) {
		TieArmShip_RunView(launch_resource, false);
		return 0;
	}
#endif
	xfilm_Set_Film_Def_Palette(armship_film, scene_head->def_palette);

	xrect_Set_Rect(&frame, 0, 0, 320, 200);
	world_input = xinput_Alloc_Input(NULL, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!world_input) {
		TieArmShip_RunView(launch_resource, false);
		return 0;
	}
#endif

	/* Create 6 weapon buttons */
	for (i = 0; i < 6; i++) {
		switch (i) {
			case 0:
				xrect_Set_Rect(&frame, 13, 66, 46, 78);
				break;
			case 1:
				xrect_Set_Rect(&frame, 46, 66, 79, 78);
				break;
			case 2:
				xrect_Set_Rect(&frame, 13, 82, 46, 94);
				break;
			case 3:
				xrect_Set_Rect(&frame, 46, 82, 79, 94);
				break;
			case 4:
				xrect_Set_Rect(&frame, 2, 157, 89, 172);
				break;
			case 5:
				xrect_Set_Rect(&frame, 2, 174, 89, 189);
				break;
		}

		/* Only create beam buttons (0-1) if beam is equipped */
		if (player_Get_Beam_Used() || i >= 2) {
			button_input[i] = xinput_Alloc_Input(world_input, &frame, 0, 0);
#ifdef TIE_MODERN
			if (!button_input[i]) {
				TieArmShip_RunView(launch_resource, false);
				return 0;
			}
#endif
			xinpattr_Set_Input_Update_Function(button_input[i], armship_iupdate_ArmShip);
			xinpattr_Set_Input_User_Function(button_input[i], armship_iuser_ArmShip);
			button_input[i]->id = i + 1;
			button_input[i]->mouseUsage = downMoveUpInput;
		} else {
			button_input[i] = NULL;
		}
	}

	/* Info panel for weapon description text */
	xrect_Set_Rect(&frame, 92, 12, 260, 152);
	info_input = xinput_Alloc_Input(world_input, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!info_input) {
		TieArmShip_RunView(launch_resource, false);
		return 0;
	}
#endif
	xinpattr_Set_Input_User_Function(info_input, armship_iuser_Arm_Info);
	xinpattr_Set_Input_Draw_Function(info_input, armship_idraw_Arm_Info);
	xinpattr_Refreshable_Input(info_input);

	/* Beam name label actor */
	xrect_Set_Rect(&frame, 12, 45, 81, 58);
	beam_name_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 0);
#ifdef TIE_MODERN
	if (!beam_name_actor) {
		TieArmShip_RunView(launch_resource, false);
		return 0;
	}
#endif
	xactor_Set_Actor_Draw_Function(beam_name_actor, armship_draw_ArmShip);
	beam_name_actor->id = 10;

	/* Torpedo name label actor */
	xrect_Set_Rect(&frame, 12, 101, 81, 114);
	torp_name_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 0);
#ifdef TIE_MODERN
	if (!torp_name_actor) {
		TieArmShip_RunView(launch_resource, false);
		return 0;
	}
#endif
	xactor_Set_Actor_Draw_Function(torp_name_actor, armship_draw_ArmShip);
	torp_name_actor->id = 11;

	xview_Set_View_Update_Function(armship_end_ArmShip_View);
#ifdef TIE_MODERN
	TieArmShip_RunView(launch_resource, true);
	return 0;
#else
	shellext_Handle_TIE_View();
	xinpcall_Clear_Active_Input();
	xview_Clear_View_Update_Function();
	xview_Enable_All_View_Erase();
	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	xres_Close_Resource(launch_resource);
	xres_Close_Resource(armship_file);
	return xerror_Get_Landru_Exit();
#endif
}

/* ======================================================================
 * armship_end_ArmShip_View — cursor show callback
 * ====================================================================== */

// FUNCTION: TIE95 0x6ee3c
// FUNCTION: TIE98 0x402a50
static void armship_end_ArmShip_View(int32_t refresh) {
	if (refresh)
		return;
	if (xcursor_Is_Cursor_Visible())
		return;
	xcursor_Show_Cursor();
}

/* ======================================================================
 * armship_film_ArmShip_Callback — conditional actor visibility
 * ====================================================================== */

// FUNCTION: TIE95 0x6ee68
// FUNCTION: TIE98 0x402a70
static int16_t armship_film_ArmShip_Callback(Film* film, FilmObject* film_obj) {
	Actor* actor;
	int16_t result = 0;

	/* Only process actor rewind events (type 3) */
	if (film_obj->id == 3) {
		xfilm_Rewind_Actor_Film(film, film_obj, (char*)film_obj + sizeof(FilmObject));
		actor = (Actor*)film_obj->object;

		switch (actor->var1) {
			case 1: /* Beam selector — skip if no beam equipped */
				if (!player_Get_Beam_Used())
					result = 1;
				else
					xactor_Set_Actor_User_Function(actor, armship_user_ArmShip);
				break;
			case 2: /* Torpedo selector — skip if no torpedo equipped */
				if (!player_Get_Torp_Used())
					result = 1;
				else
					xactor_Set_Actor_User_Function(actor, armship_user_ArmShip);
				break;
			case 3: /* Always active — install callback */
			case 6:
				xactor_Set_Actor_User_Function(actor, armship_user_ArmShip);
				break;
			case 4: /* Beam-only visibility gate — skip if beam equipped */
				if (player_Get_Beam_Used())
					result = 1;
				break;
			case 5: /* Torpedo-only visibility gate — skip if torpedo equipped */
				if (player_Get_Torp_Used())
					result = 1;
				break;
		}
	}
	return result;
}

/* ======================================================================
 * armship_iupdate_ArmShip — click tracking for weapon buttons
 * ====================================================================== */

// FUNCTION: TIE95 0x6eee8
// FUNCTION: TIE98 0x402b30
static int16_t armship_iupdate_ArmShip(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
									   uint8_t right, int16_t x, int16_t y) {
	uint8_t button;
	(void)clip_r;

	if (key)
		return 0;

	if (left)
		button = left;
	if (right)
		button = right;

	switch (button) {
		case 1:
			xinpattr_Set_Input_Flag1(input);
			soundext_Play_SFX(sfxButton, 80);
			break;
		case 2:
			if (xrect_Point_In_Rect(r, x + r->left, y + r->top))
				xinpattr_Set_Input_Flag1(input);
			else
				xinpattr_Clear_Input_Flag1(input);
			break;
		case 3:
			if (xinpattr_Is_Input_Flag1(input)) {
				xinpattr_Clear_Input_Flag1(input);
				xinpattr_Selected_Input(input);
			}
			break;
	}
	return 1;
}

/* ======================================================================
 * armship_iuser_ArmShip — weapon button actions
 * ====================================================================== */

// FUNCTION: TIE95 0x6efb4
// FUNCTION: TIE98 0x402c00
static void armship_iuser_ArmShip(Input* input, int32_t time) {
	(void)time;

	if (!xinpattr_Get_Input_Selected(input) || !input->id)
		return;

	switch (input->id) {
		case 1:
			player_Last_Beam();
			break;
		case 2:
			player_Next_Beam();
			break;
		case 3:
			player_Last_Torp();
			break;
		case 4:
			player_Next_Torp();
			break;
		case 5: /* Enter mission */
			if (shellext_Get_Last_Scene() == SCENE_DEBRIEF)
				shipext_Update_Pilot();
			if (shipext_Is_Mission_Launch())
				xerror_Set_Landru_Exit(SCENE_CUT_BATTLE_270);
			else
				xerror_Set_Landru_Exit(SCENE_FLIGHT_BATTLE);
			break;
		case 6: { /* Exit */
			if (shellext_Get_Last_Scene() == SCENE_DEBRIEF) {
#if defined(TIE98) || defined(TIE_MODERN)
				char name[40];
#else
				char name[32];
#endif
				xerror_Set_Landru_Exit(SCENE_DEBRIEF);
#ifdef TIE_MODERN
				shipext_Get_Pilot_Name(name, sizeof(name));
#else
				shipext_Get_Pilot_Name(name);
#endif
				shipext_Load_Pilot(name);
			} else {
				xerror_Set_Landru_Exit(SCENE_BRIEF);
			}
			break;
		}
	}
}

/* ======================================================================
 * armship_iuser_Arm_Info — info panel init callback
 * ====================================================================== */

// FUNCTION: TIE95 0x6f0a0
// FUNCTION: TIE98 0x402d20
static void armship_iuser_Arm_Info(Input* input, int32_t time) {
	(void)input;
	if (!time)
		shade_Build_Shaded_Palette();
}

/* ======================================================================
 * armship_idraw_Arm_Info — weapon description text panel
 * ====================================================================== */

// FUNCTION: TIE95 0x6f0ac
// FUNCTION: TIE98 0x402d30
static void armship_idraw_Arm_Info(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	char text_buf[80];
	Rect dst;
	int16_t i;

	(void)input;
	(void)clip_r;

	if (!refresh)
		return;

	/* Beam description (top half) */
	if (player_Get_Beam_Used()) {
		xrect_Copy_Rect(&dst, r);
		dst.bottom = dst.top + 34;
		shade_Draw_Talk_Shade_Rect(&dst);
		dst.bottom = dst.top + 12;
		dst.top += 2;
		for (i = 0; i < 3; i++) {
			int16_t beam = player_Get_Beam_Used();
			textext_Get_Weapon_Select_Text(text_buf, 3 * (beam + 6) + i);
			xfont_Print_Centered_Text(text_buf, &dst, 0, 15);
			xrect_Offset_Rect(&dst, 0, 10);
		}
	}

	/* Torpedo description (bottom half) */
	if (player_Get_Torp_Used()) {
		xrect_Copy_Rect(&dst, r);
		dst.top = dst.bottom - 34;
		shade_Draw_Talk_Shade_Rect(&dst);
		dst.bottom = dst.top + 12;
		dst.top += 2;
		for (i = 0; i < 3; i++) {
			int16_t torp = player_Get_Torp_Used();
			textext_Get_Weapon_Select_Text(text_buf, 3 * (torp - 1) + i);
			xfont_Print_Centered_Text(text_buf, &dst, 0, 15);
			xrect_Offset_Rect(&dst, 0, 10);
		}
	}
}

/* ======================================================================
 * armship_user_ArmShip — actor callback for weapon selector highlights
 * ====================================================================== */

// FUNCTION: TIE95 0x6f208
// FUNCTION: TIE98 0x402e80
static void armship_user_ArmShip(Actor* actor, int32_t time) {
	int16_t id = actor->var1;
	(void)time;

	switch (id) {
		case 1: /* Beam selector: state = beam type + 6 */
			xactor_Set_Actor_State(actor, player_Get_Beam_Used() + 6, 0);
			return;
		case 2: /* Torpedo selector: state from lookup table */
			xactor_Set_Actor_State(actor, torp_state[player_Get_Torp_Used() - 1], 0);
			return;
		case 3: { /* Button hover highlight (beam/torp buttons 0-3) */
			int16_t hover = 0;
			int16_t i;
			for (i = 0; i < 4; i++) {
				if (button_input[i] && xinpattr_Is_Input_Flag1(button_input[i])) {
					hover = i + 1;
					break;
				}
			}
			if (hover) {
				xactor_Set_Actor_State(actor, hover - 1, 0);
				xactor_Show_Actor(actor);
			} else {
				xactor_Hide_Actor(actor);
			}
			return;
		}
		case 6: { /* Enter/exit button hover (buttons 4-5) */
			int16_t hover = 0;
			int16_t i;
			for (i = 0; i < 2; i++) {
				if (button_input[i + 4] && xinpattr_Is_Input_Flag1(button_input[i + 4])) {
					hover = i + 1;
					break;
				}
			}
			if (hover) {
				xactor_Set_Actor_State(actor, hover - 1, 0);
				xactor_Show_Actor(actor);
			} else {
				xactor_Hide_Actor(actor);
			}
			return;
		}
	}
}

/* ======================================================================
 * armship_draw_ArmShip — actor draw callback for weapon name labels
 * ====================================================================== */

// FUNCTION: TIE95 0x6f340
// FUNCTION: TIE98 0x402f80
static int16_t armship_draw_ArmShip(Actor* actor, Rect* frame, Rect* clip_r, int16_t x, int16_t y,
									int16_t refresh) {
	char line1[32], line2[16];
	Rect dst;
	(void)clip_r;
	(void)x;
	(void)y;

	if (!refresh)
		return 1;

	xrect_Copy_Rect(&dst, frame);
	line1[0] = '\0';
	line2[0] = '\0';

	if (actor->id == 10) {
		/* Beam name label */
		switch (player_Get_Beam_Used()) {
			case 1:
				textext_Copy_Text(line1, txtArmTractor);
				textext_Copy_Text(line2, txtArmBeam);
				break;
			case 2:
				textext_Copy_Text(line1, txtArmJamming);
				textext_Copy_Text(line2, txtArmBeam);
				break;
		}
	} else {
		/* Torpedo name label */
		int16_t torp = player_Get_Torp_Used();
		switch (torp) {
			case 1:
				textext_Copy_Text(line1, txtArmHeavy);
				textext_Copy_Text(line2, txtArmBomb);
				break;
			case 2:
				textext_Copy_Text(line1, txtArmHeavy);
				textext_Copy_Text(line2, txtArmRocket);
				break;
			case 3:
				textext_Copy_Text(line1, txtArmMissile);
				break;
			case 4:
				textext_Copy_Text(line1, txtArmTorpedo);
				break;
			case 5:
				textext_Copy_Text(line1, txtArmAdvanced);
				textext_Copy_Text(line2, txtArmMissile);
				break;
			case 6:
				textext_Copy_Text(line1, txtArmAdvanced);
				textext_Copy_Text(line2, txtArmTorpedo);
				break;
			case 7:
				textext_Copy_Text(line1, txtArmIon);
				textext_Copy_Text(line2, txtArmTorpedo);
				break;
		}
	}

	if (line2[0]) {
		/* Retail derives bottom from the original top, so the
		 * resulting bottom is new_top + 6, not new_top + 7. */
		int16_t top = dst.top;
		dst.top = top + 1;
		dst.bottom = top + 7;
	}
	xfont_Print_Centered_Text(line1, &dst, 1, 15);
	if (line2[0]) {
		xrect_Offset_Rect(&dst, 0, 6);
		xfont_Print_Centered_Text(line2, &dst, 1, 15);
	}
	return 1;
}
