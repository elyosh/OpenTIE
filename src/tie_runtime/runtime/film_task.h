#ifndef TIE_RUNTIME_FILM_TASK_H
#define TIE_RUNTIME_FILM_TASK_H

#include "tie/shellext.h"
#include <landru/surface.h>

void TieFilm_Begin(SceneHeadStruct* head);

void TieFilm_RunView(ResFile* file, ResFile* file2, int16_t scene, bool rate_changed,
					 bool is_streaming_active, LandruSurfaceSet surface_set);

#endif
