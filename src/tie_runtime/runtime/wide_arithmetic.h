#ifndef TIE_RUNTIME_RUNTIME_WIDE_ARITHMETIC_H
#define TIE_RUNTIME_RUNTIME_WIDE_ARITHMETIC_H

/* Full-product arithmetic helpers used by recovered code. The originals
 * have no function addresses for these: both inlined them at every use,
 * TIE95 as Watcom #pragma aux bodies and TIE98 as MSVC __asm blocks. They
 * live here, outside the recovered sources, because every function
 * defined there must be bound to an original address.
 *
 * Each helper is written once per toolchain: portable C for native builds,
 * the original __asm (or C, where the original used plain C) for the TIE98
 * matching build, and the original #pragma aux for the TIE95 matching
 * build. Watcom 10.0a has no 64-bit integer type. */

#include "tie/edition.h" /* TIE_FLIGHT_EDITION */
#include "tie/tie.h"     /* perspShift, halfPerspFactor */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

/* Q15 product taken at full width, a * b >> 15. */
#if defined(TIE_MODERN)
static inline int32_t math2_mul_q15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
#elif defined(TIE98)
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
int32_t math2_mul_q15(int32_t a, int32_t b);
#pragma aux math2_mul_q15 = \
	"imul edx" \
	"shrd eax, edx, 15" \
	parm [eax] [edx] value [eax] modify exact [eax edx];
#endif

/* Three-term Q15 dot product, (a1 * b1 >> 15) + (a2 * b2 >> 15) + (a3 * b3 >> 15),
 * each product taken at full width and truncated before the wrapping 32-bit
 * sum. Both originals inline this with the same registers at every use
 * (orientation and rotation-axis projections); its name is not known. */
#if defined(TIE_MODERN)
static inline int32_t math2_dot3_q15(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3) {
	return (int32_t)((uint32_t)math2_mul_q15(a1, b1) + (uint32_t)math2_mul_q15(a2, b2) +
					 (uint32_t)math2_mul_q15(a3, b3));
}
#elif defined(TIE98)
static inline int32_t math2_dot3_q15(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3) {
	__asm {
		mov eax, a1
		imul b1
		shrd eax, edx, 15
		mov ebx, eax
		mov eax, a2
		imul b2
		shrd eax, edx, 15
		add ebx, eax
		mov eax, a3
		imul b3
		shrd eax, edx, 15
		add eax, ebx
		mov a1, eax
	}
	return a1;
}
#else
int32_t math2_dot3_q15(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3);
#pragma aux math2_dot3_q15 = \
	"imul edx" \
	"shrd eax, edx, 15" \
	"xchg ebx, eax" \
	"imul esi" \
	"shrd eax, edx, 15" \
	"add ebx, eax" \
	"mov eax, ecx" \
	"imul edi" \
	"shrd eax, edx, 15" \
	"add eax, ebx" \
	parm [eax] [edx] [ebx] [esi] [ecx] [edi] value [eax] modify exact [eax ebx edx];
#endif

/* Q15 product of two values that fit in 16 bits, so the 32-bit product
 * cannot overflow. TIE95 inlines this with the same registers at every
 * use; its name is not known. */
#if defined(TIE_MODERN) || defined(TIE98)
static inline int32_t math2_mul16_q15(int32_t a, int32_t b) { return (a * b) >> 15; }
#else
int32_t math2_mul16_q15(int32_t a, int32_t b);
#pragma aux math2_mul16_q15 = \
	"imul eax, ebx" \
	"sar eax, 15" \
	parm [eax] [ebx] value [eax] modify exact [eax];
#endif

/* a * b / divisor, clamping only when the quotient would not fit in 32 bits;
 * also handles divisor zero. */
#if defined(TIE_MODERN) || defined(TIE98)
static inline uint32_t math2_mul_div_u32(uint32_t a, uint32_t b, uint32_t divisor) {
	uint64_t product = (uint64_t)a * b;
	if ((uint32_t)(product >> 32) >= divisor)
		return 0x7fffffffu;
	return (uint32_t)(product / divisor);
}
#else
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
#endif

/* Perspective projection, ((magnitude << perspShift) + halfPerspFactor) /
 * divisor, saturating to 0x7FFFFF00 when the quotient would not fit in 32
 * bits. Both originals inline this at every use and read the two globals
 * from memory inside the sequence (XWA's backdrop projection is the same
 * helper); its name is not known. */
