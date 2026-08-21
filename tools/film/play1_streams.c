/*
 * Generated-by-hand mirror of the play1.c film/stream arrays. Both
 * data sets are 86 entries long; the indices match position-for-
 * position with the retail / demo cur_scene arrays. We don't expose
 * the scene IDs here — only the (film_name, stream_path) pairs
 * needed for filmview's auto-bind path.
 */
#include "play1_streams.h"

#include <string.h>

typedef struct {
	const char* film;
	const char* stream;
} TieFilmPlay1StreamsRow;

/* Retail (Collector's CD): ASTREAM/. Pulled verbatim from play1.c
 * play1_film_str_retail / play1_stream_str_retail (86 rows each). */
static const TieFilmPlay1StreamsRow play1_streams_retail[] = {
	{ "logo_f", "" },
	{ "perelogo", "" },
	{ "stard_f", "astream\\os1-v3.wrk" },
	{ "city1_f", "" },
	{ "emp1_f", "" },
	{ "swarma_f", "astream\\swarm.wrk" },
	{ "brdg1b_f", "astream\\scene9e.wrk" },
	{ "plat_f", "" },
	{ "chasea1f", "astream\\scene13a.wrk" },
	{ "totrn_f", "" },
	{ "tocmbt_f", "" },
	{ "cap_f", "" },
	{ "medic_f", "" },
	{ "fun_f", "" },
	{ "sec1_f", "" },
	{ "sec2_f", "" },
	{ "sec2_f", "" },
	{ "sec2_f", "" },
	{ "sec3_f", "" },
	{ "sec4_f", "" },
	{ "sec5_f", "" },
	{ "sec6_f", "" },
	{ "sec7_f", "" },
	{ "sec8_f", "" },
	{ "sec9_f", "" },
	{ "sec10_f", "" },
	{ "secarm_f", "" },
	{ "awards", "" },
	{ "award1", "" },
	{ "award2", "" },
	{ "award3", "" },
	{ "award4", "" },
	{ "award5", "" },
	{ "award6", "" },
	{ "award7", "" },
	{ "award8", "" },
	{ "award9", "" },
	{ "award10", "" },
	{ "award11", "" },
	{ "award12", "" },
	{ "award13", "" },
	{ "lnch_f", "" },
	{ "newtour", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "secret", "" },
	{ "scene1_f", "" },
	{ "scene2_f", "" },
	{ "scene3_f", "" },
	{ "scene4a", "" },
	{ "scene4b", "" },
	{ "scene5_f", "" },
	{ "scene6_f", "" },
	{ "scene7_f", "" },
	{ "battle8a", "" },
	{ "battle8b", "" },
	{ "battle8c", "" },
	{ "battle8d", "" },
	{ "scene9_f", "" },
	{ "scene9b", "" },
	{ "scene10a", "" },
	{ "scene10b", "" },
	{ "shot1", "" },
	{ "shot2", "astream\\shot2.wrk" },
	{ "shot3", "astream\\shot3.wrk" },
	{ "shot4", "" },
	{ "scene12", "" },
	{ "s1_v3", "astream\\s1_v3.wrk" },
	{ "s2-v2", "astream\\s2-v2.wrk" },
	{ "s3-v10", "astream\\s3-v10.wrk" },
	{ "scene13d", "" },
	{ "sec_f", "" },
	{ "seca_f", "" },
	{ "secb_f", "" },
	{ "secc_f", "" },
	{ "secd_f", "" },
	{ "sece_f", "" },
	{ "platb2_f", "astream\\scene12a.wrk" },
	{ "chaseb_f", "" },
	{ "chasec_f", "astream\\scene15.wrk" },
	{ "emp1b_f", "astream\\emp1b.wrk" },
	{ "emp1c_f", "astream\\emp1c.wrk" },
};

/* Demo (LecDemos sample disc): STREAM/. From play1_film_str_demo /
 * play1_stream_str_demo. Differs from retail at scenes 600-740 and a
 * handful of secret-arm rows. */
