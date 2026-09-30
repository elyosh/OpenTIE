#ifndef TIE_RUNTIME_RUNTIME_PLAY1_DATA_SET_H
#define TIE_RUNTIME_RUNTIME_PLAY1_DATA_SET_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Select the PLAY1 scene tables once for the installed frontend data: the
 * recovered retail tables, or the LecDemos sample-disc tables copied over
 * them when the data has no ASTREAM directory. */
void TiePlay1_SelectDataSet(void);
bool TiePlay1_UsesDemoData(void);

#ifdef __cplusplus
}
#endif

#endif
