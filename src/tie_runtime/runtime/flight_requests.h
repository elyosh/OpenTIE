#ifndef TIE_RUNTIME_RUNTIME_FLIGHT_REQUESTS_H
#define TIE_RUNTIME_RUNTIME_FLIGHT_REQUESTS_H

/* Request and handoff channels between the recovered in-flight input
 * dispatcher (user_userinterface / user_inputforplane) and the
 * cooperative flight tasks. The original code entered the pause wait,
 * the in-flight info room and the replay viewer synchronously from the
 * key handlers; the port records the request and the flight task step
 * pushes the corresponding task after tie_doframe returns. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sub-modal result handoff shared by every leaf sub-modal task (msgroom,
 * goals, maproom, damage, wingman, help, option, inflightinfo). The
 * producer publishes its return code on the tick it finishes; the parent
 * task reads it once in its post-sub-modal phase. Values mirror the
 * original synchronous returns:
 *   -1 / 0xFFFF : navigate to previous tab
 *    0          : exit info room
 *   +1          : navigate to next tab
 *   other       : sub-modal-specific (e.g. option's exit codes) */
void TieFlightRequest_SetSubmodalResult(int32_t result);
int32_t TieFlightRequest_SubmodalResult(void);

/* Info-room request: a key handler records the screen index (0..6);
 * the flight/replay task consumes it (returning -1 when none is pending)
 * and pushes TieInflightInfo_Begin. */
void TieFlightRequest_InfoRoom(int32_t screen);
int32_t TieFlightRequest_ConsumeInfoRoom(void);

/* Key left by an info room that closed on the wingmen screen (4), such as
 * a wingman order chosen there. The original dispatched it again before
 * the frame continued; the port runs the room as a task, so the info task
 * records the key and the next user_inputforplane dispatches it before
 * that frame's own key. Consume returns false when none is pending. */
void TieFlightRequest_SetRoomKey(int16_t key);
bool TieFlightRequest_ConsumeRoomKey(int16_t* key);

/* Replay-viewer request from the 'v' key; the flight task consumes it and
 * pushes TieReplaySession_Begin, then posts the RESUMED banner after the
 * viewer pops. */
void TieFlightRequest_ReplayViewer(void);
bool TieFlightRequest_ConsumeReplayViewer(void);

/* Pause: the original blocked in user_userinterface until a key arrived.
 * The port enters a paused state that tie_doframe observes, services the
 * pending keys on each later user_userinterface call, and resumes on the
 * mapped Pause command. */
void TieFlightPause_Enter(void);
void TieFlightPause_Service(void);
bool TieFlightPause_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif
