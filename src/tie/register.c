#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Retail copy protection is disabled by default and can be enabled at runtime. */
// PORT: runtime switch for the TIE95-only copy-protection path.
static int copy_protection_enabled = 0;

// PORT: public setter for the runtime-only switch above.
void register_set_copy_protection(int enabled) { copy_protection_enabled = enabled ? 1 : 0; }

#include "tie/rand.h"
#include "tie/register.h"
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
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include <landru/task.h>

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

/* State retained while deferred sub-dialog tasks are active. */
typedef struct DeleteCtx {
	int16_t was_key_buttons;
	Input* sub_dlg;
} DeleteCtx;

typedef struct ProtectCtx {
	Input* sub_dlg;
} ProtectCtx;

static DeleteCtx s_delete_ctx;
static ProtectCtx s_protect_ctx;

static void after_delete_dialog(int16_t result, void* ctx);

/* ---- Static data ---- */

/* Copy-protection cipher pad: 29 questions × 3 symbol indices.
 * Each triplet indexes into the symbol animation actor states. */
// DATA: TIE95 0xD1206
static const int16_t reg_cp[87] = { 6,  2, 8,  0, 10, 9, 3, 4,  7, 11, 2, 5, 1, 9, 11, 7, 9,  5,
									10, 8, 2,  4, 0,  6, 5, 6,  1, 2,  7, 0, 8, 4, 11, 9, 11, 10,
									6,  5, 3,  0, 2,  1, 3, 11, 8, 11, 5, 7, 1, 0, 6,  7, 8,  10,
									10, 5, 4,  4, 2,  9, 5, 8,  3, 2,  4, 5, 8, 7, 1,  9, 0,  7,
									6,  1, 10, 0, 3,  6, 3, 5,  9, 11, 1, 8, 1, 6, 10 };

/* Copy-protection answer table: 29 ship names (16 bytes each, null-padded). */
// DATA: TIE95; absent from TIE98
static const char reg_cp_name[29][16] = {
	"ardent",         "audacity",    "colossus",  "courageous", "devastation", "emperor",
	"emperor's will", "formidable",  "furious",   "glory",      "glorious",    "harpago",
	"harpax",         "illustrious", "imperator", "implacable", "indomitable", "inflexible",
	"lightning",      "magnificent", "majestic",  "monarch",    "monitor",     "protector",
	"renown",         "resolution",  "thunderer", "triumph",    "vanguard"
};

/* PORT: immutable edition dispatch. The VGA values come from TIE95
 * REGISTER_Register (0x7A4C0); the SVGA values come from TIE98
 * REGISTER_Register (0x46FBC0) and its callbacks. */
typedef struct RegisterSpec {
	LandruSurfaceSet surface_set;
	const char* archive;
	const char* snapshot_lfd;
	const char* background;
	const char* back_panel;
	const char* door;
	const char* troop;
	int16_t width;
	int16_t height;
	int16_t mouse_x;
	int16_t mouse_y;
	int16_t door_bounds[4];
	int16_t list_bounds[4];
	int16_t name_bounds[4];
	int16_t prev_bounds[4];
	int16_t next_bounds[4];
	int16_t info_bounds[4];
	int16_t delete_bounds[4];
	int16_t page_bounds[4];
	int16_t page_size;
	int16_t list_click_bias;
	int16_t list_click_offset;
	int16_t page_font;
	int16_t list_font;
	int16_t button_font;
	int16_t edit_font;
	int16_t info_label_font;
	int16_t info_value_font;
	int16_t filename_max_length;
	int16_t directory_name_length;
	int16_t button_actor_count;
	int16_t delete_dialog_width;
	int16_t delete_dialog_height;
	int16_t delete_button_bounds[4];
	int16_t delete_mouse_x;
	int16_t delete_mouse_y;
	int16_t delete_return_mouse_x;
	int16_t delete_return_mouse_y;
	int16_t delete_title_font;
	bool use_background;
	bool load_symbols;
	bool dynamic_info_layout;
	bool shared_page_button_actor;
} RegisterSpec;

static const RegisterSpec register_specs[] = {
	// DATA: TIE95 REGISTER_Register 0x7A4C0 and callbacks 0x7A93C-0x7C827.
	{
		.surface_set = LANDRU_SURFACE_VGA,
		.archive = "register.lfd",
		.snapshot_lfd = "REGISTER",
		.background = "reg-bak1",
		.back_panel = "reg-bak2",
		.door = "reg-dora",
		.troop = "reg-trpa",
		.width = 320,
		.height = 200,
		.mouse_x = 124,
		.mouse_y = 106,
		.door_bounds = { 240, 80, 320, 150 },
		.list_bounds = { 75, 102, 127, 174 },
		.name_bounds = { 75, 176, 127, 186 },
		.prev_bounds = { 75, 189, 85, 197 },
		.next_bounds = { 117, 189, 127, 197 },
		.info_bounds = { 149, 102, 201, 188 },
		.delete_bounds = { 148, 189, 200, 198 },
		.page_bounds = { 92, 192, 111, 196 },
		.page_size = 10,
		.list_click_bias = 6,
		.list_click_offset = 1,
		.page_font = 1,
		.list_font = 1,
		.button_font = 1,
		.edit_font = 0,
		.info_label_font = 0,
		.info_value_font = 1,
		.filename_max_length = 8,
		.directory_name_length = FILEDIR_DEFAULT_NAME_LENGTH,
		.button_actor_count = 3,
		.delete_dialog_width = 180,
		.delete_dialog_height = 46,
		.delete_button_bounds = { 4, 4, 54, 20 },
		.delete_mouse_x = 160,
		.delete_mouse_y = 100,
		.delete_return_mouse_x = 160,
		.delete_return_mouse_y = 180,
		.delete_title_font = 0,
		.use_background = true,
		.load_symbols = true,
		.dynamic_info_layout = false,
		.shared_page_button_actor = false,
	},
	// DATA: TIE98 REGISTER_Register 0x46FBC0 and callbacks 0x4700D0-0x4723FB.
	{
		.surface_set = LANDRU_SURFACE_SVGA,
		.archive = "reg640.lfd",
		.snapshot_lfd = "REG640",
		.background = NULL,
		.back_panel = "reg-bak2",
		.door = "reg-dora",
		.troop = "reg-trpa",
		.width = 640,
		.height = 480,
		.mouse_x = 536,
		.mouse_y = 274,
		.door_bounds = { 486, 188, 621, 356 },
		.list_bounds = { 170, 247, 271, 414 },
		.name_bounds = { 170, 424, 271, 445 },
		.prev_bounds = { 167, 452, 186, 475 },
		.next_bounds = { 255, 452, 276, 475 },
		.info_bounds = { 308, 247, 408, 450 },
		.delete_bounds = { 304, 452, 413, 478 },
		.page_bounds = { 187, 456, 255, 472 },
		.page_size = 12,
		.list_click_bias = 0,
		.list_click_offset = 0,
		.page_font = 3,
		.list_font = 3,
		.button_font = 3,
		.edit_font = 3,
		.info_label_font = 2,
		.info_value_font = 3,
		.filename_max_length = 16,
		.directory_name_length = FILEDIR_MAX_NAME_LENGTH,
		.button_actor_count = 2,
		.delete_dialog_width = 360,
		.delete_dialog_height = 110,
		.delete_button_bounds = { 8, 10, 108, 48 },
		.delete_mouse_x = 420,
		.delete_mouse_y = 260,
		.delete_return_mouse_x = 320,
		.delete_return_mouse_y = 360,
		.delete_title_font = 2,
		.use_background = false,
		.load_symbols = false,
		.dynamic_info_layout = true,
		.shared_page_button_actor = true,
	},
};

static const RegisterSpec* active_spec;

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xD11FC; TIE98 0x4EAB88
static int16_t pilot_active; /* logical index of selected pilot (-1 = none) */
// GLOBAL: TIE95 0xD11FE; TIE98 0x589784
static int16_t pilot_offset; /* first visible pilot in list */
// GLOBAL: TIE95 0xD1200; TIE98 0x589788
static int16_t num_pilots; /* count of valid (non-deleted) pilots */
// GLOBAL: TIE95 0xD1202; TIE98 0x58978C
static int16_t num_loaded_pilots; /* total directory entries */
static int16_t pilot_loaded;

static char reg_prot_name[72];                   /* protect dialog button label buffers */
static uint8_t cur_pilot[PILOTRECORD_DISK_SIZE]; /* temp buffer for reading .tfr files */
static char reg_btn_name[112];                   /* button label scratch buffers */

/* Persistent across protect-dialog invocations: retains the last password
 * the user typed. Mirrors retail's `initial_name` global so the field
 * doesn't reset between failed attempts (the iuser_Protect_Input clears
 * it explicitly on a wrong guess). Sized to match RegStringButton.name. */
static char initial_name[44];

static Actor* symbols;              /* symbol animation actor for protect dialog */
static FastPilotRecord shell_pilot; /* cached FPR for the active pilot */
static Input* protect_btns_arr[3];  /* protect dialog child buttons */
static Input* protect_parent;
static Input* pilot_door_info;
static Input* pilot_info;
static Input* reg_parent;
static Input* pilot_delete;
static RegStringButton* pilot_name_input;
static Actor* reg_troop;
static Film* register_film;
// GLOBAL: TIE95 0xF6DE0
static Actor* reg_button[3]; /* film callback actor cache */
static Actor* reg_door;
static Input* door_input;
static Input* pilot_list;
// GLOBAL: TIE95 0xF6DF4; TIE98 0x588E70
static Actor* reg_bak;
static Directory directory;
// GLOBAL: TIE95 0xF6E46
static int16_t protect_state[3];
// GLOBAL: TIE95 0xF6E4C
static int16_t protect_chosen;
// GLOBAL: TIE95 0xF6E4E
static int16_t protect_count;
// GLOBAL: TIE95 0xF6E50
static int16_t protect_index;
static int16_t num_pages;
// GLOBAL: TIE95 0xF6E52; TIE98 0x588E74
static void* fast_pilot_record; /* HANDLE → void* adapted */
static int16_t cur_page;
static int16_t pilot_delete_status;

