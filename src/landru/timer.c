#include <landru/io.h>
#include <landru/timer.h>
#include <landru/vesa.h>

#include "host_internal.h"

/* Host-clock implementation of the 250 Hz PIT timer interface. */

/* Globals */
static int16_t timer_module_gbl;
// GLOBAL: TIE 0xFB650
static int32_t timer_ticks_gbl;
static int16_t timer_count_gbl;
static int16_t frame_rate_gbl;
static uint32_t frame_elapsed_ticks_gbl;
// GLOBAL: TIE 0xD2B84
static int32_t time_gbl;

/* --- Tick source --- */

static uint64_t s_last_us;
static uint32_t s_residual_us;

enum { LTIMER_TICK_US = 4000 };

/* Advance timer_ticks_gbl to match elapsed host time. The cursor
 * crosses one PIT tick per 4 ms and accumulates
 * sub-tick remainders, so behaviour is bit-stable across init/reset
 * boundaries. */
static void ltimer_sync_ticks(void) {
	uint64_t now_us = landru_host_now_us();
	if (now_us < s_last_us) {
		s_last_us = now_us;
		s_residual_us = 0;
		return;
	}
	uint64_t elapsed_us = now_us - s_last_us;
	s_last_us = now_us;
	uint64_t total_us = elapsed_us + s_residual_us;
	uint64_t ticks = total_us / LTIMER_TICK_US;
	s_residual_us = (uint32_t)(total_us % LTIMER_TICK_US);
	timer_ticks_gbl = (int32_t)((uint32_t)timer_ticks_gbl + (uint32_t)ticks);
	uint32_t phase = (uint32_t)(timer_count_gbl - 1);
	phase = (phase + 14u - (uint32_t)(ticks % 14u)) % 14u;
	timer_count_gbl = (int16_t)(phase + 1u);
}

/* The original keeps continuous engine time and frame-lock time as separate
 * accumulators. Frame commits may discard an overrun, but elapsed engine time
 * must remain available to input, streaming, and blink timers. */
static void ltimer_consume_elapsed_ticks(void) {
	ltimer_sync_ticks();
	uint32_t elapsed = (uint32_t)timer_ticks_gbl;
	time_gbl = (int32_t)((uint32_t)time_gbl + elapsed);
	if (UINT32_MAX - frame_elapsed_ticks_gbl < elapsed)
		frame_elapsed_ticks_gbl = UINT32_MAX;
	else
		frame_elapsed_ticks_gbl += elapsed;
	timer_ticks_gbl = 0;
}

/* --- Module lifecycle --- */

void ltimer_Create_Timer_Interrupt(void) {
	timer_ticks_gbl = 0;
	timer_count_gbl = 14;
	s_last_us = landru_host_now_us();
	s_residual_us = 0;
	frame_elapsed_ticks_gbl = 0;
	timer_module_gbl = 1;
}

void ltimer_Destroy_Timer_Interrupt(void) {
	if (timer_module_gbl)
		timer_module_gbl = 0;
}

/* --- Frame pacing --- */

int ltimer_Frame_Budget_Pending(void) {
	ltimer_consume_elapsed_ticks();
	if (frame_rate_gbl <= 0)
		return 0;
	if (frame_elapsed_ticks_gbl >= (uint32_t)frame_rate_gbl)
		return 0;
	/* Pixel writes during the wait remain buffered for the host to present. */
	lio_Poll_Fast_Input();
	return 1;
}

uint64_t ltimer_Next_Frame_Delay_Us(void) {
	if (frame_rate_gbl <= 0)
		return UINT64_MAX;
	ltimer_sync_ticks();
	uint64_t elapsed_ticks = (uint64_t)frame_elapsed_ticks_gbl + (uint32_t)timer_ticks_gbl;
	if (elapsed_ticks >= (uint32_t)frame_rate_gbl)
		return 0;
	uint64_t remaining_us = ((uint32_t)frame_rate_gbl - elapsed_ticks) * LTIMER_TICK_US;
	return remaining_us - s_residual_us;
}

float ltimer_Frame_Progress(void) {
	if (frame_rate_gbl <= 0)
		return 0.0f;
	ltimer_sync_ticks();
	uint64_t elapsed_ticks = (uint64_t)frame_elapsed_ticks_gbl + (uint32_t)timer_ticks_gbl;
	if (elapsed_ticks >= (uint32_t)frame_rate_gbl)
		return 1.0f;
	uint64_t elapsed_us = elapsed_ticks * LTIMER_TICK_US + s_residual_us;
	uint32_t budget_us = (uint32_t)frame_rate_gbl * LTIMER_TICK_US;
	return (float)elapsed_us / (float)budget_us;
}

void ltimer_Commit_Frame(void) {
	ltimer_consume_elapsed_ticks();
	/* Both TIE95 and TIE98 clear the whole frame-lock accumulator after an
	 * accepted frame. This permits one frame after a blocking fade or stall,
	 * then discards the excess instead of replaying overdue film cels. */
	frame_elapsed_ticks_gbl = 0;
}

/* Periodic yield point inside long inner loops. It drains accumulated PIT
 * ticks and consumes the latest input state. */
void ltimer_Often(void) {
	ltimer_consume_elapsed_ticks();
	lio_Poll_Fast_Input();
}

/* --- Time queries --- */

int32_t ltimer_Current_Time(void) {
	/* Include uncommitted host ticks so mid-frame callers see live time. */
	ltimer_sync_ticks();
	return (int32_t)((uint32_t)time_gbl + (uint32_t)timer_ticks_gbl);
}

/* --- Frame rate control --- */

void ltimer_Set_Frame_Rate(int16_t rate) {
	frame_rate_gbl = rate;
}

/* Cel budget in microseconds at the active frame rate (rate × 4 ms).
 * Returns 0 when no rate is set so consumers can detect the
 * "no scene-clock cadence" case. */
uint32_t ltimer_Frame_Period_Us(void) {
	if (frame_rate_gbl <= 0)
		return 0;
	return (uint32_t)frame_rate_gbl * LTIMER_TICK_US;
}
