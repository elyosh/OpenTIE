#include <stddef.h>

#include <landru/actanim.h>
#include <landru/actdelt.h>
#include <landru/actor.h>
#include <landru/actraw.h>
#include <landru/font.h>
#include <landru/fourcc.h>
#include <landru/paint.h>
#include <landru/rect.h>
#include <landru/remap.h>
#include <landru/style.h>

// GLOBAL: TIE 0xD2F38
static Actor* style_actor_gbl;

int16_t lstyle_Get_Style_Base_Color(void) { return lremap_Get_Remap(REMAP_GRAY_3); }

int16_t lstyle_Get_Style_Up_Color(void) { return lremap_Get_Remap(REMAP_GRAY_2); }

int16_t lstyle_Get_Style_Down_Color(void) { return lremap_Get_Remap(REMAP_GRAY_4); }

/* REMAP_GRAY_N is a light-to-dark ramp where GRAY_1 is darkest (42/255) and
 * GRAY_5 is lightest (210/255). The Paint_* primitives below pass shadow=dark
 * and highlight=light so the underlying bevel builder produces the expected
 * raised-button look (light top, dark verticals+bottom when pressed=0).
 *
 * This matches the retail XSTYLE bindings verified at 0xaa194 (Paint_Base),
 * 0xaa210 (Paint_Alert_Base), 0xaa28c (Paint_Button), 0xaa344 (Paint_Lit_Button),
 * 0xaa3c0 (Paint_Border), 0xaa420 (Paint_TextField). */

int16_t lstyle_Style_Paint_Base(Rect* r, int16_t pressed) {
	int16_t fill = lremap_Get_Remap(REMAP_GRAY_3);
	int16_t inner_hi = lremap_Get_Remap(REMAP_GRAY_4);
	int16_t outer_hi = lremap_Get_Remap(REMAP_GRAY_5);
	int16_t inner_sh = lremap_Get_Remap(REMAP_GRAY_2);
	int16_t outer_sh = lremap_Get_Remap(REMAP_GRAY_1);
	return lpaint_Paint_Clipped_DBevel(r, outer_sh, inner_sh, outer_hi, inner_hi, fill, pressed);
}

int16_t lstyle_Style_Paint_Alert_Base(Rect* r, int16_t pressed) {
	int16_t fill = lremap_Get_Remap(REMAP_BLUE_3);
	int16_t inner_hi = lremap_Get_Remap(REMAP_BLUE_4);
	int16_t outer_hi = lremap_Get_Remap(REMAP_BLUE_5);
	int16_t inner_sh = lremap_Get_Remap(REMAP_BLUE_2);
	int16_t outer_sh = lremap_Get_Remap(REMAP_BLUE_1);
	return lpaint_Paint_Clipped_DBevel(r, outer_sh, inner_sh, outer_hi, inner_hi, fill, pressed);
}

void lstyle_Style_Paint_Button(Rect* r, int16_t pressed) {
	int16_t border = lremap_Get_Remap(REMAP_GRAY_0);
	int16_t fill = lremap_Get_Remap(pressed ? REMAP_GRAY_2 : REMAP_GRAY_3);
	lpaint_Frame_Clipped_Rect(r, border);
	lrect_Inset_Rect(r, 1, 1);
	int16_t inner_hi = lremap_Get_Remap(REMAP_GRAY_4);
	int16_t outer_hi = lremap_Get_Remap(REMAP_GRAY_5);
	int16_t inner_sh = lremap_Get_Remap(REMAP_GRAY_2);
	int16_t outer_sh = lremap_Get_Remap(REMAP_GRAY_1);
	lpaint_Paint_Clipped_DBevel(r, outer_sh, inner_sh, outer_hi, inner_hi, fill, pressed);
	lrect_Inset_Rect(r, -1, -1);
}

int16_t lstyle_Style_Paint_Lit_Button(Rect* r, int16_t pressed) {
	int16_t fill = lremap_Get_Remap(REMAP_YELLOW_6);
	int16_t inner_hi = lremap_Get_Remap(REMAP_GRAY_4);
	int16_t outer_hi = lremap_Get_Remap(REMAP_GRAY_5);
	int16_t inner_sh = lremap_Get_Remap(REMAP_GRAY_2);
	int16_t outer_sh = lremap_Get_Remap(REMAP_GRAY_1);
	return lpaint_Paint_Clipped_DBevel(r, outer_sh, inner_sh, outer_hi, inner_hi, fill, pressed);
}

