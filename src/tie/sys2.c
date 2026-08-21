/*
 * SYS2 — system utility functions.
 * calclength computes pixel width of strings using the current font.
 * checkctrlkey is a DOS keyboard stub (always returns 0).
 */

#include "tie/sys2.h"
#include "tie/tie.h"

// FUNCTION: TIE 0x559E0
int sys2_checkctrlkey(void) { return 0; }

/*
 * Compute the pixel width of a string using the current font.
 * Walks the string, skipping:
 *   - NUL or newline (0x0A): terminates
 *   - Bytes < 0x20: inline color codes (skipped, zero width)
 *   - 0xFE: color escape (skip this byte AND the next)
 * For printable chars (>= 0x20), looks up the char width from the font data:
 *   width = font_data[(ch - 32) * fontcharsize]
 * If the font doesn't support lowercase (fontlowercase == 0), a-z are
 * uppercased before lookup.
 */
// FUNCTION: TIE 0x559E4
int16_t sys2_calclength(const uint8_t* s) {
	int total_width = 0;

	while (1) {
		uint8_t ch = *s++;

		if (!ch || ch == 0x0A)
			break;

		if (ch < 0x20)
			continue; /* inline color code, skip */

		if (ch == 0xFE) {
			s++; /* color escape: skip the next byte too */
			continue;
		}

		/* Uppercase if font doesn't support lowercase */
		if (fontflag && !fontlowercase && ch >= 'a' && ch <= 'z')
			ch -= 32;

		/* Look up character width from font data */
		uint8_t* font = (uint8_t*)curfontptr;
		total_width += font[(ch - 32) * fontcharsize];
	}

	return total_width;
}
