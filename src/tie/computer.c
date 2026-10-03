#include "tie/computer.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/computer_task.h"
#endif
#include "tie/edition.h"
#if defined(TIE98) && !defined(TIE_MODERN)
#include "tie/frontend_display_tie98.h"
#endif
#include "tie/register.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/diagnostics/diagnostics.h"

#include "landru/actanim.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/btnpush.h"
#include "landru/canvas.h"
#include "landru/dialog.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/fade.h"
#include "landru/file.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/sound.h"
#include "landru/style.h"
#include "landru/surface.h"
#include "landru/view.h"
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Mouse button states (from watdbg MouseStateType) */
enum {
	MOUSE_NO_PRESS = 0,
	MOUSE_DOWN = 1,
	MOUSE_MOVE = 2,
	MOUSE_UP = 3,
};

/* ======================================================================
 * Static data — resource name and layout tables
 * ====================================================================== */

/* [0] = LFD archive, [1..5] = DELT actors, [6..8] = ANIM actors, then the
 * palettes. TIE95 loads its palettes from [1], [3], [4] and [9]; TIE98 adds
 * a dedicated palette list at [9..12]. */
// GLOBAL: TIE95 0xD1518
// GLOBAL: TIE98 0x4DF590
static const char computer_str[][14] = {
#ifdef TIE98
	"computer.lfd", "arm",    "sleeve", "newtscrn", "compface", "medlbak", "buttons",
	"compicon",     "tattoo", "arm",    "newtscrn", "computr",  "medlbak",
#else
	"computer.lfd", "newtarm",  "newtslev", "newtscrn", "computr",
	"medlbak",      "button01", "compicns", "tattoo",   "medlbak",
#endif
};

/* Tab click regions: Medals, Record, Backup, Options (Rect = {top,left,bottom,right}). */
// GLOBAL: TIE95 0xD15A4
// GLOBAL: TIE98 0x4DF648
static const Rect computer_mode_rect[4] = {
#ifdef TIE98
	{ 350, 318, 453, 380 },
	{ 350, 388, 453, 445 },
	{ 350, 451, 453, 511 },
	{ 350, 517, 453, 577 },
#else
	{ 146, 160, 188, 190 },
	{ 146, 194, 188, 224 },
	{ 146, 227, 188, 257 },
	{ 146, 260, 188, 290 },
#endif
};

/* Background DELT actor offsets. */
// GLOBAL: TIE95 0xD15C4
// GLOBAL: TIE98 0x4DF3A0
static const int16_t computer_x[5] = { 0, 0, 41, 0, -98 };
// GLOBAL: TIE95 0xD15CE
// GLOBAL: TIE98 0x50F780
static const int16_t computer_y[5];

/* [0] = LFD archive, [1..14] = medal actors, [15..18] = medal palettes. */
// GLOBAL: TIE95 0xD15D8
// GLOBAL: TIE98 0x4DF3B0
static const char computer_medal_str[19][14] = {
#ifdef TIE98
	"awardshr.lfd",
#else
	"awards.lfd",
#endif
	"trnships",     "medls-a",  "strsnbrs", "sunrisea", "aniplnts", "star-1",
	"star-2",       "coins",    "starsbak", "mdl-bak1", "mdl-bak2", "ani1-sta",
	"trnships",     "raptrhed", "trnshps",  "medlbak",  "brnzpal",  "slvrpal",
};

/* Mission disk 1 awards: [0] = LFD archive, then the ANIM actors. */
// GLOBAL: TIE95 0xD16E2
// GLOBAL: TIE98 0x4DF4C0
static const char computer_medal_str2[7][14] = {
#ifdef TIE98
	"awards1h.lfd", "mislboat", "a-medals", "amed-obj", "medpal", "coins", "tattoo",
#else
	"awards1.lfd", "mislboat", "a-medals", "amed-obj", "medpal", "coins", "comptat2",
#endif
};

/* Mission disk 2 awards: [0] = LFD archive, then the ANIM actors. */
// GLOBAL: TIE95 0xD1744
// GLOBAL: TIE98 0x4DF528
static const char computer_medal_str3[7][14] = {
#ifdef TIE98
	"awards2h.lfd", "mislboat", "b-medals", "bmed-obj", "medpal", "coins", "tattoo",
#else
	"awards2.lfd", "mislboat", "b-medals", "bmed-obj", "medpal", "coins", "comptat2",
#endif
};

/* Preferences panel rects:
 * [0]=title, [1]=music label, [2]=music on/off, [3]=music gauge,
 * [4]=sound label, [5]=sound on/off, [6]=sound gauge,
 * [7]=speech label, [8]=speech on/off, [9]=speech gauge,
 * [10]=transitions label, [11]=transitions on/off,
 * [12]=subtitles label, [13]=subtitles on/off,
 * [14]=difficulty label, [15]=difficulty selector,
 * [16]=flight res label, [17]=flight res selector.
 * TIE98 appends five rects for its joystick, brightness and texture
 * resolution controls. */
// GLOBAL: TIE95 0xD17AE
// GLOBAL: TIE98 0x4DF668
static const Rect pref_rect[] = {
#ifdef TIE98
	{ 26, 242, 60, 478 },   { 67, 190, 93, 280 },   { 67, 286, 93, 386 },   { 67, 392, 93, 530 },
	{ 98, 190, 124, 280 },  { 98, 286, 124, 386 },  { 98, 392, 124, 530 },  { 129, 190, 155, 280 },
	{ 129, 286, 155, 386 }, { 129, 392, 155, 530 }, { 160, 190, 186, 264 }, { 160, 270, 186, 354 },
	{ 160, 360, 186, 434 }, { 160, 440, 186, 530 }, { 191, 190, 217, 344 }, { 191, 350, 217, 530 },
	{ 222, 190, 248, 434 }, { 253, 190, 279, 530 }, { 222, 440, 248, 530 }, { 284, 190, 310, 260 },
	{ 284, 266, 310, 340 }, { 284, 345, 310, 421 }, { 284, 427, 310, 530 },
#else
	{ 9, 121, 23, 239 },    /* title */
	{ 26, 95, 38, 140 },    /* music label */
	{ 26, 143, 38, 193 },   /* music on/off */
	{ 26, 196, 38, 265 },   /* music gauge */
	{ 41, 95, 53, 140 },    /* sound label */
	{ 41, 143, 53, 193 },   /* sound on/off */
	{ 41, 196, 53, 265 },   /* sound gauge */
	{ 56, 95, 68, 140 },    /* speech label */
	{ 56, 143, 68, 193 },   /* speech on/off */
	{ 56, 196, 68, 265 },   /* speech gauge */
	{ 71, 95, 83, 212 },    /* transitions label */
	{ 71, 215, 83, 265 },   /* transitions on/off */
	{ 86, 95, 98, 172 },    /* subtitles label */
	{ 86, 175, 98, 265 },   /* subtitles on/off */
	{ 101, 95, 113, 172 },  /* difficulty label */
	{ 101, 175, 113, 265 }, /* difficulty selector */
	{ 116, 95, 128, 162 },  /* flight res label */
	{ 116, 165, 128, 265 }, /* flight res selector */
#endif
};

/* Backup panel rects:
 * [0]=title, [1]=auto-backup label, [2]=auto-backup on/off,
 * [3]=auto-restore label, [4]=auto-restore on/off,
 * [5]=backup button, [6]=restore button, [7]=info panel */
// GLOBAL: TIE95 0xD183E
// GLOBAL: TIE98 0x4DF748
static const Rect backup_rect[8] = {
#ifdef TIE98
	{ 26, 242, 64, 478 },   { 67, 190, 96, 424 },   { 67, 430, 96, 530 },   { 105, 190, 134, 424 },
	{ 105, 430, 134, 530 }, { 144, 190, 177, 350 }, { 144, 370, 177, 530 }, { 187, 190, 285, 530 },
#else
	{ 9, 121, 23, 239 },  /* title */
	{ 26, 95, 38, 212 },  /* auto-backup label */
	{ 26, 215, 38, 265 }, /* auto-backup on/off */
	{ 42, 95, 54, 212 },  /* auto-restore label */
	{ 42, 215, 54, 265 }, /* auto-restore on/off */
	{ 58, 95, 72, 175 },  /* backup button */
	{ 58, 185, 72, 265 }, /* restore button */
	{ 76, 95, 117, 265 }, /* info panel */
#endif
};

#ifdef TIE_MODERN
/* PORT: runtime frontend selection. Modern builds hold the TIE95 tables
 * above; the TIE98 tables come from the runtime. The recovered bodies
 * shadow the table names with these selections. */
static const Rect* active_mode_rect;
static const Rect* active_pref_rect;
static const Rect* active_backup_rect;
static const char (*active_computer_str)[14];
static const char (*active_medal_str)[14];
static const char (*active_medal_str2)[14];
static const char (*active_medal_str3)[14];
#endif

/* ======================================================================
 * Static BSS globals
 * ====================================================================== */

// GLOBAL: TIE95 0xFB404
// GLOBAL: TIE98 0x50F640
static Palette* medal_palette[4];
// GLOBAL: TIE95 0xFB414
// GLOBAL: TIE98 0x50F6F0
static Actor* medal_actor[14];
// GLOBAL: TIE95 0xFB44C
// GLOBAL: TIE98 0x50F610
static Actor* medal_actor2[10];
// GLOBAL: TIE95 0xFB474
// GLOBAL: TIE98 0x50AAD8
static Actor* computer_actors[8];
// GLOBAL: TIE95 0xFB494
// GLOBAL: TIE98 0x50F580
static uint32_t backup_pilot_points;
// GLOBAL: TIE95 0xFB498
// GLOBAL: TIE98 0x50F650
static Input* restore_input;
// GLOBAL: TIE95 0xFB4AC
// GLOBAL: TIE98 0x50F5E8
static Palette* computer_palettes[5];
// GLOBAL: TIE95 0xFB4C0
// GLOBAL: TIE98 0x50F76C
static Input* last_info_input;
// GLOBAL: TIE95 0xFB49C
// GLOBAL: TIE98 0x50F770
static Input* cancel_input;
/* TIE98: the exit confirmation's Yes button, selected by its keyboard
 * shortcuts. */
// GLOBAL: TIE98 0x50F588
static Input* exit_yes_input;
#if defined(TIE98) && !defined(TIE_MODERN)
/* TIE98: set by the preferences page to reset the joystick button keys. */
// GLOBAL: TIE98 0x50F59C
static int32_t joystick_reset_requested;
#endif
// GLOBAL: TIE95 0xFB4A0
// GLOBAL: TIE98 0x50F590
static char comp_exit_str[2][6];
// GLOBAL: TIE95 0xFB4C4
// GLOBAL: TIE98 0x50F600
static Input* backup_input;
// GLOBAL: TIE95 0xFB4C8
// GLOBAL: TIE98 0x50F5FC
static Input* next_info_input;
// GLOBAL: TIE95 0xFB4CC
// GLOBAL: TIE98 0x50F774
static Palette* computer_palette;
// GLOBAL: TIE95 0xFB4D0
// GLOBAL: TIE98 0x50F6A0
static int16_t pilot_medal_bonus_status[33];
// GLOBAL: TIE95 0xFB512
// GLOBAL: TIE98 0x50F5A0
static int16_t pilot_medal_status[33];
// GLOBAL: TIE95 0xFB554
// GLOBAL: TIE98 0x50F728
static int16_t pilot_medal_type[33];
// GLOBAL: TIE95 0xFB596
// GLOBAL: TIE98 0x50F658
static int16_t pilot_medal_id[33];
// GLOBAL: TIE95 0xFB5DA
// GLOBAL: TIE98 0x50AAF8
static int16_t restore_pilot;
// GLOBAL: TIE95 0xFB5DC
// GLOBAL: TIE98 0x50F63C
static int16_t backup_pilot_rank;
// GLOBAL: TIE95 0xFB5D8
// GLOBAL: TIE98 0x50F6EC
static int16_t pilot_medal_text;
// GLOBAL: TIE95 0xFB5DE
// GLOBAL: TIE98 0x50F6E4
static int16_t computer_mode;
// GLOBAL: TIE95 0xFB5E0
// GLOBAL: TIE98 0x50F778
static int16_t computer_display;
// GLOBAL: TIE95 0xD17A6
// GLOBAL: TIE98 0x50F78C
static int16_t pilot_medal_page = 0;
// GLOBAL: TIE95 0xD17A8
// GLOBAL: TIE98 0x50F790
static int16_t pilot_medal_num_pages = 0;
// GLOBAL: TIE95 0xD17AA
// GLOBAL: TIE98 0x50F794
static int16_t pilot_info_page = 0;
// GLOBAL: TIE95 0xD17AC
// GLOBAL: TIE98 0x50F798
static int16_t pilot_info_num_pages = 0;

static Input* computer_Build_Computer_Dialog(void);
static int16_t computer_iupdate_Computer(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
										 uint8_t right, int16_t x, int16_t y);
