#ifndef TIE_FLIGHT_STARS_H
#define TIE_FLIGHT_STARS_H

#include <stdbool.h>

#include "aeron/render.h"
#include "tie_remaster/flight/render_config.h"

typedef struct TieFlightCamera TieFlightCamera;
typedef struct TieFlightStars TieFlightStars;
typedef struct TieSnapshot TieSnapshot;

TieFlightStars* TieFlightStars_Create(AeronTextureFormat rt_format);
void TieFlightStars_Destroy(TieFlightStars* stars);

/* Uploads the direction list before the scene render pass opens. */
bool TieFlightStars_Prepare(TieFlightStars* stars, AeronCommandBuffer* cmd, const TieSnapshot* snapshot,
							TieFlightStarfieldStyle style);

/* Draws one instanced rounded quad per direction inside the scene pass. */
void TieFlightStars_DrawInPass(TieFlightStars* stars, AeronCommandBuffer* cmd, AeronRenderPass* pass,
							   const TieSnapshot* snapshot, const TieFlightCamera* camera,
							   const float view_proj[16]);

#endif
