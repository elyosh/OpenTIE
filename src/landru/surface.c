#include <stdlib.h>
#include <string.h>

#include <landru/canvas.h>
#include <landru/dirty.h>
#include <landru/io.h>
#include <landru/surface.h>
#include <landru/vesa.h>

#include "host_internal.h"

typedef struct LandruSurfaceState {
	BitmapStruct vga_render;
	BitmapStruct vga_diff;
	uint8_t* vga_video;
	LandruSurfaceSet native_set;
	LandruSurfaceSet active_set;
	uint32_t generation;
	bool secondary_vga;
	bool vga_video_dirty;
	bool initialized;
} LandruSurfaceState;

static LandruSurfaceState surface_state;

static const char* surface_name(LandruSurfaceSet set) { return set == LANDRU_SURFACE_SVGA ? "SVGA" : "VGA"; }

static BitmapStruct* render_for_set(LandruSurfaceSet set) {
	if (set == surface_state.native_set)
		return &screen_bm_gbl;
	if (set == LANDRU_SURFACE_VGA && surface_state.secondary_vga)
		return &surface_state.vga_render;
	return NULL;
}

static BitmapStruct* diff_for_set(LandruSurfaceSet set) {
	if (!diff_exists_gbl)
		return NULL;
	if (set == surface_state.native_set)
		return &diff_bm_gbl;
	if (set == LANDRU_SURFACE_VGA && surface_state.secondary_vga)
		return &surface_state.vga_diff;
	return NULL;
}

bool lsurface_Create_Surface_Module(bool create_secondary_vga) {
	if (surface_state.initialized || !screen_bm_gbl.data || !vesa_buff_gbl)
		return false;

	memset(&surface_state, 0, sizeof surface_state);
	surface_state.native_set =
		(screen_bm_gbl.w == 640 && screen_bm_gbl.h == 480) ? LANDRU_SURFACE_SVGA : LANDRU_SURFACE_VGA;
	surface_state.active_set = surface_state.native_set;
	surface_state.generation = 1;

	if (create_secondary_vga) {
		if (surface_state.native_set != LANDRU_SURFACE_SVGA ||
			!lbitmap_Alloc_System_Bitmap(&surface_state.vga_render, 320, 200) ||
			!lbitmap_Alloc_System_Bitmap(&surface_state.vga_diff, 320, 200) ||
			!(surface_state.vga_video = calloc(320u * 200u, 1))) {
			lbitmap_Free_Bitmap(&surface_state.vga_render);
			lbitmap_Free_Bitmap(&surface_state.vga_diff);
			free(surface_state.vga_video);
			surface_state.vga_video = NULL;
			return false;
		}
		/* Keep drawing and presentation separate so fades retain the previous
		 * frame until their copy step, as in the native VGA path. */
		vga_compat_buffer_gbl = surface_state.vga_video;
		surface_state.secondary_vga = true;
	}

	surface_state.initialized = true;
	return true;
}

void lsurface_Destroy_Surface_Module(void) {
	if (!surface_state.initialized)
		return;

	if (surface_state.active_set != surface_state.native_set)
		(void)lsurface_Select_Surface_Set(surface_state.native_set);
	lbitmap_Free_Bitmap(&surface_state.vga_diff);
	lbitmap_Free_Bitmap(&surface_state.vga_render);
	free(surface_state.vga_video);
	vga_compat_buffer_gbl = NULL;
	landru_video_flags_gbl &= (uint16_t)~LANDRU_VIDEO_VGA_COMPAT;
	memset(&surface_state, 0, sizeof surface_state);
}

bool lsurface_Has_Surface_Set(LandruSurfaceSet set) {
	if (!surface_state.initialized)
		return false;
	return set == surface_state.native_set || (set == LANDRU_SURFACE_VGA && surface_state.secondary_vga);
}

