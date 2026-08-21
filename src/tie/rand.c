/*
 * RAND — standard-C-style LCG. See rand.h for the retail pedigree.
 *
 * Retail addresses:
 *   0x8D59D  RAND_rand             (this function)
 *   0x8D597  RAND_initrandnext_0   (returns &next)
 *   0xD2B90  next                  (static seed, initial value = 1)
 *
 * Retail asm at 0x8D5A7..0x8D5BA:
 *     imul edx, [next], 0x41C64E6D
 *     add  edx, 0x3039
 *     mov  [next], edx
 *     mov  eax, edx
 *     shr  eax, 0x10
 *     and  eax, 0x7FFF
 *
 * i.e. `next = next * 1103515245 + 12345` then return the top 15 bits of
 * the high 16-bit word.
 */

#include "tie/rand.h"

/* Retail initial value is 1 (verified from bytes at 0xD2B90 in
 * Z_TIE__.EXE: 01 00 00 00). */
static uint32_t rand_next = 1;

int rand_rand(void) {
	rand_next = rand_next * 1103515245u + 12345u;
	return (int)((rand_next >> 16) & 0x7FFF);
}
