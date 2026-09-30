#include "tie/shellext.h"
#include "tie/asl.h"
#include "tie/computer.h"
#include "tie/edition.h"
#include "tie/shell.h"
#include "tie/tie.h"
#include "tie_runtime/audio/imuse_session.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/computer_task.h"
#include "tie_runtime/runtime/shell_task.h"
#endif

#include "landru/actanim.h"
#include "landru/actor.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/dialog.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/fade.h"
#include "landru/file.h"
#include "landru/font.h"
#include "landru/io.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/remap.h"
#include "landru/res.h"
#include "landru/style.h"
#include "landru/timer.h"
#include "landru/view.h"
#include "landru/viewadd.h"
#include "tie/computer.h"
#include "tie/frontend_display_tie98.h"
#include "tie/rtsvga2.h"
#include "tie_runtime/runtime/profile.h"

/* --- Globals --- */

// GLOBAL: TIE95 0xF5032
// GLOBAL: TIE98 0x5F32E0
FrontOptionsStruct options_gbl;
// GLOBAL: TIE95 0xCE3D0
// GLOBAL: TIE98 0x4EB920
int32_t f_res = 1;

/* Scene→redirect pairs: when transitions are disabled, these scenes skip
   to their redirect target. Sentinel = 0. */
// GLOBAL: TIE95 0xCE3D4
// GLOBAL: TIE98 0x4EB928
static int16_t transition_check[] = { 120, 121, 130, 131, 270, 4, 0 };

#include "tie/soundext.h"
#include "tie/textext.h"

#include "tie/shipext.h"

#include <imuse/hilevel.h>
#include <string.h>

/* --- Functions --- */

// FUNCTION: TIE95 0x65C61
void shellext_Open_Landru(void* extern_mem, int16_t use_timer, int16_t use_script) {
	const TieFrontendProfile* profile = TieProfile_Frontend();
	Rect r;

	(void)extern_mem;
	asl_Open_ASL();
	if (xerror_Is_Landru_Error())
		return;
	/* Retail front-end: Alt+O (key 0x1800) in xio_Poll_Input dumps a PCX. */
	if (TIE_FRONTEND_TIE98)
		xio_Set_Screenshot_Hook((void (*)(void))FrontendDisplay_CaptureScreenshot);
	else
		xio_Set_Screenshot_Hook((void (*)(void))rtsvga2_takeScreenshot);

	if (sHead_gbl->cur_scene != SCENE_FILM_VIEWER) {
		xcanvas_Erase_Canvas();
		xpal_Set_Screen_RGB(0, 0, 0, 0, 0);
		xcanvas_Get_Drawing_Canvas_Bounds(&r);
		xcanvas_Copy_Screen_To_Video(&r);
	}

	if (use_timer == 0)
		soundext_Open_Post_iMuse(use_script);

	shipext_Open_Ships();
	textext_Open_Text_Ext();
	xtimer_Set_Frame_Rate(20);

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	sHead_gbl->def_file = shellext_Open_Empire_Resource("empire.lfd");
	sHead_gbl->def_palette = xpal_Res_Palette("standard");
	sHead_gbl->def_icons = xactanim_Res_Anim_Actor("icons", &r, 0, 0, 0);
	sHead_gbl->def_cursors = xactanim_Res_Anim_Actor("cursors", &r, 0, 0, 0);
	sHead_gbl->def_font8 = 0;
	xfont_Res_Font("font8", 0);
	sHead_gbl->def_font6 = 1;
	xfont_Res_Font("font6", 1);
	if (profile->font_count > 2) {
		xfont_Res_Font("font18", 2);
		xfont_Res_Font("font12", 3);
	}
	xfont_Set_Font(0);

	xpal_Set_Screen_Palette(sHead_gbl->def_palette);
	xremap_Remap_Interface();
	xio_Flush_Input();
	xcanvas_Get_Drawing_Canvas_Bounds(&r);
	xdirty_Dirty_Master_Rect(&r);
	xdirty_Set_Dirty_Merge();
	xdirty_Max_Dirty_List();

	xpal_Free_Palette_From_System(sHead_gbl->def_palette);
	xactor_Free_Actor_From_System(sHead_gbl->def_icons);
	xstyle_Style_Set_Icon_Actor(sHead_gbl->def_icons);
	xactor_Free_Actor_From_System(sHead_gbl->def_cursors);
	xcanvas_Enable_Screen_Diff();
	xcursor_Set_Cursor(0);
	shellext_Load_Preferences();
}

