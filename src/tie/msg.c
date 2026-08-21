#include "tie/msg.h"
#include "tie/create.h"
#include "tie/fediskio.h" /* acceleratedtimesetting */
#include "tie/festring.h"
#include "tie/fsfx.h"
#include "tie/msgroom.h"
#include "tie/panelrts.h" /* buoystr, warheadstrings, placevalue, panelrts_outnum */
#include "tie/spec.h"     /* spec_name_ptrs[] (64-bit-safe side table) */
#include "tie/tie.h"
#include "tie/trig2.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- External references (globals owned by other modules) --- */

/* last-set speaker side for MsgHistoryEntry.side */
/* template substitution slots */
/* mission elapsed clock now lives in
 * _date (MissionClock) declared in tie.h */
/* timers[TIMER_SPACE_CONFIRM] = auto-cancel for the prompt below */
/* pending SPACE-bar action queued by laser/collision/input prompts */

/* --- Module globals (watdbg: msg.c ownership) --- */

uint8_t fontcolors[32] = { 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
						   0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0xD5, 0xD5,
						   0xD4, 0xD3, 0x2C, 0x2D, 0x2E, 0x2F, 0x2C, 0x2D, 0x2E, 0x2F };
uint8_t fontcolorconvert[8] = { 0x42, 0x4A, 0x46, 0x4E, 0x52, 0x45, 0x42, 0x52 };
uint8_t radiosidecolors[6] = { 0x4A, 0x52, 0x46, 0x56, 0x4A, 0x56 };
uint8_t eventsidecolors[6] = { 0x52, 0x4A, 0x46, 0x56, 0x4A, 0x56 };
int32_t frameticksmsgflag;
// GLOBAL: TIE 0xD4C6C
char* messageptrs[4];
// GLOBAL: TIE 0xD4C80
int32_t msgLineRight;
char** messagetable;
int32_t msgLineBottom;
// GLOBAL: TIE 0xD4C88
int32_t msgLineTop;
// GLOBAL: TIE 0xD4C8C
MsgHistoryEntry messagequeue[MSG_QUEUE_SLOTS];
uint16_t dxtticks;
uint16_t oxtticks;
// GLOBAL: TIE 0xD502E
uint16_t currentmessagesave;
// GLOBAL: TIE 0xD5030
uint8_t messagecnt;

/* Pending voice-clip id selector for the next msg_messageprintf call.
 * Set by callers (score.c radio-message poll, objective-complete branches)
 * to the soundhandles[] index that pairs with the message text; consumed
 * by msg_messageprintf only for template_id 174 (MSG_GENERIC_STAR_INFO)
 * or 161 (MSG_GENERIC_STAR), and packed into entry.aux_flags_hi. Other
 * templates get aux_flags_hi = 0 regardless. msg_messagedisplay fires
 * fsfx_triggervoicesfx(aux_flags_hi) when the message first renders, so
 * a stale value would mis-trigger; callers reset to 0 after their last
 * paired msg_messageprintf. Retail's word_D502C at 0xD502C. */
uint16_t pending_voice_id;

/* --- msg_messageinit -- */

// FUNCTION: TIE 0x32EE0
uint16_t msg_messageinit(void) {
	if (tie_is_high_resolution_flight()) {
		msgLineTop = 456;
		msgLineBottom = 480;
		msgLineRight = 596;
	} else {
		/* flightResolution == TIE_FLIGHT_RES_VGA (low-res 320x200) or anything else */
		msgLineTop = 190;
		msgLineBottom = 200;
		msgLineRight = 298;
	}

	festring_setlinewrap(0);
	festring_setautofill(0);
	festring_setfontsize(1);

	/* 1-pixel separator band above the message area. */
	festring_setbound(0, (int16_t)(msgLineTop - 1), (int16_t)screenXRes, (int16_t)msgLineTop);
	festring_setbackcolor(0x2D);
	if (clearwindow)
		clearwindow();

	/* Main message band. */
	festring_setbackcolor(0x2C);
	festring_setbound(0, (int16_t)msgLineTop, (int16_t)screenXRes, (int16_t)msgLineBottom);
	if (clearwindow)
		clearwindow();

	msg_timeout();
	festring_settextcolor(0x43);

	const uint16_t prev = messagequeue[0].template_idx;
	currentmessagesave = prev;
	messagequeue[0].template_idx = 0xFFFF;
	return prev;
}