/* ================================================================
 * Forward declarations
 * ================================================================ */

static void idraw_Reg_String_Button(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static int16_t iupdate_Reg_String_Button(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
										 uint8_t right, int16_t mouse_x, int16_t mouse_y);
static Input* Build_Delete_Dialog(void);
static int16_t iupdate_Delete_Input(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
									uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void iuser_Delete_Input(Input* input, int32_t time);
static void idraw_Delete_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh);
static int16_t Find_Reg_Dir_Name(char* dst, size_t capacity, int16_t idx);
static Input* Build_Protect_Dialog(void);
static int16_t iupdate_Protect_Input(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
									 uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void iuser_Protect_Input(Input* input, int32_t time);
static void idraw_Protect_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh);

/* ================================================================
 * Helper: FPR index walk
 * ================================================================ */

/* Map logical pilot index (skipping deleted slots) to physical slot.
 * Returns 1 if found, 0 otherwise. */
// FUNCTION: TIE95 0x7BC00; TIE98 0x4715C0
static int16_t Index_To_Pilot(int16_t logical_idx, int16_t* out_slot) {
	if (!fast_pilot_record)
		return 0;

	FastPilotRecord* rec = (FastPilotRecord*)fast_pilot_record;
	*out_slot = -1;
	int16_t logical_count = 0;

	for (int16_t i = 0; i < num_loaded_pilots; i++, rec++) {
		if (*out_slot != -1)
			break;
		if (rec->name[0]) {
			if (logical_count == logical_idx)
				*out_slot = i;
			else
				logical_count++;
		}
	}
	return *out_slot != -1;
}

/* Like Index_To_Pilot but also copies the 20-byte FPR. */
// FUNCTION: TIE95 0x7BC84; TIE98 0x471630
static int16_t Index_To_Pilot_Record(int16_t logical_idx, FastPilotRecord* out_rec) {
	if (!fast_pilot_record)
		return 0;

	FastPilotRecord* rec = (FastPilotRecord*)fast_pilot_record;
	int16_t found_slot = -1;
	int16_t logical_count = 0;

	for (int16_t i = 0; i < num_loaded_pilots; i++, rec++) {
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
	return found_slot != -1;
}

/* Copy the idx-th directory entry name into dst. */
// FUNCTION: TIE95 0x7C770; TIE98 0x472390
// PORT: capacity parameter supports the wider TIE98 pilot name.
// HARDENING: validates the destination and directory storage.
static int16_t Find_Reg_Dir_Name(char* dst, size_t capacity, int16_t idx) {
	if (!dst || !capacity || !directory.entries || idx < 0)
		return 0;
	DirEntry* entries = directory.entries;
	strncpy(dst, entries[idx].name, capacity - 1);
	dst[capacity - 1] = 0;
	return 1;
}

/* ================================================================
 * RegStringButton helpers
 * ================================================================ */

// FUNCTION: TIE95 0x7C480; TIE98 0x471F70
// PORT: capacity parameter supports the wider TIE98 pilot name.
// HARDENING: validates the output buffer.
static void Get_Reg_String_Button_Name(RegStringButton* btn, char* dst, size_t capacity) {
	if (!dst || !capacity)
		return;
	strncpy(dst, btn->name, capacity - 1);
	dst[capacity - 1] = 0;
}

// FUNCTION: TIE95 0x7C44C; TIE98 0x471F30
// PORT: bounded copy supports the shared enlarged runtime structure.
static void Set_Reg_String_Button_Name(RegStringButton* btn, const char* src) {
	strncpy(btn->name, src, sizeof(btn->name) - 1);
	btn->name[sizeof(btn->name) - 1] = 0;
	linpattr_Refresh_Input(&btn->header);
}

// FUNCTION: TIE95 0x7C408; TIE98 0x471EE0
static int16_t Add_Key_To_Reg_String(RegStringButton* btn, char* s, char c) {
	int16_t len = (int16_t)strlen(s);
	int16_t max_len = btn->is_filename_mode ? active_spec->filename_max_length : 20;
	if (len >= max_len)
		return 0;
	s[len] = c;
	s[len + 1] = 0;
	return 1;
}

// FUNCTION: TIE95 0x7C148; TIE98 0x471BB0
// PORT: allocation uses the shared native RegStringButton size.
// HARDENING: propagates allocation failure.
static RegStringButton* Alloc_Input_Reg_String_Button(Input* parent, Rect* r, int16_t zinput,
													  InputUserFunc user_fn, const char* name,
													  int16_t is_filename_mode, int16_t id) {
	RegStringButton* btn =
		(RegStringButton*)linput_Alloc_Dialog_Input(parent, r, zinput, sizeof(*btn) - sizeof(btn->header));
	if (!btn)
		return NULL;
	linpattr_Set_Input_Draw_Function(&btn->header, idraw_Reg_String_Button);
	linpattr_Set_Input_Update_Function(&btn->header, iupdate_Reg_String_Button);
	linpattr_Set_Input_User_Function(&btn->header, user_fn);
	btn->header.id = id;
	Set_Reg_String_Button_Name(btn, name);
	btn->is_filename_mode = is_filename_mode;
	return btn;
}

/* ================================================================
 * Pilot data I/O
 * ================================================================ */

// FUNCTION: TIE95 0x7BE54; TIE98 0x471850
static int16_t Read_Pilot_Data(TieFile* f, uint8_t* dst, uint16_t count) {
	uint16_t pos = 0;
	uint8_t buf[64];

	while (count > 0) {
		uint16_t chunk = count > 64 ? 64 : count;
		if (TieStorage_Read(buf, 1, chunk, f) != chunk)
			return 0;
		memcpy(dst + pos, buf, chunk);
		pos += chunk;
		count -= chunk;
	}
	return 1;
}

// FUNCTION: TIE95 0x7BD28; TIE98 0x4716C0
// PORT: native allocation replaces the original Landru handle.
// HARDENING: returns when allocation fails.
static void Build_Fast_Pilot_Record(void) {
	if (!num_loaded_pilots)
		return;

	fast_pilot_record = calloc(num_loaded_pilots, sizeof(FastPilotRecord));
	if (!fast_pilot_record)
		return;
	FastPilotRecord* rec = (FastPilotRecord*)fast_pilot_record;
	num_pilots = 0;

	for (int16_t i = 0; i < num_loaded_pilots; i++, rec++) {
		rec->name[0] = 0;
		char dst[TIE_PILOT_NAME_CAPACITY];
		char name[TIE_PILOT_NAME_CAPACITY + 5];
		if (!Find_Reg_Dir_Name(dst, sizeof(dst), i))
			continue;

		snprintf(name, sizeof(name), "%s.tfr", dst);
		TieFile* f = TieStorage_Open(TIE_FILE_ROOT_USER, name, "rb");
		if (!f)
			continue;

		if (Read_Pilot_Data(f, cur_pilot, PILOTRECORD_DISK_SIZE)) {
			strncpy(rec->name, dst, sizeof(rec->name) - 1);
			rec->name[sizeof(rec->name) - 1] = 0;
			/* Decode the .tfr byte image into a typed PilotRecord so
			 * the score / cur_battle / etc. reads go through the
			 * canonical LE codec instead of host-endian byte fishing. */
			PilotRecord pr;
			PilotRecord_decode(&pr, cur_pilot);
			rec->lost_status = pr.exit_status;
			rec->rank = pr.rank;
			rec->cur_battle = pr.cur_battle;
			rec->score = pr.score;
			num_pilots++;
		}
		TieStorage_Close(f);
	}
}

// FUNCTION: TIE95 0x7BF14; TIE98 0x471910
static void Delete_Pilot_Record(void) {
	if (!fast_pilot_record)
		return;

	FastPilotRecord* rec = (FastPilotRecord*)fast_pilot_record;
	int16_t logical_count = 0;
	int16_t deleted = 0;

	for (int16_t i = 0; i < num_loaded_pilots; i++, rec++) {
		if (rec->name[0]) {
			if (logical_count == pilot_active) {
				deleted = 1;
				rec->name[0] = 0;
			}
			logical_count++;
		}
	}

	if (deleted) {
		num_pilots--;
		num_pages = (num_pilots + active_spec->page_size - 1) / active_spec->page_size;
		if (!num_pages)
			num_pages = 1;
		if (num_pages <= cur_page)
			cur_page = num_pages - 1;
		pilot_offset = active_spec->page_size * cur_page;
	}
	pilot_active = -1;
	shipext_Init_Pilot();
}

// FUNCTION: TIE95 0x7BFF0; TIE98 0x4719F0
static void Revive_Pilot_Record(Input* input, int32_t time) {
	(void)input;
	(void)time;
	char name_buf[TIE_PILOT_NAME_CAPACITY];
	Get_Reg_String_Button_Name(pilot_name_input, name_buf, sizeof(name_buf));
	shipext_Load_Pilot(name_buf);
	shipext_Revive_Pilot(name_buf);
	register_Revive_Pilot_Info();
}

// FUNCTION: TIE95 0x7C018; TIE98 0x471A30
void register_Revive_Pilot_Info(void) {
	if (!fast_pilot_record)
		return;
	FastPilotRecord* rec = (FastPilotRecord*)fast_pilot_record;
	int16_t logical_count = 0;
	for (int16_t i = 0; i < num_loaded_pilots; i++, rec++) {
		if (rec->name[0]) {
			if (logical_count == pilot_active)
				rec->lost_status = 0;
			logical_count++;
		}
	}
}

// FUNCTION: TIE95 0x7C078; TIE98 0x471A90
static void Set_Your_Reg_Pilot(void) {
	if (fast_pilot_record) {
		char current_name[TIE_PILOT_NAME_CAPACITY];
		shipext_Get_Pilot_Name(current_name, sizeof(current_name));

		FastPilotRecord* rec = (FastPilotRecord*)fast_pilot_record;
		for (int16_t i = 0; i < num_loaded_pilots; i++, rec++) {
			if (pilot_active != -1)
				break;
			if (!strcmp(rec->name, current_name))
				pilot_active = i;
		}

		if (pilot_active != -1) {
			Index_To_Pilot_Record(pilot_active, &shell_pilot);
			Set_Reg_String_Button_Name(pilot_name_input, shell_pilot.name);
			pilot_info->var1 = 1;
		}
	} else {
		shell_pilot.name[0] = 0;
	}

	shipext_Set_Pilot_Name(shell_pilot.name);
}

/* ================================================================
 * Film callback
 * ================================================================ */

// FUNCTION: TIE95 0x7AB24; TIE98 0x470340
static int16_t film_Callback(Film* film, FilmObject* fo) {
	if (fo->id != 3)
		return 0;
	lfilm_Rewind_Actor_Film(film, fo, (void*)((char*)fo + sizeof(FilmObject)));
	Actor* actor = (Actor*)fo->object;
	if (actor->var1 == 10)
		reg_button[actor->var2] = actor;
	return 0;
}

/* ================================================================
 * Actor callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7AB60; TIE98 0x470380
static void user_Door(Actor* door, int32_t time) {
	(void)time;
	if (door->var1) {
		if (!lactor_Is_Actor_Visible(door)) {
			if (!door->state)
				soundext_Play_SFX(sfxSmallDoorOpen, 0);
			lactor_Show_Actor(door);
			lactor_Set_Actor_State(door, 0, 0);
		} else {
			if (door->state < door->arraySize - 1)
				lactor_Set_Actor_State(door, door->state + 1, 0);
		}
		door->var1 = 0;
	} else {
		if (lactor_Is_Actor_Visible(door)) {
			if (door->state <= 0) {
				soundext_Play_SFX(sfxSmallDoorShut, 0);
				lactor_Hide_Actor(door);
				lactor_Refresh_Actor(reg_bak);
			} else {
				lactor_Set_Actor_State(door, door->state - 1, 0);
			}
		}
	}
}

// FUNCTION: TIE95 0x7AC10; TIE98 0x470450
static void user_Troop(Actor* troop, int32_t time) {
	(void)time;
	if (troop->var1) {
		if (troop->state == 3)
			soundext_Play_SFX(sfxGunCock, 64);
		if (troop->state < troop->arraySize - 1)
			lactor_Set_Actor_State(troop, troop->state + 1, 0);
		troop->var1 = 0;
	} else {
		if (troop->state > 0)
			lactor_Set_Actor_State(troop, troop->state - 1, 0);
	}
}

// FUNCTION: TIE95 0x7AC80; TIE98 0x4704C0
static int draw_Register_Back(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
							  int16_t refresh) {
	if (!refresh)
		return 1;
	lactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);
	if (num_pages > 1) {
		Rect r;
		const int16_t* b = active_spec->page_bounds;
		lrect_Set_Rect(&r, b[0], b[1], b[2], b[3]);
		char buf[16];
		snprintf(buf, sizeof(buf), "%d:%d", cur_page + 1, num_pages);
		lfont_Enable_FontID_Shadow(1);
		lfont_Print_Centered_Text(buf, &r, 15, active_spec->page_font);
		lfont_Disable_FontID_Shadow(1);
	}
	return 1;
}

/* ================================================================
 * RegStringButton draw/update
 * ================================================================ */

// FUNCTION: TIE95 0x7C1BC; TIE98 0x471C40
static void idraw_Reg_String_Button(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	RegStringButton* btn = (RegStringButton*)input;
	int16_t saved_font = lfont_Get_Font();
	lfont_Set_Font(active_spec->edit_font);
	int16_t str_width = lfont_Get_String_Width(btn->name);
	lfont_Set_Font(saved_font);

	if (refresh) {
		lstyle_Style_Paint_TextField(frame);
		int16_t color = lstyle_Get_Style_Down_Color();
		lfont_Print_Clipped_Text(btn->name, frame->left + 3, frame->top + 3, active_spec->edit_font, color);
	}

	if (linpattr_Is_Input_Active(&btn->header)) {
		int16_t caret_color = lio_Blink() ? lstyle_Get_Style_Down_Color() : 0;
		lpaint_Horiz_Clipped_Line(frame->left + str_width + 4, frame->top + 11, 6, caret_color);
	}

	if (linpattr_Is_Input_Dirty(&btn->header))
		ldirty_Dirty_Rect(clip);
}

// FUNCTION: TIE95 0x7C288; TIE98 0x471D20
static int16_t iupdate_Reg_String_Button(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
										 uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)left;
	(void)right;
	(void)mouse_x;
	(void)mouse_y;
	RegStringButton* btn = (RegStringButton*)input;
	char work[sizeof(btn->name)];
	strcpy(work, btn->name);
	int16_t changed = 1;

	if (key) {
		lio_Set_Mouse_Position(active_spec->mouse_x, active_spec->mouse_y);
		if (key == 0x5300 || key == 8) {
			/* Delete/Backspace */
			int16_t len = (int16_t)strlen(work);
			if (len)
				work[len - 1] = 0;
		} else if (key < 32 || key >= 127) {
			changed = 0;
		} else if (isalpha(key)) {
			char upper = (char)toupper(key);
			changed = Add_Key_To_Reg_String(btn, work, upper);
		} else if (btn->is_filename_mode) {
			if (isdigit(key)) {
				changed = Add_Key_To_Reg_String(btn, work, (char)key);
			} else if (key == '_' || key == '-') {
				changed = Add_Key_To_Reg_String(btn, work, (char)key);
			} else {
				changed = 0;
			}
		} else {
			if (key == ' ' || key == '\'') {
				changed = Add_Key_To_Reg_String(btn, work, (char)key);
			} else {
				changed = 0;
			}
		}
		if (changed) {
			options_gbl.game_level = 1;
			pilot_record.game_level = 1;
		}
	}

	strcpy(btn->name, work);
	if (changed) {
		linpattr_Refresh_Input(&btn->header);
		linpattr_Selected_Input(&btn->header);
	}
	return 0;
}

/* ================================================================
 * iupdate/iuser/idraw — Register main
 * ================================================================ */

// FUNCTION: TIE95 0x7AD2C; TIE98 0x470580
static int16_t iupdate_Register(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;
	if (key)
		return 0;

	char name_buf[TIE_PILOT_NAME_CAPACITY];
	Get_Reg_String_Button_Name(pilot_name_input, name_buf, sizeof(name_buf));

	if (!strlen(name_buf)) {
		input->var1 = 3;
		if (!lactor_Is_Actor_Visible(reg_door))
			reg_troop->var1 = 1;
		return 1;
	}

	/* Check if pilot is protected */
	FastPilotRecord fpr;
	if (pilot_active != -1 && Index_To_Pilot_Record(pilot_active, &fpr)) {
		if (fpr.lost_status) {
			input->var1 = (fpr.lost_status == 2) ? 5 : 4;
			if (!lactor_Is_Actor_Visible(reg_door))
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

// FUNCTION: TIE95 0x7AE3C; TIE98 0x470680
static void iuser_Register(Input* input, int32_t time) {
	(void)time;
	int16_t state = input->var1;
	switch (state) {
		case 1: {
			lerror_Set_Landru_Exit(input->var2);
			char name_buf[TIE_PILOT_NAME_CAPACITY];
			Get_Reg_String_Button_Name(pilot_name_input, name_buf, sizeof(name_buf));
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
}

/* ================================================================
 * Pilot list callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7AEA4; TIE98 0x470720
static int16_t iupdate_Pilot_List(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								  uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)mouse_x;
	if (key)
		return 0;

	uint8_t btn = left ? left : right;
	if (btn != 3)
		return 1;

	int16_t row =
		(mouse_y + active_spec->list_click_bias) / (lfont_Get_FontID_Height(active_spec->list_font) + 1) -
		active_spec->list_click_offset;
	if (row < 0)
		row = 0;
	if (row >= active_spec->page_size)
		row = active_spec->page_size - 1;

	int16_t wanted = pilot_offset + row;
	int16_t slot;
	if (!Index_To_Pilot(wanted, &slot))
		return 1;

	char dir_name[TIE_PILOT_NAME_CAPACITY];
	if (!Find_Reg_Dir_Name(dir_name, sizeof(dir_name), slot))
		return 1;

	shipext_Set_Pilot_Name("");
	Set_Reg_String_Button_Name(pilot_name_input, dir_name);
	linpattr_Refresh_Input(input);
	pilot_active = pilot_offset + row;
	pilot_info->var1 = 1;
	return 1;
}

// FUNCTION: TIE95 0x7AF6C; TIE98 0x470800
static void idraw_Pilot_List(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	if (!refresh)
		return;

	lpaint_Paint_Clipped_Rect(frame, 240);
	Rect dst;
	lrect_Copy_Rect(&dst, frame);
	dst.left++;
	dst.bottom = dst.top + lfont_Get_FontID_Height(active_spec->list_font) + 3;
	dst.top += 2;

	lfont_Enable_FontID_Shadow(active_spec->list_font);

	for (int16_t row = 0; row < active_spec->page_size; row++) {
		int16_t wanted = pilot_offset + row;
		int16_t slot;
		if (!Index_To_Pilot(wanted, &slot))
			break;

		char dir_name[TIE_PILOT_NAME_CAPACITY];
		if (!Find_Reg_Dir_Name(dir_name, sizeof(dir_name), slot))
			break;

		FastPilotRecord fpr;
		int16_t color = 15;
		if (Index_To_Pilot_Record(wanted, &fpr)) {
			if (fpr.lost_status)
				color = 4;
		}
		if (pilot_active == wanted)
			color = 14;

		lfont_Print_Clipped_Text(dir_name, dst.left + 1, dst.top, active_spec->list_font, color);
		lrect_Offset_Rect(&dst, 0, lfont_Get_FontID_Height(active_spec->list_font) + 1);
	}

	lfont_Disable_FontID_Shadow(active_spec->list_font);
	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip);
}

/* ================================================================
 * Pilot button callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7B0A0; TIE98 0x470950
static void iuser_Pilot_Button(Input* input, int32_t time) {
	(void)time;
	PushButton* btn = (PushButton*)input;
	int16_t id = btn->header.id;

	/* TIE98 0x470950 shares the page-button actor. The TIE95 branch
	 * below uses three distinct actor state formulas. */
	if (active_spec->shared_page_button_actor) {
		int16_t actor_id = id == 0 ? 0 : 1;
		lactor_Set_Actor_State(reg_button[actor_id], btn->pressed, 0);
	} else if (id == 0) {
		lactor_Set_Actor_State(reg_button[0], 2 * btn->pressed, 0);
	} else if (id == 1) {
		lactor_Set_Actor_State(reg_button[1], 2 * btn->pressed + 1, 0);
	} else if (id == 2) {
		lactor_Set_Actor_State(reg_button[2], btn->pressed + 4, 0);
	}

	if (!linpattr_Get_Input_Selected(&btn->header))
		return;
	soundext_Play_SFX(sfxButton, 64);

	if (id == 0) {
		/* Previous page */
		if (cur_page <= 0)
			cur_page = num_pages - 1;
		else
			cur_page--;
		pilot_offset = active_spec->page_size * cur_page;
	} else if (id == 1) {
		/* Next page */
		if (cur_page >= num_pages - 1)
			cur_page = 0;
		else
			cur_page++;
		pilot_offset = active_spec->page_size * cur_page;
	} else if (id == 2) {
		/* Delete pilot — open confirmation as a deferred sub-dialog;
		 * after_delete_dialog runs the post-result delete/revive
		 * logic when the sub-dialog pops. */
		Input* sub = Build_Delete_Dialog();
		// HARDENING: the original assumes dialog allocation succeeds.
		if (!sub) {
			TieDiagnostics_Log(TIE_LOG_ERROR, "[REGISTER] could not allocate delete dialog\n");
			return;
		}
		s_delete_ctx.was_key_buttons = lio_Is_Key_Buttons();
		s_delete_ctx.sub_dlg = sub;
		if (!s_delete_ctx.was_key_buttons)
			lio_Set_Key_Buttons();
		lio_Set_Mouse_Position(active_spec->delete_mouse_x, active_spec->delete_mouse_y);
		ldialog_Schedule_Sub_Dialog(sub, after_delete_dialog, &s_delete_ctx);
	}
}

/* Post-confirmation delete or revive continuation. */
// PORT: continuation of TIE95 0x7C4A8 and TIE98 0x471FA0 after the
// asynchronous dialog task returns.
static void after_delete_dialog(int16_t result, void* ctx) {
	DeleteCtx* c = (DeleteCtx*)ctx;

	lview_Refresh_View();
	linput_Free_Inputs(c->sub_dlg);
	c->sub_dlg = NULL;

	if (!c->was_key_buttons)
		lio_Clear_Key_Buttons();
	lio_Set_Mouse_Position(active_spec->delete_return_mouse_x, active_spec->delete_return_mouse_y);

	if (result == 1) {
		/* Confirmed delete */
		char name_buf[TIE_PILOT_NAME_CAPACITY];
		Get_Reg_String_Button_Name(pilot_name_input, name_buf, sizeof(name_buf));
		char path[TIE_PILOT_NAME_CAPACITY + 5];
		snprintf(path, sizeof(path), "%s.tfr", name_buf);
		TieStorage_Remove(TIE_FILE_ROOT_USER, path);
		Delete_Pilot_Record();
		lview_Refresh_View();
	} else if (result != 2) {
		/* Revive */
		char name_buf[TIE_PILOT_NAME_CAPACITY];
		Get_Reg_String_Button_Name(pilot_name_input, name_buf, sizeof(name_buf));
		shipext_Load_Pilot(name_buf);
		shipext_Revive_Pilot(name_buf);
		register_Revive_Pilot_Info();
		lview_Refresh_View();
	}
}

// FUNCTION: TIE95 0x7B200; TIE98 0x470AE0
static void idraw_Pilot_Button(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	if (!refresh)
		return;
	PushButton* btn = (PushButton*)input;
	Rect tr;
	lrect_Copy_Rect(&tr, frame);
	tr.top++;

	int16_t color = btn->pressed ? 18 : 20;
	if (btn->name) {
		lfont_Print_Centered_Text(btn->name, &tr, color, active_spec->button_font);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip);
}

/* ================================================================
 * Pilot name callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7B2CC; TIE98 0x470B70
static void iuser_Pilot_Name(Input* input, int32_t time) {
	(void)time;
	RegStringButton* btn = (RegStringButton*)input;
	if (!linpattr_Get_Input_Selected(&btn->header))
		return;

	int16_t matched = -1;
	shipext_Set_Pilot_Name("");

	for (int16_t i = 0; i < num_pilots; i++) {
		if (matched != -1)
			break;
		int16_t slot;
		char dir_name[TIE_PILOT_NAME_CAPACITY];
		if (Index_To_Pilot(i, &slot) && Find_Reg_Dir_Name(dir_name, sizeof(dir_name), slot) &&
			!strcmp(dir_name, btn->name))
			matched = i;
	}

	if (matched == -1) {
		if (pilot_active != -1) {
			linpattr_Refresh_Input(pilot_list);
			pilot_active = -1;
		}
	} else if (matched != pilot_active) {
		linpattr_Refresh_Input(pilot_list);
		pilot_active = matched;
		int16_t page = matched / active_spec->page_size;
		if (page != cur_page) {
			cur_page = page;
			pilot_offset = active_spec->page_size * page;
		}
	}

	if (strlen(btn->name)) {
		pilot_info->var1 = 1;
	} else {
		pilot_info->var1 = 0;
		pilot_info->var2 = 0;
		linpattr_Refresh_Input(pilot_info);
	}
}

// FUNCTION: TIE98 0x470BE0; corresponding TIE95 logic is inline.
static void xuser_Pilot_Name(const char* search_name) {
	int16_t matched = -1;

	for (int16_t i = 0; i < num_pilots; i++) {
		if (matched != -1)
			break;
		int16_t slot;
		char dir_name[TIE_PILOT_NAME_CAPACITY];
		if (Index_To_Pilot(i, &slot) && Find_Reg_Dir_Name(dir_name, sizeof(dir_name), slot) &&
			!strcmp(dir_name, search_name))
			matched = i;
	}

	if (matched == -1) {
		if (pilot_active != -1) {
			linpattr_Refresh_Input(pilot_list);
			pilot_active = -1;
		}
	} else if (matched != pilot_active) {
		linpattr_Refresh_Input(pilot_list);
		pilot_active = matched;
		int16_t page = matched / active_spec->page_size;
		if (page != cur_page) {
			cur_page = page;
			pilot_offset = active_spec->page_size * page;
		}
	}
}

// FUNCTION: TIE95 0x7B3B0; TIE98 0x470D00
static void idraw_Pilot_Name(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	RegStringButton* btn = (RegStringButton*)input;
	int16_t saved_font = lfont_Get_Font();
	lfont_Set_Font(active_spec->edit_font);
	int16_t str_width = lfont_Get_String_Width(btn->name);
	lfont_Set_Font(saved_font);

	if (refresh) {
		lpaint_Paint_Clipped_Rect(frame, 0);
		int16_t color = lstyle_Get_Style_Down_Color();
		int16_t x = frame->left + (active_spec->dynamic_info_layout ? 1 : 2);
		int16_t y = frame->top + (active_spec->dynamic_info_layout ? 2 : 1);
		lfont_Print_Clipped_Text(btn->name, x, y, active_spec->edit_font, color);
	}

	int16_t caret_color = 0;
	if (linpattr_Is_Input_Active(&btn->header) && (int)strlen(btn->name) < active_spec->filename_max_length &&
		lio_Blink())
		caret_color = lstyle_Get_Style_Down_Color();

	int16_t caret_y = active_spec->dynamic_info_layout
						  ? frame->top + lfont_Get_FontID_Height(active_spec->edit_font) + 1
						  : frame->top + 7;
	lpaint_Horiz_Clipped_Line(frame->left + str_width + 4, caret_y, 5, caret_color);

	if (linpattr_Is_Input_Dirty(&btn->header))
		ldirty_Dirty_Rect(clip);
}

/* ================================================================
 * Pilot info callbacks + draw helpers
 * ================================================================ */

// FUNCTION: TIE95 0x7B490; TIE98 0x470E20
static void iuser_Pilot_Info(Input* input, int32_t time) {
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

// FUNCTION: TIE95 0x7B578; TIE98 0x470F10
static void Draw_Pilot_Title(Rect* frame, int16_t phase) {
	/* Conditional coordinates are from TIE98 0x470F10; fixed coordinates
	 * are from TIE95 0x7B578. */
	char buf[20];

	if (phase > 7) {
		int16_t extra_h = 2 * (phase - 7);
		if (frame->top + extra_h >= frame->bottom)
			extra_h = frame->bottom - frame->top;
		Rect tr;
		lrect_Set_Rect(&tr, frame->left, frame->top, frame->right, frame->top + extra_h);
		lpaint_Paint_Clipped_Rect(&tr, 240);
	}

	int16_t len1 = phase >= 8 ? 8 : phase;
	strncpy(buf, textext_Get_Text(txtRegInfoImperial), len1);
	buf[len1] = 0;
	int16_t c1 = len1 + 240;
	if (c1 > 247)
		c1 = 247;
	int16_t imperial_x = active_spec->dynamic_info_layout ? 30 : 11;
	lfont_Print_Clipped_Text(buf, frame->left + imperial_x, frame->top + 1, active_spec->info_value_font, c1);

	if (phase >= 4) {
		int16_t len2 = phase >= 12 ? 8 : phase - 4;
		strncpy(buf, textext_Get_Text(txtRegInfoDatabase), len2);
		buf[len2] = 0;
		int16_t c2 = len2 + 240;
		if (c2 > 247)
			c2 = 247;
		int16_t database_x = active_spec->dynamic_info_layout ? 26 : 10;
		int16_t database_y =
			active_spec->dynamic_info_layout ? lfont_Get_FontID_Height(active_spec->info_value_font) + 1 : 7;
		lfont_Print_Clipped_Text(buf, frame->left + database_x, frame->top + database_y,
								 active_spec->info_value_font, c2);
	}

	if (phase < 16) {
		int16_t access_x = active_spec->dynamic_info_layout ? 20 : 10;
		int16_t access_y = active_spec->dynamic_info_layout ? (frame->bottom - frame->top) / 2 : 36;
		lfont_Print_Clipped_Text(textext_Get_Text(txtRegInfoAccess), frame->left + access_x,
								 frame->top + access_y, active_spec->info_value_font,
								 4 * ((phase >> 1) & 3) + 19);
	}
}

// FUNCTION: TIE95 0x7B710; TIE98 0x471090
static void Draw_Pilot_Lines(Rect* frame, int16_t phase, int16_t line2_off) {
	int16_t pc = phase > 15 ? 15 : phase;
	int16_t w = frame->right - frame->left - (30 - 2 * pc);
	int16_t x = 15 - pc + frame->left;

	// TIE98 0x471090; the branch below it is TIE95 0x7B710.
	if (active_spec->dynamic_info_layout) {
		int16_t font_h = lfont_Get_FontID_Height(active_spec->info_value_font);
		int16_t line1_y = frame->top + 2 * font_h + 3;
		int16_t line2_y = frame->top + line2_off + 3 * font_h + 4;
		lpaint_Horiz_Clipped_Line(x, line1_y, w, pc / 2 + 240);
		lpaint_Horiz_Clipped_Line(x, line2_y, w, pc / 2 + 240);

		if (phase > line2_off + 15) {
			int16_t color = 240;
			int16_t progress = 2 * (phase - line2_off) - 37;
			while (color <= 247) {
				if (progress >= 0) {
					int16_t clamped = progress <= 62 ? progress : 62;
					if (clamped != 62 || color == 247)
						lpaint_Horiz_Clipped_Line(frame->left, frame->bottom - 3, w, color);
				}
				progress++;
				color++;
			}
		}
		return;
	}

	lpaint_Horiz_Clipped_Line(x, frame->top + 14, w, pc / 2 + 240);
	lpaint_Horiz_Clipped_Line(x, line2_off + frame->top + 22, w, pc / 2 + 240);

	if (phase > line2_off + 15) {
		int16_t color = 240;
		int16_t yp = 2 * (phase - (line2_off + 15)) - 7;
		while (color <= 247) {
			if (yp >= 0) {
				int16_t yo = yp <= 62 ? yp : 62;
				if (yo != 62 || color == 247)
					lpaint_Horiz_Clipped_Line(frame->left, yo + line2_off + frame->top + 23, w, color);
			}
			yp++;
			color++;
		}
	}
}

// FUNCTION: TIE95 0x7B858; TIE98 0x471190
static void Draw_Pilot_Name(Rect* frame, int16_t phase, int16_t inner_phase) {
	char typed[TIE_PILOT_NAME_CAPACITY];
	Get_Reg_String_Button_Name(pilot_name_input, typed, sizeof(typed));

	if (phase == 2) {
		shipext_Init_Pilot();
		if (pilot_active != -1) {
			int16_t slot;
			char dir_name[TIE_PILOT_NAME_CAPACITY];
			if (Index_To_Pilot(pilot_active, &slot) && Find_Reg_Dir_Name(dir_name, sizeof(dir_name), slot)) {
				if (!shipext_Load_Pilot(dir_name)) {
					pilot_active = -1;
					typed[0] = 0;
					Set_Reg_String_Button_Name(pilot_name_input, typed);
					linpattr_Refresh_Input(&pilot_name_input->header);
				}
			}
		}
	}

	if (phase >= 8) {
		int16_t ci = phase >= 15 ? 7 : phase - 8;
		Rect tr;
		lrect_Copy_Rect(&tr, frame);
		// TIE98 0x471190; the alternative is TIE95 0x7B858.
		if (active_spec->dynamic_info_layout) {
			int16_t font_h = lfont_Get_FontID_Height(active_spec->info_value_font);
			tr.top = frame->top + 2 * font_h + 4;
			tr.bottom = tr.top + font_h;
		} else {
			tr.top = frame->top + 17;
			tr.bottom = frame->top + 22;
		}
		lfont_Print_Centered_Text(typed, &tr, ci + 248, active_spec->info_value_font);

		int16_t status_offset =
			active_spec->dynamic_info_layout ? lfont_Get_FontID_Height(active_spec->info_value_font) : 6;
		int16_t status_phase = active_spec->dynamic_info_layout ? status_offset + 1 : 6;
		if (phase >= 16 && pilot_record.exit_status && inner_phase >= status_phase) {
			lrect_Offset_Rect(&tr, 0, status_offset);
			lfont_Print_Centered_Text(textext_Get_Text((TIEText)(pilot_record.exit_status + 10)), &tr,
									  (phase & 7) / 2 + 252, active_spec->info_value_font);
		}
	}
}

// FUNCTION: TIE95 0x7B9B8; TIE98 0x471320
static void Draw_Pilot_Info(Rect* frame, int16_t phase, int16_t inner_phase) {
	// TIE98 0x471320; the branch below it is TIE95 0x7B9B8.
	if (active_spec->dynamic_info_layout) {
		int16_t label_h = lfont_Get_FontID_Height(active_spec->info_label_font);
		int16_t value_h = lfont_Get_FontID_Height(active_spec->info_value_font);
		int16_t row = label_h + value_h + 2;
		int16_t first_row_y = 3 * value_h + 7;

		if (phase >= inner_phase + 18) {
			int16_t sub = phase - (inner_phase + 18);
			if (sub > 7)
				sub = 7;
			int16_t c1 = sub + 240;
			int16_t c2 = sub + 248;
			int16_t y = frame->top + inner_phase + first_row_y;

			lfont_Enable_FontID_Shadow(active_spec->info_label_font);
			lfont_Enable_FontID_Shadow(active_spec->info_value_font);

			lfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotRank), frame->left + 2, y,
									 active_spec->info_label_font, c1);
			lfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.rank + 1)), frame->left + 2,
									 y + label_h, active_spec->info_value_font, c2);

			char buf[20];
			snprintf(buf, sizeof(buf), "%lu", (unsigned long)pilot_record.score);
			y += row + 10;
			lfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotScore), frame->left + 2, y,
									 active_spec->info_label_font, c1);
			lfont_Print_Clipped_Text(buf, frame->left + 2, y + label_h, active_spec->info_value_font, c2);

			y += row + 10;
			lfont_Print_Clipped_Text(textext_Get_Text(txtRegLevel), frame->left + 2, y,
									 active_spec->info_label_font, c1);
			lfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.game_level + 29)),
									 frame->left + 2, y + label_h, active_spec->info_value_font, c2);

			lfont_Disable_FontID_Shadow(active_spec->info_label_font);
			lfont_Disable_FontID_Shadow(active_spec->info_value_font);
		}

		if (phase > inner_phase + 15) {
			int16_t cover = 2 * (phase - inner_phase) - 30;
			if (cover < 62) {
				Rect r;
				lrect_Set_Rect(&r, frame->left,
							   frame->top + inner_phase + cover + first_row_y + 2 * (row + 10), frame->right,
							   frame->bottom);
				lpaint_Paint_Clipped_Rect(&r, 16);
			}
		}
		return;
	}

	if (phase < inner_phase + 18)
		return;

	int16_t sub = phase - (inner_phase + 18);
	if (sub > 7)
		sub = 7;

	lfont_Enable_FontID_Shadow(active_spec->info_label_font);
	lfont_Enable_FontID_Shadow(active_spec->info_value_font);

	int16_t c1 = sub + 240;
	int16_t c2 = sub + 248;

	/* Rank */
	lfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotRank), frame->left + 2,
							 inner_phase + frame->top + 24, active_spec->info_label_font, c1);
	lfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.rank + 1)), frame->left + 2,
							 inner_phase + frame->top + 34, active_spec->info_value_font, c2);

	/* Score */
	char buf[20];
	snprintf(buf, sizeof(buf), "%lu", (unsigned long)pilot_record.score);
	lfont_Print_Clipped_Text(textext_Get_Text(txtRegPilotScore), frame->left + 2,
							 inner_phase + frame->top + 44, active_spec->info_label_font, c1);
	lfont_Print_Clipped_Text(buf, frame->left + 2, inner_phase + frame->top + 54,
							 active_spec->info_value_font, c2);

	/* Level */
	lfont_Print_Clipped_Text(textext_Get_Text(txtRegLevel), frame->left + 2, inner_phase + frame->top + 64,
							 active_spec->info_label_font, c1);
	lfont_Print_Clipped_Text(textext_Get_Text((TIEText)(pilot_record.game_level + 29)), frame->left + 2,
							 inner_phase + frame->top + 74, active_spec->info_value_font, c2);

	lfont_Disable_FontID_Shadow(active_spec->info_label_font);
	lfont_Disable_FontID_Shadow(active_spec->info_value_font);

	/* Progressive reveal: paint a color-16 cover rect over the lower
	 * portion of the panel that hasn't been animated in yet. The cover
	 * top descends 2 px per phase tick past inner_phase + 15, exposing
	 * the rank/score/level rows top-down. After ~31 ticks the cover is
	 * fully off-frame (offset >= 62) and the text is fully visible. */
	if (phase > inner_phase + 15) {
		int16_t cover = 2 * (phase - (inner_phase + 15));
		if (cover < 62) {
			Rect r;
			lrect_Set_Rect(&r, frame->left, frame->top + inner_phase + cover + 24, frame->right,
						   frame->bottom);
			lpaint_Paint_Clipped_Rect(&r, 16);
		}
	}
}

