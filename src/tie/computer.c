#include "tie/computer.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/computer_task.h"
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
 * Static data — resource name tables
 * ====================================================================== */

typedef struct ComputerResourceSpec {
	const char* archive;
	const char* delta[5];
	const char* anim[3];
	const char* palette[4];
	uint8_t palette_slot[4];
	const char* awards_archive;
	const char* award_actor[14];
	const char* award_palette[4];
	const char* awards1_archive;
	const char* awards1[6];
	const char* awards2_archive;
	const char* awards2[6];
} ComputerResourceSpec;

/* DATA: TIE95 COMPUTER_Do_Computer_Dialog 0x82AD0. */
static const ComputerResourceSpec computer_vga_resources = {
	/* archive */ "computer.lfd",
	/* delta */ { "newtarm", "newtslev", "newtscrn", "computr", "medlbak" },
	/* anim */ { "button01", "compicns", "tattoo" },
	/* palette */ { "newtarm", "newtscrn", "computr", "medlbak" },
	/* palette_slot */ { 0, 2, 3, 4 },
	/* awards_archive */ "awards.lfd",
	/* award_actor */
	{ "trnships", "medls-a", "strsnbrs", "sunrisea", "aniplnts", "star-1", "star-2", "coins", "starsbak",
	  "mdl-bak1", "mdl-bak2", "ani1-sta", "trnships", "raptrhed" },
	/* award_palette */ { "trnshps", "medlbak", "brnzpal", "slvrpal" },
	/* awards1_archive */ "awards1.lfd",
	/* awards1 */ { "mislboat", "a-medals", "amed-obj", "medpal", "coins", "comptat2" },
	/* awards2_archive */ "awards2.lfd",
	/* awards2 */ { "mislboat", "b-medals", "bmed-obj", "medpal", "coins", "comptat2" },
};

/* DATA: TIE98 COMPUTER_Do_Computer_Dialog 0x40BA40. The option and
 * joystick DELTs are intentionally absent because those controls are disabled. */
static const ComputerResourceSpec computer_svga_resources = {
	/* archive */ "computer.lfd",
	/* delta */ { "arm", "sleeve", "newtscrn", "compface", "medlbak" },
	/* anim */ { "buttons", "compicon", "tattoo" },
	/* palette */ { "arm", "newtscrn", "computr", "medlbak" },
	/* palette_slot */ { 0, 1, 2, 3 },
	/* awards_archive */ "awardshr.lfd",
	/* award_actor */
	{ "trnships", "medls-a", "strsnbrs", "sunrisea", "aniplnts", "star-1", "star-2", "coins", "starsbak",
	  "mdl-bak1", "mdl-bak2", "ani1-sta", "trnships", "raptrhed" },
	/* award_palette */ { "trnshps", "medlbak", "brnzpal", "slvrpal" },
	/* awards1_archive */ "awards1h.lfd",
	/* awards1 */ { "mislboat", "a-medals", "amed-obj", "medpal", "coins", "tattoo" },
	/* awards2_archive */ "awards2h.lfd",
	/* awards2 */ { "mislboat", "b-medals", "bmed-obj", "medpal", "coins", "tattoo" },
};

/* ======================================================================
 * Static data — layout rects (decoded from binary, Rect = {top,left,bottom,right})
 * ====================================================================== */

/* Tab click regions: Medals, Record, Backup, Options */
static const Rect computer_vga_mode_rect[4] = {
	{ 146, 160, 188, 190 },
	{ 146, 194, 188, 224 },
	{ 146, 227, 188, 257 },
	{ 146, 260, 188, 290 },
};

/* TIE98 tab click regions: Medals, Record, Backup, Options */
static const Rect computer_svga_mode_rect[4] = {
	{ 350, 318, 453, 380 },
	{ 350, 388, 453, 445 },
	{ 350, 451, 453, 511 },
	{ 350, 517, 453, 577 },
};

/* Preferences panel rects (18 entries):
 * [0]=title, [1]=music label, [2]=music on/off, [3]=music gauge,
 * [4]=sound label, [5]=sound on/off, [6]=sound gauge,
 * [7]=speech label, [8]=speech on/off, [9]=speech gauge,
 * [10]=transitions label, [11]=transitions on/off,
 * [12]=subtitles label, [13]=subtitles on/off,
 * [14]=difficulty label, [15]=difficulty selector,
 * [16]=flight res label, [17]=flight res selector */
static const Rect computer_vga_pref_rect[18] = {
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
};

/* TIE98 has five additional rectangles after these. They belong to the
 * disabled joystick, brightness, and texture-resolution controls. */
static const Rect computer_svga_pref_rect[18] = {
	{ 26, 242, 60, 478 },   { 67, 190, 93, 280 },   { 67, 286, 93, 386 },   { 67, 392, 93, 530 },
	{ 98, 190, 124, 280 },  { 98, 286, 124, 386 },  { 98, 392, 124, 530 },  { 129, 190, 155, 280 },
	{ 129, 286, 155, 386 }, { 129, 392, 155, 530 }, { 160, 190, 186, 264 }, { 160, 270, 186, 354 },
	{ 160, 360, 186, 434 }, { 160, 440, 186, 530 }, { 191, 190, 217, 344 }, { 191, 350, 217, 530 },
	{ 222, 190, 248, 434 }, { 253, 190, 279, 530 },
};

/* Backup panel rects (8 entries):
 * [0]=title, [1]=auto-backup label, [2]=auto-backup on/off,
 * [3]=auto-restore label, [4]=auto-restore on/off,
 * [5]=backup button, [6]=restore button, [7]=info panel */
static const Rect computer_vga_backup_rect[8] = {
	{ 9, 121, 23, 239 },  /* title */
	{ 26, 95, 38, 212 },  /* auto-backup label */
	{ 26, 215, 38, 265 }, /* auto-backup on/off */
	{ 42, 95, 54, 212 },  /* auto-restore label */
	{ 42, 215, 54, 265 }, /* auto-restore on/off */
	{ 58, 95, 72, 175 },  /* backup button */
	{ 58, 185, 72, 265 }, /* restore button */
	{ 76, 95, 117, 265 }, /* info panel */
};

static const Rect computer_svga_backup_rect[8] = {
	{ 26, 242, 64, 478 },   { 67, 190, 96, 424 },   { 67, 430, 96, 530 },   { 105, 190, 134, 424 },
	{ 105, 430, 134, 530 }, { 144, 190, 177, 350 }, { 144, 370, 177, 530 }, { 187, 190, 285, 530 },
};

typedef struct ComputerMedalSpec {
	int16_t ship_x, ship_y;
	int16_t base_x, base_y, base_flip_x;
	int16_t expansion1_flip_x, expansion2_flip_x;
	Rect clip, tall_clip, text_rect;
	int16_t page2_xy[4][2];
	int16_t page3_xy[5][2];
	int16_t expansion1_overlay_y[3];
	int16_t pip_status_x[2], pip_bonus_x[2];
	int16_t pip_y, pip_step;
} ComputerMedalSpec;

static const ComputerMedalSpec computer_vga_medal = {
	/* ship_x */ 34,
	/* ship_y */ -50,
	/* base_x */ 22,
	/* base_y */ -28,
	/* base_flip_x */ 14,
	/* expansion1_flip_x */ -5,
	/* expansion2_flip_x */ -11,
	/* clip */ { 77, 167, 102, 195 },
	/* tall_clip */ { 77, 165, 108, 197 },
	/* text_rect */ { 7, 92, 117, 273 },
	/* page2_xy */ { { 16, -31 }, { -11, 22 }, { -10, 22 }, { 22, -28 } },
	/* page3_xy */ { { 16, -31 }, { -37, 31 }, { -14, 29 }, { 23, -25 }, { 22, -28 } },
	/* expansion1_overlay_y */ { -28, -28, -28 },
	/* pip_status_x */ { -34, -54 },
	/* pip_bonus_x */ { 76, 96 },
	/* pip_y */ -80,
	/* pip_step */ 16,
};

static const ComputerMedalSpec computer_svga_medal = {
	/* ship_x */ 68,
	/* ship_y */ -120,
	/* base_x */ 44,
	/* base_y */ -67,
	/* base_flip_x */ 28,
	/* expansion1_flip_x */ -9,
	/* expansion2_flip_x */ -21,
	/* clip */ { 185, 334, 245, 390 },
	/* tall_clip */ { 185, 334, 259, 390 },
	/* text_rect */ { 17, 178, 290, 547 },
	/* page2_xy */ { { 32, -74 }, { -22, 53 }, { -20, 53 }, { 44, -67 } },
	/* page3_xy */ { { 32, -74 }, { -74, 75 }, { -26, 70 }, { 46, -60 }, { 44, -67 } },
	/* expansion1_overlay_y */ { -67, -67, -57 },
	/* pip_status_x */ { -68, -108 },
	/* pip_bonus_x */ { 152, 192 },
	/* pip_y */ -192,
	/* pip_step */ 36,
};

