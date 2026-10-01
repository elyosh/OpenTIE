/*
 * PLAY1.C — Cutscene/film playback module
 *
 * Plays pre-rendered cutscene films (intro sequences, battle cinematics,
 * award ceremonies, expansion-pack scenes). Each scene ID maps to an .LFD
 * resource file, a FILM resource name, an optional CD stream file, and a
 * frame rate. The main entry play1_Play1() looks up the scene, opens
 * resources, creates the film, and runs the view loop. A film callback
 * handles per-actor special effects: delta-to-literal conversion, additive
 * blending, medal arm animation, and CD FMV streaming.
 */

#include "tie/play1.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/film_task.h"
#include "tie_runtime/runtime/play1_data_set.h"
#endif
#include "landru/actor.h"
#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/file.h"
#include "landru/film.h"
#include "landru/fourcc.h"
#include "landru/pal.h"
#include "landru/res.h"
#include "landru/stream.h"
#include "landru/surface.h"
#include "landru/timer.h"
#include "landru/view.h"
#include "landru/viewadd.h"
#include "tie/shell.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/textext.h"
#include "tie/wavestream_tie98.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#include "tie_runtime/storage/storage.h"
#include "util/binio.h"

#include "tie/deltadd.h"
#include "tie/drawstrm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original TU calls the library strcpy() rather than the inline form. */
#ifdef __WATCOMC__
#pragma function(strcpy)
#endif

enum {
	STREAM_BUFFER_SIZE = 128000,
};

/* Wrap table for 320-wide scanline offsets (used by rendering subsystems) */
// GLOBAL: TIE95 0xF6174
// GLOBAL: TIE98 0x5FBC60
static int32_t wrap_table[205];
// GLOBAL: TIE95 0xF6170
// GLOBAL: TIE98 0x5FBC4C
static int32_t wrap;

/* ---- Scene lookup tables ---- */
/*
 * Indexed by play1_id. The modern runtime replaces them with the LecDemos
 * sample-disc tables when that data layout is installed (see
 * TiePlay1_SelectDataSet).
 */

// GLOBAL: TIE95 0xD0C02
// GLOBAL: TIE98 0x4E90B0
int16_t play1_cur_scene[87] = {
	6,   7,   10,  20,  30,  40,  50,  60,  70,  120, 130, 210, 231, 240, 400, 401, 402, 403,
	404, 405, 406, 407, 408, 409, 410, 411, 420, 250, 251, 252, 253, 254, 255, 256, 257, 258,
	259, 260, 261, 262, 263, 270, 170, 280, 281, 282, 283, 284, 285, 390, 500, 510, 520, 530,
	531, 540, 550, 560, 570, 571, 572, 573, 580, 581, 590, 591, 600, 601, 602, 603, 610, 620,
	621, 622, 623, 25,  700, 710, 720, 730, 740, 61,  71,  72,  31,  32,  0,
};

// GLOBAL: TIE95 0xD0CB0
// GLOBAL: TIE98 0x4E9160
int16_t play1_next_scene[86] = {
	7,   8,   20,  30,  40,  50,  60,  61,  71,  121, 131, 910, 910, 910, 420, 420, 420, 420,
	420, 420, 420, 420, 420, 420, 420, 420, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 231, 910, 910, 910, 910, 910, 910, 910, 910, 910, 531,
	910, 910, 910, 910, 571, 572, 573, 910, 581, 910, 591, 910, 601, 602, 603, 910, 910, 621,
	622, 623, 910, 910, 910, 910, 910, 910, 910, 70,  72,  80,  32,  40,
};

// GLOBAL: TIE95 0xD0D5C
// GLOBAL: TIE98 0x4E9210
int16_t play1_skip_scene[86] = {
	100, 100, 100, 100, 100, 100, 100, 100, 100, 121, 131, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 100, 100, 100, 100, 100,
};

// GLOBAL: TIE95 0xD037A
// GLOBAL: TIE98 0x4E8820
char play1_resource_str[86][14] = {
	"logo.lfd",    "perelogo.lfd", "stardest.lfd", "city.lfd",     "emperor.lfd",  "swarm.lfd",
	"bridge.lfd",  "platform.lfd", "platform.lfd", "totrain.lfd",  "tocombat.lfd", "capture.lfd",
	"medical.lfd", "funeral.lfd",  "secret1.lfd",  "secret2.lfd",  "secret2.lfd",  "secret2.lfd",
	"secret3.lfd", "secret3.lfd",  "secret4.lfd",  "secret4.lfd",  "secret4.lfd",  "secret5.lfd",
	"secret5.lfd", "secret5.lfd",  "secarm.lfd",   "awards.lfd",   "awards.lfd",   "awards.lfd",
	"awards.lfd",  "awards.lfd",   "awards.lfd",   "awards.lfd",   "awards.lfd",   "awards1.lfd",
	"awards1.lfd", "awards1.lfd",  "awards2.lfd",  "awards2.lfd",  "awards2.lfd",  "launch.lfd",
	"scene2.lfd",  "scene2.lfd",   "scene2.lfd",   "scene2.lfd",   "scene2.lfd",   "scene2.lfd",
	"scene2.lfd",  "secret.lfd",   "scene1.lfd",   "scene2.lfd",   "scene3.lfd",   "scene4.lfd",
	"scene4.lfd",  "scene5.lfd",   "scene6.lfd",   "emperor.lfd",  "scene8.lfd",   "scene8.lfd",
	"scene8.lfd",  "scene8.lfd",   "scene9.lfd",   "scene9.lfd",   "scene10.lfd",  "scene10.lfd",
	"scene11.lfd", "scene11.lfd",  "scene11.lfd",  "scene11.lfd",  "scene12.lfd",  "scene13.lfd",
	"scene13.lfd", "scene13.lfd",  "scene13.lfd",  "city.lfd",     "emperor.lfd",  "emperor.lfd",
	"emperor.lfd", "scene10.lfd",  "scene10.lfd",  "platform.lfd", "platform.lfd", "platform.lfd",
	"emperor.lfd", "emperor.lfd",
};

// GLOBAL: TIE95 0xD082E
// GLOBAL: TIE98 0x4E8CD8
char play1_film_str[86][10] = {
	"logo_f",   "perelogo", "stard_f",  "city1_f",  "emp1_f",   "swarma_f", "brdg1b_f", "plat_f",
	"chasea1f", "totrn_f",  "tocmbt_f", "cap_f",    "medic_f",  "fun_f",    "sec1_f",   "sec2_f",
	"sec2_f",   "sec2_f",   "sec3_f",   "sec4_f",   "sec5_f",   "sec6_f",   "sec7_f",   "sec8_f",
	"sec9_f",   "sec10_f",  "secarm_f", "awards",   "award1",   "award2",   "award3",   "award4",
	"award5",   "award6",   "award7",   "award8",   "award9",   "award10",  "award11",  "award12",
	"award13",  "lnch_f",   "newtour",  "landsd",   "landsd",   "landsd",   "landsd",   "landsd",
	"landsd",   "secret",   "scene1_f", "scene2_f", "scene3_f", "scene4a",  "scene4b",  "scene5_f",
	"scene6_f", "scene7_f", "battle8a", "battle8b", "battle8c", "battle8d", "scene9_f", "scene9b",
	"scene10a", "scene10b", "shot1",    "shot2",    "shot3",    "shot4",    "scene12",  "s1_v3",
	"s2-v2",    "s3-v10",   "scene13d", "sec_f",    "seca_f",   "secb_f",   "secc_f",   "secd_f",
	"sece_f",   "platb2_f", "chaseb_f", "chasec_f", "emp1b_f",  "emp1c_f",
};

/* Stream names keep the CD-root form; the hard-drive install
 * (install_cfg_mode 2) skips the leading backslash. */
// GLOBAL: TIE95 0xCFB6A
// GLOBAL: TIE98 0x4E8010
char play1_stream_str[86][24] = {
	"",
	"",
	"\\astream\\os1-v3.wrk",
	"",
	"",
	"\\astream\\swarm.wrk",
	"\\astream\\scene9e.wrk",
	"",
	"\\astream\\scene13a.wrk",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"\\astream\\shot2.wrk",
	"\\astream\\shot3.wrk",
	"",
	"",
	"\\astream\\s1_v3.wrk",
	"\\astream\\s2-v2.wrk",
	"\\astream\\s3-v10.wrk",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"\\astream\\scene12a.wrk",
	"",
	"\\astream\\scene15.wrk",
	"\\astream\\emp1b.wrk",
	"\\astream\\emp1c.wrk",
};

// GLOBAL: TIE95 0xD0B8A
// GLOBAL: TIE98 0x4E9038
static const char secret_film_str[12][10] = {
	"hall1_f", "hall2_f", "hall2_f", "hall2_f", "hall3_f", "hall3_f",
	"hall3_f", "hall3_f", "hall3_f", "hall3_f", "hall3_f", "hall3_f",
};

// GLOBAL: TIE95 0xCFB4E
// GLOBAL: TIE98 0x4E7FF0
static const char mission_disk1_resource[14] = "secarm1.lfd";
// GLOBAL: TIE95 0xCFB5C
// GLOBAL: TIE98 0x4E8000
static const char mission_disk2_resource[14] = "secarm2.lfd";

/* ---- Module state ---- */

// GLOBAL: TIE95 0xF64AC
// GLOBAL: TIE98 0x584DAC
static int16_t play1_id;
// GLOBAL: TIE95 0xF64A8
// GLOBAL: TIE98 0x584D90
static Film* play1_film;
// GLOBAL: TIE95 0xCFB4C
// GLOBAL: TIE98 0x584DB4
int16_t play1_is_streaming;
// GLOBAL: TIE95 0xF64B0
// GLOBAL: TIE98 0x584DB0
static int16_t read_state;
// GLOBAL: TIE95 0xF64AE
// GLOBAL: TIE98 0x584D88
static int16_t stream_actor_frames_to_go;
// GLOBAL: TIE95 0xF64B2
// GLOBAL: TIE98 0x584D8C
LandruHandle play1_read_buffer;
// GLOBAL: TIE95 0xF64B4
// GLOBAL: TIE98 0x584D84
static uint8_t use_chain_successful;
// GLOBAL: TIE95 0xF615C
// GLOBAL: TIE98 0x584D98
BitmapStruct play1_last_frame;
// GLOBAL: TIE95 0xF6148
// GLOBAL: TIE98 0x584D70
BitmapStruct play1_current_frame;

/* Forward declarations */
static void play1_user_Play_Arm(Actor* the_actor, int32_t time);
static void play1_Make_Literal_Actor(Actor* the_actor);
static int play1_Literal_Image(uint8_t* buffer, const uint8_t* image);
static void play1_Update_Stream_Actor(Actor* the_actor);
static int16_t play1_Draw_Stream_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y,
									   int16_t refresh);

/* ------------------------------------------------------------------ */

/*
 * View update callback. Each frame, checks if the film has finished
 * and handles scene exit. Scene 7 checks for lobo.lfd (expansion pack);
 * scene 910 redirects to the next battle cutscene.
 */
// FUNCTION: TIE95 0x78500
static void play1_end_View(int32_t time) {
	int16_t next_scene;
	int16_t skip_scene;
	int16_t scene;
	bool at_end;

	(void)time;
	next_scene = play1_next_scene[play1_id];
	skip_scene = play1_skip_scene[play1_id];

	/* Retail skips scene 7. */
	if (next_scene == 7) {
#ifdef TIE_MODERN
		/* PORT: demo data enters scene 7 only when LOBO.LFD exists. */
		if (TiePlay1_UsesDemoData()) {
			LandruFile* f = xfile_Open_File(LANDRU_FILE_ROOT_ASSET, "resource\\lobo.lfd", "rb");
			if (f)
				xfile_Close_File(f);
			else
				next_scene = 8;
		} else {
			next_scene = 8;
		}
#else
		next_scene = 8;
#endif
	}

	at_end = (play1_film->cur_cel == play1_film->cels);
	if (shellext_Check_Scene_Exit(&scene, next_scene, skip_scene, at_end)) {
		if (scene == 910)
			scene = shipext_Next_Battle_Cutscene();
		xerror_Set_Landru_Exit(scene);
	}
}

/* ------------------------------------------------------------------ */

/*
 * Film object callback. Processes actor objects (id == 3):
 * - var2 == 25: convert delta frames to literal format
 * - var1 == 15: additive blending with scene-dependent color offset
 * - var1 == 1, scene 420: install medal arm user callback
 * - var1 == 123: set up CD streaming actor
 * Returns 1 to suppress the actor.
 */
// FUNCTION: TIE95 0x78580
static int16_t play1_film_Callback(Film* the_film, FilmObject* film_object) {
	int16_t retval = 0;

	Actor* the_actor;
	int16_t cur_scene;

	if (film_object->id != 3)
		return retval;

	xfilm_Rewind_Actor_Film(the_film, film_object, (void*)(film_object + 1));
	the_actor = (Actor*)film_object->object;

	if (the_actor->var2 == 25)
		play1_Make_Literal_Actor(the_actor);

	cur_scene = shellext_Get_Cur_Scene();

	if (cur_scene == SCENE_COMBAT_TRANSITION) {
		if (the_actor->var1 == 15) {
			xactor_Set_Actor_Draw_Function(the_actor, deltadd_Draw_Delta_Add_Actor);
			xactor_Set_Actor_Color(the_actor, 112, 0);
		}
	} else if (cur_scene == SCENE_CUT_BATTLE_270) {
		if (the_actor->var1 == 15) {
			xactor_Set_Actor_Draw_Function(the_actor, deltadd_Draw_Delta_Add_Actor);
			xactor_Set_Actor_Color(the_actor, 160, 0);
		}
	} else if (cur_scene == SCENE_CUT_420) {
		if (the_actor->var1) {
			if (the_actor->var1 == 1) {
				xactor_Set_Actor_User_Function(the_actor, play1_user_Play_Arm);
			} else if (shipext_Get_Secret_Medal() - 2 < the_actor->var1) {
				retval = 1;
			}
		}
	}

	if (the_actor->var1 == 123) {
		int16_t ok;

		if (!play1_stream_str[play1_id][0] || !use_chain_successful)
			return 1;

		play1_read_buffer = xmemhdl_Alloc_Handle(STREAM_BUFFER_SIZE, LANDRU_MEMORY_DEFAULT);
		if (!play1_read_buffer)
			return 1;

		xbm_Init_Bitmap(&play1_last_frame);
		xbm_Init_Bitmap(&play1_current_frame);

		ok = xbm_Alloc_Bitmap(&play1_last_frame, 320, 200);
		if (ok)
			ok = xbm_Alloc_Bitmap(&play1_current_frame, 320, 200);

		if (!ok) {
			xmemhdl_Free_Handle(play1_read_buffer);
			play1_read_buffer = LANDRU_NULL_HANDLE;
			xbm_Free_Bitmap(&play1_last_frame);
			xbm_Free_Bitmap(&play1_current_frame);
			return 1;
		}

		xbm_Erase_Bitmap(&play1_last_frame);
		xbm_Erase_Bitmap(&play1_current_frame);
		xactor_Set_Actor_Update_Function(the_actor, (xactorUpdateFunc)play1_Update_Stream_Actor);
		xactor_Set_Actor_Draw_Function(the_actor, play1_Draw_Stream_Actor);
		xactor_Set_Actor_ZPlane(the_actor, 12700);
		xpal_Set_Screen_RGB(0, 255, 0, 0, 0);
		xcanvas_Erase_Canvas();
#ifdef TIE_MODERN
		xrect_Clear_Rect(&textext_bounds);
#endif
		play1_is_streaming = 1;
		read_state = 0;
	}

	return retval;
}

