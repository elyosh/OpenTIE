#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <landru/bitmap.h>
#include <landru/io.h>
#include <landru/rect.h>
#include <landru/surface.h>
#include <landru/vesa.h>

#include "host_internal.h"

/*
 * XVESA — Video output layer.
 *
 * Target: linear framebuffer presented by the embedding application.
 * vesa_buff_gbl is a w*h byte array of 8-bit palette indices.
 * The platform reads it each frame and renders via palette lookup.
 */

/* Globals */
// GLOBAL: TIE 0xFBB7A, TIE98 0x6B0DC2
int16_t vesa_w_gbl = 320;
// GLOBAL: TIE98 0x6B0DC0
int16_t vesa_h_gbl = 200;
// GLOBAL: TIE98 0x6B0DC4
int16_t vesa_bpsl_gbl = 320;
// GLOBAL: TIE 0xFBB40, TIE98 0x4F4420
uint8_t* vesa_buff_gbl;
// GLOBAL: TIE98 0x58B2A8
Rect vesa_rect;
// MODERN ADAPTATION: tracks pending host presentation work.
bool vesa_dirty_gbl;
// GLOBAL: TIE98 0x58B2C0
uint16_t landru_video_flags_gbl;
// GLOBAL: TIE98 0x6B0DB4
int16_t landru_logical_width_gbl = 640;
// GLOBAL: TIE98 0x6B0DB8
int16_t landru_logical_height_gbl = 480;
// GLOBAL: TIE98 0x665B60
uint8_t* vga_compat_buffer_gbl;
// PORT: retained software framebuffer and active presentation route.
static LandruPortVideoBackend vesa_port_backend_gbl;
static LandruPortVideoBackend vesa_port_initial_backend_gbl;
static bool vesa_port_initial_backend_set_gbl;
static uint8_t* vesa_port_software_buffer_gbl;
static int16_t vesa_port_software_width_gbl;
static int16_t vesa_port_software_height_gbl;
// GLOBAL: TIE98 0x4F4424
static uint32_t vesa_linear_buffer_size_gbl;
// GLOBAL: TIE98 0x58B068
static uint16_t vesa_banked_copy_available_gbl;
// GLOBAL: TIE98 0x58B2B0
static uint16_t vesa_banked_copy_enabled_gbl;

/* RECOVERY HELPER: shares the mode dimensions used by the recovered create
 * and mode-entry functions.
 *   0x13   -> VGA mode 13h  (320x200x256)
 *   0x101  -> VBE mode      (640x480x256)
 * Anything else defaults to VGA 13h for safety (retail's unreachable
 * 0x111 path falls here). */
static void vesa_decode_mode(uint16_t mode, int16_t* w, int16_t* h) {
	switch (mode) {
		case 0x101:
			*w = 640;
			*h = 480;
			break;
		case 0x13:
		default:
			*w = 320;
			*h = 200;
			break;
	}
}

/* PORT: software storage retained while the platform video surface is active. */
static bool vesa_port_Set_Software_Mode(uint16_t mode) {
	int16_t new_width;
	int16_t new_height;
	vesa_decode_mode(mode, &new_width, &new_height);
	if (new_width != vesa_port_software_width_gbl || new_height != vesa_port_software_height_gbl ||
		!vesa_port_software_buffer_gbl) {
		uint8_t* pixels = calloc((size_t)new_width * (size_t)new_height, 1);
		if (!pixels)
			return false;
		free(vesa_port_software_buffer_gbl);
		vesa_port_software_buffer_gbl = pixels;
		vesa_port_software_width_gbl = new_width;
		vesa_port_software_height_gbl = new_height;
	}
	vesa_w_gbl = new_width;
	vesa_h_gbl = new_height;
	vesa_bpsl_gbl = new_width;
	vesa_buff_gbl = vesa_port_software_buffer_gbl;
	lrect_Set_Rect(&vesa_rect, 0, 0, vesa_w_gbl, vesa_h_gbl);
	lio_Set_Mouse_Limits(&vesa_rect);
	lsurface_Invalidate_Presentation();
	return true;
}

