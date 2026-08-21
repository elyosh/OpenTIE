#include <stdlib.h>
#include <string.h>

#include <landru/bitmap.h>
#include <landru/rect.h>

void lbitmap_Init_Bitmap(BitmapStruct* bitmap) {
	lrect_Clear_Rect(&bitmap->clip);
	bitmap->w = 0;
	bitmap->h = 0;
	bitmap->data = NULL;
	bitmap->offset = 0;
	bitmap->type = 0;
	bitmap->flags = 0;
}

int16_t lbitmap_Alloc_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height) {
	return lbitmap_Alloc_Extra_System_Bitmap(bitmap, width, height, 0);
}

int16_t lbitmap_Alloc_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height) {
	return lbitmap_Alloc_Extra_Bitmap(bitmap, width, height, 0);
}

int16_t lbitmap_Alloc_Trans_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height) {
	int16_t result = lbitmap_Alloc_Extra_System_Bitmap(bitmap, width, height, 0);
	if (result)
		bitmap->flags = 1;
	return result;
}

int16_t lbitmap_Alloc_Trans_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height) {
	int16_t result = lbitmap_Alloc_Extra_Bitmap(bitmap, width, height, 0);
	if (result)
		bitmap->flags = 1;
	return result;
}

int16_t lbitmap_Alloc_Extra_Trans_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height,
												int32_t extend) {
	int16_t result = lbitmap_Alloc_Extra_System_Bitmap(bitmap, width, height, extend);
	if (result)
		bitmap->flags = 1;
	return result;
}

int16_t lbitmap_Alloc_Extra_Trans_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height,
										 int32_t extend) {
	int16_t result = lbitmap_Alloc_Extra_Bitmap(bitmap, width, height, extend);
	if (result)
		bitmap->flags = 1;
	return result;
}

int16_t lbitmap_Alloc_Extra_System_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height,
										  int32_t extend) {
	void* buf = malloc(width * height + extend);
	if (!buf) {
		lbitmap_Init_Bitmap(bitmap);
		return 0;
	}
	bitmap->data = buf;
	bitmap->offset = 0;
	lrect_Set_Rect(&bitmap->clip, 0, 0, width, height);
	bitmap->w = width;
	bitmap->h = height;
	return 1;
}

int16_t lbitmap_Alloc_Extra_Bitmap(BitmapStruct* bitmap, int16_t width, int16_t height, int32_t extend) {
	void* buf = malloc(width * height + extend);
	if (!buf) {
		lbitmap_Init_Bitmap(bitmap);
		return 0;
	}
	bitmap->data = buf;
	bitmap->offset = 0;
	lrect_Set_Rect(&bitmap->clip, 0, 0, width, height);
	bitmap->w = width;
	bitmap->h = height;
	return 1;
}

void lbitmap_Free_Bitmap(BitmapStruct* bitmap) {
	if (bitmap->data)
		free(bitmap->data);
	lbitmap_Init_Bitmap(bitmap);
}

void* lbitmap_Lock_Bitmap(BitmapStruct* bitmap) {
	if (!bitmap->data)
		return NULL;
	return (uint8_t*)bitmap->data + bitmap->offset;
}

void* lbitmap_Lock_Bitmap_Line(BitmapStruct* bitmap, int16_t line) {
	if (!bitmap->data)
		return NULL;
	return (uint8_t*)bitmap->data + bitmap->offset + line * bitmap->w;
}

/* Binary calls XMEMHDL_Unlock_Handle when bitmap->type == 0. With malloc
 * there's no compaction, so no lock/unlock pairing is needed. */
void lbitmap_Unlock_Bitmap(BitmapStruct* bitmap) { (void)bitmap; }

bool lbitmap_Clip_Rect_To_Bitmap(BitmapStruct* bitmap, Rect* rect) {
	return lrect_Clip_Rect(rect, &bitmap->clip);
}

void lbitmap_Get_Bitmap_Clipping(BitmapStruct* bitmap, Rect* out) { *out = bitmap->clip; }

void lbitmap_Set_Bitmap_Clipping(BitmapStruct* bitmap, Rect* rect) {
	lbitmap_Max_Bitmap_Clipping(bitmap);
	lrect_Clip_Rect(&bitmap->clip, rect);
}

void lbitmap_Max_Bitmap_Clipping(BitmapStruct* bitmap) {
	lrect_Set_Rect(&bitmap->clip, 0, 0, bitmap->w, bitmap->h);
}

void lbitmap_Set_Bitmap_Offset(BitmapStruct* bitmap, int32_t offset) { bitmap->offset = offset; }