typedef struct ComputerSpec {
	LandruSurfaceSet surface_set;
	const ComputerResourceSpec* resources;
	const ComputerMedalSpec* medal;
	const Rect* mode_rect;
	const Rect* pref_rect;
	const Rect* backup_rect;
	Rect next_rect, last_rect, exit_rect, accept_rect;
	Rect open_options_rect;
	Rect info_clip, medal_hover[2], secret_rect;
	Rect confirm_rect, confirm_yes_rect, confirm_no_rect;
	int16_t width, height;
	int16_t actor_x[4], actor_y[4];
	int16_t tab_x[4], tab_y;
	int16_t content_font, tab_font, toggle_label_font;
	int16_t action_button_state[2][2];
	int16_t tab_icon_state[4][2];
	int16_t page_height, line_height, heading_extra, kills_page_rows;
	int16_t gauge_step, gauge_left_inset;
	int16_t exit_hover_x, exit_hover_y;
	int16_t exit_step, exit_limit, scroll_x_divisor, scroll_y_multiplier;
	int16_t confirm_text_x, confirm_text_y, confirm_mouse_x, confirm_mouse_y;
} ComputerSpec;

/* PORT: immutable VGA/SVGA dispatch. Layout values are from the recovered
 * TIE95 callbacks at 0x83000-0x86B60 and TIE98 callbacks at 0x40C220-0x41088F. */
static const ComputerSpec computer_specs[] = {
	{
		/* surface_set */ LANDRU_SURFACE_VGA,
		/* resources */ &computer_vga_resources,
		/* medal */ &computer_vga_medal,
		/* mode_rect */ computer_vga_mode_rect,
		/* pref_rect */ computer_vga_pref_rect,
		/* backup_rect */ computer_vga_backup_rect,
		/* next_rect */ { 124, 190, 136, 266 },
		/* last_rect */ { 124, 104, 136, 180 },
		/* exit_rect */ { 169, 96, 194, 152 },
		/* accept_rect */ { 142, 96, 168, 152 },
		/* open_options_rect */ { 116, 95, 128, 265 },
		/* info_clip */ { 7, 86, 117, 273 },
		/* medal_hover */ { { 37, 92, 103, 132 }, { 37, 223, 103, 263 } },
		/* secret_rect */ { 80, 60, 104, 260 },
		/* confirm_rect */ { 0, 0, 22, 160 },
		/* confirm_yes_rect */ { 3, 80, 19, 116 },
		/* confirm_no_rect */ { 3, 120, 19, 156 },
		/* width */ 320,
		/* height */ 200,
		/* actor_x */ { 0, 0, 41, 0 },
		/* actor_y */ { 0, 0, 0, 0 },
		/* tab_x */ { 161, 195, 228, 261 },
		/* tab_y */ 182,
		/* content_font */ 0,
		/* tab_font */ 1,
		/* toggle_label_font */ 0,
		/* action_button_state */ { { 1, 3 }, { 0, 2 } },
		/* tab_icon_state */ { { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } },
		/* page_height */ 110,
		/* line_height */ 10,
		/* heading_extra */ 0,
		/* kills_page_rows */ 11,
		/* gauge_step */ 4,
		/* gauge_left_inset */ 1,
		/* exit_hover_x */ 76,
		/* exit_hover_y */ 88,
		/* exit_step */ 60,
		/* exit_limit */ 120,
		/* scroll_x_divisor */ 4,
		/* scroll_y_multiplier */ 1,
		/* confirm_text_x */ 4,
		/* confirm_text_y */ 7,
		/* confirm_mouse_x */ 189,
		/* confirm_mouse_y */ 104,
	},
	{
		/* surface_set */ LANDRU_SURFACE_SVGA,
		/* resources */ &computer_svga_resources,
		/* medal */ &computer_svga_medal,
		/* mode_rect */ computer_svga_mode_rect,
		/* pref_rect */ computer_svga_pref_rect,
		/* backup_rect */ computer_svga_backup_rect,
		/* next_rect */ { 298, 380, 326, 532 },
		/* last_rect */ { 298, 208, 326, 360 },
		/* exit_rect */ { 400, 225, 466, 302 },
		/* accept_rect */ { 350, 196, 402, 302 },
		/* open_options_rect */ { 253, 190, 279, 530 },
		/* info_clip */ { 17, 172, 290, 547 },
		/* medal_hover */ { { 100, 196, 238, 266 }, { 100, 456, 238, 523 } },
		/* secret_rect */ { 192, 120, 250, 520 },
		/* confirm_rect */ { 0, 0, 53, 340 },
		/* confirm_yes_rect */ { 7, 180, 46, 252 },
		/* confirm_no_rect */ { 7, 260, 46, 332 },
		/* width */ 640,
		/* height */ 480,
		/* actor_x */ { 0, 0, 41, 0 },
		/* actor_y */ { 0, 0, 0, 0 },
		/* tab_x */ { 320, 390, 453, 519 },
		/* tab_y */ 439,
		/* content_font */ 2,
		/* tab_font */ 3,
		/* toggle_label_font */ 3,
		/* action_button_state */ { { 2, 3 }, { 0, 1 } },
		/* tab_icon_state */ { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 } },
		/* page_height */ 273,
		/* line_height */ 0,
		/* heading_extra */ 2,
		/* kills_page_rows */ 14,
		/* gauge_step */ 8,
		/* gauge_left_inset */ 3,
		/* exit_hover_x */ 119,
		/* exit_hover_y */ 130,
		/* exit_step */ 60,
		/* exit_limit */ 120,
		/* scroll_x_divisor */ 2,
		/* scroll_y_multiplier */ 2,
		/* confirm_text_x */ 8,
		/* confirm_text_y */ 17,
		/* confirm_mouse_x */ 365,
		/* confirm_mouse_y */ 240,
	},
};

static const ComputerSpec* active_spec = &computer_specs[0];

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

/* ======================================================================
 * Init_Computer_Medal — scan pilot record, build medal page arrays
 * ====================================================================== */

// FUNCTION: TIE95 0x869A4
// FUNCTION: TIE98 0x410650
static int16_t Init_Computer_Medal(void) {
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
 * Find_Backup_Pilot_Info — read backup pilot rank/score from .tfr file
 * ====================================================================== */

// FUNCTION: TIE95 0x86B60
// FUNCTION: TIE98 0x4107D0
static int16_t Find_Backup_Pilot_Info(void) {
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

/* ======================================================================
 * Set_Computer_Medal_Palette — crossfade to medal-specific palette
 * ====================================================================== */

// FUNCTION: TIE95 0x84DD8
// FUNCTION: TIE98 0x40DFB0
static int16_t Set_Computer_Medal_Palette(void) {
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
 * Small drawing helpers
 * ====================================================================== */

// FUNCTION: TIE95 0x863C8
// FUNCTION: TIE98 0x40FCC0
static void draw_Computer_On_Off(Rect* r, int16_t on) {
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

	xfont_Print_Centered_Text(textext_Get_Text(txtCompGaugeOn), &tr1, 14, active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompGaugeOff), &tr2, 14, active_spec->content_font);
}

// FUNCTION: TIE95 0x86498
// FUNCTION: TIE98 0x40FDB0
static void draw_Computer_Level(Rect* r, int16_t state) {
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

	xfont_Print_Centered_Text(textext_Get_Text(txtCompLevelEasy), &tr1, 14, active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompLevelMed), &tr2, 14, active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompLevelHard), &tr3, 14, active_spec->content_font);
}

// FUNCTION: TIE95 0x865F4
// FUNCTION: TIE98 0x40FF90
static void draw_Computer_Gauge(Rect* r, int16_t amount) {
	Rect tr;
	int16_t i;

	xrect_Copy_Rect(&tr, r);
	xrect_Inset_Rect(&tr, 1, 1);
	xpaint_Paint_Clipped_Rect(&tr, 16);
	xrect_Inset_Rect(&tr, 1, 1);
	tr.left += active_spec->gauge_left_inset;
	tr.right = tr.left + active_spec->gauge_step - 1;

	for (i = 0; i < amount; i++) {
		xpaint_Frame_Clipped_Rect(&tr, 14);
		xrect_Offset_Rect(&tr, active_spec->gauge_step, 0);
	}
}

/* ======================================================================
 * Confirmation dialogs
 * ====================================================================== */

