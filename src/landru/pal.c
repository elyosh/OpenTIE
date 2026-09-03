#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <landru/fourcc.h>
#include <landru/memptr.h>
#include <landru/pal.h>
#include <landru/res.h>
#include <landru/view.h>

#include "host_internal.h"

static bool s_palette_module = false;
// GLOBAL: TIE 0xD2C4C
static Palette* screen_pal_gbl = NULL;
// GLOBAL: TIE 0xD2C50
static Palette* src_pal_gbl = NULL;
// GLOBAL: TIE 0xD2C54
static Palette* dst_pal_gbl = NULL;
// GLOBAL: TIE 0xD2C48
static Palette* work_pal_gbl = NULL;
// GLOBAL: TIE 0xD2C58
static Palette* video_pal_gbl = NULL;
// GLOBAL: TIE 0xD2C44
static Palette* first_palette_gbl = NULL;

/* XPAL_Set_VGA_Palette waits for a mode-13h retrace before every changed
 * palette range. Queue those waits on one absolute 70 Hz timeline so host
 * presentation frequency cannot alter their duration. */
static uint64_t s_vga_next_retrace_us;
static uint64_t s_vga_wait_until_us;

static void lpal_schedule_vga_retrace(void) {
	uint64_t now_us = landru_host_now_us();
	if (s_vga_next_retrace_us <= now_us) {
		uint64_t elapsed_periods = (now_us - s_vga_next_retrace_us) / LANDRU_VGA_RETRACE_PERIOD_US + 1u;
		s_vga_next_retrace_us += elapsed_periods * LANDRU_VGA_RETRACE_PERIOD_US;
	}
	s_vga_wait_until_us = s_vga_next_retrace_us;
	s_vga_next_retrace_us += LANDRU_VGA_RETRACE_PERIOD_US;
}

void lpal_Set_VGA_Palette(RGBStruct* pal, int16_t start, int16_t len) {
	landru_host_palette_set((const uint8_t*)pal, (int)start, (int)len);
	lpal_schedule_vga_retrace();
}

uint64_t lpal_Next_VGA_Delay_Us(void) {
	uint64_t now_us = landru_host_now_us();
	return s_vga_wait_until_us > now_us ? s_vga_wait_until_us - now_us : UINT64_MAX;
}

void lpal_Create_Palette_Module(void) {
	int16_t ok = 1;
	s_palette_module = true;
	s_vga_next_retrace_us = landru_host_now_us() + LANDRU_VGA_RETRACE_PERIOD_US;
	s_vga_wait_until_us = 0;

	screen_pal_gbl = lpal_Alloc_Palette(0, 256);
	if (screen_pal_gbl)
		lpal_Clear_Palette(screen_pal_gbl);
	else
		ok = 0;

	src_pal_gbl = lpal_Alloc_Palette(0, 256);
	if (src_pal_gbl)
		lpal_Clear_Palette(src_pal_gbl);
	else
		ok = 0;

	dst_pal_gbl = lpal_Alloc_Palette(0, 256);
	if (dst_pal_gbl)
		lpal_Clear_Palette(dst_pal_gbl);
	else
		ok = 0;

	work_pal_gbl = lpal_Alloc_Palette(0, 256);
	if (work_pal_gbl)
		lpal_Clear_Palette(work_pal_gbl);
	else
		ok = 0;

	video_pal_gbl = lpal_Alloc_Palette(0, 256);
	if (video_pal_gbl)
		lpal_Set_Palette_RGB(video_pal_gbl, 63, 63, 63, 0, 256);
	else
		ok = 0;

	s_palette_module = ok;
}

void lpal_Destroy_Palette_Module(void) {
	s_vga_next_retrace_us = 0;
	s_vga_wait_until_us = 0;
	if (screen_pal_gbl) {
		lpal_Free_Palette(screen_pal_gbl);
		screen_pal_gbl = NULL;
	}
	if (src_pal_gbl) {
		lpal_Free_Palette(src_pal_gbl);
		src_pal_gbl = NULL;
	}
	if (dst_pal_gbl) {
		lpal_Free_Palette(dst_pal_gbl);
		dst_pal_gbl = NULL;
	}
	if (work_pal_gbl) {
		lpal_Free_Palette(work_pal_gbl);
		work_pal_gbl = NULL;
	}
	if (video_pal_gbl) {
		lpal_Free_Palette(video_pal_gbl);
		video_pal_gbl = NULL;
	}
	s_palette_module = false;
}

