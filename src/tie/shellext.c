#include "tie/shellext.h"
#include "tie/asl.h"
#include "tie/shell.h"
#include "tie/tie.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/snapshot/snapshot.h"

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

#include <string.h>

/* --- Globals --- */

FrontOptionsStruct options_gbl;
int32_t f_res;

/* Scene→redirect pairs: when transitions are disabled, these scenes skip
   to their redirect target. Sentinel = 0. */
static int16_t transition_check[] = { 120, 121, 130, 131, 270, 4, 0 };

#include "tie/soundext.h"
#include "tie/textext.h"

#include "tie/shipext.h"

#include <imuse/hilevel.h>

/* --- Functions --- */

// FUNCTION: TIE 0x65C61
void shellext_Open_Landru(void* extern_mem, int16_t use_timer, int16_t use_script) {
	const TieFrontendProfile* profile = TieProfile_Frontend();
	Rect r;

	(void)extern_mem;
	asl_Open_ASL();
	if (lerror_Is_Landru_Error())
		return;
	/* Retail front-end: Alt+O (key 0x1800) in lio_Poll_Input dumps a PCX. */
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
		lio_Set_Screenshot_Hook((void (*)(void))FrontendDisplay_CaptureScreenshot);
	else
		lio_Set_Screenshot_Hook((void (*)(void))rtsvga2_takeScreenshot);

	if (sHead_gbl->cur_scene != SCENE_FILM_VIEWER) {
		lcanvas_Erase_Canvas();
		lpal_Set_Screen_RGB(0, 0, 0, 0, 0);
		lcanvas_Get_Drawing_Canvas_Bounds(&r);
		lcanvas_Copy_Screen_To_Video(&r);
	}

	if (use_timer == 0)
		soundext_Open_Post_iMuse(use_script);

	shipext_Open_Ships();
	textext_Open_Text_Ext();
	ltimer_Set_Frame_Rate(20);

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	sHead_gbl->def_file = shellext_Open_Empire_Resource("empire.lfd");
	sHead_gbl->def_palette = lpal_Res_Palette("standard");
	sHead_gbl->def_icons = lactanim_Res_Anim_Actor("icons", &r, 0, 0, 0);
	sHead_gbl->def_cursors = lactanim_Res_Anim_Actor("cursors", &r, 0, 0, 0);
	sHead_gbl->def_font8 = 0;
	lfont_Res_Font("font8", 0);
	sHead_gbl->def_font6 = 1;
	lfont_Res_Font("font6", 1);
	if (profile->font_count > 2) {
		lfont_Res_Font("font18", 2);
		lfont_Res_Font("font12", 3);
	}
	lfont_Set_Font(0);

	lpal_Set_Screen_Palette(sHead_gbl->def_palette);
	lremap_Remap_Interface();
	lio_Flush_Input();
	lcanvas_Get_Drawing_Canvas_Bounds(&r);
	ldirty_Dirty_Master_Rect(&r);
	ldirty_Set_Dirty_Merge();
	ldirty_Max_Dirty_List();

	lpal_Free_Palette_From_System(sHead_gbl->def_palette);
	lactor_Free_Actor_From_System(sHead_gbl->def_icons);
	lstyle_Style_Set_Icon_Actor(sHead_gbl->def_icons);
	lactor_Free_Actor_From_System(sHead_gbl->def_cursors);
	lcanvas_Enable_Screen_Diff();
	lcursor_Set_Cursor(0);
	shellext_Load_Preferences();
}

// FUNCTION: TIE 0x65E67
void shellext_Close_Landru(int16_t use_timer) {
	Rect r;

	lcanvas_Disable_Screen_Diff();
	lpal_Free_Palette(sHead_gbl->def_palette);
	lactor_Free_Actor(sHead_gbl->def_icons);
	lactor_Free_Actor(sHead_gbl->def_cursors);
	lres_Close_Resource(sHead_gbl->def_file);
	textext_Close_Text_Ext();
	shipext_Close_Ships();

	if (use_timer == 0)
		soundext_Close_Post_iMuse();

	if (shellext_Get_Cur_Scene() != SCENE_FILM_REPLAY) {
		lcanvas_Erase_Canvas();
		lcanvas_Get_Drawing_Canvas_Bounds(&r);
		lcanvas_Copy_Screen_To_Video(&r);
	}

	asl_Close_ASL();
}