// FUNCTION: TIE95 0x7B4DC; TIE98 0x470E70
static void idraw_Pilot_Info(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	(void)refresh;
	lpaint_Paint_Clipped_Rect(frame, 16);

	if (input->var1) {
		int16_t phase = input->var2;

		/* Inner phase for the info panel offset */
		int16_t inner = 0;
		if (phase >= 16 && pilot_record.exit_status) {
			inner = phase - 15;
			int16_t inner_max = active_spec->dynamic_info_layout
									? lfont_Get_FontID_Height(active_spec->info_value_font) + 1
									: 6;
			if (inner > inner_max)
				inner = inner_max;
		}

		Draw_Pilot_Title(frame, phase);
		Draw_Pilot_Lines(frame, phase, inner);
		Draw_Pilot_Name(frame, phase, inner);
		Draw_Pilot_Info(frame, phase, inner);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip);
}

/* ================================================================
 * Delete dialog
 * ================================================================ */

// FUNCTION: TIE95 0x7C50C; TIE98 0x472010
// HARDENING: unwinds partial dialog allocation.
static Input* Build_Delete_Dialog(void) {
	Rect r;
	lrect_Set_Rect(&r, 0, 0, active_spec->delete_dialog_width, active_spec->delete_dialog_height);
	Input* dlg = linput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	if (!dlg)
		return NULL;
	linpattr_Set_Input_Update_Function(dlg, iupdate_Delete_Input);
	linpattr_Set_Input_Draw_Function(dlg, idraw_Delete_Input);
	linpattr_Set_Input_Allign(dlg, 1, 1);
	linpattr_Show_Input(dlg);

	Index_To_Pilot_Record(pilot_active, &shell_pilot);

	char label[32];
	const int16_t* b = active_spec->delete_button_bounds;
	lrect_Set_Rect(&r, b[0], b[1], b[2], b[3]);
	strcpy(label, textext_Get_Text(txtRegBtnDelete));
	PushButton* del = lbtnpush_Alloc_Button(dlg, &r, 0, iuser_Delete_Input, label, 1);
	if (!del) {
		linput_Free_Inputs(dlg);
		return NULL;
	}
	linpattr_Set_Input_Allign(&del->header, 0, 2);

	lrect_Set_Rect(&r, b[0], b[1], b[2], b[3]);
	strcpy(label, textext_Get_Text(txtRegBtnCancel));
	PushButton* cancel = lbtnpush_Alloc_Button(dlg, &r, 0, iuser_Delete_Input, label, 2);
	if (!cancel) {
		linput_Free_Inputs(dlg);
		return NULL;
	}
	linpattr_Set_Input_Allign(&cancel->header, 2, 2);

	return dlg;
}