/* --- msg_messagerestore --
 * In the binary this function falls through into MSG_messagedisplay
 * (12-byte thunk, no ret/jmp). Mirrored here as an explicit call. */

// FUNCTION: TIE 0x3300C
void msg_messagerestore(void) {
	messagequeue[0].template_idx = currentmessagesave;
	msg_messagedisplay();
}

/* --- msg_messagedisplay -- */

// FUNCTION: TIE 0x33018
void msg_messagedisplay(void) {
	if (messagequeue[0].template_idx == 0xFFFF)
		return;

	msg_readymessage();

	/* On the first display of a message, fire the paired voice clip.
	 * The voice id lives in aux_flags_hi (entry +0x02), populated by
	 * msg_messageprintf from pending_voice_id only for templates 174
	 * and 161 (the radio-message and objective-complete generic-star
	 * templates that pair with a soundhandles[] cue). display_count==0
	 * gates this to once per message; aux_flags_hi==0 covers every
	 * non-voiced template. */
	if (messagequeue[0].aux_flags_hi && !messagequeue[0].display_count)
		fsfx_triggervoicesfx(messagequeue[0].aux_flags_hi);

	const uint8_t type_byte = (uint8_t)messagequeue[0].body[0];
	const char* body;

	if (type_byte >= 8) {
		festring_settextcolor(0x42);
		body = messagequeue[0].body;
	} else {
		festring_settextcolor(fontcolorconvert[type_byte]);
		body = &messagequeue[0].body[1];

		if (type_byte == 1) {
			/* Optional '0'..'3' sub-side selector at body[1]. */
			const uint8_t sub = (uint8_t)*body;
			if (sub >= '0' && sub <= '3') {
				festring_settextcolor(radiosidecolors[sub - '0']);
				body = &messagequeue[0].body[2];
			}
		} else if (type_byte == 2) {
			festring_settextcolor(eventsidecolors[messagequeue[0].side]);
		}
	}

	/* Walk body chars, honoring '[' dim / ']' brighten nudges, capped at 70. */
	char last_ch = 0;
	uint16_t chars_out = 0;
	while (*body && chars_out < 0x46) {
		const uint8_t c = (uint8_t)*body;
		if (c == '[') {
			textcolor = (textcolor == 0xD4) ? (uint8_t)(textcolor - 1) : (uint8_t)(textcolor + 1);
			body++;
		} else if (c == ']') {
			textcolor = (textcolor == 0xD3) ? (uint8_t)(textcolor + 1) : (uint8_t)(textcolor - 1);
			body++;
		} else {
			if (outchar)
				outchar(c);
			body++;
			chars_out++;
			last_ch = (char)c;
		}
	}

	msg_completemessage(messagequeue[0].msg_type, last_ch);
	messagequeue[0].display_count++;
}

/* --- msg_messageprintf -- */

