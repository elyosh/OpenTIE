/*
 * filmdump — Dump the contents of FILM chunks from LFD resource files.
 *
 * Usage: filmdump <file.lfd> [film_name]
 *   If film_name is given, only that film is dumped.
 *   Otherwise all FILM chunks in the LFD are dumped.
 */

#include "film.h"
#include "fourcc.h"
#include "imgbake/byteio.h"
#include "lfd_file.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

/* Print the human-readable payload description for a record. */
static void TieFilmDump_PrintPayload(uint16_t cmd, const uint8_t* payload, uint16_t sz) {
	switch (cmd) {
		case FILM_CMD_TIMESTAMP:
			if (sz >= 2)
				printf("cel=%d", rd_u16(payload));
			break;

		case FILM_CMD_ACTOR_POS:
		case FILM_CMD_ACTOR_VEL:
			if (sz >= 8)
				printf("x=%d y=%d x_frac=%d y_frac=%d", rd_i16(payload), rd_i16(payload + 2),
					   rd_i16(payload + 4), rd_i16(payload + 6));
			break;

		case FILM_CMD_ACTOR_Z:
			if (sz >= 2)
				printf("zplane=%d", rd_i16(payload));
			break;

		case FILM_CMD_ACTOR_STATE:
		case FILM_CMD_ACTOR_STATEV:
			if (sz >= 4)
				printf("value=%d frac=%d", rd_i16(payload), rd_i16(payload + 2));
			break;

		case FILM_CMD_ACTOR_VAR1:
		case FILM_CMD_ACTOR_VAR2:
			if (sz >= 2)
				printf("value=%d", rd_i16(payload));
			break;

		case FILM_CMD_ACTOR_CLIP:
		case FILM_CMD_ACTOR_CLIPV:
			if (sz >= 8)
				printf("left=%d top=%d right=%d bottom=%d", rd_i16(payload), rd_i16(payload + 2),
					   rd_i16(payload + 4), rd_i16(payload + 6));
			break;

		case FILM_CMD_ACTOR_SHOW:
			if (sz >= 2)
				printf("%s", rd_i16(payload) ? "show" : "hide");
			break;

		case FILM_CMD_ACTOR_FLIP:
			if (sz >= 4)
				printf("hflip=%d vflip=%d", rd_i16(payload), rd_i16(payload + 2));
			break;

		case FILM_CMD_PALETTE_SET:
			printf("(apply palette)");
			break;

		case FILM_CMD_VIEW_SETRGB:
			if (sz >= 5)
				printf("range=%d..%d rgb=(%d, %d, %d)", payload[0], payload[1], payload[2], payload[3],
					   payload[4]);
			break;

		case FILM_CMD_VIEW_DEFPAL:
			printf("(reset to def_palette)");
			break;

		case FILM_CMD_VIEW_FADE:
			if (sz >= 4)
				printf("fade_type=%d color_fade=%d", rd_u16(payload) & 0xff, rd_u16(payload + 2) & 0xff);
			break;

		case FILM_CMD_SOUND_START:
			printf("(start)");
			break;
		case FILM_CMD_SOUND_STOP:
			printf("(stop)");
			break;

		case FILM_CMD_SOUND_VOLUME:
			if (sz >= 2)
				printf("volume=%d", rd_i16(payload));
			break;

		case FILM_CMD_SOUND_FADE:
			if (sz >= 4)
				printf("volume=%d frames=%d", rd_i16(payload), rd_i16(payload + 2));
			break;

		case FILM_CMD_SOUND_VAR1:
			if (sz >= 2) {
				int16_t v = rd_i16(payload);
				printf("type=%d (%s)", v, v == 1 ? "speech" : "sfx");
			}
			break;

		case FILM_CMD_SOUND_VAR2:
			if (sz >= 2)
				printf("value=%d", rd_i16(payload));
			break;

		case FILM_CMD_SOUND_CMD:
			if (sz >= 8)
				printf("start=%d volume=%d fade_vol=%d fade_frames=%d", rd_i16(payload), rd_i16(payload + 2),
					   rd_i16(payload + 4), rd_i16(payload + 6));
			break;

		case FILM_CMD_SOUND_CMD2:
			if (sz >= 14)
				printf("start=%d volume=%d fade_vol=%d fade_frames=%d "
					   "pan=%d pan_fade_vol=%d pan_fade_frames=%d",
					   rd_i16(payload), rd_i16(payload + 2), rd_i16(payload + 4), rd_i16(payload + 6),
					   rd_i16(payload + 8), rd_i16(payload + 10), rd_i16(payload + 12));
			break;

		default:
			/* Unknown or no-payload — dump raw bytes */
			if (sz > 0) {
				printf("raw=[");
				for (uint16_t i = 0; i < sz; i++)
					printf("%s%02x", i ? " " : "", payload[i]);
				printf("]");
			}
			break;
	}
}