static void computer_iuser_Computer(Input* input, int32_t time);
static void computer_idraw_Computer(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void computer_xdraw_Computer_Medal(Rect* r, Rect* clip_r);
static void computer_iuser_Computer_Info(Input* input, int32_t time);
static void computer_idraw_Computer_Info(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void computer_xdraw_Computer_Info(Rect* r, Rect* clip_r);
static int16_t computer_Set_Computer_Medal_Palette(void);
static void computer_Draw_Computer_Header_Info(Rect* r, int16_t color, int16_t back_color);
static void computer_Draw_Computer_Combat_Info(Rect* r, int16_t color, int16_t back_color);
static void computer_Draw_Computer_Battle_Info(Rect* r, int16_t color, int16_t back_color);
static void computer_Draw_Computer_Kills_Info(Rect* r, int16_t color, int16_t back_color);
static void computer_update_Computer_Prefs(int16_t x, int16_t y);
static void computer_draw_Computer_Prefs(Rect* r, Rect* clip_r);
static void computer_xupdate_Computer_Backup(int16_t x, int16_t y);
static void computer_iuser_Computer_Backup(Input* input, int32_t time);
static void computer_xdraw_Computer_Backup(Rect* r, Rect* clip_r);
static void computer_draw_Computer_On_Off(Rect* r, int16_t on);
static void computer_draw_Computer_Level(Rect* r, int16_t state);
static void computer_draw_Computer_Gauge(Rect* r, int16_t amount);
static int16_t computer_Check_Backup_Pilot(void);
static int16_t computer_Check_Restore_Pilot(void);
#if defined(TIE98) && !defined(TIE_MODERN)
static int16_t computer_Check_Reset_Joystick(void);
static int16_t computer_Check_Exit_To_DOS(void);
#else
static int16_t computer_Exit_To_DOS(void);
#endif
static Input* computer_Build_Exit(int16_t id);
static void computer_idraw_Exit(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void computer_iuser_Exit(Input* input, int32_t time);
static int16_t computer_Init_Computer_Medal(void);
static int16_t computer_Find_Backup_Pilot_Info(void);

// FUNCTION: TIE95 0x82AD0
// FUNCTION: TIE98 0x40BA40
int16_t computer_Do_Computer_Dialog(void) {
	Input* the_dialog;
	int16_t retval = 0;
#ifdef TIE_MODERN
	ComputerDialogState* continuation = landru_task_top();
	the_dialog = continuation->the_dialog;
	active_mode_rect = TieProfile_UsesTie98Frontend() ? TieComputer_SvgaModeRect : computer_mode_rect;
	active_pref_rect = TieProfile_UsesTie98Frontend() ? TieComputer_SvgaPrefRect : pref_rect;
	active_backup_rect = TieProfile_UsesTie98Frontend() ? TieComputer_SvgaBackupRect : backup_rect;
	active_computer_str = TieProfile_UsesTie98Frontend() ? TieComputer_SvgaStr : computer_str;
	active_medal_str = TieProfile_UsesTie98Frontend() ? TieComputer_SvgaMedalStr : computer_medal_str;
	active_medal_str2 = TieProfile_UsesTie98Frontend() ? TieComputer_SvgaMedalStr2 : computer_medal_str2;
	active_medal_str3 = TieProfile_UsesTie98Frontend() ? TieComputer_SvgaMedalStr3 : computer_medal_str3;
	if (!continuation->started && !continuation->failed)
#endif
	{
#ifdef TIE_MODERN
		const char (*computer_str)[14] = active_computer_str;
		const char (*computer_medal_str)[14] = active_medal_str;
		const char (*computer_medal_str2)[14] = active_medal_str2;
		const char (*computer_medal_str3)[14] = active_medal_str3;
#endif
		ResFile* res_file;
		Palette* src_palette;
		Rect r;
		int16_t i;

		xsound_Pause_Sounds();
		xio_Clear_Key();
#ifdef TIE_MODERN
		if (TieProfile_UsesTie98Frontend()) {
			continuation->saved_surface_set = xsurface_Get_Surface_Set();
			xview_Get_View_Frame(0, &continuation->saved_view_frame);
			xview_Get_Full_View_Clip_Frame(&continuation->saved_view_clip);
			(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
			xrect_Set_Rect(&r, 0, 0, 640, 480);
			xview_Set_View_Frame(0, &r);
			xview_Set_Full_View_Clip_Frame(&r);
		}

#endif

		restore_pilot = 0;
		computer_display = 1;
		computer_mode = COMP_MODE_OPTIONS;
		pilot_info_page = 0;
		pilot_info_num_pages = 1;
		computer_Init_Computer_Medal();
		computer_Find_Backup_Pilot_Info();
		memset(computer_actors, 0, sizeof computer_actors);
		memset(medal_actor, 0, sizeof medal_actor);
		memset(medal_actor2, 0, sizeof medal_actor2);
		memset(computer_palettes, 0, sizeof computer_palettes);
		memset(medal_palette, 0, sizeof medal_palette);
		computer_palette = NULL;

		computer_palette = xpal_Alloc_Palette(0, 256);
		src_palette = xpal_Get_Screen_Palette();
#ifdef TIE_MODERN
		if (!computer_palette || !src_palette) {
			TieComputer_Fail(NULL, "computer palette");
			return 0;
		}
#endif
		xpal_Copy_Palette(computer_palette, src_palette, 0, 256, 0);

		res_file = shellext_Open_Empire_Resource(computer_str[0]);
#ifdef TIE_MODERN
		if (!res_file) {
			TieComputer_Fail(NULL, computer_str[0]);
			return 0;
		}
#endif
		xrect_Set_Rect(&r, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));

		for (i = 0; i < 5; i++) {
			computer_actors[i] = xactdelt_Res_Delta_Actor(computer_str[i + 1], &r, 0, 0, 0);
#ifdef TIE_MODERN
			if (!computer_actors[i]) {
				TieComputer_Fail(res_file, computer_str[i + 1]);
				return 0;
			}
#endif
			xactor_Set_Actor_Time(computer_actors[i], 0, 0);
		}
		for (i = 0; i < 3; i++) {
			computer_actors[i + 5] = xactanim_Res_Anim_Actor(computer_str[i + 6], &r, 0, 0, 0);
#ifdef TIE_MODERN
			if (!computer_actors[i + 5]) {
				TieComputer_Fail(res_file, computer_str[i + 6]);
				return 0;
			}
#endif
			xactor_Set_Actor_Time(computer_actors[i + 5], 0, 0);
		}
#ifdef TIE_MODERN
		if (TieProfile_UsesTie98Frontend()) {
			for (i = 0; i < 4; i++) {
				computer_palettes[i] = xpal_Res_Palette(computer_str[i + 9]);
				if (!computer_palettes[i]) {
					TieComputer_Fail(res_file, computer_str[i + 9]);
					return 0;
				}
			}
		} else {
			for (i = 0; i < 4; i++) {
				if (i != 1) {
					computer_palettes[i] = xpal_Res_Palette(computer_str[i + 1]);
					if (!computer_palettes[i]) {
						TieComputer_Fail(res_file, computer_str[i + 1]);
						return 0;
					}
				}
			}
			computer_palettes[4] = xpal_Res_Palette(computer_str[9]);
			if (!computer_palettes[4]) {
				TieComputer_Fail(res_file, computer_str[9]);
				return 0;
			}
		}
#elif defined(TIE98)
		for (i = 0; i < 4; i++)
			computer_palettes[i] = xpal_Res_Palette(computer_str[i + 9]);
#else
		for (i = 0; i < 4; i++) {
			if (i != 1)
				computer_palettes[i] = xpal_Res_Palette(computer_str[i + 1]);
		}
		computer_palettes[4] = xpal_Res_Palette(computer_str[9]);
#endif
		xpal_Set_Screen_RGB(0, 255, 0, 0, 0);
		xres_Close_Resource(res_file);

		res_file = shellext_Open_Empire_Resource(computer_medal_str[0]);
#ifdef TIE_MODERN
		if (!res_file) {
			TieComputer_Fail(NULL, computer_medal_str[0]);
			return 0;
		}
#endif
		for (i = 0; i < 14; i++) {
			if (i >= 8)
				medal_actor[i] = xactdelt_Res_Delta_Actor(computer_medal_str[i + 1], &r, 0, 0, 0);
			else
				medal_actor[i] = xactanim_Res_Anim_Actor(computer_medal_str[i + 1], &r, 0, 0, 0);
#ifdef TIE_MODERN
			if (!medal_actor[i]) {
				TieComputer_Fail(res_file, computer_medal_str[i + 1]);
				return 0;
			}
#endif
		}
		for (i = 0; i < 4; i++) {
			medal_palette[i] = xpal_Res_Palette(computer_medal_str[i + 15]);
#ifdef TIE_MODERN
			if (!medal_palette[i]) {
				TieComputer_Fail(res_file, computer_medal_str[i + 15]);
				return 0;
			}
#endif
		}
		xres_Close_Resource(res_file);

		if (shipext_Is_Mission_Disk1()) {
			res_file = shellext_Open_Empire_Resource(computer_medal_str2[0]);
#ifdef TIE_MODERN
			if (!res_file) {
				TieComputer_Fail(NULL, computer_medal_str2[0]);
				return 0;
			}
#endif
			medal_actor2[0] = xactanim_Res_Anim_Actor(computer_medal_str2[1], &r, 0, 0, 0);
			medal_actor2[1] = xactanim_Res_Anim_Actor(computer_medal_str2[2], &r, 0, 0, 0);
			medal_actor2[2] = xactanim_Res_Anim_Actor(computer_medal_str2[3], &r, 0, 0, 0);
			medal_actor2[3] = xactanim_Res_Anim_Actor(computer_medal_str2[5], &r, 0, 0, 0);
			medal_actor2[4] = xactanim_Res_Anim_Actor(computer_medal_str2[6], &r, 0, 0, 0);
#ifdef TIE_MODERN
			for (i = 0; i < 5; ++i) {
				if (!medal_actor2[i]) {
					TieComputer_Fail(res_file, computer_medal_str2[i < 3 ? i + 1 : i + 2]);
					return 0;
				}
			}
#endif
			xres_Close_Resource(res_file);
		}

		if (shipext_Is_Mission_Disk2()) {
			res_file = shellext_Open_Empire_Resource(computer_medal_str3[0]);
#ifdef TIE_MODERN
			if (!res_file) {
				TieComputer_Fail(NULL, computer_medal_str3[0]);
				return 0;
			}
#endif
			medal_actor2[5] = xactanim_Res_Anim_Actor(computer_medal_str3[1], &r, 0, 0, 0);
			medal_actor2[6] = xactanim_Res_Anim_Actor(computer_medal_str3[2], &r, 0, 0, 0);
			medal_actor2[7] = xactanim_Res_Anim_Actor(computer_medal_str3[3], &r, 0, 0, 0);
			medal_actor2[8] = xactanim_Res_Anim_Actor(computer_medal_str3[5], &r, 0, 0, 0);
			medal_actor2[9] = xactanim_Res_Anim_Actor(computer_medal_str3[6], &r, 0, 0, 0);
#ifdef TIE_MODERN
			for (i = 5; i < 10; ++i) {
				if (!medal_actor2[i]) {
					TieComputer_Fail(res_file, computer_medal_str3[i < 8 ? i - 4 : i - 3]);
					return 0;
				}
			}
#endif
			xres_Close_Resource(res_file);
		}

		the_dialog = computer_Build_Computer_Dialog();
#ifdef TIE_MODERN
		if (!the_dialog) {
			TieComputer_Fail(NULL, "computer dialog");
			return 0;
		}
#endif
#ifdef TIE_MODERN
		TieComputer_RunView(the_dialog);
		return 0;
#else
		retval = xdialog_Handle_Dialog_View(the_dialog);
#endif
	}
#ifdef TIE_MODERN
	if (!continuation->failed)
#endif
	{
#ifdef TIE_MODERN
		retval = xdialog_Get_Dialog_Exit();
#endif
		xdialog_Clear_Dialog_Exit();

		if (retval == 2) {
			/* Accept: save options */
			LandruFile* the_file = xfile_Open_File(LANDRU_FILE_ROOT_USER, "foption.cfg", "wb");
			if (the_file) {
				xfile_Write_Data_To_File(the_file, &options_gbl, 22);
				xfile_Write_Long_To_File(the_file, f_res);
				xfile_Close_File(the_file);
			}

			if (f_res == 0)
				flightResolution = TIE_FLIGHT_RES_VGA;
			else if (f_res == 1)
				flightResolution = TIE_FLIGHT_RES_SVGA;

			if (pilot_record.game_level != options_gbl.game_level) {
				pilot_record.game_level = options_gbl.game_level;
				shipext_Update_Pilot();
			}

			if (restore_pilot) {
				int16_t scene = shellext_Get_Cur_Scene();
				if (scene == SCENE_REGISTER || scene == SCENE_EXIT) {
					register_Revive_Pilot_Info();
					retval = xerror_Get_Landru_Exit();
				} else {
					retval = 110;
				}
			} else {
				retval = xerror_Get_Landru_Exit();
			}
		} else {
			retval = xerror_Get_Landru_Escape();
		}
	}
	{
		int16_t i;

		if (the_dialog) {
			xinput_Free_Inputs(the_dialog);
			the_dialog = NULL;
		}

		for (i = 0; i < 14; ++i) {
			if (medal_actor[i]) {
				xactor_Free_Actor_From_System(medal_actor[i]);
				xactor_Free_Actor(medal_actor[i]);
				medal_actor[i] = NULL;
			}
		}
		for (i = 0; i < 10; ++i) {
			if (medal_actor2[i]) {
				xactor_Free_Actor_From_System(medal_actor2[i]);
				xactor_Free_Actor(medal_actor2[i]);
				medal_actor2[i] = NULL;
			}
		}
		for (i = 0; i < 8; ++i) {
			if (computer_actors[i]) {
				xactor_Free_Actor_From_System(computer_actors[i]);
				xactor_Free_Actor(computer_actors[i]);
				computer_actors[i] = NULL;
			}
		}
		for (i = 0; i < 4; ++i) {
			if (medal_palette[i]) {
				xpal_Free_Palette_From_System(medal_palette[i]);
				xpal_Free_Palette(medal_palette[i]);
				medal_palette[i] = NULL;
			}
		}
		for (i = 0; i < 5; ++i) {
			if (computer_palettes[i]) {
				xpal_Free_Palette_From_System(computer_palettes[i]);
				xpal_Free_Palette(computer_palettes[i]);
				computer_palettes[i] = NULL;
			}
		}
		if (computer_palette) {
			xpal_Free_Palette(computer_palette);
			computer_palette = NULL;
		}
#ifdef TIE_MODERN
		if (continuation->tie98) {
			(void)xsurface_Select_Surface_Set(continuation->saved_surface_set);
			xview_Set_View_Frame(0, &continuation->saved_view_frame);
			xview_Set_Full_View_Clip_Frame(&continuation->saved_view_clip);
		}
#endif
	}

	shellext_Set_Prefs_Sound();
	xsound_Resume_Sounds();

#ifdef TIE_MODERN
	continuation->finished = true;
	if (continuation->failed) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "[COMPUTER] missing frontend resource: %s\n",
						   continuation->missing_resource);
		xerror_Set_Landru_Error(6);
	} else {
		xerror_Set_Landru_Exit(retval);
	}
#endif
	return retval;
}

/* ======================================================================
 * computer_Build_Computer_Dialog — construct the widget tree
 * ====================================================================== */

// FUNCTION: TIE95 0x83000
// FUNCTION: TIE98 0x40C220
static Input* computer_Build_Computer_Dialog(void) {
#ifdef TIE_MODERN
	const Rect* backup_rect = active_backup_rect;
#endif
	Rect r;
	Input *parent, *inp;

	xrect_Set_Rect(&r, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
	parent = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(parent, computer_idraw_Computer);
	xinpattr_Set_Input_User_Function(parent, computer_iuser_Computer);
	xinpattr_Set_Input_Update_Function(parent, computer_iupdate_Computer);
	xinpattr_Show_Input(parent);
	parent->mouseUsage = allInput;
	parent->id = 0;

	/* Next Page button (for medals/record) */
	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(190, 380), TIE_FRONTEND_EDITION(124, 298),
				   TIE_FRONTEND_EDITION(266, 532), TIE_FRONTEND_EDITION(136, 326));
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, computer_iuser_Computer_Info, NULL, 0);
	xinpattr_Set_Input_Draw_Function(inp, computer_idraw_Computer_Info);
	xinpattr_Hide_Input(inp);
	next_info_input = inp;

	/* Last Page button */
	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(104, 208), TIE_FRONTEND_EDITION(124, 298),
				   TIE_FRONTEND_EDITION(180, 360), TIE_FRONTEND_EDITION(136, 326));
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, computer_iuser_Computer_Info, NULL, 1);
	xinpattr_Set_Input_Draw_Function(inp, computer_idraw_Computer_Info);
	xinpattr_Hide_Input(inp);
	last_info_input = inp;

	/* Backup button */
	xrect_Copy_Rect(&r, (Rect*)&backup_rect[5]);
	xrect_Inset_Rect(&r, 1, 1);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, computer_iuser_Computer_Backup, NULL, 3);
	xinpattr_Set_Input_Draw_Function(inp, computer_idraw_Computer);
	xinpattr_Hide_Input(inp);
	backup_input = inp;

	/* Restore button */
	xrect_Copy_Rect(&r, (Rect*)&backup_rect[6]);
	xrect_Inset_Rect(&r, 1, 1);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, computer_iuser_Computer_Backup, NULL, 4);
	xinpattr_Set_Input_Draw_Function(inp, computer_idraw_Computer);
	xinpattr_Hide_Input(inp);
	restore_input = inp;

