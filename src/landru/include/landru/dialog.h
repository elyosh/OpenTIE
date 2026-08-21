#ifndef LANDRU_DIALOG_H
#define LANDRU_DIALOG_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/input.h>
#include <landru/rect.h>

typedef void (*DialogUpdateFunc)(int32_t frame);

void ldialog_Create_Dialog_Module(void);
void ldialog_Destroy_Dialog_Module(void);
void ldialog_Add_Dialog_To_System(Input* list);
void ldialog_Remove_Dialog_From_System(Input* ptr);
void ldialog_Draw_System_Dialogs(int16_t refresh);

/* Push the dialog task on the tie_core stack and run the prologue
 * setup; caller-task should yield (CONTINUE) and read
 * ldialog_Get_Dialog_Exit() on the next step after our task pops.
 * From inside an input callback, use ldialog_Schedule_Sub_Dialog
 * instead. */
void ldialog_Push_Dialog_View_Task(Input* list);
void ldialog_Update_Dialog_End_View(int32_t frame);
void ldialog_Save_Dialog_Background(void);

/* --- Deferred sub-dialog channel ---------------------------------
 *
 * Input callbacks (iuser/iupdate) run synchronously inside the owning
 * DialogTask::step and cannot push tasks themselves: returning from
 * the callback returns to linpcall_User_Inputs, then to the
 * DialogTask step, with no provision for resuming after a yielded
 * sub-task.
 *
 * To open a modal sub-dialog from inside a callback, the callback
 * stages a request via ldialog_Schedule_Sub_Dialog. After the
 * callback batch returns, the DialogTask picks up the request:
 * pushes the sub-dialog as a nested DialogTask and yields. When the
 * sub-dialog pops, `handler(result, ctx)` runs synchronously to
 * execute the post-result logic that used to follow the inline
 * synchronous ldialog_Handle_Dialog_View call.
 *
 * The channel is single-slot — only one sub-dialog can be pending
 * at a time. Subsequent calls in the same tick assert. */
typedef void (*DialogSubResultHandler)(int16_t result, void* ctx);

void ldialog_Schedule_Sub_Dialog(Input* sub_dlg, DialogSubResultHandler handler, void* ctx);

/* Drain primitives for owning task steps (DialogTask, ViewAddTask).
 * After processing a callback batch, the owning step calls
 * ldialog_Try_Push_Pending_Sub_Dialog: if a callback staged a
 * request, the sub-dialog is pushed onto the tie_core task stack and
 * the function returns 1. The caller retains the returned handler and
 * context while suspended, then invokes them after the sub-dialog pops. */
int ldialog_Try_Push_Pending_Sub_Dialog(DialogSubResultHandler* out_handler, void** out_ctx);
void ldialog_Invoke_Sub_Handler(DialogSubResultHandler handler, void* ctx);
void ldialog_Clear_Dialog_Exit(void);
int16_t ldialog_Get_Dialog_Exit(void);
void ldialog_Set_Dialog_Exit(int16_t code);
bool ldialog_Is_Dialog_Exit(void);
bool ldialog_Is_Dialog_Running(void);
bool ldialog_Is_Active_Dialog(void);
void ldialog_Set_Dialog_Update_Function(DialogUpdateFunc fn);
DialogUpdateFunc ldialog_Get_Dialog_Update_Function(void);
void ldialog_Clear_Dialog_Update_Function(void);
void ldialog_Refresh_Active_Dialog(void);

#endif
