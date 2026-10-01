/* PANELRTS -- panel real-time helpers (2 functions + 5 globals). */

#include "tie/panelrts.h"
#include "tie/festring.h"
#include "tie/panel.h"
#include "tie/tie.h"

#include <stdint.h>

/* Runtime-loaded string table pointers. Written by fediskio_loadstringdata
 * during front-end boot; read by PANEL/MSG/GOALS/USER/REPLAY. */
// GLOBAL: TIE95 0xD5E48
// GLOBAL: TIE98 0x5FBF98
void* buoystr;
// GLOBAL: TIE95 0xD5E4C
// GLOBAL: TIE98 0x5FBFA0
void* warheadstrings;
// GLOBAL: TIE95 0xD5E50
// GLOBAL: TIE98 0x5FBF94
void* unknownstring;
// GLOBAL: TIE95 0xD5E54
// GLOBAL: TIE98 0x5FBF9C
void* statusstrings;

/* Place-value table, 6 entries matching the 12-byte binary layout.
 * placevalue[pos] is the divisor used to extract the digit at position
 * 'pos' (1-based: 1=ones, 2=tens, ..., 5=ten-thousands). Entry 0 is dead. */
// GLOBAL: TIE95 0xC7204
uint16_t placevalue[6] = { 1, 1, 10, 100, 1000, 10000 };

// FUNCTION: TIE95 0x44E30
uint16_t panelrts_setnewpilotview(uint16_t view_idx) {
	if (panelviewdefs[view_idx].flags == 0)
		return 0;

	if (camera.pilotview != (uint8_t)view_idx) {
		camera.pilotview = (uint8_t)view_idx;
		camera.pilotview_save = (uint8_t)view_idx;
		if (!replayviewmode)
			panel_dosetnewpilotview(view_idx);
	}
	return 1;
}

// FUNCTION: TIE95 0x44E8C
void panelrts_outnum(uint16_t value, uint16_t ndigits, uint16_t minpad) {
	uint16_t saved_textcolor;
	uint16_t saved_dropflag;
	uint16_t leading;
	uint16_t digit;

	/* "unknown" placeholder: draw ndigits '0' glyphs in color 0x40 with the
	 * drop-shadow disabled, then restore the previous drop / text state. */
	if (value == 0xFFFF) {
		saved_textcolor = textcolor;
		saved_dropflag = dropflag;
		dropflag = 0;
		festring_settextcolor(0x40);
		while (ndigits > 0) {
			ndigits--;
			outchar('0');
		}
		dropflag = saved_dropflag;
		textcolor = saved_textcolor;
	} else {
		leading = 0;
		while (ndigits > 0) {
			uint16_t divisor = placevalue[ndigits];

			digit = value / divisor;
			value -= digit * divisor;
			if (leading == 0 && ndigits > minpad && digit == 0) {
				digit = ' ';
			} else {
				leading = 1;
				if (digit > 9)
					digit = 9;
				digit += '0';
			}
			ndigits--;
			outchar((uint8_t)digit);
		}
	}
}