// FUNCTION: TIE 0x330CC
void msg_messageprintf(MsgTemplate template_id) {
	/* Temporary 82-byte staging buffer for the entry. */
	MsgHistoryEntry entry;
	memset(&entry, 0, sizeof(entry));

	entry.template_idx = template_id;
	/* aux_flags_hi carries the voice id for templates 174 (radio-msg
	 * generic-star) and 161 (objective-complete generic-star). Every
	 * other template sets it to zero so msg_messagedisplay won't fire
	 * a stray voice clip. Mirrors retail MSG_messageprintf at 0x33131. */
	if (template_id == 174 || template_id == 161)
		entry.aux_flags_hi = pending_voice_id;
	else
		entry.aux_flags_hi = 0;
	/* Stamp the mission clock. */
	entry.seconds = _date.second;
	entry.minutes = _date.minute;
	entry.hours = _date.hour;
	entry.side = messageside;
	entry.age = 0;
	entry.display_count = 0;

	/* Walk the template string, expanding '*' and '&N' opcodes into body[]. */
	const uint8_t* tpl = (const uint8_t*)messagetable[template_id];
	uint16_t body_len = 0;
	uint16_t arg_idx = 0;

	while (*tpl && body_len < 0x46) {
		const uint8_t op = *tpl;
		if (op == '*') {
			tpl++;
			const uint16_t spec = argtable[arg_idx++];
			const char* src;
			if (spec < 0x8000u)
				src = messagetable[spec];
			else
				src = messageptrs[spec & 0x7FFF];
			while (*src && body_len < 0x46)
				entry.body[body_len++] = *src++;
		} else if (op == '&') {
			tpl++;
			uint16_t width = *tpl++;
			uint16_t value = argtable[arg_idx++];
			uint16_t nonzero_seen = 0;
			while (width) {
				const uint16_t div = placevalue[width];
				uint16_t digit = (uint16_t)(value / div);
				value = (uint16_t)(value - digit * div);
				char ch;
				if (nonzero_seen || width <= 1 || digit) {
					nonzero_seen = 1;
					if (digit > 9)
						digit = 9;
					ch = (char)('0' + digit);
				} else {
					ch = ' ';
				}
				if (body_len < 0x46)
					entry.body[body_len++] = ch;
				width--;
			}
		} else {
			tpl++;
			if (body_len < 0x46)
				entry.body[body_len++] = (char)op;
		}
	}

	/* NUL-terminate if room. (When body_len >= 0x46, the writer leaves the
	 * trailing two bytes untouched in the source buffer; since memset zeroed
	 * the staging, the tail is already 0 either way.) */
	if (body_len < 70)
		entry.body[body_len] = 0;

	/* msg_type from raw template[0] byte, clamped >=8 -> 6. */
	const uint8_t type_raw = (uint8_t)messagetable[template_id][0];
	entry.msg_type = (type_raw >= 8) ? 6 : type_raw;

	const int16_t new_type = entry.msg_type;

	/* History ring append (types 1 and 2 only, suppressed in replay view). */
	if (!replayviewmode && (new_type == 2 || new_type == 1)) {
		numhistorymsgs++;
		lasthistorymsg++;
		if (lasthistorymsg == MSG_HISTORY_SLOTS)
			lasthistorymsg = 0;
		memcpy(&messagehistory[lasthistorymsg], &entry, sizeof(MsgHistoryEntry));
	}

	/* Queue dispatch on the currently-displayed message's msg_type. */
	if (messagequeue[0].template_idx == 0xFFFF) {
		/* Slot empty -- overwrite and render. */
		memcpy(&messagequeue[0], &entry, sizeof(MsgHistoryEntry));
		msg_messagedisplay();
		return;
	}

	switch (messagequeue[0].msg_type) {
		case 1: {
			/* Current is radio: append only if new is radio (1) or event (2);
			 * otherwise preempt. */
			if (new_type != 2 && new_type != 1)
				goto preempt;
			uint8_t slot = (uint8_t)(messagecnt + 1);
			memcpy(&messagequeue[slot], &entry, sizeof(MsgHistoryEntry));
			messagecnt = slot;
			if (messagecnt >= 10)
				messagecnt--;
			return;
		}
		case 2:
		case 5: {
			/* Current is event or type-5 briefing: only type-2 appends. */
			if (new_type == 2) {
				uint8_t slot = (uint8_t)(messagecnt + 1);
				memcpy(&messagequeue[slot], &entry, sizeof(MsgHistoryEntry));
				messagecnt = (uint8_t)(messagecnt + 1);
				if (messagecnt >= 10)
					messagecnt--;
				return;
			}
			goto preempt;
		}
		case 3:
		case 6:
		case 7:
			/* Always preempt. */
			goto preempt_no_move;
		case 4:
			/* Current is scroll/report: drop type-3 silently; preempt non-1/2. */
			if (new_type == 3)
				return;
			if (new_type != 2 && new_type != 1)
				goto preempt_no_move;
			{
				uint8_t slot = (uint8_t)(messagecnt + 1);
				memcpy(&messagequeue[slot], &entry, sizeof(MsgHistoryEntry));
				messagecnt = (uint8_t)(messagecnt + 1);
				if (messagecnt >= 10)
					messagecnt--;
			}
			return;
		default:
			return;
	}

preempt:
	msg_movecurrentmessageinqueue();
	/* fallthrough */
preempt_no_move:
	memcpy(&messagequeue[0], &entry, sizeof(MsgHistoryEntry));
	msg_messagedisplay();
}

/* --- msg_movecurrentmessageinqueue -- */

// FUNCTION: TIE 0x33544
void msg_movecurrentmessageinqueue(void) {
	if (messagequeue[0].display_count >= 2)
		return;
	if (messagequeue[0].age != 0)
		return;

	/* Shift queue[0..messagecnt] to queue[1..messagecnt+1]. */
	int16_t i;
	for (i = (int16_t)(messagecnt + 1); i > 0; i--) {
		memcpy(&messagequeue[i], &messagequeue[i - 1], sizeof(MsgHistoryEntry));
	}
	messagecnt++;
	if (messagecnt >= 10)
		messagecnt--;
}

// FUNCTION: TIE 0x335B8
int16_t msg_getmessagefromqueue(void) {
	const uint8_t old_cnt = messagecnt;
	uint16_t i;
	for (i = 0; i < old_cnt; i++) {
		memcpy(&messagequeue[i], &messagequeue[i + 1], sizeof(MsgHistoryEntry));
	}
	messagecnt = (uint8_t)(old_cnt - 1);
	return (int16_t)i;
}

/* --- msg_readymessage -- */

// FUNCTION: TIE 0x336B0
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

// FUNCTION: TIE 0x3371C
void msg_completemessage(uint16_t msg_type, char last_char) {
	if (last_char != '?' && last_char != '!' && last_char != ':' && last_char != ' ')
		if (outchar)
			outchar('.');
	festring_setautofill(1);
	if (outchar)
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

// FUNCTION: TIE 0x337D4
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
	if ((uint8_t)frameticksmsgflag) {
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
	}
}

/* --- msg_clearmessagequeue -- */

// FUNCTION: TIE 0x339E8
void msg_clearmessagequeue(void) {
	messagecnt = 0;
	messagequeue[0].template_idx = 0xFFFF;
}

/* --- msg_updatemessageage -- */

// FUNCTION: TIE 0x33A00
void msg_updatemessageage(void) {
	if (messagequeue[0].template_idx != 0xFFFF)
		messagequeue[0].age++;
}

/* --- msg_timeout -- */

// FUNCTION: TIE 0x33A14
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

