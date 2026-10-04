#include "tie/register.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/register_task.h"
#endif
#ifdef TIE_MODERN
#include "tie_runtime/storage/pilot_storage.h"
#endif
#include "tie/edition.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/presentation/pilot_name.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#include "tie_runtime/storage/storage.h"

#include "landru/actanim.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/btnpush.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/dialog.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/filedir.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/fourcc.h"
#include "landru/inpattr.h"
#include "landru/inpcall.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/style.h"
#include "landru/surface.h"
#include "landru/timer.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original TU calls the library strcpy(), strcat() and strlen() rather than the inline forms. */
#ifdef __WATCOMC__
#pragma function(strcpy, strcat, strlen)
#endif

#ifndef TIE_MODERN
/* CD install state recorded by the shell at startup. */
extern uint8_t install_drive_ok; /* CD drive letter, 0 when absent */
extern char cd_check_filename[];

void shellext_Delete_Install_File(const char* filename);

/* Filedir drive helpers the modern Landru port does not provide. */
void xfiledir_Set_Current_Drive(Directory* dir, int drive);
void xfiledir_Restore_Default_Dir(void);
#endif

/* ---- Static data ---- */

/* Copy-protection cipher pad: 29 questions × 3 symbol indices.
 * Each triplet indexes into the symbol animation actor states. */
// GLOBAL: TIE95 0xD1206
static const int16_t reg_cp[87] = { 6,  2, 8,  0, 10, 9, 3, 4,  7, 11, 2, 5, 1, 9, 11, 7, 9,  5,
									10, 8, 2,  4, 0,  6, 5, 6,  1, 2,  7, 0, 8, 4, 11, 9, 11, 10,
									6,  5, 3,  0, 2,  1, 3, 11, 8, 11, 5, 7, 1, 0, 6,  7, 8,  10,
									10, 5, 4,  4, 2,  9, 5, 8,  3, 2,  4, 5, 8, 7, 1,  9, 0,  7,
									6,  1, 10, 0, 3,  6, 3, 5,  9, 11, 1, 8, 1, 6, 10 };

/* Copy-protection answer table: 29 ship names (16 bytes each, null-padded). */
// GLOBAL: TIE95 0xD12B4
static const char reg_cp_name[29][16] = {
	"ardent",         "audacity",    "colossus",  "courageous", "devastation", "emperor",
	"emperor's will", "formidable",  "furious",   "glory",      "glorious",    "harpago",
	"harpax",         "illustrious", "imperator", "implacable", "indomitable", "inflexible",
	"lightning",      "magnificent", "majestic",  "monarch",    "monitor",     "protector",
	"renown",         "resolution",  "thunderer", "triumph",    "vanguard"
};

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xD11FC
// GLOBAL: TIE98 0x4EAB88
static int16_t pilot_active; /* logical index of selected pilot (-1 = none) */
// GLOBAL: TIE95 0xD11FE
// GLOBAL: TIE98 0x589784
static int16_t pilot_offset; /* first visible pilot in list */
// GLOBAL: TIE95 0xD1200
// GLOBAL: TIE98 0x589788
static int16_t num_pilots; /* count of valid (non-deleted) pilots */
// GLOBAL: TIE95 0xD1202
// GLOBAL: TIE98 0x58978C
static int16_t num_loaded_pilots; /* total register_directory entries */

// GLOBAL: TIE95 0xF6578
static char reg_prot_name[72]; /* protect dialog button label buffers */
// GLOBAL: TIE95 0xF65C0
// GLOBAL: TIE98 0x588F28
static uint8_t cur_pilot[PILOTRECORD_DISK_SIZE]; /* temp buffer for reading .tfr files */
// GLOBAL: TIE95 0xF6D58
// GLOBAL: TIE98 0x588E88
static char reg_btn_name[96]; /* button label scratch buffers */
// GLOBAL: TIE95 0xF6DB8
static Actor* symbols; /* symbol animation actor for protect dialog */
// GLOBAL: TIE95 0xF6DF8
// GLOBAL: TIE98 0x588EF8
static FastPilotRecord shell_pilot; /* cached FPR for the active pilot */
// GLOBAL: TIE95 0xF6DBC
static Input* protect_btns_arr[3]; /* protect dialog child buttons */
// GLOBAL: TIE95 0xF6DC8
static Input* protect_parent;
// GLOBAL: TIE95 0xF6DD0
// GLOBAL: TIE98 0x588EEC
static Input* pilot_info;
// GLOBAL: TIE95 0xF6DD4
// GLOBAL: TIE98 0x5896E0
static Input* reg_parent;
// GLOBAL: TIE95 0xF6DD8
// GLOBAL: TIE98 0x588EF4
static Input* pilot_delete;
// GLOBAL: TIE95 0xF6DDC
// GLOBAL: TIE98 0x589748
static RegStringButton* pilot_name_input;
// GLOBAL: TIE95 0xF6E0C
// GLOBAL: TIE98 0x58976C
static Actor* reg_troop;
// GLOBAL: TIE95 0xF6E10
// GLOBAL: TIE98 0x589734
static Film* register_film;
// GLOBAL: TIE95 0xF6DE0
// GLOBAL: TIE98 0x589770
static Actor* reg_button[3]; /* film callback actor cache */
// GLOBAL: TIE95 0xF6E14
// GLOBAL: TIE98 0x58977C
static Actor* reg_door;
// GLOBAL: TIE95 0xF6DEC
// GLOBAL: TIE98 0x588EE8
static Input* door_input;
// GLOBAL: TIE95 0xF6DF0
// GLOBAL: TIE98 0x5896E4
static Input* pilot_list;
// GLOBAL: TIE95 0xF6DF4
// GLOBAL: TIE98 0x588E70
static Actor* reg_bak;
// GLOBAL: TIE95 0xF6E18
// GLOBAL: TIE98 0x5896B0
Directory register_directory;
// GLOBAL: TIE95 0xF6E46
static int16_t protect_state[3];
// GLOBAL: TIE95 0xF6E4C
static int16_t protect_chosen;
// GLOBAL: TIE95 0xF6E4E
static int16_t protect_count;
// GLOBAL: TIE95 0xF6E50
static int16_t protect_index;
// GLOBAL: TIE95 0xF6E54
// GLOBAL: TIE98 0x589730
static int16_t num_pages;
/* PORT: native allocation replaces the Landru handle. */
#ifdef TIE_MODERN
// GLOBAL: TIE95 0xF6E52
// GLOBAL: TIE98 0x588E74
void* register_fast_pilot_record;
#else
// GLOBAL: TIE95 0xF6E52
// GLOBAL: TIE98 0x588E74
LandruHandle register_fast_pilot_record;
#endif
// GLOBAL: TIE95 0xF6E58
// GLOBAL: TIE98 0x58974C
static int16_t cur_page;
// GLOBAL: TIE95 0xF6E56
// GLOBAL: TIE98 0x589764
static int16_t pilot_delete_status;

/* ================================================================
 * Forward declarations
 * ================================================================ */

static void register_idraw_Reg_String_Button(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static int16_t register_iupdate_Reg_String_Button(Input* input, Rect* bounds, Rect* clip, int16_t key,
												  uint8_t left, uint8_t right, int16_t mouse_x,
												  int16_t mouse_y);
static Input* register_Build_Delete_Dialog(void);
static int16_t register_Do_Delete_Dialog(void);
static int16_t register_iupdate_Delete_Input(Input* input, Rect* bounds, Rect* clip, int16_t key,
											 uint8_t left, uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void register_iuser_Delete_Input(Input* input, int32_t time);
static void register_idraw_Delete_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static int16_t register_Find_Reg_Dir_Name(Directory* dir, char* dst, int16_t idx);
static Input* register_Build_Protect_Dialog(void);
static int16_t register_iupdate_Protect_Input(Input* input, Rect* bounds, Rect* clip, int16_t key,
											  uint8_t left, uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void register_iuser_Protect_Input(Input* input, int32_t time);
static void register_idraw_Protect_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh);

static int16_t register_film_Callback(Film* film, FilmObject* fo);
static int16_t register_user_Door(Actor* door, int32_t time);
static int16_t register_user_Troop(Actor* troop, int32_t time);
static int16_t register_draw_Register_Back(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
										   int16_t refresh);
static int16_t register_iupdate_Register(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
										 uint8_t right, int16_t mouse_x, int16_t mouse_y);
static int16_t register_iuser_Register(Input* input, int32_t time);
static int16_t register_iupdate_Pilot_List(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
										   uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void register_idraw_Pilot_List(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static void register_iuser_Pilot_Button(Input* input, int32_t time);
static void register_idraw_Pilot_Button(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static void register_xuser_Pilot_Name(const char* search_name);
static void register_idraw_Pilot_Name(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static void register_iuser_Pilot_Info(Input* input, int32_t time);
static void register_idraw_Pilot_Info(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static void register_Draw_Pilot_Title(Rect* frame, int16_t phase);
static void register_Draw_Pilot_Lines(Rect* frame, int16_t phase, int16_t line2_off);
static void register_Draw_Pilot_Name(Rect* frame, int16_t phase, int16_t inner_phase);
static void register_Draw_Pilot_Info(Rect* frame, int16_t phase, int16_t inner_phase);
static int16_t register_Index_To_Pilot(int16_t logical_idx, int16_t* out_slot);
static int16_t register_Index_To_Pilot_Record(int16_t logical_idx, FastPilotRecord* out_rec);
static void register_Build_Fast_Pilot_Record(void);
static int16_t register_Read_Pilot_Data(TieFile* f, uint8_t* dst, uint16_t count);
static void register_Delete_Pilot_Record(void);
static void register_iuser_Pilot_Name(Input* input, int32_t time);
static void register_Revive_Pilot_Record(void);
static void register_Set_Your_Reg_Pilot(void);
static RegStringButton* register_Alloc_Input_Reg_String_Button(Input* parent, Rect* r, int16_t zinput,
															   InputUserFunc user_fn, const char* name,
															   int16_t is_filename_mode, int16_t id);
static int16_t register_Add_Key_To_Reg_String(RegStringButton* btn, char* s, int16_t c);
static void register_Set_Reg_String_Button_Name(Input* input, const char* src);
static void register_Get_Reg_String_Button_Name(RegStringButton* btn, char* dst);

/* ================================================================
 * Entry point
 * ================================================================ */

// FUNCTION: TIE95 0x7A4C0
// FUNCTION: TIE98 0x46FBC0
int16_t register_Register(SceneHeadStruct* scene_head) {
	ResFile* rf;
	Rect frame;

	int16_t i;
	PushButton* prev;
	PushButton* next;

	shipext_Delete_Temp_Pilot();
	pilot_active = -1;
	pilot_offset = 0;
	num_pilots = 0;
	num_loaded_pilots = 0;
	cur_page = 0;
	register_fast_pilot_record = 0;
	num_pages = 1;
	register_film = NULL;
	reg_bak = NULL;
	reg_door = NULL;
	reg_troop = NULL;
	symbols = NULL;
	reg_parent = NULL;
	pilot_list = NULL;
	pilot_name_input = NULL;
	pilot_info = NULL;
	pilot_delete = NULL;
	register_directory.entries = LANDRU_NULL_HANDLE;
	memset(reg_button, 0, sizeof(reg_button));

	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(124, 536), TIE_FRONTEND_EDITION(106, 274));

	rf = shellext_Open_Empire_Resource(TIE_FRONTEND_EDITION("register.lfd", "reg640.lfd"));
#ifdef TIE_MODERN
	if (!rf) {
		TieRegister_RunView(rf, false, TieProfile_UsesTie98Frontend() ? "reg640.lfd" : "register.lfd");
		return 0;
	}
#endif
	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));

	/* Load film. Tag the snapshot with the (lfd, film) tuple so
	 * the cutscene compositor can resolve a remaster bundle for
	 * this screen, and switch the RT to OVERLAY mode (persistent
	 * layered composite over classic FB) since the screen has
	 * dynamic UI text the engine renders into the classic FB
	 * that should still show through. */
	if (shellext_Get_Cur_Scene() == SCENE_REGISTER) {
		register_film = xfilm_Res_Callback_Film("register", &frame, 0, 0, 0, register_film_Callback);
#ifdef TIE_MODERN
		TieSnapshotBuilder_SetActiveFilm(TieProfile_UsesTie98Frontend() ? "REG640" : "REGISTER", "register");
#endif
	} else {
		register_film = xfilm_Res_Callback_Film("reg2", &frame, 0, 0, 0, register_film_Callback);
#ifdef TIE_MODERN
		TieSnapshotBuilder_SetActiveFilm(TieProfile_UsesTie98Frontend() ? "REG640" : "REGISTER", "reg2");
#endif
	}
#ifdef TIE_MODERN
	if (!register_film) {
		TieRegister_RunView(rf, false, shellext_Get_Cur_Scene() == SCENE_REGISTER ? "register" : "reg2");
		return 0;
	}
#endif
#ifdef TIE_MODERN
	for (i = 0; i < (TieProfile_UsesTie98Frontend() ? 2 : 3); i++) {
#ifdef TIE_MODERN
		if (!reg_button[i]) {
			TieRegister_RunView(rf, false, "registration button actor");
			return 0;
		}
#endif
	}
#endif
	/* Registration uses the default incremental redraw model. */

	xfilm_Set_Film_Def_Palette(register_film, scene_head->def_palette);

#ifdef TIE_MODERN
	/* TIE95 0x7A593 loads reg-bak1; TIE98 has no equivalent actor. */
	if (!TieProfile_UsesTie98Frontend()) {
		Actor* bak1 = xactor_Find_Actor(FOURCC_DELT, "reg-bak1");
#ifdef TIE_MODERN
		if (!bak1) {
			TieRegister_RunView(rf, false, "reg-bak1");
			return 0;
		}
#endif
		xactor_Non_Refreshable_Actor(bak1);
		xactor_Refresh_Actor(bak1);
	}

#elif defined(TIE98)

#else
	{
		Actor* bak1 = xactor_Find_Actor(FOURCC_DELT, "reg-bak1");
		xactor_Non_Refreshable_Actor(bak1);
		xactor_Refresh_Actor(bak1);
	}
#endif
#ifdef TIE_MODERN
	reg_bak = xactor_Find_Actor(FOURCC_DELT, "reg-bak2");
#else
	reg_bak = xactor_Find_Actor(FOURCC_DELT, "reg-bak2");
#endif
#ifdef TIE_MODERN
	if (!reg_bak) {
		TieRegister_RunView(rf, false, "reg-bak2");
		return 0;
	}
#endif
	xactor_Set_Actor_Draw_Function(reg_bak, register_draw_Register_Back);

#ifdef TIE_MODERN
	reg_door = xactor_Find_Actor(FOURCC_ANIM, "reg-dora");
#else
	reg_door = xactor_Find_Actor(FOURCC_ANIM, "reg-dora");
#endif
#ifdef TIE_MODERN
	if (!reg_door) {
		TieRegister_RunView(rf, false, "reg-dora");
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(reg_door, (xactorCallback)(void (*)(void))register_user_Door);

#ifdef TIE_MODERN
	reg_troop = xactor_Find_Actor(FOURCC_ANIM, "reg-trpa");
#else
	reg_troop = xactor_Find_Actor(FOURCC_ANIM, "reg-trpa");
#endif
#ifdef TIE_MODERN
	if (!reg_troop) {
		TieRegister_RunView(rf, false, "reg-trpa");
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(reg_troop, (xactorCallback)(void (*)(void))register_user_Troop);

	/* Init register_directory and symbol actor */
	xfiledir_Init_Directory(&register_directory, ".tfr", 0);
#ifdef TIE_MODERN
	if (!register_directory.entries) {
		TieRegister_RunView(rf, false, "pilot directory");
		return 0;
	}
#endif
#ifdef TIE_MODERN
	xfiledir_Set_Name_Length(&register_directory, FILEDIR_MAX_NAME_LENGTH);
#endif
	// TIE95 0x7A616; the copy-protection actor is absent from TIE98.
#ifdef TIE_MODERN
	if (!TieProfile_UsesTie98Frontend()) {
		symbols = xactanim_Res_Anim_Actor("symbols", &frame, 0, 0, 0);
#ifdef TIE_MODERN
		if (!symbols) {
			TieRegister_RunView(rf, false, "symbols");
			return 0;
		}
#endif
		xactor_Set_Actor_Time(symbols, 0, 0);
	}

#elif defined(TIE98)

#else
	symbols = xactanim_Res_Anim_Actor("symbols", &frame, 0, 0, 0);
	xactor_Set_Actor_Time(symbols, 0, 0);
#endif
	/* Build input tree */
	reg_parent = xinput_Alloc_Input(NULL, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!reg_parent) {
		TieRegister_RunView(rf, false, "registration input root");
		return 0;
	}
#endif

	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(240, 486), TIE_FRONTEND_EDITION(80, 188),
				   TIE_FRONTEND_EDITION(320, 621), TIE_FRONTEND_EDITION(150, 356));
	door_input = xinput_Alloc_Input(reg_parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!door_input) {
		TieRegister_RunView(rf, false, "registration door input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(door_input, register_iupdate_Register);
	xinpattr_Set_Input_User_Function(door_input, (InputUserFunc)(void (*)(void))register_iuser_Register);
	door_input->mouseUsage = allInput;
	door_input->id = 0;

	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(75, 170), TIE_FRONTEND_EDITION(102, 247),
				   TIE_FRONTEND_EDITION(127, 271), TIE_FRONTEND_EDITION(174, 414));
	pilot_list = xinput_Alloc_Input(reg_parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!pilot_list) {
		TieRegister_RunView(rf, false, "pilot list input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Update_Function(pilot_list, register_iupdate_Pilot_List);
	xinpattr_Set_Input_Draw_Function(pilot_list, register_idraw_Pilot_List);
	xinpattr_Refreshable_Input(pilot_list);
	pilot_list->id = 0;

	/* Pilot name input (RegStringButton, filename mode) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(75, 170), TIE_FRONTEND_EDITION(176, 424),
				   TIE_FRONTEND_EDITION(127, 271), TIE_FRONTEND_EDITION(186, 445));
	pilot_name_input =
		register_Alloc_Input_Reg_String_Button(reg_parent, &frame, 0, register_iuser_Pilot_Name, "", 1, 0);
#ifdef TIE_MODERN
	if (!pilot_name_input) {
		TieRegister_RunView(rf, false, "pilot name input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Draw_Function(&pilot_name_input->header, register_idraw_Pilot_Name);
	xinpattr_Refreshable_Input(&pilot_name_input->header);

	/* Prev/Next buttons */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(75, 167), TIE_FRONTEND_EDITION(189, 452),
				   TIE_FRONTEND_EDITION(85, 186), TIE_FRONTEND_EDITION(197, 475));
	prev = xbtnpush_Alloc_Small_Button(reg_parent, &frame, 0, register_iuser_Pilot_Button, NULL, 0);
#ifdef TIE_MODERN
	if (!prev) {
		TieRegister_RunView(rf, false, "previous-page input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Draw_Function(&prev->header, (InputDrawFunc)0);
	xinpattr_Refreshable_Input(&prev->header);

	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(117, 255), TIE_FRONTEND_EDITION(189, 452),
				   TIE_FRONTEND_EDITION(127, 276), TIE_FRONTEND_EDITION(197, 475));
	next = xbtnpush_Alloc_Small_Button(reg_parent, &frame, 0, register_iuser_Pilot_Button, NULL, 1);
#ifdef TIE_MODERN
	if (!next) {
		TieRegister_RunView(rf, false, "next-page input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Draw_Function(&next->header, (InputDrawFunc)0);
	xinpattr_Refreshable_Input(&next->header);

	/* Pilot info display */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(149, 308), TIE_FRONTEND_EDITION(102, 247),
				   TIE_FRONTEND_EDITION(201, 408), TIE_FRONTEND_EDITION(188, 450));
	pilot_info = xinput_Alloc_Input(reg_parent, &frame, 0, 0);
#ifdef TIE_MODERN
	if (!pilot_info) {
		TieRegister_RunView(rf, false, "pilot info input");
		return 0;
	}
#endif
	xinpattr_Set_Input_User_Function(pilot_info, register_iuser_Pilot_Info);
	xinpattr_Set_Input_Draw_Function(pilot_info, register_idraw_Pilot_Info);
	xinpattr_Refreshable_Input(pilot_info);
	pilot_info->id = 0;

	/* Delete button (initially hidden) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(148, 304), TIE_FRONTEND_EDITION(189, 452),
				   TIE_FRONTEND_EDITION(200, 413), TIE_FRONTEND_EDITION(198, 478));

	strcpy(reg_btn_name, textext_Get_Text(txtRegBtnDeletePilot));
	pilot_delete = (Input*)xbtnpush_Alloc_Small_Button(reg_parent, &frame, 0, register_iuser_Pilot_Button,
													   reg_btn_name, 2);
#ifdef TIE_MODERN
	if (!pilot_delete) {
		TieRegister_RunView(rf, false, "delete-pilot input");
		return 0;
	}
#endif
	xinpattr_Set_Input_Draw_Function(pilot_delete, register_idraw_Pilot_Button);
	xinpattr_Refreshable_Input(pilot_delete);
	xinpattr_Hide_Input(pilot_delete);

#ifdef TIE_MODERN
	TieRegister_RunView(rf,
						!TieProfile_UsesTie98Frontend() && TieRegister_CopyProtectionEnabled() &&
							shellext_Get_Cur_Scene() == SCENE_REGISTER,
						NULL);
	return 0;
#else
	xview_Set_View_Update_Function(register_end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
	xcanvas_Invalid_Screen_Diff();
	xio_Set_Key_Buttons();
	shellext_Handle_TIE_View();
	xio_Clear_Key_Buttons();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();
	xfiledir_Free_Directory(&register_directory);
	if (register_fast_pilot_record)
		xmemhdl_Free_Handle(register_fast_pilot_record);
	register_fast_pilot_record = LANDRU_NULL_HANDLE;
	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	xres_Close_Resource(rf);
	return xerror_Get_Landru_Exit();
#endif
}

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x7A93C
// FUNCTION: TIE98 0x4700D0
void register_end_View(int32_t phase) {
#ifdef TIE_MODERN
	char typed[TIE_PILOT_NAME_CAPACITY];
#else
	char typed[TIE_FRONTEND_EDITION(TIE_PILOT_NAME_CAPACITY, FILEDIR_NAME_CAPACITY)];
#endif

	if (!phase) {
		if (!xcursor_Is_Cursor_Visible())
			xcursor_Show_Cursor();

		/* Copy-protection challenge moved to register_task_step's
		 * PROTECT phase (runs before the view is pushed) so it can
		 * yield via the task stack. */
		if (shellext_Get_Cur_Scene() == SCENE_REGISTER && !TIE_FRONTEND_TIE98)
			xio_Set_Mouse_Position(160, 130);
	}

	if (phase == 18 && shellext_Get_Cur_Scene() == SCENE_REGISTER)
		soundext_Play_Speech(speechRegisterGuard);

	if (!phase) {
#ifndef TIE_MODERN
		char path[TIE_FRONTEND_EDITION(64, 256)];

#endif
		/* Load pilot register_directory and build FPR cache */
#ifdef TIE_MODERN
		xfiledir_Read_Directory(&register_directory);
#else
		xfiledir_Set_Current_Drive(&register_directory, install_drive_ok - 'a' + 1);
		path[0] = '\\';
		path[1] = '\0';
		strcat(path, cd_check_filename);
		xfiledir_Change_Current_Dir(&register_directory, path);
		if (TIE_FRONTEND_TIE98)
			xfiledir_Read_Directory(&register_directory);
		xfiledir_Restore_Default_Dir();
#endif
		num_loaded_pilots = register_directory.count;
		xcursor_Set_Cursor(1); /* waitCursor */
		register_Build_Fast_Pilot_Record();
		xcursor_Set_Cursor(0); /* mainCursor */

		num_pages = (num_pilots + TIE_FRONTEND_EDITION(10, 12) - 1) / TIE_FRONTEND_EDITION(10, 12);
		if (!num_pages)
			num_pages++;
		cur_page = 0;

		register_Set_Your_Reg_Pilot();
	}

	/* Per-frame: search for typed name in FPR */

	register_Get_Reg_String_Button_Name(pilot_name_input, typed);
	register_xuser_Pilot_Name(typed);

	/* Show/hide delete button */
	if (typed[0]) {
		if (xinpattr_Is_Input_Visible(pilot_delete)) {
			if (pilot_active == -1) {
				xinpattr_Hide_Input(pilot_delete);
				xview_Refresh_View();
			}
		} else if (pilot_active != -1) {
			xinpattr_Show_Input(pilot_delete);
			pilot_delete_status = -1;
			xview_Refresh_View();
		}

		if (xinpattr_Is_Input_Visible(pilot_delete)) {
			register_Index_To_Pilot_Record(pilot_active, &shell_pilot);
			if (pilot_delete_status) {
				strcpy(reg_btn_name + 32, textext_Get_Text(txtRegBtnDeletePilot));
				xbtnpush_Set_Button_Name((PushButton*)pilot_delete, reg_btn_name + 32);
				pilot_delete_status = 0;
				xview_Refresh_View();
			}
		}
	} else {
		if (xinpattr_Is_Input_Visible(pilot_delete)) {
			xinpattr_Hide_Input(pilot_delete);
			xview_Refresh_View();
		}
	}
}

/* ================================================================
 * Film callback
 * ================================================================ */

// FUNCTION: TIE95 0x7AB24
// FUNCTION: TIE98 0x470340
static int16_t register_film_Callback(Film* film, FilmObject* fo) {
	Actor* actor;

	if (fo->id != 3)
		return 0;
	xfilm_Rewind_Actor_Film(film, fo, (void*)((char*)fo + sizeof(FilmObject)));
	actor = (Actor*)fo->object;
	if (actor->var1 == 10)
		reg_button[actor->var2] = actor;
	return 0;
}

/* ================================================================
 * Actor callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7AB60
// FUNCTION: TIE98 0x470380
static int16_t register_user_Door(Actor* door, int32_t time) {
	(void)time;
	if (door->var1) {
		if (xactor_Is_Actor_Visible(door)) {
			if (door->state < door->arraySize - 1)
				xactor_Set_Actor_State(door, door->state + 1, 0);
		} else {
			if (!door->state)
				soundext_Play_SFX(sfxSmallDoorOpen, 0);
			xactor_Show_Actor(door);
			xactor_Set_Actor_State(door, 0, 0);
		}
		door->var1 = 0;
	} else if (xactor_Is_Actor_Visible(door)) {
		if (door->state > 0) {
			xactor_Set_Actor_State(door, door->state - 1, 0);
		} else {
			soundext_Play_SFX(sfxSmallDoorShut, 0);
			xactor_Hide_Actor(door);
			xactor_Refresh_Actor(reg_bak);
		}
	}
	return 1;
}

// FUNCTION: TIE95 0x7AC10
// FUNCTION: TIE98 0x470450
static int16_t register_user_Troop(Actor* troop, int32_t time) {
	(void)time;
	if (troop->var1) {
		if (troop->state == 3)
			soundext_Play_SFX(sfxGunCock, 64);
		if (troop->state < troop->arraySize - 1)
			xactor_Set_Actor_State(troop, troop->state + 1, 0);
		troop->var1 = 0;
	} else {
		if (troop->state > 0)
			xactor_Set_Actor_State(troop, troop->state - 1, 0);
	}
	return 1;
}

// FUNCTION: TIE95 0x7AC80
// FUNCTION: TIE98 0x4704C0
static int16_t register_draw_Register_Back(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
										   int16_t refresh) {
	if (!refresh)
		return 1;
	xactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);
	if (num_pages > 1) {
		Rect r;
		char buf[16];

		xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(92, 187), TIE_FRONTEND_EDITION(192, 456),
					   TIE_FRONTEND_EDITION(111, 255), TIE_FRONTEND_EDITION(196, 472));

#ifdef TIE_MODERN
		snprintf(buf, sizeof(buf), "%d:%d", cur_page + 1, num_pages);
#else
		sprintf(buf, "%d:%d", cur_page + 1, num_pages);
#endif
		xfont_Enable_FontID_Shadow(1);
		xfont_Print_Centered_Text(buf, &r, TIE_FRONTEND_EDITION(1, 3), 15);
		xfont_Disable_FontID_Shadow(1);
	}
	return 1;
}

/* ================================================================
 * iupdate/iuser/idraw — Register main
 * ================================================================ */

// FUNCTION: TIE95 0x7AD2C
// FUNCTION: TIE98 0x470580
static int16_t register_iupdate_Register(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
										 uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	char name_buf[TIE_PILOT_NAME_CAPACITY];
	FastPilotRecord fpr;

	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;
	if (key)
		return 0;

	register_Get_Reg_String_Button_Name(pilot_name_input, name_buf);

	if (!strlen(name_buf)) {
		input->var1 = 3;
		if (!xactor_Is_Actor_Visible(reg_door))
			reg_troop->var1 = 1;
		return 1;
	}

	/* Check if pilot is protected */

	if (pilot_active != -1 && register_Index_To_Pilot_Record(pilot_active, &fpr)) {
		if (fpr.lost_status) {
			input->var1 = (fpr.lost_status == 2) ? 5 : 4;
			if (!xactor_Is_Actor_Visible(reg_door))
				reg_troop->var1 = 1;
			return 1;
		}
	}

	if (left == 3 || right == 3) {
		input->var2 = SCENE_MAIN_MENU;
		input->var1 = 1;
	} else {
		input->var1 = 2;
	}

	if (!reg_troop->state)
		reg_door->var1 = 1;

	return 1;
}

// FUNCTION: TIE95 0x7AE3C
// FUNCTION: TIE98 0x470680
static int16_t register_iuser_Register(Input* input, int32_t time) {
	int16_t state;

	(void)time;
	state = input->var1;
	switch (state) {
		case 1: {
			char name_buf[TIE_PILOT_NAME_CAPACITY];

			xerror_Set_Landru_Exit(input->var2);

			register_Get_Reg_String_Button_Name(pilot_name_input, name_buf);
			shipext_Set_Pilot_Name(name_buf);
			if (pilot_active == -1)
				shipext_Create_Pilot(name_buf);
			shipext_Load_Pilot(name_buf);
			break;
		}
		case 2:
		case 3:
		case 4:
		case 5:
			input->var1 = 0;
			break;
		default:
			break;
	}
	return 1;
}

/* ================================================================
 * Pilot list callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7AEA4
// FUNCTION: TIE98 0x470720
static int16_t register_iupdate_Pilot_List(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
										   uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	uint8_t btn;
	int16_t row;
	int16_t wanted;
	int16_t slot;
	char dir_name[TIE_PILOT_NAME_CAPACITY];

	(void)bounds;
	(void)clip;
	(void)mouse_x;
	if (key)
		return 0;

	btn = left ? left : right;
	if (btn != 3)
		return 1;

	if (TIE_FRONTEND_TIE98)
		row = mouse_y / (xfont_Get_FontID_Height(3) + 1);
	else
		row = (mouse_y + 6) / (xfont_Get_FontID_Height(1) + 1) - 1;
	if (row < 0)
		row = 0;
	if (row >= TIE_FRONTEND_EDITION(10, 12))
		row = TIE_FRONTEND_EDITION(10, 12) - 1;

	wanted = pilot_offset + row;

	if (!register_Index_To_Pilot(wanted, &slot))
		return 1;

	if (!register_Find_Reg_Dir_Name(&register_directory, dir_name, slot))
		return 1;

	shipext_Set_Pilot_Name("");
	register_Set_Reg_String_Button_Name(&pilot_name_input->header, dir_name);
	xinpattr_Refresh_Input(input);
	pilot_active = pilot_offset + row;
	pilot_info->var1 = 1;
	return 1;
}

// FUNCTION: TIE95 0x7AF6C
// FUNCTION: TIE98 0x470800
static void register_idraw_Pilot_List(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	Rect dst;
	int16_t row;

	if (!refresh)
		return;

	xpaint_Paint_Clipped_Rect(frame, 240);

	xrect_Copy_Rect(&dst, frame);
	dst.left++;
	dst.top += 2;
	dst.bottom = dst.top + TIE_FRONTEND_EDITION(7, xfont_Get_FontID_Height(3) + 1);

	xfont_Enable_FontID_Shadow(TIE_FRONTEND_EDITION(1, 3));

	for (row = 0; row < TIE_FRONTEND_EDITION(10, 12); row++) {
		int16_t slot;
		char dir_name[TIE_PILOT_NAME_CAPACITY];
#ifdef TIE_MODERN
		char display_name[TIE_PILOT_NAME_CAPACITY];
#endif
		FastPilotRecord fpr;
		int16_t color;

		if (!register_Index_To_Pilot(pilot_offset + row, &slot))
			break;

		if (register_Find_Reg_Dir_Name(&register_directory, dir_name, slot)) {
#ifdef TIE_MODERN
			TiePilotName_CopyForDisplay(display_name, sizeof(display_name), dir_name);
#endif
			register_Index_To_Pilot_Record(pilot_offset + row, &fpr);
			color = 15;
			if (fpr.lost_status)
				color = 4;
			if (pilot_offset + row == pilot_active)
				color = 14;
#ifdef TIE_MODERN
			xfont_Print_Clipped_Text(display_name, dst.left + 1, dst.top, TIE_FRONTEND_EDITION(1, 3), color);
#else
			xfont_Print_Clipped_Text(dir_name, dst.left + 1, dst.top, TIE_FRONTEND_EDITION(1, 3), color);
#endif
		}
		xrect_Offset_Rect(&dst, 0, TIE_FRONTEND_EDITION(7, xfont_Get_FontID_Height(3) + 1));
	}

	xfont_Disable_FontID_Shadow(TIE_FRONTEND_EDITION(1, 3));
	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}

/* ================================================================
 * Pilot button callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7B0A0
// FUNCTION: TIE98 0x470950
static void register_iuser_Pilot_Button(Input* input, int32_t time) {
	PushButton* btn;
	int16_t id;

	(void)time;
	btn = (PushButton*)input;
	id = btn->header.id;

	switch (id) {
		case 0:
			xactor_Set_Actor_State(reg_button[0], TIE_FRONTEND_EDITION(2 * btn->pressed, btn->pressed), 0);
			break;
		case 1:
			xactor_Set_Actor_State(reg_button[1], TIE_FRONTEND_EDITION(2 * btn->pressed + 1, btn->pressed),
								   0);
			break;
		case 2:
			/* TIE98 drives the delete button through the next-page actor. */
			xactor_Set_Actor_State(reg_button[TIE_FRONTEND_EDITION(2, 1)],
								   TIE_FRONTEND_EDITION(btn->pressed + 4, btn->pressed), 0);
			break;
	}

	if (!xinpattr_Get_Input_Selected(&btn->header))
		return;
#ifdef TIE_MODERN
	/* The delete confirmation re-enters this callback with its result. */
	if (!TieRegister_IsResumingDelete())
#endif
		soundext_Play_SFX(sfxButton, 64);

	switch (btn->header.id) {
		case 0:
			/* Previous page */
			if (cur_page > 0)
				cur_page--;
			else
				cur_page = num_pages - 1;
			pilot_offset = TIE_FRONTEND_EDITION(10, 12) * cur_page;
			break;
		case 1:
			/* Next page */
			if (cur_page < num_pages - 1)
				cur_page++;
			else
				cur_page = 0;
			pilot_offset = TIE_FRONTEND_EDITION(10, 12) * cur_page;
			break;
		case 2: {
			int16_t result;

#ifdef TIE_MODERN
			TieRegister_BeginDelete(input);
#endif
			result = register_Do_Delete_Dialog();
			if (result == 2)
				break;
			if (result == 1) {
#ifdef TIE_MODERN
				char name_buf[TIE_PILOT_NAME_CAPACITY];
				char path[TIE_PILOT_NAME_CAPACITY + 5];

				register_Get_Reg_String_Button_Name(pilot_name_input, name_buf);
				snprintf(path, sizeof(path), "%s.tfr", name_buf);
				TieStorage_Remove(TIE_FILE_ROOT_USER, path);
#else
				char path[TIE_FRONTEND_EDITION(16, 40)];

				register_Get_Reg_String_Button_Name(pilot_name_input, path);
				strcat(path, ".tfr");
				shellext_Delete_Install_File(path);
#endif
				register_Delete_Pilot_Record();
			} else {
				register_Revive_Pilot_Record();
			}
			xview_Refresh_View();
			break;
		}
	}
}

// FUNCTION: TIE95 0x7B200
// FUNCTION: TIE98 0x470AE0
static void register_idraw_Pilot_Button(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	PushButton* btn;
	Rect tr;
	int16_t color;
	const char* text;

	if (!refresh)
		return;
	btn = (PushButton*)input;

	xrect_Copy_Rect(&tr, frame);
	tr.top++;

	if (btn->pressed)
		color = 18;
	else
		color = 20;
	text = (const char*)xmemhdl_Lock_Handle(btn->name);
	xfont_Print_Centered_Text(text, &tr, TIE_FRONTEND_EDITION(1, 3), color);
	xmemhdl_Unlock_Handle(btn->name);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}

// FUNCTION: TIE95 0x7B2CC
// FUNCTION: TIE98 0x470BE0
static void register_xuser_Pilot_Name(const char* search_name) {
	int16_t matched = -1;

	int16_t i;

	for (i = 0; i < num_pilots; i++) {
		int16_t slot;
		char dir_name[TIE_PILOT_NAME_CAPACITY];

		if (matched != -1)
			break;

		if (register_Index_To_Pilot(i, &slot) &&
			register_Find_Reg_Dir_Name(&register_directory, dir_name, slot) && !strcmp(dir_name, search_name))
			matched = i;
	}

	if (matched == -1) {
		if (pilot_active != -1) {
			xinpattr_Refresh_Input(pilot_list);
			pilot_active = -1;
		}
	} else if (matched != pilot_active) {
		int16_t page;

		xinpattr_Refresh_Input(pilot_list);
		pilot_active = matched;
		page = matched / TIE_FRONTEND_EDITION(10, 12);
		if (page != cur_page) {
			cur_page = page;
			pilot_offset = TIE_FRONTEND_EDITION(10, 12) * page;
		}
	}
}

// FUNCTION: TIE95 0x7B3B0
// FUNCTION: TIE98 0x470D00
static void register_idraw_Pilot_Name(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	RegStringButton* btn = (RegStringButton*)input;
	char display_name[TIE_PILOT_NAME_CAPACITY];
	int16_t saved_font;
	int16_t str_width;
	int16_t caret_color;
	int16_t caret_y;

	TiePilotName_CopyForDisplay(display_name, sizeof(display_name), btn->name);
	saved_font = xfont_Get_Font();
	xfont_Set_Font(TIE_FRONTEND_EDITION(0, 3));
	str_width = xfont_Get_String_Width(display_name);
	xfont_Set_Font(saved_font);

	if (refresh) {
		int16_t color;
		int16_t x;
		int16_t y;

		xpaint_Paint_Clipped_Rect(frame, 0);
		color = xstyle_Get_Style_Down_Color();
		x = frame->left + TIE_FRONTEND_EDITION(2, 1);
		y = frame->top + TIE_FRONTEND_EDITION(1, 2);
		xfont_Print_Clipped_Text(display_name, x, y, TIE_FRONTEND_EDITION(0, 3), color);
	}

	caret_color = 0;
	if (xinpattr_Is_Input_Active(&btn->header) && (int)strlen(btn->name) < TIE_PILOT_NAME_MAX && xio_Blink())
		caret_color = xstyle_Get_Style_Down_Color();

	caret_y = frame->top + TIE_FRONTEND_EDITION(7, xfont_Get_FontID_Height(3) + 1);
	xpaint_Horiz_Clipped_Line(frame->left + str_width + 4, caret_y, 5, caret_color);

	if (xinpattr_Is_Input_Dirty(&btn->header))
		xdirty_Dirty_Rect(clip);
}

/* ================================================================
 * Pilot info callbacks + draw helpers
 * ================================================================ */

// FUNCTION: TIE95 0x7B490
// FUNCTION: TIE98 0x470E20
static void register_iuser_Pilot_Info(Input* input, int32_t time) {
	(void)time;
	if (!input->var1)
		return;

	if (input->var1 == 1) {
		input->var2 = 0;
		input->var1 = 2;
	} else if (input->var2 < 256) {
		input->var2++;
		if (input->var2 == 2)
			soundext_Play_SFX(sfxLogon, 64);
	}
}

// FUNCTION: TIE95 0x7B4DC
// FUNCTION: TIE98 0x470E70
static void register_idraw_Pilot_Info(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	(void)refresh;
	xpaint_Paint_Clipped_Rect(frame, 16);

	if (input->var1) {
		int16_t phase = input->var2;

		/* Inner phase for the info panel offset */
		int16_t inner = 0;
		if (phase >= 16 && pilot_record.exit_status) {
			int16_t inner_max;

			inner = phase - 15;
			inner_max = TIE_FRONTEND_EDITION(6, xfont_Get_FontID_Height(3) + 1);
			if (inner > inner_max)
				inner = inner_max;
		}

		register_Draw_Pilot_Title(frame, phase);
		register_Draw_Pilot_Lines(frame, phase, inner);
		register_Draw_Pilot_Name(frame, phase, inner);
		register_Draw_Pilot_Info(frame, phase, inner);
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}

// FUNCTION: TIE95 0x7B578
// FUNCTION: TIE98 0x470F10
static void register_Draw_Pilot_Title(Rect* frame, int16_t phase) {
	/* Conditional coordinates are from TIE98 0x470F10; fixed coordinates
	 * are from TIE95 0x7B578. */
	char buf[20];

	int16_t len1;
	int16_t c1;
	int16_t imperial_x;

	if (phase > 7) {
		int16_t extra_h = 2 * (phase - 7);
		Rect tr;

		if (frame->top + extra_h >= frame->bottom)
			extra_h = frame->bottom - frame->top;

		xrect_Set_Rect(&tr, frame->left, frame->top, frame->right, frame->top + extra_h);
		xpaint_Paint_Clipped_Rect(&tr, 240);
	}

	len1 = phase >= 8 ? 8 : phase;
	strncpy(buf, textext_Get_Text(txtRegInfoImperial), len1);
	buf[len1] = 0;
	c1 = len1 + 240;
	if (c1 > 247)
		c1 = 247;
	imperial_x = TIE_FRONTEND_EDITION(11, 30);
	xfont_Print_Clipped_Text(buf, frame->left + imperial_x, frame->top + 1, TIE_FRONTEND_EDITION(1, 3), c1);

	if (phase >= 4) {
		int16_t len2 = phase >= 12 ? 8 : phase - 4;
		int16_t c2;
		int16_t database_x;
		int16_t database_y;

		strncpy(buf, textext_Get_Text(txtRegInfoDatabase), len2);
		buf[len2] = 0;
		c2 = len2 + 240;
		if (c2 > 247)
			c2 = 247;
		database_x = TIE_FRONTEND_EDITION(10, 26);
		database_y = TIE_FRONTEND_EDITION(7, xfont_Get_FontID_Height(3) + 1);
		xfont_Print_Clipped_Text(buf, frame->left + database_x, frame->top + database_y,
								 TIE_FRONTEND_EDITION(1, 3), c2);
	}

	if (phase < 16) {
		int16_t access_x = TIE_FRONTEND_EDITION(10, 20);
		int16_t access_y = TIE_FRONTEND_EDITION(36, (frame->bottom - frame->top) / 2);
		xfont_Print_Clipped_Text(textext_Get_Text(txtRegInfoAccess), frame->left + access_x,
								 frame->top + access_y, TIE_FRONTEND_EDITION(1, 3),
								 4 * ((phase >> 1) & 3) + 19);
	}
}

// FUNCTION: TIE95 0x7B710
// FUNCTION: TIE98 0x471090
static void register_Draw_Pilot_Lines(Rect* frame, int16_t phase, int16_t line2_off) {
	int16_t pc = phase > 15 ? 15 : phase;
	int16_t w = frame->right - frame->left - (30 - 2 * pc);
	int16_t x = 15 - pc + frame->left;

	// TIE98 0x471090; the branch below it is TIE95 0x7B710.
#ifdef TIE98
	int16_t font_h = xfont_Get_FontID_Height(3);
	int16_t line1_y = frame->top + 2 * font_h + 3;
	int16_t line2_y = frame->top + line2_off + 3 * font_h + 4;
	xpaint_Horiz_Clipped_Line(x, line1_y, w, pc / 2 + 240);
	xpaint_Horiz_Clipped_Line(x, line2_y, w, pc / 2 + 240);

	if (phase > line2_off + 15) {
		int16_t color = 240;
		int16_t progress = 2 * (phase - line2_off) - 37;
		while (color <= 247) {
			if (progress >= 0) {
				int16_t clamped = progress <= 62 ? progress : 62;
				if (clamped != 62 || color == 247)
					xpaint_Horiz_Clipped_Line(frame->left, frame->bottom - 3, w, color);
			}
			progress++;
			color++;
		}
	}
#else
#ifdef TIE_MODERN
	if (TieProfile_UsesTie98Frontend()) {
		int16_t font_h = xfont_Get_FontID_Height(3);
		int16_t line1_y = frame->top + 2 * font_h + 3;
		int16_t line2_y = frame->top + line2_off + 3 * font_h + 4;
		xpaint_Horiz_Clipped_Line(x, line1_y, w, pc / 2 + 240);
		xpaint_Horiz_Clipped_Line(x, line2_y, w, pc / 2 + 240);

		if (phase > line2_off + 15) {
			int16_t color = 240;
			int16_t progress = 2 * (phase - line2_off) - 37;
			while (color <= 247) {
				if (progress >= 0) {
					int16_t clamped = progress <= 62 ? progress : 62;
					if (clamped != 62 || color == 247)
						xpaint_Horiz_Clipped_Line(frame->left, frame->bottom - 3, w, color);
				}
				progress++;
				color++;
			}
		}
		return;
	}
#endif

	xpaint_Horiz_Clipped_Line(x, frame->top + 14, w, pc / 2 + 240);
	xpaint_Horiz_Clipped_Line(x, line2_off + frame->top + 22, w, pc / 2 + 240);

	if (phase > line2_off + 15) {
		int16_t color = 240;
		int16_t yp = 2 * (phase - (line2_off + 15)) - 7;
		while (color <= 247) {
			if (yp >= 0) {
				int16_t yo = yp <= 62 ? yp : 62;
				if (yo != 62 || color == 247)
					xpaint_Horiz_Clipped_Line(frame->left, yo + line2_off + frame->top + 23, w, color);
			}
			yp++;
			color++;
		}
	}
#endif
}

// FUNCTION: TIE95 0x7B858
// FUNCTION: TIE98 0x471190
static void register_Draw_Pilot_Name(Rect* frame, int16_t phase, int16_t inner_phase) {
	char typed[TIE_PILOT_NAME_CAPACITY];
	register_Get_Reg_String_Button_Name(pilot_name_input, typed);

	if (phase == 2) {
		shipext_Init_Pilot();
		if (pilot_active != -1) {
			int16_t slot;
			char dir_name[TIE_PILOT_NAME_CAPACITY];
			if (register_Index_To_Pilot(pilot_active, &slot) &&
				register_Find_Reg_Dir_Name(&register_directory, dir_name, slot)) {
				if (!shipext_Load_Pilot(dir_name)) {
					pilot_active = -1;
					typed[0] = 0;
					register_Set_Reg_String_Button_Name(&pilot_name_input->header, typed);
					xinpattr_Refresh_Input(&pilot_name_input->header);
				}
			}
		}
	}

	if (phase >= 8) {
		char display_name[TIE_PILOT_NAME_CAPACITY];
		int16_t ci;
		Rect tr;
		int16_t status_offset;
		int16_t status_phase;

		TiePilotName_CopyForDisplay(display_name, sizeof(display_name), typed);
		ci = phase >= 15 ? 7 : phase - 8;

		xrect_Copy_Rect(&tr, frame);
		// TIE98 0x471190; the alternative is TIE95 0x7B858.
#ifdef TIE_MODERN
		if (TieProfile_UsesTie98Frontend()) {
			int16_t font_h = xfont_Get_FontID_Height(3);
			tr.top = frame->top + 2 * font_h + 4;
			tr.bottom = tr.top + font_h;
		} else {
			tr.top = frame->top + 17;
			tr.bottom = frame->top + 22;
		}
		xfont_Print_Centered_Text(display_name, &tr, TieProfile_UsesTie98Frontend() ? 3 : 1, ci + 248);
#elif defined(TIE98)
		tr.top = frame->top + 2 * xfont_Get_FontID_Height(3) + 4;
		tr.bottom = tr.top + xfont_Get_FontID_Height(3);
		xfont_Print_Centered_Text(display_name, &tr, 3, ci + 248);
#else
		tr.top = frame->top + 17;
		tr.bottom = frame->top + 22;
		xfont_Print_Centered_Text(display_name, &tr, 1, ci + 248);
#endif

		status_offset = TIE_FRONTEND_EDITION(6, xfont_Get_FontID_Height(3));
		status_phase = TIE_FRONTEND_EDITION(6, status_offset + 1);
		if (phase >= 16 && pilot_record.exit_status && inner_phase >= status_phase) {
			xrect_Offset_Rect(&tr, 0, status_offset);
			xfont_Print_Centered_Text(textext_Get_Text((TIEText)(pilot_record.exit_status + 10)), &tr,
									  TIE_FRONTEND_EDITION(1, 3), (phase & 7) / 2 + 252);
		}
	}
}

// FUNCTION: TIE95 0x7B9B8
// FUNCTION: TIE98 0x471320
static void register_Draw_Pilot_Info(Rect* frame, int16_t phase, int16_t inner_phase) {
	// TIE98 0x471320; the branch below it is TIE95 0x7B9B8.
#ifdef TIE98
	int16_t label_h = xfont_Get_FontID_Height(2);
	int16_t value_h = xfont_Get_FontID_Height(3);
	int16_t row = label_h + value_h + 2;
	int16_t first_row_y = 3 * value_h + 7;

	if (phase >= inner_phase + 18) {
		int16_t sub = phase - (inner_phase + 18);
		int16_t c1;
		int16_t c2;
		int16_t y;
		char buf[20];

		if (sub > 7)
			sub = 7;
		c1 = sub + 240;
		c2 = sub + 248;
		y = frame->top + inner_phase + first_row_y;

		xfont_Enable_FontID_Shadow(2);
		xfont_Enable_FontID_Shadow(3);

		xfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotRank), frame->left + 2, y, 2, c1);
		xfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.rank + 1)), frame->left + 2,
								 y + label_h, 3, c2);

		snprintf(buf, sizeof(buf), "%lu", (unsigned long)pilot_record.score);
		y += row + 10;
		xfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotScore), frame->left + 2, y, 2, c1);
		xfont_Print_Clipped_Text(buf, frame->left + 2, y + label_h, 3, c2);

		y += row + 10;
		xfont_Print_Clipped_Text(textext_Get_Text(txtRegLevel), frame->left + 2, y, 2, c1);
		xfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.game_level + 29)), frame->left + 2,
								 y + label_h, 3, c2);

		xfont_Disable_FontID_Shadow(2);
		xfont_Disable_FontID_Shadow(3);
	}

	if (phase > inner_phase + 15) {
		int16_t cover = 2 * (phase - inner_phase) - 30;
		if (cover < 62) {
			Rect r;
			xrect_Set_Rect(&r, frame->left, frame->top + inner_phase + cover + first_row_y + 2 * (row + 10),
						   frame->right, frame->bottom);
			xpaint_Paint_Clipped_Rect(&r, 16);
		}
	}
#else
	int16_t color;
	char buf[20];

#ifdef TIE_MODERN
	if (TieProfile_UsesTie98Frontend()) {
		int16_t label_h = xfont_Get_FontID_Height(2);
		int16_t value_h = xfont_Get_FontID_Height(3);
		int16_t row = label_h + value_h + 2;
		int16_t first_row_y = 3 * value_h + 7;

		if (phase >= inner_phase + 18) {
			int16_t sub = phase - (inner_phase + 18);
			int16_t c1;
			int16_t c2;
			int16_t y;
			char buf[20];

			if (sub > 7)
				sub = 7;
			c1 = sub + 240;
			c2 = sub + 248;
			y = frame->top + inner_phase + first_row_y;

			xfont_Enable_FontID_Shadow(2);
			xfont_Enable_FontID_Shadow(3);

			xfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotRank), frame->left + 2, y, 2, c1);
			xfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.rank + 1)), frame->left + 2,
									 y + label_h, 3, c2);

			snprintf(buf, sizeof(buf), "%lu", (unsigned long)pilot_record.score);
			y += row + 10;
			xfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotScore), frame->left + 2, y, 2, c1);
			xfont_Print_Clipped_Text(buf, frame->left + 2, y + label_h, 3, c2);

			y += row + 10;
			xfont_Print_Clipped_Text(textext_Get_Text(txtRegLevel), frame->left + 2, y, 2, c1);
			xfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.game_level + 29)),
									 frame->left + 2, y + label_h, 3, c2);

			xfont_Disable_FontID_Shadow(2);
			xfont_Disable_FontID_Shadow(3);
		}

		if (phase > inner_phase + 15) {
			int16_t cover = 2 * (phase - inner_phase) - 30;
			if (cover < 62) {
				Rect r;
				xrect_Set_Rect(&r, frame->left,
							   frame->top + inner_phase + cover + first_row_y + 2 * (row + 10), frame->right,
							   frame->bottom);
				xpaint_Paint_Clipped_Rect(&r, 16);
			}
		}
		return;
	}
#endif

	if (phase >= inner_phase + 18) {
		color = phase - (inner_phase + 18);
		if (color > 7)
			color = 7;
		color += 248;

		xfont_Enable_FontID_Shadow(0);
		xfont_Enable_FontID_Shadow(1);

		/* Rank */
		strcpy(buf, textext_Get_Text((TIEText)(pilot_record.rank + 1)));
		xfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotRank), frame->left + 2,
								 inner_phase + frame->top + 24, 0, color - 8);
		xfont_Print_Clipped_Text(buf, frame->left + 2, inner_phase + frame->top + 34, 1, color);

		/* Score */
		sprintf(buf, "%lu", (unsigned long)pilot_record.score);
		xfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotScore), frame->left + 2,
								 inner_phase + frame->top + 44, 0, color - 8);
		xfont_Print_Clipped_Text(buf, frame->left + 2, inner_phase + frame->top + 54, 1, color);

		/* Level */
		xfont_Print_Clipped_Text(textext_Get_Text(txtRegLevel), frame->left + 2,
								 inner_phase + frame->top + 64, 0, color - 8);
		xfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.game_level + 29)), frame->left + 2,
								 inner_phase + frame->top + 74, 1, color);

		xfont_Disable_FontID_Shadow(0);
		xfont_Disable_FontID_Shadow(1);
	}

	/* Progressive reveal: paint a color-16 cover rect over the lower
	 * portion of the panel that hasn't been animated in yet. The cover
	 * top descends 2 px per phase tick past inner_phase + 15, exposing
	 * the rank/score/level rows top-down. After ~31 ticks the cover is
	 * fully off-frame (offset >= 62) and the text is fully visible. */
	if (phase > inner_phase + 15) {
		int16_t cover = 2 * (phase - (inner_phase + 15));
		if (cover < 62) {
			Rect r;
			xrect_Set_Rect(&r, frame->left, frame->top + inner_phase + cover + 24, frame->right,
						   frame->bottom);
			xpaint_Paint_Clipped_Rect(&r, 16);
		}
	}
