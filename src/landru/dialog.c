#include <stddef.h>

#include <landru/canvas.h>
#include <landru/cursor.h>
#include <landru/dialog.h>
#include <landru/dirty.h>
#include <landru/error.h>
#include <landru/fade.h>
#include <landru/inpcall.h>
#include <landru/input.h>
#include <landru/io.h>
#include <landru/pal.h>
#include <landru/rect.h>
#include <landru/task.h>
#include <landru/timer.h>
#include <landru/vesa.h>
#include <landru/view.h>
#include <landru/viewadd.h>

#include "host_internal.h"

// GLOBAL: TIE 0xD2F5C
static int16_t dialog_module_gbl;
// GLOBAL: TIE 0xD2F5E
static int16_t dlg_exit_gbl;
static int16_t dlg_back_gbl;
// GLOBAL: TIE 0xD3016
static int16_t dlg_enable_gbl;
// GLOBAL: TIE 0xFBCD4
static DialogUpdateFunc dlg_update_gbl;
// GLOBAL: TIE 0xFBCD8
static Input* dialog_list_gbl;
// GLOBAL: TIE 0xFBCDC
static int16_t refresh_dialogs_gbl;

void ldialog_Create_Dialog_Module(void) {
	dialog_list_gbl = NULL;
	refresh_dialogs_gbl = 0;
	dlg_update_gbl = NULL;
	dialog_module_gbl = 1;
}

void ldialog_Destroy_Dialog_Module(void) { dialog_module_gbl = 0; }

void ldialog_Add_Dialog_To_System(Input* list) {
	list->next = NULL;

	if (dialog_list_gbl) {
		Input* tail = dialog_list_gbl;
		while (tail->next)
			tail = tail->next;
		tail->next = list;
	} else {
		dialog_list_gbl = list;
	}

	linput_Set_Active_Input_List(list);
}

void ldialog_Remove_Dialog_From_System(Input* ptr) {
	Input* cur = dialog_list_gbl;
	Input* prev = NULL;

	while (cur) {
		if (cur == ptr)
			break;
		prev = cur;
		cur = cur->next;
	}

	if (cur == ptr) {
		if (prev)
			prev->next = ptr->next;
		else
			dialog_list_gbl = ptr->next;
		ptr->next = NULL;
	}

	/* Re-walk to find the new tail and make it the active list */
	Input* tail = dialog_list_gbl;
	if (tail) {
		while (tail->next)
			tail = tail->next;
	}
	linput_Set_Active_Input_List(tail);
}

void ldialog_Draw_System_Dialogs(int16_t refresh) {
	linpcall_Draw_Inputs(dialog_list_gbl, NULL, NULL, refresh);
}

/* ------------------------------------------------------------------
 * Dialog modal loop — fully task-driven.
 *
 * Persistent modal state lives in DialogTask. Sub-modal dialogs push their
 * own task on top — the runner always steps the top of the stack,
 * so nesting falls out naturally. Sub-dialog opens from inside an
 * input callback go through ldialog_Schedule_Sub_Dialog (see
 * dialog.h) — the channel is drained at the end of the DRAW phase
 * and the sub-dialog is pushed on top with a handler that runs once
 * the sub-dialog pops.
 * ------------------------------------------------------------------ */

typedef enum {
	DIALOG_PHASE_DRAW = 0,
	DIALOG_PHASE_WAIT,
	DIALOG_PHASE_AFTER_FRAME_FADE,
	DIALOG_PHASE_AFTER_FRAME_PRESENT,
	DIALOG_PHASE_PUSH_SUB,  /* sub-dialog scheduled; push and yield */
	DIALOG_PHASE_AFTER_SUB, /* sub-dialog popped; invoke handler */
	DIALOG_PHASE_TEARDOWN,
	DIALOG_PHASE_DONE,
} DialogPhase;

typedef struct DialogTask {
	Input* list;
	int32_t frame_counter;
	Rect saved_clip;
	Rect saved_input_frame;
	DialogSubResultHandler sub_handler;
	void* sub_ctx;
	int16_t saved_cursor_visible;
	DialogPhase phase;
} DialogTask;