// PORT: selects which backend the recovered module creation should initialize.
bool landru_port_Set_Initial_Video_Backend(LandruPortVideoBackend backend) {
	if (backend == LANDRU_PORT_VIDEO_PLATFORM && !landru_host_has_platform_video())
		return false;
	vesa_port_initial_backend_gbl = backend;
	vesa_port_initial_backend_set_gbl = true;
	return true;
}

/* --- Module lifecycle --- */

// FUNCTION: TIE 0x8FF40, TIE98 0x4A16E0
void* lvesa_Get_Vesa_Mode_Struct(void) { return NULL; }

// FUNCTION: TIE98 0x4A41C0
void XBM_Set_VGA_Compatibility_Mode(int enabled, int logical_width, int logical_height) {
	landru_video_flags_gbl = enabled ? LANDRU_VIDEO_VGA_COMPAT : 0;
	landru_logical_width_gbl = (int16_t)logical_width;
	landru_logical_height_gbl = (int16_t)logical_height;
}

// FUNCTION: TIE98 0x4A1680
uint8_t* XVESA_Get_Video_Buffer(void) { return vesa_buff_gbl; }

// FUNCTION: TIE98 0x4A1690
void XVESA_Set_Video_Buffer(uint8_t* pixels) { vesa_buff_gbl = pixels; }

/* MODERN ADAPTATION: the game-specific platform backend publishes the pitch
 * that TIE98's XVESA mode setter obtained directly from the display module. */
void lvesa_Set_Platform_Pitch(int16_t pitch) { vesa_bpsl_gbl = pitch; }

// FUNCTION: TIE98 0x4A16B0
uint32_t XVESA_Get_Linear_Buffer_Size(void) { return vesa_linear_buffer_size_gbl; }

// FUNCTION: TIE98 0x4A16C0
void XVESA_Set_Linear_Buffer_Size(uint32_t size_bytes) { vesa_linear_buffer_size_gbl = size_bytes; }

/* The embedding application owns presentation. Landru only allocates and
 * updates the 8bpp framebuffer. Mode changes update its dimensions in place. */

// FUNCTION: TIE 0x8FF48, TIE98 0x4A16F0
void lvesa_Create_Vesa_Module(uint16_t mode) {
	if (vesa_port_initial_backend_set_gbl)
		vesa_port_backend_gbl = vesa_port_initial_backend_gbl;
	else if (landru_host_has_platform_video())
		vesa_port_backend_gbl = LANDRU_PORT_VIDEO_PLATFORM;
	else
		vesa_port_backend_gbl = LANDRU_PORT_VIDEO_SOFTWARE;
	if (vesa_port_backend_gbl == LANDRU_PORT_VIDEO_PLATFORM) {
		vesa_banked_copy_available_gbl = 0;
		lvesa_Enter_VESA_Mode(mode);
		return;
	}
	(void)vesa_port_Set_Software_Mode(mode);
}

// FUNCTION: TIE 0x9008C, TIE98 0x4A1710
void lvesa_Destroy_VESA_Module(void) {
	/* MODERN ADAPTATION: releases the host storage owned by the Landru module. */
	free(vesa_port_software_buffer_gbl);
	vesa_port_software_buffer_gbl = NULL;
	vesa_port_software_width_gbl = 0;
	vesa_port_software_height_gbl = 0;
	vesa_buff_gbl = NULL;
	vesa_port_backend_gbl = LANDRU_PORT_VIDEO_SOFTWARE;
	vesa_port_initial_backend_gbl = LANDRU_PORT_VIDEO_SOFTWARE;
	vesa_port_initial_backend_set_gbl = false;
}

