#ifndef TIE_BONUS_COUNTDOWN_TASK_H
#define TIE_BONUS_COUNTDOWN_TASK_H

#include <stdbool.h>
#include <stdint.h>
typedef struct BonusCountdownTask {
	uint16_t tickbudget;
	bool waiting;
} BonusCountdownTask;

void TieBonusCountdown_Begin(void);

#endif
