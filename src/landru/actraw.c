#include <stdlib.h>
#include <string.h>

#include <landru/actor.h>
#include <landru/actraw.h>
#include <landru/canvas.h>
#include <landru/dirty.h>
#include <landru/fourcc.h>
#include <landru/rect.h>
#include <landru/res.h>

// GLOBAL: TIE 0xD336C
static bool raw_actor_module_gbl = false;

void lactraw_Create_Raw_Actor_Module(void) {
	lactor_Create_Actor_Type('RAW ', (lactorFrameFunc)lactraw_Get_Raw_Actor_Frame, NULL);
	raw_actor_module_gbl = true;
}

void lactraw_Destroy_Raw_Actor_Module(void) {
	if (raw_actor_module_gbl) {
		lactor_Destroy_Actor_Type('RAW ');
		raw_actor_module_gbl = false;
	}
}

void lactraw_Get_Raw_Actor_Frame(Actor* actor, Rect* outFrame) {
	if (actor->data) {
		int16_t* hdr = (int16_t*)actor->data;
		lrect_Set_Rect(outFrame, hdr[0], hdr[1], hdr[2] + 1, hdr[3] + 1);
	} else {
		lrect_Set_Rect(outFrame, 0, 0, 0, 0);
	}
}

Actor* lactraw_Alloc_Raw_Actor(void* data, Rect* rect, int16_t x, int16_t y, int16_t z) {
	Actor* actor = lactor_Alloc_Actor(0);
	if (actor) {
		lactraw_Init_Raw_Actor(actor, data, rect, x, y, z);
		lactor_Set_Actor_Name(actor, 'RAW ', "");
		lactor_Non_Discard_Actor_Data(actor);
	}
	return actor;
}

Actor* lactraw_Res_Raw_Actor(const char* resName, Rect* rect, int16_t x, int16_t y, int16_t z) {
	void* data = lres_Load_Resource_Data(FOURCC_DELT, resName);
	if (!data)
		return NULL;

	Actor* actor = lactor_Alloc_Actor(0);
	if (actor) {
		lactraw_Init_Raw_Actor(actor, data, rect, x, y, z);
		lactor_Set_Actor_Name(actor, 'RAW ', resName);
		return actor;
	}

	free(data);
	return NULL;
}

/* Decompress delta-encoded data into a flat pixel buffer.
   Delta format: 8-byte header [left, top, right-1, bottom-1],
   then a control-word stream. Each control word: bit 0 = RLE flag,
   bits 1-15 = byte count. Before each chunk: 2 bytes x-offset + 2 bytes y-offset.
   RLE sub-encoding: per run byte, bit 0 = fill flag, bits 1-7 = length.
   fill=1: next byte is the fill value. fill=0: next N bytes are literal. */
static void* decompress_delta(void* src) {
	int16_t* src_hdr = (int16_t*)src;
	int16_t top = src_hdr[1];
	int16_t width = src_hdr[2] - src_hdr[0] + 1;
	int16_t bottom = src_hdr[3];
	int32_t height = bottom - top + 1;
	int32_t alloc_size = height * width + 8;

	void* dst = malloc(alloc_size);
	if (!dst)
		return NULL;

	memset(dst, 0, alloc_size);
	memcpy(dst, src, 8);

	uint8_t* sp = (uint8_t*)src;
	uint8_t* dp = (uint8_t*)dst;
	int i = 10;
	uint16_t ctrl = *(uint16_t*)(sp + 8);

	while (ctrl) {
		int16_t left_off = *(int16_t*)(sp + i);
		i += 2;
		int16_t y_off = *(int16_t*)(sp + i);
		i += 2;
		int dst_offset = y_off * width + left_off + 8;
		int count = ctrl >> 1;

		if (ctrl & 1) {
			int remaining = count;
			while (remaining > 0) {
				uint8_t run_byte = sp[i++];
				int run_len = run_byte >> 1;
				if (run_byte & 1) {
					memset(dp + dst_offset, sp[i++], run_len);
				} else {
					memcpy(dp + dst_offset, sp + i, run_len);
					i += run_len;
				}
				dst_offset += run_len;
				remaining -= run_len;
			}
		} else {
			memcpy(dp + dst_offset, sp + i, count);
			i += count;
		}

		ctrl = *(uint16_t*)(sp + i);
		i += 2;
	}

	return dst;
}