/* ------------------------------------------------------------------ */

/*
 * Actor user callback for the secret medal arm on scene 420.
 * On the first frame (time == 0), sets the actor state to
 * (secret_medal - 1), capped at state 2.
 */
// FUNCTION: TIE95 0x787C4
static void play1_user_Play_Arm(Actor* the_actor, int32_t time) {
	if (time == 0) {
		int16_t medal = shipext_Get_Secret_Medal();
		if (medal > 3)
			xactor_Set_Actor_State(the_actor, 2, 0);
		else
			xactor_Set_Actor_State(the_actor, medal - 1, 0);
	}
}

/* ------------------------------------------------------------------ */

/*
 * Convert an actor's delta-encoded frames to literal (uncompressed)
 * format. Uses the canvas bitmap as a 64000-byte scratch buffer.
 *
 * Retail-only sanity guard: only replaces a delta with its literal
 * expansion if 0 < literal_size < 48000. Avoids wasting memory when
 * the literal would be larger than the original delta encoding.
 */
enum {
	LITERAL_MAX_SIZE = 48000,
};

// FUNCTION: TIE95 0x78800
static void play1_Make_Literal_Actor(Actor* the_actor) {
	BitmapStruct* canvas_bm = xcanvas_Get_Current_Canvas_Bitmap();
	uint8_t* temp_buffer = (uint8_t*)xbm_Lock_Bitmap(canvas_bm);
	memset(temp_buffer, 0, 64000);

	if (the_actor->res_type == FOURCC_DELT) {
		if (the_actor->data) {
			const uint8_t* image = xmemhdl_Lock_Handle(the_actor->data);
			int size = play1_Literal_Image(temp_buffer, image);
			xmemhdl_Unlock_Handle(the_actor->data);

			if (size > 0 && size < LITERAL_MAX_SIZE) {
				LandruHandle new_data = xmemhdl_Data_To_Handle(temp_buffer, size, LANDRU_MEMORY_DEFAULT);
				if (new_data) {
					xmemhdl_Free_Handle(the_actor->data);
					the_actor->data = new_data;
				}
			}
		}
		memset(temp_buffer, 0, 64000);
	} else {
		int16_t num_frames = the_actor->arraySize;
		if (the_actor->array) {
			LandruHandle* arr = xmemhdl_Lock_Handle(the_actor->array);
			int16_t i;

			for (i = 0; i < num_frames; i++) {
				const uint8_t* image;
				int size;
				LandruHandle new_data;

				if (!arr[i])
					continue;

				image = xmemhdl_Lock_Handle(arr[i]);
				size = play1_Literal_Image(temp_buffer, image);
				xmemhdl_Unlock_Handle(arr[i]);

				if (size <= 0 || size >= LITERAL_MAX_SIZE) {
					memset(temp_buffer, 0, 64000);
					continue;
				}

				new_data = xmemhdl_Data_To_Handle(temp_buffer, size, LANDRU_MEMORY_DEFAULT);
				if (!new_data) {
					memset(temp_buffer, 0, 64000);
					break;
				}

				xmemhdl_Free_Handle(arr[i]);
				arr[i] = new_data;
				memset(temp_buffer, 0, 64000);
			}
			xmemhdl_Unlock_Handle(the_actor->array);
		}
	}

	xbm_Unlock_Bitmap(canvas_bm);
}

/* ------------------------------------------------------------------ */

/*
 * Delta-to-literal image decompressor. Copies the 8-byte header, then
 * processes scanlines. Each has a 2-byte length (low bit = compressed),
 * then 2+2 bytes of x/y data. Compressed: RLE packets (odd byte = fill,
 * even = copy). Uncompressed: raw pixel data. Strips compression bit
 * from the output length. 63000-byte overflow guard.
 */
