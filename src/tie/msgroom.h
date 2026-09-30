#ifndef TIE_MSGROOM_H
#define TIE_MSGROOM_H

#include "tie/msg.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* State shared by room rendering and input processing. */
typedef struct MsgRoomRoomState {
	int16_t cur_top_idx;
	int16_t exit_dir;
} MsgRoomRoomState;

void msgroom_OpenRoom(MsgRoomRoomState* state);
void msgroom_render_page(int16_t top_index);
/* Poll result: 0 idle, 1 exit, 2 redraw. */
int msgroom_poll_once(MsgRoomRoomState* state);

/* Scroll-clamp helper: returns the new page-top ring index after applying
 * delta, honoring wrap + the "don't cross the seam" clamp in both regimes
 * (linear when numhistorymsgs<300, circular when ring is full). */
int16_t msgroom_scrollmsgs(int16_t cur_idx, int16_t delta);

/* --- Module globals (watdbg: msgroom.c ownership) --- */

extern int16_t lasthistorymsg;          /* 0xD5150 - newest ring slot (-1 = empty) */
extern uint16_t numhistorymsgs;         /* 0xD5152 - saturating msg count (<= 300) */
extern int32_t msgsPerPage;             /* 0xE3B24 - msgs per info-panel page (14 hi-res / 16 low-res) */
extern MsgHistoryEntry* messagehistory; /* 0xE3B28 - pointer to the 300-slot ring */

#ifdef __cplusplus
}
#endif

#endif
