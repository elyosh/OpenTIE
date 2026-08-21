#include <stdlib.h>
#include <string.h>

#include <landru/font.h>
#include <landru/fourcc.h>
#include <landru/paragrp.h>
#include <landru/res.h>

/*
 * TEXT resource bundle format:
 *   u16 paragraph_count
 *   For each paragraph:
 *     u16 size  (byte count of the string data that follows)
 *     <size> bytes of payload: consecutive 0-terminated strings,
 *       ended by a double-0 sentinel (empty string)
 */

/* Walk past para_idx paragraphs, returning a pointer to the target paragraph's
   size header. buf points to the start of the bundle (the u16 count). */
static uint8_t* walk_to_paragraph(uint8_t* buf, int16_t para_idx) {
	uint8_t* p = buf + 2; /* skip u16 paragraph_count */
	for (int16_t i = 0; i < para_idx; i++) {
		uint16_t para_size = *(uint16_t*)p;
		p += 2 + para_size; /* skip u16 size header + payload */
	}
	return p;
}

/* Walk past str_idx strings within a paragraph's payload.
   payload points to the first byte after the u16 size header. */
static uint8_t* walk_to_string(uint8_t* payload, int16_t str_idx) {
	uint8_t* p = payload;
	for (int16_t j = 0; j < str_idx; j++) {
		if (*p == 0)
			break;
		while (*p)
			p++;
		p++; /* skip the 0 terminator */
	}
	return p;
}

void* lparagrp_Res_Paragraph(ResFile* rf, const char* res_name) {
	int offset;
	uint32_t size;
	if (lres_Get_Resource_Offset(rf, FOURCC_TEXT, res_name, &offset, &size)) {
		return lres_Load_Resource_Data(FOURCC_TEXT, res_name);
	}
	return NULL;
}

void lparagrp_Free_Paragraph(void* paragraph) { free(paragraph); }

void lparagrp_Get_Paragraph_String(void* text, char* dst, int16_t para_idx, int16_t str_idx) {
	uint8_t* p = walk_to_paragraph(text, para_idx);
	p += 2; /* skip u16 size header → payload */
	p = walk_to_string(p, str_idx);

	/* Copy the target string including its null terminator */
	do {
		*dst++ = *p;
	} while (*p++);
}

int16_t lparagrp_Count_Paragraphs(void* text) { return *(uint16_t*)text; }

int16_t lparagrp_Count_Paragraph_Strings(void* text, int16_t para_idx) {
	uint8_t* p = walk_to_paragraph(text, para_idx);
	p += 2; /* skip u16 size header → payload */

	int16_t count = 0;
	if (*p) {
		do {
			while (*p++)
				;
			count++;
		} while (*p);
	}
	return count;
}

int16_t lparagrp_Draw_Paragraph(void* text, Rect* clip, int16_t x, int16_t y, int16_t font_id, int16_t color,
								int16_t para_idx) {
	uint8_t* p = walk_to_paragraph(text, para_idx);
	p += 2; /* skip u16 size header → payload */

	int16_t saved_font = lfont_Get_Font();
	lfont_Set_Font(font_id);

	int16_t char_gap, line_gap;
	lfont_Get_FontID_Spacing(font_id, &char_gap, &line_gap);
	int16_t font_height = lfont_Get_FontID_Height(font_id);

	while (*p) {
		if (y + font_height + 1 > clip->top && y < clip->bottom) {
			lfont_Print_Clipped_Text((const char*)p, x, y, font_id, color);
		}
		p += strlen((const char*)p) + 1;
		y += line_gap + font_height;
	}

	lfont_Set_Font(saved_font);
	return 0;
}

int16_t lparagrp_Get_Paragraph_Size(void* text, int16_t font_id, int16_t para_idx, int16_t* out_max_width,
									int16_t* out_total_height) {
	uint8_t* p = walk_to_paragraph(text, para_idx);
	p += 2; /* skip u16 size header → payload */

	int16_t saved_font = lfont_Get_Font();
	lfont_Set_Font(font_id);

	int16_t char_gap, line_gap;
	lfont_Get_FontID_Spacing(font_id, &char_gap, &line_gap);
	int16_t font_height = lfont_Get_FontID_Height(font_id);

	int16_t max_width = 0;
	int16_t total_height = 0;

	while (*p) {
		int16_t w = lfont_Get_String_Width((const char*)p);
		if (w > max_width)
			max_width = w;
		p += strlen((const char*)p) + 1;
		total_height += line_gap + font_height;
	}

	total_height -= line_gap; /* no trailing gap after last line */

	*out_max_width = max_width;
	*out_total_height = total_height;

	lfont_Set_Font(saved_font);
	return 0;
}

char* lparagrp_Find_Paragraph(char* buf, int16_t para_idx) {
	char* p = buf + 2;
	for (int16_t i = 0; i < para_idx; i++) {
		p += *(int16_t*)p + 2;
	}
	return p + 2; /* skip size header → payload */
}

char* lparagrp_Find_Paragraph_String(char* buf, int16_t para_idx, int16_t str_idx) {
	char* p = lparagrp_Find_Paragraph(buf, para_idx);
	for (int16_t j = 0; j < str_idx; j++) {
		if (!*p)
			break;
		while (*p)
			p++;
		p++;
	}
	return p;
}
