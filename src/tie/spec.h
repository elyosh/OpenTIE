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

/* 64-bit-safe side table of species display-name pointers.
 * SpecData.name_ptr is int32_t (fixed by the retail blob layout) and
 * can't hold a host pointer on LP64. Consumers read spec_name_ptrs[i]
 * instead of casting the int32_t field back to a pointer. */
extern const char* spec_name_ptrs[69];

#ifdef __cplusplus
}
#endif

#endif
