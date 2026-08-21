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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "tie/play1.h"
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
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include "util/binio.h"
#include <landru/task.h>

#include "tie/deltadd.h"
#include "tie/drawstrm.h"

#define STREAM_BUFFER_SIZE 128000

static const char* play1_tie98_music_path(int16_t scene) {
	switch (scene) {
		case 6:
			return "music/tieintro.wav";
		case 25:
			return "music/emperor.wav";
		case 120:
			return "music/trainpod.wav";
		case 130:
			return "music/fightpod.wav";
		case 210:
			return "music/starlog.wav";
		case 240:
			return "music/funeral.wav";
		case 270:
			return "music/launch.wav";
		case 280:
			return "music/medical.wav";
		case 281:
		case 282:
			return "music/battle7.wav";
		case 283:
			return "music/medals.wav";
		case 284:
			return "music/awe.wav";
		default:
			break;
	}
	if (scene >= 500 && scene <= 620 && scene % 10 == 0) {
		static char battle_path[32];
		snprintf(battle_path, sizeof battle_path, "music/battle%d.wav", (scene - 490) / 10);
		return battle_path;
	}
	return NULL;
}

static bool play1_tie98_music_ends_after_scene(int16_t scene) {
	if (scene >= 251 && scene <= 263)
		return true;

	switch (scene) {
		case 90:
		case 210:
		case 230:
		case 240:
		case 270:
		case 420:
		case 500:
		case 510:
		case 520:
		case 531:
		case 550:
		case 573:
		case 581:
		case 591:
		case 603:
		case 610:
		case 623:
		case 700:
		case 710:
		case 720:
		case 730:
		case 740:
			return true;
		default:
			return false;
	}
}

/* Wrap table for 320-wide scanline offsets (used by rendering subsystems) */
static int32_t wrap_table[205];
static int32_t wrap;

/* ---- Scene lookup tables ---- */
/*
 * Two parallel sets exist: one matches the LecDemos sample-disc data
 * layout (under STREAM/), the other matches the Collector's CD retail
 * layout (under ASTREAM/). They differ in late-game scene IDs, secret
 * medal films, a few resource files, and the FMV directory name.
 *
 * Selection is decided once at startup by probing the data directory.
 * Indexed access goes through the selected play1_data_set_t.
 */

/* ---- Demo data set (LecDemos sample disc) ---- */

static const int16_t play1_cur_scene_demo[87] = {
	6,   7,   10,  20,  30,  40,  50,  60,  70,  120, 130, 210, 231, 240, 400, 401, 402, 403,
	404, 405, 406, 407, 408, 409, 410, 411, 420, 250, 251, 252, 253, 254, 255, 256, 257, 258,
	259, 260, 261, 262, 263, 270, 170, 280, 281, 282, 283, 284, 285, 390, 500, 510, 520, 530,
	531, 540, 550, 560, 570, 571, 572, 573, 580, 581, 590, 591, 600, 601, 602, 603, 610, 611,
	612, 620, 621, 622, 25,  700, 710, 720, 730, 61,  71,  72,  31,  32,  0,
};

static const int16_t play1_next_scene_demo[86] = {
	7,   8,   20,  30,  40,  50,  61,  61,  71,  121, 131, 910, 910, 910, 420, 420, 420, 420,
	420, 420, 420, 420, 420, 420, 420, 420, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 231, 910, 910, 910, 910, 910, 910, 910, 910, 910, 531,
	910, 910, 910, 910, 571, 572, 573, 910, 581, 910, 591, 910, 601, 602, 603, 910, 611, 612,
	910, 621, 622, 910, 910, 910, 910, 910, 910, 70,  72,  80,  32,  40,
};

static const int16_t play1_skip_scene_demo[86] = {
	100, 100, 100, 100, 100, 100, 100, 100, 100, 121, 131, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 100, 100, 100, 100, 100,
};

