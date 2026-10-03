#include "tie/msg.h"
#ifdef TIE_MODERN
#include "tie_runtime/storage/string_table.h"
#endif
#include "tie/create.h"
#include "tie/fediskio.h" /* acceleratedtimesetting */
#include "tie/festring.h"
#include "tie/fsfx.h"
#include "tie/msgroom.h"
#include "tie/panelrts.h" /* buoystr, warheadstrings, placevalue, panelrts_outnum */
#include "tie/spec.h"
#include "tie/tie.h"
#include "tie/trig2.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// GLOBAL: TIE95 0xC5850
// GLOBAL: TIE98 0x4E62F0
uint8_t fontcolors[32] = { 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
						   0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0xD5, 0xD5,
						   0xD4, 0xD3, 0x2C, 0x2D, 0x2E, 0x2F, 0x2C, 0x2D, 0x2E, 0x2F };
// GLOBAL: TIE95 0xC5870
// GLOBAL: TIE98 0x4E6310
uint8_t fontcolorconvert[8] = { 0x42, 0x4A, 0x46, 0x4E, 0x52, 0x45, 0x42, 0x52 };
// GLOBAL: TIE95 0xC5878
// GLOBAL: TIE98 0x4E6318
uint8_t radiosidecolors[6] = { 0x4A, 0x52, 0x46, 0x56, 0x4A, 0x56 };
// GLOBAL: TIE95 0xC587E
// GLOBAL: TIE98 0x4E6320
uint8_t eventsidecolors[6] = { 0x52, 0x4A, 0x46, 0x56, 0x4A, 0x56 };
// GLOBAL: TIE95 0xC5884
// GLOBAL: TIE98 0x584D10
uint8_t frameticksmsgflag;
// GLOBAL: TIE95 0xD4C6C
// GLOBAL: TIE98 0x5FD220
char* messageptrs[4];
// GLOBAL: TIE95 0xD4C80
// GLOBAL: TIE98 0x5FD230
int32_t msgLineRight;
// GLOBAL: TIE95 0xD4C7C
// GLOBAL: TIE98 0x5FD20C
char** messagetable;
// GLOBAL: TIE95 0xD4C84
// GLOBAL: TIE98 0x5FD214
int32_t msgLineBottom;
// GLOBAL: TIE95 0xD4C88
// GLOBAL: TIE98 0x5FD200
int32_t msgLineTop;
// GLOBAL: TIE95 0xD4C8C
// GLOBAL: TIE98 0x5FCE60
MsgHistoryEntry messagequeue[MSG_QUEUE_SLOTS];
// GLOBAL: TIE95 0xD5028
// GLOBAL: TIE98 0x5FD1FC
uint16_t dxtticks;
// GLOBAL: TIE95 0xD502A
// GLOBAL: TIE98 0x5FD210
uint16_t oxtticks;
// GLOBAL: TIE95 0xD502E
// GLOBAL: TIE98 0x5FD206
uint16_t currentmessagesave;
// GLOBAL: TIE95 0xD5030
// GLOBAL: TIE98 0x5FD208
uint8_t messagecnt;

/* Cue selected by SCORE for the next voiced message. */
// GLOBAL: TIE95 0xD502C
// GLOBAL: TIE98 0x5FD204
uint16_t pending_voice_id;

/* --- msg_messageinit -- */

// FUNCTION: TIE95 0x32EE0
uint16_t msg_messageinit(void) {
	uint16_t prev;

	switch (flightResolution) {
		case TIE_FLIGHT_RES_SVGA:
#if defined(TIE98) || defined(TIE_MODERN)
		case TIE_FLIGHT_RES_SVGA_16:
		case TIE_FLIGHT_RES_SVGA_D3D:
#endif
			msgLineTop = 456;
			msgLineBottom = 480;
			msgLineRight = 596;
			break;
		case TIE_FLIGHT_RES_VGA:
			msgLineTop = 190;
			msgLineBottom = 200;
			msgLineRight = 298;
			break;
		default:
			msgLineTop = 190;
			msgLineBottom = 200;
			msgLineRight = 298;
			break;
	}

	festring_setlinewrap(0);
	festring_setautofill(0);
	festring_setfontsize(1);

	/* 1-pixel separator band above the message area. */
	festring_setbound(0, (int16_t)(msgLineTop - 1), (int16_t)screenXRes, (int16_t)msgLineTop);
	festring_setbackcolor(0x2D);
	clearwindow();

	/* Main message band. */
	festring_setbackcolor(0x2C);
	festring_setbound(0, (int16_t)msgLineTop, (int16_t)screenXRes, (int16_t)msgLineBottom);
	clearwindow();

	msg_timeout();
	festring_settextcolor(0x43);

	prev = messagequeue[0].template_idx;
	currentmessagesave = prev;
	messagequeue[0].template_idx = 0xFFFF;
	return prev;
}