// FUNCTION: TIE 0x33AB8
void msg_reportfgcreation(uint16_t fg_idx, uint16_t species_idx) {
	/* Locate the FG's lead object (leader_obj_idx == 255) or fall back to
	 * CREATE_getworldposition(0x8000, fg_idx) anchor. */
	if (fg_array[fg_idx].start_fg_used) {
		for (uint16_t i = 0; i < NUM_CRAFTS; i++) {
			if (objects[i].ship_idx && objects[i].fg_idx == fg_idx && objects[i].craft_ptr &&
				objects[i].craft_ptr->leader_obj_idx == 255) {
				create_getworldposition(i, fg_idx);
				break;
			}
		}
	} else {
		create_getworldposition(0x8000, fg_idx);
	}

	/* Convert world delta to polar, distance in game "clicks". */
	trig2_ctop(worldlocx - pstate.player->world_x, worldlocy - pstate.player->world_y,
			   worldlocz - pstate.player->world_z);
	int32_t dist = trig2_polardistance * 161;
	int32_t clicks = ((dist >> 16) + 50) / 100;
	if ((int16_t)clicks == 0)
		clicks = 1;

	const uint16_t count = fg_array[fg_idx].count;
	const uint8_t side = fg_array[fg_idx].side; /* Watcom read byte+3 of DWORD at version */
	const uint16_t abbrev_flag = 0x8000;        /* ptr to messageptrs[0] */

	argtable[0] = count;

	MsgTemplate tpl;
	if (side == 1) {
		/* Friendly/blue report. */
		if (count == 1) {
			messageptrs[1] = fg_array[fg_idx].name;
			argtable[2] = (uint16_t)clicks;
			messageptrs[0] = (char*)spec_name_ptrs[species_idx];
			argtable[0] = abbrev_flag;      /* overwrite the count */
			argtable[1] = (uint16_t)0x8001; /* ptr to messageptrs[1] */
			msg_messageprintf(MSG_CRAFT_ENTERING_AT);
			return;
		}
		messageptrs[2] = fg_array[fg_idx].name;
		argtable[3] = (uint16_t)clicks;
		argtable[2] = (uint16_t)0x8002; /* ptr to messageptrs[2] */
		messageptrs[1] = (char*)spec_name_ptrs[species_idx];
		argtable[1] = (uint16_t)0x8001;
		tpl = MSG_CRAFT_GROUP_ENTERING_AT;
	} else {
		/* Hostile sighting -- color line by side via messageside. */
		messageside = side;
		messageptrs[1] = (char*)spec_name_ptrs[species_idx];
		argtable[1] = (uint16_t)0x8001;
		argtable[2] = (uint16_t)clicks;
		tpl = (count == 1) ? MSG_NEW_CRAFT_ALERT : MSG_NEW_CRAFT_ALERT_PLURAL;
	}
	msg_messageprintf(tpl);
}

/* --- msg_addmessageptr -- */

// FUNCTION: TIE 0x33CF4
uint16_t msg_addmessageptr(uint16_t slot_idx, char* ptr) {
	messageptrs[slot_idx] = ptr;
	const uint16_t tagged = (uint16_t)(slot_idx | 0x8000);
	argtable[slot_idx] = tagged;
	return tagged;
}

/* --- msg_craftmessage -- */

// FUNCTION: TIE 0x33D10
void msg_craftmessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id) {
	messageside = objects[obj_idx].side;
	argtable[0] = 0x8000;
	messageptrs[0] = (char*)spec_name_ptrs[craft->species_idx];
	const uint8_t fg_idx = objects[obj_idx].fg_idx;
	argtable[1] = 0x8001;
	messageptrs[1] = fg_array[fg_idx].name;

	if ((int8_t)fg_array[fg_idx].count <= 1) {
		/* Single craft FG -- no "#N". */
		argtable[2] = msg_template_id;
		msg_messageprintf(MSG_CRAFT_REPORT);
	} else {
		argtable[2] = (uint16_t)(craft->craft_idx_in_fg + 1);
		argtable[3] = msg_template_id;
		msg_messageprintf(MSG_CRAFT_REPORT_FG);
	}
}

/* --- msg_radiomessage -- */

// FUNCTION: TIE 0x33DD0
int8_t msg_radiomessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id, uint16_t cmdr_mode) {
	MsgTemplate tpl;
	if (cmdr_mode) {
		const uint8_t fg_idx = objects[obj_idx].fg_idx;
		argtable[1] = msg_template_id;
		messageptrs[0] = fg_array[fg_idx].name;
		tpl = MSG_ACK_FG_PLAIN;
		argtable[0] = 0x8000;
	} else {
		argtable[0] = 0x8000;
		messageptrs[0] = spec_data[craft->species_idx].short_name;
		const uint8_t fg_idx = objects[obj_idx].fg_idx;
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
	return fsfx_speakorderack(obj_idx, msg_template_id, cmdr_mode);
}

/* --- msg_reportmessage -- */

// FUNCTION: TIE 0x33EF4
void msg_reportmessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id) {
	argtable[0] = 0x8000;
	messageptrs[0] = spec_data[craft->species_idx].short_name;
	const uint8_t fg_idx = objects[obj_idx].fg_idx;
	argtable[1] = 0x8001;
	/* messageptrs[1] points at the FG; char[0..11] inside is fg.name. */
	messageptrs[1] = (char*)&fg_array[fg_idx];

	if ((int8_t)fg_array[fg_idx].count <= 1) {
		argtable[2] = msg_template_id;
		msg_messageprintf(MSG_REPORTING_FG);
	} else {
		argtable[2] = (uint16_t)(craft->craft_idx_in_fg + 1);
		argtable[3] = msg_template_id;
		msg_messageprintf(MSG_REPORTING_FG_INDEXED);
	}
}