static const char play1_resource_str_demo[86][14] = {
	"logo.lfd",    "perelogo.lfd", "stardest.lfd", "city.lfd",     "emperor.lfd",  "swarm.lfd",
	"bridge.lfd",  "platform.lfd", "platform.lfd", "totrain.lfd",  "tocombat.lfd", "capture.lfd",
	"medical.lfd", "funeral.lfd",  "secret1.lfd",  "secret2.lfd",  "secret2.lfd",  "secret2.lfd",
	"secret3.lfd", "secret3.lfd",  "secret4.lfd",  "secret4.lfd",  "secret4.lfd",  "secret4.lfd",
	"secret4.lfd", "secret4.lfd",  "secarm.lfd",   "awards.lfd",   "awards.lfd",   "awards.lfd",
	"awards.lfd",  "awards.lfd",   "awards.lfd",   "awards.lfd",   "awards.lfd",   "awards1.lfd",
	"awards1.lfd", "awards1.lfd",  "awards2.lfd",  "awards2.lfd",  "awards2.lfd",  "launch.lfd",
	"scene2.lfd",  "scene2.lfd",   "scene2.lfd",   "scene2.lfd",   "scene2.lfd",   "scene2.lfd",
	"scene2.lfd",  "secret.lfd",   "scene1.lfd",   "scene2.lfd",   "scene3.lfd",   "scene4.lfd",
	"scene4.lfd",  "scene5.lfd",   "scene6.lfd",   "emperor.lfd",  "scene8.lfd",   "scene8.lfd",
	"scene8.lfd",  "scene8.lfd",   "scene9.lfd",   "scene9.lfd",   "scene10.lfd",  "scene10.lfd",
	"scene11.lfd", "scene11.lfd",  "scene11.lfd",  "scene11.lfd",  "scene12.lfd",  "scene12.lfd",
	"scene12.lfd", "scene13.lfd",  "scene13.lfd",  "scene13.lfd",  "city.lfd",     "emperor.lfd",
	"emperor.lfd", "emperor.lfd",  "scene10.lfd",  "platform.lfd", "platform.lfd", "platform.lfd",
	"emperor.lfd", "emperor.lfd",
};

static const char play1_film_str_demo[86][10] = {
	"logo_f",   "perelogo", "stard_f",  "city1_f",  "emp1_f",   "swarma_f", "brdg1b_f", "plat_f",
	"chasea1f", "totrn_f",  "tocmbt_f", "cap_f",    "medic_f",  "fun_f",    "sec1_f",   "sec2_f",
	"sec2_f",   "sec2_f",   "sec3_f",   "sec4_f",   "sec5_f",   "sec6_f",   "sec7_f",   "sec5_f",
	"sec5_f",   "sec5_f",   "secarm_f", "awards",   "award1",   "award2",   "award3",   "award4",
	"award5",   "award6",   "award7",   "award8",   "award9",   "award10",  "award11",  "award12",
	"award13",  "lnch_f",   "newtour",  "landsd",   "landsd",   "landsd",   "landsd",   "landsd",
	"landsd",   "secret",   "scene1_f", "scene2_f", "scene3_f", "scene4a",  "scene4b",  "scene5_f",
	"scene6_f", "scene7_f", "battle8a", "battle8b", "battle8c", "battle8d", "scene9_f", "scene9b",
	"scene10a", "scene10b", "shot1",    "shot2",    "shot3",    "shot4",    "s1_v3",    "s2-v2",
	"s3-v10",   "s1_v3",    "s2-v2",    "s3-v10",   "sec_f",    "seca_f",   "secb_f",   "secc_f",
	"secd_f",   "platb2_f", "chaseb_f", "chasec_f", "emp1b_f",  "emp1c_f",
};

static const char play1_stream_str_demo[86][24] = {
	"",
	"",
	"stream\\os1-v3.wrk",
	"",
	"",
	"stream\\swarm.wrk",
	"stream\\scene9e.wrk",
	"",
	"stream\\scene13a.wrk",
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
	"stream\\shot1.wrk",
	"stream\\shot2.wrk",
	"stream\\shot3.wrk",
	"stream\\shot4.wrk",
	"stream\\s1_v3.wrk",
	"stream\\s2-v2.wrk",
	"stream\\s3-v10.wrk",
	"stream\\s1_v3.wrk",
	"stream\\s2-v2.wrk",
	"stream\\s3-v10.wrk",
	"",
	"",
	"",
	"",
	"",
	"stream\\scene12a.wrk",
	"",
	"stream\\scene15.wrk",
	"stream\\emp1b.wrk",
	"stream\\emp1c.wrk",
};

/* ---- Retail data set (Collector's CD) ---- */

static const int16_t play1_cur_scene_retail[87] = {
	6,   7,   10,  20,  30,  40,  50,  60,  70,  120, 130, 210, 231, 240, 400, 401, 402, 403,
	404, 405, 406, 407, 408, 409, 410, 411, 420, 250, 251, 252, 253, 254, 255, 256, 257, 258,
	259, 260, 261, 262, 263, 270, 170, 280, 281, 282, 283, 284, 285, 390, 500, 510, 520, 530,
	531, 540, 550, 560, 570, 571, 572, 573, 580, 581, 590, 591, 600, 601, 602, 603, 610, 620,
	621, 622, 623, 25,  700, 710, 720, 730, 740, 61,  71,  72,  31,  32,  0,
};