// FUNCTION: TIE95 0x86910
// FUNCTION: TIE98 0x4105A0
static void idraw_Exit(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;

	xstyle_Style_Paint_Border(r, 0);
	xfont_Print_Clipped_Text(textext_Get_Text(input->var1), r->left + active_spec->confirm_text_x,
							 r->top + active_spec->confirm_text_y, active_spec->content_font, 15);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

// FUNCTION: TIE95 0x86968
// FUNCTION: TIE98 0x410610
static void iuser_Exit(Input* input, int32_t time) {
	(void)time;

	if (input->id == 1 && xinpattr_Get_Input_Selected(input))
		xdialog_Set_Dialog_Exit(1);
	else if (input->id == 2 && xinpattr_Get_Input_Selected(input))
		xdialog_Set_Dialog_Exit(2);
}

// FUNCTION: TIE95 0x867F8
// FUNCTION: TIE98 0x410350
static Input* Build_Exit(int16_t id) {
	Rect r;
	Input* the_input;

	xrect_Copy_Rect(&r, (Rect*)&active_spec->confirm_rect);
	the_input = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(the_input, idraw_Exit);
	xinpattr_Set_Input_Allign(the_input, 1, 1);
	xinpattr_Start_Input(the_input);

	/* Store text_id in var1 for the draw callback */
	the_input->var1 = id;

	strcpy(comp_exit_str[0], textext_Get_Text(txtCompExitYes));
	strcpy(comp_exit_str[1], textext_Get_Text(txtCompExitNo));

	xrect_Copy_Rect(&r, (Rect*)&active_spec->confirm_yes_rect);
	xbtnpush_Alloc_Button(the_input, &r, 0, iuser_Exit, comp_exit_str[0], 1);

	xrect_Copy_Rect(&r, (Rect*)&active_spec->confirm_no_rect);
	xbtnpush_Alloc_Button(the_input, &r, 0, iuser_Exit, comp_exit_str[1], 2);

	return the_input;
}

/* In the modern build the confirmation cannot block inside an input
 * callback. The runtime runs the confirmation as a sub-dialog and then
 * re-enters the calling callback, where the stored result is returned. */

// FUNCTION: TIE95 0x86738
// FUNCTION: TIE98 0x4101F0
static int16_t Check_Backup_Pilot(void) {
	Input* the_input;
	int16_t retval;

#ifdef TIE_MODERN
	if (TieComputer_TakeConfirmResult(&retval))
		return retval != 2;
#endif
	the_input = Build_Exit(txtCompBackupPilot);
	xio_Set_Mouse_Position(active_spec->confirm_mouse_x, active_spec->confirm_mouse_y);
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
static int16_t Check_Restore_Pilot(void) {
	Input* the_input;
	int16_t retval;

#ifdef TIE_MODERN
	if (TieComputer_TakeConfirmResult(&retval))
		return retval != 2;
#endif
	the_input = Build_Exit(txtCompRestorePilot);
	xio_Set_Mouse_Position(active_spec->confirm_mouse_x, active_spec->confirm_mouse_y);
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

/* TIE98's Check_Exit_To_DOS (0x4102E0) also services the window-close
 * request, which this body does not model. */
// FUNCTION: TIE95 0x867B8
static int16_t Exit_To_DOS(void) {
	Input* the_input;
	int16_t retval;

#ifdef TIE_MODERN
	if (TieComputer_TakeConfirmResult(&retval))
		return retval != 2;
#endif
	the_input = Build_Exit(txtCompExitDOS);
	xio_Set_Mouse_Position(active_spec->confirm_mouse_x, active_spec->confirm_mouse_y);
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

/* ======================================================================
 * Preferences panel — update + draw
 * ====================================================================== */

// FUNCTION: TIE95 0x85974
// FUNCTION: TIE98 0x40EC80
static void update_Computer_Prefs(int16_t x, int16_t y) {
	const Rect* pref_rect = active_spec->pref_rect;
	int16_t refresh = 0;

	if (xrect_Point_In_Rect((Rect*)&pref_rect[2], x, y)) {
		options_gbl.music_active = (x < pref_rect[2].left + (pref_rect[2].right - pref_rect[2].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[3], x, y)) {
		options_gbl.music_volume = (x - pref_rect[3].left) / active_spec->gauge_step;
		if (options_gbl.music_volume > 16)
			options_gbl.music_volume = 16;
		/* TIE98 couples each volume gauge to its enable flag; TIE95 does not. */
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA)
			options_gbl.music_active = options_gbl.music_volume != 0;
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[5], x, y)) {
		options_gbl.sound_active = (x < pref_rect[5].left + (pref_rect[5].right - pref_rect[5].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[6], x, y)) {
		options_gbl.sound_volume = (x - pref_rect[6].left) / active_spec->gauge_step;
		if (options_gbl.sound_volume > 16)
			options_gbl.sound_volume = 16;
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA)
			options_gbl.sound_active = options_gbl.sound_volume != 0;
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[8], x, y)) {
		options_gbl.speech_active = (x < pref_rect[8].left + (pref_rect[8].right - pref_rect[8].left) / 2);
		refresh = 1;
	}
	if (xrect_Point_In_Rect((Rect*)&pref_rect[9], x, y)) {
		options_gbl.speech_volume = (x - pref_rect[9].left) / active_spec->gauge_step;
		if (options_gbl.speech_volume > 16)
			options_gbl.speech_volume = 16;
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA)
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
static void draw_Computer_Prefs(Rect* r, Rect* clip_r) {
	const Rect* pref_rect = active_spec->pref_rect;
	int16_t i;
	(void)r;
	(void)clip_r;

	for (i = 0; i < 16; i++)
		xpaint_Frame_Clipped_Rect((Rect*)&pref_rect[i], 38);

	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefTitle), (Rect*)&pref_rect[0], 15,
							  active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefMusic), (Rect*)&pref_rect[1], 15,
							  active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefSound), (Rect*)&pref_rect[4], 15,
							  active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefSpeech), (Rect*)&pref_rect[7], 15,
							  active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefTrans), (Rect*)&pref_rect[10], 15,
							  active_spec->toggle_label_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefSub), (Rect*)&pref_rect[12], 15,
							  active_spec->toggle_label_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompPrefGame), (Rect*)&pref_rect[14], 15,
							  active_spec->content_font);
	draw_Computer_On_Off((Rect*)&pref_rect[2], options_gbl.music_active);
	draw_Computer_On_Off((Rect*)&pref_rect[5], options_gbl.sound_active);
	draw_Computer_On_Off((Rect*)&pref_rect[8], options_gbl.speech_active);
	draw_Computer_On_Off((Rect*)&pref_rect[11], options_gbl.transition_active);
	draw_Computer_On_Off((Rect*)&pref_rect[13], options_gbl.text_active);
	draw_Computer_Level((Rect*)&pref_rect[15], options_gbl.game_level);
	draw_Computer_Gauge((Rect*)&pref_rect[3], options_gbl.music_volume);
	draw_Computer_Gauge((Rect*)&pref_rect[6], options_gbl.sound_volume);
	draw_Computer_Gauge((Rect*)&pref_rect[9], options_gbl.speech_volume);
}

/* ======================================================================
 * Backup panel — update + draw + user
 * ====================================================================== */

// FUNCTION: TIE95 0x85E64
// FUNCTION: TIE98 0x40F700
static void xupdate_Computer_Backup(int16_t x, int16_t y) {
	const Rect* backup_rect = active_spec->backup_rect;
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
static void iuser_Computer_Backup(Input* input, int32_t time) {
	(void)time;

	if (input->id < 3 || input->id > 4)
		return;
	if (!xinpattr_Get_Input_Selected(input))
		return;

#ifdef TIE_MODERN
	TieComputer_BeginConfirm(input);
#endif
	if (input->id == 3) {
		if (!Check_Backup_Pilot())
			return;
		shipext_Backup_Pilot();
		backup_pilot_rank = pilot_record.rank;
		backup_pilot_points = pilot_record.score;
	} else {
		if (!Check_Restore_Pilot())
			return;
		shipext_Restore_Pilot();
		Init_Computer_Medal();
		pilot_info_page = 0;
		restore_pilot = 1;
		xinpattr_Show_Input(backup_input);
	}
	xview_Refresh_View();
}

// FUNCTION: TIE95 0x85FB4
// FUNCTION: TIE98 0x40F840
static void xdraw_Computer_Backup(Rect* r, Rect* clip_r) {
	const Rect* backup_rect = active_spec->backup_rect;
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
	xfont_Print_Centered_Text(textext_Get_Text(txtCompBackTitle), (Rect*)&backup_rect[0], 15,
							  active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompBackAutoBackup), (Rect*)&backup_rect[1], color,
							  active_spec->content_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtCompBackAutoRestore), (Rect*)&backup_rect[3], color,
							  active_spec->content_font);

	/* Current pilot info */
	xrect_Copy_Rect(&tr, (Rect*)&backup_rect[7]);
	tr.top += active_spec->surface_set == LANDRU_SURFACE_SVGA ? 5 : 2;
	tr.bottom = tr.top + (active_spec->line_height ? active_spec->line_height - 2
												   : xfont_Get_FontID_Height(active_spec->content_font));

	textext_Copy_Text(name, pilot_record.rank + txtCompRankCadet);
	strcat(name, " ");
#ifdef TIE_MODERN
	shipext_Get_Pilot_Name(pilot_name, sizeof(pilot_name));
#else
	shipext_Get_Pilot_Name(pilot_name);
