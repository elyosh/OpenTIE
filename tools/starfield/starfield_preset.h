/*
 * starfield_preset — read/write TieStarfieldParams as a .tune.yaml
 * sidecar, shared by the CLI (--preset) and the tuner (Save/Load).
 * Matches the project's existing *.tune.yaml convention.
 */
#ifndef STARFIELD_PRESET_H
#define STARFIELD_PRESET_H

#include "starfield_core.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write p + the n_els elements to path (atomically via .tmp + rename).
 * Returns false on failure; errbuf (if non-NULL) gets a note. */
bool TieStarfieldPreset_Save(const char* path, const TieStarfieldParams* p, const TieStarfieldElement* els,
							 int n_els, char* errbuf, size_t errcap);

/* Load path into p and els (up to max_els; *n_els gets the count).
 * Fields absent from the file keep their incoming value, so seed p with
 * starfield_default_params() first. Returns false on parse/open failure
 * (errbuf gets a note). */
bool TieStarfieldPreset_Load(const char* path, TieStarfieldParams* p, TieStarfieldElement* els, int* n_els,
							 int max_els, char* errbuf, size_t errcap);

#ifdef __cplusplus
}
#endif

#endif /* STARFIELD_PRESET_H */