/* --- msg_messagerestore --
 * In the binary this function falls through into MSG_messagedisplay
 * (12-byte thunk, no ret/jmp). Mirrored here as an explicit call. */

// FUNCTION: TIE95 0x3300C
void msg_messagerestore(void) {
	messagequeue[0].template_idx = currentmessagesave;
	msg_messagedisplay();
}

/* --- msg_messagedisplay -- */

// FUNCTION: TIE95 0x33018
void msg_messagedisplay(void) {
	const char* body;
	uint8_t type_byte;
	uint8_t last_ch;
	uint16_t chars_out;

	if (messagequeue[0].template_idx == (uint16_t)0xFFFF)
		return;

	msg_readymessage();

	/* Play a queued voice cue only on the first display. */
	if (messagequeue[0].voice_id && !messagequeue[0].display_count)
		fsfx_triggervoicesfx(messagequeue[0].voice_id);

	type_byte = (uint8_t)messagequeue[0].body[0];
	body = messagequeue[0].body;

	if (type_byte < 8) {
		festring_settextcolor(fontcolorconvert[type_byte]);
		body++;
		if (type_byte == 1) {
			/* Optional '0'..'3' sub-side selector after the type byte. */
			if ((uint8_t)*body >= '0' && (uint8_t)*body <= '3') {
				festring_settextcolor(radiosidecolors[(uint8_t)*body - '0']);
				body++;
			}
		} else if (type_byte == 2) {
			festring_settextcolor(eventsidecolors[messagequeue[0].side]);
		}
	} else {
		festring_settextcolor(0x42);
	}

	/* Walk body chars, honoring '[' dim / ']' brighten nudges, capped at 70. */
	chars_out = 0;
	while (*body && chars_out < 0x46) {
		if ((uint8_t)*body == '[') {
			if (textcolor == 0xD4)
				textcolor--;
			else
				textcolor++;
			body++;
		} else if ((uint8_t)*body == ']') {
			if (textcolor == 0xD3)
				textcolor++;
			else
				textcolor--;
			body++;
		} else {
			outchar((uint8_t)*body);
			last_ch = (uint8_t)*body++;
			chars_out++;
		}
	}

	msg_completemessage(messagequeue[0].msg_type, last_ch);
	messagequeue[0].display_count++;
}

/* --- msg_messageprintf -- */

