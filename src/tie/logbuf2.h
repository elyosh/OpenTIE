#ifndef __LOGBUF2_H__
#define __LOGBUF2_H__

#include <stdint.h>

/*
 * LOGBUF2 — Logical drawing-buffer module.
 *
 * Owns a single off-screen surface (buffer_ptr, geometry in
 * pixelswide x pixelsdeep) that the game code draws into.
 * outbuffer / outdiffbuffer later copy it onto the linear framebuffer at
 * displaycorner (linear byte offset). Also provides a clipped 2D line
 * rasterizer and PIP viewport save/restore.
 */

/* Viewport / logical-buffer state (shared; other modules read these). */
extern uint16_t pixelswide;
extern uint16_t pixelswidemin1;
extern uint16_t halfpixelswide;
extern uint16_t pixelsdeep;
extern uint16_t pixelsdeepmin1;
extern uint16_t halfpixelsdeep;
extern uint32_t displaycorner;
extern uint32_t displaycorner_lines;
extern uint32_t displaycorner_columns;
extern void* buffer_ptr;

/* Background palette index used by the logical-buffer clear. */
extern uint8_t deepspacecolor;

// GLOBAL: TIE98 0x5926D8
extern uint32_t g_surfacePitch;
// GLOBAL: TIE98 0x4F2ACC
extern uint32_t g_flight16bppBytesPerPixel;

/* --- API --- */

void logbuf2_graphsetup(void);
void logbuf2_selectbuffer(void* buffer);
void logbuf2_setbufferdimensions(uint16_t width, uint16_t depth, uint32_t displaycorner);
void logbuf2_setbufferdimensions_tie98(uint16_t width, uint16_t depth, int unused, uint32_t displaycorner);
void logbuf2_clearbuffer(void);
void logbuf2_clearbuffer_tie98(void);
void logbuf2_outbuffer(const void* src);
void logbuf2_outbuffer_tie98(const void* src);
void logbuf2_outdiffbuffer(const void* oldbuf, const void* newbuf);
void logbuf2_outdiffbuffer_tie98(const void* oldbuf, const void* newbuf);
void logbuf2_drawclippedline(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color);
void logbuf2_drawclippedline_tie98(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color);
void logbuf2_startPIP(uint16_t width, uint16_t depth, int16_t clear_runs, uint32_t displaycorner);
void logbuf2_finishPIP(void);
void logbuf2_startPIP_tie98(uint16_t width, uint16_t depth, int clear_runs, uint32_t displaycorner);
void logbuf2_finishPIP_tie98(void);

#endif
