#include <stddef.h>

#include <landru/btnsldr.h>
#include <landru/dirty.h>
#include <landru/inpattr.h>
#include <landru/paint.h>
#include <landru/rect.h>
#include <landru/style.h>

#include "binio.h"
#include <landru/fourcc.h>

static bool slider_button_module_gbl;

void lbtnsldr_Create_Slider_Button_Module(void) {
	linput_Create_Input_Type(FOURCC_SLDR, (void*)lbtnsldr_Generate_Slider_Button, NULL);
	slider_button_module_gbl = true;
}

void lbtnsldr_Destroy_Slider_Button_Module(void) {
	if (slider_button_module_gbl) {
		linput_Destroy_Input_Type(FOURCC_SLDR);
		slider_button_module_gbl = false;
	}
}

SliderButton* lbtnsldr_Generate_Slider_Button(Input* parent, char* res_buffer, const char* name) {
	(void)name;
	InputResourceHeader hdr;
	InputResourceHeader_decode(&hdr, (const uint8_t*)res_buffer);

	SliderButton* btn = lbtnsldr_Alloc_Slider_Button(parent, &hdr.frame, hdr.zinput, NULL, 4, hdr.id);
	if (!btn)
		return NULL;

	btn->header.alignment = hdr.window_flags;
	btn->header.flags = hdr.state_flags | INPUT_REFRESH;
	btn->header.var1 = hdr.exit_pending;
	btn->header.var2 = hdr.exit_code;

	const uint8_t* ext = (const uint8_t*)res_buffer + INPUTRESOURCEHEADER_DISK_SIZE;
	if (br_i16le(ext + 0)) {
		btn->range_min = br_i16le(ext + 2);
		btn->range_max = br_i16le(ext + 4);
		btn->repeat_counter = ((const uint8_t*)res_buffer)[33];
		btn->page_step = ((const uint8_t*)res_buffer)[34];
	}

	return btn;
}

SliderButton* lbtnsldr_Alloc_Slider_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
										   uint8_t page_step, int16_t id) {
	SliderButton* btn =
		(SliderButton*)linput_Alloc_Dialog_Input(parent, frame, zinput, sizeof(SliderButton) - sizeof(Input));
	if (!btn)
		return NULL;

	linpattr_Set_Input_Draw_Function(&btn->header, lbtnsldr_idraw_Slider_Button);
	linpattr_Set_Input_Update_Function(&btn->header, lbtnsldr_iupdate_Slider_Button);
	linpattr_Set_Input_User_Function(&btn->header, callback);
	btn->header.type = FOURCC_SLDR;
	btn->header.mouseUsage = downMoveUpInput;
	btn->header.id = id;

	btn->drag_offset = 0;
	btn->range_min = 0;
	btn->range_max = 0;
	btn->value = 0;
	btn->repeat_counter = 0;
	btn->action_mode = 0;

	int16_t w = frame->right - frame->left;
	int16_t h = frame->bottom - frame->top;
	btn->is_vertical = (w < h);
	btn->page_step = page_step;

	return btn;
}

void lbtnsldr_Get_Slider_Button_Range(SliderButton* btn, int16_t* out_min, int16_t* out_max) {
	*out_min = btn->range_min;
	*out_max = btn->range_max;
}

void lbtnsldr_Set_Slider_Button_Range(SliderButton* btn, int16_t min, int16_t max) {
	btn->range_min = min;
	btn->range_max = max;
	if (btn->value < btn->range_min)
		btn->value = btn->range_min;
	if (btn->value > btn->range_max)
		btn->value = btn->range_max;
	linpattr_Refresh_Input(&btn->header);
}

int16_t lbtnsldr_Get_Slider_Button_Value(SliderButton* btn) { return btn->value; }

void lbtnsldr_Set_Slider_Button_Value(SliderButton* btn, int16_t value) {
	if (value < btn->range_min)
		value = btn->range_min;
	if (value > btn->range_max)
		value = btn->range_max;
	btn->value = value;
	linpattr_Refresh_Input(&btn->header);
}

void lbtnsldr_Calc_Slider_Button_Rects(SliderButton* btn, Rect* frame, Rect* out_dec, Rect* out_inc,
									   Rect* out_track, Rect* out_thumb) {
	int16_t w = frame->right - frame->left;
	int16_t h = frame->bottom - frame->top;
	btn->is_vertical = (w < h);

	int16_t track_len;
	if (btn->is_vertical) {
		lrect_Set_Rect(out_dec, frame->left, frame->top, frame->right, frame->top + w);
		lrect_Set_Rect(out_inc, frame->left, frame->bottom - w, frame->right, frame->bottom);
		lrect_Set_Rect(out_track, frame->left, frame->top + w, frame->right, frame->bottom - w);
		track_len = out_track->bottom - out_track->top;
	} else {
		lrect_Set_Rect(out_dec, frame->left, frame->top, frame->left + h, frame->bottom);
		lrect_Set_Rect(out_inc, frame->right - h, frame->top, frame->right, frame->bottom);
		lrect_Set_Rect(out_track, frame->left + h, frame->top, frame->right - h, frame->bottom);
		track_len = out_track->right - out_track->left;
	}

	int16_t range = btn->range_max - btn->range_min;
	int16_t thumb_size = track_len - 4 - range;
	int16_t usable_len = range;
	if (thumb_size < 6) {
		usable_len -= (6 - thumb_size);
		thumb_size = 6;
	}

	int16_t range_size = btn->range_max - btn->range_min;
	int16_t thumb_offset;
	if (range_size)
		thumb_offset = (btn->value - btn->range_min) * usable_len / range_size + 2;
	else
		thumb_offset = 2;

	if (btn->is_vertical)
		lrect_Set_Rect(out_thumb, out_track->left + 2, out_track->top + thumb_offset, out_track->right - 2,
					   out_track->top + thumb_offset + thumb_size);
	else
		lrect_Set_Rect(out_thumb, out_track->left + thumb_offset, out_track->top + 2,
					   out_track->left + thumb_offset + thumb_size, out_track->bottom - 2);
}

int16_t lbtnsldr_Calc_Slider_Thumb_Value(SliderButton* btn, Rect* track_rect, Rect* thumb_rect,
										 int16_t mouse_x, int16_t mouse_y) {
	int16_t track_start, track_end;
	if (btn->is_vertical) {
		track_start = track_rect->top + 2;
		track_end = track_rect->bottom - (thumb_rect->bottom - thumb_rect->top);
	} else {
		track_start = track_rect->left + 2;
		track_end = track_rect->right - (thumb_rect->right - thumb_rect->left);
		mouse_y = mouse_x;
	}

	int16_t rel_pos = mouse_y - btn->drag_offset - track_start;
	int16_t usable_len = track_end - 2 - track_start;
	if (rel_pos < 0)
		rel_pos = 0;
	if (rel_pos > usable_len)
		rel_pos = usable_len;

	int16_t range_size = btn->range_max - btn->range_min;
	return btn->range_min + range_size * rel_pos / usable_len;
}

void lbtnsldr_idraw_Slider_Button(Input* input, Rect* paint_rect, Rect* dirty_rect, int16_t refresh) {
	SliderButton* btn = (SliderButton*)input;
	if (!refresh)
		return;

	Rect dec_rect, inc_rect, track_rect, thumb_rect;
	lbtnsldr_Calc_Slider_Button_Rects(btn, paint_rect, &dec_rect, &inc_rect, &track_rect, &thumb_rect);

	lstyle_Style_Paint_Button(&dec_rect, btn->action_mode == 1);
	lstyle_Style_Paint_Button(&inc_rect, btn->action_mode == 2);

	int16_t base_color = lstyle_Get_Style_Base_Color();
	lpaint_Paint_Clipped_Rect(&track_rect, base_color);

	if (btn->range_min < btn->range_max)
		lstyle_Style_Paint_Button(&thumb_rect, btn->action_mode == 3);

	if (linpattr_Is_Input_Dirty(&btn->header))
		ldirty_Dirty_Rect(dirty_rect);
}

