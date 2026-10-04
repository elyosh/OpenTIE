#ifndef TIE_RUNTIME_INPUT_MOUSE_FLIGHT_H
#define TIE_RUNTIME_INPUT_MOUSE_FLIGHT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct AeronInputSnapshot AeronInputSnapshot;

enum { TIE_MOUSE_SENSITIVITY_MIN = 1, TIE_MOUSE_SENSITIVITY_MAX = 9 };

typedef enum TieMouseFlightMode { TIE_MOUSE_VIRTUAL_STICK, TIE_MOUSE_CLASSIC } TieMouseFlightMode;

typedef struct TieMouseFlightOptions {
	bool enabled;
	TieMouseFlightMode mode;
	int sensitivity;
	bool invert_y;
} TieMouseFlightOptions;

bool TieMouseFlight_OptionsValid(const TieMouseFlightOptions* options);
void TieMouseFlight_SetOptions(const TieMouseFlightOptions* options);
const TieMouseFlightOptions* TieMouseFlight_Options(void);
/* Live cockpit flight applies the flight options; map and replay cameras keep
 * the unscaled classic motion. A change clears all pending motion. */
void TieMouseFlight_SetFlightActive(bool active);
void TieMouseFlight_Reset(void);
void TieMouseFlight_ClearFraction(void);
/* Once per captured host frame. */
void TieMouseFlight_Collect(const AeronInputSnapshot* input, int32_t delta_us);
/* Classic motion counts; zero while the virtual stick owns the motion. */
void TieMouseFlight_ReadMovement(int16_t* dx, int16_t* dy);
/* Joystick-scaled stick axes; false unless the virtual stick is active. */
bool TieMouseFlight_ReadStick(int16_t* x, int16_t* y);
/* Read the held stick without consuming pending motion. */
bool TieMouseFlight_GetHudMarker(int* yaw, int* pitch);

#endif