// PORT: no recovered counterpart; switches presentation ownership by edition.
bool landru_port_Select_Video_Backend(LandruPortVideoBackend backend, uint16_t mode) {
	if (backend == LANDRU_PORT_VIDEO_PLATFORM && !landru_host_has_platform_video())
		return false;
	if (backend == LANDRU_PORT_VIDEO_SOFTWARE) {
		if (!vesa_port_Set_Software_Mode(mode))
			return false;
		vesa_port_backend_gbl = backend;
		return true;
	}
	vesa_port_backend_gbl = backend;
	(void)lvesa_Set_VESA_Mode_Internal(mode);
	return true;
}

// FUNCTION: TIE98 0x4A1730
int16_t lvesa_Set_VESA_Mode_Internal(uint16_t mode) {
	if (vesa_port_backend_gbl == LANDRU_PORT_VIDEO_SOFTWARE)
		return vesa_port_Set_Software_Mode(mode) ? 0 : (int16_t)mode;

	if (vesa_banked_copy_available_gbl)
		return (int16_t)mode;
	landru_host_video_set_mode(mode);
	switch (mode) {
		case 0x13:
			vesa_w_gbl = 320;
			vesa_h_gbl = 200;
			vesa_banked_copy_enabled_gbl = 0;
			break;
		case 0x101:
			vesa_w_gbl = 640;
			vesa_h_gbl = 480;
			vesa_banked_copy_enabled_gbl = 0;
			break;
		case 0x103:
			vesa_w_gbl = 800;
			vesa_h_gbl = 600;
			vesa_banked_copy_enabled_gbl = 0;
			break;
		default:
			lrect_Set_Rect(&vesa_rect, 0, 0, vesa_w_gbl, vesa_h_gbl);
			lio_Set_Mouse_Limits(&vesa_rect);
			return (int16_t)mode;
	}
	lrect_Set_Rect(&vesa_rect, 0, 0, vesa_w_gbl, vesa_h_gbl);
	lio_Set_Mouse_Limits(&vesa_rect);
	return 0;
}

// FUNCTION: TIE 0x900C0, TIE98 0x4A1720
void lvesa_Enter_VESA_Mode(uint16_t mode) { (void)lvesa_Set_VESA_Mode_Internal(mode); }

/* RECOVERY HELPER: the TIE98 XVESA functions repeat this direct-surface
 * lock around each copy. The secondary VGA compatibility buffer remains an
 * ordinary Landru-owned bitmap. */
static bool lvesa_Begin_Video_Access(LandruVideoTarget* target) {
	const bool direct_surface = vesa_port_backend_gbl == LANDRU_PORT_VIDEO_PLATFORM &&
								!(lsurface_Get_Surface_Set() == LANDRU_SURFACE_VGA && vesa_w_gbl == 640);
	if (direct_surface)
		landru_host_video_lock();
	if (lsurface_Get_Active_Video_Target(target))
		return direct_surface;
	if (direct_surface)
		landru_host_video_unlock();
	return false;
}

static void lvesa_End_Video_Access(bool direct_surface) {
	if (direct_surface)
		landru_host_video_unlock();
}

// FUNCTION: TIE98 0x4A23B0
void lvesa_Erase_Video(uint8_t color) {
	LandruVideoTarget target;
	const bool direct_surface = lvesa_Begin_Video_Access(&target);
	if (!target.pixels)
		return;
	for (int16_t y = 0; y < target.height; y++)
		memset(target.pixels + (size_t)y * target.stride, color, (size_t)target.width);
	lsurface_Mark_Active_Video_Dirty();
	lvesa_End_Video_Access(direct_surface);
}

/* --- Bitmap pixel access helper --- */

/* RECOVERY HELPER: shares the recovered bitmap payload calculation. */
static inline uint8_t* bm_pixels(BitmapStruct* bm) { return (uint8_t*)bm->data + bm->offset; }

/* --- Standard bitmap ↔ video blits --- */