// FUNCTION: TIE95 0x78A28
static int play1_Literal_Image(uint8_t* buffer, const uint8_t* image) {
	int32_t index, bindex;

	int16_t length;

	for (index = 0; index < 8; index++)
		buffer[index] = image[index];

	length = *(const int16_t*)(image + 8);
	buffer[index] = image[8] & 0xFE;
	buffer[index + 1] = image[9];
	bindex = 10;
	index += 2;

	while (length && (63000 - length) > index) {
		/* Copy 2-byte x position */
		buffer[index] = image[bindex];
		buffer[index + 1] = image[bindex + 1];
		bindex += 2;
		index += 2;
		/* Copy 2-byte y position */
		buffer[index] = image[bindex];
		buffer[index + 1] = image[bindex + 1];
		bindex += 2;
		index += 2;

		if (length & 1) {
			int16_t remaining = length >> 1;
			while (remaining) {
				uint8_t pack_byte = image[bindex++];
				uint8_t pack_len = pack_byte >> 1;
				if (pack_byte & 1) {
					uint8_t color = image[bindex++];
					memset(&buffer[index], color, pack_len);
				} else {
					memcpy(&buffer[index], &image[bindex], pack_len);
					bindex += pack_len;
				}
				index += pack_len;
				remaining -= pack_len;
			}
		} else {
			int16_t half_len = length >> 1;
			memcpy(&buffer[index], &image[bindex], half_len);
			bindex += half_len;
			index += half_len;
		}

		length = *(const int16_t*)(image + bindex);
		buffer[index] = image[bindex] & 0xFE;
		buffer[index + 1] = image[bindex + 1];
		bindex += 2;
		index += 2;
	}

	return index;
}

/* ------------------------------------------------------------------ */

/*
 * Stream actor update callback for CD FMV playback.
 * State 0: read 16-byte chunk header (frame count at WORD offset 2).
 * State 1: read frames (4-byte size + data), decode via
 * drawstrm_Convert_Frame_To_Palette.
 */
// FUNCTION: TIE95 0x78B70
static void play1_Update_Stream_Actor(Actor* the_actor) {
	uint32_t size;
	void* prev_pixels;
	void* cur_pixels;

	uint8_t* data;

	if (!xactor_Is_Actor_Visible(the_actor))
		return;
	if (!play1_is_streaming)
		return;
	if (xfilm_Is_Film_Fade())
		return;

	if (read_state == 0) {
		const uint8_t* data;

		if (xstream_Read_From_Stream_Buffer(0, play1_read_buffer, 0, 16, 1) != 16) {
			read_state = 0;
			xactor_Deactivate_Actor(the_actor);
			return;
		}
		data = xmemhdl_Lock_Handle(play1_read_buffer);
		stream_actor_frames_to_go = br_i16le(data + 2);
		xmemhdl_Unlock_Handle(play1_read_buffer);
		read_state = 1;
	}

	if (read_state != 1)
		return;

	if (stream_actor_frames_to_go <= 0) {
		xactor_Deactivate_Actor(the_actor);
		read_state = 0;
		return;
	}

	if (xstream_Read_From_Stream_Buffer(0, play1_read_buffer, 0, 4, 1) != 4) {
		read_state = 0;
		xactor_Deactivate_Actor(the_actor);
		return;
	}
	data = xmemhdl_Lock_Handle(play1_read_buffer);
	size = br_u32le(data);
	xmemhdl_Unlock_Handle(play1_read_buffer);

	if (size == 0 || size > STREAM_BUFFER_SIZE ||
		xstream_Read_From_Stream_Buffer(0, play1_read_buffer, 0, size, 1) != (int32_t)size) {
		read_state = 0;
		xactor_Deactivate_Actor(the_actor);
		return;
	}

	prev_pixels = xbm_Lock_Bitmap(&play1_last_frame);
	cur_pixels = xbm_Lock_Bitmap(&play1_current_frame);
	data = xmemhdl_Lock_Handle(play1_read_buffer);
	drawstrm_Convert_Frame_To_Palette(prev_pixels, data, cur_pixels);
	xmemhdl_Unlock_Handle(play1_read_buffer);
	xbm_Unlock_Bitmap(&play1_last_frame);
	xbm_Unlock_Bitmap(&play1_current_frame);
	stream_actor_frames_to_go--;
}

/* ------------------------------------------------------------------ */

/* Stream actor draw callback. Copies play1_current_frame to canvas. */
// FUNCTION: TIE95 0x78D2C
static int16_t play1_Draw_Stream_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y,
									   int16_t refresh) {
	(void)the_actor;
	(void)r;
	(void)clip_r;
	(void)off_x;
	(void)off_y;
	if (!refresh)
		return 0;
	xcanvas_Copy_Bitmap_To_Canvas(&play1_current_frame, 0, 0);
	xdirty_Max_Dirty_List();
	return 1;
}

/* ------------------------------------------------------------------ */

/*
 * Prepare CD streaming for the current and next scene. Tries Use first;
 * falls back to Chain+Use. Then pre-chains the next scene's stream.
 */
