#ifndef FILM_FOURCC_H
#define FILM_FOURCC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MK4CC(a, b, c, d)                                                                                    \
	(((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | ((uint32_t)(uint8_t)(c) << 8) |       \
	 (uint32_t)(uint8_t)(d))

#define FCC_FILM MK4CC('F', 'I', 'L', 'M')
#define FCC_DELT MK4CC('D', 'E', 'L', 'T')
#define FCC_ANIM MK4CC('A', 'N', 'I', 'M')
#define FCC_RAW MK4CC('R', 'A', 'W', ' ')
#define FCC_CUST MK4CC('C', 'U', 'S', 'T')
#define FCC_PLTT MK4CC('P', 'L', 'T', 'T')
#define FCC_VIEW MK4CC('V', 'I', 'E', 'W')
#define FCC_VOIC MK4CC('V', 'O', 'I', 'C')
#define FCC_GMID MK4CC('G', 'M', 'I', 'D')

static inline uint32_t TieFilmFourcc_Be(const uint8_t* p) { return MK4CC(p[0], p[1], p[2], p[3]); }

static inline void TieFilmFourcc_Str(uint32_t v, char out[5]) {
	out[0] = (char)((v >> 24) & 0xFF);
	out[1] = (char)((v >> 16) & 0xFF);
	out[2] = (char)((v >> 8) & 0xFF);
	out[3] = (char)(v & 0xFF);
	out[4] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif
