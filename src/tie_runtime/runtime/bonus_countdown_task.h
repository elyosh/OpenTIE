#ifndef TIE_BONUS_COUNTDOWN_TASK_H
#define TIE_BONUS_COUNTDOWN_TASK_H

#include <stdbool.h>
#include <stdint.h>
typedef struct BonusCountdownTask {
	uint16_t tickbudget;
	bool waiting;
} BonusCountdownTask;

/* Set while the per-section bonus countdown task is on the task stack
 * (TieBonusCountdown_Begin -> 1, gate_updategateanimations on completion
 * -> 0). The classic cockpit bonus bar is only visible during this
 * window: `panel_updatepanel` redraws the cockpit bitmap every tick
 * and only the per-step `gate_updatebonuspoints` paints the timer +
 * bonus text on top -- outside the countdown the region is bare
 * cockpit. HD reads this flag to reproduce the same gating. */
extern uint8_t bonus_countdown_active;

void TieBonusCountdown_Begin(void);

#endif
