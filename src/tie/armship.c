#include <stdint.h>
#include <string.h>

#include "tie/armship.h"
#include "tie/player.h"
#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include <landru/task.h>

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

/* ======================================================================
 * Static data
 * ====================================================================== */

/* Torpedo type → actor state mapping (indexed by torp_used - 1) */
static const int16_t torp_state[7] = { 2, 3, 4, 0, 5, 1, 6 };

static const char armship_str[] = "launch.lfd";

/* ======================================================================
 * Static BSS globals
 * ====================================================================== */

static Input* button_input[6];
static Input* world_input;
static Actor* torp_name_actor;
static Actor* beam_name_actor;
static Film* armship_film;
static ResFile* armship_file;
static Input* info_input;

/* ======================================================================
 * end_ArmShip_View — cursor show callback
 * ====================================================================== */

static void end_ArmShip_View(int32_t refresh) {
	if (refresh)
		return;
	if (lcursor_Is_Cursor_Visible())
		return;
	lcursor_Show_Cursor();
}

/* ======================================================================
 * user_ArmShip — actor callback for weapon selector highlights
 * ====================================================================== */

static void user_ArmShip(Actor* actor, int32_t time) {
	int16_t id = actor->var1;
	(void)time;

	switch (id) {
		case 1: /* Beam selector: state = beam type + 6 */
			lactor_Set_Actor_State(actor, player_Get_Beam_Used() + 6, 0);
			return;
		case 2: /* Torpedo selector: state from lookup table */
			lactor_Set_Actor_State(actor, torp_state[player_Get_Torp_Used() - 1], 0);
			return;
		case 3: { /* Button hover highlight (beam/torp buttons 0-3) */
			int16_t hover = 0;
			for (int16_t i = 0; i < 4; i++) {
				if (button_input[i] && linpattr_Is_Input_Flag1(button_input[i])) {
					hover = i + 1;
					break;
				}
			}
			if (hover) {
				lactor_Set_Actor_State(actor, hover - 1, 0);
				lactor_Show_Actor(actor);
			} else {
				lactor_Hide_Actor(actor);
			}
			return;
		}
		case 6: { /* Enter/exit button hover (buttons 4-5) */
			int16_t hover = 0;
			for (int16_t i = 0; i < 2; i++) {
				if (button_input[i + 4] && linpattr_Is_Input_Flag1(button_input[i + 4])) {
					hover = i + 1;
					break;
				}
			}
			if (hover) {
				lactor_Set_Actor_State(actor, hover - 1, 0);
				lactor_Show_Actor(actor);
			} else {
				lactor_Hide_Actor(actor);
			}
			return;
		}
	}
}

/* ======================================================================
 * draw_ArmShip — actor draw callback for weapon name labels
 * ====================================================================== */

