#ifndef TIE_RUNTIME_RUNTIME_WIDE_ARITHMETIC_H
#define TIE_RUNTIME_RUNTIME_WIDE_ARITHMETIC_H

/* Full-product arithmetic helpers used by recovered code (tie/math2_wide.h).
 * Exception to recovered ownership: the originals have no function
 * addresses for these, because TIE98 inlined them at every use and TIE95
 * expressed them as #pragma aux (see math2_wide.h). The TIE98 matching
 * build uses the original inline assembly; native builds use portable C. */

#include <stdint.h>

#if defined(TIE98) && !defined(TIE_MODERN)

static inline int32_t math2_mul_q15(int32_t a, int32_t b) {
	__asm {
		push edx
		mov eax, a
		imul b
		shrd eax, edx, 15
		mov a, eax
		pop edx
	}
	return a;
}

#else

static inline int32_t math2_mul_q15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }

#endif

/* Clamp only when the quotient would not fit; also handles divisor zero. */
static inline uint32_t math2_mul_div_u32(uint32_t a, uint32_t b, uint32_t divisor) {
	uint64_t product = (uint64_t)a * b;
	if ((uint32_t)(product >> 32) >= divisor)
		return 0x7fffffffu;
	return (uint32_t)(product / divisor);
}

/* Preserve the low-word rounding carry and the backdrop overflow sentinel. */
static inline uint32_t math2_project_u32(uint32_t magnitude, uint32_t shift, uint32_t rounding,
										 uint32_t divisor) {
	uint64_t numerator = ((uint64_t)magnitude << (shift & 31)) + rounding;
	if ((uint32_t)(numerator >> 32) >= divisor)
		return 0x7fffff00u;
	return (uint32_t)(numerator / divisor);
}

#endif
