#ifndef TIE_RUNTIME_RUNTIME_POINTER_KEY_H
#define TIE_RUNTIME_RUNTIME_POINTER_KEY_H

#include <stdint.h>

/* Low 32 bits of a host pointer, as the 32-bit originals hash object
 * addresses used as cache keys. */
static inline int TiePointerKey_LowBits(const void* pointer) { return (int)(uint32_t)(uintptr_t)pointer; }

#endif
