#include "tie/mainmenu.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/mainmenu_task.h"
#include "tie_runtime/runtime/profile.h"
#endif
#include "landru/vesa.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"

#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/cursor.h"
#include "landru/error.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/fourcc.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <string.h>

/* ---- Static data ---- */

/* Per-door SFX volume (indexed by door actor id 0..7) */
// GLOBAL: TIE95 0xcf714
// GLOBAL: TIE98 0x4e4600
static const int16_t door_volume[8] = { 92, 64, 64, 64, 72, 80, 68, 68 };

const MainMenuLayout mainmenu_layout_tie95 = {
	"mainmenu.lfd",
	"mainmenu",
	"main-1",
	{ "m-hang-d", "m-door-1", "m-door-2", "m-door-3", "m-door-4", "m-door-5", "m-door-6", "m-door-7" },
	320,
	200,
	190,
	100,
	{ { 14, 28, 170, 64 },
	  { 0 },
	  { 82, 64, 128, 90 },
	  { 220, 64, 260, 90 },
	  { 272, 68, 304, 94 },
	  { 0, 110, 34, 146 },
	  { 44, 96, 82, 126 },
	  { 130, 86, 168, 106 } },
	{ SCENE_BRIEF, 0, SCENE_TOUR_DESK, SCENE_BLUEPRINT, SCENE_FILM_VIEWER, SCENE_EXIT, SCENE_TRAIN_TRANSITION,
	  SCENE_COMBAT_TRANSITION },
	{ 0, txtMainCustom, 0, txtMainTrain, txtMainCombat, txtMainRegister, txtMainTech, txtMainFilm },
	0,
	4,
};

#ifdef TIE_MODERN
static const MainMenuLayout* active_spec;
#elif defined(TIE98)
static const MainMenuLayout* const active_spec = &mainmenu_layout_tie98;
#else
static const MainMenuLayout* const active_spec = &mainmenu_layout_tie95;
#endif

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xf5d98
// GLOBAL: TIE98 0x584c30
static Actor* door[8]; /* 8 door animation actors */
// GLOBAL: TIE95 0xf5dd0
// GLOBAL: TIE98 0x584c1c
static Film* mainmenu_film;
// GLOBAL: TIE95 0xf5dc0
// GLOBAL: TIE98 0x584c5c
static Actor* title_actor; /* title text overlay delta actor */
// GLOBAL: TIE95 0xf5dcc
// GLOBAL: TIE98 0x584c18
static Input* parent; /* root XINPUT for menu buttons */
// GLOBAL: TIE95 0xf5dbc
// GLOBAL: TIE98 0x584c58
static Input* tour_input; /* Tour Battle (id=0, conditional) */
static Input* menu_input[8];
// GLOBAL: TIE95 0xf5dd8
static Actor* mainmenu_actor; /* main background delta actor */

/* ---- Forward declarations ---- */

static void end_View(int32_t frame_num);
static int16_t iupdate_MainMenu(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void iuser_MainMenu(Input* input, int32_t time);
static void user_Title(Actor* actor, int32_t time);
static int16_t draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
						  int16_t refresh);
static void user_Door(Actor* actor, int32_t time);

/* ================================================================
 * View update callback
 * ================================================================ */

/* On frame 0, shows the cursor if hidden. Shared epilogue with Main_Menu
 * in the binary (JUMPOUT to retn). */