#ifdef TIE_MODERN
	/* Modern options button; COMPUTER starts on the Options tab. */
	if (TieProfile_UsesTie98Frontend())
		xrect_Set_Rect(&r, 190, 253, 530, 279);
	else
		xrect_Set_Rect(&r, 95, 116, 265, 128);
	TieComputer_AllocOptionsButton(parent, &r, computer_idraw_Computer);
#endif

	/* OK button (id=1, Exit to DOS) */
	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(96, 225), TIE_FRONTEND_EDITION(169, 400),
				   TIE_FRONTEND_EDITION(152, 302), TIE_FRONTEND_EDITION(194, 466));
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, computer_iuser_Computer, NULL, 1);
	xinpattr_Set_Input_Draw_Function(inp, computer_idraw_Computer);

	/* Cancel button (id=2, Accept/Save) */
	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(96, 196), TIE_FRONTEND_EDITION(142, 350),
				   TIE_FRONTEND_EDITION(152, 302), TIE_FRONTEND_EDITION(168, 402));
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, computer_iuser_Computer, NULL, 2);
	xinpattr_Set_Input_Draw_Function(inp, computer_idraw_Computer);
	cancel_input = inp;

	return parent;
}

/* ======================================================================
 * computer_iupdate_Computer — main dialog update callback
 * ====================================================================== */

#if defined(TIE95) && !defined(TIE_MODERN)
int16_t xio_Is_Joystick_Callibrate(void);
void xdlgjoy_Do_Joystick_Callibrate(void);
#endif

// FUNCTION: TIE95 0x83248
// FUNCTION: TIE98 0x40C4A0
static int16_t computer_iupdate_Computer(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
										 uint8_t right, int16_t x, int16_t y) {
#ifdef TIE_MODERN
	const Rect* computer_mode_rect = active_mode_rect;
#endif
	char name[16];
	Rect tr;
	int16_t new_mode;
	int16_t i;
	uint8_t button;

	(void)clip_r;

	if (key) {
#if defined(TIE95) && !defined(TIE_MODERN)
		/* Alt-C opens the joystick calibration dialog. */
		if (xio_Is_Joystick_Input() && xio_Is_Joystick_Callibrate() && key == 0x2E00) {
			xsound_Resume_Sounds();
			xdlgjoy_Do_Joystick_Callibrate();
			xsound_Pause_Sounds();
			xdialog_Clear_Dialog_Exit();
		} else
#endif
			/* ESC → select cancel button */
			if (key == 27) {
				xinpattr_Selected_Input(cancel_input);
			} else if (!TIE_FRONTEND_TIE98 || computer_mode != COMP_MODE_OPTIONS) {
				/* TIE98 consumes other keys while its options page is active. */
				return 0;
			}
		return 1;
	}

	/* Mouse handling */
	button = left ? left : right;

	switch (button) {
		case MOUSE_NO_PRESS:
			/* Hover: check if mouse is in the left panel for exit animation trigger */
			if (x <= TIE_FRONTEND_EDITION(76, 119) && y >= TIE_FRONTEND_EDITION(88, 130))
				input->var2 = 1;

			/* Medal hover text detection */
			new_mode = 0;
			if (computer_mode == COMP_MODE_MEDALS && pilot_medal_type[pilot_medal_page] == 2) {
				if (pilot_medal_status[pilot_medal_page]) {
					xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(92, 196), TIE_FRONTEND_EDITION(37, 100),
								   TIE_FRONTEND_EDITION(132, 266), TIE_FRONTEND_EDITION(103, 238));
					if (xrect_Point_In_Rect(&tr, x, y))
						new_mode = 1;
				}
				if (pilot_medal_bonus_status[pilot_medal_page]) {
					xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(223, 456), TIE_FRONTEND_EDITION(37, 100),
								   TIE_FRONTEND_EDITION(263, 523), TIE_FRONTEND_EDITION(103, 238));
					if (xrect_Point_In_Rect(&tr, x, y))
						new_mode = 2;
				}
			}
			if (new_mode != pilot_medal_text) {
				pilot_medal_text = new_mode;
				xview_Refresh_View();
			}
			break;

		case MOUSE_DOWN:
			/* Click: check tab switching */
			new_mode = computer_mode;
			for (i = 0; i < 4; i++) {
				if (xrect_Point_In_Rect((Rect*)&computer_mode_rect[i], r->left + x, r->top + y)) {
#ifdef TIE_MODERN
					shipext_Get_Pilot_Name(name, sizeof(name));
#else
					shipext_Get_Pilot_Name(name);
#endif
					if (i == 3 || name[0])
						new_mode = i;
					if (i == 0 && !pilot_medal_num_pages)
						new_mode = computer_mode;
				}
			}

			if (new_mode != computer_mode) {
				/* Tab switch: hide old mode's widgets */
				switch (computer_mode) {
					case COMP_MODE_MEDALS:
						xpal_Screen_To_Dest_Palette(0, 0, 255);
						if (TIE_FRONTEND_TIE98) {
							for (i = 0; i < 4; i++)
								xpal_Set_Dest_Palette(computer_palettes[i]);
						} else {
							for (i = 0; i < 5; i++) {
								if (i != 1)
									xpal_Set_Dest_Palette(computer_palettes[i]);
							}
						}
						xfade_Start_Full_Fade(FADE_WIPE_INSTANT, FADE_COLOR_CROSSFADE, 1, 0, 0);
					case COMP_MODE_RECORD:
						xinpattr_Hide_Input(next_info_input);
						xinpattr_Hide_Input(last_info_input);
						break;
					case COMP_MODE_BACKUP:
						xinpattr_Hide_Input(backup_input);
						xinpattr_Hide_Input(restore_input);
						break;
					case COMP_MODE_OPTIONS:
#ifdef TIE_MODERN
						TieComputer_ShowOptionsButton(false);
#endif
						break;
				}

				computer_mode = new_mode;

				/* Show new mode's widgets */
				switch (new_mode) {
					case COMP_MODE_MEDALS:
						computer_Set_Computer_Medal_Palette();
					case COMP_MODE_RECORD:
						xinpattr_Show_Input(next_info_input);
						xinpattr_Show_Input(last_info_input);
						break;
					case COMP_MODE_BACKUP:
						if (!pilot_record.exit_status)
							xinpattr_Show_Input(backup_input);
						xinpattr_Show_Input(restore_input);
						break;
					case COMP_MODE_OPTIONS:
#ifdef TIE_MODERN
						TieComputer_ShowOptionsButton(true);
#endif
						break;
				}

				xview_Refresh_View();
			} else {
				/* Same mode: dispatch to mode-specific click handler */
				switch (computer_mode) {
					case COMP_MODE_MEDALS:
						break;
					case COMP_MODE_RECORD:
						break;
					case COMP_MODE_BACKUP:
						computer_xupdate_Computer_Backup(r->left + x, r->top + y);
						break;
					case COMP_MODE_OPTIONS:
						computer_update_Computer_Prefs(r->left + x, r->top + y);
						break;
				}
			}
			break;
	}

	return 1;
}

/* ======================================================================
 * computer_iuser_Computer — main dialog user callback
 * ====================================================================== */