int32_t lbitmap_Get_Bitmap_Offset(BitmapStruct* bitmap) { return bitmap->offset; }

void lbitmap_Erase_Bitmap(BitmapStruct* bitmap) {
	if (!bitmap->data)
		return;
	void* ptr = lbitmap_Lock_Bitmap(bitmap);
	memset(ptr, 0, bitmap->w * bitmap->h);
	lbitmap_Unlock_Bitmap(bitmap);
}

int16_t lbitmap_Copy_Bitmap(BitmapStruct* dst, BitmapStruct* src, int16_t x, int16_t y) {
	Rect rect;
	lrect_Set_Rect(&rect, 0, 0, src->w, src->h);
	return lbitmap_Copy_Bitmap_Portion(dst, src, &rect, x, y);
}

int16_t lbitmap_Copy_Bitmap_Clip(BitmapStruct* dst, BitmapStruct* src, int16_t x, int16_t y) {
	Rect src_clip = src->clip;
	return lbitmap_Copy_Bitmap_Portion(dst, src, &src_clip, x - src->clip.left, y - src->clip.top);
}

int16_t lbitmap_Copy_Bitmap_Offset_Clip(BitmapStruct* dst, BitmapStruct* src, int16_t x, int16_t y) {
	return lbitmap_Copy_Bitmap_Portion(dst, src, &src->clip, x, y);
}

int16_t lbitmap_Copy_Bitmap_Portion(BitmapStruct* dst, BitmapStruct* src, Rect* clipRect, int16_t x,
									int16_t y) {
	Rect src_clipped;
	Rect dst_rect;
	Rect dst_clipped;

	lrect_Copy_Rect(&src_clipped, clipRect);
	if (!lrect_Clip_Rect(&src_clipped, &src->clip))
		return 0;

	lrect_Copy_Rect(&dst_rect, &src_clipped);
	lrect_Offset_Rect(&dst_rect, x - clipRect->left, y - clipRect->top);
	lrect_Copy_Rect(&dst_clipped, &dst_rect);
	if (!lrect_Clip_Rect(&dst_clipped, &dst->clip))
		return 0;

	src_clipped.left += dst_clipped.left - dst_rect.left;
	src_clipped.top += dst_clipped.top - dst_rect.top;
	src_clipped.right += dst_clipped.right - dst_rect.right;
	src_clipped.bottom += dst_clipped.bottom - dst_rect.bottom;
	lbitmap_Copy_Bitmap_Rect(dst, src, &dst_clipped, &src_clipped);
	return 1;
}

void lbitmap_Copy_Bitmap_Rect(BitmapStruct* dst, BitmapStruct* src, Rect* dst_rect, Rect* src_rect) {
	uint8_t* pSrc = lbitmap_Lock_Bitmap(src);
	uint8_t* pDst = lbitmap_Lock_Bitmap(dst);

	if (pSrc && pDst) {
		int16_t sw = src->w;
		int16_t dw = dst->w;
		int16_t copy_w = dst_rect->right - dst_rect->left;
		int16_t copy_h = dst_rect->bottom - dst_rect->top;
		uint8_t* s = pSrc + src_rect->left + src_rect->top * sw;
		uint8_t* d = pDst + dst_rect->left + dst_rect->top * dw;

		if (src->flags & 1)
			lbitmap_Copy_Trans_Bitmap_Data(dw, copy_h, copy_w, sw, d, s);
		else
			lbitmap_Copy_Bitmap_Data(dw, copy_h, copy_w, sw, d, s);
	}

	lbitmap_Unlock_Bitmap(dst);
	lbitmap_Unlock_Bitmap(src);
}

void lbitmap_Copy_Bitmap_Data(int32_t dst_stride, int32_t num_lines, int32_t copy_width, int32_t src_stride,
							  uint8_t* dst, uint8_t* src) {
	int32_t dst_pad = dst_stride - copy_width;
	int32_t src_pad = src_stride - copy_width;

	while (num_lines--) {
		memcpy(dst, src, copy_width);
		dst += copy_width + dst_pad;
		src += copy_width + src_pad;
	}
}

void lbitmap_Copy_Trans_Bitmap_Data(int32_t dst_stride, int32_t num_lines, int32_t copy_width,
									int32_t src_stride, uint8_t* dst, const uint8_t* src) {
	int32_t dst_pad = dst_stride - copy_width;
	int32_t src_pad = src_stride - copy_width;

	while (num_lines--) {
		for (int32_t x = 0; x < copy_width; x++) {
			if (src[x])
				dst[x] = src[x];
		}
		dst += copy_width + dst_pad;
		src += copy_width + src_pad;
	}
}