// FUNCTION: TIE95 0x70c8c
// FUNCTION: TIE98 0x44d410
static void end_View(int32_t frame_num) {
	if (frame_num)
		return;
	if (!xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

/* iupdate: hover highlights door + title, click dispatches scene exit. */
// FUNCTION: TIE95 0x70cc0
// FUNCTION: TIE98 0x44d430
static int16_t iupdate_MainMenu(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;

	if (key)
		return 0;

	/* Highlight the door for this menu button */
	door[input->id]->var1 = 1;

	/* Show the title overlay with this button's text id */
	title_actor->var1 = 1;
	title_actor->var2 = input->id;

	/* Check for mouse click (button state 3 = released) */
	if (left != 3 && right != 3)
		return 1;

	switch (input->id) {
		case 0: { /* Tour Battle */
			int16_t ok = shipext_Set_Tour_Battle();
			if (!ok) {
				xinpattr_Hide_Input(input);
				return 1;
			}
			input->var2 = SCENE_BRIEF; /* exit_code */
			input->var1 = 1;           /* exit_pending */
			break;
		}
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			input->var2 = active_spec->exit_scene[input->id];
			input->var1 = 1;
			if (input->id == active_spec->outcome_input_id)
				shipext_Set_Mission_Outcome(16);
			break;
		default:
			break;
	}
	return 1;
}

/* iuser: when exit_pending, triggers the scene transition. */
// FUNCTION: TIE95 0x70dd4
// FUNCTION: TIE98 0x44d550
static void iuser_MainMenu(Input* input, int32_t time) {
	(void)time;
	if (!input->var1)
		return; /* exit_pending */

	if (input->var2 == 180) { /* exit_code == Tour Battle */
		char name[68];
		shipext_Get_Battle_Mission_Name(name);
		shipext_Set_Mission_Name(name);
	}
	xerror_Set_Landru_Exit(input->var2); /* exit_code */
}

/* ================================================================
 * Actor callbacks
 * ================================================================ */

/* Title overlay: show on hover frame, hide otherwise. */
// FUNCTION: TIE95 0x70e14
// FUNCTION: TIE98 0x44d5a0
static void user_Title(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1 == 1) {
		if (!xactor_Is_Actor_Visible(actor))
			xactor_Show_Actor(actor);
		actor->var1 = 0;
	} else {
		if (xactor_Is_Actor_Visible(actor))
			xactor_Hide_Actor(actor);
	}
}

/* Title overlay draw: render the delta actor + centered text label. */
// FUNCTION: TIE95 0x70e78
// FUNCTION: TIE98 0x44d5f0
static int16_t draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
						  int16_t refresh) {
	int16_t offx, offy;
	Rect r;
	char label[32];

	if (!refresh)
		return 0;

	xactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	xactor_Get_Actor_Offset(actor, &offx, &offy);

	xrect_Set_Rect(&r, offx, offy, actor->w + offx, actor->h + offy);

	switch (actor->var2) {
		case 0: { /* Continue Battle N */
			char num[16];
			textext_Copy_Text(label, txtMainContBattle);
			strcat(label, " ");
			textext_Copy_Text(num, (TIEText)(txtMainOne + pilot_record.cur_battle));
			strcat(label, num);
			break;
		}
		case 1:
			strcpy(label, textext_Get_Text(active_spec->title_text[1]));
			break;
		case 2: /* New/Change/View TOD */
			if (shipext_Find_Battle()) {
				if (pilot_record.battle_status[pilot_record.cur_battle] == 1)
					strcpy(label, textext_Get_Text(txtMainChangeBattle));
				else
					strcpy(label, textext_Get_Text(txtMainNewBattle));
			} else {
				strcpy(label, textext_Get_Text(txtMainViewTOD));
			}
			break;
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			strcpy(label, textext_Get_Text(active_spec->title_text[actor->var2]));
			break;
		default:
			label[0] = 0;
			break;
	}

	/* Drop shadow: dark color at (1,1) offset, then bright at (0,0) */
	xrect_Offset_Rect(&r, 1, 1);
	xfont_Print_Centered_Text(label, &r, 16, active_spec->title_font);
	xrect_Offset_Rect(&r, -1, -1);
	xfont_Print_Centered_Text(label, &r, 15, active_spec->title_font);
	return 1;
}

/* Door animation: open when var1 set (hover), close when cleared. */
// FUNCTION: TIE95 0x70ff8
// FUNCTION: TIE98 0x44d7e0
static void user_Door(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1) {
		/* Opening */
		if (!actor->state)
			soundext_Play_SFX(sfxSmallDoorOpen, door_volume[actor->id]);
		if (actor->state < actor->arraySize - 1)
			xactor_Set_Actor_State(actor, actor->state + 1, 0);
		actor->var1 = 0;
	} else {
		/* Closing */
		if (actor->state > 0) {
			xactor_Set_Actor_State(actor, actor->state - 1, 0);
			if (!actor->state)
				soundext_Play_SFX(sfxSmallDoorShut, door_volume[actor->id]);
		}
	}
}

/* ================================================================
 * Entry point
 * ================================================================ */

// FUNCTION: TIE95 0x70820
// FUNCTION: TIE98 0x44CEB0
int16_t mainmenu_Main_Menu(SceneHeadStruct* scene_head) {
	ResFile* resource;
	Rect frame;
	int16_t i;

#ifdef TIE_MODERN
	active_spec = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? &mainmenu_layout_tie98
																		: &mainmenu_layout_tie95;
#endif
#ifdef TIE_MODERN
	xio_Set_Mouse_Position(active_spec->mouse_x, active_spec->mouse_y);
#elif defined(TIE98)
	xio_Set_Mouse_Position(387, 387);
#else
	xio_Set_Mouse_Position(190, 100);
#endif

#ifdef TIE_MODERN
	resource = shellext_Open_Empire_Resource(active_spec->archive);
#elif defined(TIE98)
	resource = shellext_Open_Empire_Resource("mm640.lfd");
#else
	resource = shellext_Open_Empire_Resource("mainmenu.lfd");
#endif
#ifdef TIE_MODERN
	if (!resource) {
		TieMainMenu_RunView(resource, active_spec->archive);
		return 0;
	}
#endif
#ifdef TIE_MODERN
	xrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);