// FUNCTION: TIE95 0x835A4
// FUNCTION: TIE98 0x40C950
static void computer_iuser_Computer(Input* input, int32_t time) {
	Palette* screen_pal;
	int16_t i;
#if defined(TIE98) && !defined(TIE_MODERN)
	int16_t button_count;
#endif

	switch (input->id) {
		case 0:
			/* Parent dialog: exit animation */
			if (input->var2) {
				if (input->var1 < 120) {
					input->var1 += 60;
					xview_Refresh_View();
				}
				input->var2 = 0;
			} else if (input->var1) {
				input->var1 -= 60;
				xview_Refresh_View();
			}
#if defined(TIE98) && !defined(TIE_MODERN)
			if (joystick_reset_requested) {
				joystick_reset_requested = 0;
				button_count = computer_Check_Reset_Joystick();
				if (button_count) {
					button_count = Flight_GetJoystickButtonCount();
					for (i = 0; i < button_count && i < 28; i++) {
						switch (i) {
							case 0:
								options_gbl.joystick_keys[0] = 0x9C;
								break;
							case 1:
								options_gbl.joystick_keys[1] = 0x9D;
								break;
							case 2:
								options_gbl.joystick_keys[2] = 'r';
								break;
							case 3:
								options_gbl.joystick_keys[3] = '.';
								break;
							case 4:
								options_gbl.joystick_keys[4] = 'e';
								break;
							case 5:
								options_gbl.joystick_keys[5] = 'i';
								break;
							case 6:
								options_gbl.joystick_keys[6] = '[';
								break;
							case 7:
								options_gbl.joystick_keys[7] = '\b';
								break;
							case 8:
								options_gbl.joystick_keys[8] = '\r';
								break;
							case 9:
								options_gbl.joystick_keys[9] = ']';
								break;
						}
					}
				}
				xinpattr_Refresh_Input(input);
			}
#endif
			break;
		case 1:
		case 2:
			/* OK/Cancel buttons: restore palette on button-down with id 2 (cancel) */
			if (time == 1 && input->id == 2) {
				screen_pal = xpal_Get_Screen_Palette();
				xpal_Copy_Palette(screen_pal, computer_palette, 0, 32, 0);
				xpal_Put_Screen_Pal_Range(0, 32);
				if (TIE_FRONTEND_TIE98) {
					for (i = 0; i < 4; i++)
						xpal_Set_Screen_Palette(computer_palettes[i]);
				} else {
					for (i = 0; i < 5; i++) {
						if (i != 1)
							xpal_Set_Screen_Palette(computer_palettes[i]);
					}
				}
			}

#if defined(TIE98) && !defined(TIE_MODERN)
			/* Window close: confirm, then leave the dialog */
			if (g_closeRequested && g_closeRequestFrames >= 2) {
				computer_Check_Exit_To_DOS();
				input->var1 = 1;
				computer_display = 0;
				xdialog_Set_Dialog_Exit(input->id);
				break;
			}
#endif

			if (xinpattr_Get_Input_Selected(input)) {
				if (input->id == 1) {
#ifdef TIE_MODERN
					TieComputer_BeginConfirm(input);
#endif
#if defined(TIE98) && !defined(TIE_MODERN)
					if (computer_Check_Exit_To_DOS()) {
#else
					if (computer_Exit_To_DOS()) {
#endif
						input->var1 = 1;
						computer_display = 0;
					}
				} else {
					/* Accept */
					input->var1 = 1;
					computer_display = 0;
				}
				xview_Refresh_View();
			} else if (input->var1 == 1) {
				/* Post-selection cleanup: restore palette, signal dialog exit */
				xpal_Set_Screen_Palette(computer_palette);
				xdialog_Set_Dialog_Exit(input->id);
			}
			break;
	}
}

/* ======================================================================
 * computer_idraw_Computer — main dialog draw callback
 * ====================================================================== */

// FUNCTION: TIE95 0x836E0
// FUNCTION: TIE98 0x40CB70
static void computer_idraw_Computer(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
#ifdef TIE_MODERN
	int16_t font_id = TieProfile_UsesTie98Frontend() ? 2 : 0;
	const Rect* computer_mode_rect = active_mode_rect;
	const Rect* backup_rect = active_backup_rect;
#elif defined(TIE98)
	int16_t font_id = 2;
#else
	int16_t font_id = 0;
#endif
	PushButton* btn;
	char name[40];
	Rect tr;
	int16_t scroll_x, scroll_y;
	int16_t rank_state;
	int16_t tab_color;
	int16_t i, j, k;

	if (!refresh)
		return;

	switch (input->id) {
		case 0:
			/* Main background */
			xpaint_Paint_Clipped_Rect(r, 0);
			if (!computer_display)
				break;

			/* Draw the 4 background layers */
			for (i = 0; i < 4; i++) {
				if (i == 1) {
					/* Layer 1 scrolls for exit animation */
					if (TIE_FRONTEND_TIE98) {
						scroll_x = -(input->var1 / 2);
						scroll_y = input->var1 / 2 + 2 * input->var1;
					} else {
						scroll_x = -(input->var1 / 4);
						scroll_y = input->var1;
					}
				} else {
					scroll_x = 0;
					scroll_y = 0;
				}

				/* Secret Order rank insignia on layer 1 */
				if (i == 1 && pilot_record.secret_order_rank) {
					for (j = 0; j < 4; j++) {
						rank_state = -1;
						if (j == 0) {
							rank_state = pilot_record.secret_order_rank - 1;
							if (rank_state > 2)
								rank_state = 2;
						} else {
							if (pilot_record.secret_order_rank - 3 >= j)
								rank_state = j + 2;
						}
						if (rank_state >= 0) {
							xactor_Set_Actor_State(computer_actors[7], rank_state, 0);
							xactanim_Draw_Anim_Actor(computer_actors[7], r, clip_r, 0, 0, 1);
						}
					}

					/* Expansion pack rank overlays */
					k = -1;
					if (shipext_Is_Mission_Disk2())
						k = 5;
					else if (shipext_Is_Mission_Disk1())
						k = 0;

					if (k >= 0) {
						for (j = 0; j < 6; j++) {
							rank_state = pilot_record.secret_order_rank - 7 - j;
							if (rank_state >= 0) {
								xactor_Set_Actor_State(medal_actor2[k + 4], rank_state, 0);
								xactanim_Draw_Anim_Actor(medal_actor2[k + 4], r, clip_r, 0, 0, 1);
							}
						}
					}
				}

				/* Draw actor: use shifted version (actor i+2) for the medal display overlay,
				 * except when on medals tab with no battle medal */
				if (TIE_FRONTEND_TIE98) {
					/* TIE98 draws the medal-display overlay layer unshifted. */
					if (i == 2) {
						if (computer_mode || pilot_medal_type[pilot_medal_page])
							xactdelt_Draw_Delta_Actor(computer_actors[4], r, clip_r, 0, 0, 1);
						else
							xactdelt_Draw_Delta_Actor(computer_actors[2], r, clip_r, 0, 0, 1);
					} else {
						xactdelt_Draw_Delta_Actor(computer_actors[i], r, clip_r, scroll_x + computer_x[i],
												  scroll_y + computer_y[i], 1);
					}
				} else if (i == 2 && (computer_mode || pilot_medal_type[pilot_medal_page])) {
					xactdelt_Draw_Delta_Actor(computer_actors[i + 2], r, clip_r,
											  scroll_x + computer_x[i] - 41, scroll_y + computer_y[i], 1);
				} else {
					xactdelt_Draw_Delta_Actor(computer_actors[i], r, clip_r, scroll_x + computer_x[i],
											  scroll_y + computer_y[i], 1);
				}
			}

			/* Draw tab indicators */
			for (i = 0; i < 4; i++) {
				if (TIE_FRONTEND_TIE98)
					xactor_Set_Actor_State(computer_actors[6], computer_mode == i ? 2 * i + 1 : 2 * i, 0);
				else
					xactor_Set_Actor_State(computer_actors[6], computer_mode == i ? i + 4 : i, 0);
				xactanim_Draw_Anim_Actor(computer_actors[6], r, clip_r, 0, 0, 1);
			}

			/* Tab labels */
#ifdef TIE_MODERN
			shipext_Get_Pilot_Name(name, sizeof(name));
#else
			shipext_Get_Pilot_Name(name);
#endif

			/* Options tab (always accessible) */
			tab_color = (computer_mode == COMP_MODE_OPTIONS) ? 14 : 15;
			if (TIE_FRONTEND_TIE98) {
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeOptions), computer_mode_rect[3].left + 2,
										 439, 3, tab_color);
			} else {
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeOptions), computer_mode_rect[3].left + 1,
										 computer_mode_rect[3].bottom - 6, 1, tab_color);
			}

			/* Medals tab */
			if (name[0] && pilot_medal_num_pages) {
				tab_color = (computer_mode == COMP_MODE_MEDALS) ? 14 : 15;
				if (TIE_FRONTEND_TIE98) {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeMedals),
											 computer_mode_rect[0].left + 2, 439, 3, tab_color);
				} else {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeMedals),
											 computer_mode_rect[0].left + 1, computer_mode_rect[0].bottom - 6,
											 1, tab_color);
				}
			} else {
				if (TIE_FRONTEND_TIE98) {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeMedals),
											 computer_mode_rect[0].left + 2, 439, 3, 20);
				} else {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeMedals),
											 computer_mode_rect[0].left + 1, computer_mode_rect[0].bottom - 6,
											 1, 20);
				}
			}

			/* Record + Backup tabs */
			if (name[0]) {
				tab_color = (computer_mode == COMP_MODE_RECORD) ? 14 : 15;
				if (TIE_FRONTEND_TIE98) {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeRecord),
											 computer_mode_rect[1].left + 2, 439, 3, tab_color);
				} else {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeRecord),
											 computer_mode_rect[1].left + 1, computer_mode_rect[1].bottom - 6,
											 1, tab_color);
				}

				tab_color = (computer_mode == COMP_MODE_BACKUP) ? 14 : 15;
				if (TIE_FRONTEND_TIE98) {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeBackup),
											 computer_mode_rect[2].left + 2, 439, 3, tab_color);
				} else {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeBackup),
											 computer_mode_rect[2].left + 1, computer_mode_rect[2].bottom - 6,
											 1, tab_color);
				}
			} else {
				/* Greyed out (no pilot loaded) */
				if (TIE_FRONTEND_TIE98) {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeRecord),
											 computer_mode_rect[1].left + 2, 439, 3, 20);
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeBackup),
											 computer_mode_rect[2].left + 2, 439, 3, 20);
				} else {
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeRecord),
											 computer_mode_rect[1].left + 1, computer_mode_rect[1].bottom - 6,
											 1, 20);
					xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeBackup),
											 computer_mode_rect[2].left + 1, computer_mode_rect[2].bottom - 6,
											 1, 20);
				}
			}
			break;

		case 1:
			/* OK button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			if (TIE_FRONTEND_TIE98)
				xactor_Set_Actor_State(computer_actors[5], btn->pressed + 2, 0);
			else
				xactor_Set_Actor_State(computer_actors[5], 2 * btn->pressed + 1, 0);
			xactanim_Draw_Anim_Actor(computer_actors[5], r, clip_r, 0, 0, 1);
			break;

		case 2:
			/* Cancel button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			if (TIE_FRONTEND_TIE98)
				xactor_Set_Actor_State(computer_actors[5], btn->pressed, 0);
			else
				xactor_Set_Actor_State(computer_actors[5], 2 * btn->pressed, 0);
			xactanim_Draw_Anim_Actor(computer_actors[5], r, clip_r, 0, 0, 1);
			break;

		case 3:
			/* Backup button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			xpaint_Paint_Clipped_Rect(r, btn->pressed ? 38 : 16);
			xfont_Print_Centered_Text(textext_Get_Text(txtCompBackBackup), (Rect*)&backup_rect[5], font_id,
									  14);
			if (xinpattr_Is_Input_Dirty(input))
				xdirty_Dirty_Rect(clip_r);
			break;

		case 4:
			/* Restore button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			xpaint_Paint_Clipped_Rect(r, btn->pressed ? 38 : 16);
			xfont_Print_Centered_Text(textext_Get_Text(txtCompBackRestore), (Rect*)&backup_rect[6], font_id,
									  14);
			if (xinpattr_Is_Input_Dirty(input))
				xdirty_Dirty_Rect(clip_r);
			break;

#ifdef TIE_MODERN
		case 5:
			/* OpenTIE Options button */
			if (!computer_display || computer_mode != COMP_MODE_OPTIONS)
				break;
			btn = (PushButton*)input;
			xpaint_Paint_Clipped_Rect(r, btn->pressed ? 38 : 16);
			xpaint_Frame_Clipped_Rect(r, 38);
			xfont_Print_Centered_Text("OpenTIE Options", r, font_id, 14);
			if (xinpattr_Is_Input_Dirty(input))
				xdirty_Dirty_Rect(clip_r);
			break;
#endif
	}

	/* Mode-specific content (only for parent, id=0) */
	if (input->id == 0 && computer_display) {
		switch (computer_mode) {
			case COMP_MODE_MEDALS:
				computer_xdraw_Computer_Medal(r, clip_r);
				break;
			case COMP_MODE_RECORD:
				computer_xdraw_Computer_Info(r, clip_r);
				break;
			case COMP_MODE_BACKUP:
				computer_xdraw_Computer_Backup(r, clip_r);
				break;
			case COMP_MODE_OPTIONS:
				computer_draw_Computer_Prefs(r, clip_r);
				break;
		}

		/* Secret Order rank popup during exit scroll */
		if (input->var1 && pilot_record.secret_order_rank) {
			int16_t line_step = TIE_FRONTEND_EDITION(10, 20);

			xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(60, 120), TIE_FRONTEND_EDITION(80, 192),
						   TIE_FRONTEND_EDITION(260, 520), TIE_FRONTEND_EDITION(104, 250));
			xpaint_Paint_Clipped_Rect(&tr, 1);
			xpaint_Frame_Clipped_Rect(&tr, 16);
			xfont_Enable_FontID_Shadow(0);

			tr.top += TIE_FRONTEND_EDITION(2, 10);
			tr.bottom = tr.top + line_step;
			if (pilot_record.secret_order_rank > 6)
				xfont_Print_Centered_Text(
					textext_Get_Text(pilot_record.secret_order_rank + txtComp2Secret7 - 7), &tr, font_id, 15);
			else
				xfont_Print_Centered_Text(
					textext_Get_Text(pilot_record.secret_order_rank + txtCompSecret1 - 1), &tr, font_id, 15);

			tr.top = tr.bottom;
			tr.bottom = tr.top + line_step;
			xfont_Print_Centered_Text(textext_Get_Text(txtCompSecretOrder), &tr, font_id, 15);
			xfont_Disable_FontID_Shadow(0);
		}

		if (xinpattr_Is_Input_Dirty(input))
			xdirty_Dirty_Rect(clip_r);
	}
}

/* Retail TIE95 keeps this callback but never installs it. */
// FUNCTION: TIE95 0x83DD4
static void computer_idraw_Computer_Medal(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	PushButton* btn = (PushButton*)input;

	if (!refresh)
		return;

	if (btn->pressed)
		xpaint_Paint_Clipped_Rect(r, 38);
	else
		xpaint_Paint_Clipped_Rect(r, 16);
	xpaint_Frame_Clipped_Rect(r, 38);

	if (input->id)
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoLast), r, TIE_FRONTEND_EDITION(0, 2), 14);
	else
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoNext), r, TIE_FRONTEND_EDITION(0, 2), 14);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * computer_xdraw_Computer_Medal — medal display rendering (complex)
 * ====================================================================== */

