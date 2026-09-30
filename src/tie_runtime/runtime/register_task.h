#ifndef TIE_REGISTER_TASK_H
#define TIE_REGISTER_TASK_H

#include "tie/shellext.h"
#include <landru/input.h>
#include <stdbool.h>
#include <stdint.h>

void TieRegister_Begin(SceneHeadStruct* head);

void TieRegister_RunView(ResFile* resource, bool protect, const char* missing_resource);

/* Optional copy-protection challenge (retail leaves it disabled). */
void TieRegister_SetCopyProtection(bool enabled);
bool TieRegister_CopyProtectionEnabled(void);

/* register_Do_Protect_Dialog continuation: RunProtect pushes the dialog
 * view; ResumeProtect returns it when the register task resumes. */
void TieRegister_RunProtect(Input* dialog);
bool TieRegister_ResumeProtect(Input** dialog);

/* Delete-pilot confirmation: BeginDelete records the pilot button whose
 * callback asks, RunDelete schedules the dialog, and the callback is
 * re-entered with the dialog result once it closes. */
void TieRegister_BeginDelete(Input* owner);
void TieRegister_RunDelete(Input* dialog, int16_t key_buttons);
bool TieRegister_ResumeDelete(Input** dialog, int16_t* key_buttons, int16_t* result);
bool TieRegister_IsResumingDelete(void);

#endif