#elif defined(TIE98)
	xrect_Set_Rect(&frame, 0, 0, 640, 480);
#else
	xrect_Set_Rect(&frame, 0, 0, 320, 200);
#endif

#ifdef TIE_MODERN
	mainmenu_film = xfilm_Res_Film(active_spec->film, &frame, 0, 0, 0);
#elif defined(TIE98)
	mainmenu_film = xfilm_Res_Film("main_00", &frame, 0, 0, 0);
#else
	mainmenu_film = xfilm_Res_Film("mainmenu", &frame, 0, 0, 0);
#endif
#ifdef TIE_MODERN
	if (!mainmenu_film) {
		TieMainMenu_RunView(resource, active_spec->film);
		return 0;
	}
#endif
	xfilm_Set_Film_Def_Palette(mainmenu_film, scene_head->def_palette);

#ifdef TIE_MODERN
	/* Find the background actor */
	mainmenu_actor = active_spec->background ? xactor_Find_Actor(FOURCC_DELT, active_spec->background) : NULL;
#ifdef TIE_MODERN
	if (active_spec->background && !mainmenu_actor) {
		TieMainMenu_RunView(resource, active_spec->background);
		return 0;
	}
#endif
	if (mainmenu_actor)
		xactor_Non_Refreshable_Actor(mainmenu_actor);

#elif defined(TIE98)

#else
	mainmenu_actor = xactor_Find_Actor(FOURCC_DELT, "main-1");
	xactor_Non_Refreshable_Actor(mainmenu_actor);
#endif

#ifdef TIE_MODERN
	for (i = 0; i < 8; i++) {
		door[i] = xactor_Find_Actor(FOURCC_ANIM, active_spec->door_names[i]);
		if (!door[i]) {
			TieMainMenu_RunView(resource, active_spec->door_names[i]);
			return 0;
		}
	}
#elif defined(TIE98)
	door[0] = xactor_Find_Actor(FOURCC_ANIM, "hr_mhngr");
	door[1] = xactor_Find_Actor(FOURCC_ANIM, "hr_md3_1");
	door[2] = xactor_Find_Actor(FOURCC_ANIM, "hr_md2_1");
	door[3] = xactor_Find_Actor(FOURCC_ANIM, "hr_md1_2");
	door[4] = xactor_Find_Actor(FOURCC_ANIM, "hr_md1_3");
	door[5] = xactor_Find_Actor(FOURCC_ANIM, "hr_md1_1");
	door[6] = xactor_Find_Actor(FOURCC_ANIM, "hr_md2_2");
	door[7] = xactor_Find_Actor(FOURCC_ANIM, "hr_md2_3");
#else
	door[0] = xactor_Find_Actor(FOURCC_ANIM, "m-hang-d");
	door[1] = xactor_Find_Actor(FOURCC_ANIM, "m-door-1");
	door[2] = xactor_Find_Actor(FOURCC_ANIM, "m-door-2");
	door[3] = xactor_Find_Actor(FOURCC_ANIM, "m-door-3");
	door[4] = xactor_Find_Actor(FOURCC_ANIM, "m-door-4");
	door[5] = xactor_Find_Actor(FOURCC_ANIM, "m-door-5");
	door[6] = xactor_Find_Actor(FOURCC_ANIM, "m-door-6");
	door[7] = xactor_Find_Actor(FOURCC_ANIM, "m-door-7");
#endif
	for (i = 0; i < 8; i++) {
		xactor_Set_Actor_User_Function(door[i], (xactorCallback)user_Door);
		door[i]->id = i;
	}

	/* Create title text overlay */
	title_actor = xactdelt_Res_Delta_Actor("title", &frame, 0, 0, 0);
