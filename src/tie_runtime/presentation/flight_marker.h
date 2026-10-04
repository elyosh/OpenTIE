#ifndef TIE_RUNTIME_PRESENTATION_FLIGHT_MARKER_H
#define TIE_RUNTIME_PRESENTATION_FLIGHT_MARKER_H

#include <stdbool.h>

typedef struct AeronCommandBuffer AeronCommandBuffer;
typedef struct TieSnapshot TieSnapshot;

/* Records the marker texture upload on the startup command buffer. */
bool TieFlightMarker_Init(AeronCommandBuffer* startup_cmd);
void TieFlightMarker_Submit(const TieSnapshot* snapshot);
void TieFlightMarker_Shutdown(void);

#endif