#endif
}

/* ================================================================
 * Helper: FPR index walk
 * ================================================================ */

/* Map logical pilot index (skipping deleted slots) to physical slot.
 * Returns 1 if found, 0 otherwise. */
// FUNCTION: TIE95 0x7BC00
// FUNCTION: TIE98 0x4715C0
static int16_t register_Index_To_Pilot(int16_t logical_idx, int16_t* out_slot) {
	FastPilotRecord* rec;
	int16_t logical_count;
	int16_t i;

	if (!register_fast_pilot_record)
		return 0;

#ifdef TIE_MODERN
	rec = (FastPilotRecord*)register_fast_pilot_record;
#else
	rec = xmemhdl_Lock_Handle(register_fast_pilot_record);
#endif
	*out_slot = -1;
	logical_count = 0;

	for (i = 0; i < num_loaded_pilots; i++, rec++) {
		if (*out_slot != -1)
			break;
		if (rec->name[0]) {
			if (logical_count == logical_idx)
				*out_slot = i;
			else
				logical_count++;
		}
	}
#ifndef TIE_MODERN
	xmemhdl_Unlock_Handle(register_fast_pilot_record);
#endif
	return *out_slot != -1;
}

/* Like register_Index_To_Pilot but also copies the 20-byte FPR. */
// FUNCTION: TIE95 0x7BC84
// FUNCTION: TIE98 0x471630
static int16_t register_Index_To_Pilot_Record(int16_t logical_idx, FastPilotRecord* out_rec) {
	FastPilotRecord* rec;
	int16_t found_slot;
	int16_t logical_count;
	int16_t i;

	if (!register_fast_pilot_record)
		return 0;

#ifdef TIE_MODERN
	rec = (FastPilotRecord*)register_fast_pilot_record;
#else
	rec = xmemhdl_Lock_Handle(register_fast_pilot_record);
#endif
	found_slot = -1;
	logical_count = 0;

	for (i = 0; i < num_loaded_pilots; i++, rec++) {
		if (found_slot != -1)
			break;
		if (rec->name[0]) {
			if (logical_count == logical_idx) {
				found_slot = i;
				*out_rec = *rec;
			} else {
				logical_count++;
			}
		}
	}
#ifndef TIE_MODERN
	xmemhdl_Unlock_Handle(register_fast_pilot_record);
#endif
	return found_slot != -1;
}