// FUNCTION: TIE95 0x330CC
void msg_messageprintf(uint16_t template_id) {
	/* Retail queue and history share this record. */
	MsgHistoryEntry entry;
	const uint8_t* tpl;
	uint16_t body_len;
	uint16_t arg_idx;
	uint16_t new_type;

	entry.template_idx = template_id;
	/* Stamp the mission clock. */
	entry.subsecond = date.subsec;
	entry.seconds = date.second;
	entry.minutes = date.minute;
	entry.hours = date.hour;
	entry.age = 0;
	entry.display_count = 0;
	entry.side = messageside;
	/* Only the generic radio/objective templates carry a voice cue. */
	if (template_id == 174 || template_id == 161)
		entry.voice_id = pending_voice_id;
	else
		entry.voice_id = 0;

	/* Walk the template string, expanding '*' and '&N' opcodes into body[]. */
#ifdef TIE_MODERN
	if (template_id == MSG_PAUSED)
		tpl = (const uint8_t*)"\006Mission paused. Press your pause key or button to continue.";
	else
#endif
		tpl = (const uint8_t*)messagetable[template_id];
	arg_idx = 0;
	body_len = 0;

	while (*tpl && body_len < 70) {
		if (*tpl == '*') {
			int spec;
			const char* src;

			tpl++;
			spec = argtable[arg_idx++];
			if (spec >= 0x8000)
				src = messageptrs[spec & 0x7FFF];
			else
				src = messagetable[spec];
			while (*src && body_len < 70)
				entry.body[body_len++] = *src++;
		} else if (*tpl == '&') {
			uint16_t width;
			uint16_t value;
			uint16_t nonzero_seen;

			tpl++;
			width = *tpl++;
			value = argtable[arg_idx++];
			nonzero_seen = 0;

			while (width > 0) {
				uint16_t div = placevalue[width];
				uint16_t digit = value / div;

				value -= digit * div;
				if (!nonzero_seen && (int16_t)width > 1 && digit == 0) {
					digit = ' ';
				} else {
					nonzero_seen = 1;
					if (digit > 9)
						digit = 9;
					digit += '0';
				}
#ifdef TIE_MODERN
				if (body_len < 70)
#endif
					entry.body[body_len++] = (char)digit;
				width--;
			}
		} else {
			entry.body[body_len++] = *tpl++;
		}
	}

	if (body_len < 70)
		entry.body[body_len] = 0;
	else
		entry.body[69] = 0;

	/* msg_type from the raw template prefix byte, clamped >=8 -> 6. */
#ifdef TIE_MODERN
	if (template_id == MSG_PAUSED)
		entry.msg_type = 6;
	else
#endif
		if ((uint8_t)messagetable[template_id][0] < 8)
		entry.msg_type = (uint8_t)messagetable[template_id][0];
	else
		entry.msg_type = 6;

	new_type = entry.msg_type;

	/* History ring append (types 1 and 2 only, suppressed in replay view). */
	if (!replayviewmode && (new_type == 2 || new_type == 1)) {
		numhistorymsgs++;
		lasthistorymsg++;
		if ((uint16_t)lasthistorymsg == MSG_HISTORY_SLOTS)
			lasthistorymsg = 0;
		messagehistory = (MsgHistoryEntry*)xmemhdl_Lock_Handle(messageloghandle);
		xmemhdl_Unlock_Handle(messageloghandle);
		messagehistory[(uint16_t)lasthistorymsg] = entry;
	}

	/* Queue dispatch on the currently-displayed message's msg_type. */
	if (messagequeue[0].template_idx == (uint16_t)0xFFFF) {
		/* Slot empty -- overwrite and render. */
		messagequeue[0] = entry;
		msg_messagedisplay();
		return;
	}

	switch (messagequeue[0].msg_type) {
		case 3:
		case 6:
		case 7:
			messagequeue[0] = entry;
			msg_messagedisplay();
			break;
		case 2:
		case 5:
			/* Only events queue behind an event or briefing message. */
			if (new_type == 2) {
				messagequeue[messagecnt + 1] = entry;
				messagecnt++;
				if (messagecnt >= 10)
					messagecnt--;
			} else {
				msg_movecurrentmessageinqueue();
				messagequeue[0] = entry;
				msg_messagedisplay();
			}
			break;
		case 1:
			/* Radio and event messages queue behind an existing radio message. */
			if (new_type == 2 || new_type == 1) {
				messagequeue[messagecnt + 1] = entry;
				messagecnt++;
				if (messagecnt >= 10)
					messagecnt--;
			} else {
				msg_movecurrentmessageinqueue();
				messagequeue[0] = entry;
				msg_messagedisplay();
			}
			break;
		case 4:
			/* Reports discard type 3, queue radio/events, and yield to others. */
			if (new_type == 3)
				break;
			if (new_type == 2 || new_type == 1) {
				messagequeue[messagecnt + 1] = entry;
				messagecnt++;
				if (messagecnt >= 10)
					messagecnt--;
			} else {
				messagequeue[0] = entry;
				msg_messagedisplay();
			}
			break;
	}
}

/* --- msg_movecurrentmessageinqueue -- */

