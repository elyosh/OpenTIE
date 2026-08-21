#include "tie/festring.h"
#include "tie/tie.h"

#include <string.h>

#include "tie/sys2.h"

/* --- Remap helper --- */

static uint8_t remap_color(uint16_t color) {
	if (color >= 0x40)
		return color_remap_table[color];
	return (uint8_t)color;
}

/* --- Cursor and margin setters --- */

// FUNCTION: TIE 0x23670, TIE98 0x41D460
void festring_setcursor(int16_t x, int16_t y) {
	cursorx = x;
	cursory = y;
}

// FUNCTION: TIE 0x23680, TIE98 0x41D480
void festring_setbound(int16_t left, int16_t top, int16_t right, int16_t bottom) {
	leftmargin = left;
	topmargin = top;
	rightmargin = right;
	bottommargin = bottom;
}

/* --- Color setters (remap palette indices >= 0x40) --- */

// FUNCTION: TIE 0x2369C
void festring_settextcolor(uint16_t color) { textcolor = remap_color(color); }

// FUNCTION: TIE 0x236B8
void festring_setbackcolor(uint16_t color) { backcolor = remap_color(color); }

// FUNCTION: TIE 0x236D4
void festring_setdropcolor(uint16_t color) { dropcolor = remap_color(color); }

/* --- Flag setters --- */

// FUNCTION: TIE 0x236F0
void festring_setlinewrap(int16_t enable) { lwrapflag = enable; }

void festring_setautofill(int16_t enable) { autofillflag = enable; }

/* --- Font selection ---
 * Size 0: keep current font
 * Size 1: tiny font (9px height, 20 char size; hi-res: 21px, 170 char size), lowercase
 * Size 2: micro font (5px height, 12 char size; hi-res: 9px, 74 char size), uppercase only
 */
// FUNCTION: TIE 0x23700, TIE98 0x41D530
void festring_setfontsize(int16_t size) {
	int16_t char_size = fontcharsize;
	uint8_t height = fontheight;

	fontflag = (uint8_t)size;

	if ((uint8_t)size == 1) {
		curfontptr = fontptrtiny;
		if (tie_is_high_resolution_flight()) {
			height = 21;
			char_size = 170;
		} else {
			height = 9;
			char_size = 20;
		}
		fontlowercase = 1;
	} else if ((uint8_t)size == 2) {
		curfontptr = fontptrmicro;
		if (tie_is_high_resolution_flight()) {
			height = 9;
			char_size = 74;
		} else {
			height = 5;
			char_size = 12;
		}
		fontlowercase = 0;
	}

	fontheight = height;
	fontcharsize = char_size;
}

/* --- String buffer operations (on the global tempstring[40]) --- */

// FUNCTION: TIE 0x237A8
void festring_farstrcpy(const char* src) {
	char* dst = tempstring;
	while (*src)
		*dst++ = *src++;
	*dst = '\0';
}

// FUNCTION: TIE 0x237C8
void festring_farstrcat(const char* src) {
	char* dst = tempstring;
	while (*dst)
		dst++;
	while (*src)
		*dst++ = *src++;
	*dst = '\0';
}

// FUNCTION: TIE 0x237F8
void festring_farstradd(char c) {
	char* dst = tempstring;
	while (*dst)
		dst++;
	*dst++ = c;
	*dst = '\0';
}

/* --- String output ---
 * Outputs a null-terminated string at the current cursor position.
 * Inline escape codes:
 *   0x00:        end of string
 *   0x01-0x0F:   set textcolor directly (or remap if >= 0x40)
 *   0x10-0xFD:   printable character, passed to outchar()
 *   0xFE:        color escape: next byte sets textcolor (with remap)
 *   0xFF:        (not used as escape; treated as printable)
 */
// FUNCTION: TIE 0x23820
void festring_outstring(const uint8_t* s) {
	if (!*s)
		return;

	while (1) {
		uint8_t ch = *s;

		if (ch == 0xFE) {
			/* Color escape: read next byte as new textcolor */
			s++;
			textcolor = remap_color(*s);
			if (!*++s)
				return;
			continue;
		}

		if (ch >= 0x10) {
			/* Printable character */
			outchar(ch);
			if (!*++s)
				return;
		} else {
			/* Inline color code (0x01-0x0F) */
			textcolor = remap_color(ch);
			if (!*++s)
				return;
		}
	}
}

// FUNCTION: TIE 0x238B4, TIE98 0x41D6D0
void festring_outstringcenter(const uint8_t* s) {
	int16_t center = ((uint16_t)rightmargin + (uint16_t)leftmargin) / 2;
	int16_t half_len = (uint16_t)sys2_calclength(s) / 2;
	int16_t x = center - half_len;

	if ((uint16_t)x < (uint16_t)leftmargin)
		x = leftmargin;

	cursorx = x;
	festring_outstring(s);
}

// FUNCTION: TIE 0x23914
void festring_outstringright(const uint8_t* s) {
	uint16_t x = rightmargin - (sys2_calclength(s) + 2);

	if (x >= 0x8000u)
		x = 0;
	if (x < (uint16_t)leftmargin)
		x = leftmargin;

	cursorx = x;
	festring_outstring(s);
}

/* --- Screen operations --- */

// FUNCTION: TIE 0x23964
void festring_clearscreen(void) {
	topmargin = 0;
	bottommargin = 200;
	leftmargin = 0;
	rightmargin = 320;
	backcolor = 0;
	clearwindow();
}

// FUNCTION: TIE 0x239A0
void festring_hidescreen(void) { blank(); }

// FUNCTION: TIE 0x239A8
void festring_showscreen(void) { unblank(); }
