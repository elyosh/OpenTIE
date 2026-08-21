#include "tie_runtime/display/classic_framebuffer.h"

#include "tie/frontend_display_tie98.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"

#include <landru/surface.h>
#include <landru/vesa.h>

#include <string.h>

enum { TIE_PRESENTED_VGA_WIDTH = 320, TIE_PRESENTED_VGA_HEIGHT = 200 };

static uint8_t s_palette[256 * 3];
static TieFramebuffer s_framebuffer;
static uint8_t s_presented_vga_pixels[TIE_PRESENTED_VGA_WIDTH * TIE_PRESENTED_VGA_HEIGHT];
static TieFramebuffer s_presented_vga_framebuffer;
static uint32_t s_presented_vga_surface_generation;
static bool s_presented_vga_valid;

bool TieClassicFramebuffer_TakeDirty(void) { return lsurface_Take_Active_Video_Dirty(); }

const TieFramebuffer* TieClassicFramebuffer_Current(void) {
	LandruVideoTarget target;
	if (TieClassicDisplay_FrontendActive() && lsurface_Get_Active_Video_Target(&target)) {
		s_framebuffer.pixels = target.pixels;
		s_framebuffer.width = target.width;
		s_framebuffer.height = target.height;
		s_framebuffer.pitch = target.stride;
		s_framebuffer.generation = target.generation;
	} else {
		s_framebuffer.pixels = vesa_buff_gbl;
		s_framebuffer.width = vesa_w_gbl;
		s_framebuffer.height = vesa_h_gbl;
		s_framebuffer.pitch = vesa_bpsl_gbl;
		s_framebuffer.generation = 0;
	}
	s_framebuffer.palette = s_palette;
	return &s_framebuffer;
}

void TieClassicFramebuffer_CapturePresentedVga(void) {
	LandruVideoTarget target;
	s_presented_vga_valid = false;
	if (!TieClassicDisplay_FrontendActive() || TieProfile_FrontendId() != TIE_FRONTEND_PROFILE_TIE98 ||
		!(landru_video_flags_gbl & LANDRU_VIDEO_VGA_COMPAT) || !lsurface_Get_Active_Video_Target(&target) ||
		!target.pixels || target.width != TIE_PRESENTED_VGA_WIDTH ||
		target.height != TIE_PRESENTED_VGA_HEIGHT || target.stride < TIE_PRESENTED_VGA_WIDTH)
		return;
	for (int y = 0; y < TIE_PRESENTED_VGA_HEIGHT; ++y)
		memcpy(&s_presented_vga_pixels[y * TIE_PRESENTED_VGA_WIDTH], &target.pixels[y * target.stride],
			   TIE_PRESENTED_VGA_WIDTH);
	s_presented_vga_surface_generation = target.generation;
	s_presented_vga_valid = true;
}

void TieClassicFramebuffer_InvalidatePresentedVga(void) { s_presented_vga_valid = false; }

const TieFramebuffer* TieClassicFramebuffer_PresentedVga(void) {
	if (!s_presented_vga_valid || !TieClassicDisplay_FrontendActive() ||
		TieProfile_FrontendId() != TIE_FRONTEND_PROFILE_TIE98)
		return NULL;
	s_presented_vga_framebuffer = (TieFramebuffer) {
		.pixels = s_presented_vga_pixels,
		.width = TIE_PRESENTED_VGA_WIDTH,
		.height = TIE_PRESENTED_VGA_HEIGHT,
		.pitch = TIE_PRESENTED_VGA_WIDTH,
		.generation = s_presented_vga_surface_generation,
		.palette = s_palette,
	};
	return &s_presented_vga_framebuffer;
}

void TieClassicFramebuffer_SetPalette(const uint8_t* rgb, int start, int count) {
	if (!rgb || count <= 0 || start >= 256)
		return;
	if (start < 0)
		start = 0;
	if (count > 256 - start)
		count = 256 - start;
	memcpy(&s_palette[start * 3], rgb, (size_t)count * 3u);
	if (TieClassicDisplay_UsesDx5())
		FrontendDisplay_UpdatePalette(s_palette, 0, 256);
}