#endif
	strcat(name, pilot_name);
	xfont_Print_Centered_Text(name, &tr, color, active_spec->content_font);

	xrect_Offset_Rect(&tr, 0,
					  active_spec->line_height ? 9 : xfont_Get_FontID_Height(active_spec->content_font));
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
	xfont_Print_Centered_Text(name, &tr, color, active_spec->content_font);

	/* Backup pilot info */
	if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
		xrect_Copy_Rect(&tr, (Rect*)&backup_rect[7]);
		tr.top += (tr.bottom - tr.top) / 2 + 5;
		tr.bottom = tr.top + xfont_Get_FontID_Height(active_spec->content_font);
	} else {
		xrect_Offset_Rect(&tr, 0, 11);
	}
	textext_Copy_Text(name, txtCompNameLast);
	strcat(name, " ");
	textext_Cat_Text(name, backup_pilot_rank + txtCompRankCadet);
	xfont_Print_Centered_Text(name, &tr, 90, active_spec->content_font);

	xrect_Offset_Rect(&tr, 0,
					  active_spec->line_height ? 9 : xfont_Get_FontID_Height(active_spec->content_font));
	snprintf(points, sizeof(points), " %ld ", (long)backup_pilot_points);
	textext_Copy_Text(name, txtCompNameWith);
	strcat(name, points);
	textext_Cat_Text(name, txtCompNamePoints);
	xfont_Print_Centered_Text(name, &tr, 90, active_spec->content_font);

	draw_Computer_On_Off((Rect*)&backup_rect[2], options_gbl.auto_backup);
	draw_Computer_On_Off((Rect*)&backup_rect[4], options_gbl.auto_restore);
}

/* ======================================================================
 * Record info drawing — header, combat, battle, kills panels
 * ====================================================================== */

// FUNCTION: TIE95 0x84E64
// FUNCTION: TIE98 0x40E060
static void Draw_Computer_Header_Info(Rect* r, int16_t color, int16_t back_color) {
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
	xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
	xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
	xrect_Offset_Rect(r, 0,
					  (active_spec->line_height ? active_spec->line_height
												: xfont_Get_FontID_Height(active_spec->content_font)) +
						  active_spec->heading_extra);

	/* Score + Skill */
	textext_Copy_Text(str1, txtCompInfoScore);
	snprintf(str2, sizeof(str2), " %ld    ", (long)pilot_record.score);
	strcat(str1, str2);
	textext_Cat_Text(str1, txtCompInfoSkill);
	snprintf(str2, sizeof(str2), " %u", (unsigned)pilot_record.avg_score);
	strcat(str1, str2);
	xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
	xrect_Offset_Rect(r, 0,
					  (active_spec->line_height ? active_spec->line_height
												: xfont_Get_FontID_Height(active_spec->content_font)));

	/* Laser accuracy */
	if (pilot_record.laser_hits)
		val = (uint32_t)(100 * pilot_record.laser_hits) / pilot_record.laser_total;
	else
		val = 0;
	textext_Copy_Text(str2, txtCompInfoLaser);
	snprintf(str1, sizeof(str1), str2, pilot_record.laser_hits, pilot_record.laser_total, (int16_t)val);
	xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
	xrect_Offset_Rect(r, 0,
					  (active_spec->line_height ? active_spec->line_height
												: xfont_Get_FontID_Height(active_spec->content_font)));

	/* Warhead accuracy */
	if (pilot_record.warhead_hits > pilot_record.warhead_total)
		pilot_record.warhead_total = pilot_record.warhead_hits;
	if (pilot_record.warhead_hits)
		val = 100 * pilot_record.warhead_hits / pilot_record.warhead_total;
	else
		val = 0;
	textext_Copy_Text(str2, txtCompInfoRocket);
	snprintf(str1, sizeof(str1), str2, pilot_record.warhead_hits, pilot_record.warhead_total, (int16_t)val);
	xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
	xrect_Offset_Rect(r, 0,
					  (active_spec->line_height ? active_spec->line_height
												: xfont_Get_FontID_Height(active_spec->content_font)));

	/* Total kills */
	textext_Copy_Text(str2, txtCompInfoKills);
	snprintf(str1, sizeof(str1), str2, pilot_record.total_kills);
	xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
	xrect_Offset_Rect(r, 0,
					  (active_spec->line_height ? active_spec->line_height
												: xfont_Get_FontID_Height(active_spec->content_font)));

	/* Total captures */
	textext_Copy_Text(str2, txtCompInfoCaptures);
	snprintf(str1, sizeof(str1), str2, pilot_record.total_captures);
	xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
	xrect_Offset_Rect(r, 0,
					  (active_spec->line_height ? active_spec->line_height
												: xfont_Get_FontID_Height(active_spec->content_font)));

	/* Craft lost */
	textext_Copy_Text(str2, txtCompInfoCraftLost);
	snprintf(str1, sizeof(str1), str2, pilot_record.ejection_count);
	xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
	xrect_Offset_Rect(&page, 0, active_spec->page_height);
	xrect_Copy_Rect(r, &page);
}

// FUNCTION: TIE95 0x851E8
// FUNCTION: TIE98 0x40E470
static void Draw_Computer_Combat_Info(Rect* r, int16_t color, int16_t back_color) {
	char str1[80];
	char str2[40];
	int16_t ship_info[12];
	int16_t num_ships = 0;
	int16_t count;
	int16_t i, j;

	for (i = 0; i < 12; i++) {
		count = 0;
		if (shipext_Is_Ship(i)) {
			if (pilot_record.train_score[i])
				count++;
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
		Rect page;

		if (!ship_info[i])
			continue;
		xrect_Copy_Rect(&page, r);

		shipext_Get_Ship_Name(str1, i, 0, 0);
		xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
		xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
		xrect_Offset_Rect(r, 0,
						  (active_spec->line_height ? active_spec->line_height
													: xfont_Get_FontID_Height(active_spec->content_font)) +
							  active_spec->heading_extra);

		if (pilot_record.train_score[i]) {
			if (pilot_record.train_max_level[i] < 4)
				textext_Copy_Text(str2, txtCompInfoTrainIncomplete);
			else
				textext_Copy_Text(str2, txtCompInfoTrainComplete);
			snprintf(str1, sizeof(str1), str2, pilot_record.train_score[i]);
			xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
			xrect_Offset_Rect(r, 0,
							  (active_spec->line_height
								   ? active_spec->line_height
								   : xfont_Get_FontID_Height(active_spec->content_font)));
		}

		for (j = 0; j < 8; j++) {
			if (pilot_record.combat_score[i][j]) {
				if (pilot_record.combat_complete[i][j])
					textext_Copy_Text(str2, txtCompInfoCombatComplete);
				else
					textext_Copy_Text(str2, txtCompInfoCombatIncomplete);
				snprintf(str1, sizeof(str1), str2, j + 1, pilot_record.combat_score[i][j]);
				xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
				xrect_Offset_Rect(r, 0,
								  (active_spec->line_height
									   ? active_spec->line_height
									   : xfont_Get_FontID_Height(active_spec->content_font)));
			}
		}

		xrect_Offset_Rect(&page, 0, active_spec->page_height);
		xrect_Copy_Rect(r, &page);
	}
}

// FUNCTION: TIE95 0x854A0
// FUNCTION: TIE98 0x40E770
static void Draw_Computer_Battle_Info(Rect* r, int16_t color, int16_t back_color) {
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
		xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
		xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
		xrect_Offset_Rect(r, 0,
						  (active_spec->line_height ? active_spec->line_height
													: xfont_Get_FontID_Height(active_spec->content_font)) +
							  active_spec->heading_extra);

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
			xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
			total_score += pilot_record.tour_score[i][j];
			xrect_Offset_Rect(r, 0,
							  (active_spec->line_height
								   ? active_spec->line_height
								   : xfont_Get_FontID_Height(active_spec->content_font)));
		}

		if (total_score && max_missions > 1) {
			xpaint_Horiz_Clipped_Line(r->left + 10, r->top - 1, r->right - r->left - 20, back_color);
			xrect_Offset_Rect(r, 0, active_spec->heading_extra);
			textext_Copy_Text(str2, txtCompTotalScore);
			snprintf(str1, sizeof(str1), str2, (long)total_score);
			xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
			xrect_Offset_Rect(r, 0,
							  (active_spec->line_height
								   ? active_spec->line_height
								   : xfont_Get_FontID_Height(active_spec->content_font)));
		}

		xrect_Offset_Rect(&page, 0, active_spec->page_height);
		xrect_Copy_Rect(r, &page);
	}
}

// FUNCTION: TIE95 0x857C0
// FUNCTION: TIE98 0x40EA90
static void Draw_Computer_Kills_Info(Rect* r, int16_t color, int16_t back_color) {
	char str1[80];
	Rect page;
	int16_t num_craft = 0;
	int16_t craft_count = 0;
	int16_t count = 0;
	int16_t i;

	xrect_Copy_Rect(&page, r);

	for (i = 0; i < NUM_SPEC; i++) {
		if (pilot_record.kills_by_ship_type[i])
			num_craft++;
	}

	if (!num_craft)
		return;

	for (i = 0; i < NUM_SPEC && craft_count < num_craft; i++) {
		if (!(count % active_spec->kills_page_rows)) {
			if (count) {
				xrect_Offset_Rect(&page, 0, active_spec->page_height);
				xrect_Copy_Rect(r, &page);
			}
			textext_Copy_Text(str1, txtCompInfoVictories);
			xfont_Print_Centered_Text(str1, r, color, active_spec->content_font);
			xpaint_Horiz_Clipped_Line(r->left + 10, r->bottom - 1, r->right - r->left - 20, back_color);
			xrect_Offset_Rect(r, 0,
							  (active_spec->line_height
								   ? active_spec->line_height
								   : xfont_Get_FontID_Height(active_spec->content_font)) +
								  active_spec->heading_extra);
			count++;
		}

		if (pilot_record.kills_by_ship_type[i]) {
			textext_Get_Ship_Text(str1, i);
			xfont_Print_Clipped_Text(str1, r->left + 8, r->top + 1, active_spec->content_font, color);
			snprintf(str1, sizeof(str1), "%d", pilot_record.kills_by_ship_type[i]);
			xfont_Print_Clipped_Text(str1, r->right - 30, r->top + 1, active_spec->content_font, color);
			xrect_Offset_Rect(r, 0,
							  (active_spec->line_height
								   ? active_spec->line_height
								   : xfont_Get_FontID_Height(active_spec->content_font)));
			count++;
			craft_count++;
		}
	}

	xrect_Offset_Rect(&page, 0, active_spec->page_height);
	xrect_Copy_Rect(r, &page);
}

