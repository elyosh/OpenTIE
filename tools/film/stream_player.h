/*
 * stream_player — sequential WRK FMV decoder for static FILM viewers.
 *
 * Drives the engine's drawstrm block decoder against a .WRK file,
 * exposing one frame at a time as a 320×200 8-bit indexed buffer.
 * Pairs with film_player.c: each FILM cel that displays a CUST/var1=123
 * stream actor advances the session by one frame.
 *
 * On-disk WRK format (verified against play1_Update_Stream_Actor):
 *   - 16-byte header: u16 magic=2, u16 frame_count, 12 bytes opaque.
 *   - Per frame: u32 size_le, then `size` bytes of encoded data fed to
 *     drawstrm_Convert_Frame_To_Palette(prev, frame_buf, cur).
 *
 * The decoder requires sequential access — each frame references the
 * previous as a delta source. Backward seeks must rewind to frame 0
 * and re-decode forward.
 */
#ifndef FILM_STREAM_PLAYER_H
#define FILM_STREAM_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define STREAM_FB_W 320
#define STREAM_FB_H 200
#define STREAM_FB_SZ (STREAM_FB_W * STREAM_FB_H)

typedef struct {
	FILE* fp;              /* owned; NULL when closed */
	char* path;            /* duplicated for diagnostic logging */
	uint16_t total_frames; /* from header @+2; 0 => empty stream */
	uint16_t cur_frame;    /* number of frames consumed so far */
	uint8_t* prev;         /* STREAM_FB_SZ — reference for next decode */
	uint8_t* cur;          /* STREAM_FB_SZ — last decoded frame (display) */
	uint8_t* frame_buf;    /* encoded-frame staging; grown on demand */
	size_t frame_buf_cap;
	bool header_ok; /* false if header was malformed */
} TieFilmStreamSession;

/* Open `path`, allocate buffers, parse the 16-byte header. Returns
 * false (and leaves the session zeroed) on I/O or parse failure. The
 * cur/prev buffers start zero-filled, matching the engine's
 * lbitmap_Erase_Bitmap on init. */
bool TieFilmStreamPlayer_StreamOpen(TieFilmStreamSession* s, const char* path);

/* Free buffers, close the file. Safe to call on a zeroed session. */
void TieFilmStreamPlayer_StreamClose(TieFilmStreamSession* s);

/* Decode the next frame. Returns false at EOS (cur_frame ==
 * total_frames) or on a read error. After success, s->cur holds the
 * new frame; s->prev mirrors it (next decode will use s->prev as
 * reference). */
bool TieFilmStreamPlayer_StreamAdvanceOneFrame(TieFilmStreamSession* s);

/* Reposition to frame 0. Re-reads the header so total_frames is
 * refreshed. Buffers are zeroed — the engine starts each stream from
 * a black reference too (lbitmap_Erase_Bitmap on Init). */
bool TieFilmStreamPlayer_StreamRewind(TieFilmStreamSession* s);

#ifdef __cplusplus
}
#endif

#endif
