#include <stdlib.h>
#include <string.h>

#include <landru/actdelt.h>
#include <landru/actor.h>
#include <landru/canvas.h>
#include <landru/color.h>
#include <landru/delta.h>
#include <landru/dirty.h>
#include <landru/flip.h>
#include <landru/fourcc.h>
#include <landru/rect.h>
#include <landru/res.h>
#include <landru/scale.h>

#include "host_internal.h"

// GLOBAL: TIE 0xD2F70
static bool delta_actor_module_gbl = false;
static Actor* scaled_svga_warnings[32];

static void warn_scaled_svga_once(Actor* actor) {
	for (size_t i = 0; i < sizeof scaled_svga_warnings / sizeof scaled_svga_warnings[0]; i++) {
		if (scaled_svga_warnings[i] == actor)
			return;
		if (!scaled_svga_warnings[i]) {
			scaled_svga_warnings[i] = actor;
			landru_host_log(LANDRU_LOG_WARN, "scaled DELT '%.*s' is unsupported on a %dx%d SVGA canvas\n", 8,
							actor->res_name, draw_w_gbl, draw_h_gbl);
			return;
		}
	}
}

void lactdelt_Create_Delta_Actor_Module(void) {
	memset(scaled_svga_warnings, 0, sizeof scaled_svga_warnings);
	lactor_Create_Actor_Type(FOURCC_DELT, lactdelt_Get_Delta_Actor_Frame, NULL);
	delta_actor_module_gbl = true;
}

void lactdelt_Destroy_Delta_Actor_Module(void) {
	if (delta_actor_module_gbl) {
		lactor_Destroy_Actor_Type(FOURCC_DELT);
		delta_actor_module_gbl = false;
	}
}

void lactdelt_Get_Delta_Actor_Frame(Actor* actor, Rect* outFrame) {
	if (actor->data) {
		int16_t* hdr = (int16_t*)actor->data;
		lrect_Set_Rect(outFrame, hdr[0], hdr[1], hdr[2] + 1, hdr[3] + 1);
	} else {
		lrect_Set_Rect(outFrame, 0, 0, 0, 0);
	}
}

Actor* lactdelt_Alloc_Delta_Actor(void* data, Rect* rect, int16_t x, int16_t y, int16_t z) {
	Actor* actor = lactor_Alloc_Actor(0);
	if (!actor)
		return NULL;

	lactdelt_Init_Delta_Actor(actor, data, rect, x, y, z);
	lactor_Set_Actor_Name(actor, FOURCC_DELT, "");
	lactor_Non_Discard_Actor_Data(actor);
	return actor;
}

Actor* lactdelt_Res_Delta_Actor(const char* resName, Rect* rect, int16_t x, int16_t y, int16_t z) {
	void* data = lres_Load_Resource_Data(FOURCC_DELT, resName);
	if (!data)
		return NULL;

	Actor* actor = lactor_Alloc_Actor(0);
	if (!actor) {
		free(data);
		return NULL;
	}

	lactdelt_Init_Delta_Actor(actor, data, rect, x, y, z);
	lactor_Set_Actor_Name(actor, FOURCC_DELT, resName);
	return actor;
}

void lactdelt_Init_Delta_Actor(Actor* actor, void* data, Rect* rect, int16_t x, int16_t y, int16_t z) {
	lrect_Set_Rect(&actor->frame, rect->left, rect->top, rect->right, rect->bottom);
	actor->x = x;
	actor->y = y;
	actor->zplane = z;
	lactor_Discard_Actor_Data(actor);
	actor->draw = (lactorDrawFunc)lactdelt_Draw_Delta_Actor;
	actor->data = data;
	actor->update = (lactorUpdateFunc)lactdelt_Update_Delta_Actor;
	lactor_Add_Actor_To_System(actor);

	Rect frame;
	lactdelt_Get_Delta_Actor_Frame(actor, &frame);
	actor->w = frame.right - frame.left;
	actor->h = frame.bottom - frame.top;
	lrect_Copy_Rect(&actor->bounds, &frame);
}