// FUNCTION: TIE95 0x33544
// FUNCTION: TIE98 0x4563E0
void msg_movecurrentmessageinqueue(void) {
	uint16_t i;

	if (messagequeue[0].display_count >= 2)
		return;
	if (messagequeue[0].age != 0)
		return;

	/* Shift queue[0..messagecnt] to queue[1..messagecnt+1]. */

	for (i = (uint16_t)(messagecnt + 1); i > 0; i--) {
		messagequeue[i] = messagequeue[i - 1];
	}
	messagecnt++;
	if (messagecnt >= 10)
		messagecnt--;
}

// FUNCTION: TIE95 0x335B8
// FUNCTION: TIE98 0x456450
void msg_getmessagefromqueue(void) {
	const uint8_t old_cnt = messagecnt;
	uint16_t i;
	for (i = 0; i < old_cnt; i++) {
		messagequeue[i] = messagequeue[i + 1];
	}
	messagecnt = (uint8_t)(old_cnt - 1);
}

/* --- msg_readymessage -- */

// FUNCTION: TIE95 0x336B0
void msg_readymessage(void) {
	festring_setfontsize(1);
	festring_setbackcolor(0x2C);
	dropflag = 1;
	festring_setdropcolor(0x40);
	festring_setbound(0, (int16_t)msgLineTop, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setcursor(2, (int16_t)msgLineTop);
	festring_settextcolor(0x43);
}

/* --- msg_completemessage -- */

// FUNCTION: TIE95 0x3371C
void msg_completemessage(uint16_t msg_type, char last_char) {
	if (last_char != '?' && last_char != '!' && last_char != ':' && last_char != ' ')
		outchar('.');
	festring_setautofill(1);
	outchar('\n');
	festring_setautofill(0);

	if (msg_type == 4) {
		timers[TIMER_MSG] = 1888;
	} else if (messagecnt) {
		timers[TIMER_MSG] = 354;
	} else if (msg_type == 2 || msg_type == 1) {
		timers[TIMER_MSG] = 1416;
	} else {
		timers[TIMER_MSG] = 944;
	}

	msg_timeout();
	festring_setfontsize(2);
}

/* --- msg_messageupdate -- */

// FUNCTION: TIE95 0x337D4
void msg_messageupdate(void) {
	/* Timer-driven queue advance. */
	if (timers[TIMER_MSG] == 0 && messagequeue[0].template_idx != 0xFFFF) {
		if (messagecnt) {
			uint16_t i;
			for (i = 0; i < messagecnt; i++) {
				memcpy(&messagequeue[i], &messagequeue[i + 1], sizeof(MsgHistoryEntry));
			}
			messagecnt--;
			msg_messagedisplay();
		} else {
			festring_setbackcolor(0x2C);
			festring_setbound(0, (int16_t)msgLineTop, (int16_t)msgLineRight, (int16_t)screenYRes);
			if (clearwindow)
				clearwindow();
			messagequeue[0].template_idx = 0xFFFF;
		}
	}

	/* Auto-cancel an armed SPACE prompt when its timer expires; for the
	 * laser-warning prompt (state==1), also auto-fire MSG 209 immediately
	 * unless the warned target is one of the three ship types {144,152,149}
	 * that require a manual SPACE confirm. */
	if (!timers[TIMER_SPACE_CONFIRM])
		pstate.space_confirm_action = 0;
	if (pstate.space_confirm_action == 1) {
		const uint8_t ship_idx = objects[(uint16_t)pstate.msg_arg_obj_idx].ship_idx;
		if (ship_idx != 144 && ship_idx != 152 && ship_idx != 149) {
			pstate.space_confirm_action = 0;
			msg_messageprintf(MSG_MISSILE_DESTROYED);
		}
	}

	/* Debug frame-timing overlay. */
	if (frameticksmsgflag) {
		festring_setbackcolor(0x2C);
		festring_setbound((int16_t)msgLineRight, (int16_t)msgLineTop, (int16_t)screenXRes,
						  (int16_t)screenYRes);
		if (clearwindow)
			clearwindow();
		msg_readymessage();
		festring_setcursor((int16_t)(msgLineRight - fontheight), (int16_t)msgLineTop);
		festring_settextcolor(0x43);
		panelrts_outnum(frameticks, 2, 2);
		festring_setcursor((int16_t)msgLineRight, (int16_t)msgLineTop);
		panelrts_outnum(dxtticks, 2, 2);
		festring_setcursor((int16_t)(msgLineRight + fontheight), (int16_t)msgLineTop);
		panelrts_outnum(oxtticks, 2, 2);
		festring_setfontsize(2);
	}
}

/* --- msg_clearmessagequeue -- */

// FUNCTION: TIE95 0x339E8
// FUNCTION: TIE98 0x4568D0
void msg_clearmessagequeue(void) {
	messagecnt = 0;
	messagequeue[0].template_idx = 0xFFFF;
}

/* --- msg_updatemessageage -- */

// FUNCTION: TIE95 0x33A00
// FUNCTION: TIE98 0x4568F0
void msg_updatemessageage(void) {
	if (messagequeue[0].template_idx != (uint16_t)0xFFFF)
		messagequeue[0].age++;
}

/* --- msg_timeout -- */

// FUNCTION: TIE95 0x33A14
void msg_timeout(void) {
	static const char ts_prefix[] = "T:";
	festring_setbackcolor(0x2C);
	festring_setbound((int16_t)msgLineRight, (int16_t)msgLineTop, (int16_t)screenXRes, (int16_t)screenYRes);
	if (clearwindow)
		clearwindow();
	msg_readymessage();
	festring_setcursor((int16_t)msgLineRight, (int16_t)msgLineTop);
	festring_settextcolor(0x46);
	festring_outstring((const uint8_t*)ts_prefix);
	festring_settextcolor(0x4E);
	panelrts_outnum(acceleratedtimesetting, 2, 1);
	festring_settextcolor(0x43);
	if (outchar)
		outchar('x');
}

/* --- msg_reportfgcreation -- */

// FUNCTION: TIE95 0x33AB8
void msg_reportfgcreation(uint16_t fg_idx, uint16_t species_idx) {
	/* Locate the FG's lead object (leader_obj_idx == 255) or fall back to
	 * CREATE_getworldposition(0x8000, fg_idx) anchor. */
	uint16_t clicks;
	uint16_t count;
	uint16_t tpl;

	if (!fg_array[fg_idx].start_fg_used) {
		create_getworldposition(0x8000, fg_idx);
	} else {
		uint16_t i;

		for (i = 0; i < NUM_CRAFTS; i++) {
			if (objects[i].ship_idx) {
				CraftData* craft = objects[i].craft_ptr;

				if (objects[i].fg_idx == fg_idx && craft->leader_obj_idx == 255) {
					create_getworldposition(i, fg_idx);
					break;
				}
			}
		}
	}

	/* Convert world delta to polar, distance in game "clicks". */
	trig2_ctop(worldlocx - pstate.player->world_x, worldlocy - pstate.player->world_y,
			   worldlocz - pstate.player->world_z);
	trig2_polardistance *= 161;
	clicks = ((uint16_t)(trig2_polardistance >> 16) + 50) / 100;
	if (clicks == 0)
		clicks = 1;

	count = (int8_t)fg_array[fg_idx].count;
	argtable[0] = count;

	if ((int8_t)fg_array[fg_idx].side != 1) {
		/* Hostile sighting -- color line by side via messageside. */
		messageside = (int8_t)fg_array[fg_idx].side;
#ifdef TIE_MODERN
		messageptrs[1] = (char*)spec_name_ptrs[species_idx];
#else
		messageptrs[1] = (char*)spec_data[species_idx].name_ptr;
#endif
		argtable[1] = (uint16_t)0x8001;
		argtable[2] = clicks;
		if (count == 1)
			msg_messageprintf(MSG_NEW_CRAFT_ALERT);
		else
			msg_messageprintf(MSG_NEW_CRAFT_ALERT_PLURAL);
	} else if (count == 1) {
		/* Friendly/blue report. */
#ifdef TIE_MODERN
		messageptrs[0] = (char*)spec_name_ptrs[species_idx];
#else
		messageptrs[0] = (char*)spec_data[species_idx].name_ptr;
#endif
		argtable[0] = (uint16_t)0x8000; /* overwrite the count */
		messageptrs[1] = fg_array[fg_idx].name;
		argtable[2] = clicks;
		argtable[1] = (uint16_t)0x8001;
		msg_messageprintf(MSG_CRAFT_ENTERING_AT);
	} else {
		messageptrs[2] = fg_array[fg_idx].name;
		argtable[3] = clicks;
		argtable[2] = (uint16_t)0x8002;
#ifdef TIE_MODERN
		messageptrs[1] = (char*)spec_name_ptrs[species_idx];
#else
		messageptrs[1] = (char*)spec_data[species_idx].name_ptr;
#endif
		argtable[1] = (uint16_t)0x8001;
		msg_messageprintf(MSG_CRAFT_GROUP_ENTERING_AT);
	}
}

/* --- msg_addmessageptr -- */

// FUNCTION: TIE95 0x33CF4
uint16_t msg_addmessageptr(uint16_t slot_idx, char* ptr) {
	uint16_t tagged;

	messageptrs[slot_idx] = ptr;
	tagged = (uint16_t)(slot_idx + 0x8000);
	argtable[slot_idx] = tagged;
	return tagged;
}

/* --- msg_craftmessage -- */

// FUNCTION: TIE95 0x33D10
void msg_craftmessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id) {
	uint16_t fg_idx;

	messageside = objects[obj_idx].side;
#ifdef TIE_MODERN
	messageptrs[0] = (char*)spec_name_ptrs[craft->species_idx];
#else
	messageptrs[0] = (char*)spec_data[craft->species_idx].name_ptr;
#endif
	argtable[0] = 0x8000;
	fg_idx = objects[obj_idx].fg_idx;
	messageptrs[1] = fg_array[fg_idx].name;
	argtable[1] = 0x8001;

	if ((int8_t)fg_array[fg_idx].count > 1) {
		argtable[2] = (uint16_t)(craft->craft_idx_in_fg + 1);
		argtable[3] = msg_template_id;
		msg_messageprintf(MSG_CRAFT_REPORT_FG);
	} else {
		/* Single craft FG -- no "#N". */
		argtable[2] = msg_template_id;
		msg_messageprintf(MSG_CRAFT_REPORT);
	}
}

