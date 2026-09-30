#ifndef TIE_COMPUTER_H
#define TIE_COMPUTER_H

#include <landru/input.h>
#include <landru/surface.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Computer mode tabs */
typedef enum {
	COMP_MODE_MEDALS = 0,
	COMP_MODE_RECORD = 1,
	COMP_MODE_BACKUP = 2,
	COMP_MODE_OPTIONS = 3,
} ComputerMode;

int16_t computer_Do_Computer_Dialog(void);

#ifdef __cplusplus
}
#endif

#endif
