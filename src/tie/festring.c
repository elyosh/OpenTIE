#include "tie/festring.h"
#include "tie/tie.h"

#include "tie/edition.h"
#include "tie/sys2.h"

#include <string.h>

/* --- Cursor and margin setters --- */

// FUNCTION: TIE95 0x23670
// FUNCTION: TIE98 0x41D460
void festring_setcursor(FestringCoord x, FestringCoord y) {
	cursory = y;
	cursorx = x;
}

// FUNCTION: TIE95 0x23680
// FUNCTION: TIE98 0x41D480
void festring_setbound(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom) {
	topmargin = top;
	bottommargin = bottom;
	leftmargin = left;
	rightmargin = right;
}

/* --- Color setters (remap palette indices >= 0x40) --- */

// FUNCTION: TIE95 0x2369C
void festring_settextcolor(uint16_t color) {
	if (color >= 0x40)
		textcolor = color_remap_table[color];
	else
		textcolor = (uint8_t)color;
}

// FUNCTION: TIE95 0x236B8
void festring_setbackcolor(uint16_t color) {
	if (color >= 0x40)
		backcolor = color_remap_table[color];
	else
		backcolor = (uint8_t)color;
}

// FUNCTION: TIE95 0x236D4
void festring_setdropcolor(uint16_t color) {
	if (color >= 0x40)
		dropcolor = color_remap_table[color];
	else
		dropcolor = (uint8_t)color;
}

/* --- Flag setters --- */

// FUNCTION: TIE95 0x236F0
void festring_setlinewrap(int16_t enable) { lwrapflag = enable; }

// FUNCTION: TIE95 0x236F8
void festring_setautofill(int16_t enable) { autofillflag = enable; }

/* --- Font selection ---
 * Size 0: keep current font
 * Size 1: tiny font (9px height, 20 char size; hi-res: 21px, 170 char size), lowercase
 * Size 2: micro font (5px height, 12 char size; hi-res: 9px, 74 char size), uppercase only
 */
// FUNCTION: TIE95 0x23700
// FUNCTION: TIE98 0x41D530
void festring_setfontsize(int size) {
	int16_t char_size = fontcharsize;
	uint8_t height = fontheight;

	fontflag = (uint8_t)size;

	if ((uint8_t)size == 1) {
		curfontptr = fontptrtiny;
		if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
			height = 21;
			char_size = 170;
		} else {
			height = 9;
			char_size = 20;
		}
		fontlowercase = 1;
	} else if ((uint8_t)size == 2) {
		curfontptr = fontptrmicro;
		if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
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

// FUNCTION: TIE95 0x237A8
void festring_farstrcpy(const char* src) {
	char* dst = tempstring;
	while (*src)
		*dst++ = *src++;
	*dst = '\0';
}

// FUNCTION: TIE95 0x237C8
void festring_farstrcat(const char* src) {
	char* dst = tempstring;
	while (*dst)
		dst++;
	while (*src)
		*dst++ = *src++;
	*dst = '\0';
}

// FUNCTION: TIE95 0x237F8
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
// FUNCTION: TIE95 0x23820
void festring_outstring(const uint8_t* s) {
	uint16_t color;

	if (!*s)
		return;

	do {
		if (*s == 0xFE) {
			/* Color escape: next byte sets textcolor */
			color = *++s;
			if (color >= 0x40)
				textcolor = color_remap_table[color];
			else
				textcolor = (uint8_t)color;
		} else if (*s < 0x10) {
			/* Inline color code */
			color = *s;
			if (color >= 0x40)
				textcolor = color_remap_table[color];
			else
				textcolor = (uint8_t)color;
		} else {
			/* Printable character */
			outchar(*s);
		}
	} while (*++s);
}

// FUNCTION: TIE95 0x238B4
// FUNCTION: TIE98 0x41D6D0
void festring_outstringcenter(const uint8_t* s) {
	uint16_t half_len = (uint16_t)sys2_calclength(s) / 2;
	uint16_t center = (rightmargin + leftmargin) / 2;
	uint16_t x = center - half_len;

	if (x < leftmargin)
		x = leftmargin;

	if (TIE_FLIGHT_TIE98)
		festring_setcursor(x, cursory);
	else
		cursorx = x;
	festring_outstring(s);
}

// FUNCTION: TIE95 0x23914
void festring_outstringright(const uint8_t* s) {
	uint16_t width = sys2_calclength(s) + 2;
	uint16_t x = rightmargin;

	x -= width;

	if (x >= 0x8000)
		x = 0;
	if (x < leftmargin)
		cursorx = leftmargin;
	else
		cursorx = x;
	festring_outstring(s);
}

/* --- Screen operations --- */

// FUNCTION: TIE95 0x23964
void festring_clearscreen(void) {
	topmargin = 0;
	bottommargin = 200;
	leftmargin = 0;
	rightmargin = 320;
	backcolor = 0;
	clearwindow();
}

// FUNCTION: TIE95 0x239A0
void festring_hidescreen(void) { blank(); }

// FUNCTION: TIE95 0x239A8
void festring_showscreen(void) { unblank(); }