// FUNCTION: TIE95 0x7BD28
// FUNCTION: TIE98 0x4716C0
// PORT: native allocation replaces the original Landru handle.
// HARDENING: returns when allocation fails.
static void register_Build_Fast_Pilot_Record(void) {
	FastPilotRecord* rec;
	int16_t i;

	if (!num_loaded_pilots)
		return;

#ifdef TIE_MODERN
	register_fast_pilot_record = calloc(num_loaded_pilots, sizeof(FastPilotRecord));
	if (!register_fast_pilot_record)
		return;
	rec = (FastPilotRecord*)register_fast_pilot_record;
#else
	register_fast_pilot_record =
		xmemhdl_Alloc_Handle(num_loaded_pilots * sizeof(FastPilotRecord), LANDRU_MEMORY_RESOURCE);
	rec = xmemhdl_Lock_Handle(register_fast_pilot_record);
#endif
	num_pilots = 0;

	for (i = 0; i < num_loaded_pilots; i++, rec++) {
		char dst[TIE_PILOT_NAME_CAPACITY];
		char name[TIE_PILOT_NAME_CAPACITY + 5];
		TieFile* f;

		rec->name[0] = 0;

		if (!register_Find_Reg_Dir_Name(&register_directory, dst, i))
			continue;

		snprintf(name, sizeof(name), "%s.tfr", dst);
		f = TieStorage_Open(TIE_FILE_ROOT_USER, name, "rb");
		if (!f)
			continue;

		if (register_Read_Pilot_Data(f, cur_pilot, PILOTRECORD_DISK_SIZE)) {
#ifdef TIE_MODERN
			PilotRecord decoded;
			const PilotRecord* pr = &decoded;
#else
			const PilotRecord* pr = (const PilotRecord*)cur_pilot;
#endif

			strncpy(rec->name, dst, sizeof(rec->name) - 1);
			rec->name[sizeof(rec->name) - 1] = 0;
			/* Decode the .tfr byte image into a typed PilotRecord so
			 * the score / cur_battle / etc. reads go through the
			 * canonical LE codec instead of host-endian byte fishing. */

#ifdef TIE_MODERN
			PilotRecord_decode(&decoded, cur_pilot);
#endif
			rec->lost_status = pr->exit_status;
			rec->rank = pr->rank;
			rec->cur_battle = pr->cur_battle;
			rec->score = pr->score;
			num_pilots++;
		}
		TieStorage_Close(f);
	}
#ifndef TIE_MODERN
	xmemhdl_Unlock_Handle(register_fast_pilot_record);
#endif
}

/* ================================================================
 * Pilot data I/O
 * ================================================================ */

// FUNCTION: TIE95 0x7BE54
// FUNCTION: TIE98 0x471850
static int16_t register_Read_Pilot_Data(TieFile* f, uint8_t* dst, uint16_t count) {
	uint16_t failed = 0;
	uint16_t pos = 0;
	uint8_t buf[64];

	while (count && !failed) {
		uint16_t chunk = count;
		uint16_t i;

		if (chunk > 64)
			chunk = 64;

		failed |= (uint16_t)TieStorage_Read(buf, 1, chunk, f) != chunk;
		if (failed)
			return !failed;
		xtimer_Often();
		for (i = 0; i < chunk;)
			dst[pos++] = buf[i++];
		count -= chunk;
	}
	return !failed;
}

// FUNCTION: TIE95 0x7BF14
// FUNCTION: TIE98 0x471910
static void register_Delete_Pilot_Record(void) {
	FastPilotRecord* rec;
	int16_t logical_count;
	int16_t deleted;
	int16_t i;

	if (!register_fast_pilot_record)
		return;

#ifdef TIE_MODERN
	rec = (FastPilotRecord*)register_fast_pilot_record;
#else
	rec = xmemhdl_Lock_Handle(register_fast_pilot_record);
#endif
	logical_count = 0;
	deleted = 0;

	for (i = 0; i < num_loaded_pilots; i++, rec++) {
		if (rec->name[0]) {
			if (logical_count == pilot_active) {
				deleted = 1;
				rec->name[0] = 0;
			}
			logical_count++;
		}
	}

#ifndef TIE_MODERN
	xmemhdl_Unlock_Handle(register_fast_pilot_record);
#endif

	if (deleted) {
		num_pilots--;
		num_pages = (num_pilots + TIE_FRONTEND_EDITION(10, 12) - 1) / TIE_FRONTEND_EDITION(10, 12);
		if (!num_pages)
			num_pages = 1;
		if (num_pages <= cur_page)
			cur_page = num_pages - 1;
		pilot_offset = TIE_FRONTEND_EDITION(10, 12) * cur_page;
	}
	pilot_active = -1;
	shipext_Init_Pilot();
}

/* ================================================================
 * Pilot name callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7B270
// FUNCTION: TIE98 0x470B70
static void register_iuser_Pilot_Name(Input* input, int32_t time) {
	RegStringButton* btn = (RegStringButton*)input;

	(void)time;
	if (!xinpattr_Get_Input_Selected(&btn->header))
		return;

	shipext_Set_Pilot_Name("");
	register_xuser_Pilot_Name(btn->name);
	if (strlen(btn->name)) {
		pilot_info->var1 = 1;
	} else {
		pilot_info->var1 = 0;
		pilot_info->var2 = 0;
		xinpattr_Refresh_Input(pilot_info);
	}
}

// FUNCTION: TIE95 0x7BFF0
// FUNCTION: TIE98 0x4719F0
static void register_Revive_Pilot_Record(void) {
	char name_buf[TIE_PILOT_NAME_CAPACITY];

	register_Get_Reg_String_Button_Name(pilot_name_input, name_buf);
	shipext_Load_Pilot(name_buf);
	shipext_Revive_Pilot(name_buf);
	register_Revive_Pilot_Info();
}

// FUNCTION: TIE95 0x7C018
// FUNCTION: TIE98 0x471A30
void register_Revive_Pilot_Info(void) {
	FastPilotRecord* rec;
	int16_t i;
	int16_t logical_count;

	if (!register_fast_pilot_record)
		return;
#ifdef TIE_MODERN
	rec = (FastPilotRecord*)register_fast_pilot_record;
#else
	rec = xmemhdl_Lock_Handle(register_fast_pilot_record);
#endif
	logical_count = 0;
	for (i = 0; i < num_loaded_pilots; i++) {
		if (rec->name[0]) {
			if (logical_count == pilot_active)
				rec->lost_status = 0;
			logical_count++;
		}
		rec++;
	}
#ifndef TIE_MODERN
	xmemhdl_Unlock_Handle(register_fast_pilot_record);
#endif
}

// FUNCTION: TIE95 0x7C078
// FUNCTION: TIE98 0x471A90
static void register_Set_Your_Reg_Pilot(void) {
	if (register_fast_pilot_record) {
		char current_name[TIE_PILOT_NAME_CAPACITY];
		FastPilotRecord* rec;
		int16_t i;
#ifdef TIE_MODERN
		shipext_Get_Pilot_Name(current_name, sizeof(current_name));
#else
		shipext_Get_Pilot_Name(current_name);
#endif

#ifdef TIE_MODERN
		rec = (FastPilotRecord*)register_fast_pilot_record;
#else
		rec = xmemhdl_Lock_Handle(register_fast_pilot_record);
#endif
		for (i = 0; i < num_loaded_pilots; i++, rec++) {
			if (pilot_active != -1)
				break;
			if (!strcmp(rec->name, current_name))
				pilot_active = i;
		}
#ifndef TIE_MODERN
		xmemhdl_Unlock_Handle(register_fast_pilot_record);
#endif

		if (pilot_active != -1) {
			register_Index_To_Pilot_Record(pilot_active, &shell_pilot);
			register_Set_Reg_String_Button_Name(&pilot_name_input->header, shell_pilot.name);
			pilot_info->var1 = 1;
		}
	} else {
		shell_pilot.name[0] = 0;
	}

	shipext_Set_Pilot_Name(shell_pilot.name);
}

// FUNCTION: TIE95 0x7C148
// FUNCTION: TIE98 0x471BB0
// HARDENING: modern builds propagate allocation failure and bound the name copy.
static RegStringButton* register_Alloc_Input_Reg_String_Button(Input* parent, Rect* r, int16_t zinput,
															   InputUserFunc user_fn, const char* name,
															   int16_t is_filename_mode, int16_t id) {
	Input* input;
	RegStringButton* btn;
	int16_t i;

	input = xinput_Alloc_Dialog_Input(parent, r, zinput, sizeof(*btn) - sizeof(btn->header));
#ifdef TIE_MODERN
	if (!input)
		return NULL;
#endif
	xinpattr_Set_Input_Draw_Function(input, register_idraw_Reg_String_Button);
	xinpattr_Set_Input_Update_Function(input, register_iupdate_Reg_String_Button);
	xinpattr_Set_Input_User_Function(input, user_fn);
	input->id = id;
	btn = (RegStringButton*)input;
	if (TIE_FRONTEND_TIE98)
		memset(btn->name, 0, sizeof(btn->name));
	for (i = 0; name[i]; i++) {
#ifdef TIE_MODERN
		if (i >= (int16_t)(sizeof(btn->name) - 1))
			break;
#endif
		btn->name[i] = name[i];
	}
	btn->name[i] = 0;
	btn->is_filename_mode = is_filename_mode;
	return (RegStringButton*)input;
}

/* ================================================================
 * RegStringButton draw/update
 * ================================================================ */

