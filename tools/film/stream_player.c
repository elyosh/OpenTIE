#include "stream_player.h"

#include "tie/drawstrm.h"

#include <stdlib.h>
#include <string.h>

#define WRK_HEADER_SIZE 16
#define WRK_INITIAL_BUF 16384 /* grown on demand if a frame is larger */

static char* TieFilmStreamPlayer_DuplicateString(const char* value) {
	size_t length = strlen(value) + 1;
	char* copy = (char*)malloc(length);
	if (copy)
		memcpy(copy, value, length);
	return copy;
}

/* Read little-endian u16/u32 from a byte buffer. WRK is DOS-era
 * Watcom output; everything on disk is LE regardless of host. */
static uint16_t TieFilmStreamPlayer_ReadU16Le(const uint8_t* p) {
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t TieFilmStreamPlayer_ReadU32Le(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Grow s->frame_buf to hold at least `needed` bytes. Returns false on
 * allocation failure (rare; rolls back nothing — caller treats the
 * session as dead). */
static bool TieFilmStreamPlayer_EnsureFrameBuf(TieFilmStreamSession* s, size_t needed) {
	if (s->frame_buf_cap >= needed)
		return true;
	size_t n = s->frame_buf_cap ? s->frame_buf_cap : WRK_INITIAL_BUF;
	while (n < needed)
		n *= 2;
	uint8_t* nb = (uint8_t*)realloc(s->frame_buf, n);
	if (!nb)
		return false;
	s->frame_buf = nb;
	s->frame_buf_cap = n;
	return true;
}

/* Read the 16-byte header at the current file position into a stack
 * buffer; populate total_frames from offset +2. Returns false on
 * short read. The header's other fields aren't used by the engine's
 * stream_actor path, so we don't expose them. */
static bool TieFilmStreamPlayer_ReadHeader(TieFilmStreamSession* s) {
	uint8_t hdr[WRK_HEADER_SIZE];
	if (fread(hdr, 1, sizeof hdr, s->fp) != sizeof hdr) {
		s->header_ok = false;
		return false;
	}
	s->total_frames = TieFilmStreamPlayer_ReadU16Le(hdr + 2);
	s->header_ok = true;
	return true;
}

bool TieFilmStreamPlayer_StreamOpen(TieFilmStreamSession* s, const char* path) {
	memset(s, 0, sizeof *s);
	if (!path || !*path)
		return false;

	s->fp = fopen(path, "rb");
	if (!s->fp)
		return false;

	s->path = TieFilmStreamPlayer_DuplicateString(path);
	if (!s->path)
		goto fail;

	s->prev = (uint8_t*)calloc(STREAM_FB_SZ, 1);
	s->cur = (uint8_t*)calloc(STREAM_FB_SZ, 1);
	if (!s->prev || !s->cur)
		goto fail;

	if (!TieFilmStreamPlayer_EnsureFrameBuf(s, WRK_INITIAL_BUF))
		goto fail;

	if (!TieFilmStreamPlayer_ReadHeader(s))
		goto fail;

	s->cur_frame = 0;
	return true;

fail:
	TieFilmStreamPlayer_StreamClose(s);
	return false;
}

void TieFilmStreamPlayer_StreamClose(TieFilmStreamSession* s) {
	if (!s)
		return;
	if (s->fp) {
		fclose(s->fp);
		s->fp = NULL;
	}
	free(s->path);
	s->path = NULL;
	free(s->prev);
	s->prev = NULL;
	free(s->cur);
	s->cur = NULL;
	free(s->frame_buf);
	s->frame_buf = NULL;
	s->frame_buf_cap = 0;
	s->total_frames = 0;
	s->cur_frame = 0;
	s->header_ok = false;
}

bool TieFilmStreamPlayer_StreamAdvanceOneFrame(TieFilmStreamSession* s) {
	if (!s || !s->fp || !s->header_ok)
		return false;
	if (s->cur_frame >= s->total_frames)
		return false;

	uint8_t size_buf[4];
	if (fread(size_buf, 1, 4, s->fp) != 4)
		return false;
	uint32_t size = TieFilmStreamPlayer_ReadU32Le(size_buf);
	/* Defend against runaway sizes from a corrupt file — the engine's
	 * STREAM_BUFFER_SIZE was 128000 bytes; real WRKs stay well under
	 * that. Accept up to 256 KB to leave headroom. */
	if (size == 0 || size > 256u * 1024u)
		return false;
	if (!TieFilmStreamPlayer_EnsureFrameBuf(s, size))
		return false;

	if (fread(s->frame_buf, 1, size, s->fp) != size)
		return false;

	/* drawstrm decodes into `cur` using `prev` as reference, then
	 * copies cur → prev so next call uses the new frame. */
	drawstrm_Convert_Frame_To_Palette(s->prev, s->frame_buf, s->cur);

	s->cur_frame++;
	return true;
}

bool TieFilmStreamPlayer_StreamRewind(TieFilmStreamSession* s) {
	if (!s || !s->fp)
		return false;
	if (fseek(s->fp, 0, SEEK_SET) != 0)
		return false;

	memset(s->prev, 0, STREAM_FB_SZ);
	memset(s->cur, 0, STREAM_FB_SZ);
	s->cur_frame = 0;
	return TieFilmStreamPlayer_ReadHeader(s);
}
