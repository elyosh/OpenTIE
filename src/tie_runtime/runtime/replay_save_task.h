#ifndef TIE_REPLAY_SAVE_TASK_H
#define TIE_REPLAY_SAVE_TASK_H
#include <stdbool.h>
#include <stdint.h>
enum { TIE_REPLAY_SAVE_PENDING = UINT16_MAX };
typedef enum ReplaySavePhase {
	REPLAY_SAVE_PHASE_BEGIN = 0,
	REPLAY_SAVE_PHASE_EDIT_NAME,
	REPLAY_SAVE_PHASE_CHECK_FILE,
	REPLAY_SAVE_PHASE_CONFIRM_REPLACE,
	REPLAY_SAVE_PHASE_WRITE,
	REPLAY_SAVE_PHASE_FINISH,
} ReplaySavePhase;
typedef struct ReplaySaveTask {
	uint8_t name_input[40];
	char filename[16];
	uint16_t result;
	uint8_t position;
	uint8_t editor_active;
	uint8_t front_surface_route;
	bool waiting;
	ReplaySavePhase phase;
} ReplaySaveTask;
bool TieReplaySave_Begin(void);
#endif
