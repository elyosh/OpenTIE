#include "tie/credits_internal.h"

#include <landru/actdelt.h>
#include <landru/canvas.h>
#include <landru/dirty.h>
#include <landru/font.h>
#include <landru/paint.h>
#include <landru/paragrp.h>
#include <landru/rect.h>

// FUNCTION: TIE98 0x4148A0
void credits_Init_Credit_Info_tie98(void) {
	xrect_Set_Rect(&credits_dirty_rect, 20, 40, 300, 160);
	credits_num_credit_lines = xparagrp_Count_Paragraphs(credits_text);
	credits_text_len = 130;
	credits_film_time = 0;
	credits_film_len = 90 * (credits_num_credit_lines - 1) + 138;
}

// FUNCTION: TIE98 0x414900
int16_t credits_draw_Credit_tie98(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								  int16_t refresh) {
	int16_t time_offset;
	int16_t credit_idx = 0;
	int16_t base_y;

	(void)actor;
	(void)xoff;
	(void)yoff;

	if (!refresh)
		return 1;

	/* Film ended: black screen */
	if (credits_film_len <= credits_film_time) {
		Rect r;
		xrect_Set_Rect(&r, 0, 0, 320, 200);
		xpaint_Paint_Clipped_Rect(&r, 0);
		xcanvas_Invalid_Screen_Diff();
		xdirty_Max_Dirty_List();
		return 1;
	}

	/* Draw tiled star background */
	credits_Credit_Stars_To_Back();

	time_offset = credits_film_time;
	base_y = credits_film_time + 96;

	/* Walk through each credit paragraph */
	while (credit_idx < credits_num_credit_lines) {
		if (time_offset < 0)
			break;

		if (time_offset < credits_text_len) {
			/* This paragraph is visible */
			int16_t color;
			int16_t text_y;
			int16_t num_strings;
			int16_t i;
			Rect text_rect;

			if (time_offset < 60) {
				/* Fade-in phase */
				color = base_y;
				text_y = 140 - time_offset;
			} else {
				int16_t hold_time = time_offset - 60;
				if (hold_time < 40) {
					/* Hold phase */
					text_y = 80;
					if (hold_time >= 31)
						color = time_offset - 60 + 167;
					else
						color = base_y;
				} else {
					/* Fade-out phase */
					color = time_offset - 100 + 207;
					text_y = 80 - (time_offset - 100);
					if (color > 239)
						color = 239;
				}
			}

			num_strings = xparagrp_Count_Paragraph_Strings(credits_text, credit_idx);

			/* Draw red/blue bar decorations during hold phase */
			if (time_offset >= 45) {
				int16_t bar_width;

				if (time_offset - 45 >= 35) {
					if (time_offset - 80 < 0) {
						bar_width = 280;
					} else {
						bar_width = 8 * (time_offset - 80 + 35);
					}
				} else {
					bar_width = 8 * (time_offset - 45);
				}

				if (bar_width != -1) {
					Rect saved_clip;
					Rect bar_clip;
					xactdelt_Draw_Delta_Actor(credits_red_bar, bounds, clip, bar_width - 240, 79, refresh);

					xcanvas_Get_Drawing_Canvas_Clip(&saved_clip);

					xrect_Set_Rect(&bar_clip, 0, 0, 320, 10 * (num_strings - 1) + 93);
					xrect_Clip_Rect(&bar_clip, clip);
					xcanvas_Set_Drawing_Canvas_Clip(&bar_clip);

					xactdelt_Draw_Delta_Actor(credits_blue_bar, &bar_clip, &bar_clip, 320 - bar_width, 91,
											  refresh);

					xcanvas_Set_Drawing_Canvas_Clip(&saved_clip);
				}
			}

			/* Draw text lines */
			xrect_Set_Rect(&text_rect, 0, text_y, 320, text_y + 10);

			for (i = 0; i < num_strings; i++) {
				char line_buf[80];
				xparagrp_Get_Paragraph_String(credits_text, line_buf, credit_idx, i);
				xfont_Print_Centered_Text(line_buf, &text_rect, color, 0);

				xrect_Offset_Rect(&text_rect, 0, i ? 10 : 12);
			}
		}

		/* Advance to next paragraph */
		base_y -= 90;
		time_offset -= 90;
		credit_idx++;
	}

	xdirty_Max_Dirty_List();
	return 1;
}