// FUNCTION: TIE95 0x7C1BC
// FUNCTION: TIE98 0x471C40
static void register_idraw_Reg_String_Button(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	RegStringButton* btn = (RegStringButton*)input;
	int16_t saved_font = xfont_Get_Font();
	int16_t str_width;

	xfont_Set_Font(TIE_FRONTEND_EDITION(0, 3));
	str_width = xfont_Get_String_Width(btn->name);
	xfont_Set_Font(saved_font);

	if (refresh) {
		int16_t color;

		xstyle_Style_Paint_TextField(frame);
		color = xstyle_Get_Style_Down_Color();
		xfont_Print_Clipped_Text(btn->name, frame->left + 3, frame->top + 3, TIE_FRONTEND_EDITION(0, 3),
								 color);
	}

	if (xinpattr_Is_Input_Active(&btn->header)) {
		int16_t caret_color = xio_Blink() ? xstyle_Get_Style_Down_Color() : 0;
		xpaint_Horiz_Clipped_Line(frame->left + str_width + 4, frame->top + 11, 6, caret_color);
	}

	if (xinpattr_Is_Input_Dirty(&btn->header))
		xdirty_Dirty_Rect(clip);
}

// FUNCTION: TIE95 0x7C288
// FUNCTION: TIE98 0x471D20
static int16_t register_iupdate_Reg_String_Button(Input* input, Rect* bounds, Rect* clip, int16_t key,
												  uint8_t left, uint8_t right, int16_t mouse_x,
												  int16_t mouse_y) {
	RegStringButton* btn = (RegStringButton*)input;
	char work[32];
	int16_t i;
	int16_t len;
	int16_t changed;

	(void)bounds;
	(void)clip;
	(void)left;
	(void)right;
	(void)mouse_x;
	(void)mouse_y;
	for (i = 0; btn->name[i]; i++)
		work[i] = btn->name[i];
	work[i] = 0;
	len = (int16_t)strlen(work);
	changed = 1;

	if (key) {
		xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(280, 536), TIE_FRONTEND_EDITION(115, 274));
		if (key == 0x5300 || key == 8) {
			/* Delete/Backspace */
			if (len)
				work[len - 1] = 0;
		} else if (key >= 32 && key < 127) {
			if ((isalpha)(key)) {
				changed = register_Add_Key_To_Reg_String(btn, work, (int16_t)toupper(key));
			} else if (btn->is_filename_mode) {
				if ((isdigit)(key))
					changed = register_Add_Key_To_Reg_String(btn, work, key);
				else if (key == '_' || key == '-')
					changed = register_Add_Key_To_Reg_String(btn, work, key);
				else
					changed = 0;
			} else {
				if (key == ' ' || key == '\'')
					changed = register_Add_Key_To_Reg_String(btn, work, key);
				else
					changed = 0;
			}
		} else {
			changed = 0;
		}
		if (changed) {
			options_gbl.game_level = 1;
			pilot_record.game_level = 1;
		}
	}

	for (i = 0; work[i]; i++)
		btn->name[i] = work[i];
	btn->name[i] = 0;
	if (changed) {
		xinpattr_Refresh_Input(&btn->header);
		xinpattr_Selected_Input(&btn->header);
	}
	return changed;
}

// FUNCTION: TIE95 0x7C408
// FUNCTION: TIE98 0x471EE0
static int16_t register_Add_Key_To_Reg_String(RegStringButton* btn, char* s, int16_t c) {
	int16_t len = (int16_t)strlen(s);
	int16_t max_len = btn->is_filename_mode ? TIE_FRONTEND_EDITION(8, TIE_PILOT_NAME_MAX) : 20;

	if (len < max_len) {
		s[len] = (char)c;
		s[len + 1] = 0;
		return 1;
	}
	return 0;
}

// FUNCTION: TIE95 0x7C44C
// FUNCTION: TIE98 0x471F30
// HARDENING: modern builds bound the copy to the button name buffer.
static void register_Set_Reg_String_Button_Name(Input* input, const char* src) {
	RegStringButton* btn = (RegStringButton*)input;
	int16_t i;

	for (i = 0; src[i]; i++) {
#ifdef TIE_MODERN
		if (i >= (int16_t)(sizeof(btn->name) - 1))
			break;
#endif
		btn->name[i] = src[i];
	}
	btn->name[i] = 0;
	xinpattr_Refresh_Input(input);
}

/* ================================================================
 * RegStringButton helpers
 * ================================================================ */

// FUNCTION: TIE95 0x7C480
// FUNCTION: TIE98 0x471F70
// HARDENING: modern builds bound the copy to the pilot name buffer callers pass.
static void register_Get_Reg_String_Button_Name(RegStringButton* btn, char* dst) {
	int16_t i;

	for (i = 0; btn->name[i]; i++) {
#ifdef TIE_MODERN
		if (i >= TIE_PILOT_NAME_MAX)
			break;
#endif
		dst[i] = btn->name[i];
	}
	dst[i] = 0;
}

/* The modern build cannot block in the button callback: the first call
 * schedules the dialog and reports cancel, and the register task re-enters
 * the callback with the dialog result once the dialog closes. */
// FUNCTION: TIE95 0x7C4A8
// FUNCTION: TIE98 0x471FA0
static int16_t register_Do_Delete_Dialog(void) {
	Input* the_input;
	int16_t key_buttons;
	int16_t retval;

#ifdef TIE_MODERN
	if (!TieRegister_ResumeDelete(&the_input, &key_buttons, &retval)) {
#endif
		key_buttons = xio_Is_Key_Buttons();
		if (!key_buttons)
			xio_Set_Key_Buttons();
		xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(160, 420), TIE_FRONTEND_EDITION(100, 260));
		the_input = register_Build_Delete_Dialog();
#ifdef TIE_MODERN
		// HARDENING: the original assumes dialog allocation succeeds.
		if (!the_input) {
			TieDiagnostics_Log(TIE_LOG_ERROR, "[REGISTER] could not allocate delete dialog\n");
			if (!key_buttons)
				xio_Clear_Key_Buttons();
			return 2;
		}
		TieRegister_RunDelete(the_input, key_buttons);
		return 2;
	}
#else
	retval = xdialog_Handle_Dialog_View(the_input);
#endif
	xview_Refresh_View();
	xdialog_Clear_Dialog_Exit();
	xinput_Free_Inputs(the_input);
	if (!key_buttons)
		xio_Clear_Key_Buttons();
	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(160, 320), TIE_FRONTEND_EDITION(180, 360));
	return retval;
}

/* ================================================================
 * Delete dialog
 * ================================================================ */

// FUNCTION: TIE95 0x7C50C
// FUNCTION: TIE98 0x472010
// HARDENING: unwinds partial dialog allocation.
static Input* register_Build_Delete_Dialog(void) {
	Rect r;
	Input* dlg;
	PushButton* button;

	xrect_Set_Rect(&r, 0, 0, TIE_FRONTEND_EDITION(180, 360), TIE_FRONTEND_EDITION(46, 110));
	dlg = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
#ifdef TIE_MODERN
	if (!dlg)
		return NULL;
#endif
	xinpattr_Set_Input_Update_Function(dlg, register_iupdate_Delete_Input);
	xinpattr_Set_Input_Draw_Function(dlg, register_idraw_Delete_Input);
	xinpattr_Set_Input_Allign(dlg, 1, 1);
	xinpattr_Show_Input(dlg);

	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(4, 8), TIE_FRONTEND_EDITION(4, 10), TIE_FRONTEND_EDITION(54, 108),
				   TIE_FRONTEND_EDITION(20, 48));
	strcpy(reg_btn_name + 48, textext_Get_Text(txtRegBtnDelete));
	button = xbtnpush_Alloc_Button(dlg, &r, 0, register_iuser_Delete_Input, reg_btn_name + 48, 1);
#ifdef TIE_MODERN
	if (!button) {
		xinput_Free_Inputs(dlg);
		return NULL;
	}
#endif
	xinpattr_Set_Input_Allign(&button->header, 0, 2);

	register_Index_To_Pilot_Record(pilot_active, &shell_pilot);

	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(4, 8), TIE_FRONTEND_EDITION(4, 10), TIE_FRONTEND_EDITION(54, 108),
				   TIE_FRONTEND_EDITION(20, 48));
	strcpy(reg_btn_name + 80, textext_Get_Text(txtRegBtnCancel));
	button = xbtnpush_Alloc_Button(dlg, &r, 0, register_iuser_Delete_Input, reg_btn_name + 80, 2);
#ifdef TIE_MODERN
	if (!button) {
		xinput_Free_Inputs(dlg);
		return NULL;
	}
#endif
	xinpattr_Set_Input_Allign(&button->header, 2, 2);

	return dlg;
}

// FUNCTION: TIE95 0x7C624
// FUNCTION: TIE98 0x472170
// DIVERGENCE: the original joystick-calibration shortcut is not implemented.
static int16_t register_iupdate_Delete_Input(Input* input, Rect* bounds, Rect* clip, int16_t key,
											 uint8_t left, uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)input;
	(void)bounds;
	(void)clip;
	(void)left;
	(void)right;
	(void)mouse_x;
	(void)mouse_y;
	(void)key;
	return 0;
}

// FUNCTION: TIE95 0x7C660
// FUNCTION: TIE98 0x4721B0
static void register_iuser_Delete_Input(Input* input, int32_t time) {
	int16_t id;

	(void)time;
	if (!xinpattr_Get_Input_Selected(input))
		return;
	id = input->id;
	if (id >= 1 && id <= 3)
		xdialog_Set_Dialog_Exit(id);
}

// FUNCTION: TIE95 0x7C688
// FUNCTION: TIE98 0x4721E0
static void register_idraw_Delete_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	char title[64], name_buf[TIE_PILOT_NAME_CAPACITY], display_name[TIE_PILOT_NAME_CAPACITY];
	Rect dst;

	if (!refresh)
		return;

	register_Get_Reg_String_Button_Name(pilot_name_input, name_buf);
	TiePilotName_CopyForDisplay(display_name, sizeof(display_name), name_buf);
	register_Index_To_Pilot_Record(pilot_active, &shell_pilot);

	strcpy(title, textext_Get_Text(txtRegBtnDeletePilot));
	strcat(title, " ");
	strcat(title, display_name);
	strcat(title, "?");

	xrect_Copy_Rect(&dst, frame);
	xpaint_Frame_Clipped_Rect(&dst, 16);
	xrect_Inset_Rect(&dst, 1, 1);
	xstyle_Style_Paint_Border(&dst, 0);