#ifdef TIE_MODERN
	if (!title_actor) {
		TieMainMenu_RunView(resource, "title");
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(title_actor, (xactorCallback)user_Title);
	xactor_Set_Actor_Draw_Function(title_actor, draw_Title);

	/* Create XINPUT button regions */
	parent = xinput_Alloc_Input(NULL, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!parent) {
		TieMainMenu_RunView(resource, "main-menu input root");
		return 0;
	}
#endif

	/* Tour Battle button (only if current battle is active) */
	if (pilot_record.battle_status[pilot_record.cur_battle] == 1) {
#ifdef TIE_MODERN
		const int16_t* b = active_spec->button_bounds[0];
		xrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
#elif defined(TIE98)
		xrect_Set_Rect(&frame, 19, 66, 340, 142);
#else
		xrect_Set_Rect(&frame, 14, 28, 170, 64);
#endif
		tour_input = xinput_Alloc_Input(parent, &frame, 0, 0);
#ifdef TIE_MODERN
		if (!tour_input) {
			TieMainMenu_RunView(resource, "tour input");
			return 0;
		}
#endif
		xinpattr_Set_Input_Update_Function(tour_input, iupdate_MainMenu);
		xinpattr_Set_Input_User_Function(tour_input, iuser_MainMenu);
		tour_input->mouseUsage = allInput;
		tour_input->id = 0;
	}

#ifdef TIE_MODERN
	{
		const int16_t* b = active_spec->button_bounds[2];
		xrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
	}
#elif defined(TIE98)
	xrect_Set_Rect(&frame, 172, 163, 248, 210);
#else
	xrect_Set_Rect(&frame, 82, 64, 128, 90);
#endif
	menu_input[2] = xinput_Alloc_Input(parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!menu_input[2]) {
		TieMainMenu_RunView(resource, "main-menu input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(menu_input[2], iupdate_MainMenu);
	xinpattr_Set_Input_User_Function(menu_input[2], iuser_MainMenu);
	menu_input[2]->mouseUsage = allInput;
	menu_input[2]->id = 2;
#ifdef TIE_MODERN
	{
		const int16_t* b = active_spec->button_bounds[3];
		xrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
	}
#elif defined(TIE98)
	xrect_Set_Rect(&frame, 104, 237, 154, 299);
#else
	xrect_Set_Rect(&frame, 220, 64, 260, 90);
#endif
	menu_input[3] = xinput_Alloc_Input(parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!menu_input[3]) {
		TieMainMenu_RunView(resource, "main-menu input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(menu_input[3], iupdate_MainMenu);
	xinpattr_Set_Input_User_Function(menu_input[3], iuser_MainMenu);
	menu_input[3]->mouseUsage = allInput;
	menu_input[3]->id = 3;
#ifdef TIE_MODERN
	{
		const int16_t* b = active_spec->button_bounds[4];
		xrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
	}
#elif defined(TIE98)
	xrect_Set_Rect(&frame, 266, 212, 331, 260);
#else
	xrect_Set_Rect(&frame, 272, 68, 304, 94);
#endif
	menu_input[4] = xinput_Alloc_Input(parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!menu_input[4]) {
		TieMainMenu_RunView(resource, "main-menu input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(menu_input[4], iupdate_MainMenu);
	xinpattr_Set_Input_User_Function(menu_input[4], iuser_MainMenu);
	menu_input[4]->mouseUsage = allInput;
	menu_input[4]->id = 4;
#ifdef TIE_MODERN
	{
		const int16_t* b = active_spec->button_bounds[5];
		xrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
	}
#elif defined(TIE98)
	xrect_Set_Rect(&frame, 0, 268, 55, 357);
#else
	xrect_Set_Rect(&frame, 0, 110, 34, 146);
#endif
	menu_input[5] = xinput_Alloc_Input(parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!menu_input[5]) {
		TieMainMenu_RunView(resource, "main-menu input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(menu_input[5], iupdate_MainMenu);
	xinpattr_Set_Input_User_Function(menu_input[5], iuser_MainMenu);
	menu_input[5]->mouseUsage = allInput;
	menu_input[5]->id = 5;
#ifdef TIE_MODERN
	{
		const int16_t* b = active_spec->button_bounds[6];
		xrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
	}
#elif defined(TIE98)
	xrect_Set_Rect(&frame, 433, 159, 512, 215);
#else
	xrect_Set_Rect(&frame, 44, 96, 82, 126);
#endif
	menu_input[6] = xinput_Alloc_Input(parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!menu_input[6]) {
		TieMainMenu_RunView(resource, "main-menu input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(menu_input[6], iupdate_MainMenu);
	xinpattr_Set_Input_User_Function(menu_input[6], iuser_MainMenu);
	menu_input[6]->mouseUsage = allInput;
	menu_input[6]->id = 6;
#ifdef TIE_MODERN
	{
		const int16_t* b = active_spec->button_bounds[7];
		xrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
	}
#elif defined(TIE98)
	xrect_Set_Rect(&frame, 549, 168, 604, 226);
#else
	xrect_Set_Rect(&frame, 130, 86, 168, 106);
#endif
	menu_input[7] = xinput_Alloc_Input(parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!menu_input[7]) {
		TieMainMenu_RunView(resource, "main-menu input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(menu_input[7], iupdate_MainMenu);
	xinpattr_Set_Input_User_Function(menu_input[7], iuser_MainMenu);
	menu_input[7]->mouseUsage = allInput;
	menu_input[7]->id = 7;

	xview_Set_View_Update_Function(end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
#ifdef TIE_MODERN
	TieMainMenu_RunView(resource, NULL);
	return 0;
#else
	shellext_Handle_TIE_View();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();
	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	xres_Close_Resource(resource);
#ifdef TIE98
	xvesa_Erase_Video(16);
#endif
	return xerror_Get_Landru_Exit();
#endif
}
