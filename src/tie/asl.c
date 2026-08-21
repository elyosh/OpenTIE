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
// FUNCTION: TIE 0x86C05
void asl_Open_ASL(void) {
	const TieFrontendProfile* profile = TieProfile_Frontend();
	Rect canvas_bounds;
	TieDiagnostics_Log(TIE_LOG_INFO, "[ASL] frontend VESA=0x%X canvas=%dx%d scratch=0x%X fonts=%u\n",
					   profile->vesa_mode, profile->width, profile->height,
					   (unsigned)(uint16_t)profile->scratch_size, profile->font_count);

	lerror_Set_Landru_Bail_Function(asl_Case_Bail);
	if (!TieClassicDisplay_InitializeFrontend()) {
		lerror_Set_Landru_Error(12);
		return;
	}
	lvesa_Create_Vesa_Module(profile->vesa_mode);
	lmouse_MS_Initialize_Mouse();
	ltimer_Create_Timer_Interrupt();
	lio_Create_IO_Module();
	lres_Create_Resource_Module();
	lpal_Create_Palette_Module();
	lremap_Create_Remap_Module();
	lfade_Create_Fade_Module();
	lcursor_Create_Cursor_Module();
	ldirty_Create_Dirty_List_Module(64);
	lcanvas_Create_Canvas_Module(profile->scratch_size, profile->width, profile->height, true);
	if (!lsurface_Create_Surface_Module(profile->secondary_vga)) {
		lerror_Set_Landru_Error(12);
		return;
	}
	linput_Create_Input_Module();
	ldialog_Create_Dialog_Module();
	lbtnpush_Create_Button_Module();
	lbtnstr_Create_String_Button_Module();
	lbtnsldr_Create_Slider_Button_Module();
	lbtntext_Create_Text_Button_Module();
	lbtnchk_Create_Check_Button_Module();
	lactor_Create_Actor_Module();
	lactanim_Create_Anim_Actor_Module();
	lactdelt_Create_Delta_Actor_Module();
	lactcust_Create_Custom_Actor_Module();
	lfont_Create_Font_Module();
	lview_Create_View_Module();
	lfiledir_Create_Directory_Module();
	lcanvas_Erase_Canvas();
	lio_Flush_Input();
	lcanvas_Get_Drawing_Canvas_Bounds(&canvas_bounds);
	ldirty_Dirty_Master_Rect(&canvas_bounds);
	ldirty_Set_Dirty_Merge();
	ldirty_Max_Dirty_List();
}

/* Shut down all Landru modules in reverse creation order. */
// FUNCTION: TIE 0x86D22
void asl_Close_ASL(void) {
	lfiledir_Destroy_Directory_Module();
	lview_Destroy_View_Module();
	lfont_Destroy_Font_Module();
	lactcust_Destroy_Custom_Actor_Module();
	lactdelt_Destroy_Delta_Actor_Module();
	lactanim_Destroy_Anim_Actor_Module();
	lactor_Destroy_Actor_Module();
	lbtnchk_Destroy_Check_Button_Module();
	lbtntext_Destroy_Text_Button_Module();
	lbtnsldr_Destroy_Slider_Button_Module();
	lbtnstr_Destroy_String_Button_Module();
	lbtnpush_Destroy_Button_Module();
	ldialog_Destroy_Dialog_Module();
	linput_Destroy_Input_Module();
	lsurface_Destroy_Surface_Module();
	lcanvas_Destroy_Canvas_Module();
	ldirty_Destroy_Dirty_List_Module();
	lcursor_Destroy_Cursor_Module();
	lfade_Destroy_Fade_Module();
	lremap_Destroy_Remap_Module();
	lpal_Destroy_Palette_Module();
	lres_Destroy_Resource_Module();
	lio_Destroy_IO_Module();
	ltimer_Destroy_Timer_Interrupt();
	lvesa_Destroy_VESA_Module();
}

/* Emergency teardown, including iMUSE cleanup. */
// FUNCTION: TIE 0x86DC3
int asl_Case_Bail(void) {
	lfiledir_Destroy_Directory_Module();
	lview_Destroy_View_Module();
	lfont_Destroy_Font_Module();
	lactcust_Destroy_Custom_Actor_Module();
	lactdelt_Destroy_Delta_Actor_Module();
	lactanim_Destroy_Anim_Actor_Module();
	lactor_Destroy_Actor_Module();
	lbtnchk_Destroy_Check_Button_Module();
	lbtntext_Destroy_Text_Button_Module();
	lbtnsldr_Destroy_Slider_Button_Module();
	lbtnstr_Destroy_String_Button_Module();
	lbtnpush_Destroy_Button_Module();
	ldialog_Destroy_Dialog_Module();
	linput_Destroy_Input_Module();
	lsurface_Destroy_Surface_Module();
	lcanvas_Destroy_Canvas_Module();
	ldirty_Destroy_Dirty_List_Module();
	lcursor_Destroy_Cursor_Module();
	lfade_Destroy_Fade_Module();
	lremap_Destroy_Remap_Module();
	lpal_Destroy_Palette_Module();
	lres_Destroy_Resource_Module();
	lio_Destroy_IO_Module();
	ltimer_Destroy_Timer_Interrupt();
	lvesa_Destroy_VESA_Module();
	gamesnd_Close_Pre_iMuse();
	return 1;
}