int lactdelt_Draw_Delta_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
							  int16_t refresh) {
	(void)clip;
	(void)dest;
	if (!refresh)
		return 0;
	if (!actor->data)
		return 0;

	lactor_emit_draw(actor, xoff, yoff);

	int dirty = lactor_Is_Actor_Dirty(actor);

	/* Scaled path */
	if (actor->xscale != 256 || actor->yscale != 256) {
		return lactdelt_Draw_Scaled_Clipped_Delta(actor, actor->data, xoff, yoff, dirty);
	}

	/* Flipped path */
	if ((actor->flags & AF_HFLIP) || (actor->flags & AF_VFLIP)) {
		return lactdelt_Draw_Flipped_Clipped_Delta(actor, actor->data, xoff, yoff, dirty);
	}

	/* Color-substitution path */
	if (actor->flags & AF_REMAP_COLOR) {
		return lactdelt_Draw_Color_Clipped_Delta(actor->data, xoff, actor->foreColor, yoff, dirty);
	}

	/* Default: plain clipped delta */
	return lactdelt_Draw_Clipped_Delta(actor->data, xoff, yoff, dirty);
}

void lactdelt_Update_Delta_Actor(Actor* actor) {
	lactor_Move_Actor(actor);
	lactor_Move_Actor_Frame(actor);
}

int lactdelt_Draw_Clipped_Delta(void* data, int16_t x, int16_t y, int dirty) {
	int16_t* hdr = (int16_t*)data;
	Rect r, clipped;
	lrect_Set_Rect(&r, hdr[0] + x, hdr[1] + y, hdr[2] + x + 1, hdr[3] + y + 1);
	lrect_Copy_Rect(&clipped, &r);

	int ret = 0;
	if (lcanvas_Clip_Rect_To_Canvas(&clipped)) {
		uint8_t* pixels = (uint8_t*)(hdr + 4);
		if (lrect_Equal_Rect(&clipped, &r)) {
			ldelta_Delta_Image(x, y, pixels);
		} else {
			ldelta_Delta_Clip(x, y, pixels);
		}
		if (dirty)
			ldirty_Dirty_Rect(&clipped);
		ret = 1;
	}
	return ret;
}

int lactdelt_Draw_Flipped_Clipped_Delta(Actor* actor, void* data, int16_t xoff, int16_t yoff, int dirty) {
	if (!data)
		return 0;

	int16_t* hdr = (int16_t*)data;
	bool hFlip = (actor->flags & AF_HFLIP) != 0;
	bool vFlip = (actor->flags & AF_VFLIP) != 0;

	int16_t br_x = actor->bounds.right + actor->bounds.left - 1;
	int16_t br_y = actor->bounds.bottom + actor->bounds.top - 1;

	int16_t left, right, top, bottom;
	if (hFlip) {
		/* mirror around (br_x+1)/2: hdr[0] (smaller) maps to the right edge */
		left = (br_x - hdr[2]) + xoff;
		right = (br_x - hdr[0]) + xoff;
	} else {
		left = hdr[0] + xoff;
		right = hdr[2] + xoff;
	}
	if (vFlip) {
		top = (br_y - hdr[3]) + yoff;
		bottom = (br_y - hdr[1]) + yoff;
	} else {
		top = hdr[1] + yoff;
		bottom = hdr[3] + yoff;
	}

	uint8_t* pixels = (uint8_t*)(hdr + 4);
	int16_t cl = clip_left_gbl;
	int16_t ct = clip_top_gbl;
	int16_t cr = clip_right_gbl;
	int16_t cb = clip_bottom_gbl;

	int ret = 1;
	if (left >= cl && top >= ct && right < cr && bottom < cb) {
		lflip_Delta_Flip(pixels, xoff, yoff, br_x, br_y, hFlip, vFlip);
	} else if (left < cr && top < cb && right >= cl && bottom >= ct) {
		lflip_Delta_Clip_Flip(pixels, xoff, yoff, br_x, br_y, hFlip, vFlip, cl, ct, cr - 1, cb - 1);
	} else {
		ret = 0;
	}

	if (ret && dirty) {
		Rect dirtyRect;
		lrect_Set_Rect(&dirtyRect, left, top, right + 1, bottom + 1);
		Rect clipRect;
		lrect_Set_Rect(&clipRect, cl, ct, cr, cb);
		lrect_Clip_Rect(&dirtyRect, &clipRect);
		ldirty_Dirty_Rect(&dirtyRect);
	}

	return ret;
}

