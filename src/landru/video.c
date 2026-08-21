#include <landru/cursor.h>
#include <landru/vesa.h>
#include <landru/video.h>

#include <string.h>

// GLOBAL: TIE98 0x58BA8C
int vga_compat_slow_blit_gbl;

// FUNCTION: TIE98 0x4C1BB0
static void VIDEO_Copy_Indexed_Rect_1x(uint8_t* dst, int dst_stride, const uint8_t* src, int width,
									   int height, int src_stride, int dst_x, int dst_y) {
	uint8_t* output = dst + dst_x + dst_stride * dst_y;
	for (int y = 0; y < height; ++y) {
		memcpy(output, src, (size_t)width);
		output += dst_stride;
		src += src_stride;
	}
}

// FUNCTION: TIE98 0x4C1D80
static void VIDEO_Copy_Indexed_Rect_2x(uint8_t* dst, int dst_stride, const uint8_t* src, int width,
									   int height, int src_stride, int dst_x, int dst_y) {
	uint8_t* output = dst + 2 * dst_x + 2 * dst_stride * dst_y;
	for (int y = 0; y < height; ++y) {
		uint8_t* upper = output;
		uint8_t* lower = output + dst_stride;
		for (int x = 0; x < width; ++x) {
			upper[2 * x] = src[x];
			upper[2 * x + 1] = src[x];
			lower[2 * x] = src[x];
			lower[2 * x + 1] = src[x];
		}
		output += 2 * dst_stride;
		src += src_stride;
	}
}

// FUNCTION: TIE98 0x4C2170
static void VIDEO_Copy_Indexed_Rect_1x_Alternate_Lines(uint8_t* dst, int dst_stride, const uint8_t* src,
													   int width, int height, int src_stride, int dst_x,
													   int dst_y) {
	uint8_t* output = dst + dst_x + dst_stride * dst_y;
	for (int y = 0; y < height; ++y) {
		memcpy(output, src, (size_t)width);
		output += 2 * dst_stride;
		src += src_stride;
	}
}

// FUNCTION: TIE98 0x4C22E0
static void VIDEO_Clear_2x_Vertical_Borders(uint8_t* dst, int dst_stride, int dst_height, int width,
											int height) {
	const int first_row = (dst_height - 2 * height) >> 1;
	const int last_row = first_row + 2 * height;
	for (int y = 0; y < first_row; ++y)
		memset(dst + y * dst_stride, 0, (size_t)(2 * width));
	for (int y = last_row; y < dst_height; ++y)
		memset(dst + y * dst_stride, 0, (size_t)(2 * width));
}

// FUNCTION: TIE98 0x4C24E0
static void VIDEO_Copy_Indexed_Rect_Repeat_Every_Sixth_Line(uint8_t* dst, int dst_stride, const uint8_t* src,
															int width, int height, int src_stride, int dst_x,
															int dst_y) {
	int line_counter = dst_y % 6;
	uint8_t* output = dst + dst_x + dst_stride * (dst_y + dst_y / 6);
	for (int y = 0; y < height; ++y) {
		memcpy(output, src, (size_t)width);
		output += dst_stride;
		if (++line_counter == 6) {
			memcpy(output, src, (size_t)width);
			output += dst_stride;
			line_counter = 0;
		}
		src += src_stride;
	}
}

// FUNCTION: TIE98 0x4C2920
static void VIDEO_Copy_Indexed_Rect_2x_Alternate_Lines(uint8_t* dst, int dst_stride, const uint8_t* src,
													   int width, int height, int src_stride, int dst_x,
													   int dst_y) {
	uint8_t* output = dst + 2 * dst_x + 2 * dst_stride * dst_y;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			output[2 * x] = src[x];
			output[2 * x + 1] = src[x];
		}
		output += 2 * dst_stride;
		src += src_stride;
	}
}

