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

enum { TIE_CONTROLLER_MODEL_CAP = 8 };
typedef struct TieControllerModel {
	char guid[33];
	char name[AERON_CONTROLLER_NAME_CAPACITY];
	AeronControllerKind kind; /* Saved SDL device type; must match the connected snapshot. */
	TieControllerProfile profile;
} TieControllerModel;
typedef struct TieControllerOptions {
	TieControllerModel models[TIE_CONTROLLER_MODEL_CAP];
	size_t count;
} TieControllerOptions;

void TieControllerMapping_ClearProfile(TieControllerProfile* profile, AeronControllerKind kind);
int TieControllerMapping_FindModel(const TieControllerOptions* options, const char* guid);
bool TieControllerMapping_OptionsValid(const TieControllerOptions* options, char* error, size_t capacity);
bool TieControllerMapping_AddModel(TieControllerOptions* options, const AeronControllerSnapshot* device,
								   char* error, size_t capacity);
bool TieControllerMapping_InitializeGamepads(TieControllerOptions* options,
											 const TieControllerProfile* defaults,
											 const AeronInputSnapshot* input, char* error, size_t capacity);
const AeronControllerSnapshot* TieControllerMapping_Resolve(const TieControllerModel* model,
															const AeronInputSnapshot* input,
															uint32_t preferred);
uint32_t TieControllerMapping_AnalogInstance(const char* guid);
uint16_t TieControllerMapping_ThrottlePosition(int16_t raw, AeronControllerKind kind, int source,
											   bool invert);
bool TieControllerMapping_ThrottleSample(uint16_t* position, uint32_t* generation);

bool TieControllerMapping_Optionsequal(const TieControllerOptions* left, const TieControllerOptions* right);
bool TieControllerMapping_Profilevalidate(const TieControllerProfile* profile, AeronControllerKind kind,
										  char* error, size_t error_capacity);
/* Converts the player-facing inversion preference to the raw-axis polarity used by TieInputMapping. */
bool TieControllerMapping_EffectiveAxisInvert(AeronControllerKind kind, TieInputAxis axis, bool invert);

bool TieControllerMapping_SetOptions(const TieControllerOptions* options);
void TieControllerMapping_Suspend(void);
void TieControllerMapping_Resume(void);
void TieControllerMapping_Update(const AeronInputSnapshot* input);
int TieControllerMapping_Present(void);
void TieControllerMapping_Read(int16_t* axes, int axis_count, uint16_t* buttons);

#endif
