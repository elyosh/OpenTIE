#ifndef LANDRU_CURSOR_H
#define LANDRU_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

void lcursor_Create_Cursor_Module(void);
void lcursor_Destroy_Cursor_Module(void);
void lcursor_Set_Cursor_HotSpot(int16_t hot_x, int16_t hot_y);
void lcursor_Get_Cursor_HotSpot(int16_t* out_x, int16_t* out_y);
void lcursor_Set_Cursor(uint8_t cursorId);
void lcursor_Push_Cursor_Canvas(void);
void lcursor_Pop_Cursor_Canvas(void);
void lcursor_Set_Cursor_Canvas(void);
void lcursor_Cursor_To_Screen(int16_t x, int16_t y);
void lcursor_Draw_Cursor(int16_t x, int16_t y, int16_t offscreen);
void lcursor_Erase_Cursor(int16_t offscreen);
void lcursor_Cursor_To_Back(void);
void lcursor_Cursor_To_Front(void);
void lcursor_Cursor_From_Fade(void);
bool lcursor_Is_Cursor(void);
bool lcursor_Is_Cursor_Visible(void);
void lcursor_Enable_Cursor(void);
void lcursor_Disable_Cursor(void);
void lcursor_Refresh_Cursor(void);
void lcursor_Show_Cursor(void);
void lcursor_Hide_Cursor(void);

/* PORT: selects a presentation-owned cursor without changing Landru's
 * visibility, input, hotspot or cursor-kind state. */
void lcursor_port_Set_External_Presentation(bool external);

extern int g_softwareCursorEnabled;

int XCURSOR_Get_Display_Count(void);
void XCURSOR_Draw_Software_Cursor_To_Surface(uint8_t* surface, int pitch, int height);
void XCURSOR_Select_Contrast_Colors(const uint8_t* rgb6_palette);

/* Publish the current cursor pose to the optional render sink. */
void lcursor_emit_render_state(void);

/* Read-only access to the cursor's current 8bpp bitmap. `*out_w / *out_h`
 * give the bitmap dimensions;
 * data is `out_w * out_h` bytes of palette indices (8bpp). Returns
 * NULL if no cursor is loaded. The returned pointer is valid until the
 * next lcursor_Set_Cursor or module shutdown. */
const uint8_t* lcursor_get_bitmap(int16_t* out_w, int16_t* out_h);

#endif