/* Sub-dialog request channel — see ldialog_Schedule_Sub_Dialog in
 * dialog.h. Single-slot; the DialogTask drains it after each
 * callback batch. The handler/ctx pair is captured into per-task
 * fields below at the moment the sub-dialog is pushed, so a fresh
 * Schedule from inside the sub-dialog's own callback batch can land
 * cleanly without overwriting the parent's pending handler. */
static Input* s_sub_dlg_pending;
static DialogSubResultHandler s_sub_handler_pending;
static void* s_sub_ctx_pending;

void ldialog_Schedule_Sub_Dialog(Input* sub_dlg, DialogSubResultHandler handler, void* ctx) {
	/* If multiple callbacks queue in one tick, the latest wins.
	 * Asserting at this layer is overkill — the guarantee callers
	 * rely on is "the next sub-dialog opens", and the staging order
	 * within a tick is undefined anyway. */
	s_sub_dlg_pending = sub_dlg;
	s_sub_handler_pending = handler;
	s_sub_ctx_pending = ctx;
}

int ldialog_Try_Push_Pending_Sub_Dialog(DialogSubResultHandler* out_handler, void** out_ctx) {
	if (!s_sub_dlg_pending)
		return 0;

	Input* sub = s_sub_dlg_pending;
	*out_handler = s_sub_handler_pending;
	*out_ctx = s_sub_ctx_pending;
	s_sub_dlg_pending = NULL;
	s_sub_handler_pending = NULL;
	s_sub_ctx_pending = NULL;

	ldialog_Push_Dialog_View_Task(sub);
	return 1;
}

void ldialog_Invoke_Sub_Handler(DialogSubResultHandler handler, void* ctx) {
	int16_t result = dlg_exit_gbl;
	dlg_exit_gbl = 0;
	if (handler)
		handler(result, ctx);
}

/* Push the per-frame "Fade_Screen_To_Video(1)" task. Caller-task
 * yields after this; the FadeTask's end callback runs cursor + present
 * synchronously when it pops. Used by WAIT and TEARDOWN. */
static void dialog_push_frame_fade(void) {
	Rect bounds;
	lrect_Set_Rect(&bounds, 0, 0, draw_bm_gbl->w, draw_bm_gbl->h);
	bool cursor_was_visible = lcursor_Is_Cursor_Visible();
	if (cursor_was_visible)
		lcursor_Cursor_To_Back();

	(void)lfade_Push_Fade_To_Video_Screen_Task(
		&bounds, /*is_dialog=*/1, cursor_was_visible ? FADE_END_CURSOR_TO_FRONT : FADE_END_CURSOR_FROM_FADE,
		/*force_refresh_view=*/false);
}

/* Restore-Background body, minus the trailing Fade_Screen_To_Video
 * which is pushed by the caller as a separate step. */
static void dialog_restore_background_no_fade(void) {
	if (lcanvas_Is_Screen_Diff()) {
		dlg_back_gbl--;
		lcanvas_Copy_Diff_To_Screen();
		if (dlg_back_gbl)
			linpcall_Draw_Inputs(dialog_list_gbl, NULL, NULL, 1);
		else if (dlg_enable_gbl)
			lcanvas_Enable_Screen_Diff();
	} else {
		lviewadd_Clear_View();
		lviewadd_Draw_View(1);
		lviewadd_Draw_View_Debug(view_gbl->time);
	}
	ldirty_Max_Dirty_List();
}

