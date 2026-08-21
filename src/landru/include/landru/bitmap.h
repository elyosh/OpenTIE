#ifndef LANDRU_BITMAP_H
#define LANDRU_BITMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>

typedef struct {
	void* data;
	int32_t offset;
	Rect clip;
	int16_t w;
	int16_t h;
	uint8_t type;
	uint8_t flags;
} BitmapStruct;

void lbitmap_Init_Bitmap(BitmapStruct* bitmap);
int16_t lbitmap_Alloc_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height);
int16_t lbitmap_Alloc_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height);
int16_t lbitmap_Alloc_Trans_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height);
int16_t lbitmap_Alloc_Trans_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height);
int16_t lbitmap_Alloc_Extra_Trans_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height,
												int32_t extend);
int16_t lbitmap_Alloc_Extra_Trans_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height, int32_t extend);
int16_t lbitmap_Alloc_Extra_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height,
										  int32_t extend);
int16_t lbitmap_Alloc_Extra_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height, int32_t extend);
void lbitmap_Free_Bitmap(BitmapStruct* bitmap);
void* lbitmap_Lock_Bitmap(BitmapStruct* bitmap);
void* lbitmap_Lock_Bitmap_Line(BitmapStruct* bitmap, int16_t line);
void lbitmap_Unlock_Bitmap(BitmapStruct* bitmap);
bool lbitmap_Clip_Rect_To_Bitmap(BitmapStruct* bitmap, Rect* rect);
void lbitmap_Get_Bitmap_Clipping(BitmapStruct* bitmap, Rect* out);
void lbitmap_Set_Bitmap_Clipping(BitmapStruct* bitmap, Rect* rect);
void lbitmap_Max_Bitmap_Clipping(BitmapStruct* bitmap);
void lbitmap_Set_Bitmap_Offset(BitmapStruct* bitmap, int32_t offset);
int32_t lbitmap_Get_Bitmap_Offset(BitmapStruct* bitmap);
void lbitmap_Erase_Bitmap(BitmapStruct* bitmap);
int16_t lbitmap_Copy_Bitmap(BitmapStruct* dst, BitmapStruct* src, int16_t x, int16_t y);
int16_t lbitmap_Copy_Bitmap_Clip(BitmapStruct* dst, BitmapStruct* src, int16_t x, int16_t y);
int16_t lbitmap_Copy_Bitmap_Offset_Clip(BitmapStruct* dst, BitmapStruct* src, int16_t x, int16_t y);
int16_t lbitmap_Copy_Bitmap_Portion(BitmapStruct* dst, BitmapStruct* src, Rect* clipRect, int16_t x,
									int16_t y);
void lbitmap_Copy_Bitmap_Rect(BitmapStruct* dst, BitmapStruct* src, Rect* dst_rect, Rect* src_rect);
void lbitmap_Copy_Bitmap_Data(int32_t dst_stride, int32_t num_lines, int32_t copy_width, int32_t src_stride,
							  uint8_t* dst, uint8_t* src);
void lbitmap_Copy_Trans_Bitmap_Data(int32_t dst_stride, int32_t num_lines, int32_t copy_width,
									int32_t src_stride, uint8_t* dst, const uint8_t* src);

#endif