Palette* lpal_Ask_Palette_List(void) { return first_palette_gbl; }

void lpal_Add_Palette_To_System(Palette* pal) {
	Palette* cur = first_palette_gbl;
	Palette* tail = NULL;
	while (cur) {
		tail = cur;
		cur = cur->next;
	}
	pal->next = NULL;
	if (tail)
		tail->next = pal;
	else
		first_palette_gbl = pal;
}

void lpal_Free_Palette_From_System(Palette* pal) {
	Palette* cur = first_palette_gbl;
	Palette* prev = NULL;
	while (cur && cur != pal) {
		prev = cur;
		cur = cur->next;
	}
	if (cur == pal) {
		if (prev)
			prev->next = pal->next;
		else
			first_palette_gbl = pal->next;
		pal->next = NULL;
	}
}

Palette* lpal_Alloc_Palette(int16_t start, int16_t numColors) {
	Palette* pal = lmemptr_Alloc_System_Pointer(sizeof(Palette));
	if (!pal)
		return NULL;

	RGBStruct* colors = NULL;
	if (numColors) {
		colors = (RGBStruct*)lpal_Alloc_RGB(numColors);
		if (!colors) {
			lmemptr_Free_System_Pointer(pal);
			return NULL;
		}
	}

	pal->colors = colors;
	pal->cycle_count = 0;
	pal->cycle_active = 0;
	pal->varptr = NULL;
	pal->next = NULL;
	pal->start = start;
	pal->len = numColors;
	pal->end = start + numColors - 1;
	return pal;
}

void lpal_Free_Palette(Palette* pal) {
	if (pal->colors) {
		free(pal->colors);
	}
	if (pal->varptr) {
		lmemptr_Free_System_Pointer(pal->varptr);
	}
	lmemptr_Free_System_Pointer(pal);
}

void lpal_Free_Palettes(Palette* pal) {
	while (pal) {
		Palette* next = pal->next;
		lpal_Free_Palette_From_System(pal);
		lpal_Free_Palette(pal);
		pal = next;
	}
}

uint8_t* lpal_Alloc_RGB(int16_t numColors) { return malloc(numColors * 3); }

void lpal_Free_RGB(uint8_t* rgb) {
	if (rgb)
		free(rgb);
}

Palette* lpal_Res_Palette(const char* palName) {
	uint8_t* palBuf = lres_Load_Resource_Data(FOURCC_PLTT, palName);
	if (!palBuf)
		return NULL;

	uint8_t* p = palBuf;
	uint8_t palStart = *p++;
	uint8_t palEnd = *p++;
	int16_t numColors = palEnd - palStart + 1;

	Palette* pal = lpal_Alloc_Palette(palStart, numColors);
	if (!pal) {
		free(palBuf);
		return NULL;
	}

	for (int i = 0; i < numColors; i++) {
		pal->colors[i].r = p[0] >> 2;
		pal->colors[i].g = p[1] >> 2;
		pal->colors[i].b = p[2] >> 2;
		p += 3;
	}
	pal->cycle_count = *p++;

	if (pal->cycle_count > 4)
		pal->cycle_count = 4;

	for (int i = 0; i < pal->cycle_count; i++) {
		pal->cycles[i].rate = p[0] + p[1] * 256;
		pal->cycles[i].rate = 4915 / pal->cycles[i].rate;

		uint8_t raw_start = p[2];
		uint8_t raw_end = p[3];
		if (raw_start < raw_end) {
			pal->cycles[i].dir = 1;
			pal->cycles[i].high = raw_end;
			pal->cycles[i].low = raw_start;
		} else {
			pal->cycles[i].dir = -1;
			pal->cycles[i].high = raw_start;
			pal->cycles[i].low = raw_end;
		}
		pal->cycles[i].active = 0;
		p += 4;
	}

	free(palBuf);

	lpal_Set_Palette_Name(pal, FOURCC_PLTT, palName);
	lpal_Add_Palette_To_System(pal);
	return pal;
}

