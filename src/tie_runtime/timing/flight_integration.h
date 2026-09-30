#ifndef TIE_RUNTIME_TIMING_FLIGHT_INTEGRATION_H
#define TIE_RUNTIME_TIMING_FLIGHT_INTEGRATION_H

#include <stdint.h>

struct FlightObject;

uint16_t TieFlightIntegration_AutopilotStep(uint16_t obj_idx, unsigned int axis, int16_t rate_cap,
											int16_t pacing, uint16_t axis_scale);
void TieFlightIntegration_Move(uint16_t object_index, struct FlightObject* object);
void TieFlightIntegration_SeparateFriendly(void);
/* Logical movement frames, independent of host ticks. */
void TieFlightIntegration_BeginFrame(void);
uint32_t TieFlightIntegration_Frame(void);

#endif
