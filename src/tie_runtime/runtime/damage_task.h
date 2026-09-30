#ifndef TIE_RUNTIME_DAMAGE_TASK_H
#define TIE_RUNTIME_DAMAGE_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct DamageRoomState {
	int16_t sel_sys;
	int16_t mouse_prev;
	int16_t ret_dir;
	bool started;
	bool render;
	bool finished;
} DamageRoomState;

void TieDamage_Begin(void);

#endif