static const TieFilmPlay1StreamsRow play1_streams_demo[] = {
	{ "logo_f", "" },
	{ "perelogo", "" },
	{ "stard_f", "stream\\os1-v3.wrk" },
	{ "city1_f", "" },
	{ "emp1_f", "" },
	{ "swarma_f", "stream\\swarm.wrk" },
	{ "brdg1b_f", "stream\\scene9e.wrk" },
	{ "plat_f", "" },
	{ "chasea1f", "stream\\scene13a.wrk" },
	{ "totrn_f", "" },
	{ "tocmbt_f", "" },
	{ "cap_f", "" },
	{ "medic_f", "" },
	{ "fun_f", "" },
	{ "sec1_f", "" },
	{ "sec2_f", "" },
	{ "sec2_f", "" },
	{ "sec2_f", "" },
	{ "sec3_f", "" },
	{ "sec4_f", "" },
	{ "sec5_f", "" },
	{ "sec6_f", "" },
	{ "sec7_f", "" },
	{ "sec5_f", "" },
	{ "sec5_f", "" },
	{ "sec5_f", "" },
	{ "secarm_f", "" },
	{ "awards", "" },
	{ "award1", "" },
	{ "award2", "" },
	{ "award3", "" },
	{ "award4", "" },
	{ "award5", "" },
	{ "award6", "" },
	{ "award7", "" },
	{ "award8", "" },
	{ "award9", "" },
	{ "award10", "" },
	{ "award11", "" },
	{ "award12", "" },
	{ "award13", "" },
	{ "lnch_f", "" },
	{ "newtour", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "landsd", "" },
	{ "secret", "" },
	{ "scene1_f", "" },
	{ "scene2_f", "" },
	{ "scene3_f", "" },
	{ "scene4a", "" },
	{ "scene4b", "" },
	{ "scene5_f", "" },
	{ "scene6_f", "" },
	{ "scene7_f", "" },
	{ "battle8a", "" },
	{ "battle8b", "" },
	{ "battle8c", "" },
	{ "battle8d", "" },
	{ "scene9_f", "" },
	{ "scene9b", "" },
	{ "scene10a", "" },
	{ "scene10b", "" },
	{ "shot1", "stream\\shot1.wrk" },
	{ "shot2", "stream\\shot2.wrk" },
	{ "shot3", "stream\\shot3.wrk" },
	{ "shot4", "stream\\shot4.wrk" },
	{ "s1_v3", "stream\\s1_v3.wrk" },
	{ "s2-v2", "stream\\s2-v2.wrk" },
	{ "s3-v10", "stream\\s3-v10.wrk" },
	{ "s1_v3", "stream\\s1_v3.wrk" },
	{ "s2-v2", "stream\\s2-v2.wrk" },
	{ "s3-v10", "stream\\s3-v10.wrk" },
	{ "sec_f", "" },
	{ "seca_f", "" },
	{ "secb_f", "" },
	{ "secc_f", "" },
	{ "secd_f", "" },
	{ "platb2_f", "stream\\scene12a.wrk" },
	{ "chaseb_f", "" },
	{ "chasec_f", "stream\\scene15.wrk" },
	{ "emp1b_f", "stream\\emp1b.wrk" },
	{ "emp1c_f", "stream\\emp1c.wrk" },
};

const char* TieFilmPlay1Streams_Play1StreamForFilm(const char* film_name, TieFilmPlay1DataSet ds) {
	if (!film_name || !*film_name)
		return NULL;

	const TieFilmPlay1StreamsRow* rows;
	size_t n;
	if (ds == PLAY1_DATA_DEMO) {
		rows = play1_streams_demo;
		n = sizeof play1_streams_demo / sizeof play1_streams_demo[0];
	} else {
		rows = play1_streams_retail;
		n = sizeof play1_streams_retail / sizeof play1_streams_retail[0];
	}

	for (size_t i = 0; i < n; i++) {
		if (strcmp(rows[i].film, film_name) != 0)
			continue;
		if (rows[i].stream[0] == '\0')
			continue;
		return rows[i].stream;
	}
	return NULL;
}
