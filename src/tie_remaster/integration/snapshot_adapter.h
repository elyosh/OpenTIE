#ifndef TIE_REMASTER_INTEGRATION_SNAPSHOT_ADAPTER_H
#define TIE_REMASTER_INTEGRATION_SNAPSHOT_ADAPTER_H

/*
 * tie_core ↔ remaster adapter — the snapshot-coupled half of the
 * compositor wiring that filmview's preview path doesn't need (it
 * builds its own Actor2DViews from a PlayerObject). Lives here
 * rather than next to the manifest layer because both functions
 * call tie_core's TieSnapshot_Current() and the manifest layer is
 * built into a static lib that filmview links without tie_core.
 */

#include "tie_remaster/scene2d/cutscene.h" /* TieScene2dCutscene */
#include "tie_remaster/scene2d/manifest.h" /* TieScene2dActorView */

#ifdef __cplusplus
extern "C" {
#endif

/* True when the live snapshot has a (lfd, film) tuple AND the
 * compositor `g` has a complete bundle for it. The caller's gating
 * reads the same predicate every frame to decide whether to drive
 * the HD overlay. */
bool TieRemasterSnapshot_HasScene2dBundle(TieScene2dCutscene* g);

/* True when the active, source-compatible bundle has manifest-driven
 * sub-cel actor interpolation. */
bool TieRemasterSnapshot_HasScene2dInterpolation(TieScene2dCutscene* g);

/* Marshal the current snapshot into TieScene2dActorView[] for the compositor.
 * Writes at most `cap` views, returns the count. Also fills the
 * out-pointers (each may be NULL) with the live (lfd, film, cur_cel)
 * triple — useful for the same-frame `TieScene2dCutscene_RecordActorsInSource` call. */
int TieRemasterSnapshot_BuildActorViews(TieScene2dActorView* out, int cap, int* out_cur_cel,
										const char** out_lfd, const char** out_film);

#ifdef __cplusplus
}
#endif

#endif