void lactraw_Init_Raw_Actor(Actor* actor, void* data, Rect* rect, int16_t x, int16_t y, int16_t z) {
	lrect_Set_Rect(&actor->frame, rect->left, rect->top, rect->right, rect->bottom);
	actor->x = x;
	actor->y = y;
	actor->zplane = z;
	lactor_Discard_Actor_Data(actor);
	actor->draw = (lactorDrawFunc)lactraw_Draw_Raw_Actor;
	actor->update = (lactorUpdateFunc)lactraw_Update_Raw_Actor;

	void* raw_data = decompress_delta(data);
	free(data);
	actor->data = raw_data;

	lactor_Add_Actor_To_System(actor);
	Rect frame;
	lactraw_Get_Raw_Actor_Frame(actor, &frame);
	actor->w = frame.right - frame.left;
	actor->h = frame.bottom - frame.top;
	lrect_Copy_Rect(&actor->bounds, &frame);
}

int lactraw_Draw_Raw_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
						   int16_t refresh) {
	(void)clip;
	(void)dest;
	if (!refresh)
		return 0;
	if (!actor->data)
		return 0;

	lactor_emit_draw(actor, xoff, yoff);

	int dirty = lactor_Is_Actor_Dirty(actor);
	return lactraw_Draw_Clipped_Raw(actor->data, xoff, yoff, dirty);
}

void lactraw_Update_Raw_Actor(Actor* actor) {
	lactor_Move_Actor(actor);
	lactor_Move_Actor_Frame(actor);
}

/* Unclipped raw blit: copy width*height pixels to draw buffer at (xoff, yoff) */
static void raw_image(uint8_t* pixels, int16_t xoff, int16_t yoff, int16_t width, int16_t height) {
	int src_offset = 0;
	int dst_offset = xoff + yoff * draw_w_gbl;
	for (int row = 0; row < height; row++) {
		memcpy((uint8_t*)draw_buff_gbl + dst_offset, pixels + src_offset, width);
		dst_offset += draw_w_gbl;
		src_offset += width;
	}
}

/* Clipped raw blit: clip against canvas clip rect then copy visible portion */
static void raw_clip(uint8_t* pixels, int16_t xoff, int16_t yoff, int16_t width, int16_t height) {
	int skip_rows = 0, skip_cols = 0;
	int16_t src_width = width;
	int16_t clip_width = width;

	if (xoff < clip_left_gbl) {
		int16_t left_skip = clip_left_gbl - xoff;
		xoff = clip_left_gbl;
		skip_cols = left_skip;
		clip_width -= left_skip;
	}
	if (clip_width + xoff > clip_right_gbl)
		clip_width = clip_right_gbl - xoff;
	if (yoff < clip_top_gbl) {
		int16_t top_skip = clip_top_gbl - yoff;
		yoff = clip_top_gbl;
		skip_rows = top_skip;
		height -= top_skip;
	}
	if (height + yoff > clip_bottom_gbl)
		height = clip_bottom_gbl - yoff;

	int dst_offset = draw_w_gbl * yoff + xoff;
	int src_offset = skip_rows * src_width;
	for (int row = 0; row < height; row++) {
		src_offset += skip_cols;
		memcpy((uint8_t*)draw_buff_gbl + dst_offset, pixels + src_offset, clip_width);
		dst_offset += draw_w_gbl;
		skip_cols = src_width;
	}
}

int lactraw_Draw_Clipped_Raw(void* data, int16_t xoff, int16_t yoff, int dirty) {
	int16_t* hdr = (int16_t*)data;
	Rect r, clipped;
	lrect_Set_Rect(&r, hdr[0] + xoff, hdr[1] + yoff, hdr[2] + xoff + 1, hdr[3] + yoff + 1);
	lrect_Copy_Rect(&clipped, &r);

	int ret = 0;
	if (lcanvas_Clip_Rect_To_Canvas(&clipped)) {
		int16_t width = hdr[2] - hdr[0] + 1;
		int16_t height = hdr[3] - hdr[1] + 1;
		uint8_t* pixels = (uint8_t*)(hdr + 4);

		if (lrect_Equal_Rect(&clipped, &r))
			raw_image(pixels, xoff, yoff, width, height);
		else
			raw_clip(pixels, xoff, yoff, width, height);

		if (dirty)
			ldirty_Dirty_Rect(&clipped);
		ret = 1;
	}
	return ret;
}

void* lactraw_Uncompress_Delta_Data(void* src) {
	void* result = decompress_delta(src);
	free(src);
	return result;
}