static void TieFilmDump_DumpRecordStream(const TieFilmEntry* e, int indent) {
	uint16_t off = 0;
	TieFilmRecord rec;
	bool zero = false;

	while (TieFilm_RecordNext(e, &off, &rec, &zero)) {
		const char* name = TieFilm_CmdName(rec.cmd);
		printf("%*s@%-4u size=%-3u cmd=%-2u %-16s", indent, "", rec.offset, rec.size, rec.cmd,
			   name ? name : "???");
		if (rec.payload_size > 0)
			TieFilmDump_PrintPayload(rec.cmd, rec.payload, rec.payload_size);
		printf("\n");
	}

	if (zero)
		printf("%*s  [zero-size record, aborting stream]\n", indent, "");
	else if (off < e->records_size)
		printf("%*s  [%u trailing bytes after last record]\n", indent, "", e->records_size - off);
}

static bool TieFilmDump_DumpFilmChunk(const uint8_t* data, uint32_t size, const char* chunk_name) {
	TieFilmHeader h;
	if (!TieFilm_Parse(&h, data, size)) {
		fprintf(stderr, "  Film data too small (%u bytes)\n", size);
		return false;
	}

	printf("FILM \"%s\"\n", chunk_name);
	printf("  version:    %u%s\n", h.version, h.version != 4 ? " (UNSUPPORTED)" : "");
	printf("  cels:       %u (frames 0..%u)\n", h.cels, h.cels > 0 ? h.cels - 1 : 0);
	printf("  array_size: %u entries\n", h.array_size);
	printf("\n");

	uint32_t iter = 0;
	for (uint16_t i = 0; i < h.array_size; i++) {
		TieFilmEntry e;
		if (!TieFilm_EntryNext(&h, &iter, &e)) {
			fprintf(stderr, "  Entry %u: truncated header at offset %u\n", i, iter);
			return false;
		}

		char res_type[5];
		TieFilmFourcc_Str(e.res_type, res_type);
		printf("  Entry %u: %s \"%s\"  type_code=%u (%s)  records=%u  data=%u bytes", i, res_type, e.res_name,
			   e.type_code, TieFilm_TypeCodeName(e.type_code), e.num_records, e.data_size);

		uint32_t expected = FILM_ENTRY_HDR_SIZE + e.data_size;
		if (e.block_size != expected)
			printf("  [block_size=%u, expected %u]", e.block_size, expected);
		printf("\n");

		if (e.records_size < e.data_size)
			fprintf(stderr, "    [record stream extends past film data, truncated]\n");

		TieFilmDump_DumpRecordStream(&e, 4);
		printf("\n");
	}

	if (iter < h.size)
		printf("  [%u bytes remaining after last entry]\n\n", h.size - iter);

	return true;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <file.lfd> [film_name]\n", argv[0]);
		return 1;
	}

	const char* path = argv[1];
	const char* filter_name = (argc >= 3) ? argv[2] : NULL;

	TieLfdFile lfd;
	TieFilmUtil_OpenLfd(&lfd, path);

	int film_count = 0;
	for (uint32_t i = 0; i < lfd.count; i++) {
		const TieLfdFileEntry* e = &lfd.entries[i];
		if (e->type != FCC_FILM)
			continue;
		if (filter_name && strncmp(e->name, filter_name, 8) != 0)
			continue;

		if (film_count > 0)
			printf("========================================\n\n");
		TieFilmDump_DumpFilmChunk(TieLfdFile_Data(&lfd, e), e->size, e->name);
		film_count++;
	}

	if (film_count == 0) {
		if (filter_name)
			fprintf(stderr, "No FILM chunk named '%s' found in %s\n", filter_name, path);
		else
			fprintf(stderr, "No FILM chunks found in %s\n", path);
		TieLfdFile_Close(&lfd);
		return 1;
	}

	printf("--- %d FILM chunk(s) dumped ---\n", film_count);
	TieLfdFile_Close(&lfd);
	return 0;
}
