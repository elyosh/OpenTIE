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
static uint32_t frame_start_ms;
// GLOBAL: TIE 0xD2B84
static int32_t time_gbl;

/* --- Tick source --- */

static uint64_t s_last_us;
static uint32_t s_residual_us;

enum { LTIMER_MAX_CATCH_UP_FRAMES = 4 };

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
	uint64_t ticks = total_us / 4000u;
	s_residual_us = (uint32_t)(total_us % 4000u);
	timer_ticks_gbl = (int32_t)((uint32_t)timer_ticks_gbl + (uint32_t)ticks);
	uint32_t phase = (uint32_t)(timer_count_gbl - 1);
	phase = (phase + 14u - (uint32_t)(ticks % 14u)) % 14u;
	timer_count_gbl = (int16_t)(phase + 1u);
}

static uint32_t ltimer_now_ms(void) { return (uint32_t)(landru_host_now_us() / 1000u); }

/* --- Module lifecycle --- */

void ltimer_Create_Timer_Interrupt(void) {
	timer_ticks_gbl = 0;
	timer_count_gbl = 14;
	s_last_us = landru_host_now_us();
	s_residual_us = 0;
	frame_start_ms = ltimer_now_ms();
	timer_module_gbl = 1;
}

void ltimer_Destroy_Timer_Interrupt(void) {
	if (timer_module_gbl)
		timer_module_gbl = 0;
}

/* --- Frame pacing --- */

int ltimer_Frame_Budget_Pending(void) {
	if (frame_rate_gbl <= 0)
		return 0;
	uint32_t budget_ms = (uint32_t)frame_rate_gbl * 4;
	uint32_t now = ltimer_now_ms();
	if (now - frame_start_ms >= budget_ms)
		return 0;
	/* Pixel writes during the wait remain buffered for the host to present. */
	lio_Poll_Fast_Input();
	return 1;
}

uint64_t ltimer_Next_Frame_Delay_Us(void) {
	if (frame_rate_gbl <= 0)
		return UINT64_MAX;
	const uint32_t budget_ms = (uint32_t)frame_rate_gbl * 4u;
	const uint32_t elapsed_ms = ltimer_now_ms() - frame_start_ms;
	return elapsed_ms < budget_ms ? (uint64_t)(budget_ms - elapsed_ms) * 1000u : 0u;
}

float ltimer_Frame_Progress(void) {
	if (frame_rate_gbl <= 0)
		return 0.0f;
	uint32_t budget_ms = (uint32_t)frame_rate_gbl * 4;
	if (budget_ms == 0)
		return 0.0f;
	uint32_t now = ltimer_now_ms();
	uint32_t elapsed = now - frame_start_ms;
	if (elapsed >= budget_ms)
		return 1.0f; /* clamp at upper bound — should be rare */
	return (float)elapsed / (float)budget_ms;
}

void ltimer_Commit_Frame(void) {
	const uint32_t now = ltimer_now_ms();
	if (frame_rate_gbl > 0) {
		const uint32_t budget_ms = (uint32_t)frame_rate_gbl * 4u;
		const uint32_t elapsed_ms = now - frame_start_ms;
		/* Retain the configured timeline across ordinary late host samples.
		 * Rebase after a long discontinuity instead of replaying stale frames. */
		if (elapsed_ms / budget_ms > LTIMER_MAX_CATCH_UP_FRAMES)
			frame_start_ms = now;
		else
			frame_start_ms += budget_ms;
	} else {
		frame_start_ms = now;
	}
	ltimer_sync_ticks();
	/* Consume the FULL int32 accumulator into time_gbl. Casting through
	 * uint16_t here would silently drop the upper bits whenever the game
	 * thread stalled for >262 s (~65 536 ticks @ 250 Hz) — App Nap, sleep,
	 * debugger break, etc. — making Current_Time jump backwards and freezing
	 * the lio_Poll_Fast_Input cursor-rate gate (cursor_time_gbl ahead of
	 * Current_Time) so XCURSOR_Cursor_To_Screen would stop being called. */
	time_gbl += timer_ticks_gbl;
	timer_ticks_gbl = 0;
}

/* Periodic yield point inside long inner loops. It drains accumulated PIT
 * ticks and consumes the latest input state. */
void ltimer_Often(void) {
	ltimer_sync_ticks();
	time_gbl += timer_ticks_gbl;
	timer_ticks_gbl = 0;
	lio_Poll_Fast_Input();
}

/* --- Time queries --- */

int32_t ltimer_Current_Time(void) {
	/* Include uncommitted host ticks so mid-frame callers see live time. */
	ltimer_sync_ticks();
	return time_gbl + timer_ticks_gbl;
}

/* --- Frame rate control --- */

void ltimer_Set_Frame_Rate(int16_t rate) {
	frame_rate_gbl = rate;
	frame_start_ms = ltimer_now_ms();
}

/* Cel budget in microseconds at the active frame rate (rate × 4 ms).
 * Returns 0 when no rate is set so consumers can detect the
 * "no scene-clock cadence" case. */
uint32_t ltimer_Frame_Period_Us(void) {
	if (frame_rate_gbl <= 0)
		return 0;
	return (uint32_t)frame_rate_gbl * 4u * 1000u;
}
