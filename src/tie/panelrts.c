/* PANELRTS -- panel real-time helpers (2 functions + 5 globals). */

#include <stdint.h>

#include "tie/festring.h"
#include "tie/panel.h"
#include "tie/panelrts.h"
#include "tie/tie.h"

/* Runtime-loaded string table pointers. Written by fediskio_loadstringdata
 * during front-end boot; read by PANEL/MSG/GOALS/USER/REPLAY. */
void* buoystr;
void* warheadstrings;
void* unknownstring;
void* statusstrings;

/* Place-value table, 6 entries matching the 12-byte binary layout.
 * placevalue[pos] is the divisor used to extract the digit at position
 * 'pos' (1-based: 1=ones, 2=tens, ..., 5=ten-thousands). Entry 0 is dead. */
// GLOBAL: TIE 0xC7204
uint16_t placevalue[6] = { 1, 1, 10, 100, 1000, 10000 };

// FUNCTION: TIE 0x44E30
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

// FUNCTION: TIE 0x44E8C
void panelrts_outnum(int32_t value, uint16_t ndigits, uint16_t minpad) {
	/* "unknown" placeholder: draw ndigits '0' glyphs in color 0x40 with the
	 * drop-shadow disabled, then restore the previous drop / text state.
	 * Matches the binary's 32-bit compare on eax (only exactly 0xFFFF). */
	if ((uint32_t)value == 0xFFFFu) {
		uint8_t saved_dropflag = dropflag;
		uint8_t saved_textcolor = textcolor;

		dropflag = 0;
		festring_settextcolor(0x40);
		while (ndigits) {
			--ndigits;
			outchar('0');
		}
		dropflag = saved_dropflag;
		textcolor = saved_textcolor;
		return;
	}

	if (ndigits == 0)
		return;

	/* The binary only consumes the low 16 bits of eax for the digit loop
	 * (after the 0xFFFF sentinel check on the full 32-bit register). */
	uint16_t rolling = (uint16_t)value;
	uint16_t pos = ndigits;
	uint16_t leading_nonzero = 0;

	do {
		const uint16_t divisor = placevalue[pos];
		uint16_t digit = (uint16_t)(rolling / divisor);
		rolling = (uint16_t)(rolling - digit * divisor);

		uint8_t out_ch;
		if (leading_nonzero || pos <= minpad || digit != 0) {
			leading_nonzero = 1;
			if (digit > 9)
				digit = 9;
			out_ch = (uint8_t)('0' + digit);
		} else {
			out_ch = ' ';
		}
		--pos;
		outchar(out_ch);
	} while (pos);
}