bool lsurface_Select_Surface_Set(LandruSurfaceSet set) {
	BitmapStruct* old_render;
	BitmapStruct* new_render;
	LandruSurfaceSet old_set;
	int16_t old_w;
	int16_t old_h;
	int16_t new_x;
	int16_t new_y;
	Rect bounds;

	if (!lsurface_Has_Surface_Set(set)) {
		landru_host_log(LANDRU_LOG_ERROR, "invalid Landru surface transition to %s\n", surface_name(set));
		return false;
	}
	if (set == surface_state.active_set) {
		BitmapStruct* active = render_for_set(set);
		if (active) {
			lrect_Set_Rect(&bounds, 0, 0, active->w, active->h);
			lio_Set_Mouse_Limits(&bounds);
		}
		return true;
	}

	old_set = surface_state.active_set;
	old_render = render_for_set(old_set);
	new_render = render_for_set(set);
	if (!old_render || !new_render || !lcanvas_Activate_Screen_Bitmap(old_render, new_render))
		return false;

	old_w = old_render->w;
	old_h = old_render->h;
	new_x = (int16_t)((int32_t)lio_Mouse_X() * new_render->w / old_w);
	new_y = (int16_t)((int32_t)lio_Mouse_Y() * new_render->h / old_h);
	if (new_x >= new_render->w)
		new_x = new_render->w - 1;
	if (new_y >= new_render->h)
		new_y = new_render->h - 1;

	surface_state.active_set = set;
	if (set == LANDRU_SURFACE_VGA && surface_state.secondary_vga)
		XBM_Set_VGA_Compatibility_Mode(1, 320, 200);
	else
		XBM_Set_VGA_Compatibility_Mode(0, new_render->w, new_render->h);
	lrect_Set_Rect(&bounds, 0, 0, new_render->w, new_render->h);
	lio_Set_Mouse_Limits(&bounds);
	lio_Set_Mouse_Position(new_x, new_y);
	lcanvas_Invalid_Screen_Diff();
	ldirty_Max_Dirty_List();

	if (set == LANDRU_SURFACE_VGA && surface_state.secondary_vga) {
		memset(surface_state.vga_video, 0, 320u * 200u);
		surface_state.vga_video_dirty = true;
	} else {
		vesa_dirty_gbl = true;
	}
	surface_state.generation++;
	landru_host_log(LANDRU_LOG_INFO, "Landru surface %s %dx%d -> %s %dx%d\n", surface_name(old_set), old_w,
					old_h, surface_name(set), new_render->w, new_render->h);
	return true;
}

LandruSurfaceSet lsurface_Get_Surface_Set(void) { return surface_state.active_set; }

BitmapStruct* lsurface_Get_Active_Render_Bitmap(void) { return render_for_set(surface_state.active_set); }

BitmapStruct* lsurface_Get_Active_Diff_Bitmap(void) { return diff_for_set(surface_state.active_set); }

bool lsurface_Get_Active_Video_Target(LandruVideoTarget* out) {
	if (!out)
		return false;
	memset(out, 0, sizeof *out);
	if (surface_state.initialized && surface_state.active_set == LANDRU_SURFACE_VGA &&
		surface_state.secondary_vga) {
		out->pixels = surface_state.vga_video;
		out->width = 320;
		out->height = 200;
		out->stride = 320;
		out->dirty = surface_state.vga_video_dirty;
	} else {
		out->pixels = vesa_buff_gbl;
		out->width = vesa_w_gbl;
		out->height = vesa_h_gbl;
		out->stride = vesa_bpsl_gbl;
		out->dirty = vesa_dirty_gbl;
	}
	lrect_Set_Rect(&out->bounds, 0, 0, out->width, out->height);
	out->generation = surface_state.generation;
	return out->pixels != NULL;
}

void lsurface_Get_Logical_Bounds(Rect* out) {
	BitmapStruct* render = lsurface_Get_Active_Render_Bitmap();
	if (!out)
		return;
	if (!render)
		lrect_Clear_Rect(out);
	else
		lrect_Set_Rect(out, 0, 0, render->w, render->h);
}

void lsurface_Invalidate_Presentation(void) {
	if (!surface_state.initialized)
		return;
	surface_state.generation++;
	lsurface_Mark_Active_Video_Dirty();
}

void lsurface_Mark_Active_Video_Dirty(void) {
	if (surface_state.initialized && surface_state.active_set == LANDRU_SURFACE_VGA &&
		surface_state.secondary_vga)
		surface_state.vga_video_dirty = true;
	else
		vesa_dirty_gbl = true;
}

bool lsurface_Take_Active_Video_Dirty(void) {
	bool dirty;
	if (surface_state.initialized && surface_state.active_set == LANDRU_SURFACE_VGA &&
		surface_state.secondary_vga) {
		dirty = surface_state.vga_video_dirty;
		surface_state.vga_video_dirty = false;
	} else {
		dirty = vesa_dirty_gbl;
		vesa_dirty_gbl = false;
	}
	return dirty;
}

uint32_t lsurface_Get_Presentation_Generation(void) { return surface_state.generation; }