// FUNCTION: TIE95 0x83E7C
// FUNCTION: TIE98 0x40D220
static void computer_xdraw_Computer_Medal(Rect* r, Rect* clip_r) {
	char string[40];
	char name1[40];
	char fmt[40];
	Rect tr;
	int16_t type, id, status, bonus;
	int16_t page, x, y;
	int16_t i, k;

	type = pilot_medal_type[pilot_medal_page];
	id = pilot_medal_id[pilot_medal_page];
	status = pilot_medal_status[pilot_medal_page];
	bonus = pilot_medal_bonus_status[pilot_medal_page];
	name1[0] = '\0';
	string[0] = '\0';

	switch (type) {
		case 0:
			/* Training certificate */
			textext_Copy_Text(name1, txtCompCertificate);
			xactdelt_Draw_Delta_Actor(medal_actor[12], r, clip_r, r->left, r->top, 1);

			for (i = 0; i < 6; i++) {
				if (pilot_record.train_max_level[i] >= 4) {
					page = i;
					if (i == 3)
						page = 4;
					else if (page == 4)
						page = 3;
					xactor_Set_Actor_State(medal_actor[0], page, 0);
					xactanim_Draw_Anim_Actor(medal_actor[0], r, clip_r, r->left, r->top, 1);
				}
			}

			/* Expansion pack training patch */
			k = -1;
			if (shipext_Is_Mission_Disk2())
				k = 5;
			else if (shipext_Is_Mission_Disk1())
				k = 0;

			if (k >= 0) {
				if (pilot_record.train_max_level[6] >= 4) {
					xactor_Set_Actor_State(medal_actor2[k], 0, 0);
					xactanim_Draw_Anim_Actor(medal_actor2[k], r, clip_r, r->left, r->top, 1);
				} else {
					xactor_Set_Actor_State(medal_actor2[k], 1, 0);
					xactanim_Draw_Anim_Actor(medal_actor2[k], r, clip_r, r->left, r->top, 1);
				}
			}
			break;
		case 1:
			/* Ship combat medal */
			if (id < 7)
				shipext_Get_Ship_Name(name1, id, 0, 0);
			textext_Copy_Text(string, status + txtCompBronze);

			if (id < 6) {
				xactor_Set_Actor_State(medal_actor[7], id, 0);
				xactanim_Draw_Anim_Actor(medal_actor[7], r, clip_r, r->left + TIE_FRONTEND_EDITION(34, 68),
										 r->top - TIE_FRONTEND_EDITION(50, 120), 1);
			} else {
				if (shipext_Is_Mission_Disk1()) {
					xactor_Set_Actor_State(medal_actor2[3], 0, 0);
					xactanim_Draw_Anim_Actor(medal_actor2[3], r, clip_r,
											 r->left + TIE_FRONTEND_EDITION(34, 68),
											 r->top - TIE_FRONTEND_EDITION(50, 120), 1);
				}
				if (shipext_Is_Mission_Disk2()) {
					xactor_Set_Actor_State(medal_actor2[8], 0, 0);
					xactanim_Draw_Anim_Actor(medal_actor2[8], r, clip_r,
											 r->left + TIE_FRONTEND_EDITION(34, 68),
											 r->top - TIE_FRONTEND_EDITION(50, 120), 1);
				}
			}
			break;
		case 2:
			/* Battle completion medal */
			switch (pilot_medal_text) {
				case 1:
					textext_Copy_Text(name1, txtCompSecGoal1);
					textext_Copy_Text(fmt, txtCompSecGoal2);
					sprintf(string, fmt, pilot_medal_status[pilot_medal_page],
							pilot_medal_id[pilot_medal_page] + 1);
					break;
				case 2:
					textext_Copy_Text(name1, txtCompBonGoal1);
					textext_Copy_Text(fmt, txtCompBonGoal2);
					sprintf(string, fmt, pilot_medal_bonus_status[pilot_medal_page],
							pilot_medal_id[pilot_medal_page] + 1);
					break;
				default:
					/* Battle name and status */
					textext_Copy_Text(string, txtCompInfoBattle);
					sprintf(name1, string, id + 1);
					if (pilot_record.battle_status[id] == 3) {
						if (id < 7)
							textext_Copy_Text(string, id + txtCompMedal1);
						else if (id < 13)
							textext_Copy_Text(string, id + txtComp3Medal8 - 7);
					} else {
						string[0] = '\0';
					}
					break;
			}

			/* Draw the battle medal actor */
			y = -TIE_FRONTEND_EDITION(28, 67);
			x = TIE_FRONTEND_EDITION(22, 44);

			/* Determine medal page/variant */
			switch (id) {
				case 0:
					page = 0;
					break;
				case 1:
					page = 6;
					break;
				case 2:
					page = 3;
					break;
				case 3:
					page = 2;
					break;
				case 6:
					page = 1;
					break;
				case 7:
				case 8:
				case 9:
				case 10:
				case 11:
				case 12:
					page = id;
					break;
				default:
					page = id;
					break;
			}

			if (pilot_record.battle_status[id] == 3) {
				if (page <= 6) {
					/* Base game battle medals */
					if (page != 4) {
						xactor_Set_Actor_Flip(medal_actor[1], 1, 0);
						xactor_Set_Actor_State(medal_actor[1], 3 * page, 0);
						xactanim_Draw_Anim_Actor(medal_actor[1], r, clip_r, x + TIE_FRONTEND_EDITION(-8, -16),
												 y, 1);
					}
					xactor_Set_Actor_Flip(medal_actor[1], 0, 0);
					xactor_Set_Actor_State(medal_actor[1], 3 * page, 0);
					xactanim_Draw_Anim_Actor(medal_actor[1], r, clip_r, x, y, 1);

					/* Special overlays for specific pages */
					if (page == 2) {
						xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(167, 334), TIE_FRONTEND_EDITION(77, 185),
									   TIE_FRONTEND_EDITION(195, 390), TIE_FRONTEND_EDITION(102, 245));
						xrect_Clip_Rect(&tr, clip_r);
						xcanvas_Set_Drawing_Canvas_Clip(&tr);
						xactor_Set_Actor_State(medal_actor[3], 4, 0);
						xactor_Set_Actor_State(medal_actor[4], 0, 0);
						xactdelt_Draw_Delta_Actor(medal_actor[8], r, &tr, x + TIE_FRONTEND_EDITION(-6, -12),
												  y + TIE_FRONTEND_EDITION(-3, -7), 1);
						xactanim_Draw_Anim_Actor(medal_actor[4], r, &tr, x + TIE_FRONTEND_EDITION(-33, -66),
												 y + TIE_FRONTEND_EDITION(50, 120), 1);
						xactanim_Draw_Anim_Actor(medal_actor[3], r, &tr, x + TIE_FRONTEND_EDITION(-32, -64),
												 y + TIE_FRONTEND_EDITION(50, 120), 1);
						xactdelt_Draw_Delta_Actor(medal_actor[10], r, &tr, x, y, 1);
					}
					if (page == 3) {
						xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(165, 334), TIE_FRONTEND_EDITION(77, 185),
									   TIE_FRONTEND_EDITION(197, 390), TIE_FRONTEND_EDITION(108, 259));
						xrect_Clip_Rect(&tr, clip_r);
						xcanvas_Set_Drawing_Canvas_Clip(&tr);
						xactdelt_Draw_Delta_Actor(medal_actor[8], r, &tr, x + TIE_FRONTEND_EDITION(-6, -12),
												  y + TIE_FRONTEND_EDITION(-3, -7), 1);
						xactor_Set_Actor_State(medal_actor[4], 1, 0);
						xactanim_Draw_Anim_Actor(medal_actor[4], r, &tr, x + TIE_FRONTEND_EDITION(-59, -118),
												 y + TIE_FRONTEND_EDITION(59, 142), 1);
						xactor_Set_Actor_State(medal_actor[4], 0, 0);
						xactanim_Draw_Anim_Actor(medal_actor[4], r, &tr, x + TIE_FRONTEND_EDITION(-36, -70),
												 y + TIE_FRONTEND_EDITION(57, 137), 1);
						xactdelt_Draw_Delta_Actor(medal_actor[11], r, &tr, x + TIE_FRONTEND_EDITION(1, 2),
												  y + TIE_FRONTEND_EDITION(3, 7), 1);
						xactdelt_Draw_Delta_Actor(medal_actor[9], r, &tr, x, y, 1);
					}
					if (page == 1) {
						xactdelt_Draw_Delta_Actor(medal_actor[13], r, clip_r, x, y, 1);
					}
				} else if (page <= 9) {
					/* Expansion pack 1 medals */
					if (shipext_Is_Mission_Disk1()) {
						page -= 7;
						xactor_Set_Actor_Flip(medal_actor2[1], 1, 0);
						xactor_Set_Actor_State(medal_actor2[1], page, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[1], r, clip_r,
												 x + TIE_FRONTEND_EDITION(-27, -53), y, 1);
						xactor_Set_Actor_Flip(medal_actor2[1], 0, 0);
						xactor_Set_Actor_State(medal_actor2[1], page, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[1], r, clip_r, x, y, 1);

						switch (page) {
							case 0:
								xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(167, 334),
											   TIE_FRONTEND_EDITION(77, 185), TIE_FRONTEND_EDITION(195, 390),
											   TIE_FRONTEND_EDITION(102, 245));
								xrect_Clip_Rect(&tr, clip_r);
								xactor_Set_Actor_State(medal_actor2[2], 0, 0);
								xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x, y, 1);
								xactor_Set_Actor_State(medal_actor2[2], 2, 0);
								xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x, y, 1);
								break;
							case 1:
								xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(167, 334),
											   TIE_FRONTEND_EDITION(77, 185), TIE_FRONTEND_EDITION(195, 390),
											   TIE_FRONTEND_EDITION(102, 245));
								xrect_Clip_Rect(&tr, clip_r);
								xactor_Set_Actor_State(medal_actor2[2], 3, 0);
								xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x, y, 1);
								break;
							case 2:
								xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(167, 334),
											   TIE_FRONTEND_EDITION(77, 185), TIE_FRONTEND_EDITION(195, 390),
											   TIE_FRONTEND_EDITION(102, 245));
								xrect_Clip_Rect(&tr, clip_r);
								xactor_Set_Actor_State(medal_actor2[2], 1, 0);
								xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x,
														 y + TIE_FRONTEND_EDITION(0, 10), 1);
								break;
						}
					}
				} else if (page <= 12) {
					/* Expansion pack 2 medals */
					if (shipext_Is_Mission_Disk2()) {
						page -= 10;
						xactor_Set_Actor_Flip(medal_actor2[6], 1, 0);
						xactor_Set_Actor_State(medal_actor2[6], page, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[6], r, clip_r,
												 x + TIE_FRONTEND_EDITION(-33, -65), y, 1);
						xactor_Set_Actor_Flip(medal_actor2[6], 0, 0);
						xactor_Set_Actor_State(medal_actor2[6], page, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[6], r, clip_r, x, y, 1);

						switch (page) {
							case 2:
								xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(167, 334),
											   TIE_FRONTEND_EDITION(77, 185), TIE_FRONTEND_EDITION(195, 390),
											   TIE_FRONTEND_EDITION(102, 245));
								xrect_Clip_Rect(&tr, clip_r);
								xactor_Set_Actor_State(medal_actor2[7], 0, 0);
								xactanim_Draw_Anim_Actor(medal_actor2[7], r, &tr, x, y, 1);
								break;
						}
					}
				}
			}

			xcanvas_Set_Drawing_Canvas_Clip(clip_r);

			/* Draw mission completion pips */
			x = -TIE_FRONTEND_EDITION(34, 68);
			y = -TIE_FRONTEND_EDITION(80, 192);
			xactor_Set_Actor_State(medal_actor[2], 2, 0);
			for (i = 0; i < status; i++) {
				if (i < 4)
					xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, x, y + TIE_FRONTEND_EDITION(16, 36) * i,
											 1);
				else
					xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, x - TIE_FRONTEND_EDITION(20, 40),
											 y + TIE_FRONTEND_EDITION(16, 36) * (i - 4), 1);
			}

			x = TIE_FRONTEND_EDITION(76, 152);
			y = -TIE_FRONTEND_EDITION(80, 192);
			xactor_Set_Actor_State(medal_actor[2], 0, 0);
			for (i = 0; i < bonus; i++) {
				if (i < 4)
					xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, x, y + TIE_FRONTEND_EDITION(16, 36) * i,
											 1);
				else
					xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, x + TIE_FRONTEND_EDITION(20, 40),
											 y + TIE_FRONTEND_EDITION(16, 36) * (i - 4), 1);
			}
	}

	/* Print medal name and description */
	if (name1[0]) {
		xrect_Set_Rect(&tr, TIE_FRONTEND_EDITION(92, 178), TIE_FRONTEND_EDITION(7, 17),
					   TIE_FRONTEND_EDITION(273, 547), TIE_FRONTEND_EDITION(117, 290));
		/* TIE95 uses fixed 8/9-pixel spacing; TIE98 advances by font height. */
		tr.bottom = tr.top + TIE_FRONTEND_EDITION(8, xfont_Get_FontID_Height(TIE_FRONTEND_EDITION(0, 2)));
		xfont_Enable_FontID_Shadow(TIE_FRONTEND_EDITION(0, 2));
		xfont_Print_Centered_Text(name1, &tr, TIE_FRONTEND_EDITION(0, 2), 15);
		if (string[0]) {
			xrect_Offset_Rect(&tr, 0,
							  TIE_FRONTEND_EDITION(9, xfont_Get_FontID_Height(TIE_FRONTEND_EDITION(0, 2))));
			xfont_Print_Centered_Text(string, &tr, TIE_FRONTEND_EDITION(0, 2), 15);
		}
		xfont_Disable_FontID_Shadow(TIE_FRONTEND_EDITION(0, 2));
	}
}

/* ======================================================================
 * Medal/Info page navigation buttons
 * ====================================================================== */