int lactdelt_Draw_Scaled_Clipped_Delta(Actor* actor, void* data, int16_t xoff, int16_t yoff, int dirty) {
	int16_t w, h, ox, oy;
	lactor_Get_Actor_Size(actor, &w, &h);
	if (actor->res_type == FOURCC_DELT)
		lactor_Get_Actor_Offset(actor, &ox, &oy);
	else {
		ox = 0;
		oy = 0;
	}

	int16_t scaled_w = (int16_t)((int32_t)w * actor->xscale / 256);
	int16_t scaled_h = (int16_t)((int32_t)h * actor->yscale / 256);

	int16_t left_x = xoff - (scaled_w - w) / 2;
	int16_t top_y = yoff - (scaled_h - h) / 2;
	int16_t right_x = left_x + scaled_w;
	int16_t bottom_y = top_y + scaled_h;

	int16_t cw = clip_right_gbl - clip_left_gbl;
	int16_t ch = clip_bottom_gbl - clip_top_gbl;
	if (draw_w_gbl > 320 || draw_h_gbl > 200) {
		warn_scaled_svga_once(actor);
		return 0;
	}

	if (left_x >= clip_left_gbl + cw)
		return 0;
	if (top_y >= clip_top_gbl + ch)
		return 0;
	if (right_x < clip_left_gbl)
		return 0;
	if (bottom_y < clip_top_gbl)
		return 0;

	uint8_t* pixels = (uint8_t*)data + 8;
	if (!lscale_Scale_Delta_Clip(pixels, left_x, top_y, w, h, ox, oy, actor->xscale, actor->yscale,
								 clip_left_gbl, clip_top_gbl, clip_left_gbl + cw - 1, clip_top_gbl + ch - 1))
		return 0;

	if (dirty) {
		Rect r, clip;
		lrect_Set_Rect(&r, left_x, top_y, right_x + 1, bottom_y + 1);
		lrect_Set_Rect(&clip, clip_left_gbl, clip_top_gbl, clip_left_gbl + cw, clip_top_gbl + ch);
		lrect_Clip_Rect(&r, &clip);
		ldirty_Dirty_Rect(&r);
	}

	return 1;
}

int lactdelt_Draw_Color_Clipped_Delta(void* data, int16_t xoff, int16_t color, int16_t yoff, int dirty) {
	int16_t* hdr = (int16_t*)data;
	Rect r, clipped;
	lrect_Set_Rect(&r, hdr[0] + xoff, hdr[1] + yoff, hdr[2] + xoff + 1, hdr[3] + yoff + 1);
	lcanvas_Get_Drawing_Canvas_Clip(&clipped);
	lrect_Clip_Rect(&clipped, &r);

	if (lrect_Empty_Rect(&clipped))
		return 0;

	uint8_t* pixels = (uint8_t*)(hdr + 4);
	if (lrect_Equal_Rect(&clipped, &r)) {
		lcolor_Delta_Color_Image(pixels, xoff, yoff, (uint8_t)color);
	} else {
		lcolor_Delta_Color_Clip(pixels, xoff, yoff, clipped.left, clipped.top, clipped.right - 1,
								clipped.bottom - 1, (uint8_t)color);
	}
	if (dirty)
		ldirty_Dirty_Rect(&clipped);
	return 1;
}