int16_t lbtnsldr_iupdate_Slider_Button(Input* input, Rect* frame, Rect* clip, int16_t phase, uint8_t mouse_l,
									   uint8_t mouse_r, int16_t x, int16_t y) {
	SliderButton* btn = (SliderButton*)input;
	(void)clip;
	if (phase)
		return 0;

	Rect dec_rect, inc_rect, track_rect, thumb_rect;
	lbtnsldr_Calc_Slider_Button_Rects(btn, frame, &dec_rect, &inc_rect, &track_rect, &thumb_rect);

	int16_t mx = frame->left + x;
	int16_t my = frame->top + y;

	/* Retail initialises v27=1 and only writes v27=0 on the
	 * range_max<=range_min reject path; every other return path
	 * yields v27 (= 1). XINPCALL swallows release events when the
	 * iupdate handler returns 0, so this is load-bearing. */
	int16_t result = 1;

	/* Mouse down */
	if (mouse_l == 1 || mouse_r == 1) {
		btn->action_mode = 0;
		btn->repeat_counter = 0;

		if (lrect_Point_In_Rect(&dec_rect, mx, my)) {
			btn->action_mode = 1;
		} else if (lrect_Point_In_Rect(&inc_rect, mx, my)) {
			btn->action_mode = 2;
		} else if (btn->range_max <= btn->range_min) {
			/* range empty — refuse the click */
			result = 0;
		} else if (lrect_Point_In_Rect(&thumb_rect, mx, my)) {
			btn->action_mode = 3;
			if (btn->is_vertical)
				btn->drag_offset = my - frame->top - thumb_rect.top;
			else
				btn->drag_offset = mx - frame->left - thumb_rect.left;
		} else {
			/* Click on track outside thumb → page jump */
			int16_t page = 0;
			if (btn->is_vertical) {
				if (my > dec_rect.bottom && my < thumb_rect.top)
					page = -btn->page_step;
				if (my > thumb_rect.bottom && my < inc_rect.top)
					page = btn->page_step;
			} else {
				if (mx > dec_rect.right && mx < thumb_rect.left)
					page = -btn->page_step;
				if (mx > thumb_rect.right && mx < inc_rect.left)
					page = btn->page_step;
			}
			if (page) {
				lbtnsldr_Set_Slider_Button_Value(btn, btn->value + page);
				linpattr_Selected_Input(&btn->header);
				btn->action_mode = 4;
			}
		}
		linpattr_Refresh_Input(&btn->header);
		return result;
	}

	/* Mouse up */
	if (mouse_l == 3 || mouse_r == 3) {
		if (btn->action_mode == 1 && btn->repeat_counter <= 4) {
			lbtnsldr_Set_Slider_Button_Value(btn, btn->value - 1);
			linpattr_Selected_Input(&btn->header);
		} else if (btn->action_mode == 2 && btn->repeat_counter <= 4) {
			lbtnsldr_Set_Slider_Button_Value(btn, btn->value + 1);
			linpattr_Selected_Input(&btn->header);
		}
		btn->action_mode = 0;
		linpattr_Refresh_Input(&btn->header);
		return result;
	}

	/* Drag / auto-repeat */
	switch (btn->action_mode) {
		case 1: /* Dec held */
			if (++btn->repeat_counter > 4) {
				lbtnsldr_Set_Slider_Button_Value(btn, btn->value - 1);
				linpattr_Selected_Input(&btn->header);
			}
			break;

		case 2: /* Inc held */
			if (++btn->repeat_counter > 4) {
				lbtnsldr_Set_Slider_Button_Value(btn, btn->value + 1);
				linpattr_Selected_Input(&btn->header);
			}
			break;

		case 3: { /* Thumb drag */
			int16_t new_val = lbtnsldr_Calc_Slider_Thumb_Value(btn, &track_rect, &thumb_rect,
															   mx - frame->left, my - frame->top);
			if (new_val != btn->value) {
				lbtnsldr_Set_Slider_Button_Value(btn, new_val);
				linpattr_Selected_Input(&btn->header);
			}
			break;
		}

		case 4: { /* Page-jump held */
			if (++btn->repeat_counter <= 4)
				break;
			int16_t page = 0;
			if (btn->is_vertical) {
				if (my > dec_rect.bottom && my < thumb_rect.top)
					page = -btn->page_step;
				if (my > thumb_rect.bottom && my < inc_rect.top)
					page = btn->page_step;
			} else {
				if (mx > dec_rect.right && mx < thumb_rect.left)
					page = -btn->page_step;
				if (mx > thumb_rect.right && mx < inc_rect.left)
					page = btn->page_step;
			}
			if (page) {
				lbtnsldr_Set_Slider_Button_Value(btn, btn->value + page);
				linpattr_Selected_Input(&btn->header);
			}
			break;
		}

		default:
			break;
	}

	linpattr_Refresh_Input(&btn->header);
	return result;
}