// FUNCTION: TIE95 0x65E67
void shellext_Close_Landru(int16_t use_timer) {
	Rect r;

	xcanvas_Disable_Screen_Diff();
	xpal_Free_Palette(sHead_gbl->def_palette);
	xactor_Free_Actor(sHead_gbl->def_icons);
	xactor_Free_Actor(sHead_gbl->def_cursors);
	xres_Close_Resource(sHead_gbl->def_file);
	textext_Close_Text_Ext();
	shipext_Close_Ships();

	if (use_timer == 0)
		soundext_Close_Post_iMuse();

	if (shellext_Get_Cur_Scene() != SCENE_FILM_REPLAY) {
		xcanvas_Erase_Canvas();
		xcanvas_Get_Drawing_Canvas_Bounds(&r);
		xcanvas_Copy_Screen_To_Video(&r);
	}

	asl_Close_ASL();
}

// FUNCTION: TIE95 0x65F0F
void shellext_Open_Landru_Scene(int16_t scene) {
	xerror_Clear_Landru_Escape();
	xerror_Clear_Landru_Exit();
	xerror_Set_Landru_Escape_Function(shellext_escape_TIE);
	sHead_gbl->cur_scene = scene;
	sHead_gbl->sudden_end = 0;
	/* The shell preserves scene tags until the outgoing fade completes. */
	soundext_Open_Sound_Scene(scene);
	textext_Open_Text_Ext_Scene(scene);
}

// FUNCTION: TIE95 0x65F71
// FUNCTION: TIE98 0x480550
void shellext_Close_Landru_Scene(int16_t scene) {
	int16_t next_scene;
	textext_Close_Text_Ext_Scene(scene);
	next_scene = xerror_Get_Landru_Exit();
	if (scene != 270 && ((uint16_t)next_scene >= 2u && ((uint16_t)next_scene <= 4u || next_scene == 290)))
		next_scene = 270;
	soundext_Close_Sound_Scene(scene, next_scene);
	sHead_gbl->last_scene = scene;

#ifdef TIE_MODERN
	if (sHead_gbl->sudden_end)
		TieShell_PushSuddenSceneFadeTask();
#else
	if (sHead_gbl->sudden_end)
		shellext_Sudden_Scene_Fade();
	xcanvas_Copy_Screen_To_Diff();
#endif
}

// FUNCTION: TIE95 0x66006
ResFile* shellext_Open_Empire_Resource(const char* filename) {
	char res_name[64];

	strcpy(res_name, "resource/");
	strcat(res_name, filename);
	return xres_Open_Resource(res_name);
}

// FUNCTION: TIE95 0x6604F
LandruFile* shellext_Open_Empire_File(const char* filename, const char* mode) {
	char file_name[64];

	strcpy(file_name, "resource/");
	strcat(file_name, filename);
	return xfile_Open_File(LANDRU_FILE_ROOT_ASSET, file_name, mode);
}

// FUNCTION: TIE95 0x661CF
// FUNCTION: TIE98 0x4808F0
int16_t shellext_Check_Cur_Scene(int16_t current_scene) { return sHead_gbl->cur_scene == current_scene; }

// FUNCTION: TIE95 0x6621B
int16_t shellext_Get_Cur_Scene(void) { return sHead_gbl->cur_scene; }

// FUNCTION: TIE95 0x6624B
// FUNCTION: TIE98 0x480920
int16_t shellext_Check_Last_Scene(int16_t last_scene) { return sHead_gbl->last_scene == last_scene; }

// FUNCTION: TIE95 0x66297
int16_t shellext_Get_Last_Scene(void) { return sHead_gbl->last_scene; }

// FUNCTION: TIE95 0x662C7
// FUNCTION: TIE98 0x480950
int16_t shellext_Is_Scene_Exit(int16_t scene_flag) {
	int16_t key;

	key = xio_Get_Free_Key();
	if (xio_Right_Button_Release() || key == 13)
		return 1;
	if (xio_Left_Button_Release() || key == 32)
		return 1;
	return scene_flag;
}

// FUNCTION: TIE95 0x66338
int16_t shellext_Check_Scene_Exit(int16_t* exit_id, int16_t next_scene, int16_t next_section,
								  int16_t scene_flag) {
	int16_t key;

	key = xio_Get_Key();
	if (!xerror_Get_Landru_Exit())
		return 0;
	if (xio_Right_Button_Release() || key == 13) {
		/* Right click / Enter: skip ahead to the optional next_section
		 * (long-form path). */
		shellext_Sudden_Scene_End();
		*exit_id = next_section;
	} else if (xio_Left_Button_Release() || key == 32) {
		/* Left click / Space: end the current scene early and continue
		 * to the regular next_scene. */
		shellext_Sudden_Scene_End();
		*exit_id = next_scene;
	} else if (scene_flag) {
		*exit_id = next_scene;
	} else {
		return 0;
	}
	return 1;
}

// FUNCTION: TIE95 0x663ED
int16_t shellext_Sudden_Scene_End(void) {
	sHead_gbl->sudden_end = 1;
	return 1;
}

// FUNCTION: TIE95 0x66423
int16_t shellext_Is_Sudden_Scene_End(void) { return sHead_gbl->sudden_end; }

// FUNCTION: TIE95 0x66526
int16_t shellext_escape_TIE(void) {
	/* Native dialog completion writes the exit latch after the wait. */
	if (!xdialog_Is_Active_Dialog() && !xfade_Fade_Active() && xview_Get_View_Time() > 0) {
#ifdef TIE_MODERN
		TieComputer_Begin();
		return -1;
#else
		return computer_Do_Computer_Dialog();
#endif
	}
	return xerror_Get_Landru_Exit();
}

// FUNCTION: TIE95 0x6659A
void shellext_Load_Preferences(void) {
	char name[16];
	LandruFile* the_file;

	options_gbl.music_active = 1;
	options_gbl.sound_active = 1;
	options_gbl.speech_active = 1;
	options_gbl.music_volume = 14;
	options_gbl.sound_volume = 15;
	options_gbl.speech_volume = 16;
	options_gbl.text_active = 0;
	options_gbl.transition_active = 1;
	options_gbl.game_level = 1;
	options_gbl.auto_backup = 1;
	options_gbl.auto_restore = 1;
#ifdef TIE_MODERN
	/* PORT: Default new users to the TIE95 640x480 flight mode on every
	 * load; the original keeps the previous value when foption.cfg is
	 * missing. */
	f_res = 1;
#endif

	the_file = xfile_Open_File(LANDRU_FILE_ROOT_USER, "foption.cfg", "rb");
	if (the_file) {
		xfile_Read_Data_From_File(the_file, &options_gbl, sizeof(FrontOptionsStruct));
		xfile_Read_Long_From_File(the_file, &f_res);
		xfile_Close_File(the_file);
	}

	/* Retail SHELLEXT_Load_Preferences translates f_res (0 or 1) into the
	 * VGA/VBE mode number that tie_initflightresolution later consumes.
	 * Without this, flightResolution stays zero and feinput_SetGraphicsPtrs
	 * silently falls back to mode 0 regardless of the user's preference. */
	if (f_res == 1)
		flightResolution = TIE_FLIGHT_RES_SVGA;
	else
		flightResolution = TIE_FLIGHT_RES_VGA;
#ifdef TIE_MODERN
	shipext_Get_Pilot_Name(name, sizeof(name));
#else
	shipext_Get_Pilot_Name(name);
#endif
	if (name[0])
		options_gbl.game_level = pilot_record.game_level;

	shellext_Set_Prefs_Sound();
}

// FUNCTION: TIE95 0x666A9
int16_t shellext_Set_Prefs_Sound(void) {
	if (options_gbl.music_active && options_gbl.music_volume)
		imuse_set_music_vol(im, options_gbl.music_volume * 8 - 1);
	else
		imuse_set_music_vol(im, 0);

	if (options_gbl.sound_active && options_gbl.sound_volume)
		imuse_set_sfx_vol(im, options_gbl.sound_volume * 8 - 1);
	else
		imuse_set_sfx_vol(im, 0);

	if (options_gbl.speech_active && options_gbl.speech_volume)
		imuse_set_voice_vol(im, options_gbl.speech_volume * 8 - 1);
	else
		imuse_set_voice_vol(im, 0);

	return 1;
}

// FUNCTION: TIE95 0x66779
int16_t shellext_Convert_Transition(int16_t scene, int16_t sudden) {
	int16_t i;

	if (options_gbl.transition_active)
		return scene;

	for (i = 0; transition_check[i] != scene && transition_check[i]; i += 2)
		;

	if (!transition_check[i])
		return scene;

	if (sudden)
		shellext_Sudden_Scene_End();

	soundext_Prep_Sound_Scene(transition_check[i]);
	return transition_check[i + 1];
}