static LandruTaskStepResult dialog_task_step(void* self) {
	DialogTask* t = (DialogTask*)self;

	switch (t->phase) {

		case DIALOG_PHASE_DRAW:
			if (!lerror_Is_Landru_Running() || ldialog_Is_Dialog_Exit()) {
				t->phase = DIALOG_PHASE_TEARDOWN;
				return LANDRU_TASK_STEP_CONTINUE;
			}

			lio_Poll_Input();

			if (!view_gbl->step) {
				linpcall_Update_Inputs(t->list);
				linpcall_User_Inputs(t->list, t->frame_counter);
			}

			/* Drain any sub-dialog request a callback staged this tick.
			 * Transition to PUSH_SUB before drawing — the sub-dialog
			 * handles its own draw on the next step. */
			if (ldialog_Try_Push_Pending_Sub_Dialog(&t->sub_handler, &t->sub_ctx)) {
				t->phase = DIALOG_PHASE_AFTER_SUB;
				return LANDRU_TASK_STEP_CONTINUE;
			}

			if (view_gbl->refresh_world) {
				lviewadd_Clear_View();
				lviewadd_Draw_View(view_gbl->refresh_world);
				view_gbl->refresh_world = 0;
			} else {
				linpcall_Draw_Inputs(t->list, NULL, NULL, refresh_dialogs_gbl);
				refresh_dialogs_gbl = 0;
			}

			lviewadd_Draw_View_Debug(t->frame_counter);

			t->phase = DIALOG_PHASE_WAIT;
			return LANDRU_TASK_STEP_CONTINUE;

		case DIALOG_PHASE_PUSH_SUB:
			/* Unused — DRAW transitions directly to AFTER_SUB after
			 * pushing via ldialog_Try_Push_Pending_Sub_Dialog. Kept
			 * for the enum's exhaustiveness. */
			t->phase = DIALOG_PHASE_DRAW;
			return LANDRU_TASK_STEP_CONTINUE;

		case DIALOG_PHASE_AFTER_SUB:
			/* Sub-dialog popped. Invoke the handler, return to DRAW so
			 * the parent dialog repaints itself. dlg_exit_gbl is
			 * cleared by the helper. */
			ldialog_Invoke_Sub_Handler(t->sub_handler, t->sub_ctx);
			t->sub_handler = NULL;
			t->sub_ctx = NULL;
			t->phase = DIALOG_PHASE_DRAW;
			return LANDRU_TASK_STEP_CONTINUE;

		case DIALOG_PHASE_WAIT:
			if (!lerror_Is_Landru_Running() || ldialog_Is_Dialog_Exit()) {
				t->phase = DIALOG_PHASE_TEARDOWN;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			if (ltimer_Frame_Budget_Pending())
				return LANDRU_TASK_STEP_YIELD;
			ltimer_Commit_Frame();
			dialog_push_frame_fade();
			t->phase = DIALOG_PHASE_AFTER_FRAME_FADE;
			return LANDRU_TASK_STEP_CONTINUE;

		case DIALOG_PHASE_AFTER_FRAME_FADE:
			/* WAIT pushed a fade; FadeTask has popped (else we wouldn't
			 * be running). Run the post-fade work and return to DRAW. */
			if (!view_gbl->step) {
				if (dlg_update_gbl)
					dlg_update_gbl(t->frame_counter);
				t->frame_counter++;
			}
			(void)landru_port_Present_Platform_Video();
			t->phase = DIALOG_PHASE_AFTER_FRAME_PRESENT;
			return LANDRU_TASK_STEP_FRAME_COMPLETE;

		case DIALOG_PHASE_AFTER_FRAME_PRESENT:
			t->phase = DIALOG_PHASE_DRAW;
			return LANDRU_TASK_STEP_CONTINUE;

		case DIALOG_PHASE_TEARDOWN:
			/* Dismiss after the trailing fade completes. */
			lio_Poll_Input();
			linpcall_Clear_Active_Input();
			ldialog_Remove_Dialog_From_System(t->list);
			linput_Set_System_Input_Frame(&t->saved_input_frame);

			if (lcanvas_Is_Screen_Diff()) {
				lview_Restore_Full_View_Clip_Frame();
			} else {
				Rect tmp_clip;
				lview_Get_Full_View_Clip_Frame(&tmp_clip);
				lrect_Enclose_Rect(&tmp_clip, &t->saved_clip);
				lview_Set_Full_View_Clip_Frame(&tmp_clip);
				ldirty_Max_Dirty_List();
			}
			dialog_restore_background_no_fade();
			lview_Set_Full_View_Clip_Frame(&t->saved_clip);
			lcanvas_Invalid_Screen_Diff();

			if (!t->saved_cursor_visible)
				lcursor_Hide_Cursor();

			dialog_push_frame_fade();
			t->phase = DIALOG_PHASE_DONE;
			return LANDRU_TASK_STEP_CONTINUE;

		case DIALOG_PHASE_DONE:
			/* Dismissal fade has popped — we're finished. */
			return LANDRU_TASK_STEP_DONE;
	}

	return LANDRU_TASK_STEP_DONE;
}

static uint64_t dialog_task_next_wake_delay_us(const void* self) {
	const DialogTask* t = (const DialogTask*)self;
	if (t->phase == DIALOG_PHASE_WAIT || t->phase == DIALOG_PHASE_AFTER_FRAME_PRESENT)
		return ltimer_Next_Frame_Delay_Us();
	return 0;
}

static const LandruTaskVtable dialog_task_vt = {
	.step = dialog_task_step,
	.next_wake_delay_us = dialog_task_next_wake_delay_us,
};

/* Push a DialogTask onto the tie_core task stack and run the prologue
 * setup. Caller provides the dialog Input* list; once the task
 * pops, the dialog exit code is in dlg_exit_gbl (see
 * ldialog_Get_Dialog_Exit). The caller is itself expected to be a
 * task: push us, return CONTINUE, then read dlg_exit_gbl on the
 * next step (or use ldialog_Schedule_Sub_Dialog from inside an
 * input callback). */
void ldialog_Push_Dialog_View_Task(Input* list) {
	DialogTask* t = (DialogTask*)landru_task_push(&dialog_task_vt);
	if (!t)
		return;

	t->list = list;
	t->frame_counter = 0;
	t->sub_handler = NULL;
	t->sub_ctx = NULL;
	t->phase = DIALOG_PHASE_DRAW;

	lio_Flush_Input();
	t->saved_cursor_visible = lcursor_Is_Cursor_Visible();
	if (!t->saved_cursor_visible)
		lcursor_Show_Cursor();

	lpal_Cycles_To_Start();
	dlg_exit_gbl = 0;

	ldialog_Add_Dialog_To_System(list);

	lview_Get_Full_View_Clip_Frame(&t->saved_clip);

	Rect frame, clip;
	linput_Get_System_Input_Frame(&t->saved_input_frame);
	lcanvas_Get_Drawing_Canvas_Bounds(&frame);
	linput_Set_System_Input_Frame(&frame);
	linpcall_Get_Input_Parent_Frame(NULL, NULL, &frame, &clip);
	linpcall_Clip_Input_To_Frame(list, &frame, &clip);
	lview_Set_Full_View_Clip_Frame(&clip);

	ldialog_Save_Dialog_Background();
	refresh_dialogs_gbl = 1;
}

void ldialog_Update_Dialog_End_View(int32_t frame) {
	if (dlg_update_gbl)
		dlg_update_gbl(frame);
}

void ldialog_Save_Dialog_Background(void) {
	if (lcanvas_Is_Screen_Diff()) {
		if (!dlg_back_gbl) {
			dlg_enable_gbl = lcanvas_Is_Screen_Diff_Used();
			if (dlg_enable_gbl)
				lcanvas_Disable_Screen_Diff();
			lcanvas_Copy_Screen_To_Diff();
		}
		dlg_back_gbl++;
	} else {
		lcanvas_Invalid_Screen_Diff();
		lview_Refresh_View();
	}
}

/* --- Simple accessors --- */

void ldialog_Clear_Dialog_Exit(void) { dlg_exit_gbl = 0; }
int16_t ldialog_Get_Dialog_Exit(void) { return dlg_exit_gbl; }
void ldialog_Set_Dialog_Exit(int16_t code) { dlg_exit_gbl = code; }
bool ldialog_Is_Dialog_Exit(void) { return dlg_exit_gbl != 0; }

bool ldialog_Is_Dialog_Running(void) { return lerror_Is_Landru_Running() && !ldialog_Is_Dialog_Exit(); }

bool ldialog_Is_Active_Dialog(void) { return dialog_list_gbl != NULL; }

void ldialog_Set_Dialog_Update_Function(DialogUpdateFunc fn) { dlg_update_gbl = fn; }
DialogUpdateFunc ldialog_Get_Dialog_Update_Function(void) { return dlg_update_gbl; }
void ldialog_Clear_Dialog_Update_Function(void) { dlg_update_gbl = NULL; }
void ldialog_Refresh_Active_Dialog(void) { refresh_dialogs_gbl = 1; }
