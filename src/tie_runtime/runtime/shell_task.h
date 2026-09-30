#ifndef TIE_RUNTIME_RUNTIME_SHELL_TASK_H
#define TIE_RUNTIME_RUNTIME_SHELL_TASK_H

#include "tie/shellext.h"
#include <stdbool.h>
typedef enum ShellPhase {
	SHELL_PHASE_DISPATCH,
	SHELL_PHASE_FLIGHT_BEGIN,
	SHELL_PHASE_AFTER_FLIGHT,
	SHELL_PHASE_AWAITING,
	SHELL_PHASE_AFTER_SUDDEN_FADE,
} ShellPhase;
typedef struct ShellTask {
	SceneHeadStruct the_head;
	int16_t cur_scene;
	int16_t next_scene;
	int16_t exit_flag;
	int16_t script;
	bool diff_disabled;
	bool owns_tie98_music;
	bool started;
	bool finished;
	ShellPhase phase;
} ShellTask;
bool TieShell_PrepareScene(int16_t scene);
/* Push the sudden-scene-end fade; the caller yields until it completes. */
void TieShell_PushSuddenSceneFadeTask(void);

#include <stdint.h>

void TieShell_Begin(int16_t scene, int16_t script);

#endif