/* ======================================================================
 * Info page dispatch
 * ====================================================================== */

// FUNCTION: TIE95 0x84CFC
// FUNCTION: TIE98 0x40DEB0
static void xdraw_Computer_Info(Rect* r, Rect* clip_r) {
	Rect clip_tr, tr;
	int16_t color = 15;
	int16_t back_color = 38;
	int16_t start_top;

	(void)r;

	xrect_Copy_Rect(&clip_tr, (Rect*)&active_spec->info_clip);
	xcanvas_Set_Drawing_Canvas_Clip(&clip_tr);
	xrect_Copy_Rect(&tr, &clip_tr);
	tr.bottom = tr.top + (active_spec->line_height ? active_spec->line_height
												   : xfont_Get_FontID_Height(active_spec->content_font));
	xrect_Offset_Rect(&tr, 0, -active_spec->page_height * pilot_info_page);
	start_top = tr.top;

	Draw_Computer_Header_Info(&tr, color, back_color);
	Draw_Computer_Combat_Info(&tr, color, back_color);
	Draw_Computer_Battle_Info(&tr, color, back_color);
	Draw_Computer_Kills_Info(&tr, color, back_color);

	pilot_info_num_pages = (tr.top - start_top) / active_spec->page_height;
	xcanvas_Set_Drawing_Canvas_Clip(clip_r);
}

/* ======================================================================
 * Medal/Info page navigation buttons
 * ====================================================================== */

// FUNCTION: TIE95 0x84B84
// FUNCTION: TIE98 0x40DD20
static void iuser_Computer_Info(Input* input, int32_t time) {
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
		Set_Computer_Medal_Palette();
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

/* Retail TIE95 keeps this callback but never installs it. */
// FUNCTION: TIE95 0x83DD4
static void idraw_Computer_Medal(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	PushButton* btn = (PushButton*)input;

	if (!refresh)
		return;

	if (btn->pressed)
		xpaint_Paint_Clipped_Rect(r, 38);
	else
		xpaint_Paint_Clipped_Rect(r, 16);
	xpaint_Frame_Clipped_Rect(r, 38);

	if (input->id)
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoLast), r, 14, active_spec->content_font);
	else
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoNext), r, 14, active_spec->content_font);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

// FUNCTION: TIE95 0x84C88
// FUNCTION: TIE98 0x40DE30
static void idraw_Computer_Info(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	PushButton* btn = (PushButton*)input;

	if (!refresh)
		return;

	if (btn->pressed)
		xpaint_Paint_Clipped_Rect(r, 38);
	else
		xpaint_Paint_Clipped_Rect(r, 16);
	xpaint_Frame_Clipped_Rect(r, 38);

	if (input->id)
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoLast), r, 14, active_spec->content_font);
	else
		xfont_Print_Centered_Text(textext_Get_Text(txtCompInfoNext), r, 14, active_spec->content_font);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * xdraw_Computer_Medal — medal display rendering (complex)
 * ====================================================================== */

// FUNCTION: TIE95 0x83E7C
// FUNCTION: TIE98 0x40D220
static void xdraw_Computer_Medal(Rect* r, Rect* clip_r) {
	const ComputerMedalSpec* medal = active_spec->medal;
	char name1[40];
	char string[40];
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

	if (type == 0) {
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
			if (pilot_record.train_max_level[6] < 4) {
				xactor_Set_Actor_State(medal_actor2[k], 1, 0);
			} else {
				xactor_Set_Actor_State(medal_actor2[k], 0, 0);
			}
			xactanim_Draw_Anim_Actor(medal_actor2[k], r, clip_r, r->left, r->top, 1);
		}
	} else if (type == 1) {
		/* Ship combat medal */
		if (id < 7)
			shipext_Get_Ship_Name(name1, id, 0, 0);
		textext_Copy_Text(string, status + txtCompBronze);

		if (id < 6) {
			xactor_Set_Actor_State(medal_actor[7], id, 0);
			xactanim_Draw_Anim_Actor(medal_actor[7], r, clip_r, r->left + medal->ship_x,
									 r->top + medal->ship_y, 1);
		} else {
			if (shipext_Is_Mission_Disk1()) {
				xactor_Set_Actor_State(medal_actor2[3], 0, 0);
				xactanim_Draw_Anim_Actor(medal_actor2[3], r, clip_r, r->left + medal->ship_x,
										 r->top + medal->ship_y, 1);
			}
			if (shipext_Is_Mission_Disk2()) {
				xactor_Set_Actor_State(medal_actor2[8], 0, 0);
				xactanim_Draw_Anim_Actor(medal_actor2[8], r, clip_r, r->left + medal->ship_x,
										 r->top + medal->ship_y, 1);
			}
		}
	} else if (type == 2) {
		/* Battle completion medal */
		if (pilot_medal_text == 1) {
			textext_Copy_Text(name1, txtCompSecGoal1);
			textext_Copy_Text(fmt, txtCompSecGoal2);
			snprintf(string, sizeof(string), fmt, pilot_medal_status[pilot_medal_page],
					 pilot_medal_id[pilot_medal_page] + 1);
		} else if (pilot_medal_text == 2) {
			textext_Copy_Text(name1, txtCompBonGoal1);
			textext_Copy_Text(fmt, txtCompBonGoal2);
			snprintf(string, sizeof(string), fmt, pilot_medal_bonus_status[pilot_medal_page],
					 pilot_medal_id[pilot_medal_page] + 1);
		} else {
			/* Battle name and status */
			textext_Copy_Text(string, txtCompInfoBattle);
			snprintf(name1, sizeof(name1), string, id + 1);
			if (pilot_record.battle_status[id] == 3) {
				if (id < 7)
					textext_Copy_Text(string, id + txtCompMedal1);
				else if (id < 13)
					textext_Copy_Text(string, id + txtComp2Medal8 - 7);
				else
					string[0] = '\0';
			} else {
				string[0] = '\0';
			}
		}

		/* Draw the battle medal actor */
		x = medal->base_x;
		y = medal->base_y;

		/* Determine medal page/variant */
		if (id < 2) {
			page = id ? 6 : 0;
		} else if (id == 2) {
			page = 3;
		} else if (id == 3) {
			page = 2;
		} else if (id < 6) {
			page = id;
		} else if (id == 6) {
			page = 1;
		} else if (id <= 12) {
			page = id;
		} else {
			page = id;
		}

		if (pilot_record.battle_status[id] == 3) {
			if (page <= 6) {
				/* Base game battle medals */
				if (page != 4) {
					xactor_Set_Actor_Flip(medal_actor[1], 1, 0);
					xactor_Set_Actor_State(medal_actor[1], 3 * page, 0);
					xactanim_Draw_Anim_Actor(medal_actor[1], r, clip_r, medal->base_flip_x, y, 1);
				}
				xactor_Set_Actor_Flip(medal_actor[1], 0, 0);
				xactor_Set_Actor_State(medal_actor[1], 3 * page, 0);
				xactanim_Draw_Anim_Actor(medal_actor[1], r, clip_r, x, y, 1);

				/* Special overlays for specific pages */
				if (page == 2) {
					xrect_Copy_Rect(&tr, (Rect*)&medal->clip);
					xrect_Clip_Rect(&tr, clip_r);
					xcanvas_Set_Drawing_Canvas_Clip(&tr);
					xactor_Set_Actor_State(medal_actor[3], 4, 0);
					xactor_Set_Actor_State(medal_actor[4], 0, 0);
					xactdelt_Draw_Delta_Actor(medal_actor[8], r, &tr, medal->page2_xy[0][0],
											  medal->page2_xy[0][1], 1);
					xactanim_Draw_Anim_Actor(medal_actor[4], r, &tr, medal->page2_xy[1][0],
											 medal->page2_xy[1][1], 1);
					xactanim_Draw_Anim_Actor(medal_actor[3], r, &tr, medal->page2_xy[2][0],
											 medal->page2_xy[2][1], 1);
					xactdelt_Draw_Delta_Actor(medal_actor[10], r, &tr, medal->page2_xy[3][0],
											  medal->page2_xy[3][1], 1);
				}
				if (page == 3) {
					xrect_Copy_Rect(&tr, (Rect*)&medal->tall_clip);
					xrect_Clip_Rect(&tr, clip_r);
					xcanvas_Set_Drawing_Canvas_Clip(&tr);
					xactdelt_Draw_Delta_Actor(medal_actor[8], r, &tr, medal->page3_xy[0][0],
											  medal->page3_xy[0][1], 1);
					xactor_Set_Actor_State(medal_actor[4], 1, 0);
					xactanim_Draw_Anim_Actor(medal_actor[4], r, &tr, medal->page3_xy[1][0],
											 medal->page3_xy[1][1], 1);
					xactor_Set_Actor_State(medal_actor[4], 0, 0);
					xactanim_Draw_Anim_Actor(medal_actor[4], r, &tr, medal->page3_xy[2][0],
											 medal->page3_xy[2][1], 1);
					xactdelt_Draw_Delta_Actor(medal_actor[11], r, &tr, medal->page3_xy[3][0],
											  medal->page3_xy[3][1], 1);
					xactdelt_Draw_Delta_Actor(medal_actor[9], r, &tr, medal->page3_xy[4][0],
											  medal->page3_xy[4][1], 1);
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
					xactanim_Draw_Anim_Actor(medal_actor2[1], r, clip_r, medal->expansion1_flip_x, y, 1);
					xactor_Set_Actor_Flip(medal_actor2[1], 0, 0);
					xactor_Set_Actor_State(medal_actor2[1], page, 0);
					xactanim_Draw_Anim_Actor(medal_actor2[1], r, clip_r, x, y, 1);

					if (page == 0) {
						xrect_Copy_Rect(&tr, (Rect*)&medal->clip);
						xrect_Clip_Rect(&tr, clip_r);
						xactor_Set_Actor_State(medal_actor2[2], 0, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x, medal->expansion1_overlay_y[0],
												 1);
						xactor_Set_Actor_State(medal_actor2[2], 2, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x, medal->expansion1_overlay_y[0],
												 1);
					} else if (page == 1) {
						xrect_Copy_Rect(&tr, (Rect*)&medal->clip);
						xrect_Clip_Rect(&tr, clip_r);
						xactor_Set_Actor_State(medal_actor2[2], 3, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x, medal->expansion1_overlay_y[1],
												 1);
					} else if (page == 2) {
						xrect_Copy_Rect(&tr, (Rect*)&medal->clip);
						xrect_Clip_Rect(&tr, clip_r);
						xactor_Set_Actor_State(medal_actor2[2], 1, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[2], r, &tr, x, medal->expansion1_overlay_y[2],
												 1);
					}
				}
			} else if (page <= 12) {
				/* Expansion pack 2 medals */
				if (shipext_Is_Mission_Disk2()) {
					page -= 10;
					xactor_Set_Actor_Flip(medal_actor2[6], 1, 0);
					xactor_Set_Actor_State(medal_actor2[6], page, 0);
					xactanim_Draw_Anim_Actor(medal_actor2[6], r, clip_r, medal->expansion2_flip_x, y, 1);
					xactor_Set_Actor_Flip(medal_actor2[6], 0, 0);
					xactor_Set_Actor_State(medal_actor2[6], page, 0);
					xactanim_Draw_Anim_Actor(medal_actor2[6], r, clip_r, x, y, 1);

					if (page == 2) {
						xrect_Copy_Rect(&tr, (Rect*)&medal->clip);
						xrect_Clip_Rect(&tr, clip_r);
						xactor_Set_Actor_State(medal_actor2[7], 0, 0);
						xactanim_Draw_Anim_Actor(medal_actor2[7], r, &tr, x, y, 1);
					}
				}
			}
		}

		xcanvas_Set_Drawing_Canvas_Clip(clip_r);

		/* Draw mission completion pips */
		x = medal->pip_status_x[0];
		y = medal->pip_y;
		xactor_Set_Actor_State(medal_actor[2], 2, 0);
		for (i = 0; i < status; i++) {
			if (i >= 4)
				xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, medal->pip_status_x[1],
										 y + medal->pip_step * (i - 4), 1);
			else
				xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, x, y + medal->pip_step * i, 1);
		}

		x = medal->pip_bonus_x[0];
		y = medal->pip_y;
		xactor_Set_Actor_State(medal_actor[2], 0, 0);
		for (i = 0; i < bonus; i++) {
			if (i >= 4)
				xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, medal->pip_bonus_x[1],
										 y + medal->pip_step * (i - 4), 1);
			else
				xactanim_Draw_Anim_Actor(medal_actor[2], r, &tr, x, y + medal->pip_step * i, 1);
		}
	}

	/* Print medal name and description */
	if (name1[0]) {
		int16_t text_height;
		int16_t text_step;

		xrect_Copy_Rect(&tr, (Rect*)&medal->text_rect);
		/* TIE95 uses fixed 8/9-pixel spacing; TIE98 advances by font height. */
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
			text_height = xfont_Get_FontID_Height(active_spec->content_font);
			text_step = text_height;
		} else {
			text_height = 8;
			text_step = 9;
		}
		tr.bottom = tr.top + text_height;
		xfont_Enable_FontID_Shadow(active_spec->content_font);
		xfont_Print_Centered_Text(name1, &tr, 15, active_spec->content_font);
		if (string[0]) {
			xrect_Offset_Rect(&tr, 0, text_step);
			xfont_Print_Centered_Text(string, &tr, 15, active_spec->content_font);
		}
		xfont_Disable_FontID_Shadow(active_spec->content_font);
	}
}