// FUNCTION: TIE95 0x84B84
// FUNCTION: TIE98 0x40DD20
static void computer_iuser_Computer_Info(Input* input, int32_t time) {
	(void)time;

	if (!xinpattr_Get_Input_Selected(input))
		return;

	if (computer_mode == COMP_MODE_MEDALS) {
		if (input->id == 1) {
			if (pilot_medal_page)
				pilot_medal_page--;
			else
				pilot_medal_page = pilot_medal_num_pages - 1;
		} else {
			if (pilot_medal_page == pilot_medal_num_pages - 1)
				pilot_medal_page = 0;
			else
				pilot_medal_page++;
		}
		computer_Set_Computer_Medal_Palette();
	} else if (computer_mode == COMP_MODE_RECORD) {
		if (input->id == 1) {
			if (pilot_info_page)
				pilot_info_page--;
			else
				pilot_info_page = pilot_info_num_pages - 1;
		} else {
			if (pilot_info_page == pilot_info_num_pages - 1)
				pilot_info_page = 0;
			else
				pilot_info_page++;
		}
	}

	xview_Refresh_View();
}

/* TIE98 keyboard shortcuts for the exit confirmation: Enter, 'y' or F7
 * select Yes, 'n' selects No. Without a key the buttons update normally. */
// FUNCTION: TIE98 0x4104B0
static int16_t computer_iupdate_Exit_Yes(Input* input, Rect* r, Rect* clip_r, int16_t key,
										 InputMouseEvent left, InputMouseEvent right, int16_t x, int16_t y) {
	if (key) {
		switch (key) {
			case 13: /* Enter */
				xinpattr_Selected_Input(exit_yes_input);
				return 1;
			case 'y':
				xinpattr_Selected_Input(exit_yes_input);
				return 1;
			case 193: /* F7 */
				xinpattr_Selected_Input(exit_yes_input);
				return 1;
		}
		return 0;
	}
	return xbtnpush_iupdate_Button(input, r, clip_r, 0, left, right, x, y);
}

// FUNCTION: TIE98 0x410540
static int16_t computer_iupdate_Exit_No(Input* input, Rect* r, Rect* clip_r, int16_t key,
										InputMouseEvent left, InputMouseEvent right, int16_t x, int16_t y) {
	if (key) {
		if (key != 'n')
			return 0;
		xinpattr_Selected_Input(cancel_input);
		return 1;
	}
	return xbtnpush_iupdate_Button(input, r, clip_r, 0, left, right, x, y);
}

// FUNCTION: TIE95 0x84C88
// FUNCTION: TIE98 0x40DE30
static void computer_idraw_Computer_Info(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	PushButton* btn = (PushButton*)input;

	if (!refresh)
		return;

	if (btn->pressed)
		xpaint_Paint_Clipped_Rect(r, 38);
	else
		xpaint_Paint_Clipped_Rect(r, 16);
	xpaint_Frame_Clipped_Rect(r, 38);

	if (input->id)
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoLast), r, TIE_FRONTEND_EDITION(0, 2), 14);
	else
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoNext), r, TIE_FRONTEND_EDITION(0, 2), 14);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * Info page dispatch
 * ====================================================================== */

// FUNCTION: TIE95 0x84CFC
// FUNCTION: TIE98 0x40DEB0
static void computer_xdraw_Computer_Info(Rect* r, Rect* clip_r) {
	Rect clip_tr, tr;
	int16_t color = 15;
	int16_t back_color = 38;
	int16_t start_top;

	(void)r;

	xrect_Set_Rect(&clip_tr, TIE_FRONTEND_EDITION(86, 172), TIE_FRONTEND_EDITION(7, 17),
				   TIE_FRONTEND_EDITION(273, 547), TIE_FRONTEND_EDITION(117, 290));
	xcanvas_Set_Drawing_Canvas_Clip(&clip_tr);
	xrect_Copy_Rect(&tr, &clip_tr);
	tr.bottom = tr.top + TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2));
	xrect_Offset_Rect(&tr, 0, -TIE_FRONTEND_EDITION(110, 273) * pilot_info_page);
	start_top = tr.top;

	computer_Draw_Computer_Header_Info(&tr, color, back_color);
	computer_Draw_Computer_Combat_Info(&tr, color, back_color);
	computer_Draw_Computer_Battle_Info(&tr, color, back_color);
	computer_Draw_Computer_Kills_Info(&tr, color, back_color);

	pilot_info_num_pages = (tr.top - start_top) / TIE_FRONTEND_EDITION(110, 273);
	xcanvas_Set_Drawing_Canvas_Clip(clip_r);
}

/* ======================================================================
 * computer_Set_Computer_Medal_Palette — crossfade to medal-specific palette
 * ====================================================================== */

// FUNCTION: TIE95 0x84DD8
// FUNCTION: TIE98 0x40DFB0
static int16_t computer_Set_Computer_Medal_Palette(void) {
	xpal_Screen_To_Dest_Palette(0, 0, 255);

	switch (pilot_medal_type[pilot_medal_page]) {
		case 0:
		case 3:
			xpal_Set_Dest_Palette(medal_palette[0]);
			break;
		case 1:
			xpal_Set_Dest_Palette(medal_palette[1]);
			if (pilot_medal_status[pilot_medal_page] < 2)
				xpal_Set_Dest_Palette(medal_palette[pilot_medal_status[pilot_medal_page] + 2]);
			break;
		case 2:
			xpal_Set_Dest_Palette(medal_palette[1]);
			break;
	}

	xfade_Start_Full_Fade(FADE_WIPE_INSTANT, FADE_COLOR_CROSSFADE, 1, 0, 0);
	return 1;
}

/* ======================================================================
 * Record info drawing — header, combat, battle, kills panels
 * ====================================================================== */

// FUNCTION: TIE95 0x84E64
// FUNCTION: TIE98 0x40E060
static void computer_Draw_Computer_Header_Info(Rect* r, int16_t color, int16_t back_color) {
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	char str1[80];
	char str2[40];
	Rect page;
	uint32_t val;

	xrect_Copy_Rect(&page, r);

	/* Rank + Name header */
	textext_Copy_Text(str1, pilot_record.rank + 1);
	strcat(str1, " ");
#ifdef TIE_MODERN
	shipext_Get_Pilot_Name(str2, sizeof(str2));
#else
	shipext_Get_Pilot_Name(str2);
#endif
	strcat(str1, str2);
	xfont_Print_Centered_Text(str1, r, font_id, color);
	xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
	xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id) + 2));

	/* Score + Skill */
	textext_Copy_Text(str1, txtCompInfoScore);
	snprintf(str2, sizeof(str2), " %ld    ", (long)pilot_record.score);
	strcat(str1, str2);
	textext_Cat_Text(str1, txtCompInfoSkill);
	snprintf(str2, sizeof(str2), " %u", (unsigned)pilot_record.avg_score);
	strcat(str1, str2);
	xfont_Print_Centered_Text(str1, r, font_id, color);
	xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id)));

	/* Laser accuracy */
	if (pilot_record.laser_hits)
		val = (uint32_t)(100 * pilot_record.laser_hits) / pilot_record.laser_total;
	else
		val = 0;
	textext_Copy_Text(str2, txtCompInfoLaser);
	snprintf(str1, sizeof(str1), str2, pilot_record.laser_hits, pilot_record.laser_total, (int16_t)val);
	xfont_Print_Centered_Text(str1, r, font_id, color);
	xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id)));

	/* Warhead accuracy */
	if (pilot_record.warhead_hits > pilot_record.warhead_total)
		pilot_record.warhead_total = pilot_record.warhead_hits;
	if (pilot_record.warhead_hits)
		val = 100 * pilot_record.warhead_hits / pilot_record.warhead_total;
	else
		val = 0;
	textext_Copy_Text(str2, txtCompInfoRocket);
	snprintf(str1, sizeof(str1), str2, pilot_record.warhead_hits, pilot_record.warhead_total, (int16_t)val);
	xfont_Print_Centered_Text(str1, r, font_id, color);
	xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id)));

	/* Total kills */
	textext_Copy_Text(str2, txtCompInfoKills);
	snprintf(str1, sizeof(str1), str2, pilot_record.total_kills);
	xfont_Print_Centered_Text(str1, r, font_id, color);
	xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id)));

	/* Total captures */
	textext_Copy_Text(str2, txtCompInfoCaptures);
	snprintf(str1, sizeof(str1), str2, pilot_record.total_captures);
	xfont_Print_Centered_Text(str1, r, font_id, color);
	xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id)));

	/* Craft lost */
	textext_Copy_Text(str2, txtCompInfoCraftLost);
	snprintf(str1, sizeof(str1), str2, pilot_record.ejection_count);
	xfont_Print_Centered_Text(str1, r, font_id, color);
	xrect_Offset_Rect(&page, 0, TIE_FRONTEND_EDITION(110, 273));
	xrect_Copy_Rect(r, &page);
}

// FUNCTION: TIE95 0x851E8
// FUNCTION: TIE98 0x40E470
static void computer_Draw_Computer_Combat_Info(Rect* r, int16_t color, int16_t back_color) {
	char str1[80];
	char str2[40];
	int16_t ship_info[12];
	Rect page;
	int num_ships = 0;
	int16_t count;
	int16_t i, j;

	if (TIE_FRONTEND_TIE98)
		xrect_Copy_Rect(&page, r);

	for (i = 0; i < 12; i++) {
		count = 0;
		if (shipext_Is_Ship(i)) {
			if (pilot_record.train_score[i])
				count = 1;
			for (j = 0; j < 8; j++) {
				if (pilot_record.combat_score[i][j])
					count++;
			}
		}
		if (count) {
			ship_info[i] = count;
			num_ships++;
		} else {
			ship_info[i] = 0;
		}
	}

	if (!num_ships)
		return;

	for (i = 0; i < 12; i++) {
		if (!ship_info[i])
			continue;

		shipext_Get_Ship_Name(str1, i, 0, 0);
		xfont_Print_Centered_Text(str1, r, TIE_FRONTEND_EDITION(0, 2), color);
		xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
		xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2) + 2));

		if (pilot_record.train_score[i]) {
			if (pilot_record.train_max_level[i] >= 4) {
				textext_Copy_Text(str2, txtCompInfoTrainComplete);
				sprintf(str1, str2, pilot_record.train_score[i]);
			} else {
				textext_Copy_Text(str2, txtCompInfoTrainIncomplete);
				sprintf(str1, str2, pilot_record.train_score[i]);
			}
			xfont_Print_Centered_Text(str1, r, TIE_FRONTEND_EDITION(0, 2), color);
			xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2)));
		}

		for (j = 0; j < 8; j++) {
			if (pilot_record.combat_score[i][j]) {
				if (pilot_record.combat_complete[i][j]) {
					textext_Copy_Text(str2, txtCompInfoCombatComplete);
					sprintf(str1, str2, j + 1, pilot_record.combat_score[i][j]);
				} else {
					textext_Copy_Text(str2, txtCompInfoCombatIncomplete);
					sprintf(str1, str2, j + 1, pilot_record.combat_score[i][j]);
				}
				xfont_Print_Centered_Text(str1, r, TIE_FRONTEND_EDITION(0, 2), color);
				xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2)));
			}
		}

		if (TIE_FRONTEND_TIE98) {
			xrect_Offset_Rect(&page, 0, 273);
			xrect_Copy_Rect(r, &page);
		} else {
			xrect_Offset_Rect(r, 0, (10 - ship_info[i]) * 10);
		}
	}
}

// FUNCTION: TIE95 0x854A0
// FUNCTION: TIE98 0x40E770
static void computer_Draw_Computer_Battle_Info(Rect* r, int16_t color, int16_t back_color) {
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	char str1[80];
	char str2[40];
	int16_t battle_info[20];
	int32_t total_score;
	int16_t num_battles = 0;
	int16_t max_missions;
	int16_t i, j;

	for (i = 0; i < 20; i++) {
		battle_info[i] = 0;
		if (pilot_record.battle_status[i] &&
			(pilot_record.tour_score[i][0] || pilot_record.battle_cursor[i])) {
			battle_info[i] = 1;
			num_battles++;
		}
	}

	if (!num_battles)
		return;

	for (i = 0; i < 20; i++) {
		Rect page;

		if (!battle_info[i])
			continue;
		xrect_Copy_Rect(&page, r);

		textext_Copy_Text(str2, txtCompInfoBattle);
		snprintf(str1, sizeof(str1), str2, i + 1);
		textext_Cat_Text(str1, pilot_record.battle_status[i] + txtCompInfoBattle);
		xfont_Print_Centered_Text(str1, r, font_id, color);
		xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
		xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id) + 2));

		max_missions = pilot_record.battle_cursor[i] + 1;
		if (shipext_Get_Tour_Battle_Size(i) < max_missions)
			max_missions = shipext_Get_Tour_Battle_Size(i);

		/* If battle in progress and last mission has no score, trim */
		if (pilot_record.battle_status[i] == 1 && !pilot_record.tour_score[i][max_missions - 1] &&
			max_missions > 1) {
			max_missions--;
		}

		total_score = 0;
		for (j = 0; j < max_missions; j++) {
			textext_Copy_Text(str2, txtCompInfoMissionPoints);
			snprintf(str1, sizeof(str1), str2, j + 1, pilot_record.tour_score[i][j]);
			xfont_Print_Centered_Text(str1, r, font_id, color);
			total_score += pilot_record.tour_score[i][j];
			xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id)));
		}

		if (total_score && max_missions > 1) {
			xpaint_Horiz_Clipped_Line(r->left + 10, r->top - 1, r->right - r->left - 20, back_color);
			xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(0, 2));
			textext_Copy_Text(str2, txtCompTotalScore);
			snprintf(str1, sizeof(str1), str2, (long)total_score);
			xfont_Print_Centered_Text(str1, r, font_id, color);
			xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(font_id)));
		}

		xrect_Offset_Rect(&page, 0, TIE_FRONTEND_EDITION(110, 273));
		xrect_Copy_Rect(r, &page);
	}
}