#ifdef TIE_MODERN
	if (TieProfile_UsesTie98Frontend()) {
		int16_t font_h = xfont_Get_FontID_Height(2);
		dst.top += font_h;
		dst.bottom = dst.top + font_h;
	} else {
		dst.bottom = dst.top + 14;
	}
	xfont_Enable_FontID_Shadow(TieProfile_UsesTie98Frontend() ? 2 : 0);
	xfont_Print_Centered_Text(title, &dst, TieProfile_UsesTie98Frontend() ? 2 : 0, 15);
	xfont_Disable_FontID_Shadow(TieProfile_UsesTie98Frontend() ? 2 : 0);
#elif defined(TIE98)
	dst.top += xfont_Get_FontID_Height(2);
	dst.bottom = dst.top + xfont_Get_FontID_Height(2);
	xfont_Enable_FontID_Shadow(2);
	xfont_Print_Centered_Text(title, &dst, 2, 15);
	xfont_Disable_FontID_Shadow(2);
#else
	dst.bottom = dst.top + 14;
	xfont_Enable_FontID_Shadow(0);
	xfont_Print_Centered_Text(title, &dst, 0, 15);
	xfont_Disable_FontID_Shadow(0);
#endif

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}

/* Copy the idx-th register_directory entry name into dst. */
// FUNCTION: TIE95 0x7C770
// FUNCTION: TIE98 0x472390
// PORT: bounds the copy by the wider TIE98 pilot name capacity.
// HARDENING: validates the destination and directory storage.
static int16_t register_Find_Reg_Dir_Name(Directory* dir, char* dst, int16_t idx) {
	const DirEntry* entries;

	if (!dst || !dir->entries || idx < 0 || idx >= dir->count)
		return 0;
	entries = xmemhdl_Lock_Handle(dir->entries);
	if (!entries)
		return 0;
	strncpy(dst, entries[idx].name, TIE_PILOT_NAME_CAPACITY - 1);
	dst[TIE_PILOT_NAME_CAPACITY - 1] = 0;
	xmemhdl_Unlock_Handle(dir->entries);
	return 1;
}

/* Retail TIE95 keeps this dialog but never calls it. The modern build
 * runs it from the register task when copy protection is enabled; the
 * task calls it again after the dialog view finishes. */
// FUNCTION: TIE95 0x7C7AC
int16_t register_Do_Protect_Dialog(void) {
	Input* the_input;
	int16_t retval;
	int16_t i;

#ifdef TIE_MODERN
	if (!TieRegister_ResumeProtect(&the_input)) {
#endif
		protect_count = 0;
		protect_index = 0;
		protect_chosen = 0;
		for (i = 0; i < 3; i++)
			protect_state[i] = reg_cp[i];
		xio_Set_Mouse_Position(160, 135);
		the_input = register_Build_Protect_Dialog();
#ifdef TIE_MODERN
		TieRegister_RunProtect(the_input);
		return 0;
	}
	retval = xdialog_Get_Dialog_Exit();
#else
	retval = xdialog_Handle_Dialog_View(the_input);
#endif
	xview_Refresh_View();
	xdialog_Clear_Dialog_Exit();
	xinput_Free_Inputs(the_input);
	return retval != 2;
}

/* ================================================================
 * Protect (password) dialog
 * ================================================================ */

// FUNCTION: TIE95 0x7C820
// absent from TIE98
static Input* register_Build_Protect_Dialog(void) {
	Rect r;
	Input* dlg;
	Input* sym_input;
	Input* sub_input;
	RegStringButton* pwd;
	PushButton* btn_ok;
	PushButton* btn_exit;
	PushButton* btn_quit;

	xrect_Set_Rect(&r, 0, 0, 112, 86);
	dlg = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	xinpattr_Set_Input_Update_Function(dlg, register_iupdate_Protect_Input);
	xinpattr_Set_Input_Draw_Function(dlg, register_idraw_Protect_Input);
	xinpattr_Set_Input_Allign(dlg, 1, 1);
	xinpattr_Show_Input(dlg);
	dlg->id = 0;
	protect_parent = dlg;

	/* Symbol challenge area */
	xrect_Set_Rect(&r, 4, 14, 108, 37);
	sym_input = xinput_Alloc_Dialog_Input(dlg, &r, 0, 0);
	xinpattr_Set_Input_User_Function(sym_input, register_iuser_Protect_Input);
	xinpattr_Set_Input_Draw_Function(sym_input, register_idraw_Protect_Input);
	xinpattr_Show_Input(sym_input);
	sym_input->id = 3;

	/* Subtitle */
	xrect_Set_Rect(&r, 4, 39, 108, 49);
	sub_input = xinput_Alloc_Dialog_Input(dlg, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(sub_input, register_idraw_Protect_Input);
	xinpattr_Show_Input(sub_input);
	sub_input->id = 6;

	/* Password field (RegStringButton, filename mode=0). */
	xrect_Set_Rect(&r, 4, 51, 108, 65);
	pwd = register_Alloc_Input_Reg_String_Button(dlg, &r, 0, register_iuser_Protect_Input, "", 0, 4);
	xinpattr_Hide_Input(&pwd->header);
	protect_btns_arr[0] = &pwd->header;

	/* OK button */
	xrect_Set_Rect(&r, 4, 4, 32, 20);
	textext_Copy_Text(reg_prot_name, txtRegProtOK);
	btn_ok = xbtnpush_Alloc_Button(dlg, &r, 0, register_iuser_Protect_Input, reg_prot_name, 1);
	xinpattr_Set_Input_Allign(&btn_ok->header, 0, 2);
	xinpattr_Hide_Input(&btn_ok->header);
	protect_btns_arr[1] = &btn_ok->header;

	/* Exit to DOS button */
	xrect_Set_Rect(&r, 4, 4, 76, 20);
	textext_Copy_Text(reg_prot_name + 24, txtRegProtExit);
	btn_exit = xbtnpush_Alloc_Button(dlg, &r, 0, register_iuser_Protect_Input, reg_prot_name + 24, 2);
	xinpattr_Set_Input_Allign(&btn_exit->header, 2, 2);
	xinpattr_Hide_Input(&btn_exit->header);
	protect_btns_arr[2] = &btn_exit->header;

	/* Press to Continue button */
	xrect_Set_Rect(&r, 4, 4, 108, 20);
	textext_Copy_Text(reg_prot_name + 48, txtRegProtPress);
	btn_quit = xbtnpush_Alloc_Button(dlg, &r, 0, register_iuser_Protect_Input, reg_prot_name + 48, 5);
	xinpattr_Set_Input_Allign(&btn_quit->header, 0, 2);

	return dlg;
}

// FUNCTION: TIE95 0x7CA58
// absent from TIE98
static int16_t register_iupdate_Protect_Input(Input* input, Rect* bounds, Rect* clip, int16_t key,
											  uint8_t left, uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)input;
	(void)left;
	(void)right;
	(void)mouse_x;
	(void)mouse_y;
	if (!key)
		return 0;

	if (key == 13) {
		xinpattr_Selected_Input(protect_btns_arr[1]);
	}
	(void)bounds;
	(void)clip;
	return 0;
}

// FUNCTION: TIE95 0x7CAC0
// absent from TIE98
static void register_iuser_Protect_Input(Input* input, int32_t time) {
	int16_t i;
	(void)time;

	/* On first frame for symbol display (id=3), pick random question */
	if (input->id == 3 && !protect_chosen) {
		int16_t i;

		protect_index = rand() % 29;
		for (i = 0; i < 3; i++)
			protect_state[i] = reg_cp[3 * protect_index + i];
		xinpattr_Refresh_Input(input);
	}

	if (!xinpattr_Get_Input_Selected(input))
		return;

	switch (input->id) {
		case 1: { /* Continue / check answer */
			RegStringButton* pwd = (RegStringButton*)protect_btns_arr[0];
			char buf[32];
			int16_t i;

			strcpy(buf, pwd->name);
			/* Lowercase the answer */
			for (i = 0; buf[i]; i++)
				buf[i] = (char)tolower(buf[i]);

			if (strcmp(buf, reg_cp_name[protect_index]) == 0 || strcmp(buf, "evarobinyali") == 0) {
				/* Correct answer — exit dialog with input's id */
				xdialog_Set_Dialog_Exit(input->id);
			} else {
				/* Wrong answer — allow up to 3 attempts */
				if (++protect_count >= 3) {
					xdialog_Set_Dialog_Exit(2);
				} else {
					pwd->name[0] = 0;
					xinpattr_Refresh_Input(&pwd->header);
					xinpattr_Refresh_Input(protect_parent);
				}
			}
			break;
		}
		case 2: /* Exit to DOS */
			xdialog_Set_Dialog_Exit(2);
			break;
		case 4: /* Password field clicked */
			xio_Set_Mouse_Position(120, 130);
			break;
		case 5: /* Quit/Skip */
			protect_chosen = 1;
			xinpattr_Hide_Input(input);
			for (i = 0; i < 3; i++)
				xinpattr_Show_Input(protect_btns_arr[i]);
			xinpattr_Refresh_Input(protect_parent);
			break;
		default:
			break;
	}
}

// FUNCTION: TIE95 0x7CC78
// absent from TIE98
static void register_idraw_Protect_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	int16_t id;

	if (!refresh)
		return;

	id = input->id;
	if (id == 0) {
		/* Title bar */
		Rect tr;
		char buf[32];

		xrect_Copy_Rect(&tr, frame);
		xpaint_Frame_Clipped_Rect(&tr, 16);
		xrect_Inset_Rect(&tr, 1, 1);
		xstyle_Style_Paint_Border(&tr, 0);
		tr.bottom = tr.top + 14;
		xfont_Enable_FontID_Shadow(0);

		textext_Copy_Text(buf, txtRegProtCopy);
		xfont_Print_Centered_Text(buf, &tr, 0, 15);
		xfont_Disable_FontID_Shadow(0);
	} else if (id == 3) {
		/* Symbol challenge */
		int16_t x_off = 0;
		int16_t i;

		for (i = 0; i < 3; i++) {
			Rect tr;
			xrect_Copy_Rect(&tr, frame);
			tr.left += x_off;
			tr.right = tr.left + 32;
			xstyle_Style_Paint_TextField(&tr);
			xrect_Inset_Rect(&tr, 2, 2);
			xactor_Set_Actor_State(symbols, protect_state[i], 0);
			xactanim_Draw_Anim_Actor(symbols, frame, clip, tr.left + 4, tr.top + 1, 1);
			x_off += 36;
		}
	} else if (id == 6 && protect_chosen) {
		/* "See Manual Page N" */
		char fmt[32], buf[32];
		textext_Copy_Text(fmt, txtRegProtManual);
		snprintf(buf, sizeof(buf), fmt, protect_index + 4);
		xfont_Enable_FontID_Shadow(0);
		xfont_Print_Centered_Text(buf, frame, 0, 15);
		xfont_Disable_FontID_Shadow(0);
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}
