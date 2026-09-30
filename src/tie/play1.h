#ifndef TIE_PLAY1_H
#define TIE_PLAY1_H

#include "tie/shellext.h"

#include <landru/surface.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resources and presentation state retained for one film scene. */
typedef struct Play1SceneState {
	SceneHeadStruct* the_head;
	ResFile* file;
	ResFile* file2;
	int16_t scene;
	bool rate_changed;
	bool is_streaming_active; /* mirrors module-static is_streaming for cleanup */
	LandruSurfaceSet surface_set;
} Play1SceneState;

bool play1_OpenScene(Play1SceneState* state);
void play1_CloseScene(Play1SceneState* state);

#ifdef __cplusplus
}
#endif

#endif
