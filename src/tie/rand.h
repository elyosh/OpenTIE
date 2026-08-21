#ifndef TIE_RAND_H
#define TIE_RAND_H

#include <stdint.h>

/*
 * RAND — retail's "standard" random number generator.
 *
 * Distinct from MATH2_getrandom (LFSR used by gameplay / mission logic where
 * replay determinism matters). RAND_rand is a classic glibc-style linear
 * congruential generator used by front-end / non-deterministic code: star
 * field init, backdrop tile pick, sfx selection variance, cockpit-light
 * flicker, pilot-record protect challenge, etc.
 *
 * Retail Z_TIE__.EXE:
 *   RAND_rand            @ 0x8D59D
 *   next (static seed)   @ 0xD2B90, statically initialised to 1.
 *
 * Algorithm (verified from asm at 0x8D5A7..0x8D5BA):
 *     next = next * 0x41C64E6D + 0x3039;      // 1103515245 * next + 12345
 *     return (next >> 16) & 0x7FFF;           // top 15 bits of high word
 *
 * Return type is int (matching classic rand()); range is 0..0x7FFF.
 */
int rand_rand(void);

#endif /* TIE_RAND_H */
