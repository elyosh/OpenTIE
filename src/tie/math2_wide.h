#ifndef TIE_MATH2_WIDE_H
#define TIE_MATH2_WIDE_H

#if defined(TIE_MODERN) || defined(TIE98)
#include "tie_runtime/runtime/wide_arithmetic.h"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full-product arithmetic used by the original 32-bit x86 code. Watcom
 * 10.0a has no 64-bit integer type, so TIE95 expresses these as inline
 * assembly. TIE98 and native builds take them from wide_arithmetic.h. */
#if !defined(TIE_MODERN) && !defined(TIE98)
// clang-format off

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

/* Three-term dot product, a1 * b1 + a2 * b2 + a3 * b3, wrapping in 32 bits.
 * The original inlines this with the same registers at every use, so it
 * was a helper of this form; its name is not known. */
int32_t math2_dot3(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3);
#pragma aux math2_dot3 = \
	"imul eax, edx" \
	"imul ebx, esi" \
	"imul ecx, edi" \
	"add eax, ebx" \
	"add eax, ecx" \
	parm [eax] [edx] [ebx] [esi] [ecx] [edi] value [eax] modify exact [eax ebx ecx];

// clang-format on

#endif
#ifdef __cplusplus
}
#endif

#endif
