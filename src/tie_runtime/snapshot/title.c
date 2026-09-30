#include "tie_runtime/snapshot/title.h"
#include "tie/title.h"

#include <landru/paragrp.h>
#include <stdio.h>
#include <string.h>

static int16_t initial_y[TITLE_MAX_LINES];

void TieTitleSnapshot_Reset(void) { memset(initial_y, 0, sizeof initial_y); }

void TieTitleSnapshot_SetOrigin(int index, int16_t y) {
	if ((unsigned)index < TITLE_MAX_LINES)
		initial_y[index] = y;
}

int TieTitleSnapshot_LineCount(void) { return title_text && title_num_lines > 0 ? title_num_lines : 0; }

bool TieTitleSnapshot_ReadLine(int index, char* text, size_t capacity, float* origin) {
	if (!title_text || !text || !capacity || !origin || index < 0 || index >= title_num_lines ||
		index >= TITLE_MAX_LINES)
		return false;
	char* data = xmemhdl_Lock_Handle(title_text);
	if (!data)
		return false;
	const char* line = xparagrp_Find_Paragraph_String(data, 0, (int16_t)index);
	snprintf(text, capacity, "%s", line);
	xmemhdl_Unlock_Handle(title_text);
	*origin = (float)initial_y[index];
	return true;
}