// FUNCTION: TIE95 0x78D54
static void play1_Chain_Scene(void) {
	char name[24];
	int16_t next_id;
	int16_t target;
	int16_t i;

	use_chain_successful = 0;

#ifdef TIE_MODERN
	/* PORT: stream files resolve relative to the installed data, like the
	 * hard-drive install (install_cfg_mode 2). */
	strcpy(name, play1_stream_str[play1_id] + 1);
#else
	if (install_cfg_mode <= 1)
		strcpy(name, play1_stream_str[play1_id]);
	else if (install_cfg_mode == 2)
		strcpy(name, play1_stream_str[play1_id] + 1);
#endif
	if (name[0]) {
		if (xstream_Use_Stream_File(0, name)) {
			use_chain_successful = 1;
		} else if (xstream_Chain_Stream_File(0, name) && xstream_Use_Stream_File(0, name)) {
			use_chain_successful = 1;
		}
	}

	/* Look up the next scene's index in cur_scene[]. Loop is bounded by
	 * cur_scene's 0 sentinel; retail bounded by next_scene[i] which has
	 * no sentinel and only terminated by accident of adjacent-global
	 * memory layout (ASan redzones break that coincidence). */
	next_id = 0;
	target = play1_next_scene[play1_id];
	for (i = 0; play1_cur_scene[i]; i++) {
		if (play1_cur_scene[i] == target) {
			next_id = i;
			break;
		}
	}
#ifdef TIE_MODERN
	strcpy(name, play1_stream_str[next_id] + 1);
#else
	if (install_cfg_mode <= 1)
		strcpy(name, play1_stream_str[next_id]);
	else if (install_cfg_mode == 2)
		strcpy(name, play1_stream_str[next_id] + 1);
#endif
	if (name[0])
		xstream_Chain_Stream_File(0, name);
}

/* ------------------------------------------------------------------ */

#ifdef TIE_MODERN
/*
 * Scenes after which retail restores 20fps once the view ends (every
 * scene whose film case changes the frame rate except 740). The modern
 * view task takes the answer up front because it runs the view
 * asynchronously.
 */
static bool play1_Scene_Changes_Frame_Rate(int16_t cur) {
	switch (cur) {
		case 10:
		case 30:
		case 31:
		case 32:
		case 50:
		case 60:
		case 61:
		case 70:
		case 71:
		case 72:
		case 500:
		case 510:
		case 520:
		case 530:
		case 531:
		case 550:
		case 560:
		case 570:
		case 571:
		case 572:
		case 573:
		case 580:
		case 581:
		case 590:
		case 591:
		case 600:
		case 601:
		case 602:
		case 603:
		case 610:
		case 620:
		case 621:
		case 622:
		case 623:
		case 700:
		case 710:
		case 720:
		case 730:
			return true;
		default:
			return false;
	}
}

#endif
/*
 * Main cutscene entry. Looks up the scene in play1_cur_scene[], selects
 * resources, film name, frame rate, creates the film, runs the view,
 * and cleans up on exit. Scenes that changed the frame rate get 20fps
 * restored after the view, except 740 which retail leaves at 20fps.
 */