void lpal_Set_Palette_Name(Palette* pal, uint32_t res_type, const char* pal_name) {
	pal->res_type = res_type;
	int i;
	for (i = 0; i < 8 && pal_name[i]; i++)
		pal->res_name[i] = tolower(pal_name[i]);
	for (; i < 8; i++)
		pal->res_name[i] = 0;
}

void lpal_Clear_Palette(Palette* pal) { lpal_Set_Palette_RGB(pal, 0, 0, 0, pal->start, pal->len); }

void lpal_Copy_Palette(Palette* dstPal, Palette* srcPal, int16_t start, int16_t length, int16_t offset) {
	int16_t srcSlot, dstSlot, dir;

	if (start < offset) {
		srcSlot = start + length - 1;
		dstSlot = offset + length - 1;
		dir = -1;
	} else {
		srcSlot = start;
		dstSlot = offset;
		dir = 1;
	}

	RGBStruct* src = srcPal->colors;
	RGBStruct* dst = dstPal->colors;
	while (srcSlot >= start && srcSlot < length + start) {
		if (srcSlot >= srcPal->start && srcSlot <= srcPal->end && dstSlot >= dstPal->start &&
			dstSlot <= dstPal->end) {
			dst[dstSlot - dstPal->start] = src[srcSlot - srcPal->start];
		}
		srcSlot += dir;
		dstSlot += dir;
	}
}

void lpal_Set_Palette_RGB(Palette* pal, uint8_t r, uint8_t g, uint8_t b, int16_t start, int16_t length) {
	RGBStruct* dst = pal->colors;
	for (int16_t slot = start; slot < start + length; slot++) {
		if (slot >= pal->start && slot <= pal->end) {
			dst[slot - pal->start].r = r;
			dst[slot - pal->start].g = g;
			dst[slot - pal->start].b = b;
		}
	}
}

bool lpal_Compare_Palette(Palette* dstPal, Palette* srcPal, int16_t start, int16_t length, int16_t offset) {
	RGBStruct* src = srcPal->colors;
	RGBStruct* dst = dstPal->colors;

	for (int16_t srcSlot = start, dstSlot = offset; srcSlot < start + length; srcSlot++, dstSlot++) {
		if (srcSlot >= srcPal->start && srcSlot <= srcPal->end && dstSlot >= dstPal->start &&
			dstSlot <= dstPal->end) {
			RGBStruct s = src[srcSlot - srcPal->start];
			RGBStruct d = dst[dstSlot - dstPal->start];
			if (d.r != s.r || d.g != s.g || d.b != s.b)
				return false;
		}
	}
	return true;
}

bool lpal_Compare_Palette_RGB(Palette* pal, uint8_t r, uint8_t g, uint8_t b, int16_t start, int16_t count) {
	RGBStruct* data = pal->colors;
	for (int16_t i = start; i < start + count; i++) {
		if (i >= pal->start && i <= pal->end) {
			RGBStruct c = data[i - pal->start];
			if (c.r != r || c.g != g || c.b != b)
				return false;
		}
	}
	return true;
}

bool lpal_Get_Palette_Index_RGB(Palette* pal, int16_t* r, int16_t* g, int16_t* b, int16_t color_idx) {
	if (color_idx < pal->start || color_idx > pal->end) {
		*r = *g = *b = 0;
		return false;
	}
	RGBStruct c = pal->colors[color_idx - pal->start];
	*r = c.r;
	*g = c.g;
	*b = c.b;
	return true;
}

void lpal_Set_Palette_Index_RGB(Palette* pal, uint8_t r, uint8_t g, uint8_t b, int16_t color_idx) {
	if (color_idx >= pal->start && color_idx <= pal->end) {
		pal->colors[color_idx - pal->start].r = r;
		pal->colors[color_idx - pal->start].g = g;
		pal->colors[color_idx - pal->start].b = b;
	}
}