// FUNCTION: TIE98 0x4C2B20
static void VIDEO_Copy_Indexed_Rect_2x_Repeat_Every_Third_Line(uint8_t* dst, int dst_stride,
															   const uint8_t* src, int width, int height,
															   int src_stride, int dst_x, int dst_y) {
	int line_counter = 2 * dst_y % 6;
	uint8_t* output = dst + 2 * dst_x + dst_stride * (2 * dst_y + 2 * dst_y / 6);
	for (int y = 0; y < height; ++y) {
		for (int repeat = 0; repeat < 2; ++repeat) {
			for (int x = 0; x < width; ++x) {
				output[2 * x] = src[x];
				output[2 * x + 1] = src[x];
			}
			output += dst_stride;
		}
		if (++line_counter == 3) {
			memcpy(output, output - dst_stride, (size_t)(2 * width));
			output += dst_stride;
			line_counter = 0;
		}
		src += src_stride;
	}
}

// FUNCTION: TIE98 0x4C3CA0
void VIDEO_Blit_Indexed_Rect(uint8_t* dst, int dst_stride, int dst_height, const uint8_t* src, int width,
							 int height, int src_stride, int dst_x, int dst_y, int flags) {
	if (dst_x < 0) {
		width += dst_x;
		src -= dst_x;
		dst_x = 0;
	}
	if (dst_y < 0) {
		height += dst_y;
		src -= src_stride * dst_y;
		dst_y = 0;
	}
	if (dst_x + width > landru_logical_width_gbl)
		width = landru_logical_width_gbl - dst_x;
	if (dst_y + height > landru_logical_height_gbl)
		height = landru_logical_height_gbl - dst_y;
	if (width <= 0 || height <= 0)
		return;

	if (vga_compat_slow_blit_gbl && (flags & LANDRU_VIDEO_VGA_COMPAT) && XCURSOR_Get_Display_Count() < 0)
		flags |= LANDRU_VIDEO_ALTERNATE_LINES;

	switch (flags & 0x0e) {
		case 0:
			VIDEO_Copy_Indexed_Rect_1x(dst, dst_stride, src, width, height, src_stride, dst_x, dst_y);
			break;
		case LANDRU_VIDEO_VGA_COMPAT:
			if (g_softwareCursorEnabled && XCURSOR_Get_Display_Count() >= 0)
				VIDEO_Clear_2x_Vertical_Borders(dst, dst_stride, dst_height, width, height);
			VIDEO_Copy_Indexed_Rect_2x(dst + dst_stride * ((dst_height - 2 * landru_logical_height_gbl) >> 1),
									   dst_stride, src, width, height, src_stride, dst_x, dst_y);
			break;
		case LANDRU_VIDEO_ALTERNATE_LINES:
			VIDEO_Copy_Indexed_Rect_1x_Alternate_Lines(dst, dst_stride, src, width, height, src_stride, dst_x,
													   dst_y);
			break;
		case LANDRU_VIDEO_VGA_COMPAT | LANDRU_VIDEO_ALTERNATE_LINES:
			VIDEO_Copy_Indexed_Rect_2x_Alternate_Lines(
				dst + dst_stride * ((dst_height - 2 * landru_logical_height_gbl) >> 1), dst_stride, src,
				width, height, src_stride, dst_x, dst_y);
			break;
		case LANDRU_VIDEO_REPEAT_LINES:
		case LANDRU_VIDEO_REPEAT_LINES | LANDRU_VIDEO_ALTERNATE_LINES:
			VIDEO_Copy_Indexed_Rect_Repeat_Every_Sixth_Line(dst, dst_stride, src, width, height, src_stride,
															dst_x, dst_y);
			break;
		case LANDRU_VIDEO_VGA_COMPAT | LANDRU_VIDEO_REPEAT_LINES:
		case LANDRU_VIDEO_VGA_COMPAT | LANDRU_VIDEO_REPEAT_LINES | LANDRU_VIDEO_ALTERNATE_LINES:
			VIDEO_Copy_Indexed_Rect_2x_Repeat_Every_Third_Line(dst, dst_stride, src, width, height,
															   src_stride, dst_x, dst_y);
			break;
	}
}
