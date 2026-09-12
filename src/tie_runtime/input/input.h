#ifndef TIE_RUNTIME_INPUT_INPUT_H
#define TIE_RUNTIME_INPUT_INPUT_H

#include "aeron/aeron.h"
#include "tie_runtime/snapshot/snapshot.h"

#include <stdbool.h>
#include <stdint.h>

enum { TIE_INPUT_AXIS_MAX = 16 };

/* Percent of full throttle travel (0.5f means 0.5%). Endpoint tolerance must
 * remain below 50%; jitter tolerance is independent of endpoint saturation. */
#define TIE_INPUT_THROTTLE_ENDPOINT_PERCENT 0.5f
#define TIE_INPUT_THROTTLE_JITTER_PERCENT 0.1f

typedef enum TieInputAxis {
	TIE_INPUT_AXIS_YAW = 0,
	TIE_INPUT_AXIS_PITCH,
	TIE_INPUT_AXIS_ROLL,
	TIE_INPUT_AXIS_THROTTLE,
	TIE_INPUT_AXIS_COUNT
} TieInputAxis;

typedef struct TieInputAxisBinding {
	int8_t source;
	bool invert;
	float deadzone;
} TieInputAxisBinding;

typedef struct TieInputMapping {
	TieInputAxisBinding axes[TIE_INPUT_AXIS_COUNT];
} TieInputMapping;

void TieInput_SetMapping(const TieInputMapping* mapping);
const TieInputMapping* TieInput_Mapping(void);
/* Total center deadzone, including the fixed Landru cutoff. */
float TieInput_AxisDeadzonePercent(float configured_deadzone);
float TieInput_AxisDeadzoneFromPercent(float total_percent);
int16_t TieInput_MapAxis(const int16_t* raw, int count, TieInputAxisBinding binding);

uint32_t TieInput_ReadThrottleCommand(bool eligible);
void TieInput_ResetThrottle(void);
void TieInput_EnqueueDosKey(int16_t key);
void TieInput_EnqueueKey(int16_t key);
int TieInput_KeyPending(void);
int TieInput_ReadKey(void);
int TieInput_ModifierKeys(void);
void TieInput_GetMousePosition(int16_t* buttons, int16_t* x, int16_t* y);
void TieInput_GetMouseMovement(int16_t* dx, int16_t* dy);
void TieInput_SetMousePosition(int16_t x, int16_t y);
void TieInput_ShowCursor(bool show);
int TieInput_JoystickPresent(void);
void TieInput_JoystickShutdown(void);
void TieInput_ReadJoystick(int port, int16_t* axes, int axis_count, uint16_t* buttons);

void TieInput_UpdateCapture(const TieSnapshot* snapshot, bool settings_open);
void TieInput_SyncSystemCursor(bool settings_open);
void TieInput_SetClassicLayout(const AeronRectI* frame, const AeronRectI* classic);
void TieInput_UpdateCursor(bool pillarbox_active, int16_t engine_x, int16_t engine_y);
void TieInput_CursorFramebufferPosition(float* x, float* y);
void TieInput_SetFramebufferSize(int width, int height);
void TieInput_SuppressKey(int aeron_key);
void TieInput_BeginFrame(int32_t delta_us);

#endif
