#ifndef TIE_CONTROLLER_MAPPING_H
#define TIE_CONTROLLER_MAPPING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aeron/input.h"
#include "tie_runtime/input/actions.h"
#include "tie_runtime/input/input.h"

enum {
	TIE_CONTROLLER_BINDING_CAP =
		AERON_CONTROLLER_BUTTON_MAX + 2 * AERON_CONTROLLER_AXIS_MAX + 4 * AERON_CONTROLLER_HAT_MAX,
};

typedef struct TieInputActionBinding {
	AeronControllerDigitalSource source;
	TieInputAction action;
} TieInputActionBinding;

typedef struct TieControllerProfile {
	TieInputMapping mapping;
	TieInputActionBinding bindings[TIE_CONTROLLER_BINDING_CAP];
	size_t binding_count;
} TieControllerProfile;

typedef struct TieControllerOptions {
	AeronControllerSelector selector;
	TieControllerProfile gamepad;
	TieControllerProfile joystick;
} TieControllerOptions;

bool TieControllerMapping_Optionsequal(const TieControllerOptions* left, const TieControllerOptions* right);
bool TieControllerMapping_Profilevalidate(const TieControllerProfile* profile, AeronControllerKind kind,
										  char* error, size_t error_capacity);
/* Converts the player-facing inversion preference to the raw-axis polarity used by TieInputMapping. */
bool TieControllerMapping_EffectiveAxisInvert(AeronControllerKind kind, TieInputAxis axis, bool invert);

void TieControllerMapping_SetOptions(const TieControllerOptions* options);
void TieControllerMapping_Suspend(void);
void TieControllerMapping_Resume(void);
void TieControllerMapping_Update(const AeronInputSnapshot* input);
int TieControllerMapping_Present(void);
void TieControllerMapping_Read(int16_t* axes, int axis_count, uint16_t* buttons);

#endif
