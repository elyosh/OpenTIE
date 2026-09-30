#ifndef TIE_RUNTIME_RUNTIME_FLIGHT_TASK_H
#define TIE_RUNTIME_RUNTIME_FLIGHT_TASK_H

/* Begin a live mission (zero) or replay-only simulator session (nonzero). */
void TieFlightTask_Begin(int replay_mode);
void TieFlightTask_BeginMission(void);
void TieFlightRuntime_ReleaseRecoveredResources(void);

#endif