/* --- msg_radiomessage -- */

// FUNCTION: TIE95 0x33DD0
void msg_radiomessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id, uint16_t cmdr_mode) {
	MsgTemplate tpl;
	if (cmdr_mode) {
		const uint8_t fg_idx = objects[obj_idx].fg_idx;
		argtable[1] = msg_template_id;
		messageptrs[0] = fg_array[fg_idx].name;
		tpl = MSG_ACK_FG_PLAIN;
		argtable[0] = 0x8000;
	} else {
		uint8_t fg_idx;

		argtable[0] = 0x8000;
		messageptrs[0] = spec_data[craft->species_idx].short_name;
		fg_idx = objects[obj_idx].fg_idx;
		messageptrs[1] = fg_array[fg_idx].name;
		argtable[1] = 0x8001;
		if ((int8_t)fg_array[fg_idx].count == 1) {
			tpl = MSG_ACK_FG;
			argtable[2] = msg_template_id;
		} else {
			argtable[2] = (uint16_t)(craft->craft_idx_in_fg + 1);
			tpl = MSG_ACK_FG_INDEXED;
			argtable[3] = msg_template_id;
		}
	}
	msg_messageprintf(tpl);
	fsfx_speakorderack(obj_idx, msg_template_id, cmdr_mode);
}