static int draw_ArmShip(Actor* actor, Rect* frame, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	char line1[16], line2[16];
	Rect dst;
	(void)clip_r;
	(void)x;
	(void)y;

	if (!refresh)
		return 1;

	lrect_Copy_Rect(&dst, frame);
	line1[0] = '\0';
	line2[0] = '\0';

	if (actor->id == 10) {
		/* Beam name label */
		int16_t beam = player_Get_Beam_Used();
		if (beam == 1) {
			textext_Copy_Text(line1, txtArmTractor);
			textext_Copy_Text(line2, txtArmBeam);
		} else if (beam == 2) {
			textext_Copy_Text(line1, txtArmJamming);
			textext_Copy_Text(line2, txtArmBeam);
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
		/* Retail captures `top + 7` BEFORE incrementing top, so the
		 * resulting bottom is new_top + 6, not new_top + 7. */
		int16_t saved_bottom = dst.top + 7;
		dst.top++;
		dst.bottom = saved_bottom;
	}
	lfont_Print_Centered_Text(line1, &dst, 15, 1);
	if (line2[0]) {
		lrect_Offset_Rect(&dst, 0, 6);
		lfont_Print_Centered_Text(line2, &dst, 15, 1);
	}
	return 1;
}

/* ======================================================================
 * film_ArmShip_Callback — conditional actor visibility
 * ====================================================================== */

static int16_t film_ArmShip_Callback(Film* film, FilmObject* film_obj) {
	/* Only process actor rewind events (type 3) */
	if ((int16_t)film_obj->id != 3)
		return 0;

	lfilm_Rewind_Actor_Film(film, film_obj, (char*)film_obj + sizeof(FilmObject));
	Actor* actor = (Actor*)film_obj->object;

	switch (actor->var1) {
		case 1: /* Beam selector — skip if no beam equipped */
			if (!player_Get_Beam_Used())
				return 1;
			lactor_Set_Actor_User_Function(actor, user_ArmShip);
			return 0;
		case 2: /* Torpedo selector — skip if no torpedo equipped */
			if (!player_Get_Torp_Used())
				return 1;
			lactor_Set_Actor_User_Function(actor, user_ArmShip);
			return 0;
		case 3: /* Always active — install callback */
		case 6:
			lactor_Set_Actor_User_Function(actor, user_ArmShip);
			return 0;
		case 4: /* Beam-only visibility gate — skip if beam equipped */
			return player_Get_Beam_Used() ? 1 : 0;
		case 5: /* Torpedo-only visibility gate — skip if torpedo equipped */
			return player_Get_Torp_Used() ? 1 : 0;
	}
	return 0;
}

/* ======================================================================
 * iupdate_ArmShip — click tracking for weapon buttons
 * ====================================================================== */

static int16_t iupdate_ArmShip(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
							   int16_t x, int16_t y) {
	(void)clip_r;

	if (key)
		return 0;

	uint8_t button = 0;
	if (left)
		button = left;
	if (right)
		button = right;

	if (button == 1) {
		linpattr_Set_Input_Flag1(input);
		soundext_Play_SFX(sfxButton, 80);
	} else if (button == 2) {
		if (lrect_Point_In_Rect(r, r->left + x, r->top + y))
			linpattr_Set_Input_Flag1(input);
		else
			linpattr_Clear_Input_Flag1(input);
	} else if (button == 3) {
		if (linpattr_Is_Input_Flag1(input)) {
			linpattr_Clear_Input_Flag1(input);
			linpattr_Selected_Input(input);
		}
	}
	return 1;
}

/* ======================================================================
 * iuser_ArmShip — weapon button actions
 * ====================================================================== */

static void iuser_ArmShip(Input* input, int32_t time) {
	(void)time;

	if (!linpattr_Get_Input_Selected(input))
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
				lerror_Set_Landru_Exit(SCENE_CUT_BATTLE_270);
			else
				lerror_Set_Landru_Exit(SCENE_FLIGHT_BATTLE);
			break;
		case 6: { /* Exit */
			if (shellext_Get_Last_Scene() == SCENE_DEBRIEF) {
				lerror_Set_Landru_Exit(SCENE_DEBRIEF);
				char name[36];
				shipext_Get_Pilot_Name(name, sizeof(name));
				shipext_Load_Pilot(name);
			} else {
				lerror_Set_Landru_Exit(SCENE_BRIEF);
			}
			break;
		}
	}
}

/* ======================================================================
 * iuser_Arm_Info — info panel init callback
 * ====================================================================== */

static void iuser_Arm_Info(Input* input, int32_t time) {
	(void)input;
	if (!time)
		shade_Build_Shaded_Palette();
}

/* ======================================================================
 * idraw_Arm_Info — weapon description text panel
 * ====================================================================== */

static void idraw_Arm_Info(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	char text_buf[80];
	Rect dst;

	(void)input;
	(void)clip_r;

	if (!refresh)
		return;

	/* Beam description (top half) */
	if (player_Get_Beam_Used()) {
		lrect_Copy_Rect(&dst, r);
		dst.bottom = dst.top + 34;
		shade_Draw_Talk_Shade_Rect(&dst);
		dst.bottom = dst.top + 12;
		dst.top += 2;
		for (int16_t i = 0; i < 3; i++) {
			int16_t beam = player_Get_Beam_Used();
			textext_Get_Weapon_Select_Text(text_buf, 3 * (beam + 6) + i);
			lfont_Print_Centered_Text(text_buf, &dst, 15, 0);
			lrect_Offset_Rect(&dst, 0, 10);
		}
	}

	/* Torpedo description (bottom half) */
	if (player_Get_Torp_Used()) {
		lrect_Copy_Rect(&dst, r);
		dst.top = dst.bottom - 34;
		shade_Draw_Talk_Shade_Rect(&dst);
		dst.bottom = dst.top + 12;
		dst.top += 2;
		for (int16_t i = 0; i < 3; i++) {
			int16_t torp = player_Get_Torp_Used();
			textext_Get_Weapon_Select_Text(text_buf, 3 * (torp - 1) + i);
			lfont_Print_Centered_Text(text_buf, &dst, 15, 0);
			lrect_Offset_Rect(&dst, 0, 10);
		}
	}
}

