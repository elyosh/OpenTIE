#ifndef LANDRU_VESA_H
#define LANDRU_VESA_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/bitmap.h>
#include <landru/rect.h>

extern int16_t vesa_w_gbl;
extern int16_t vesa_h_gbl;
extern int16_t vesa_bpsl_gbl;
extern uint8_t* vesa_buff_gbl;
extern Rect vesa_rect;

/* Set when vesa_buff_gbl has been written since the last consumer upload. */
extern bool vesa_dirty_gbl;

typedef enum LandruVideoFlags {
	LANDRU_VIDEO_VGA_COMPAT = 0x02,
	LANDRU_VIDEO_ALTERNATE_LINES = 0x04,
	LANDRU_VIDEO_REPEAT_LINES = 0x08,
} LandruVideoFlags;

extern uint16_t landru_video_flags_gbl;
extern int16_t landru_logical_width_gbl;
extern int16_t landru_logical_height_gbl;
extern uint8_t* vga_compat_buffer_gbl;

typedef enum LandruPortVideoBackend {
	LANDRU_PORT_VIDEO_SOFTWARE,
	LANDRU_PORT_VIDEO_PLATFORM,
} LandruPortVideoBackend;

void XBM_Set_VGA_Compatibility_Mode(int enabled, int logical_width, int logical_height);

void* lvesa_Get_Vesa_Mode_Struct(void);
uint8_t* XVESA_Get_Video_Buffer(void);
void XVESA_Set_Video_Buffer(uint8_t* pixels);
void lvesa_Set_Platform_Pitch(int16_t pitch);
uint32_t XVESA_Get_Linear_Buffer_Size(void);
void XVESA_Set_Linear_Buffer_Size(uint32_t size_bytes);
void lvesa_Create_Vesa_Module(uint16_t mode);
void lvesa_Destroy_VESA_Module(void);
/* PORT: no recovered counterparts; configure startup ownership, then switch
 * host/software presentation while retaining the inactive software buffer. */
bool landru_port_Set_Initial_Video_Backend(LandruPortVideoBackend backend);
bool landru_port_Select_Video_Backend(LandruPortVideoBackend backend, uint16_t mode);
void lvesa_Enter_VESA_Mode(uint16_t mode);
int16_t lvesa_Set_VESA_Mode_Internal(uint16_t mode);
void lvesa_Erase_Video(uint8_t color);
int lvesa_Copy_Bitmap_Clip_To_Video(BitmapStruct* bm, int16_t x, int16_t y);
int lvesa_Copy_Bitmap_Portion_To_Video(BitmapStruct* bm, Rect* rect, int16_t x, int16_t y);
int lvesa_Copy_Video_Portion_To_Bitmap(BitmapStruct* bm, Rect* rect, int16_t x, int16_t y);
void lvesa_Copy_Bitmap_Video(BitmapStruct* bm, Rect* src, int16_t dst_x, int16_t dst_y, int16_t to_video);
void lvesa_Copy_Bitmap_Video_Data(BitmapStruct* bm, uint8_t* dst, uint8_t* src, int16_t lines, int16_t width,
								  int16_t dst_stride, int16_t src_stride);

int lvesa_Copy_Diff_Bitmap_Portion_To_Video(BitmapStruct* bm, BitmapStruct* diff, Rect* rect, int16_t x,
											int16_t y);
void lvesa_Copy_Diff_Bitmap_Video(BitmapStruct* bm, BitmapStruct* diff, Rect* src, int16_t dst_x,
								  int16_t dst_y);
void lvesa_Copy_Diff_Bitmap_Data(int lines, int width, int stride, uint8_t* dst, const uint8_t* src);

#endif
