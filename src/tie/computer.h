#ifndef __COMPUTER_H__
#define __COMPUTER_H__

#include <stdint.h>

/* Computer mode tabs */
typedef enum {
	COMP_MODE_MEDALS = 0,
	COMP_MODE_RECORD = 1,
	COMP_MODE_BACKUP = 2,
	COMP_MODE_OPTIONS = 3,
} ComputerMode;

/* Push the in-flight Computer (options/medal/backup/record) screen
 * as a sub-task. Caller-task yields after this; the dialog itself
 * sets landru_exit_gbl on its way out via lerror_Set_Landru_Exit
 * (read by callers on the next step or via lerror_Get_Landru_Exit). */
void computer_Push_Computer_Dialog_Task(void);

#endif