void lpal_Start_Cycle(Palette* pal) {
	if (!pal->cycle_active) {
		pal->cycle_active = 1;
		for (int i = 0; i < pal->cycle_count; i++) {
			pal->cycles[i].active = 1;
			pal->cycles[i].current = pal->cycles[i].low;
		}
	}
}

void lpal_Stop_Cycle(Palette* pal) {
	if (pal->cycle_active) {
		pal->cycle_active = 0;
		for (int i = 0; i < pal->cycle_count; i++) {
			pal->cycles[i].current = pal->cycles[i].low;
			pal->cycles[i].active = 0;
			lpal_Restore_Screen_Palette(pal->cycles[i].low, pal->cycles[i].high);
		}
	}
}

void lpal_Cycle_Screen(void) {
	Palette* curPal = first_palette_gbl;
	bool active = false;
	while (curPal) {
		active |= curPal->cycle_active;
		curPal = curPal->next;
	}

	int16_t min = 256;
	int16_t max = -1;
	int32_t time = lview_Get_View_Time();

	if (active) {
		lpal_Copy_Palette(work_pal_gbl, screen_pal_gbl, 0, 256, 0);
		curPal = first_palette_gbl;
		while (curPal) {
			if (curPal->cycle_active) {
				for (int i = 0; i < curPal->cycle_count; i++) {
					int16_t low = curPal->cycles[i].low;
					int16_t high = curPal->cycles[i].high;
					int16_t current = curPal->cycles[i].current;

					if (curPal->cycles[i].active && curPal->cycles[i].rate) {
						if ((time % curPal->cycles[i].rate) == 0) {
							if (curPal->cycles[i].dir < 0) {
								current--;
								if (current < low)
									current = high;
							} else {
								current++;
								if (current > high)
									current = low;
							}
							curPal->cycles[i].current = current;
						}

						if (min > low)
							min = low;
						if (max < high)
							max = high;
						lpal_Set_Cycle_Range(low, high, current);
					}
				}
			}
			curPal = curPal->next;
		}
		if (min < max) {
			lpal_Set_Video_Palette(work_pal_gbl, min, max - min + 1, min);
		}
	}
}

void lpal_Cycles_To_Start(void) {
	Palette* curPal = first_palette_gbl;
	while (curPal) {
		if (curPal->cycle_active) {
			for (int i = 0; i < curPal->cycle_count; i++) {
				int16_t low = curPal->cycles[i].low;
				int16_t high = curPal->cycles[i].high;
				curPal->cycles[i].current = low;
				lpal_Restore_Screen_Palette(low, high);
			}
		}
		curPal = curPal->next;
	}
}

void lpal_Stop_All_Cycles(void) {
	Palette* curPal = first_palette_gbl;
	while (curPal) {
		curPal->cycle_active = 0;
		for (int i = 0; i < curPal->cycle_count; i++) {
			curPal->cycles[i].active = 0;
		}
		curPal = curPal->next;
	}
	lpal_Restore_Screen_Palette(0, 255);
}

void lpal_Set_Screen_Palette(Palette* pal) {
	lpal_Set_Video_Palette(pal, pal->start, pal->len, pal->start);
	lpal_Copy_Palette(screen_pal_gbl, pal, pal->start, pal->len, pal->start);
}

bool lpal_Compare_Screen_Palette(Palette* pal) {
	return lpal_Compare_Palette(screen_pal_gbl, pal, pal->start, pal->len, pal->start);
}

void lpal_Set_Screen_RGB(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b) {
	int16_t len = (end - start) + 1;
	lpal_Set_Palette_RGB(screen_pal_gbl, r, g, b, start, len);
	lpal_Put_Screen_Pal_Range(start, len);
}

bool lpal_Compare_Screen_RGB(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b) {
	return lpal_Compare_Palette_RGB(screen_pal_gbl, r, g, b, start, end - start + 1);
}

void lpal_Put_Screen_Palette(void) { lpal_Put_Screen_Pal_Range(0, 256); }

void lpal_Put_Screen_Pal_Range(int16_t start, int16_t len) {
	lpal_Set_Video_Palette(screen_pal_gbl, start, len, start);
}