#if defined(TIE_MODERN)
static inline uint32_t math2_project_persp(uint32_t magnitude, uint32_t divisor) {
	uint64_t numerator = ((uint64_t)magnitude << (perspShift & 31)) + (uint32_t)halfPerspFactor;
	if ((uint32_t)(numerator >> 32) >= divisor)
		return 0x7fffff00u;
	return (uint32_t)(numerator / divisor);
}
#elif defined(TIE98)
static inline uint32_t math2_project_persp(uint32_t magnitude, uint32_t divisor) {
	__asm {
		mov eax, magnitude
		mov cl, perspShift
		xor edx, edx
		shld edx, eax, cl
		shl eax, cl
		add eax, halfPerspFactor
		adc edx, 0
		cmp edx, divisor
		jb pp_divide
		mov eax, 7fffff00h
		jmp pp_done
	pp_divide:
		div divisor
	pp_done:
		mov magnitude, eax
	}
	return magnitude;
}
#else
uint32_t math2_project_persp(uint32_t magnitude, uint32_t divisor);
#pragma aux math2_project_persp = \
	"xor edx, edx" \
	"mov cl, perspShift" \
	"shld edx, eax, cl" \
	"shl eax, cl" \
	"add eax, halfPerspFactor" \
	"adc edx, 0" \
	"cmp edx, ebx" \
	"jb pp_divide" \
	"mov eax, 7fffff00h" \
	"jmp pp_done" \
	"pp_divide: div ebx" \
	"pp_done:" \
	parm [eax] [ebx] value [eax] modify exact [eax ecx edx];
#endif

/* Three-term dot product, a1 * b1 + a2 * b2 + a3 * b3, wrapping in 32 bits.
 * TIE95 inlines this with the same registers at every use; its name is not
 * known. */
#if defined(TIE_MODERN) || defined(TIE98)
static inline int32_t math2_dot3(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3) {
	return (int32_t)((uint32_t)a1 * (uint32_t)b1 + (uint32_t)a2 * (uint32_t)b2 + (uint32_t)a3 * (uint32_t)b3);
}
#else
int32_t math2_dot3(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3);
#pragma aux math2_dot3 = \
	"imul eax, edx" \
	"imul ebx, esi" \
	"imul ecx, edi" \
	"add eax, ebx" \
	"add eax, ecx" \
	parm [eax] [edx] [ebx] [esi] [ecx] [edi] value [eax] modify exact [eax ebx ecx];
#endif

/* Clamped Q15 dot product of (a1, a2, a3) and (b1, b2, b3): the wrapping
 * 32-bit sum a1 * b1 + a2 * b2 + a3 * b3, saturated before the >> 15.
 * Negative sums clamp to -0x3FFF0000; positive sums clamp to 0x3FFF0000 in
 * TIE95 and 0x3FFFFFFF in TIE98. Unlike the other helpers it takes each
 * vector whole, which is the order the originals load the arguments. Both
 * originals inline this at every use (XvT's Math_Dot3Q15Wrapped is the TIE98
 * form); its name is not known. */
#if defined(TIE_MODERN)
static inline int32_t math2_dot3_q15_clamped(int32_t a1, int32_t a2, int32_t a3, int32_t b1, int32_t b2, int32_t b3) {
	int32_t sum = math2_dot3(a1, b1, a2, b2, a3, b3);
	if (sum >= 0x40000000)
		sum = TIE_FLIGHT_EDITION(0x3FFF0000, 0x3FFFFFFF);
	if (sum <= -0x40000000)
		sum = -0x3FFF0000;
	return sum >> 15;
}
#elif defined(TIE98)
static inline int32_t math2_dot3_q15_clamped(int32_t a1, int32_t a2, int32_t a3, int32_t b1, int32_t b2, int32_t b3) {
	__asm {
		mov eax, a1
		mov ebx, a2
		mov ecx, a3
		imul eax, b1
		imul ebx, b2
		imul ecx, b3
		add eax, ebx
		add eax, ecx
		cmp eax, 40000000h
		jl dc_positive_ok
		mov eax, 3fffffffh
	dc_positive_ok:
		cmp eax, 0c0000000h
		jg dc_negative_ok
		mov eax, 0c0010000h
	dc_negative_ok:
		sar eax, 15
		mov a1, eax
	}
	return a1;
}
#else
int32_t math2_dot3_q15_clamped(int32_t a1, int32_t a2, int32_t a3, int32_t b1, int32_t b2, int32_t b3);
#pragma aux math2_dot3_q15_clamped = \
	"imul eax, edx" \
	"imul ebx, esi" \
	"imul ecx, edi" \
	"add eax, ebx" \
	"add eax, ecx" \
	"cmp eax, 40000000h" \
	"jl dc_positive_ok" \
	"mov eax, 3fff0000h" \
	"dc_positive_ok: cmp eax, 0c0000000h" \
	"jg dc_negative_ok" \
	"mov eax, 0c0010000h" \
	"dc_negative_ok: sar eax, 15" \
	parm [eax] [ebx] [ecx] [edx] [esi] [edi] value [eax] modify exact [eax ebx ecx];
#endif

// clang-format on

#ifdef __cplusplus
}
#endif

#endif
