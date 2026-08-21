/*
 * play1_streams — lookup of FILM name → .WRK relative path.
 *
 * Mirrors the parallel arrays in src/tie/play1.c
 * (play1_film_str_retail/_demo + play1_stream_str_retail/_demo): given
 * a film name, returns the .WRK relative path string used by the
 * scene that drives that film. Two data sets exist because the demo
 * (LecDemos sample disc) ships under STREAM/ with a slightly
 * different scene roster, while the Collector's CD retail build uses
 * ASTREAM/. Pick by ENGINE_DATA_RETAIL when the WRK directory you're
 * pointing at is named ASTREAM, ENGINE_DATA_DEMO when it's STREAM.
 *
 * Scenes that exist on both data sets but with no stream (empty
 * "" entry in play1.c) return NULL — caller treats as "no auto
 * binding for this film".
 */
#ifndef FILM_PLAY1_STREAMS_H
#define FILM_PLAY1_STREAMS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	PLAY1_DATA_RETAIL = 0, /* Collector's CD: ASTREAM/ */
	PLAY1_DATA_DEMO = 1,   /* LecDemos sample disc: STREAM/ */
} TieFilmPlay1DataSet;

/* Look up `film_name` (case-sensitive, the FILM resource name from the
 * LFD, e.g. "stard_f"). Returns the relative path (e.g.
 * "astream\\os1-v3.wrk" on retail, "stream\\os1-v3.wrk" on demo) or
 * NULL if there's no auto-binding. The string lives in static
 * storage; do not free.
 *
 * The path uses backslash separators as in the original game data;
 * callers normalise to / for portable fopen on POSIX. */
const char* TieFilmPlay1Streams_Play1StreamForFilm(const char* film_name, TieFilmPlay1DataSet ds);

#ifdef __cplusplus
}
#endif

#endif