/* --- msg_reportmessage -- */

// FUNCTION: TIE95 0x33EF4
void msg_reportmessage(int obj_idx, CraftData* craft, uint16_t msg_template_id) {
	uint16_t fg_idx;

	argtable[0] = 0x8000;
	messageptrs[0] = spec_data[craft->species_idx].short_name;
	fg_idx = objects[obj_idx].fg_idx;
	argtable[1] = 0x8001;
	messageptrs[1] = fg_array[fg_idx].name;
	if ((int8_t)fg_array[fg_idx].count > 1) {
		argtable[2] = (uint16_t)(craft->craft_idx_in_fg + 1);
		argtable[3] = msg_template_id;
		msg_messageprintf(MSG_REPORTING_FG_INDEXED);
	} else {
		argtable[2] = msg_template_id;
		msg_messageprintf(MSG_REPORTING_FG);
	}
}

/* --- msg_createobjectname -- Compose a printable object label.
 * TIE95 inlines the string and character append operations. */

// FUNCTION: TIE95 0x33FA8
void msg_createobjectname(uint16_t obj_idx, int16_t use_official, char* out_buf) {
	uint8_t ship_idx;

	*out_buf = 0;
	if (obj_idx < 0x3800) {
		ship_idx = objects[obj_idx].ship_idx;
		if (objects[obj_idx].category == 0) {
			CraftData* craft = objects[obj_idx].craft_ptr;
			const char* src;
			char* dst;

			if (use_official) {
#ifdef TIE_MODERN
				src = spec_name_ptrs[craft->species_idx];
#else
				src = spec_data[craft->species_idx].name_ptr;
#endif
				dst = out_buf;
				while (*dst)
					dst++;
				while (*src)
					*dst++ = *src++;
				*dst = 0;
			} else {
				src = spec_data[craft->species_idx].short_name;
				dst = out_buf;
				while (*dst)
					dst++;
				while (*src)
					*dst++ = *src++;
				*dst = 0;
			}

			if (fg_array[objects[obj_idx].fg_idx].name[0]) {
				dst = out_buf;
				while (*dst)
					dst++;
				*dst++ = ' ';
				*dst = 0;

				src = fg_array[objects[obj_idx].fg_idx].name;
				dst = out_buf;
				while (*dst)
					dst++;
				while (*src)
					*dst++ = *src++;
				*dst = 0;
			}

			if ((int8_t)fg_array[objects[obj_idx].fg_idx].count > 1) {
				dst = out_buf;
				while (*dst)
					dst++;
				*dst++ = ' ';
				*dst = 0;

				dst = out_buf;
				while (*dst)
					dst++;
				*dst++ = (char)(craft->craft_idx_in_fg + '1');
				*dst = 0;
			}
		} else if (ship_idx >= 0x8F && ship_idx <= 0x9A) {
			const char* src = ((char**)warheadstrings)[ship_idx - 143];
			char* dst = out_buf;
			while (*dst)
				dst++;
			while (*src)
				*dst++ = *src++;
			*dst = 0;
		} else if (ship_idx >= 0x46 && ship_idx <= 0x54) {
			const char* src = ((char**)buoystr)[ship_idx - 70];
			char* dst = out_buf;
			while (*dst)
				dst++;
			while (*src)
				*dst++ = *src++;
			*dst = 0;
		}
	} else {
		uint16_t static_idx = (uint16_t)(obj_idx - 0x3800);
		const char* src = ((char**)buoystr)[staticobjects[static_idx].species - 70];
		char* dst = out_buf;

		while (*dst)
			dst++;
		while (*src)
			*dst++ = *src++;
		*dst = 0;

		if (fg_array[staticobjects[static_idx].fg_idx].name[0]) {
			dst = out_buf;
			while (*dst)
				dst++;
			*dst++ = ' ';
			*dst = 0;

			src = fg_array[staticobjects[static_idx].fg_idx].name;
			dst = out_buf;
			while (*dst)
				dst++;
			while (*src)
				*dst++ = *src++;
			*dst = 0;
		}
	}
}

/* Append a string to the end of dst. */

// FUNCTION: TIE95 0x34300
// FUNCTION: TIE98 0x457070
void msg_msgstrcat(const char* src, char* dst) {
	while (*dst)
		dst++;
	while (*src) {
		*dst++ = *src++;
	}
	*dst = 0;
}

/* Append a character and terminate dst. */

// FUNCTION: TIE95 0x34324
// FUNCTION: TIE98 0x4570A0
void msg_msgstradd(char ch, char* dst) {
	while (*dst)
		dst++;
	*dst++ = ch;
	*dst = 0;
}
