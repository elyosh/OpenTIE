#ifndef TIE_RTSVGA2_H
#define TIE_RTSVGA2_H

#ifdef TIE_MODERN
#include "tie_runtime/display/tie98_starfield.h"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Low-resolution 8-bit renderer using a linear framebuffer. VESA page
 * selection is a no-op. */

/* -------------------------------------------------------------------- */
/* RTSVGA2-owned globals (watdbg-owned by RTSVGA2 in the demo; the      */
/* retail binary keeps the same owner).                                 */
/* -------------------------------------------------------------------- */

/* 256-color VGA palette, 3 bytes per entry (R, G, B; 6-bit DAC values). */
extern uint8_t rtsvga2_vgapalette[768];

/* Linear 8bpp framebuffer base. NULL until initgraph / setvgapointers. */
extern uint8_t* vgapointer;

/* Scanline-start LUT: lineaddressVGA[y] = y * screenMemWidth. The
 * originals hold 480 lines; drawshape's 0xFE-then-0xFF tail reads one line
 * past the last drawn row before terminating, so the port keeps one more
 * entry. */
#ifdef TIE_MODERN
#define RTSVGA2_LINE_ADDRESS_COUNT 481
#else
#define RTSVGA2_LINE_ADDRESS_COUNT 480
#endif
extern int32_t lineaddressVGA[RTSVGA2_LINE_ADDRESS_COUNT];

/* Star double-buffer pointers: two int[768] lists of pixel-offsets;
 * swapped on every drawstars call. newstarptr is this frame's write
 * target, oldstarptr is the previous frame's drawn list (used by the
 * erase pass). */
extern int32_t* newstarptr;
extern int32_t* oldstarptr;

/* 3x3 scaled-camera-basis tables used by the starfield renderer
 * (populated per-frame by BACKDRP2_backdrop). shift*[0..15]. */
extern int32_t shiftA1mul[16], shiftA2mul[16], shiftA3mul[16];
extern int32_t shiftB1mul[16], shiftB2mul[16], shiftB3mul[16];
extern int32_t shiftC1mul[16], shiftC2mul[16], shiftC3mul[16];

/* 5x5x5 parallax-star grid in eye space (125 used of 128 slots). */
extern int32_t stareyex[128], stareyey[128], stareyez[128];

/* Shape blitter state shared with LOGBUF2 and other modules. */
extern uint16_t stardetaillevel; /* 1 = full detail, >1 = coarser */
extern int16_t drawshapex;
extern int16_t drawshapey;
extern int16_t drawwidth;           /* reused as scratch by save/restore box */
extern uint8_t basecolor;           /* shape blitter running base color */
extern int16_t skipcolorvga;        /* shape "transparent" color (low byte) */
extern uint32_t brightness_setting; /* [256..768] step 64; wraps 768 -> 256 */

/* -------------------------------------------------------------------- */
/* Public API                                                           */
/* -------------------------------------------------------------------- */

struct RadarBlip;

/* Graphics init / VESA window management */
void rtsvga2_initgraphVGA(void);
void rtsvga2_setvgapointers(void* vga_ptr, uint16_t mem_width, uint16_t num_lines);
void rtsvga2_SetCurrentPage(uint8_t window, uint16_t page);
void rtsvga2_InvalidatePageCache(void);
void rtsvga2_SetVESAScanLineLength(uint32_t width_px);

/* Palette */
void rtsvga2_blankVGA(void);
void rtsvga2_unblankVGA(void);
void rtsvga2_clearflightdisplay(void);
void rtsvga2_buildpaletteVGA(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count);
void rtsvga2_savepaletteVGA(uint8_t* rgb_dst);
void rtsvga2_restorepaletteVGA(const uint8_t* rgb_src);
void rtsvga2_buildpaletteVGA_tie98(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count);
void rtsvga2_applyBrightness16_tie98(const uint8_t* rgb6, uint16_t* output, uint32_t start_idx,
									 uint32_t count);
void rtsvga2_savepaletteVGA_tie98(uint8_t* rgb_dst);
void rtsvga2_restorepaletteVGA_tie98(const uint8_t* rgb_src);

/* Retail-only palette helpers */
void rtsvga2_applyBrightness(const uint8_t* rgb_src, uint8_t* rgb_dst, int start_idx, int count);
uint32_t rtsvga2_findNearestColor(const uint8_t* rgb_target, const uint8_t* palette, uint32_t start_idx,
								  uint32_t end_idx);
void rtsvga2_remapRGBImage(uint32_t* image_header);
void rtsvga2_remapRGBImage_tie98(uint32_t* image_header);

/* Position helper */
uint32_t rtsvga2_calcpositionVGA(uint16_t x, uint16_t y);
uint32_t rtsvga2_calcpositionVGA_tie98(uint16_t x, uint16_t y);

/* Shape blitter */
void rtsvga2_drawshapeVGA(const uint8_t* shape, int16_t x, int16_t y, int16_t skip_color, uint16_t flip_x);
void rtsvga2_drawshapeVGA_tie98(const uint8_t* shape, int16_t x, int16_t y, int16_t skip_color,
								uint16_t flip_x);
void rtsvga2_drawmonoshapeVGA(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color,
							  uint16_t color);
void rtsvga2__lowdrawshapeVGA(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color, int flip_x,
							  char mono_flag);
void rtsvga2_drawdotVGA(uint16_t x, uint16_t y, uint8_t color);

/* Text */
void rtsvga2_outcharVGA(uint8_t ch);
void rtsvga2_outchar32VGA(uint8_t ch);
void rtsvga2_outchar32VGA_tie98(uint8_t ch);
void rtsvga2_autofillVGA(void);
void rtsvga2_autofillVGA_tie98(void);

/* Rect fills */
void rtsvga2_clearwindowVGA(void);
void rtsvga2_fillrectangleVGA(void);
void rtsvga2_fillboxVGA(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);
void rtsvga2_clearwindowVGA_tie98(void);
void rtsvga2_fillrectangleVGA_tie98(void);
void rtsvga2_fillboxVGA_tie98(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);

/* Buffer scroll / save / restore */
void rtsvga2_scrollbufferVGA(uint8_t* buffer, uint16_t num_rows, int16_t scroll_up);
void rtsvga2_saveboxVGA(uint8_t* dst, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void rtsvga2_restoreboxVGA(const uint8_t* src, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void rtsvga2_saveboxVGA_tie98(uint8_t* dst, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void rtsvga2_restoreboxVGA_tie98(const uint8_t* src, uint16_t x, uint16_t y, uint16_t width, uint16_t height);

/* Radar blips */
void rtsvga2_drawblipsVGA(struct RadarBlip* blips, uint16_t count);
void rtsvga2_removeblipsVGA(struct RadarBlip* blips, uint16_t count);

/* Reticles */
void rtsvga2_drawbracket(void);
void rtsvga2_removebracket(void);
void rtsvga2_drawcross(uint16_t x, uint16_t y, uint8_t color);
void rtsvga2_removecross(uint16_t x, uint16_t y);

/* Starfield */
void rtsvga2_drawstars(void);
void rtsvga2_drawstars_tie98(void);

/* TIE98 drawstars lazy-initialisation flags for its star tables. */
extern int g_starColor8Allocated;
extern int g_starColor8Initialized;
extern int g_starColor16Initialized;
extern int g_starPositionsInitialized;

/* Retail-only screenshot */
int rtsvga2_takeScreenshot(void);

#ifdef __cplusplus
}
#endif

#endif