int16_t lstyle_Style_Paint_Border(Rect* r, int16_t pressed) {
	int16_t base = lremap_Get_Remap(REMAP_GRAY_3);
	int16_t shadow = lremap_Get_Remap(REMAP_GRAY_2);
	int16_t highlight = lremap_Get_Remap(REMAP_GRAY_4);
	return lpaint_Paint_Clipped_Bevel(r, shadow, highlight, base, pressed);
}

int16_t lstyle_Style_Paint_TextField(Rect* r) {
	int16_t fill = lremap_Get_Remap(REMAP_GRAY_0);
	int16_t shadow = lremap_Get_Remap(REMAP_GRAY_2);
	int16_t highlight = lremap_Get_Remap(REMAP_GRAY_4);
	return lpaint_Paint_Clipped_Bevel(r, shadow, highlight, fill, 1);
}

void lstyle_Style_Button_Text(const char* str, Rect* r, int16_t pressed) {
	Rect ra = *r;
	int16_t color;
	if (pressed) {
		color = lremap_Get_Remap(REMAP_GRAY_4);
		ra.right -= 2;
		ra.bottom -= 2;
	} else {
		color = lremap_Get_Remap(REMAP_GRAY_1);
	}
	lfont_Print_Centered_Text(str, &ra, color, 0);
}

void lstyle_Style_Small_Button_Text(const char* str, Rect* r, int16_t pressed) {
	Rect ra = *r;
	int16_t color = lremap_Get_Remap(pressed ? REMAP_GRAY_4 : REMAP_GRAY_2);
	lfont_Print_Centered_Text(str, &ra, color, 1);
}

void lstyle_Style_Trim_Base(Rect* r) { lrect_Inset_Rect(r, 2, 2); }
void lstyle_Style_Trim_Button(Rect* r) { lrect_Inset_Rect(r, 2, 2); }
void lstyle_Style_Trim_Border(Rect* r) { lrect_Inset_Rect(r, 1, 1); }
void lstyle_Style_Trim_TextField(Rect* r) { lrect_Inset_Rect(r, 1, 1); }

void lstyle_Style_Set_Icon_Actor(Actor* actor) { style_actor_gbl = actor; }

void lstyle_Style_Clear_Icon_Actor(void) { style_actor_gbl = NULL; }

void lstyle_Style_Draw_Icon(uint8_t icon_id, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
							int16_t lit) {
	if (!style_actor_gbl)
		return;
	lactor_Set_Actor_State(style_actor_gbl, 2 * icon_id + lit, 0);
	int16_t w, h;
	lactor_Get_Actor_Size(style_actor_gbl, &w, &h);
	if (style_actor_gbl->state < style_actor_gbl->arraySize)
		lactanim_Draw_Anim_Actor(style_actor_gbl, clip, dest, xoff, yoff, 1);
}

void lstyle_Style_Draw_Centered_Icon(uint8_t icon_id, Rect* clip, Rect* dest, int16_t lit) {
	lstyle_Style_Draw_Centered_Actor(style_actor_gbl, clip, dest, 2 * icon_id + lit);
}

void lstyle_Style_Draw_Centered_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t state) {
	if (!actor)
		return;

	int16_t saved_state = 0, saved_fract = 0;
	if (actor->res_type == FOURCC_ANIM) {
		lactor_Get_Actor_State(actor, &saved_state, &saved_fract);
		lactor_Set_Actor_State(actor, state, 0);
	}

	int16_t w, h, ox, oy;
	lactor_Get_Actor_Size(actor, &w, &h);
	lactor_Get_Actor_Offset(actor, &ox, &oy);

	int16_t cx = ((clip->right - clip->left - w) >> 1) + clip->left - ox;
	int16_t cy = ((clip->bottom - clip->top - h) >> 1) + clip->top - oy;

	switch (actor->res_type) {
		case FOURCC_DELT:
			lactdelt_Draw_Delta_Actor(actor, clip, dest, cx, cy, 1);
			break;
		case 'RAW ':
			lactraw_Draw_Raw_Actor(actor, clip, dest, cx, cy, 1);
			break;
		case FOURCC_ANIM:
			if (actor->arraySize > actor->state) {
				lactanim_Draw_Anim_Actor(actor, clip, dest, cx, cy, 1);
				lactor_Set_Actor_State(actor, saved_state, saved_fract);
			}
			break;
	}
}