/* --- msg_createobjectname -- Compose a printable object label.
 * Inline strcat loops (not calls to msg_msgstrcat) matching the binary. */

/* Internal helper: strcat one char* onto buf, returning the NUL position. */
static char* str_append(char* buf, const char* src) {
	while (*buf)
		buf++;
	while (*src)
		*buf++ = *src++;
	*buf = 0;
	return buf;
}

// FUNCTION: TIE 0x33FA8
int16_t msg_createobjectname(uint16_t obj_idx, int16_t use_official, char* out_buf) {
	*out_buf = 0;

	if (obj_idx >= 0x3800) {
		/* Static-object path. */
		const uint16_t sidx = (uint16_t)(obj_idx - 14336);
		const uint8_t species = staticobjects[sidx].species;
		const char* base = ((char**)buoystr)[species - 70];
		str_append(out_buf, base);

		const uint8_t fg_idx = staticobjects[sidx].fg_idx;
		if (fg_array[fg_idx].name[0]) {
			char* p = out_buf;
			while (*p)
				p++;
			*p++ = ' ';
			*p = 0;
			/* Inline append of fg.name -- up to 12 bytes, NUL-terminated. */
			str_append(out_buf, fg_array[fg_idx].name);
		}
		/* Return the final end-pointer (low 16 bits); callers ignore. */
		char* p = out_buf;
		while (*p)
			p++;
		return (int16_t)(uintptr_t)p;
	}

	/* Regular object path. */
	const uint8_t ship_idx = objects[obj_idx].ship_idx;
	if (objects[obj_idx].category == 0) {
		/* Craft path. */
		CraftData* craft_ptr = objects[obj_idx].craft_ptr;
		const char* base = use_official ? spec_name_ptrs[craft_ptr->species_idx]
										: spec_data[craft_ptr->species_idx].short_name;
		str_append(out_buf, base);

		const uint8_t fg_idx = objects[obj_idx].fg_idx;
		if (fg_array[fg_idx].name[0]) {
			char* p = out_buf;
			while (*p)
				p++;
			*p++ = ' ';
			*p = 0;
			str_append(out_buf, fg_array[fg_idx].name);
		}

		if ((int8_t)fg_array[fg_idx].count > 1) {
			char* p = out_buf;
			while (*p)
				p++;
			*p++ = ' ';
			*p = 0;
			p = out_buf;
			while (*p)
				p++;
			*p++ = (char)(craft_ptr->craft_idx_in_fg + '1');
			*p = 0;
		}
		char* p = out_buf;
		while (*p)
			p++;
		return (int16_t)(uintptr_t)p;
	}

	/* Non-craft object. */
	if (ship_idx >= 0x8F && ship_idx <= 0x9A) {
		const char* base = ((char**)warheadstrings)[ship_idx - 143];
		str_append(out_buf, base);
	} else if (ship_idx >= 0x46 && ship_idx <= 0x54) {
		const char* base = ((char**)buoystr)[ship_idx - 70];
		str_append(out_buf, base);
	}
	char* p = out_buf;
	while (*p)
		p++;
	return (int16_t)(uintptr_t)p;
}

/* --- msg_msgstrcat --
 * Appends src to dst in place. Returns src + strlen(src) (Watcom quirk). */

// FUNCTION: TIE 0x34300
char* msg_msgstrcat(char* src, char* dst) {
	while (*dst)
		dst++;
	while (*src) {
		*dst++ = *src++;
	}
	*dst = 0;
	return src;
}

/* --- msg_msgstradd -- Append single char, NUL-terminate, return char. */

// FUNCTION: TIE 0x34324
char msg_msgstradd(char ch, char* dst) {
	while (*dst)
		dst++;
	*dst++ = ch;
	*dst = 0;
	return ch;
}