// FUNCTION: TIE98 0x4A1820
int lvesa_Copy_Bitmap_Clip_To_Video(BitmapStruct* bm, int16_t x, int16_t y) {
	LandruVideoTarget target;
	Rect src = bm->clip;
	int16_t dx = x - src.left;
	int16_t dy = y - src.top;

	if (!lrect_Clip_Rect(&src, &bm->clip))
		return 0;

	Rect dst;
	lrect_Copy_Rect(&dst, &src);
	lrect_Offset_Rect(&dst, dx - src.left, dy - src.top);

	if (!lsurface_Get_Active_Video_Target(&target))
		return 0;
	Rect r;
	lrect_Copy_Rect(&r, &dst);
	if (!lrect_Clip_Rect(&r, &target.bounds))
		return 0;

	src.left += r.left - dst.left;
	src.top += r.top - dst.top;
	src.right += r.right - dst.right;
	src.bottom += r.bottom - dst.bottom;

	lvesa_Copy_Bitmap_Video(bm, &src, r.left, r.top, 1);
	return 1;
}

// FUNCTION: TIE98 0x4A1860
int lvesa_Copy_Bitmap_Portion_To_Video(BitmapStruct* bm, Rect* rect, int16_t x, int16_t y) {
	LandruVideoTarget target;
	Rect src = *rect;
	if (!lrect_Clip_Rect(&src, &bm->clip))
		return 0;

	Rect dst;
	lrect_Copy_Rect(&dst, &src);
	lrect_Offset_Rect(&dst, x - rect->left, y - rect->top);

	if (!lsurface_Get_Active_Video_Target(&target))
		return 0;
	Rect r;
	lrect_Copy_Rect(&r, &dst);
	if (!lrect_Clip_Rect(&r, &target.bounds))
		return 0;

	src.left += r.left - dst.left;
	src.top += r.top - dst.top;
	src.right += r.right - dst.right;
	src.bottom += r.bottom - dst.bottom;

	lvesa_Copy_Bitmap_Video(bm, &src, r.left, r.top, 1);
	return 1;
}

// FUNCTION: TIE98 0x4A1880
int lvesa_Copy_Video_Portion_To_Bitmap(BitmapStruct* bm, Rect* rect, int16_t x, int16_t y) {
	LandruVideoTarget target;
	Rect offset_rect = *rect;
	lrect_Offset_Rect(&offset_rect, x - rect->left, y - rect->top);

	Rect src = offset_rect;
	if (!lrect_Clip_Rect(&src, &bm->clip))
		return 0;

	Rect dst;
	lrect_Copy_Rect(&dst, &src);
	lrect_Offset_Rect(&dst, rect->left - offset_rect.left, rect->top - offset_rect.top);

	if (!lsurface_Get_Active_Video_Target(&target))
		return 0;
	Rect r;
	lrect_Copy_Rect(&r, &dst);
	if (!lrect_Clip_Rect(&r, &target.bounds))
		return 0;

	src.left += r.left - dst.left;
	src.top += r.top - dst.top;
	src.right += r.right - dst.right;
	src.bottom += r.bottom - dst.bottom;

	lvesa_Copy_Bitmap_Video(bm, &src, r.left, r.top, 0);
	return 1;
}

// FUNCTION: TIE98 0x4A1A70
void lvesa_Copy_Bitmap_Video(BitmapStruct* bm, Rect* src, int16_t dst_x, int16_t dst_y, int16_t to_video) {
	LandruVideoTarget target;
	const bool direct_surface = lvesa_Begin_Video_Access(&target);
	if (!target.pixels)
		return;

	int16_t width = src->right - src->left;
	int16_t lines = src->bottom - src->top;
	if (width <= 0 || lines <= 0) {
		lvesa_End_Video_Access(direct_surface);
		return;
	}

	uint8_t* bm_ptr = bm_pixels(bm) + src->top * bm->w + src->left;
	uint8_t* fb_ptr = target.pixels + dst_y * target.stride + dst_x;

	if (to_video) {
		lvesa_Copy_Bitmap_Video_Data(bm, fb_ptr, bm_ptr, lines, width, target.stride, bm->w);
		lsurface_Mark_Active_Video_Dirty();
	} else {
		lvesa_Copy_Bitmap_Video_Data(bm, bm_ptr, fb_ptr, lines, width, bm->w, target.stride);
	}
	lvesa_End_Video_Access(direct_surface);
}