// FUNCTION: TIE95 0x77CE0
// FUNCTION: TIE98 0x4682D0
int play1_Play1(SceneHeadStruct* the_head) {
	char name[16];
	Rect r;
	uint16_t i;
	int16_t scene;
	ResFile* file;
	ResFile* file2;
#ifdef TIE_MODERN
	LandruSurfaceSet surface_set = LANDRU_SURFACE_VGA;

	TiePlay1_SelectDataSet();
#endif

	for (i = 0; i < 205; i++)
		wrap_table[i] = 320 * i;
	wrap = 312;

	scene = shellext_Get_Cur_Scene();
	if (scene == SCENE_CUT_900)
		return xerror_Get_Landru_Exit();

#ifdef TIE_MODERN
	/* TIE98 leaves the TOTRAIN and TOCOMBAT transitions on the
	 * native SVGA target; every other PLAY1 film uses VGA. */
	if (TieProfile_UsesTie98Frontend() &&
		(scene == SCENE_TRAIN_TRANSITION || scene == SCENE_COMBAT_TRANSITION)) {
		surface_set = LANDRU_SURFACE_SVGA;
		(void)xsurface_Select_Surface_Set(surface_set);
		xview_Init_View(xview_Get_Current_View());
	}

#endif
	play1_id = 0;
	while (scene != play1_cur_scene[play1_id] && play1_cur_scene[play1_id])
		play1_id++;
	if (!play1_cur_scene[play1_id])
		return xerror_Get_Landru_Exit();

	file = shellext_Open_Empire_Resource(play1_resource_str[play1_id]);
	xcanvas_Get_Drawing_Canvas_Bounds(&r);
	file2 = NULL;

	switch (play1_cur_scene[play1_id]) {
		/* Scene 390: secret medal film */
		case 390:
			strcpy(name, secret_film_str[shipext_Get_Secret_Medal() - 1]);
			break;

		/* Scene 270: launch ship resource */
		case 270:
			shipext_Get_Launch_Name(name);
			file2 = file;
			file = shipext_Open_Launch_Resource();
			break;

		/* 24fps + file2 = "bridge.lfd" */
		case 520:
			xtimer_Set_Frame_Rate(24);
			strcpy(name, play1_film_str[play1_id]);
			file2 = shellext_Open_Empire_Resource(play1_resource_str[6]);
			break;

		case 70:
			xtimer_Set_Frame_Rate(24);
			strcpy(name, play1_film_str[play1_id]);
			break;

		/* 20fps, standard film */
		case 10:
		case 60:
		case 61:
		case 71:
		case 72:
		case 610:
		case 620:
		case 621:
		case 622:
			xtimer_Set_Frame_Rate(20);
			strcpy(name, play1_film_str[play1_id]);
			break;

		case 600:
			xtimer_Set_Frame_Rate(20);
			strcpy(name, play1_film_str[play1_id]);
			break;

		case 601:
			xtimer_Set_Frame_Rate(20);
			strcpy(name, play1_film_str[play1_id]);
			break;

		case 602:
			xtimer_Set_Frame_Rate(20);
			strcpy(name, play1_film_str[play1_id]);
			break;

		case 603:
			xtimer_Set_Frame_Rate(20);
			strcpy(name, play1_film_str[play1_id]);
			break;

		/* 24fps, standard film */
		case 30:
		case 31:
		case 32:
		case 50:
		case 500:
		case 510:
		case 530:
		case 531:
		case 550:
		case 560:
		case 700:
		case 710:
		case 720:
			xtimer_Set_Frame_Rate(24);
			strcpy(name, play1_film_str[play1_id]);
			break;

		/* 24fps + file2 = "emperor.lfd" */
		case 590:
		case 591:
		case 730:
			xtimer_Set_Frame_Rate(24);
			strcpy(name, play1_film_str[play1_id]);
			file2 = shellext_Open_Empire_Resource(play1_resource_str[4]);
			break;

		/* 20fps + file2 = "emperor.lfd" */
		case 740:
			xtimer_Set_Frame_Rate(20);
			strcpy(name, play1_film_str[play1_id]);
			file2 = shellext_Open_Empire_Resource(play1_resource_str[4]);
			break;

		case 570:
		case 571:
		case 572:
		case 573:
			xtimer_Set_Frame_Rate(24);
			strcpy(name, play1_film_str[play1_id]);
			break;

		/* 24fps + file2 from peer resource */
		case 580:
			xtimer_Set_Frame_Rate(24);
			strcpy(name, play1_film_str[play1_id]);
			file2 = shellext_Open_Empire_Resource(play1_resource_str[play1_id - 11]);
			break;

		/* 24fps + file2 = "bridge.lfd" */
		case 581:
			xtimer_Set_Frame_Rate(24);
			strcpy(name, play1_film_str[play1_id]);
			file2 = shellext_Open_Empire_Resource(play1_resource_str[6]);
			break;

		/* 20fps + file2 = "scene10.lfd" */
		case 623:
			xtimer_Set_Frame_Rate(20);
			strcpy(name, play1_film_str[play1_id]);
			file2 = shellext_Open_Empire_Resource(play1_resource_str[80]);
			break;

		/* Award scenes 258-263: film + file2 = "awards.lfd" */
		case 258:
		case 259:
		case 260:
		case 261:
		case 262:
		case 263:
			strcpy(name, play1_film_str[play1_id]);
			file2 = shellext_Open_Empire_Resource(play1_resource_str[27]);
			break;

		/* Scene 420: mission disk resource swapping. The mission-disk
		 * arming cutscene uses film "secarm2f" (only present in
		 * secarm{1,2}.lfd), not the base "secarm_f" from secarm.lfd. */
		case 420:
			strcpy(name, play1_film_str[play1_id]);
			if (shipext_Is_Mission_Disk1() || shipext_Is_Mission_Disk2()) {
				strcpy(name, "secarm2f");
				xres_Close_Resource(file);
				if (shipext_Is_Mission_Disk2())
					file = shellext_Open_Empire_Resource(mission_disk2_resource);
				else if (shipext_Is_Mission_Disk1())
					file = shellext_Open_Empire_Resource(mission_disk1_resource);
				file2 = shellext_Open_Empire_Resource(play1_resource_str[play1_id]);
			}
			break;

		default:
			strcpy(name, play1_film_str[play1_id]);
			break;
	}

#ifdef TIE_MODERN
	/* Tag the snapshot with the (LFD basename, film name) tuple so a
	 * cutscene compositor on the host side can locate its remaster
	 * asset bundle. The basename is the resource_str entry minus the
	 * ".lfd" extension; the setter uppercases to match retail asset
	 * directory conventions. Cleared in PLAY1_PHASE_CLEANUP. */
	{
		char lfd_base[16];
		const char* res = play1_resource_str[play1_id];
		size_t i = 0;
		for (; i + 1 < sizeof lfd_base && res[i] && res[i] != '.'; ++i)
			lfd_base[i] = res[i];
		lfd_base[i] = '\0';
		TieSnapshotBuilder_SetActiveFilm(lfd_base, name);
	}

#endif
	play1_Chain_Scene();
#ifdef TIE_MODERN
	TieDiagnostics_Log(TIE_LOG_INFO, "[PLAY1] scene=%d film='%s' resource='%s' stream='%s'\n", play1_id, name,
					   play1_resource_str[play1_id],
					   play1_stream_str[play1_id][0] ? play1_stream_str[play1_id] : "(none)");
#endif
	play1_film = xfilm_Res_Callback_Film(name, &r, 0, 0, 0, play1_film_Callback);
	if (!play1_film) {
		if (file2)
			xres_Close_Resource(file2);
		xres_Close_Resource(file);
#ifdef TIE_MODERN
		xerror_Set_Landru_Exit(play1_next_scene[play1_id]);
#endif
		return play1_next_scene[play1_id];
	}
#ifdef TIE_MODERN
	if (TieMusicPolicy_UsesTie98()) {
		/* TIE98 plays the cutscene's digital score after creating the film. */
		switch (scene) {
			case 6:
				FrontendWaveStream_PlayWaveFile("music/tieintro.wav", 0);
				break;
			case 25:
				FrontendWaveStream_PlayWaveFile("music/emperor.wav", 0);
				break;
			case 120:
				FrontendWaveStream_PlayWaveFile("music/trainpod.wav", 0);
				break;
			case 130:
				FrontendWaveStream_PlayWaveFile("music/fightpod.wav", 0);
				break;
			case 210:
				FrontendWaveStream_PlayWaveFile("music/starlog.wav", 0);
				break;
			case 240:
				FrontendWaveStream_PlayWaveFile("music/funeral.wav", 0);
				break;
			case 270:
				FrontendWaveStream_PlayWaveFile("music/launch.wav", 0);
				break;
			case 280:
				FrontendWaveStream_PlayWaveFile("music/medical.wav", 0);
				break;
			case 281:
			case 282:
				FrontendWaveStream_PlayWaveFile("music/battle7.wav", 0);
				break;
			case 283:
				FrontendWaveStream_PlayWaveFile("music/medals.wav", 0);
				break;
			case 284:
				FrontendWaveStream_PlayWaveFile("music/awe.wav", 0);
				break;
			case 500:
				FrontendWaveStream_PlayWaveFile("music/battle1.wav", 0);
				break;
			case 510:
				FrontendWaveStream_PlayWaveFile("music/battle2.wav", 0);
				break;
			case 520:
				FrontendWaveStream_PlayWaveFile("music/battle3.wav", 0);
				break;
			case 530:
				FrontendWaveStream_PlayWaveFile("music/battle4.wav", 0);
				break;
			case 540:
				FrontendWaveStream_PlayWaveFile("music/battle5.wav", 0);
				break;
			case 550:
				FrontendWaveStream_PlayWaveFile("music/battle6.wav", 0);
				break;
			case 560:
				/* PORT: TIE98 has no score for scene 560. */
				FrontendWaveStream_PlayWaveFile("music/battle7.wav", 0);
				break;
			case 570:
				FrontendWaveStream_PlayWaveFile("music/battle8.wav", 0);
				break;
			case 580:
				FrontendWaveStream_PlayWaveFile("music/battle9.wav", 0);
				break;
			case 590:
				FrontendWaveStream_PlayWaveFile("music/battle10.wav", 0);
				break;
			case 600:
				FrontendWaveStream_PlayWaveFile("music/battle11.wav", 0);
				break;
			case 610:
				FrontendWaveStream_PlayWaveFile("music/battle12.wav", 0);
				break;
			case 620:
				FrontendWaveStream_PlayWaveFile("music/battle13.wav", 0);
				break;
			default:
				break;
		}
	}

#endif
	xfilm_Set_Film_Def_Palette(play1_film, the_head->def_palette);
	xview_Set_View_Update_Function(play1_end_View);

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	/* Start palette cycling for scenes 20 and 25 */
	switch (scene) {
		case 20:
		case 25: {
			Palette* pal;

			for (pal = xpal_Ask_Palette_List(); pal; pal = pal->next) {
				if (pal->cycle_count)
					xpal_Start_Cycle(pal);
			}
			break;
		}
	}
#ifdef TIE_MODERN
	TieFilm_RunView(file, file2, scene, play1_Scene_Changes_Frame_Rate(play1_cur_scene[play1_id]), play1_is_streaming != 0, surface_set);
	return 0;
#else
	shellext_Handle_TIE_View();
	/* Restore 20fps for scenes whose film changed the frame rate */
	switch (play1_cur_scene[play1_id]) {
		case 10:
		case 30:
		case 31:
		case 32:
		case 50:
		case 60:
		case 61:
		case 70:
		case 71:
		case 72:
		case 500:
		case 510:
		case 520:
		case 530:
		case 531:
		case 550:
		case 560:
		case 570:
		case 571:
		case 572:
		case 573:
		case 580:
		case 581:
		case 590:
		case 591:
		case 600:
		case 601:
		case 602:
		case 603:
		case 610:
		case 620:
		case 621:
		case 622:
		case 623:
		case 700:
		case 710:
		case 720:
		case 730:
			xtimer_Set_Frame_Rate(20);
			break;
	}

	/* Tear down streaming */
	if (play1_is_streaming) {
		xstream_Unchain_Current_Stream_File(0);
		play1_is_streaming = 0;
		if (play1_read_buffer) {
			xmemhdl_Free_Handle(play1_read_buffer);
		}
		if (play1_last_frame.data)
			xbm_Free_Bitmap(&play1_last_frame);
		if (play1_current_frame.data)
			xbm_Free_Bitmap(&play1_current_frame);
	}

	xview_Clear_View_Update_Function();
	if (file2)
		xres_Close_Resource(file2);
	xres_Close_Resource(file);

	return xerror_Get_Landru_Exit();
#endif
}
