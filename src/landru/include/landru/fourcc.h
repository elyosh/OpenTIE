#ifndef LANDRU_FOURCC_H
#define LANDRU_FOURCC_H

#include <stdint.h>

/* Pack four ASCII characters into a 32-bit FOURCC value, high byte first.
 * Endian-safe: the result is a numeric value computed by shifts, independent
 * of host byte order. Single-character literals are well-defined (no
 * implementation-defined multi-character literal warnings). */
#define MK4CC(a, b, c, d)                                                                                    \
	(((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | ((uint32_t)(uint8_t)(c) << 8) |       \
	 (uint32_t)(uint8_t)(d))

/* Resource container tags (.LFD top-level) */
#define FOURCC_RMAP MK4CC('R', 'M', 'A', 'P')

/* Asset resource types */
#define FOURCC_ANIM MK4CC('A', 'N', 'I', 'M')
#define FOURCC_CUST MK4CC('C', 'U', 'S', 'T')
#define FOURCC_DELT MK4CC('D', 'E', 'L', 'T')
#define FOURCC_FILM MK4CC('F', 'I', 'L', 'M')
#define FOURCC_FONT MK4CC('F', 'O', 'N', 'T')
#define FOURCC_GMID MK4CC('G', 'M', 'I', 'D')
#define FOURCC_MTRX MK4CC('M', 'T', 'R', 'X')
#define FOURCC_PLTT MK4CC('P', 'L', 'T', 'T')
#define FOURCC_SHIP MK4CC('S', 'H', 'I', 'P')
#define FOURCC_TEXT MK4CC('T', 'E', 'X', 'T')
#define FOURCC_VIEW MK4CC('V', 'I', 'E', 'W')
#define FOURCC_VOIC MK4CC('V', 'O', 'I', 'C')

/* Input widget type tags */
#define FOURCC_BACK MK4CC('B', 'A', 'C', 'K')
#define FOURCC_BUTN MK4CC('B', 'U', 'T', 'N')
#define FOURCC_CHCK MK4CC('C', 'H', 'C', 'K')
#define FOURCC_FRAM MK4CC('F', 'R', 'A', 'M')
#define FOURCC_INPT MK4CC('I', 'N', 'P', 'T')
#define FOURCC_SBTN MK4CC('S', 'B', 'T', 'N')
#define FOURCC_SLDR MK4CC('S', 'L', 'D', 'R')
#define FOURCC_STRG MK4CC('S', 'T', 'R', 'G')

#endif