// FUNCTION: TIE 0x65F0F
void shellext_Open_Landru_Scene(int16_t scene) {
	lerror_Clear_Landru_Escape();
	lerror_Clear_Landru_Exit();
	lerror_Set_Landru_Escape_Function(shellext_escape_TIE);
	sHead_gbl->cur_scene = scene;
	sHead_gbl->sudden_end = 0;
	/* Snapshot scene tagging is NOT reset here. shell_dispatch_converted
	 * pushes the scene task (which calls e.g. play1_Push_Play1_Task →
	 * set_scene_kind(CUTSCENE), or a non-cutscene scene's Push that
	 * tags itself) BEFORE shellext_Open_Landru_Scene runs — see
	 * shell.c:439-451. Resetting here would clobber the push-time
	 * tags. Cleanup (clearing stale tags from the prior scene)
	 * happens in shellext_Begin_Close_Landru_Scene + each scene's
	 * end path, which run BEFORE the next scene's push. */
	soundext_Open_Sound_Scene(scene);
	textext_Open_Text_Ext_Scene(scene);
}

/* Close-scene step 1: text + sound teardown, last_scene stamp. Caller
 * (shell task) inspects the returned `out_sudden_end` flag — when 1,
 * caller is responsible for pushing shellext_Push_Sudden_Scene_Fade_Task
 * + yielding before calling shellext_Finalize_Close_Landru_Scene to
 * land the screen-diff copy. The synchronous bridge function it
 * replaces had been called from inside ShellTask::step's AWAITING
 * phase, so the caller-task can push and yield naturally. */
void shellext_Begin_Close_Landru_Scene(int16_t scene, int16_t* out_sudden_end) {
	int16_t next_scene;

	/* Snapshot scene tags are NOT cleared here. The closing scene's
	 * bundle MUST stay active throughout XFADE's transition fade
	 * (which animates the classic FB) — without it, the HD overlay
	 * drops mid-fade and the user sees the classic fading-out of the
	 * old scene. Tags get reset by the shell's main dispatcher right
	 * before the next scene's Push runs, so the next scene either
	 * inherits a clean default or overrides as needed. See
	 * shell_task_step in shell.c for the clear point. */
	textext_Close_Text_Ext_Scene(scene);
	next_scene = lerror_Get_Landru_Exit();
	if (scene != 270 && ((uint16_t)next_scene >= 2u && ((uint16_t)next_scene <= 4u || next_scene == 290)))
		next_scene = 270;
	soundext_Close_Sound_Scene(scene, next_scene);
	sHead_gbl->last_scene = scene;

	if (out_sudden_end)
		*out_sudden_end = sHead_gbl->sudden_end;
}

/* Close-scene step 2: stage the screen-diff copy. Run after any
 * sudden-end fade pushed by the caller has popped. */
void shellext_Finalize_Close_Landru_Scene(void) { lcanvas_Copy_Screen_To_Diff(); }

// FUNCTION: TIE 0x66006
ResFile* shellext_Open_Empire_Resource(const char* filename) {
	char res_name[64];

	strcpy(res_name, "resource/");
	strcat(res_name, filename);
	return lres_Open_Resource(res_name);
}

// FUNCTION: TIE 0x6604F
LandruFile* shellext_Open_Empire_File(const char* filename, const char* mode) {
	char file_name[64];

	strcpy(file_name, "resource/");
	strcat(file_name, filename);
	return lfile_Open_File(LANDRU_FILE_ROOT_ASSET, file_name, mode);
}

int16_t shellext_Check_Cur_Scene(int16_t current_scene) { return sHead_gbl->cur_scene == current_scene; }

// FUNCTION: TIE 0x6621B
int16_t shellext_Get_Cur_Scene(void) { return sHead_gbl->cur_scene; }

int16_t shellext_Check_Last_Scene(int16_t last_scene) { return sHead_gbl->last_scene == last_scene; }

// FUNCTION: TIE 0x66297
int16_t shellext_Get_Last_Scene(void) { return sHead_gbl->last_scene; }

int16_t shellext_Is_Scene_Exit(int16_t scene_flag) {
	int16_t key;

	key = lio_Get_Free_Key();
	if (lio_Right_Button_Release() || key == 13)
		return 1;
	if (lio_Left_Button_Release() || key == 32)
		return 1;
	return scene_flag;
}

// FUNCTION: TIE 0x66338
int16_t shellext_Check_Scene_Exit(int16_t* exit_id, int16_t next_scene, int16_t next_section,
								  int16_t scene_flag) {
	int16_t key;

	key = lio_Get_Key();
	if (!lerror_Get_Landru_Exit())
		return 0;
	if (lio_Right_Button_Release() || key == 13) {
		/* Right click / Enter: skip ahead to the optional next_section
		 * (long-form path). */
		shellext_Sudden_Scene_End();
		*exit_id = next_section;
	} else if (lio_Left_Button_Release() || key == 32) {
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

// FUNCTION: TIE 0x663ED
int16_t shellext_Sudden_Scene_End(void) {
	sHead_gbl->sudden_end = 1;
	return 1;
}

// FUNCTION: TIE 0x66423
int16_t shellext_Is_Sudden_Scene_End(void) { return sHead_gbl->sudden_end; }

/* Push the "back stage to VGA" fade task: caller-task yields after
 * this call; the FadeTask's end callback restores the cursor as it
 * pops, matching the pre/post cursor state of the original
 * synchronous shellext_Back_Stage_To_VGA. */
void shellext_Push_Back_Stage_To_VGA_Task(int16_t dialog) {
	Rect r;
	lcanvas_Get_Drawing_Canvas_Bounds(&r);
	bool cursor_was_visible = lcursor_Is_Cursor_Visible();
	if (cursor_was_visible)
		lcursor_Cursor_To_Back();
	(void)lfade_Push_Fade_To_Video_Screen_Task(
		&r, dialog, cursor_was_visible ? FADE_END_CURSOR_TO_FRONT : FADE_END_CURSOR_FROM_FADE,
		/*force_refresh_view=*/false);
}

/* Push the sudden-scene-end fade. Used by shell_task_step when
 * shellext_Close_Landru_Scene reports sudden_end was set. The
 * lviewadd_Clear_View + lfade_Start_Full_Fade pair runs synchronously
 * before the fade push so the FadeTask sees the configured wipe. */
void shellext_Push_Sudden_Scene_Fade_Task(void) {
	lviewadd_Clear_View();
	lfade_Start_Full_Fade(2, 2, 0, 0, 1);
	shellext_Push_Back_Stage_To_VGA_Task(0);
}

// FUNCTION: TIE 0x66526
int16_t shellext_escape_TIE(void) {
	/* ESC key handler: outside an active dialog/fade and once a view
	 * has accumulated time, push the in-flight Computer dialog
	 * task. The escape callback runs synchronously inside an input
	 * poll and cannot wait on the dialog; computer_Push_Computer_
	 * Dialog_Task pushes the task on the tie_core stack and the
	 * dialog itself calls lerror_Set_Landru_Exit on its way out
	 * (whose value lerror_Do_Landru_Escape would otherwise have
	 * stored from a synchronous return). We return -1 so the
	 * escape mechanism leaves landru_exit_gbl alone — the dialog's
	 * exit value already populated it. */
	if (!ldialog_Is_Active_Dialog() && !lfade_Fade_Active() && lview_Get_View_Time() > 0) {
		computer_Push_Computer_Dialog_Task();
		return -1;
	}
	return lerror_Get_Landru_Exit();
}

// FUNCTION: TIE 0x6659A
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
	/* PORT: Default new users to the TIE95 640x480 flight mode. */
	f_res = 1;

	the_file = lfile_Open_File(LANDRU_FILE_ROOT_USER, "foption.cfg", "rb");
	if (the_file) {
		lfile_Read_Data_From_File(the_file, &options_gbl, sizeof(FrontOptionsStruct));
		lfile_Read_Long_From_File(the_file, &f_res);
		lfile_Close_File(the_file);
	}

	/* Retail SHELLEXT_Load_Preferences translates f_res (0 or 1) into the
	 * VGA/VBE mode number that tie_initflightresolution later consumes.
	 * Without this, flightResolution stays zero and feinput_SetGraphicsPtrs
	 * silently falls back to mode 0 regardless of the user's preference. */
	if (f_res == 1)
		flightResolution = TIE_FLIGHT_RES_SVGA;
	else
		flightResolution = TIE_FLIGHT_RES_VGA;

	shipext_Get_Pilot_Name(name, sizeof(name));
	if (name[0])
		options_gbl.game_level = pilot_record.game_level;

	shellext_Set_Prefs_Sound();
}

// FUNCTION: TIE 0x666A9
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

// FUNCTION: TIE 0x66779
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