/* ======================================================================
 * armship_ArmShip — main entry point
 * ====================================================================== */

typedef enum {
	ARMSHIP_PHASE_BEGIN = 0,
	ARMSHIP_PHASE_CLEANUP = 1,
} ArmShipPhase;

typedef struct ArmShipTask {
	SceneHeadStruct* scene_head;
	ResFile* launch_res;
	ArmShipPhase phase;
} ArmShipTask;

static LandruTaskStepResult armship_task_step(void* self) {
	ArmShipTask* t = (ArmShipTask*)self;

	if (t->phase == ARMSHIP_PHASE_BEGIN) {
		Rect frame;
		char name[32];
		int16_t i;

		lio_Set_Mouse_Position(44, 166);
		armship_file = shellext_Open_Empire_Resource(armship_str);
		t->launch_res = (ResFile*)(uintptr_t)shipext_Open_Launch_Resource();

		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();

		lrect_Set_Rect(&frame, 0, 0, 320, 200);
		shipext_Get_Weapon_Select_Name(name);
		armship_film = lfilm_Res_Callback_Film(name, &frame, 0, 0, 0, film_ArmShip_Callback);
		lfilm_Set_Film_Def_Palette(armship_film, t->scene_head->def_palette);

		lrect_Set_Rect(&frame, 0, 0, 320, 200);
		world_input = linput_Alloc_Input(NULL, &frame, 0, 0);

		/* Create 6 weapon buttons */
		for (i = 0; i < 6; i++) {
			switch (i) {
				case 0:
					lrect_Set_Rect(&frame, 13, 66, 46, 78);
					break;
				case 1:
					lrect_Set_Rect(&frame, 46, 66, 79, 78);
					break;
				case 2:
					lrect_Set_Rect(&frame, 13, 82, 46, 94);
					break;
				case 3:
					lrect_Set_Rect(&frame, 46, 82, 79, 94);
					break;
				case 4:
					lrect_Set_Rect(&frame, 2, 157, 89, 172);
					break;
				case 5:
					lrect_Set_Rect(&frame, 2, 174, 89, 189);
					break;
			}

			/* Only create beam buttons (0-1) if beam is equipped */
			if (player_Get_Beam_Used() || i >= 2) {
				button_input[i] = linput_Alloc_Input(world_input, &frame, 0, 0);
				linpattr_Set_Input_Update_Function(button_input[i], iupdate_ArmShip);
				linpattr_Set_Input_User_Function(button_input[i], iuser_ArmShip);
				button_input[i]->id = i + 1;
				button_input[i]->mouseUsage = downMoveUpInput;
			} else {
				button_input[i] = NULL;
			}
		}

		/* Info panel for weapon description text */
		lrect_Set_Rect(&frame, 92, 12, 260, 152);
		info_input = linput_Alloc_Input(world_input, &frame, 0, 0);
		linpattr_Set_Input_User_Function(info_input, iuser_Arm_Info);
		linpattr_Set_Input_Draw_Function(info_input, idraw_Arm_Info);
		linpattr_Refreshable_Input(info_input);

		/* Beam name label actor */
		lrect_Set_Rect(&frame, 12, 45, 81, 58);
		beam_name_actor = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, 0);
		lactor_Set_Actor_Draw_Function(beam_name_actor, draw_ArmShip);
		beam_name_actor->id = 10;

		/* Torpedo name label actor */
		lrect_Set_Rect(&frame, 12, 101, 81, 114);
		torp_name_actor = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, 0);
		lactor_Set_Actor_Draw_Function(torp_name_actor, draw_ArmShip);
		torp_name_actor->id = 11;

		/* Push the modal view task */
		lview_Set_View_Update_Function(end_ArmShip_View);
		lviewadd_Push_Handle_View_Task();

		t->phase = ARMSHIP_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	linpcall_Clear_Active_Input();
	lview_Clear_View_Update_Function();
	lview_Enable_All_View_Erase();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	lres_Close_Resource(t->launch_res);
	lres_Close_Resource(armship_file);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable armship_task_vt = {
	.step = armship_task_step,
};

void armship_Push_ArmShip_Task(SceneHeadStruct* scene_head) {
	ArmShipTask* t = (ArmShipTask*)landru_task_push(&armship_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->launch_res = NULL;
	t->phase = ARMSHIP_PHASE_BEGIN;
}