// FUNCTION: TIE95 0x857C0
// FUNCTION: TIE98 0x40EA90
static void computer_Draw_Computer_Kills_Info(Rect* r, int16_t color, int16_t back_color) {
	char str1[80];
	Rect page;
	int16_t num_craft = 0;
	int16_t craft_count;
	int16_t count;
	int16_t i;

	if (TIE_FRONTEND_TIE98)
		xrect_Copy_Rect(&page, r);

	for (i = 0; i < NUM_SPEC; i++) {
		if (pilot_record.kills_by_ship_type[i])
			num_craft++;
	}

	if (!num_craft)
		return;

	count = 0;
	craft_count = 0;
	for (i = 0; i < NUM_SPEC && craft_count < num_craft; i++) {
		if (!(count % TIE_FRONTEND_EDITION(11, 14))) {
			if (TIE_FRONTEND_TIE98 && count) {
				xrect_Offset_Rect(&page, 0, TIE_FRONTEND_EDITION(110, 273));
				xrect_Copy_Rect(r, &page);
			}
			textext_Copy_Text(str1, txtCompInfoVictories);
			xfont_Print_Centered_Text(str1, r, TIE_FRONTEND_EDITION(0, 2), color);
			xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
			xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2) + 2));
			count++;
		}

		if (pilot_record.kills_by_ship_type[i]) {
			textext_Get_Ship_Text(str1, i);
			xfont_Print_Clipped_Text(str1, r->left + 8, r->top + 1, TIE_FRONTEND_EDITION(0, 2), color);
			sprintf(str1, "%d", pilot_record.kills_by_ship_type[i]);
			xfont_Print_Clipped_Text(str1, r->right - 30, r->top + 1, TIE_FRONTEND_EDITION(0, 2), color);
			xrect_Offset_Rect(r, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2)));
			count++;
			craft_count++;
		}
	}

	if (TIE_FRONTEND_TIE98) {
		xrect_Offset_Rect(&page, 0, 273);
		xrect_Copy_Rect(r, &page);
	} else {
		/* TIE95 pads the column out to a whole page of ten rows. */
		int16_t remainder = num_craft % 10;

		if (remainder)
			xrect_Offset_Rect(r, 0, (10 - remainder) * 10);
	}
}

/* ======================================================================
 * Preferences panel — update + draw
 * ====================================================================== */

// FUNCTION: TIE95 0x85974
// FUNCTION: TIE98 0x40EC80
static void computer_update_Computer_Prefs(int16_t x, int16_t y) {
#ifdef TIE_MODERN
	const Rect* pref_rect = active_pref_rect;
#endif
	int16_t refresh = 0;

	if (xrect_Point_In_Rect((Rect*)&pref_rect[2], x, y)) {
		options_gbl.music_active = (x < pref_rect[2].left + (pref_rect[2].right - pref_rect[2].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[3], x, y)) {
		options_gbl.music_volume = (x - pref_rect[3].left) / TIE_FRONTEND_EDITION(4, 8);
		if (options_gbl.music_volume > 16)
			options_gbl.music_volume = 16;
		/* TIE98 couples each volume gauge to its enable flag; TIE95 does not. */
		if (TIE_FRONTEND_TIE98)
			options_gbl.music_active = options_gbl.music_volume != 0;
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[5], x, y)) {
		options_gbl.sound_active = (x < pref_rect[5].left + (pref_rect[5].right - pref_rect[5].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[6], x, y)) {
		options_gbl.sound_volume = (x - pref_rect[6].left) / TIE_FRONTEND_EDITION(4, 8);
		if (options_gbl.sound_volume > 16)
			options_gbl.sound_volume = 16;
		if (TIE_FRONTEND_TIE98)
			options_gbl.sound_active = options_gbl.sound_volume != 0;
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[8], x, y)) {
		options_gbl.speech_active = (x < pref_rect[8].left + (pref_rect[8].right - pref_rect[8].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[9], x, y)) {
		options_gbl.speech_volume = (x - pref_rect[9].left) / TIE_FRONTEND_EDITION(4, 8);
		if (options_gbl.speech_volume > 16)
			options_gbl.speech_volume = 16;
		if (TIE_FRONTEND_TIE98)
			options_gbl.speech_active = options_gbl.speech_volume != 0;
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[11], x, y)) {
		options_gbl.transition_active =
			(x < pref_rect[11].left + (pref_rect[11].right - pref_rect[11].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[13], x, y)) {
		options_gbl.text_active = (x < pref_rect[13].left + (pref_rect[13].right - pref_rect[13].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[15], x, y)) {
		int16_t third = (pref_rect[15].right - pref_rect[15].left) / 3;
		if (x < pref_rect[15].left + third)
			options_gbl.game_level = 0;
		else if (x < pref_rect[15].right - third)
			options_gbl.game_level = 1;
		else
			options_gbl.game_level = 2;
		refresh = 1;
	}
	if (refresh)
		xview_Refresh_View();
}

// FUNCTION: TIE95 0x85CAC
// FUNCTION: TIE98 0x40F1E0
static void computer_draw_Computer_Prefs(Rect* r, Rect* clip_r) {
#ifdef TIE_MODERN
	int16_t font_id = TieProfile_UsesTie98Frontend() ? 2 : 0;
	const Rect* pref_rect = active_pref_rect;
#elif defined(TIE98)
	int16_t font_id = 2;
#else
	int16_t font_id = 0;
#endif
	int16_t i;
	(void)r;
	(void)clip_r;

	for (i = 0; i < 16; i++)
		xpaint_Frame_Clipped_Rect((Rect*)&pref_rect[i], 38);

	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefTitle), (Rect*)&pref_rect[0], font_id, 15);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefMusic), (Rect*)&pref_rect[1], font_id, 15);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefSound), (Rect*)&pref_rect[4], font_id, 15);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefSpeech), (Rect*)&pref_rect[7], font_id, 15);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefTrans), (Rect*)&pref_rect[10],
							  TIE_FRONTEND_EDITION(0, 3), 15);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefSub), (Rect*)&pref_rect[12],
							  TIE_FRONTEND_EDITION(0, 3), 15);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefGame), (Rect*)&pref_rect[14], font_id, 15);
	computer_draw_Computer_On_Off((Rect*)&pref_rect[2], options_gbl.music_active);
	computer_draw_Computer_On_Off((Rect*)&pref_rect[5], options_gbl.sound_active);
	computer_draw_Computer_On_Off((Rect*)&pref_rect[8], options_gbl.speech_active);
	computer_draw_Computer_On_Off((Rect*)&pref_rect[11], options_gbl.transition_active);
	computer_draw_Computer_On_Off((Rect*)&pref_rect[13], options_gbl.text_active);
	computer_draw_Computer_Level((Rect*)&pref_rect[15], options_gbl.game_level);
	computer_draw_Computer_Gauge((Rect*)&pref_rect[3], options_gbl.music_volume);
	computer_draw_Computer_Gauge((Rect*)&pref_rect[6], options_gbl.sound_volume);
	computer_draw_Computer_Gauge((Rect*)&pref_rect[9], options_gbl.speech_volume);
}

/* ======================================================================
 * Backup panel — update + draw + user
 * ====================================================================== */

// FUNCTION: TIE95 0x85E64
// FUNCTION: TIE98 0x40F700
static void computer_xupdate_Computer_Backup(int16_t x, int16_t y) {
#ifdef TIE_MODERN
	const Rect* backup_rect = active_backup_rect;
#endif
	int16_t refresh = 0;

	if (xrect_Point_In_Rect((Rect*)&backup_rect[2], x, y)) {
		options_gbl.auto_backup =
			(x < backup_rect[2].left + (backup_rect[2].right - backup_rect[2].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&backup_rect[4], x, y)) {
		options_gbl.auto_restore =
			(x < backup_rect[4].left + (backup_rect[4].right - backup_rect[4].left) / 2);
		refresh = 1;
	}

	if (refresh)
		xview_Refresh_View();
}

// FUNCTION: TIE95 0x85F20
// FUNCTION: TIE98 0x40F7A0
static void computer_iuser_Computer_Backup(Input* input, int32_t time) {
	(void)time;

	if (input->id < 3 || input->id > 4)
		return;
	if (!xinpattr_Get_Input_Selected(input))
		return;

#ifdef TIE_MODERN
	TieComputer_BeginConfirm(input);
#endif
	if (input->id == 3) {
		if (!computer_Check_Backup_Pilot())
			return;
		shipext_Backup_Pilot();
		backup_pilot_rank = pilot_record.rank;
		backup_pilot_points = pilot_record.score;
	} else {
		if (!computer_Check_Restore_Pilot())
			return;
		shipext_Restore_Pilot();
		computer_Init_Computer_Medal();
		pilot_info_page = 0;
		restore_pilot = 1;
		xinpattr_Show_Input(backup_input);
	}
	xview_Refresh_View();
}

// FUNCTION: TIE95 0x85FB4
// FUNCTION: TIE98 0x40F840
static void computer_xdraw_Computer_Backup(Rect* r, Rect* clip_r) {
#ifdef TIE_MODERN
	int16_t font_id = TieProfile_UsesTie98Frontend() ? 2 : 0;
	const Rect* backup_rect = active_backup_rect;
#elif defined(TIE98)
	int16_t font_id = 2;
#else
	int16_t font_id = 0;
#endif
	char name[64];
	/* TIE98 uses a 36-byte scratch buffer for the displayed pilot name. */
	char pilot_name[36];
	char points[12];
	Rect tr;
	int16_t color = 38;
	int16_t i;

	(void)r;
	(void)clip_r;

	/* Frame all backup rects */
	for (i = 0; i < 8; i++) {
		if (i < 5 || i > 5) {
			if (i == 7) {
				/* Split info panel into two halves */
				xrect_Copy_Rect(&tr, (Rect*)&backup_rect[7]);
				tr.bottom = tr.top + (tr.bottom - tr.top) / 2;
				xpaint_Paint_Clipped_Rect(&tr, 34);
				tr.top = tr.bottom;
				tr.bottom = backup_rect[i].bottom;
				xpaint_Paint_Clipped_Rect(&tr, 82);
				xrect_Copy_Rect(&tr, (Rect*)&backup_rect[i]);
				xpaint_Frame_Clipped_Rect(&tr, color);
				xpaint_Horiz_Clipped_Line(tr.left, (tr.bottom - tr.top) / 2 + tr.top, tr.right - tr.left,
										  color);
			} else {
				xpaint_Frame_Clipped_Rect((Rect*)&backup_rect[i], color);
			}
		} else {
			/* Backup button (index 5): only show if pilot not lost */
			if (!pilot_record.exit_status)
				xpaint_Frame_Clipped_Rect((Rect*)&backup_rect[i], color);
		}
	}

	color = 15;
	xfont_Print_Centered_Text(textext_Get_Text(txtCompBackTitle), (Rect*)&backup_rect[0], font_id, 15);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompBackAutoBackup), (Rect*)&backup_rect[1], font_id,
							  color);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompBackAutoRestore), (Rect*)&backup_rect[3], font_id,
							  color);

	/* Current pilot info */
	xrect_Copy_Rect(&tr, (Rect*)&backup_rect[7]);
	tr.top += TIE_FRONTEND_EDITION(2, 5);
	tr.bottom = tr.top + TIE_FRONTEND_EDITION(8, xfont_Get_FontID_Height(font_id));

	textext_Copy_Text(name, pilot_record.rank + txtCompRankCadet);
	strcat(name, " ");
#ifdef TIE_MODERN
	shipext_Get_Pilot_Name(pilot_name, sizeof(pilot_name));
#else
	shipext_Get_Pilot_Name(pilot_name);
#endif
	strcat(name, pilot_name);
	xfont_Print_Centered_Text(name, &tr, font_id, color);

	xrect_Offset_Rect(&tr, 0, TIE_FRONTEND_EDITION(9, xfont_Get_FontID_Height(font_id)));
	if (pilot_record.exit_status) {
		textext_Copy_Text(name, pilot_record.exit_status + txtCompStatusCapture - 1);
		strcat(name, " ");
	} else {
		name[0] = '\0';
	}
	textext_Cat_Text(name, txtCompNameWith);
	snprintf(points, sizeof(points), " %ld ", (long)pilot_record.score);
	strcat(name, points);
	textext_Cat_Text(name, txtCompNamePoints);
	xfont_Print_Centered_Text(name, &tr, font_id, color);

	/* Backup pilot info */
	if (TIE_FRONTEND_TIE98) {
		xrect_Copy_Rect(&tr, (Rect*)&backup_rect[7]);
		tr.top += (tr.bottom - tr.top) / 2 + 5;
		tr.bottom = tr.top + xfont_Get_FontID_Height(font_id);
	} else {
		xrect_Offset_Rect(&tr, 0, 11);
	}
	textext_Copy_Text(name, txtCompNameLast);
	strcat(name, " ");
	textext_Cat_Text(name, backup_pilot_rank + txtCompRankCadet);
	xfont_Print_Centered_Text(name, &tr, font_id, 90);

	xrect_Offset_Rect(&tr, 0, TIE_FRONTEND_EDITION(9, xfont_Get_FontID_Height(font_id)));
	snprintf(points, sizeof(points), " %ld ", (long)backup_pilot_points);
	textext_Copy_Text(name, txtCompNameWith);
	strcat(name, points);
	textext_Cat_Text(name, txtCompNamePoints);
	xfont_Print_Centered_Text(name, &tr, font_id, 90);

	computer_draw_Computer_On_Off((Rect*)&backup_rect[2], options_gbl.auto_backup);
	computer_draw_Computer_On_Off((Rect*)&backup_rect[4], options_gbl.auto_restore);
}

