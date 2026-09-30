#ifndef TIE_MATH2_WIDE_H
#define TIE_MATH2_WIDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full-product arithmetic used by the original 32-bit x86 code. */
// clang-format off
#ifdef __WATCOMC__

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

#else

static inline int32_t math2_mul_q15(int32_t a, int32_t b) {
#if defined(_MSC_VER) && !defined(TIE_MODERN)
	__asm {
		push edx
		mov eax, a
		imul b
		shrd eax, edx, 15
		mov a, eax
		pop edx
	}
	return a;
#else
	return (int32_t)(((int64_t)a * b) >> 15);
#endif
}

static inline uint32_t math2_mul_div_u32(uint32_t a, uint32_t b, uint32_t divisor) {
	uint64_t product = (uint64_t)a * b;
	if ((uint32_t)(product >> 32) >= divisor)
		return 0x7fffffffu;
	return (uint32_t)(product / divisor);
}

static inline uint32_t math2_project_u32(uint32_t magnitude, uint32_t shift, uint32_t rounding,
										 uint32_t divisor) {
	uint64_t numerator = ((uint64_t)magnitude << (shift & 31)) + rounding;
	if ((uint32_t)(numerator >> 32) >= divisor)
		return 0x7fffff00u;
	return (uint32_t)(numerator / divisor);
}

#endif
#ifdef __cplusplus
}
#endif

#endif