/* ======================================================================
 * iupdate_Computer — main dialog update callback
 * ====================================================================== */

// FUNCTION: TIE95 0x83248
// FUNCTION: TIE98 0x40C4A0
static int16_t iupdate_Computer(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
								int16_t x, int16_t y) {
	const Rect* computer_mode_rect = active_spec->mode_rect;
	char name[16];
	int16_t new_mode;
	int16_t i;
	uint8_t button;

	(void)clip_r;

	if (key) {
		/* ESC → select cancel button */
		if (key == 27) {
			xinpattr_Selected_Input(cancel_input);
			return 1;
		}
		/* TIE98 consumes other keys while its options page is active. */
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA && computer_mode == COMP_MODE_OPTIONS)
			return 1;
		return 0;
	}

	/* Mouse handling */
	button = left ? left : right;

	if (!button) {
		/* Hover: check if mouse is in the left panel for exit animation trigger */
		if (x <= active_spec->exit_hover_x && y >= active_spec->exit_hover_y)
			input->var2 = 1;

		/* Medal hover text detection */
		new_mode = 0;
		if (computer_mode == COMP_MODE_MEDALS && pilot_medal_type[pilot_medal_page] == 2) {
			if (pilot_medal_status[pilot_medal_page]) {
				if (xrect_Point_In_Rect((Rect*)&active_spec->medal_hover[0], x, y))
					new_mode = 1;
			}
			if (pilot_medal_bonus_status[pilot_medal_page]) {
				if (xrect_Point_In_Rect((Rect*)&active_spec->medal_hover[1], x, y))
					new_mode = 2;
			}
		}
		if (new_mode != pilot_medal_text) {
			pilot_medal_text = new_mode;
			xview_Refresh_View();
		}
		return 1;
	}

	if (button != MOUSE_DOWN)
		return 1;

	/* Click: check tab switching */
	new_mode = computer_mode;
	for (i = 0; i < 4; i++) {
		if (xrect_Point_In_Rect((Rect*)&computer_mode_rect[i], x + r->left, y + r->top)) {
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

	if (new_mode == computer_mode) {
		/* Same mode: dispatch to mode-specific click handler */
		switch (computer_mode) {
			case COMP_MODE_BACKUP:
				xupdate_Computer_Backup(x + r->left, y + r->top);
				break;
			case COMP_MODE_OPTIONS:
				update_Computer_Prefs(x + r->left, y + r->top);
				break;
			default:
				break;
		}
	} else {
		/* Tab switch: hide old mode's widgets */
		switch (computer_mode) {
			case COMP_MODE_MEDALS:
				xpal_Screen_To_Dest_Palette(0, 0, 255);
				for (i = 0; i < 4; i++)
					xpal_Set_Dest_Palette(computer_palettes[active_spec->resources->palette_slot[i]]);
				xfade_Start_Full_Fade(FADE_WIPE_INSTANT, FADE_COLOR_CROSSFADE, 1, 0, 0);
				xinpattr_Hide_Input(next_info_input);
				xinpattr_Hide_Input(last_info_input);
				break;
			case COMP_MODE_RECORD:
				xinpattr_Hide_Input(next_info_input);
				xinpattr_Hide_Input(last_info_input);
				break;
			case COMP_MODE_BACKUP:
				xinpattr_Hide_Input(backup_input);
				xinpattr_Hide_Input(restore_input);
				break;
#ifdef TIE_MODERN
			case COMP_MODE_OPTIONS:
				TieComputer_ShowOptionsButton(false);
				break;
#endif
			default:
				break;
		}

		computer_mode = new_mode;

		/* Show new mode's widgets */
		switch (new_mode) {
			case COMP_MODE_MEDALS:
				Set_Computer_Medal_Palette();
				xinpattr_Show_Input(next_info_input);
				xinpattr_Show_Input(last_info_input);
				break;
			case COMP_MODE_RECORD:
				xinpattr_Show_Input(next_info_input);
				xinpattr_Show_Input(last_info_input);
				break;
			case COMP_MODE_BACKUP:
				if (!pilot_record.exit_status)
					xinpattr_Show_Input(backup_input);
				xinpattr_Show_Input(restore_input);
				break;
#ifdef TIE_MODERN
			case COMP_MODE_OPTIONS:
				TieComputer_ShowOptionsButton(true);
				break;
#endif
			default:
				break;
		}

		xview_Refresh_View();
	}

	return 1;
}

/* ======================================================================
 * iuser_Computer — main dialog user callback
 * ====================================================================== */

// FUNCTION: TIE95 0x835A4
// FUNCTION: TIE98 0x40C950
static void iuser_Computer(Input* input, int32_t time) {
	Palette* screen_pal;
	int16_t i;

	if (input->id == 0) {
		/* Parent dialog: exit animation */
		if (input->var2) {
			if (input->var1 < active_spec->exit_limit) {
				input->var1 += active_spec->exit_step;
				xview_Refresh_View();
			}
			input->var2 = 0;
		} else if (input->var1) {
			input->var1 -= active_spec->exit_step;
			xview_Refresh_View();
		}
		return;
	}

	if (input->id > 2)
		return;

	/* OK/Cancel buttons: restore palette on button-down with id 2 (cancel) */
	if (time == 1 && input->id == 2) {
		screen_pal = xpal_Get_Screen_Palette();
		xpal_Copy_Palette(screen_pal, computer_palette, 0, 32, 0);
		xpal_Put_Screen_Pal_Range(0, 32);
		for (i = 0; i < 4; i++)
			xpal_Set_Screen_Palette(computer_palettes[active_spec->resources->palette_slot[i]]);
	}

	if (xinpattr_Get_Input_Selected(input)) {
		if (input->id == 1) {
#ifdef TIE_MODERN
			TieComputer_BeginConfirm(input);
#endif
			if (Exit_To_DOS()) {
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
}

/* ======================================================================
 * idraw_Computer — main dialog draw callback
 * ====================================================================== */

// FUNCTION: TIE95 0x836E0
// FUNCTION: TIE98 0x40CB70
static void idraw_Computer(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	const Rect* backup_rect = active_spec->backup_rect;
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
					scroll_x = -(input->var1 / active_spec->scroll_x_divisor);
					scroll_y = input->var1 * active_spec->scroll_y_multiplier;
					/* TIE98 vertical offset is phase / 2 + 2 * phase. */
					if (active_spec->surface_set == LANDRU_SURFACE_SVGA)
						scroll_y += input->var1 / 2;
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
				if (i == 2 && (computer_mode || pilot_medal_type[pilot_medal_page])) {
					xactdelt_Draw_Delta_Actor(computer_actors[i + 2], r, clip_r,
											  scroll_x + active_spec->actor_x[i] - 41,
											  scroll_y + active_spec->actor_y[i], 1);
				} else {
					xactdelt_Draw_Delta_Actor(computer_actors[i], r, clip_r,
											  scroll_x + active_spec->actor_x[i],
											  scroll_y + active_spec->actor_y[i], 1);
				}
			}

			/* Draw tab indicators */
			for (i = 0; i < 4; i++) {
				xactor_Set_Actor_State(computer_actors[6], active_spec->tab_icon_state[i][computer_mode == i],
									   0);
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
			xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeOptions), active_spec->tab_x[3],
									 active_spec->tab_y, active_spec->tab_font, tab_color);

			/* Medals tab */
			if (name[0] && pilot_medal_num_pages) {
				tab_color = (computer_mode == COMP_MODE_MEDALS) ? 14 : 15;
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeMedals), active_spec->tab_x[0],
										 active_spec->tab_y, active_spec->tab_font, tab_color);
			} else {
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeMedals), active_spec->tab_x[0],
										 active_spec->tab_y, active_spec->tab_font, 20);
			}

			/* Record + Backup tabs */
			if (name[0]) {
				tab_color = (computer_mode == COMP_MODE_RECORD) ? 14 : 15;
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeRecord), active_spec->tab_x[1],
										 active_spec->tab_y, active_spec->tab_font, tab_color);

				tab_color = (computer_mode == COMP_MODE_BACKUP) ? 14 : 15;
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeBackup), active_spec->tab_x[2],
										 active_spec->tab_y, active_spec->tab_font, tab_color);
			} else {
				/* Greyed out (no pilot loaded) */
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeRecord), active_spec->tab_x[1],
										 active_spec->tab_y, active_spec->tab_font, 20);
				xfont_Print_Clipped_Text(textext_Get_Text(txtCompModeBackup), active_spec->tab_x[2],
										 active_spec->tab_y, active_spec->tab_font, 20);
			}
			break;

		case 1:
			/* OK button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			xactor_Set_Actor_State(computer_actors[5], active_spec->action_button_state[0][btn->pressed], 0);
			xactanim_Draw_Anim_Actor(computer_actors[5], r, clip_r, 0, 0, 1);
			break;

		case 2:
			/* Cancel button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			xactor_Set_Actor_State(computer_actors[5], active_spec->action_button_state[1][btn->pressed], 0);
			xactanim_Draw_Anim_Actor(computer_actors[5], r, clip_r, 0, 0, 1);
			break;

		case 3:
			/* Backup button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			xpaint_Paint_Clipped_Rect(r, btn->pressed ? 38 : 16);
			xfont_Print_Centered_Text(textext_Get_Text(txtCompBackBackup), (Rect*)&backup_rect[5], 14,
									  active_spec->content_font);
			if (xinpattr_Is_Input_Dirty(input))
				xdirty_Dirty_Rect(clip_r);
			break;

		case 4:
			/* Restore button */
			if (!computer_display)
				break;
			btn = (PushButton*)input;
			xpaint_Paint_Clipped_Rect(r, btn->pressed ? 38 : 16);
			xfont_Print_Centered_Text(textext_Get_Text(txtCompBackRestore), (Rect*)&backup_rect[6], 14,
									  active_spec->content_font);
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
			xfont_Print_Centered_Text("OpenTIE Options", r, 14, active_spec->content_font);
			if (xinpattr_Is_Input_Dirty(input))
				xdirty_Dirty_Rect(clip_r);
			break;
