/* An XFILM chunk holds:
 *   u16 version
 *   u16 cels                   total frame count
 *   u16 array_size             number of FilmObject entries
 *   FilmObject[array_size]
 *
 * Each FilmObject is a 22-byte header followed by `data_size` bytes of
 * record stream. Records are { u16 size, u16 cmd, payload[size-4] }. */
#ifndef FILM_FILM_H
#define FILM_FILM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define FILM_HEADER_SIZE 6
#define FILM_ENTRY_HDR_SIZE 22
#define FILM_REC_HDR_SIZE 4

/* FilmObject command IDs (XFILM record opcodes). */
typedef enum {
	FILM_CMD_NONE = 0,
	FILM_CMD_DELETE = 1,
	FILM_CMD_END = 2,
	FILM_CMD_TIMESTAMP = 3,
	FILM_CMD_ACTOR_POS = 4,
	FILM_CMD_ACTOR_VEL = 5,
	FILM_CMD_ACTOR_Z = 6,
	FILM_CMD_ACTOR_STATE = 7,
	FILM_CMD_ACTOR_STATEV = 8,
	FILM_CMD_ACTOR_VAR1 = 9,
	FILM_CMD_ACTOR_VAR2 = 10,
	FILM_CMD_ACTOR_CLIP = 11,
	FILM_CMD_ACTOR_CLIPV = 12,
	FILM_CMD_ACTOR_SHOW = 13,
	FILM_CMD_ACTOR_FLIP = 14,
	FILM_CMD_PALETTE_SET = 15,
	FILM_CMD_VIEW_SETRGB = 16,
	FILM_CMD_VIEW_DEFPAL = 17,
	FILM_CMD_VIEW_FADE = 18,
	FILM_CMD_SOUND_START = 19,
	FILM_CMD_SOUND_STOP = 20,
	FILM_CMD_SOUND_VOLUME = 21,
	FILM_CMD_SOUND_FADE = 22,
	FILM_CMD_SOUND_VAR1 = 23,
	FILM_CMD_SOUND_VAR2 = 24,
	FILM_CMD_SOUND_CMD = 25,
	FILM_CMD_SOUND_PAN = 26,
	FILM_CMD_SOUND_PAN_FADE = 27,
	FILM_CMD_SOUND_CMD2 = 28,
} TieFilmCommand;

/* type_code values for FilmObject entries. */
typedef enum {
	FILM_TC_VIEW = 2,
	FILM_TC_ACTOR = 3,
	FILM_TC_PALETTE = 4,
	FILM_TC_SOUND = 5,
} TieFilmTypeCode;

typedef struct {
	uint16_t version;
	uint16_t cels;
	uint16_t array_size;
	const uint8_t* data; /* full chunk payload */
	uint32_t size;
} TieFilmHeader;

typedef struct {
	uint32_t res_type;   /* FOURCC */
	char res_name[9];    /* 8 chars + NUL */
	uint32_t block_size; /* as stored in the entry — may disagree with hdr+data */
	uint16_t type_code;
	uint16_t num_records;
	uint16_t data_size; /* declared record-stream size */
	const uint8_t* records;
	uint16_t records_size; /* clamped to remaining payload if entry overruns */
} TieFilmEntry;

typedef struct {
	uint16_t size; /* full record size including the 4-byte header */
	uint16_t cmd;
	const uint8_t* payload;
	uint16_t payload_size;
	uint16_t offset; /* offset within entry->records */
} TieFilmRecord;

bool TieFilm_Parse(TieFilmHeader* out, const uint8_t* data, uint32_t size);

/* Iterate the FilmObject array. Initialize *iter to 0 before the loop;
   it is updated to the next entry's offset. Returns false at end of
   array or on a truncated header — distinguish via *iter == h->size. */
bool TieFilm_EntryNext(const TieFilmHeader* h, uint32_t* iter, TieFilmEntry* out);

/* Iterate one entry's record stream. Initialize *iter to 0. Returns
   false at end-of-stream or on a zero-size record (terminator); when
   that's the cause, *zero_size (if non-NULL) is set to true. */
bool TieFilm_RecordNext(const TieFilmEntry* e, uint16_t* iter, TieFilmRecord* out, bool* zero_size);

const char* TieFilm_CmdName(uint16_t cmd);
const char* TieFilm_TypeCodeName(uint16_t tc);

#ifdef __cplusplus
}
#endif

#endif
