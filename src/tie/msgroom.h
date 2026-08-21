#ifndef __MSGROOM_H__
#define __MSGROOM_H__

#include "tie/msg.h"
#include <stdint.h>

/* --- Public API ----------------------------------------------------
 *
 * Push the scrollable info-room as a tie_core task. Runs render + poll
 * phases on the task stack until an exit-class key fires (Up/Down to
 * switch tabs, ESC/Q/q/l/F1 to dismiss). The exit code is parked in
 * `user_submodal_result` before pop:
 *    -1  Up arrow       : switch to previous info room
 *    +1  Down arrow     : switch to next info room
 *     0  ESC/Q/q/l/F1   : dismiss info rooms entirely */
void msgroom_Push_MessageRoom_Task(void);

/* Scroll-clamp helper: returns the new page-top ring index after applying
 * delta, honoring wrap + the "don't cross the seam" clamp in both regimes
 * (linear when numhistorymsgs<300, circular when ring is full). */
int16_t msgroom_scrollmsgs(int16_t cur_idx, int16_t delta);

/* --- Module globals (watdbg: msgroom.c ownership) --- */

extern int16_t lasthistorymsg;          /* 0xD5150 - newest ring slot (-1 = empty) */
extern uint16_t numhistorymsgs;         /* 0xD5152 - saturating msg count (<= 300) */
extern int32_t msgsPerPage;             /* 0xE3B24 - msgs per info-panel page (14 hi-res / 16 low-res) */
extern MsgHistoryEntry* messagehistory; /* 0xE3B28 - pointer to the 300-slot ring */

#endif
