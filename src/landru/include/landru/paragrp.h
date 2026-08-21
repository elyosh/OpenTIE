#ifndef LANDRU_PARAGRP_H
#define LANDRU_PARAGRP_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>
#include <landru/res.h>

void* lparagrp_Res_Paragraph(ResFile* rf, const char* res_name);
void lparagrp_Free_Paragraph(void* paragraph);
void lparagrp_Get_Paragraph_String(void* text, char* dst, int16_t para_idx, int16_t str_idx);
int16_t lparagrp_Draw_Paragraph(void* text, Rect* clip, int16_t x, int16_t y, int16_t font_id, int16_t color,
								int16_t para_idx);
int16_t lparagrp_Count_Paragraphs(void* text);
int16_t lparagrp_Count_Paragraph_Strings(void* text, int16_t para_idx);
int16_t lparagrp_Get_Paragraph_Size(void* text, int16_t font_id, int16_t para_idx, int16_t* out_max_width,
									int16_t* out_total_height);
char* lparagrp_Find_Paragraph(char* buf, int16_t para_idx);
char* lparagrp_Find_Paragraph_String(char* buf, int16_t para_idx, int16_t str_idx);

#endif