void lpal_Set_Video_Palette(Palette* pal, int16_t start, int16_t len, int16_t dstStart) {
	RGBStruct* src = pal->colors;
	RGBStruct* dst = video_pal_gbl->colors;

	int16_t palStart = pal->start;
	int16_t palEnd = pal->end;
	int16_t count = 0;

	RGBStruct* palVga = NULL;
	int16_t palVgaStart = 0;

	for (int16_t srcSlot = start, dstSlot = dstStart; srcSlot < start + len; srcSlot++, dstSlot++) {
		if (srcSlot >= palStart && srcSlot <= palEnd) {
			RGBStruct s = src[srcSlot - palStart];
			if (s.r != dst[dstSlot].r || s.g != dst[dstSlot].g || s.b != dst[dstSlot].b) {
				if (!count) {
					palVga = &src[srcSlot - palStart];
					palVgaStart = dstSlot;
				}
				dst[dstSlot] = s;
				count++;
			} else {
				if (count)
					count++;
			}
		}
	}
	if (count) {
		lpal_Set_VGA_Palette(palVga, palVgaStart, count);
	}
}

void lpal_Set_Cycle_Range(int16_t start, int16_t stop, int16_t offset) {
	int16_t len = (stop - offset) + 1;
	lpal_Copy_Palette(work_pal_gbl, screen_pal_gbl, start, len, offset);
	if ((offset - start) != 0) {
		lpal_Copy_Palette(work_pal_gbl, screen_pal_gbl, start + len, offset - start, start);
	}
}

void lpal_Restore_Screen_Palette(int16_t start, int16_t stop) {
	lpal_Set_Video_Palette(screen_pal_gbl, start, (stop - start) + 1, start);
}

Palette* lpal_Get_Screen_Palette(void) { return screen_pal_gbl; }
Palette* lpal_Get_Source_Palette(void) { return src_pal_gbl; }
Palette* lpal_Get_Dest_Palette(void) { return dst_pal_gbl; }
Palette* lpal_Get_Work_Palette(void) { return work_pal_gbl; }

void lpal_Screen_To_Src_Palette(int16_t offset, int16_t start, int16_t stop) {
	lpal_Copy_Palette(src_pal_gbl, screen_pal_gbl, start, stop - start + 1, offset);
}

void lpal_Src_To_Screen_Palette(int16_t offset, int16_t start, int16_t stop) {
	lpal_Copy_Palette(screen_pal_gbl, src_pal_gbl, start, stop - start + 1, offset);
}

void lpal_Screen_To_Dest_Palette(int16_t offset, int16_t start, int16_t stop) {
	lpal_Copy_Palette(dst_pal_gbl, screen_pal_gbl, start, stop - start + 1, offset);
}

void lpal_Dest_To_Screen_Palette(int16_t offset, int16_t start, int16_t stop) {
	lpal_Copy_Palette(screen_pal_gbl, dst_pal_gbl, start, stop - start + 1, offset);
}

void lpal_Dest_To_Src_Palette(int16_t offset, int16_t start, int16_t stop) {
	lpal_Copy_Palette(src_pal_gbl, dst_pal_gbl, start, stop - start + 1, offset);
}

void lpal_Src_To_Dest_Palette(int16_t offset, int16_t start, int16_t stop) {
	lpal_Copy_Palette(dst_pal_gbl, src_pal_gbl, start, stop - start + 1, offset);
}

void lpal_Set_Source_Palette(Palette* pal) {
	lpal_Copy_Palette(src_pal_gbl, pal, pal->start, pal->len, pal->start);
}

bool lpal_Compare_Source_Palette(Palette* pal) {
	return lpal_Compare_Palette(src_pal_gbl, pal, pal->start, pal->len, pal->start);
}

void lpal_Set_Src_Pal_Color(int16_t start, int16_t stop, uint8_t r, uint8_t g, uint8_t b) {
	lpal_Set_Palette_RGB(src_pal_gbl, r, g, b, start, stop - start + 1);
}