static const int16_t play1_next_scene_retail[86] = {
	7,   8,   20,  30,  40,  50,  60,  61,  71,  121, 131, 910, 910, 910, 420, 420, 420, 420,
	420, 420, 420, 420, 420, 420, 420, 420, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 231, 910, 910, 910, 910, 910, 910, 910, 910, 910, 531,
	910, 910, 910, 910, 571, 572, 573, 910, 581, 910, 591, 910, 601, 602, 603, 910, 910, 621,
	622, 623, 910, 910, 910, 910, 910, 910, 910, 70,  72,  80,  32,  40,
};

static const int16_t play1_skip_scene_retail[86] = {
	100, 100, 100, 100, 100, 100, 100, 100, 100, 121, 131, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 100, 100, 100, 100, 100,
};

static const char play1_resource_str_retail[86][14] = {
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

static const char play1_film_str_retail[86][10] = {
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

static const char play1_stream_str_retail[86][24] = {
	"",
	"",
	"astream\\os1-v3.wrk",
	"",
	"",
	"astream\\swarm.wrk",
	"astream\\scene9e.wrk",
	"",
	"astream\\scene13a.wrk",
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
	"astream\\shot2.wrk",
	"astream\\shot3.wrk",
	"",
	"",
	"astream\\s1_v3.wrk",
	"astream\\s2-v2.wrk",
	"astream\\s3-v10.wrk",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"astream\\scene12a.wrk",
	"",
	"astream\\scene15.wrk",
	"astream\\emp1b.wrk",
	"astream\\emp1c.wrk",
};

/* ---- Data-set descriptor + runtime selector ---- */

typedef struct {
	const char* name;
	const int16_t* cur_scene;  /* 87 entries, ends with 0 sentinel */
	const int16_t* next_scene; /* 86 entries */
	const int16_t* skip_scene; /* 86 entries */
	const char (*resource_str)[14];
	const char (*film_str)[10];
	const char (*stream_str)[24];
} play1_data_set_t;

static const play1_data_set_t play1_set_demo = {
	.name = "demo",
	.cur_scene = play1_cur_scene_demo,
	.next_scene = play1_next_scene_demo,
	.skip_scene = play1_skip_scene_demo,
	.resource_str = play1_resource_str_demo,
	.film_str = play1_film_str_demo,
	.stream_str = play1_stream_str_demo,
};

static const play1_data_set_t play1_set_retail = {
	.name = "retail",
	.cur_scene = play1_cur_scene_retail,
	.next_scene = play1_next_scene_retail,
	.skip_scene = play1_skip_scene_retail,
	.resource_str = play1_resource_str_retail,
	.film_str = play1_film_str_retail,
	.stream_str = play1_stream_str_retail,
};

/*
 * Probe the data directory once and pick demo vs retail. The retail
 * Collector's CD stores FMV under ASTREAM/; the LecDemos sample disc
 * uses STREAM/. Probes both case spellings since DOS filesystems are
 * case-insensitive but Unix is not.
 */
static const play1_data_set_t* play1_data_set(void) {
	static const play1_data_set_t* cached = NULL;
	if (cached)
		return cached;

	if (TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "astream") ||
		TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "ASTREAM")) {
		cached = &play1_set_retail;
	} else {
		cached = &play1_set_demo;
	}
	TieDiagnostics_Log(TIE_LOG_INFO, "[PLAY1] data set = %s\n", cached->name);
	return cached;
}

static const char secret_film_str[12][10] = {
	"hall1_f", "hall2_f", "hall2_f", "hall2_f", "hall3_f", "hall3_f",
	"hall3_f", "hall3_f", "hall3_f", "hall3_f", "hall3_f", "hall3_f",
};

static const char mission_disk1_resource[14] = "secarm1.lfd";
static const char mission_disk2_resource[14] = "secarm2.lfd";

/* ---- Module state ---- */

static int16_t play1_id;
static Film* play1_film;
static int16_t is_streaming;
static int16_t read_state;
static int16_t stream_actor_frames_to_go;
static void* read_buffer;
static uint8_t use_chain_successful;
static BitmapStruct last_frame;
static BitmapStruct current_frame;

/* Forward declarations */
static void play1_user_Play_Arm(Actor* the_actor, int32_t time);
static void play1_Make_Literal_Actor(Actor* the_actor);
static int play1_Literal_Image(uint8_t* buffer, const uint8_t* image);
static void play1_Update_Stream_Actor(Actor* the_actor);
static int play1_Draw_Stream_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y,
								   int16_t refresh);

/* ------------------------------------------------------------------ */

/*
 * View update callback. Each frame, checks if the film has finished
 * and handles scene exit. Scene 7 checks for lobo.lfd (expansion pack);
 * scene 910 redirects to the next battle cutscene.
 */
// FUNCTION: TIE 0x78500
static void play1_end_View(int32_t time) {
	(void)time;
	const play1_data_set_t* p = play1_data_set();
	int16_t next_scene = p->next_scene[play1_id];
	int16_t skip_scene = p->skip_scene[play1_id];

	/* Retail data skips scene 7. Demo data enters it only when LOBO.LFD exists. */
	if (next_scene == 7) {
		if (p == &play1_set_retail) {
			next_scene = 8;
		} else {
			LandruFile* f = lfile_Open_File(LANDRU_FILE_ROOT_ASSET, "resource\\lobo.lfd", "rb");
			if (f)
				lfile_Close_File(f);
			else
				next_scene = 8;
		}
	}

	int16_t scene;
	bool at_end = (play1_film->cur_cel == play1_film->cels);
	if (shellext_Check_Scene_Exit(&scene, next_scene, skip_scene, at_end)) {
		if (scene == 910)
			scene = shipext_Next_Battle_Cutscene();
		lerror_Set_Landru_Exit(scene);
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
// FUNCTION: TIE 0x78580
static int16_t play1_film_Callback(Film* the_film, FilmObject* film_object) {
	int16_t retval = 0;

	if (film_object->id != 3)
		return retval;

	lfilm_Rewind_Actor_Film(the_film, film_object, (void*)(film_object + 1));
	Actor* the_actor = (Actor*)film_object->object;

	if (the_actor->var2 == 25)
		play1_Make_Literal_Actor(the_actor);

	int16_t cur_scene = shellext_Get_Cur_Scene();

	if (cur_scene == SCENE_COMBAT_TRANSITION) {
		if (the_actor->var1 == 15) {
			lactor_Set_Actor_Draw_Function(the_actor, (lactorDrawFunc)deltadd_Draw_Delta_Add_Actor);
			lactor_Set_Actor_Color(the_actor, 112, 0);
		}
	} else if (cur_scene == SCENE_CUT_BATTLE_270) {
		if (the_actor->var1 == 15) {
			lactor_Set_Actor_Draw_Function(the_actor, (lactorDrawFunc)deltadd_Draw_Delta_Add_Actor);
			lactor_Set_Actor_Color(the_actor, 160, 0);
		}
	} else if (cur_scene == SCENE_CUT_420) {
		if (the_actor->var1) {
			if (the_actor->var1 == 1) {
				lactor_Set_Actor_User_Function(the_actor, play1_user_Play_Arm);
			} else if (shipext_Get_Secret_Medal() - 2 < the_actor->var1) {
				retval = 1;
			}
		}
	}

	if (the_actor->var1 == 123) {
		const play1_data_set_t* p = play1_data_set();
		if (!p->stream_str[play1_id][0] || !use_chain_successful)
			return 1;

		read_buffer = malloc(STREAM_BUFFER_SIZE);
		if (!read_buffer)
			return 1;

		lbitmap_Init_Bitmap(&last_frame);
		lbitmap_Init_Bitmap(&current_frame);

		int16_t ok = lbitmap_Alloc_Bitmap(&last_frame, 320, 200);
		if (ok)
			ok = lbitmap_Alloc_Bitmap(&current_frame, 320, 200);

		if (!ok) {
			free(read_buffer);
			read_buffer = NULL;
			lbitmap_Free_Bitmap(&last_frame);
			lbitmap_Free_Bitmap(&current_frame);
			return 1;
		}

		lbitmap_Erase_Bitmap(&last_frame);
		lbitmap_Erase_Bitmap(&current_frame);
		lactor_Set_Actor_Update_Function(the_actor, (lactorUpdateFunc)play1_Update_Stream_Actor);
		lactor_Set_Actor_Draw_Function(the_actor, (lactorDrawFunc)play1_Draw_Stream_Actor);
		lactor_Set_Actor_ZPlane(the_actor, 12700);
		lpal_Set_Screen_RGB(0, 255, 0, 0, 0);
		lcanvas_Erase_Canvas();
		textext_Clear_Text_Bounds_Rect();
		is_streaming = 1;
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
// FUNCTION: TIE 0x787C4
static void play1_user_Play_Arm(Actor* the_actor, int32_t time) {
	if (time == 0) {
		int16_t medal = shipext_Get_Secret_Medal();
		if (medal > 3)
			lactor_Set_Actor_State(the_actor, 2, 0);
		else
			lactor_Set_Actor_State(the_actor, medal - 1, 0);
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
#define LITERAL_MAX_SIZE 48000

// FUNCTION: TIE 0x78800
static void play1_Make_Literal_Actor(Actor* the_actor) {
	BitmapStruct* canvas_bm = lcanvas_Get_Current_Canvas_Bitmap();
	uint8_t* temp_buffer = (uint8_t*)lbitmap_Lock_Bitmap(canvas_bm);
	memset(temp_buffer, 0, 64000);

	if (the_actor->res_type == FOURCC_DELT) {
		if (the_actor->data) {
			uint8_t* image = (uint8_t*)the_actor->data;
			int size = play1_Literal_Image(temp_buffer, image);

			if (size > 0 && size < LITERAL_MAX_SIZE) {
				uint8_t* new_data = malloc(size);
				if (new_data) {
					memcpy(new_data, temp_buffer, size);
					free(the_actor->data);
					the_actor->data = new_data;
				}
			}
		}
		memset(temp_buffer, 0, 64000);
	} else {
		int16_t num_frames = the_actor->arraySize;
		if (the_actor->array) {
			for (int16_t i = 0; i < num_frames; i++) {
				void** arr = the_actor->array;
				if (!arr[i])
					continue;

				uint8_t* image = (uint8_t*)arr[i];
				int size = play1_Literal_Image(temp_buffer, image);

				if (size <= 0 || size >= LITERAL_MAX_SIZE) {
					memset(temp_buffer, 0, 64000);
					continue;
				}

				uint8_t* new_data = malloc(size);
				if (!new_data) {
					memset(temp_buffer, 0, 64000);
					break;
				}

				memcpy(new_data, temp_buffer, size);
				free(arr[i]);
				arr[i] = new_data;
				memset(temp_buffer, 0, 64000);
			}
		}
	}

	lbitmap_Unlock_Bitmap(canvas_bm);
}

/* ------------------------------------------------------------------ */

/*
 * Delta-to-literal image decompressor. Copies the 8-byte header, then
 * processes scanlines. Each has a 2-byte length (low bit = compressed),
 * then 2+2 bytes of x/y data. Compressed: RLE packets (odd byte = fill,
 * even = copy). Uncompressed: raw pixel data. Strips compression bit
 * from the output length. 63000-byte overflow guard.
 */
// FUNCTION: TIE 0x78A28
static int play1_Literal_Image(uint8_t* buffer, const uint8_t* image) {
	int32_t index, bindex;

	for (index = 0; index < 8; index++)
		buffer[index] = image[index];

	int16_t length = *(const int16_t*)(image + 8);
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
// FUNCTION: TIE 0x78B70
static void play1_Update_Stream_Actor(Actor* the_actor) {
	if (!lactor_Is_Actor_Visible(the_actor))
		return;
	if (!is_streaming)
		return;
	if (lfilm_Is_Film_Fade())
		return;

	if (read_state == 0) {
		if (lstream_Read_From_Stream_Buffer(read_buffer, 16, 1) != 16) {
			read_state = 0;
			lactor_Deactivate_Actor(the_actor);
			return;
		}
		stream_actor_frames_to_go = br_i16le(read_buffer + 2);
		read_state = 1;
	}

	if (read_state != 1)
		return;

	if (stream_actor_frames_to_go <= 0) {
		lactor_Deactivate_Actor(the_actor);
		read_state = 0;
		return;
	}

	if (lstream_Read_From_Stream_Buffer(read_buffer, 4, 1) == 0) {
		read_state = 0;
		lactor_Deactivate_Actor(the_actor);
		return;
	}
	uint32_t size = br_u32le(read_buffer);

	if (lstream_Read_From_Stream_Buffer(read_buffer, size, 1) != size) {
		read_state = 0;
		lactor_Deactivate_Actor(the_actor);
		return;
	}

	void* prev_pixels = lbitmap_Lock_Bitmap(&last_frame);
	void* cur_pixels = lbitmap_Lock_Bitmap(&current_frame);
	drawstrm_Convert_Frame_To_Palette(prev_pixels, read_buffer, cur_pixels);
	lbitmap_Unlock_Bitmap(&last_frame);
	lbitmap_Unlock_Bitmap(&current_frame);
	stream_actor_frames_to_go--;
}

/* ------------------------------------------------------------------ */

/* Stream actor draw callback. Copies current_frame to canvas. */
// FUNCTION: TIE 0x78D2C
static int play1_Draw_Stream_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y,
								   int16_t refresh) {
	(void)the_actor;
	(void)r;
	(void)clip_r;
	(void)off_x;
	(void)off_y;
	if (!refresh)
		return 0;
	lcanvas_Copy_Bitmap_To_Canvas(&current_frame, 0, 0);
	ldirty_Max_Dirty_List();
	return 1;
}

/* ------------------------------------------------------------------ */

/*
 * Prepare CD streaming for the current and next scene. Tries Use first;
 * falls back to Chain+Use. Then pre-chains the next scene's stream.
 */
// FUNCTION: TIE 0x78D54
static void play1_Chain_Scene(void) {
	const play1_data_set_t* p = play1_data_set();
	use_chain_successful = 0;

	if (p->stream_str[play1_id][0]) {
		if (lstream_Use_Stream_File(p->stream_str[play1_id])) {
			use_chain_successful = 1;
		} else if (lstream_Chain_Stream_File(p->stream_str[play1_id]) &&
				   lstream_Use_Stream_File(p->stream_str[play1_id])) {
			use_chain_successful = 1;
		}
	}

	/* Look up the next scene's index in cur_scene[]. Loop is bounded by
	 * cur_scene's 0 sentinel; retail bounded by next_scene[i] which has
	 * no sentinel and only terminated by accident of adjacent-global
	 * memory layout (ASan redzones break that coincidence). */
	int16_t next_id = 0;
	int16_t target = p->next_scene[play1_id];
	for (int16_t i = 0; p->cur_scene[i]; i++) {
		if (p->cur_scene[i] == target) {
			next_id = i;
			break;
		}
	}
	if (p->stream_str[next_id][0])
		lstream_Chain_Stream_File(p->stream_str[next_id]);
}

/* ------------------------------------------------------------------ */

/*
 * Main cutscene entry. Looks up the scene in play1_cur_scene[], selects
 * resources, film name, frame rate, creates the film, runs the view,
 * and cleans up on exit.
 *
 * The scene switch below was verified against the binary's nested
 * if-else tree. Actions:
 *   (default)  = copy film name, no frame rate change
 *   24fps      = set 24fps + copy film name
 *   20fps      = set 20fps + copy film name
 *   +bridge    = 24fps + film + file2 = "bridge.lfd" (resource[6])
 *   +emperor   = 24fps + film + file2 = "emperor.lfd" (resource[4])
 *   +awards    = copy film + file2 = "awards.lfd" (resource[27])
 */
typedef enum {
	PLAY1_PHASE_BEGIN = 0,
	PLAY1_PHASE_CLEANUP = 1,
} Play1Phase;

typedef struct Play1Task {
	SceneHeadStruct* the_head;
	ResFile* file;
	ResFile* file2;
	int16_t scene;
	bool rate_changed;
	bool is_streaming_active; /* mirrors module-static is_streaming for cleanup */
	LandruSurfaceSet surface_set;
	Play1Phase phase;
} Play1Task;

/* PORT: asynchronous adaptation of TIE95 PLAY1_Play1 and
 * TIE98 PLAY1_Play1 (0x4682D0). */
static LandruTaskStepResult play1_task_step(void* self) {
	Play1Task* t = (Play1Task*)self;

	if (t->phase == PLAY1_PHASE_BEGIN) {
		char name[16];
		Rect r;
		const play1_data_set_t* p = play1_data_set();

		for (int i = 0; i < 205; i++)
			wrap_table[i] = 320 * i;
		wrap = 312;

		int16_t scene = shellext_Get_Cur_Scene();
		if (scene == SCENE_CUT_900)
			return LANDRU_TASK_STEP_DONE;
		t->scene = scene;

		/* TIE98 leaves the TOTRAIN and TOCOMBAT transitions on the
		 * native SVGA target; every other PLAY1 film uses VGA. */
		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 &&
			(scene == SCENE_TRAIN_TRANSITION || scene == SCENE_COMBAT_TRANSITION)) {
			t->surface_set = LANDRU_SURFACE_SVGA;
			(void)lsurface_Select_Surface_Set(t->surface_set);
			lview_Init_View(lview_Get_Current_View());
		}

		for (play1_id = 0; scene != p->cur_scene[play1_id] && p->cur_scene[play1_id]; play1_id++)
			;
		if (!p->cur_scene[play1_id])
			return LANDRU_TASK_STEP_DONE;

		t->file = shellext_Open_Empire_Resource(p->resource_str[play1_id]);
		t->file2 = NULL;
		ResFile* file = t->file; /* shadow for the existing switch body */
		ResFile* file2 = t->file2;
		lcanvas_Get_Drawing_Canvas_Bounds(&r);

		int16_t cur = p->cur_scene[play1_id];
		bool rate_changed = false;

		switch (cur) {
			/* 24fps, standard film */
			case 30:
			case 31:
			case 32:
			case 50:
			case 70:
			case 500:
			case 510:
			case 530:
			case 531:
			case 550:
			case 560:
			case 570:
			case 571:
			case 572:
			case 573:
			case 700:
			case 710:
			case 720:
				ltimer_Set_Frame_Rate(24);
				strcpy(name, p->film_str[play1_id]);
				rate_changed = true;
				break;

			/* 20fps, standard film */
			case 10:
			case 60:
			case 61:
			case 71:
			case 72:
			case 600:
			case 601:
			case 602:
			case 603:
			case 610:
			case 620:
			case 621:
			case 622:
				ltimer_Set_Frame_Rate(20);
				strcpy(name, p->film_str[play1_id]);
				rate_changed = true;
				break;

			/* 24fps + file2 = "bridge.lfd" (resource[6]) */
			case 520:
			case 581:
				ltimer_Set_Frame_Rate(24);
				strcpy(name, p->film_str[play1_id]);
				file2 = shellext_Open_Empire_Resource(p->resource_str[6]);
				rate_changed = true;
				break;

			/* 24fps + file2 = "emperor.lfd" (resource[4]) */
			case 590:
			case 591:
			case 730:
				ltimer_Set_Frame_Rate(24);
				strcpy(name, p->film_str[play1_id]);
				file2 = shellext_Open_Empire_Resource(p->resource_str[4]);
				rate_changed = true;
				break;

			/* 20fps + file2 = "scene10.lfd" (resource[80]) — retail-only */
			case 623:
				ltimer_Set_Frame_Rate(20);
				strcpy(name, p->film_str[play1_id]);
				file2 = shellext_Open_Empire_Resource(p->resource_str[80]);
				rate_changed = true;
				break;

			/* 20fps + file2 = "emperor.lfd" (resource[4]) — retail-only */
			case 740:
				ltimer_Set_Frame_Rate(20);
				strcpy(name, p->film_str[play1_id]);
				file2 = shellext_Open_Empire_Resource(p->resource_str[4]);
				rate_changed = true;
				break;

			/* 24fps + file2 from peer resource */
			case 580:
				ltimer_Set_Frame_Rate(24);
				strcpy(name, p->film_str[play1_id]);
				file2 = shellext_Open_Empire_Resource(p->resource_str[play1_id - 11]);
				rate_changed = true;
				break;

			/* Award scenes 258-263: film + file2 = "awards.lfd" */
			case 258:
			case 259:
			case 260:
			case 261:
			case 262:
			case 263:
				strcpy(name, p->film_str[play1_id]);
				file2 = shellext_Open_Empire_Resource(p->resource_str[27]);
				break;

			/* Scene 270: launch ship resource */
			case 270:
				file2 = file;
				shipext_Get_Launch_Name(name);
				file = (ResFile*)(intptr_t)shipext_Open_Launch_Resource();
				break;

			/* Scene 390: secret medal film */
			case 390: {
				int16_t medal = shipext_Get_Secret_Medal();
				strcpy(name, secret_film_str[medal - 1]);
				break;
			}

			/* Scene 420: mission disk resource swapping. The mission-disk
			 * arming cutscene uses film "secarm2f" (only present in
			 * secarm{1,2}.lfd), not the base "secarm_f" from secarm.lfd. */
			case 420:
				strcpy(name, p->film_str[play1_id]);
				if (shipext_Is_Mission_Disk1() || shipext_Is_Mission_Disk2()) {
					strcpy(name, "secarm2f");
					lres_Close_Resource(file);
					if (shipext_Is_Mission_Disk2())
						file = shellext_Open_Empire_Resource(mission_disk2_resource);
					else if (shipext_Is_Mission_Disk1())
						file = shellext_Open_Empire_Resource(mission_disk1_resource);
					file2 = shellext_Open_Empire_Resource(p->resource_str[play1_id]);
				}
				break;

			/* Default: copy film name, no frame rate change */
			default:
				strcpy(name, p->film_str[play1_id]);
				break;
		}

		/* Sync file/file2 back to the task struct (the switch above may
		 * have rebound them). */
		t->file = file;
		t->file2 = file2;
		t->rate_changed = rate_changed;

		/* Tag the snapshot with the (LFD basename, film name) tuple so a
		 * cutscene compositor on the host side can locate its remaster
		 * asset bundle. The basename is the resource_str entry minus the
		 * ".lfd" extension; the setter uppercases to match retail asset
		 * directory conventions. Cleared in PLAY1_PHASE_CLEANUP. */
		{
			char lfd_base[16];
			const char* res = p->resource_str[play1_id];
			size_t i = 0;
			for (; i + 1 < sizeof lfd_base && res[i] && res[i] != '.'; ++i)
				lfd_base[i] = res[i];
			lfd_base[i] = '\0';
			TieSnapshotBuilder_SetActiveFilm(lfd_base, name);
		}

		play1_Chain_Scene();
		TieDiagnostics_Log(TIE_LOG_INFO, "[PLAY1] scene=%d film='%s' resource='%s' stream='%s'\n", play1_id,
						   name, p->resource_str[play1_id],
						   p->stream_str[play1_id][0] ? p->stream_str[play1_id] : "(none)");
		play1_film = lfilm_Res_Callback_Film(name, &r, 0, 0, 0, play1_film_Callback);
		if (!play1_film) {
			if (t->file2)
				lres_Close_Resource(t->file2);
			lres_Close_Resource(t->file);
			lerror_Set_Landru_Exit(p->next_scene[play1_id]);
			return LANDRU_TASK_STEP_DONE;
		}
		if (TieMusicPolicy_UsesTie98()) {
			const char* music_path = play1_tie98_music_path(scene);
			if (music_path)
				FrontendWaveStream_PlayWaveFile(music_path, 0);
		}

		lfilm_Set_Film_Def_Palette(play1_film, t->the_head->def_palette);
		lview_Set_View_Update_Function(play1_end_View);

		if (lcursor_Is_Cursor_Visible())
			lcursor_Hide_Cursor();

		/* Start palette cycling for scenes 20 and 25 */
		if (scene == 20 || scene == 25) {
			for (Palette* pal = lpal_Ask_Palette_List(); pal; pal = pal->next) {
				if (pal->cycle_active)
					lpal_Start_Cycle(pal);
			}
		}

		/* Snapshot the streaming flag for the cleanup phase — is_streaming
		 * is module-static and may flip during the modal view; remember
		 * what it was set to here so cleanup tears down what setup built. */
		t->is_streaming_active = is_streaming;

		/* Push the modal view task */
		lviewadd_Push_Handle_View_Task();

		t->phase = PLAY1_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	if (TieMusicPolicy_UsesTie98() && play1_tie98_music_ends_after_scene(t->scene))
		FrontendWaveStream_Shutdown();
	/* Restore frame rate to 20fps for scenes that changed it */
	if (t->rate_changed)
		ltimer_Set_Frame_Rate(20);

	/* Tear down streaming */
	if (t->is_streaming_active && is_streaming) {
		lstream_Unchain_Current_Stream_File();
		is_streaming = 0;
		if (read_buffer) {
			free(read_buffer);
			read_buffer = NULL;
		}
		if (last_frame.data)
			lbitmap_Free_Bitmap(&last_frame);
		if (current_frame.data)
			lbitmap_Free_Bitmap(&current_frame);
	}

	lview_Clear_View_Update_Function();
	if (t->file2)
		lres_Close_Resource(t->file2);
	if (t->file)
		lres_Close_Resource(t->file);

	/* Drop the active-film tag — the compositor will fall back to
	 * classic rendering for whatever scene runs next until another
	 * PLAY1 push re-tags. */
	TieSnapshotBuilder_SetActiveFilm(NULL, NULL);
	if (t->surface_set == LANDRU_SURFACE_SVGA)
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable play1_task_vt = {
	.step = play1_task_step,
};

void play1_Push_Play1_Task(SceneHeadStruct* the_head) {
	/* Tag the snapshot before pushing so the cutscene compositor in
	 * the host can recognise this scene as a film-only cinematic.
	 * Re-tagged on the next scene push when PLAY1 pops. */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_CUTSCENE);
	/* Cutscenes redraw every actor every tick — RT clears per frame
	 * to avoid trails from moving sprites. UI scenes (default) keep
	 * INCREMENTAL where the RT persists across frames. */
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);

	Play1Task* t = (Play1Task*)landru_task_push(&play1_task_vt);
	if (!t)
		return;
	t->the_head = the_head;
	t->file = NULL;
	t->file2 = NULL;
	t->rate_changed = false;
	t->is_streaming_active = false;
	t->surface_set = LANDRU_SURFACE_VGA;
	t->phase = PLAY1_PHASE_BEGIN;
}
