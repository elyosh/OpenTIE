#include "film.h"

#include "fourcc.h"
#include "imgbake/byteio.h"

#include <string.h>

bool TieFilm_Parse(TieFilmHeader* out, const uint8_t* data, uint32_t size) {
	if (size < FILM_HEADER_SIZE)
		return false;
	out->version = rd_u16(data);
	out->cels = rd_u16(data + 2);
	out->array_size = rd_u16(data + 4);
	out->data = data;
	out->size = size;
	return true;
}

bool TieFilm_EntryNext(const TieFilmHeader* h, uint32_t* iter, TieFilmEntry* out) {
	uint32_t off = (*iter == 0) ? FILM_HEADER_SIZE : *iter;
	if (off + FILM_ENTRY_HDR_SIZE > h->size)
		return false;

	out->res_type = TieFilmFourcc_Be(h->data + off);
	memcpy(out->res_name, h->data + off + 4, 8);
	out->res_name[8] = '\0';
	out->block_size = rd_u32(h->data + off + 12);
	out->type_code = rd_u16(h->data + off + 16);
	out->num_records = rd_u16(h->data + off + 18);
	out->data_size = rd_u16(h->data + off + 20);
	out->records = h->data + off + FILM_ENTRY_HDR_SIZE;

	uint32_t avail = h->size - off - FILM_ENTRY_HDR_SIZE;
	out->records_size = ((uint32_t)out->data_size <= avail) ? out->data_size : (uint16_t)avail;

	/* Advance by the declared data_size; if the entry lies the next call
	   will fail the bounds check and return false. */
	*iter = off + FILM_ENTRY_HDR_SIZE + out->data_size;
	return true;
}

bool TieFilm_RecordNext(const TieFilmEntry* e, uint16_t* iter, TieFilmRecord* out, bool* zero_size) {
	if (zero_size)
		*zero_size = false;
	uint16_t off = *iter;
	if ((uint32_t)off + FILM_REC_HDR_SIZE > e->records_size)
		return false;

	out->size = rd_u16(e->records + off);
	out->cmd = rd_u16(e->records + off + 2);
	out->offset = off;

	if (out->size == 0) {
		if (zero_size)
			*zero_size = true;
		return false;
	}

	out->payload_size = (out->size > FILM_REC_HDR_SIZE) ? (uint16_t)(out->size - FILM_REC_HDR_SIZE) : 0;
	out->payload = e->records + off + FILM_REC_HDR_SIZE;

	/* Advance past this record. If it claims to extend past the stream,
	   the next call's bounds check will return false. */
	*iter = (uint16_t)(off + out->size);
	return true;
}

const char* TieFilm_CmdName(uint16_t cmd) {
	switch (cmd) {
		case FILM_CMD_NONE:
			return "NONE";
		case FILM_CMD_DELETE:
			return "DELETE";
		case FILM_CMD_END:
			return "END";
		case FILM_CMD_TIMESTAMP:
			return "TIMESTAMP";
		case FILM_CMD_ACTOR_POS:
			return "ACTOR_POS";
		case FILM_CMD_ACTOR_VEL:
			return "ACTOR_VEL";
		case FILM_CMD_ACTOR_Z:
			return "ACTOR_Z";
		case FILM_CMD_ACTOR_STATE:
			return "ACTOR_STATE";
		case FILM_CMD_ACTOR_STATEV:
			return "ACTOR_STATEV";
		case FILM_CMD_ACTOR_VAR1:
			return "ACTOR_VAR1";
		case FILM_CMD_ACTOR_VAR2:
			return "ACTOR_VAR2";
		case FILM_CMD_ACTOR_CLIP:
			return "ACTOR_CLIP";
		case FILM_CMD_ACTOR_CLIPV:
			return "ACTOR_CLIPV";
		case FILM_CMD_ACTOR_SHOW:
			return "ACTOR_SHOW";
		case FILM_CMD_ACTOR_FLIP:
			return "ACTOR_FLIP";
		case FILM_CMD_PALETTE_SET:
			return "PALETTE_SET";
		case FILM_CMD_VIEW_SETRGB:
			return "VIEW_SETRGB";
		case FILM_CMD_VIEW_DEFPAL:
			return "VIEW_DEFPAL";
		case FILM_CMD_VIEW_FADE:
			return "VIEW_FADE";
		case FILM_CMD_SOUND_START:
			return "SOUND_START";
		case FILM_CMD_SOUND_STOP:
			return "SOUND_STOP";
		case FILM_CMD_SOUND_VOLUME:
			return "SOUND_VOLUME";
		case FILM_CMD_SOUND_FADE:
			return "SOUND_FADE";
		case FILM_CMD_SOUND_VAR1:
			return "SOUND_VAR1";
		case FILM_CMD_SOUND_VAR2:
			return "SOUND_VAR2";
		case FILM_CMD_SOUND_CMD:
			return "SOUND_CMD";
		case FILM_CMD_SOUND_PAN:
			return "SOUND_PAN";
		case FILM_CMD_SOUND_PAN_FADE:
			return "SOUND_PAN_FADE";
		case FILM_CMD_SOUND_CMD2:
			return "SOUND_CMD2";
		default:
			return NULL;
	}
}

const char* TieFilm_TypeCodeName(uint16_t tc) {
	switch (tc) {
		case FILM_TC_VIEW:
			return "VIEW";
		case FILM_TC_ACTOR:
			return "ACTOR";
		case FILM_TC_PALETTE:
			return "PALETTE";
		case FILM_TC_SOUND:
			return "SOUND";
		default:
			return "UNKNOWN";
	}
}