#endif
	}

	/* Mode-specific content (only for parent, id=0) */
	if (input->id == 0 && computer_display) {
		switch (computer_mode) {
			case COMP_MODE_MEDALS:
				xdraw_Computer_Medal(r, clip_r);
				break;
			case COMP_MODE_RECORD:
				xdraw_Computer_Info(r, clip_r);
				break;
			case COMP_MODE_BACKUP:
				xdraw_Computer_Backup(r, clip_r);
				break;
			case COMP_MODE_OPTIONS:
				draw_Computer_Prefs(r, clip_r);
				break;
		}

		/* Secret Order rank popup during exit scroll */
		if (input->var1 && pilot_record.secret_order_rank) {
			int16_t line_step = active_spec->surface_set == LANDRU_SURFACE_SVGA ? 20 : 10;

			xrect_Copy_Rect(&tr, (Rect*)&active_spec->secret_rect);
			xpaint_Paint_Clipped_Rect(&tr, 1);
			xpaint_Frame_Clipped_Rect(&tr, 16);
			xfont_Enable_FontID_Shadow(0);

			tr.top += active_spec->surface_set == LANDRU_SURFACE_SVGA ? 10 : 2;
			tr.bottom = tr.top + line_step;
			if (pilot_record.secret_order_rank > 6)
				xfont_Print_Centered_Text(
					textext_Get_Text(pilot_record.secret_order_rank + txtComp2Secret7 - 7), &tr, 15,
					active_spec->content_font);
			else
				xfont_Print_Centered_Text(
					textext_Get_Text(pilot_record.secret_order_rank + txtCompSecret1 - 1), &tr, 15,
					active_spec->content_font);

			tr.top = tr.bottom;
			tr.bottom = tr.top + line_step;
			xfont_Print_Centered_Text(textext_Get_Text(txtCompSecretOrder), &tr, 15,
									  active_spec->content_font);
			xfont_Disable_FontID_Shadow(0);
		}

		if (xinpattr_Is_Input_Dirty(input))
			xdirty_Dirty_Rect(clip_r);
	}
}

/* ======================================================================
 * Build_Computer_Dialog — construct the widget tree
 * ====================================================================== */