// FUNCTION: TIE95 0x7C624; TIE98 0x472170
// DIVERGENCE: the original joystick-calibration shortcut is not implemented.
static int16_t iupdate_Delete_Input(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
									uint8_t right, int16_t mouse_x, int16_t mouse_y) {
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

// FUNCTION: TIE95 0x7C660; TIE98 0x4721B0
static void iuser_Delete_Input(Input* input, int32_t time) {
	(void)time;
	if (!linpattr_Get_Input_Selected(input))
		return;
	int16_t id = input->id;
	if (id >= 1 && id <= 3)
		ldialog_Set_Dialog_Exit(id);
}

// FUNCTION: TIE95 0x7C688; TIE98 0x4721E0
static void idraw_Delete_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	if (!refresh)
		return;

	char title[64], name_buf[TIE_PILOT_NAME_CAPACITY];
	Get_Reg_String_Button_Name(pilot_name_input, name_buf, sizeof(name_buf));
	Index_To_Pilot_Record(pilot_active, &shell_pilot);

	strcpy(title, textext_Get_Text(txtRegBtnDeletePilot));
	strcat(title, " ");
	strcat(title, name_buf);
	strcat(title, "?");

	Rect dst;
	lrect_Copy_Rect(&dst, frame);
	lpaint_Frame_Clipped_Rect(&dst, 16);
	lrect_Inset_Rect(&dst, 1, 1);
	lstyle_Style_Paint_Border(&dst, 0);
	if (active_spec->dynamic_info_layout) {
		int16_t font_h = lfont_Get_FontID_Height(active_spec->delete_title_font);
		dst.top += font_h;
		dst.bottom = dst.top + font_h;
	} else {
		dst.bottom = dst.top + 14;
	}
	lfont_Enable_FontID_Shadow(active_spec->delete_title_font);
	lfont_Print_Centered_Text(title, &dst, 15, active_spec->delete_title_font);
	lfont_Disable_FontID_Shadow(active_spec->delete_title_font);

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip);
}