/* ======================================================================
 * Small drawing helpers
 * ====================================================================== */

// FUNCTION: TIE95 0x863C8
// FUNCTION: TIE98 0x40FCC0
static void computer_draw_Computer_On_Off(Rect* r, int16_t on) {
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	Rect tr1, tr2;

	xrect_Copy_Rect(&tr1, r);
	xrect_Inset_Rect(&tr1, 1, 1);
	tr1.right = tr1.left + (tr1.right - tr1.left) / 2;

	xrect_Copy_Rect(&tr2, r);
	xrect_Inset_Rect(&tr2, 1, 1);
	tr2.left = tr1.right;

	if (on) {
		xpaint_Paint_Clipped_Rect(&tr1, 38);
		xpaint_Paint_Clipped_Rect(&tr2, 0);
	} else {
		xpaint_Paint_Clipped_Rect(&tr1, 0);
		xpaint_Paint_Clipped_Rect(&tr2, 38);
	}

	xfont_Print_Centered_Text(textext_Get_Text(txtCompGaugeOn), &tr1, font_id, 14);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompGaugeOff), &tr2, font_id, 14);
}

// FUNCTION: TIE95 0x86498
// FUNCTION: TIE98 0x40FDB0
static void computer_draw_Computer_Level(Rect* r, int16_t state) {
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	Rect tr1, tr2, tr3;

	xrect_Copy_Rect(&tr1, r);
	xrect_Inset_Rect(&tr1, 1, 1);
	tr1.right = tr1.left + (tr1.right - tr1.left) / 3;

	xrect_Copy_Rect(&tr2, r);
	xrect_Inset_Rect(&tr2, 1, 1);
	tr2.left = tr1.right;
	tr2.right -= (r->right - r->left) / 3;

	xrect_Copy_Rect(&tr3, r);
	xrect_Inset_Rect(&tr3, 1, 1);
	tr3.left = tr2.right;

	xpaint_Paint_Clipped_Rect(&tr1, 0);
	xpaint_Paint_Clipped_Rect(&tr2, 0);
	xpaint_Paint_Clipped_Rect(&tr3, 0);

	if (state == 0)
		xpaint_Paint_Clipped_Rect(&tr1, 38);
	else if (state == 1)
		xpaint_Paint_Clipped_Rect(&tr2, 38);
	else if (state == 2)
		xpaint_Paint_Clipped_Rect(&tr3, 38);

	xfont_Print_Centered_Text(textext_Get_Text(txtCompLevelEasy), &tr1, font_id, 14);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompLevelMed), &tr2, font_id, 14);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompLevelHard), &tr3, font_id, 14);
}

// FUNCTION: TIE95 0x865F4
// FUNCTION: TIE98 0x40FF90
static void computer_draw_Computer_Gauge(Rect* r, int16_t amount) {
	Rect tr;
	int16_t i;

	xrect_Copy_Rect(&tr, r);
	xrect_Inset_Rect(&tr, 1, 1);
	xpaint_Paint_Clipped_Rect(&tr, 16);
	xrect_Inset_Rect(&tr, 1, 1);
	tr.left += TIE_FRONTEND_EDITION(1, 3);
	tr.right = tr.left + TIE_FRONTEND_EDITION(4, 8) - 1;

	for (i = 0; i < amount; i++) {
		xpaint_Frame_Clipped_Rect(&tr, 14);
		xrect_Offset_Rect(&tr, TIE_FRONTEND_EDITION(4, 8), 0);
	}
}

/* In the modern build the confirmation cannot block inside an input
 * callback. The runtime runs the confirmation as a sub-dialog and then
 * re-enters the calling callback, where the stored result is returned. */

// FUNCTION: TIE95 0x86738
// FUNCTION: TIE98 0x4101F0
static int16_t computer_Check_Backup_Pilot(void) {
	Input* the_input;
	int16_t retval;

#ifdef TIE_MODERN
	if (TieComputer_TakeConfirmResult(&retval))
		return retval != 2;
#endif
	the_input = computer_Build_Exit(txtCompBackupPilot);
	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(189, 365), TIE_FRONTEND_EDITION(104, 240));
#ifdef TIE_MODERN
	TieComputer_RunConfirm(the_input);
	return 0;
#else
	retval = xdialog_Handle_Dialog_View(the_input);
	xinput_Free_Inputs(the_input);
	xdialog_Clear_Dialog_Exit();
	return retval != 2;
#endif
}

// FUNCTION: TIE95 0x86778
// FUNCTION: TIE98 0x410240
static int16_t computer_Check_Restore_Pilot(void) {
	Input* the_input;
	int16_t retval;

#ifdef TIE_MODERN
	if (TieComputer_TakeConfirmResult(&retval))
		return retval != 2;
#endif
	the_input = computer_Build_Exit(txtCompRestorePilot);
	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(189, 365), TIE_FRONTEND_EDITION(104, 240));
#ifdef TIE_MODERN
	TieComputer_RunConfirm(the_input);
	return 0;
#else
	retval = xdialog_Handle_Dialog_View(the_input);
	xinput_Free_Inputs(the_input);
	xdialog_Clear_Dialog_Exit();
	return retval != 2;
#endif
}

#if defined(TIE98) && !defined(TIE_MODERN)
// FUNCTION: TIE98 0x410290
static int16_t computer_Check_Reset_Joystick(void) {
	Input* the_input;
	int16_t retval;

	the_input = computer_Build_Exit(336);
	xio_Set_Mouse_Position(365, 240);
	retval = xdialog_Handle_Dialog_View(the_input);
	xinput_Free_Inputs(the_input);
	xdialog_Clear_Dialog_Exit();
	return retval != 2;
}

/* TIE98 also services the window-close request: once the close has been
 * pending for three dialog frames the confirmation is not shown again. */
// FUNCTION: TIE98 0x4102E0
static int16_t computer_Check_Exit_To_DOS(void) {
	Input* the_input;
	int16_t retval = 0;

	the_input = computer_Build_Exit(txtCompExitDOS);
	xio_Set_Mouse_Position(365, 240);
	if (g_closeRequestFrames < 3)
		retval = xdialog_Handle_Dialog_View(the_input);
	xinput_Free_Inputs(the_input);
	xdialog_Clear_Dialog_Exit();
	if (retval != 2)
		return 1;
	g_closeRequested = 0;
	return 0;
}
#else
// FUNCTION: TIE95 0x867B8
static int16_t computer_Exit_To_DOS(void) {
	Input* the_input;
	int16_t retval;

#ifdef TIE_MODERN
	if (TieComputer_TakeConfirmResult(&retval))
		return retval != 2;
#endif
	the_input = computer_Build_Exit(txtCompExitDOS);
	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(189, 365), TIE_FRONTEND_EDITION(104, 240));
#ifdef TIE_MODERN
	TieComputer_RunConfirm(the_input);
	return 0;
#else
	retval = xdialog_Handle_Dialog_View(the_input);
	xinput_Free_Inputs(the_input);
	xdialog_Clear_Dialog_Exit();
	return retval != 2;
#endif
}

#endif

// FUNCTION: TIE95 0x867F8
// FUNCTION: TIE98 0x410350
static Input* computer_Build_Exit(int16_t id) {
	Rect r;
	Input* the_input;
	Input* button;

	xrect_Set_Rect(&r, 0, 0, TIE_FRONTEND_EDITION(160, 340), TIE_FRONTEND_EDITION(22, 53));
	the_input = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(the_input, computer_idraw_Exit);
	xinpattr_Set_Input_Allign(the_input, 1, 1);
	xinpattr_Start_Input(the_input);

	/* Store text_id in var1 for the draw callback */
	the_input->var1 = id;

	strcpy(comp_exit_str[0], textext_Get_Text(txtCompExitYes));
	strcpy(comp_exit_str[1], textext_Get_Text(txtCompExitNo));

	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(80, 180), TIE_FRONTEND_EDITION(3, 7),
				   TIE_FRONTEND_EDITION(116, 252), TIE_FRONTEND_EDITION(19, 46));
	button = (Input*)xbtnpush_Alloc_Button(the_input, &r, 0, computer_iuser_Exit, comp_exit_str[0], 1);
	if (TIE_FRONTEND_TIE98) {
		exit_yes_input = button;
		xinpattr_Set_Input_Update_Function(exit_yes_input, computer_iupdate_Exit_Yes);
	}

	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(120, 260), TIE_FRONTEND_EDITION(3, 7),
				   TIE_FRONTEND_EDITION(156, 332), TIE_FRONTEND_EDITION(19, 46));
	button = (Input*)xbtnpush_Alloc_Button(the_input, &r, 0, computer_iuser_Exit, comp_exit_str[1], 2);
	if (TIE_FRONTEND_TIE98) {
		/* The No button becomes the cancel target, so Esc answers No too. */
		cancel_input = button;
		xinpattr_Set_Input_Update_Function(cancel_input, computer_iupdate_Exit_No);
	}

	return the_input;
}

/* ======================================================================
 * Confirmation dialogs
 * ====================================================================== */

// FUNCTION: TIE95 0x86910
// FUNCTION: TIE98 0x4105A0
static void computer_idraw_Exit(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;

	xstyle_Style_Paint_Border(r, 0);
	xfont_Print_Clipped_Text(textext_Get_Text(input->var1), r->left + TIE_FRONTEND_EDITION(4, 8),
							 r->top + TIE_FRONTEND_EDITION(7, 17), TIE_FRONTEND_EDITION(0, 2), 15);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

// FUNCTION: TIE95 0x86968
// FUNCTION: TIE98 0x410610
static void computer_iuser_Exit(Input* input, int32_t time) {
	(void)time;

	switch (input->id) {
		case 1:
			if (xinpattr_Get_Input_Selected(input))
				xdialog_Set_Dialog_Exit(1);
			break;
		case 2:
			if (xinpattr_Get_Input_Selected(input))
				xdialog_Set_Dialog_Exit(2);
			break;
	}
}

/* ======================================================================
 * computer_Init_Computer_Medal — scan pilot record, build medal page arrays
 * ====================================================================== */

// FUNCTION: TIE95 0x869A4
// FUNCTION: TIE98 0x410650
static int16_t computer_Init_Computer_Medal(void) {
	int16_t count, bits;
	int16_t i, j;

	pilot_medal_page = 0;
	pilot_medal_text = 0;

	/* Page 0: training certificate (always present) */
	pilot_medal_type[0] = 0;
	pilot_medal_id[0] = 1;
	pilot_medal_status[0] = 0;
	pilot_medal_bonus_status[0] = 0;
	pilot_medal_num_pages = 1;

	/* Per-ship combat medals: earned when >= 2 missions completed */
	for (i = 0; i < 12; i++) {
		count = 0;
		for (j = 0; j < 8; j++) {
			if (pilot_record.combat_complete[i][j])
				count++;
		}
		if (count >= 2) {
			pilot_medal_type[pilot_medal_num_pages] = 1;
			pilot_medal_id[pilot_medal_num_pages] = i;
			if (count > 4)
				count = 4;
			pilot_medal_status[pilot_medal_num_pages] = count - 2;
			pilot_medal_bonus_status[pilot_medal_num_pages++] = 0;
		}
	}

	/* Per-battle completion medals */
	for (i = 0; i < 20; i++) {
		if (pilot_record.mission_bonus_bits[i] + pilot_record.secret_complete_bits[i] +
			(pilot_record.battle_status[i] == 3)) {
			pilot_medal_type[pilot_medal_num_pages] = 2;
			pilot_medal_id[pilot_medal_num_pages] = i;

			bits = 0;
			for (j = 0; j < 8; j++) {
				if ((1 << j) & pilot_record.secret_complete_bits[i])
					bits++;
			}
			pilot_medal_status[pilot_medal_num_pages] = bits;

			bits = 0;
			for (j = 0; j < 8; j++) {
				if ((1 << j) & pilot_record.mission_bonus_bits[i])
					bits++;
			}
			pilot_medal_bonus_status[pilot_medal_num_pages++] = bits;
		}
	}

	return 1;
}

/* ======================================================================
 * computer_Find_Backup_Pilot_Info — read backup pilot rank/score from .tfr file
 * ====================================================================== */

// FUNCTION: TIE95 0x86B60
// FUNCTION: TIE98 0x4107D0
static int16_t computer_Find_Backup_Pilot_Info(void) {
	/* TIE98 stack size; also accommodates the widened runtime pilot name. */
	char file_name[40];
	LandruFile* the_file;

	backup_pilot_rank = 0;
	backup_pilot_points = 0;
#ifdef TIE_MODERN
	shipext_Get_Pilot_Name(file_name, sizeof(file_name));
#else
	shipext_Get_Pilot_Name(file_name);
#endif
	strcat(file_name, ".tfr");

	the_file = xfile_Open_File(LANDRU_FILE_ROOT_USER, file_name, "rb");
	if (the_file) {
		/* Seek to backup slot: offset 1930 from start of second slot */
		xfile_Seek_File(the_file, 1930, 1);
		xfile_Read_Byte_From_File(the_file, (uint8_t*)&backup_pilot_rank);
		xfile_Seek_File(the_file, 1, 1);
		xfile_Read_Long_From_File(the_file, (int32_t*)&backup_pilot_points);
		xfile_Close_File(the_file);
	}

	return 1;
}