// FUNCTION: TIE95 0x83000
// FUNCTION: TIE98 0x40C220
static Input* Build_Computer_Dialog(void) {
	const Rect* backup_rect = active_spec->backup_rect;
	Rect r;
	Input *parent, *inp;

	xrect_Set_Rect(&r, 0, 0, active_spec->width, active_spec->height);
	parent = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(parent, idraw_Computer);
	xinpattr_Set_Input_User_Function(parent, iuser_Computer);
	xinpattr_Set_Input_Update_Function(parent, iupdate_Computer);
	xinpattr_Show_Input(parent);
	parent->mouseUsage = allInput;
	parent->id = 0;

	/* Next Page button (for medals/record) */
	xrect_Copy_Rect(&r, (Rect*)&active_spec->next_rect);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, iuser_Computer_Info, NULL, 0);
	xinpattr_Set_Input_Draw_Function(inp, idraw_Computer_Info);
	xinpattr_Hide_Input(inp);
	next_info_input = inp;

	/* Last Page button */
	xrect_Copy_Rect(&r, (Rect*)&active_spec->last_rect);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, iuser_Computer_Info, NULL, 1);
	xinpattr_Set_Input_Draw_Function(inp, idraw_Computer_Info);
	xinpattr_Hide_Input(inp);
	last_info_input = inp;

	/* Backup button */
	xrect_Copy_Rect(&r, (Rect*)&backup_rect[5]);
	xrect_Inset_Rect(&r, 1, 1);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, iuser_Computer_Backup, NULL, 3);
	xinpattr_Set_Input_Draw_Function(inp, idraw_Computer);
	xinpattr_Hide_Input(inp);
	backup_input = inp;

	/* Restore button */
	xrect_Copy_Rect(&r, (Rect*)&backup_rect[6]);
	xrect_Inset_Rect(&r, 1, 1);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, iuser_Computer_Backup, NULL, 4);
	xinpattr_Set_Input_Draw_Function(inp, idraw_Computer);
	xinpattr_Hide_Input(inp);
	restore_input = inp;

#ifdef TIE_MODERN
	/* Modern options button; COMPUTER starts on the Options tab. */
	xrect_Copy_Rect(&r, (Rect*)&active_spec->open_options_rect);
	TieComputer_AllocOptionsButton(parent, &r, idraw_Computer);
#endif

	/* OK button (id=1, Exit to DOS) */
	xrect_Copy_Rect(&r, (Rect*)&active_spec->exit_rect);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, iuser_Computer, NULL, 1);
	xinpattr_Set_Input_Draw_Function(inp, idraw_Computer);

	/* Cancel button (id=2, Accept/Save) */
	xrect_Copy_Rect(&r, (Rect*)&active_spec->accept_rect);
	inp = (Input*)xbtnpush_Alloc_Button(parent, &r, 0, iuser_Computer, NULL, 2);
	xinpattr_Set_Input_Draw_Function(inp, idraw_Computer);
	cancel_input = inp;

	return parent;
}

// FUNCTION: TIE95 0x82AD0
// FUNCTION: TIE98 0x40BA40
int16_t computer_Do_Computer_Dialog(void) {
	Input* the_dialog;
	int16_t retval = 0;
#ifdef TIE_MODERN
	ComputerDialogState* continuation = landru_task_top();
	the_dialog = continuation->the_dialog;
	if (!continuation->started && !continuation->failed)
#endif
	{
#ifdef TIE_MODERN
		const ComputerSpec* spec = &computer_specs[continuation->tie98 ? 1 : 0];
#elif defined(TIE98)
		const ComputerSpec* spec = &computer_specs[1];
#else
		const ComputerSpec* spec = &computer_specs[0];
#endif
		const ComputerResourceSpec* resources = spec->resources;
		ResFile* res_file;
		Palette* src_palette;
		Rect r;
		int16_t i;

		xsound_Pause_Sounds();
		xio_Clear_Key();
		active_spec = spec;
#ifdef TIE_MODERN
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
			continuation->saved_surface_set = xsurface_Get_Surface_Set();
			xview_Get_View_Frame(0, &continuation->saved_view_frame);
			xview_Get_Full_View_Clip_Frame(&continuation->saved_view_clip);
			(void)xsurface_Select_Surface_Set(active_spec->surface_set);
			xrect_Set_Rect(&r, 0, 0, active_spec->width, active_spec->height);
			xview_Set_View_Frame(0, &r);
			xview_Set_Full_View_Clip_Frame(&r);
		}

#endif

		restore_pilot = 0;
		computer_display = 1;
		computer_mode = COMP_MODE_OPTIONS;
		pilot_info_page = 0;
		pilot_info_num_pages = 1;
		Init_Computer_Medal();
		Find_Backup_Pilot_Info();
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

		res_file = shellext_Open_Empire_Resource(resources->archive);
#ifdef TIE_MODERN
		if (!res_file) {
			TieComputer_Fail(NULL, resources->archive);
			return 0;
		}
#endif
		xrect_Set_Rect(&r, 0, 0, active_spec->width, active_spec->height);

		for (i = 0; i < 5; i++) {
			computer_actors[i] = xactdelt_Res_Delta_Actor(resources->delta[i], &r, 0, 0, 0);
#ifdef TIE_MODERN
			if (!computer_actors[i]) {
				TieComputer_Fail(res_file, resources->delta[i]);
				return 0;
			}
#endif
			xactor_Set_Actor_Time(computer_actors[i], 0, 0);
		}
		for (i = 0; i < 3; i++) {
			computer_actors[i + 5] = xactanim_Res_Anim_Actor(resources->anim[i], &r, 0, 0, 0);
#ifdef TIE_MODERN
			if (!computer_actors[i + 5]) {
				TieComputer_Fail(res_file, resources->anim[i]);
				return 0;
			}
#endif
			xactor_Set_Actor_Time(computer_actors[i + 5], 0, 0);
		}
		for (i = 0; i < 4; i++) {
			uint8_t slot = resources->palette_slot[i];
			computer_palettes[slot] = xpal_Res_Palette(resources->palette[i]);
#ifdef TIE_MODERN
			if (!computer_palettes[slot]) {
				TieComputer_Fail(res_file, resources->palette[i]);
				return 0;
			}
#endif
		}
		xpal_Set_Screen_RGB(0, 255, 0, 0, 0);
		xres_Close_Resource(res_file);

		res_file = shellext_Open_Empire_Resource(resources->awards_archive);
#ifdef TIE_MODERN
		if (!res_file) {
			TieComputer_Fail(NULL, resources->awards_archive);
			return 0;
		}
#endif
		for (i = 0; i < 14; i++) {
			if (i >= 8)
				medal_actor[i] = xactdelt_Res_Delta_Actor(resources->award_actor[i], &r, 0, 0, 0);
			else
				medal_actor[i] = xactanim_Res_Anim_Actor(resources->award_actor[i], &r, 0, 0, 0);
#ifdef TIE_MODERN
			if (!medal_actor[i]) {
				TieComputer_Fail(res_file, resources->award_actor[i]);
				return 0;
			}
#endif
		}
		for (i = 0; i < 4; i++) {
			medal_palette[i] = xpal_Res_Palette(resources->award_palette[i]);
#ifdef TIE_MODERN
			if (!medal_palette[i]) {
				TieComputer_Fail(res_file, resources->award_palette[i]);
				return 0;
			}
#endif
		}
		xres_Close_Resource(res_file);

		if (shipext_Is_Mission_Disk1()) {
			res_file = shellext_Open_Empire_Resource(resources->awards1_archive);
#ifdef TIE_MODERN
			if (!res_file) {
				TieComputer_Fail(NULL, resources->awards1_archive);
				return 0;
			}
#endif
			medal_actor2[0] = xactanim_Res_Anim_Actor(resources->awards1[0], &r, 0, 0, 0);
			medal_actor2[1] = xactanim_Res_Anim_Actor(resources->awards1[1], &r, 0, 0, 0);
			medal_actor2[2] = xactanim_Res_Anim_Actor(resources->awards1[2], &r, 0, 0, 0);
			medal_actor2[3] = xactanim_Res_Anim_Actor(resources->awards1[4], &r, 0, 0, 0);
			medal_actor2[4] = xactanim_Res_Anim_Actor(resources->awards1[5], &r, 0, 0, 0);
#ifdef TIE_MODERN
			for (i = 0; i < 5; ++i)
#ifdef TIE_MODERN
				if (!medal_actor2[i]) {
					TieComputer_Fail(res_file, resources->awards1[i < 3 ? i : i + 1]);
					return 0;
				}
#endif
#endif
			xres_Close_Resource(res_file);
		}

		if (shipext_Is_Mission_Disk2()) {
			res_file = shellext_Open_Empire_Resource(resources->awards2_archive);
#ifdef TIE_MODERN
			if (!res_file) {
				TieComputer_Fail(NULL, resources->awards2_archive);
				return 0;
			}
#endif
			medal_actor2[5] = xactanim_Res_Anim_Actor(resources->awards2[0], &r, 0, 0, 0);
			medal_actor2[6] = xactanim_Res_Anim_Actor(resources->awards2[1], &r, 0, 0, 0);
			medal_actor2[7] = xactanim_Res_Anim_Actor(resources->awards2[2], &r, 0, 0, 0);
			medal_actor2[8] = xactanim_Res_Anim_Actor(resources->awards2[4], &r, 0, 0, 0);
			medal_actor2[9] = xactanim_Res_Anim_Actor(resources->awards2[5], &r, 0, 0, 0);
#ifdef TIE_MODERN
			for (i = 5; i < 10; ++i)
#ifdef TIE_MODERN
				if (!medal_actor2[i]) {
					TieComputer_Fail(res_file, resources->awards2[i < 8 ? i - 5 : i - 4]);
					return 0;
				}
#endif
#endif
			xres_Close_Resource(res_file);
		}

		the_dialog = Build_Computer_Dialog();
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
		if (computer_specs[continuation->tie98 ? 1 : 0].surface_set == LANDRU_SURFACE_SVGA) {
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