// FUNCTION: TIE98 0x4A1DC0
void lvesa_Copy_Bitmap_Video_Data(BitmapStruct* bm, uint8_t* dst, uint8_t* src, int16_t lines, int16_t width,
								  int16_t dst_stride, int16_t src_stride) {
	if (bm->flags & 1) {
		for (int16_t y = 0; y < lines; y++) {
			for (int16_t x = 0; x < width; x++) {
				if (src[x])
					dst[x] = src[x];
			}
			dst += dst_stride;
			src += src_stride;
		}
	} else {
		for (int16_t y = 0; y < lines; y++) {
			memcpy(dst, src, width);
			dst += dst_stride;
			src += src_stride;
		}
	}
}

/* --- Diff bitmap → video blits --- */

// FUNCTION: TIE98 0x4A1E90
int lvesa_Copy_Diff_Bitmap_Portion_To_Video(BitmapStruct* bm, BitmapStruct* diff, Rect* rect, int16_t x,
											int16_t y) {
	LandruVideoTarget target;
	Rect src = *rect;
	if (!lrect_Clip_Rect(&src, &bm->clip))
		return 0;

	Rect dst;
	lrect_Copy_Rect(&dst, &src);
	lrect_Offset_Rect(&dst, x - rect->left, y - rect->top);

	if (!lsurface_Get_Active_Video_Target(&target))
		return 0;
	Rect r;
	lrect_Copy_Rect(&r, &dst);
	if (!lrect_Clip_Rect(&r, &target.bounds))
		return 0;

	src.left += r.left - dst.left;
	src.top += r.top - dst.top;
	src.right += r.right - dst.right;
	src.bottom += r.bottom - dst.bottom;

	lvesa_Copy_Diff_Bitmap_Video(bm, diff, &src, r.left, r.top);
	return 1;
}

// FUNCTION: TIE98 0x4A2050
void lvesa_Copy_Diff_Bitmap_Video(BitmapStruct* bm, BitmapStruct* diff, Rect* src, int16_t dst_x,
								  int16_t dst_y) {
	LandruVideoTarget target;
	const bool direct_surface = lvesa_Begin_Video_Access(&target);
	if (!target.pixels)
		return;

	int16_t width = src->right - src->left;
	int16_t lines = src->bottom - src->top;
	if (width <= 0 || lines <= 0) {
		lvesa_End_Video_Access(direct_surface);
		return;
	}

	uint8_t* bm_ptr = bm_pixels(bm) + src->top * bm->w + src->left;
	uint8_t* diff_ptr = bm_pixels(diff) + src->top * diff->w + src->left;
	uint8_t* fb_ptr = target.pixels + dst_y * target.stride + dst_x;

	bool dirty = false;
	for (int16_t y = 0; y < lines; y++) {
		int16_t x = 0;
		while (x < width) {
			while (x < width && bm_ptr[x] == diff_ptr[x])
				++x;
			const int16_t first = x;
			while (x < width && bm_ptr[x] != diff_ptr[x]) {
				diff_ptr[x] = bm_ptr[x];
				fb_ptr[x] = bm_ptr[x];
				++x;
			}
			if (x > first) {
				dirty = true;
			}
		}
		bm_ptr += bm->w;
		diff_ptr += diff->w;
		fb_ptr += target.stride;
	}
	if (dirty)
		lsurface_Mark_Active_Video_Dirty();
	lvesa_End_Video_Access(direct_surface);
}

// FUNCTION: TIE98 0x4C3EC0
void lvesa_Copy_Diff_Bitmap_Data(int lines, int width, int stride, uint8_t* dst, const uint8_t* src) {
	for (int y = 0; y < lines; y++) {
		for (int x = 0; x < width; x++) {
			if (src[x] != dst[x])
				dst[x] = src[x];
		}
		dst += stride;
		src += stride;
	}
}
