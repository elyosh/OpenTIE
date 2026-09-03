#ifndef LANDRU_TIMER_H
#define LANDRU_TIMER_H

#include <stdint.h>

void ltimer_Create_Timer_Interrupt(void);
void ltimer_Destroy_Timer_Interrupt(void);

/* Frame-budget gate for tasks.
 *
 * ltimer_Frame_Budget_Pending: returns 1 if frame_rate_gbl is set AND
 * the per-scene budget has not yet elapsed since the last frame
 * commit. Tasks yield when 1 is reported; pending pixels remain buffered.
 *
 * ltimer_Commit_Frame: budget elapsed; consume accumulated PIT ticks and
 * clear the entire frame-lock accumulator. Tasks call this once the gate
 * returns 0 to mark the frame as begun. */
int ltimer_Frame_Budget_Pending(void);
void ltimer_Commit_Frame(void);
/* Remaining synthetic-clock delay to the current scheduled frame deadline. */
uint64_t ltimer_Next_Frame_Delay_Us(void);

/* Clamped progress through the current scheduled frame, or zero when idle. */
float ltimer_Frame_Progress(void);

/* Periodic yield point inside long inner loops — drains accumulated
 * PIT ticks into the engine's time_gbl and pumps fast-input. */
void ltimer_Often(void);

/* Live PIT-tick time (time_gbl + unconsumed ticks). */
int32_t ltimer_Current_Time(void);

/* Set the per-scene frame target in PIT ticks without resetting elapsed time. */
void ltimer_Set_Frame_Rate(int16_t rate);

/* Cel budget in microseconds (frame_rate_gbl × 4 × 1000). Returns
 * 0 when no rate is set. Render emitters publish it as an interpolation
 * hint. */
uint32_t ltimer_Frame_Period_Us(void);

#endif