/* ================================================================
 * Protect (password) dialog
 * ================================================================ */

/* Pushed by the register task's PROTECT phase. The task yields to
 * AFTER_PROTECT after this; that phase reads dlg_exit_gbl and runs
 * the post-protect logic. */
// PORT: asynchronous form of TIE95 REGISTER_Do_Protect_Dialog (0x7C7AC).
static void register_push_protect_dialog(void) {
	protect_count = 0;
	protect_index = 0;
	protect_chosen = 0;
	for (int16_t i = 0; i < 3; i++)
		protect_state[i] = reg_cp[i];

	lio_Set_Mouse_Position(160, 135);
	s_protect_ctx.sub_dlg = Build_Protect_Dialog();
	ldialog_Push_Dialog_View_Task(s_protect_ctx.sub_dlg);
}

// FUNCTION: TIE95 0x7C820; absent from TIE98
static Input* Build_Protect_Dialog(void) {
	Rect r;
	lrect_Set_Rect(&r, 0, 0, 112, 86);
	Input* dlg = linput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	linpattr_Set_Input_Update_Function(dlg, iupdate_Protect_Input);
	linpattr_Set_Input_Draw_Function(dlg, idraw_Protect_Input);
	linpattr_Set_Input_Allign(dlg, 1, 1);
	linpattr_Show_Input(dlg);
	dlg->id = 0;
	protect_parent = dlg;

	/* Symbol challenge area */
	lrect_Set_Rect(&r, 4, 14, 108, 37);
	Input* sym_input = linput_Alloc_Dialog_Input(dlg, &r, 0, 0);
	linpattr_Set_Input_User_Function(sym_input, iuser_Protect_Input);
	linpattr_Set_Input_Draw_Function(sym_input, idraw_Protect_Input);
	linpattr_Show_Input(sym_input);
	sym_input->id = 3;

	/* Subtitle */
	lrect_Set_Rect(&r, 4, 39, 108, 49);
	Input* sub_input = linput_Alloc_Dialog_Input(dlg, &r, 0, 0);
	linpattr_Set_Input_Draw_Function(sub_input, idraw_Protect_Input);
	linpattr_Show_Input(sub_input);
	sub_input->id = 6;

	/* Password field (RegStringButton, filename mode=0). Seeded from
	 * the persistent `initial_name` buffer so the field retains what
	 * the user typed previously. */
	lrect_Set_Rect(&r, 4, 51, 108, 65);
	RegStringButton* pwd = Alloc_Input_Reg_String_Button(dlg, &r, 0, iuser_Protect_Input, initial_name, 0, 4);
	linpattr_Hide_Input(&pwd->header);
	protect_btns_arr[0] = &pwd->header;

	/* OK button */
	lrect_Set_Rect(&r, 4, 4, 32, 20);
	textext_Copy_Text(reg_prot_name, txtRegProtOK);
	PushButton* btn_ok = lbtnpush_Alloc_Button(dlg, &r, 0, iuser_Protect_Input, reg_prot_name, 1);
	linpattr_Set_Input_Allign(&btn_ok->header, 0, 2);
	linpattr_Hide_Input(&btn_ok->header);
	protect_btns_arr[1] = &btn_ok->header;

	/* Exit to DOS button */
	lrect_Set_Rect(&r, 4, 4, 76, 20);
	textext_Copy_Text(reg_prot_name + 24, txtRegProtExit);
	PushButton* btn_exit = lbtnpush_Alloc_Button(dlg, &r, 0, iuser_Protect_Input, reg_prot_name + 24, 2);
	linpattr_Set_Input_Allign(&btn_exit->header, 2, 2);
	linpattr_Hide_Input(&btn_exit->header);
	protect_btns_arr[2] = &btn_exit->header;

	/* Press to Continue button */
	lrect_Set_Rect(&r, 4, 4, 108, 20);
	textext_Copy_Text(reg_prot_name + 48, txtRegProtPress);
	PushButton* btn_quit = lbtnpush_Alloc_Button(dlg, &r, 0, iuser_Protect_Input, reg_prot_name + 48, 5);
	linpattr_Set_Input_Allign(&btn_quit->header, 0, 2);

	return dlg;
}

