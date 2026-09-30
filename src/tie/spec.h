#ifndef TIE_SPEC_H
#define TIE_SPEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * spec_getspecnum -- maps a species_idx (0..160) into the spec_data[]
 * row index (0..68). Mirrors the binary's SPEC_getspecnum: a direct
 * SpeciesEntry.spec_num read with no bounds check.
 */
uint16_t spec_getspecnum(uint16_t species_idx);

#ifdef __cplusplus
}
#endif

#endif
