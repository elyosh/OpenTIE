#include "tie_runtime/runtime/play1_data_set.h"
#include "tie/play1.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/storage/storage.h"

#include <string.h>

/* LecDemos sample-disc PLAY1 scene tables. They differ from the Collector's
 * CD retail tables in late-game scene IDs, secret medal films, a few
 * resource files and the FMV directory name (STREAM instead of ASTREAM).
 * Stream names keep the retail leading-backslash form. */

static const int16_t demo_cur_scene[87] = {
	6,   7,   10,  20,  30,  40,  50,  60,  70,  120, 130, 210, 231, 240, 400, 401, 402, 403,
	404, 405, 406, 407, 408, 409, 410, 411, 420, 250, 251, 252, 253, 254, 255, 256, 257, 258,
	259, 260, 261, 262, 263, 270, 170, 280, 281, 282, 283, 284, 285, 390, 500, 510, 520, 530,
	531, 540, 550, 560, 570, 571, 572, 573, 580, 581, 590, 591, 600, 601, 602, 603, 610, 611,
	612, 620, 621, 622, 25,  700, 710, 720, 730, 61,  71,  72,  31,  32,  0,
};

static const int16_t demo_next_scene[86] = {
	7,   8,   20,  30,  40,  50,  61,  61,  71,  121, 131, 910, 910, 910, 420, 420, 420, 420,
	420, 420, 420, 420, 420, 420, 420, 420, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 231, 910, 910, 910, 910, 910, 910, 910, 910, 910, 531,
	910, 910, 910, 910, 571, 572, 573, 910, 581, 910, 591, 910, 601, 602, 603, 910, 611, 612,
	910, 621, 622, 910, 910, 910, 910, 910, 910, 70,  72,  80,  32,  40,
};

static const int16_t demo_skip_scene[86] = {
	100, 100, 100, 100, 100, 100, 100, 100, 100, 121, 131, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 4,   180, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910, 910,
	910, 910, 910, 910, 910, 910, 910, 910, 910, 100, 100, 100, 100, 100,
};

static const char demo_resource_str[86][14] = {
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

static const char demo_film_str[86][10] = {
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

static const char demo_stream_str[86][24] = {
	"",
	"",
	"\\stream\\os1-v3.wrk",
	"",
	"",
	"\\stream\\swarm.wrk",
	"\\stream\\scene9e.wrk",
	"",
	"\\stream\\scene13a.wrk",
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
	"\\stream\\shot1.wrk",
	"\\stream\\shot2.wrk",
	"\\stream\\shot3.wrk",
	"\\stream\\shot4.wrk",
	"\\stream\\s1_v3.wrk",
	"\\stream\\s2-v2.wrk",
	"\\stream\\s3-v10.wrk",
	"\\stream\\s1_v3.wrk",
	"\\stream\\s2-v2.wrk",
	"\\stream\\s3-v10.wrk",
	"",
	"",
	"",
	"",
	"",
	"\\stream\\scene12a.wrk",
	"",
	"\\stream\\scene15.wrk",
	"\\stream\\emp1b.wrk",
	"\\stream\\emp1c.wrk",
};

static bool s_selected;
static bool s_uses_demo_data;

void TiePlay1_SelectDataSet(void) {
	if (s_selected)
		return;
	s_selected = true;
	/* The retail Collector's CD stores FMV under ASTREAM/; the LecDemos
	 * sample disc uses STREAM/. Probe both case spellings since DOS
	 * filesystems are case-insensitive but Unix is not. */
	s_uses_demo_data = !TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "astream") &&
					   !TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "ASTREAM");
	if (s_uses_demo_data) {
		memcpy(play1_cur_scene, demo_cur_scene, sizeof play1_cur_scene);
		memcpy(play1_next_scene, demo_next_scene, sizeof play1_next_scene);
		memcpy(play1_skip_scene, demo_skip_scene, sizeof play1_skip_scene);
		memcpy(play1_resource_str, demo_resource_str, sizeof play1_resource_str);
		memcpy(play1_film_str, demo_film_str, sizeof play1_film_str);
		memcpy(play1_stream_str, demo_stream_str, sizeof play1_stream_str);
	}
	TieDiagnostics_Log(TIE_LOG_INFO, "[PLAY1] data set = %s\n", s_uses_demo_data ? "demo" : "retail");
}

bool TiePlay1_UsesDemoData(void) { return s_uses_demo_data; }