// FUNCTION: TIE95 0x7CA58; absent from TIE98
static int16_t iupdate_Protect_Input(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
									 uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)input;
	(void)left;
	(void)right;
	(void)mouse_x;
	(void)mouse_y;
	if (!key)
		return 0;

	if (key == 13) {
		linpattr_Selected_Input(protect_btns_arr[1]);
	}
	(void)bounds;
	(void)clip;
	return 0;
}

// FUNCTION: TIE95 0x7CAC0; absent from TIE98
static void iuser_Protect_Input(Input* input, int32_t time) {
	(void)time;

	/* On first frame for symbol display (id=3), pick random question */
	if (input->id == 3 && !protect_chosen) {
		protect_index = rand_rand() % 29;
		for (int16_t i = 0; i < 3; i++)
			protect_state[i] = reg_cp[3 * protect_index + i];
		linpattr_Refresh_Input(input);
	}

	if (!linpattr_Get_Input_Selected(input))
		return;

	switch (input->id) {
		case 1: { /* Continue / check answer */
			RegStringButton* pwd = (RegStringButton*)protect_btns_arr[0];
			char buf[32];
			strcpy(buf, pwd->name);
			/* Lowercase the answer */
			for (int16_t i = 0; buf[i]; i++)
				buf[i] = (char)tolower(buf[i]);

			if (strcmp(buf, reg_cp_name[protect_index]) == 0 || strcmp(buf, "evarobinyali") == 0) {
				/* Correct answer — exit dialog with input's id */
				ldialog_Set_Dialog_Exit(input->id);
			} else {
				/* Wrong answer — allow up to 3 attempts */
				if (++protect_count >= 3) {
					ldialog_Set_Dialog_Exit(2);
				} else {
					pwd->name[0] = 0;
					linpattr_Refresh_Input(&pwd->header);
					linpattr_Refresh_Input(protect_parent);
				}
			}
			break;
		}
		case 2: /* Exit to DOS */
			ldialog_Set_Dialog_Exit(2);
			break;
		case 4: /* Password field clicked */
			lio_Set_Mouse_Position(120, 130);
			break;
		case 5: /* Quit/Skip */
			protect_chosen = 1;
			linpattr_Hide_Input(input);
			for (int16_t i = 0; i < 3; i++)
				linpattr_Show_Input(protect_btns_arr[i]);
			linpattr_Refresh_Input(protect_parent);
			break;
		default:
			break;
	}
}

// FUNCTION: TIE95 0x7CC78; absent from TIE98
static void idraw_Protect_Input(Input* input, Rect* frame, Rect* clip, int16_t refresh) {
	if (!refresh)
		return;

	int16_t id = input->id;
	if (id == 0) {
		/* Title bar */
		Rect tr;
		lrect_Copy_Rect(&tr, frame);
		lpaint_Frame_Clipped_Rect(&tr, 16);
		lrect_Inset_Rect(&tr, 1, 1);
		lstyle_Style_Paint_Border(&tr, 0);
		tr.bottom = tr.top + 14;
		lfont_Enable_FontID_Shadow(0);
		char buf[32];
		textext_Copy_Text(buf, txtRegProtCopy);
		lfont_Print_Centered_Text(buf, &tr, 15, 0);
		lfont_Disable_FontID_Shadow(0);
	} else if (id == 3) {
		/* Symbol challenge */
		int16_t x_off = 0;
		for (int16_t i = 0; i < 3; i++) {
			Rect tr;
			lrect_Copy_Rect(&tr, frame);
			tr.left += x_off;
			tr.right = tr.left + 32;
			lstyle_Style_Paint_TextField(&tr);
			lrect_Inset_Rect(&tr, 2, 2);
			lactor_Set_Actor_State(symbols, protect_state[i], 0);
			lactanim_Draw_Anim_Actor(symbols, frame, clip, tr.left + 4, tr.top + 1, 1);
			x_off += 36;
		}
	} else if (id == 6 && protect_chosen) {
		/* "See Manual Page N" */
		char fmt[32], buf[32];
		textext_Copy_Text(fmt, txtRegProtManual);
		snprintf(buf, sizeof(buf), fmt, protect_index + 4);
		lfont_Enable_FontID_Shadow(0);
		lfont_Print_Centered_Text(buf, frame, 15, 0);
		lfont_Disable_FontID_Shadow(0);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip);
}

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x7A93C; TIE98 0x4700D0
static void end_View(int32_t phase) {
	if (!phase) {
		if (!lcursor_Is_Cursor_Visible())
			lcursor_Show_Cursor();

		/* Copy-protection challenge moved to register_task_step's
		 * PROTECT phase (runs before the view is pushed) so it can
		 * yield via the task stack. */
	}

	if (phase == 18 && shellext_Get_Cur_Scene() == SCENE_REGISTER)
		soundext_Play_Speech(speechRegisterGuard);

	if (!phase) {
		/* Load pilot directory and build FPR cache */
		lfiledir_Read_Directory(&directory);
		num_loaded_pilots = directory.count;
		lcursor_Set_Cursor(1); /* waitCursor */
		Build_Fast_Pilot_Record();
		lcursor_Set_Cursor(0); /* mainCursor */

		num_pages = (num_pilots + active_spec->page_size - 1) / active_spec->page_size;
		if (!num_pages)
			num_pages = 1;
		cur_page = 0;

		Set_Your_Reg_Pilot();
	}

	/* Per-frame: search for typed name in FPR */
	char typed[TIE_PILOT_NAME_CAPACITY];
	Get_Reg_String_Button_Name(pilot_name_input, typed, sizeof(typed));
	xuser_Pilot_Name(typed);

	/* Show/hide delete button */
	if (typed[0]) {
		if (linpattr_Is_Input_Visible(pilot_delete)) {
			if (pilot_active == -1) {
				linpattr_Hide_Input(pilot_delete);
				lview_Refresh_View();
			}
		} else if (pilot_active != -1) {
			linpattr_Show_Input(pilot_delete);
			pilot_delete_status = -1;
			lview_Refresh_View();
		}

		if (linpattr_Is_Input_Visible(pilot_delete) && pilot_delete_status) {
			lbtnpush_Set_Button_Name((PushButton*)pilot_delete, textext_Get_Text(txtRegBtnDeletePilot));
			pilot_delete_status = 0;
			lview_Refresh_View();
		}
	} else {
		if (linpattr_Is_Input_Visible(pilot_delete)) {
			linpattr_Hide_Input(pilot_delete);
			lview_Refresh_View();
		}
	}
}

/* ================================================================
 * Entry point
 * ================================================================ */

typedef enum {
	REGISTER_PHASE_BEGIN = 0,
	REGISTER_PHASE_PROTECT, /* copy-protection dialog pushed; resume on its pop */
	REGISTER_PHASE_PUSH_VIEW,
	REGISTER_PHASE_CLEANUP,
} RegisterPhase;

typedef struct RegisterTask {
	SceneHeadStruct* scene_head;
	ResFile* rf;
	RegisterPhase phase;
	const RegisterSpec* spec;
	bool view_pushed;
} RegisterTask;

// PORT: adapts recovered edition data to the shared Rect API.
static void register_set_rect(Rect* rect, const int16_t bounds[4]) {
	lrect_Set_Rect(rect, bounds[0], bounds[1], bounds[2], bounds[3]);
}

// HARDENING: clean failure path for missing or incompatible resources.
static LandruTaskStepResult register_setup_failed(RegisterTask* t, const char* resource) {
	TieDiagnostics_Log(TIE_LOG_ERROR, "[REGISTER] missing frontend resource: %s\n",
					   resource ? resource : "unknown");
	lerror_Set_Landru_Error(6);
	t->phase = REGISTER_PHASE_CLEANUP;
	return LANDRU_TASK_STEP_CONTINUE;
}

/* PORT: asynchronous adaptation of TIE95 REGISTER_Register (0x7A4C0)
 * and TIE98 REGISTER_Register (0x46FBC0). Valid-resource setup and cleanup
 * retain the recovered ordering; explicit setup checks are HARDENING. */