bool lpal_Compare_Src_Pal_Color(int16_t start, int16_t stop, uint8_t r, uint8_t g, uint8_t b) {
	return lpal_Compare_Palette_RGB(src_pal_gbl, r, g, b, start, stop - start + 1);
}

void lpal_Set_Dest_Palette(Palette* pal) {
	lpal_Copy_Palette(dst_pal_gbl, pal, pal->start, pal->len, pal->start);
}

bool lpal_Compare_Dest_Palette(Palette* pal) {
	return lpal_Compare_Palette(dst_pal_gbl, pal, pal->start, pal->len, pal->start);
}

void lpal_Set_Dest_Pal_Color(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b) {
	lpal_Set_Palette_RGB(dst_pal_gbl, r, g, b, start, end - start + 1);
}

bool lpal_Compare_Dest_Pal_Color(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b) {
	return lpal_Compare_Palette_RGB(dst_pal_gbl, r, g, b, start, end - start + 1);
}

bool lpal_Compare_Src_Dest_Palette(void) {
	return lpal_Compare_Palette(src_pal_gbl, dst_pal_gbl, src_pal_gbl->start, src_pal_gbl->len,
								src_pal_gbl->start);
}

void lpal_Build_Fade_Palette(int16_t type, int16_t start, int16_t stop, int16_t step, int16_t fr, int16_t fg,
							 int16_t fb) {
	switch (type) {
		case 0:
			lpal_Build_Palette_To_Palette(start, stop, step);
			break;
		case 1:
			lpal_Build_Mono_To_Palette(start, stop, step, fr, fg, fb);
			break;
		case 2:
			lpal_Build_Palette_To_Mono(start, stop, step, fr, fg, fb);
			break;
		default:
			break;
	}
}

void lpal_Build_Palette_To_Palette(int16_t start, int16_t stop, int16_t step) {
	RGBStruct* src1 = src_pal_gbl->colors;
	RGBStruct* src2 = dst_pal_gbl->colors;
	RGBStruct* dst = screen_pal_gbl->colors;

	for (int32_t slot = start; slot <= stop; slot++) {
		int32_t dr = (int32_t)src2[slot].r - (int32_t)src1[slot].r;
		int32_t dg = (int32_t)src2[slot].g - (int32_t)src1[slot].g;
		int32_t db = (int32_t)src2[slot].b - (int32_t)src1[slot].b;

		dst[slot].r = src1[slot].r + (dr * step) / 256;
		dst[slot].g = src1[slot].g + (dg * step) / 256;
		dst[slot].b = src1[slot].b + (db * step) / 256;
	}
}

void lpal_Build_Palette_To_Mono(int16_t start, int16_t stop, int16_t step, int16_t fr, int16_t fg,
								int16_t fb) {
	RGBStruct* src = src_pal_gbl->colors;
	RGBStruct* dst = screen_pal_gbl->colors;

	for (int32_t slot = start; slot <= stop; slot++) {
		int32_t dr = (int32_t)fr - (int32_t)src[slot].r;
		int32_t dg = (int32_t)fg - (int32_t)src[slot].g;
		int32_t db = (int32_t)fb - (int32_t)src[slot].b;

		dst[slot].r = src[slot].r + (dr * step) / 256;
		dst[slot].g = src[slot].g + (dg * step) / 256;
		dst[slot].b = src[slot].b + (db * step) / 256;
	}
}

void lpal_Build_Mono_To_Palette(int16_t start, int16_t stop, int16_t step, int16_t fr, int16_t fg,
								int16_t fb) {
	RGBStruct* src = dst_pal_gbl->colors;
	RGBStruct* dst = screen_pal_gbl->colors;

	for (int32_t slot = start; slot <= stop; slot++) {
		int32_t dr = (int32_t)src[slot].r - (int32_t)fr;
		int32_t dg = (int32_t)src[slot].g - (int32_t)fg;
		int32_t db = (int32_t)src[slot].b - (int32_t)fb;

		dst[slot].r = fr + (dr * step) / 256;
		dst[slot].g = fg + (dg * step) / 256;
		dst[slot].b = fb + (db * step) / 256;
	}
}
