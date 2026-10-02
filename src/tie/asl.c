// FLAGS: TIE95 -d2
#include "tie/asl.h"
#include "landru/actanim.h"
#include "landru/actcust.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/btnchk.h"
#include "landru/btnpush.h"
#include "landru/btnsldr.h"
#include "landru/btnstr.h"
#include "landru/btntext.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/dialog.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/fade.h"
#include "landru/filedir.h"
#include "landru/font.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/mouse.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/remap.h"
#include "landru/res.h"
#include "landru/timer.h"
#include "landru/vesa.h"
#include "landru/view.h"

#include "landru/surface.h"
#include "tie/gamesnd.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

/* Create Landru modules in dependency order. */
// FUNCTION: TIE95 0x86C05
void asl_Open_ASL(void) {
	const TieFrontendProfile* profile = TieProfile_Frontend();
	Rect canvas_bounds;
	TieDiagnostics_Log(TIE_LOG_INFO, "[ASL] frontend VESA=0x%X canvas=%dx%d scratch=0x%X fonts=%u\n",
					   profile->vesa_mode, profile->width, profile->height,
					   (unsigned)(uint16_t)profile->scratch_size, profile->font_count);

	xerror_Set_Landru_Bail_Function(asl_Case_Bail);
	if (!TieClassicDisplay_InitializeFrontend()) {
		xerror_Set_Landru_Error(12);
		return;
	}
	xvesa_Create_Vesa_Module(profile->vesa_mode);
	xmouse_MS_Initialize_Mouse();
	xtimer_Create_Timer_Interrupt();
	xio_Create_IO_Module();
	xres_Create_Resource_Module();
	xpal_Create_Palette_Module();
	xremap_Create_Remap_Module();
	xfade_Create_Fade_Module();
	xcursor_Create_Cursor_Module();
	xdirty_Create_Dirty_List_Module(64);
	xcanvas_Create_Canvas_Module(profile->scratch_size, profile->width, profile->height, true);
	if (!xsurface_Create_Surface_Module(profile->secondary_vga)) {
		xerror_Set_Landru_Error(12);
		return;
	}
	xinput_Create_Input_Module();
	xdialog_Create_Dialog_Module();
	xbtnpush_Create_Button_Module();
	xbtnstr_Create_String_Button_Module();
	xbtnsldr_Create_Slider_Button_Module();
	xbtntext_Create_Text_Button_Module();
	xbtnchk_Create_Check_Button_Module();
	xactor_Create_Actor_Module();
	xactanim_Create_Anim_Actor_Module();
	xactdelt_Create_Delta_Actor_Module();
	xactcust_Create_Custom_Actor_Module();
	xfont_Create_Font_Module();
	xview_Create_View_Module();
	xfiledir_Create_Directory_Module();
	xcanvas_Erase_Canvas();
	xio_Flush_Input();
	xcanvas_Get_Drawing_Canvas_Bounds(&canvas_bounds);
	xdirty_Dirty_Master_Rect(&canvas_bounds);
	xdirty_Set_Dirty_Merge();
	xdirty_Max_Dirty_List();
}

/* Shut down all Landru modules in reverse creation order. */
// FUNCTION: TIE95 0x86D22
void asl_Close_ASL(void) {
	xfiledir_Destroy_Directory_Module();
	xview_Destroy_View_Module();
	xfont_Destroy_Font_Module();
	xactcust_Destroy_Custom_Actor_Module();
	xactdelt_Destroy_Delta_Actor_Module();
	xactanim_Destroy_Anim_Actor_Module();
	xactor_Destroy_Actor_Module();
	xbtnchk_Destroy_Check_Button_Module();
	xbtntext_Destroy_Text_Button_Module();
	xbtnsldr_Destroy_Slider_Button_Module();
	xbtnstr_Destroy_String_Button_Module();
	xbtnpush_Destroy_Button_Module();
	xdialog_Destroy_Dialog_Module();
	xinput_Destroy_Input_Module();
	xsurface_Destroy_Surface_Module();
	xcanvas_Destroy_Canvas_Module();
	xdirty_Destroy_Dirty_List_Module();
	xcursor_Destroy_Cursor_Module();
	xfade_Destroy_Fade_Module();
	xremap_Destroy_Remap_Module();
	xpal_Destroy_Palette_Module();
	xres_Destroy_Resource_Module();
	xio_Destroy_IO_Module();
	xtimer_Destroy_Timer_Interrupt();
	xvesa_Destroy_VESA_Module();
}

/* Emergency teardown, including iMUSE cleanup. */
// FUNCTION: TIE95 0x86DC3
int asl_Case_Bail(void) {
	xfiledir_Destroy_Directory_Module();
	xview_Destroy_View_Module();
	xfont_Destroy_Font_Module();
	xactcust_Destroy_Custom_Actor_Module();
	xactdelt_Destroy_Delta_Actor_Module();
	xactanim_Destroy_Anim_Actor_Module();
	xactor_Destroy_Actor_Module();
	xbtnchk_Destroy_Check_Button_Module();
	xbtntext_Destroy_Text_Button_Module();
	xbtnsldr_Destroy_Slider_Button_Module();
	xbtnstr_Destroy_String_Button_Module();
	xbtnpush_Destroy_Button_Module();
	xdialog_Destroy_Dialog_Module();
	xinput_Destroy_Input_Module();
	xsurface_Destroy_Surface_Module();
	xcanvas_Destroy_Canvas_Module();
	xdirty_Destroy_Dirty_List_Module();
	xcursor_Destroy_Cursor_Module();
	xfade_Destroy_Fade_Module();
	xremap_Destroy_Remap_Module();
	xpal_Destroy_Palette_Module();
	xres_Destroy_Resource_Module();
	xio_Destroy_IO_Module();
	xtimer_Destroy_Timer_Interrupt();
	xvesa_Destroy_VESA_Module();
	gamesnd_Close_Pre_iMuse();
	return 1;
}