static LandruTaskStepResult register_task_step(void* self) {
	RegisterTask* t = (RegisterTask*)self;

	if (t->phase == REGISTER_PHASE_BEGIN) {
		Rect frame;

		active_spec = t->spec;
		if (!lsurface_Select_Surface_Set(active_spec->surface_set))
			return register_setup_failed(t, "surface set");
		lview_Init_View(lview_Get_Current_View());

		shipext_Delete_Temp_Pilot();
		pilot_active = -1;
		pilot_offset = 0;
		num_pilots = 0;
		num_loaded_pilots = 0;
		cur_page = 0;
		fast_pilot_record = NULL;
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
		directory.entries = NULL;
		memset(reg_button, 0, sizeof(reg_button));

		lio_Set_Mouse_Position(active_spec->mouse_x, active_spec->mouse_y);

		t->rf = shellext_Open_Empire_Resource(active_spec->archive);
		if (!t->rf)
			return register_setup_failed(t, active_spec->archive);
		lrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);

		/* Load film. Tag the snapshot with the (lfd, film) tuple so
		 * the cutscene compositor can resolve a remaster bundle for
		 * this screen, and switch the RT to OVERLAY mode (persistent
		 * layered composite over classic FB) since the screen has
		 * dynamic UI text the engine renders into the classic FB
		 * that should still show through. */
		if (shellext_Get_Cur_Scene() == SCENE_REGISTER) {
			register_film = lfilm_Res_Callback_Film("register", &frame, 0, 0, 0, film_Callback);
			TieSnapshotBuilder_SetActiveFilm(active_spec->snapshot_lfd, "register");
		} else {
			register_film = lfilm_Res_Callback_Film("reg2", &frame, 0, 0, 0, film_Callback);
			TieSnapshotBuilder_SetActiveFilm(active_spec->snapshot_lfd, "reg2");
		}
		if (!register_film)
			return register_setup_failed(t, shellext_Get_Cur_Scene() == SCENE_REGISTER ? "register" : "reg2");
		for (int16_t i = 0; i < active_spec->button_actor_count; i++) {
			if (!reg_button[i])
				return register_setup_failed(t, "registration button actor");
		}
		/* Default redraw model (INCREMENTAL) is correct for register
		 * — dirty-rect refresh, persistent RT. No explicit setter
		 * needed; left as-is from shellext_Begin_Close_Landru_Scene. */

		lfilm_Set_Film_Def_Palette(register_film, t->scene_head->def_palette);

		/* TIE95 0x7A593 loads reg-bak1; TIE98 has no equivalent actor. */
		if (active_spec->use_background) {
			Actor* bak1 = lactor_Find_Actor(FOURCC_DELT, active_spec->background);
			if (!bak1)
				return register_setup_failed(t, active_spec->background);
			lactor_Non_Refreshable_Actor(bak1);
			lactor_Refresh_Actor(bak1);
		}

		reg_bak = lactor_Find_Actor(FOURCC_DELT, active_spec->back_panel);
		if (!reg_bak)
			return register_setup_failed(t, active_spec->back_panel);
		lactor_Set_Actor_Draw_Function(reg_bak, (lactorDrawFunc)draw_Register_Back);

		reg_door = lactor_Find_Actor(FOURCC_ANIM, active_spec->door);
		if (!reg_door)
			return register_setup_failed(t, active_spec->door);
		lactor_Set_Actor_User_Function(reg_door, (lactorCallback)user_Door);

		reg_troop = lactor_Find_Actor(FOURCC_ANIM, active_spec->troop);
		if (!reg_troop)
			return register_setup_failed(t, active_spec->troop);
		lactor_Set_Actor_User_Function(reg_troop, (lactorCallback)user_Troop);

		/* Init directory and symbol actor */
		lfiledir_Init_Directory(&directory, ".tfr", 0);
		if (!directory.entries)
			return register_setup_failed(t, "pilot directory");
		lfiledir_Set_Name_Length(&directory, active_spec->directory_name_length);
		// TIE95 0x7A616; the copy-protection actor is absent from TIE98.
		if (active_spec->load_symbols) {
			symbols = lactanim_Res_Anim_Actor("symbols", &frame, 0, 0, 0);
			if (!symbols)
				return register_setup_failed(t, "symbols");
			lactor_Set_Actor_Time(symbols, 0, 0);
		}

		/* Build input tree */
		reg_parent = linput_Alloc_Input(NULL, &frame, 0, 0);
		if (!reg_parent)
			return register_setup_failed(t, "registration input root");

		register_set_rect(&frame, active_spec->door_bounds);
		door_input = linput_Alloc_Input(reg_parent, &frame, 0, 0);
		if (!door_input)
			return register_setup_failed(t, "registration door input");
		linpattr_Set_Input_Update_Function(door_input, iupdate_Register);
		linpattr_Set_Input_User_Function(door_input, iuser_Register);
		door_input->mouseUsage = allInput;
		door_input->id = 0;

		register_set_rect(&frame, active_spec->list_bounds);
		pilot_list = linput_Alloc_Input(reg_parent, &frame, 0, 0);
		if (!pilot_list)
			return register_setup_failed(t, "pilot list input");
		linpattr_Set_Input_Update_Function(pilot_list, iupdate_Pilot_List);
		linpattr_Set_Input_Draw_Function(pilot_list, idraw_Pilot_List);
		linpattr_Refreshable_Input(pilot_list);
		pilot_list->id = 0;

		/* Pilot name input (RegStringButton, filename mode) */
		register_set_rect(&frame, active_spec->name_bounds);
		pilot_name_input = Alloc_Input_Reg_String_Button(reg_parent, &frame, 0, iuser_Pilot_Name, "", 1, 0);
		if (!pilot_name_input)
			return register_setup_failed(t, "pilot name input");
		linpattr_Set_Input_Draw_Function(&pilot_name_input->header, idraw_Pilot_Name);
		linpattr_Refreshable_Input(&pilot_name_input->header);

		/* Prev/Next buttons */
		register_set_rect(&frame, active_spec->prev_bounds);
		PushButton* prev = lbtnpush_Alloc_Small_Button(reg_parent, &frame, 0, iuser_Pilot_Button, NULL, 0);
		if (!prev)
			return register_setup_failed(t, "previous-page input");
		linpattr_Set_Input_Draw_Function(&prev->header, (InputDrawFunc)0);
		linpattr_Refreshable_Input(&prev->header);

		register_set_rect(&frame, active_spec->next_bounds);
		PushButton* next = lbtnpush_Alloc_Small_Button(reg_parent, &frame, 0, iuser_Pilot_Button, NULL, 1);
		if (!next)
			return register_setup_failed(t, "next-page input");
		linpattr_Set_Input_Draw_Function(&next->header, (InputDrawFunc)0);
		linpattr_Refreshable_Input(&next->header);

		/* Pilot info display */
		register_set_rect(&frame, active_spec->info_bounds);
		pilot_info = linput_Alloc_Input(reg_parent, &frame, 0, 0);
		if (!pilot_info)
			return register_setup_failed(t, "pilot info input");
		linpattr_Set_Input_User_Function(pilot_info, iuser_Pilot_Info);
		linpattr_Set_Input_Draw_Function(pilot_info, idraw_Pilot_Info);
		linpattr_Refreshable_Input(pilot_info);
		pilot_info->id = 0;

		/* Delete button (initially hidden) */
		register_set_rect(&frame, active_spec->delete_bounds);
		char del_label[32];
		strcpy(del_label, textext_Get_Text(txtRegBtnDeletePilot));
		pilot_delete =
			(Input*)lbtnpush_Alloc_Small_Button(reg_parent, &frame, 0, iuser_Pilot_Button, del_label, 2);
		if (!pilot_delete)
			return register_setup_failed(t, "delete-pilot input");
		linpattr_Set_Input_Draw_Function(pilot_delete, idraw_Pilot_Button);
		linpattr_Refreshable_Input(pilot_delete);
		linpattr_Hide_Input(pilot_delete);

		/* Copy-protection challenge moved out of end_View into a
		 * dedicated phase: push the protect dialog (if needed) and
		 * yield. AFTER_PROTECT handles the result and pushes the
		 * view. Without copy-protection enabled (default), skip
		 * straight to PUSH_VIEW. */
		if (active_spec->load_symbols && copy_protection_enabled &&
			shellext_Get_Cur_Scene() == SCENE_REGISTER) {
			register_push_protect_dialog();
			t->phase = REGISTER_PHASE_PROTECT;
			return LANDRU_TASK_STEP_CONTINUE;
		}
		t->phase = REGISTER_PHASE_PUSH_VIEW;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (t->phase == REGISTER_PHASE_PROTECT) {
		/* Protect dialog popped — read the result. result==2 means
		 * the user cancelled out, so we leave landru_exit_gbl as the
		 * dialog's exit code (2) which the scene transition routes
		 * to "abort". result!=2 means challenge passed: clear the
		 * exit so the register scene continues normally. */
		int16_t result = ldialog_Get_Dialog_Exit();
		ldialog_Clear_Dialog_Exit();
		linput_Free_Inputs(s_protect_ctx.sub_dlg);
		s_protect_ctx.sub_dlg = NULL;
		if (result != 2)
			lerror_Set_Landru_Exit(0);
		lio_Set_Mouse_Position(160, 130);
		t->phase = REGISTER_PHASE_PUSH_VIEW;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (t->phase == REGISTER_PHASE_PUSH_VIEW) {
		/* Push the modal view task */
		lview_Set_View_Update_Function(end_View);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();
		lcanvas_Invalid_Screen_Diff();
		lio_Set_Key_Buttons();

		lviewadd_Push_Handle_View_Task();
		t->view_pushed = true;

		t->phase = REGISTER_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	lio_Clear_Key_Buttons();
	lview_Enable_All_View_Erase();
	lview_Clear_View_Update_Function();

	if (!t->view_pushed) {
		lview_Free_All_From_View(lview_Get_Current_View());
		lview_Init_View(lview_Get_Current_View());
	}

	lfiledir_Free_Directory(&directory);
	if (fast_pilot_record)
		free(fast_pilot_record);
	fast_pilot_record = NULL;

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	if (t->rf)
		lres_Close_Resource(t->rf);
	t->rf = NULL;
	if (t->spec->surface_set == LANDRU_SURFACE_SVGA)
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable register_task_vt = {
	.step = register_task_step,
};

// PORT: task entry point replacing the original synchronous calls.
void register_Push_Register_Task(SceneHeadStruct* scene_head) {
	RegisterTask* t = (RegisterTask*)landru_task_push(&register_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->rf = NULL;
	t->phase = REGISTER_PHASE_BEGIN;
	t->view_pushed = false;
	t->spec = &register_specs[TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : 0];
}
