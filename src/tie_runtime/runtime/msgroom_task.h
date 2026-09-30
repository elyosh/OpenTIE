#ifndef TIE_RUNTIME_MSGROOM_TASK_H
#define TIE_RUNTIME_MSGROOM_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct MsgRoomRoomState {
	int16_t cur_top_idx;
	int16_t exit_dir;
	bool started;
	bool render;
	bool finished;
} MsgRoomRoomState;

void TieMsgRoom_Begin(void);

#endif
