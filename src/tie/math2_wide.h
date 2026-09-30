#ifndef TIE_MATH2_WIDE_H
#define TIE_MATH2_WIDE_H

#if defined(TIE_MODERN) && !defined(__WATCOMC__) && !defined(__386__)
#include "tie_runtime/runtime/wide_arithmetic.h"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full-product arithmetic used by the original 32-bit x86 code. Watcom
 * 10.0a has no 64-bit integer type, so TIE95 expresses these as inline
 * assembly; __386__ is Watcom's predefined 32-bit target macro. */
// clang-format off
#if defined(__WATCOMC__) || defined(__386__)

int32_t math2_mul_q15(int32_t a, int32_t b);
#pragma aux math2_mul_q15 = \
	"imul edx" \
	"shrd eax, edx, 15" \
	parm [eax] [edx] value [eax] modify exact [eax edx];

/* Clamp only when the quotient would not fit in EAX; also handles divisor zero. */
uint32_t math2_mul_div_u32(uint32_t a, uint32_t b, uint32_t divisor);
#pragma aux math2_mul_div_u32 = \
	"mul edx" \
	"cmp edx, ebx" \
	"jae md_overflow" \
	"div ebx" \
	"jmp md_done" \
	"md_overflow: mov eax, 7fffffffh" \
	"md_done:" \
	parm [eax] [edx] [ebx] value [eax] modify exact [eax edx];

/* Preserve the low-word rounding carry and the backdrop overflow sentinel. */
uint32_t math2_project_u32(uint32_t magnitude, uint32_t shift, uint32_t rounding, uint32_t divisor);
#pragma aux math2_project_u32 = \
	"xor edx, edx" \
	"shld edx, eax, cl" \
	"shl eax, cl" \
	"add eax, esi" \
	"adc edx, 0" \
	"cmp edx, ebx" \
	"jae pr_overflow" \
	"div ebx" \
	"jmp pr_done" \
	"pr_overflow: mov eax, 7fffff00h" \
	"pr_done:" \
	parm [eax] [ecx] [esi] [ebx] value [eax] modify exact [eax edx];

// clang-format on

#elif defined(TIE_MODERN)

/* Native builds use the port's portable C arithmetic (wide_arithmetic.h). */

#elif defined(_MSC_VER)

/* VC5 inline helpers, as in OpenXvT's Math_MulQ15. */
static __inline int32_t math2_mul_q15(int32_t a, int32_t b) {
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

static __inline uint32_t math2_mul_div_u32(uint32_t a, uint32_t b, uint32_t divisor) {
	uint64_t product = (uint64_t)a * b;
	if ((uint32_t)(product >> 32) >= divisor)
		return 0x7fffffffu;
	return (uint32_t)(product / divisor);
}

static __inline uint32_t math2_project_u32(uint32_t magnitude, uint32_t shift, uint32_t rounding,
										 uint32_t divisor) {
	uint64_t numerator = ((uint64_t)magnitude << (shift & 31)) + rounding;
	if ((uint32_t)(numerator >> 32) >= divisor)
		return 0x7fffff00u;
	return (uint32_t)(numerator / divisor);
}

#else
#error "math2_wide.h: unsupported compiler"
#endif
#ifdef __cplusplus
}
#endif

#endif
