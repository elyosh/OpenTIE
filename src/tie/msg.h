#ifndef __MSG_H__
#define __MSG_H__

#include "tie/msg_templates.h"
#include "tie/tie.h"
#include <stdint.h>

#pragma pack(push, 2)
/*
 * MsgHistoryEntry — one message slot (in the 10-entry queue or the
 * 300-slot history ring). 82 bytes.
 *
 * body[0] is a color/type prefix; the renderer skips it on emit and treats
 * msg_type at +7 as the pre-clamped (>=8 -> 6) dispatch tag. See the
 * IDA IDB comment on MsgHistoryEntry.body for the full prefix grammar.
 */
typedef struct MsgHistoryEntry {
	uint16_t template_idx; /* +0x00: messagetable[] index for this message */
	uint16_t aux_flags_hi; /* +0x02: HIWORD of the wall-clock cluster
							*        (`_date.subsec` at the time the
							*        message was logged); unused by the
							*        renderer, kept for binary parity. */
	uint8_t seconds;       /* +0x04: mission clock seconds */
	uint8_t minutes;       /* +0x05: mission clock minutes */
	uint8_t hours;         /* +0x06: mission clock hours (0 = omit HH in display) */
	uint8_t msg_type;      /* +0x07: dispatch tag (template[0], clamped >=8 -> 6) */
	uint16_t side;         /* +0x08: faction side (for msg_type==2 coloring) */
	uint8_t age;           /* +0x0A: ticks since creation (incremented by msg_updatemessageage) */
	uint8_t display_count; /* +0x0B: times rendered (incremented by msg_messagedisplay) */
	char body[70];         /* +0x0C: expanded text; body[0] = raw type prefix,
							*        body[1..] is the emitted text (NUL-terminated).
							*        For msg_type==1, body[1] may be a '0'..'3' sub-side
							*        selector that's also consumed before emission. */
} MsgHistoryEntry;         /* 82 bytes */
#pragma pack(pop)

#define MSG_QUEUE_SLOTS 11 /* slot 0 = current, slots 1..9 pending, slot 10 = transient overflow */
#define MSG_HISTORY_SLOTS 300

/* --- msg_* API (1:1 with MSG_* in the binary) --- */

/* Configure message band geometry (msgLineTop/Bottom/Right) for the current
 * flightResolution, clear the band, redraw the time-warp indicator, and
 * save the currently-displayed template_idx in currentmessagesave so
 * msg_messagerestore can bring it back. Returns the saved template_idx. */
uint16_t msg_messageinit(void);

/* Restore a message previously saved by msg_messageinit and re-render it. */
void msg_messagerestore(void);

/* Render messagequeue[0] to the message band, honoring the body[] prefix
 * grammar and the '['/']' textcolor nudges. Arms the display timer via
 * msg_completemessage. No-op when the slot is empty (template_idx == 0xFFFF). */
void msg_messagedisplay(void);

/* Expand template_id from messagetable[] with argtable/messageptrs
 * substitutions, stamp it with the mission clock + messageside, and either
 * overwrite messagequeue[0] (preempting) or append to the queue tail
 * (messagecnt++). For msg_type in {1, 2} (radio/event) and when not in
 * replay view, also append to messagehistory ring. */
void msg_messageprintf(MsgTemplate template_id);

/* Shift messagequeue[0..messagecnt] to [1..messagecnt+1], bump messagecnt.
 * No-op when the current slot has age != 0 or display_count >= 2. */
void msg_movecurrentmessageinqueue(void);

/* Pop messagequeue[0] by shifting [1..messagecnt] down. Returns old messagecnt.
 * Orphan in the demo build — kept for API completeness. */
int16_t msg_getmessagefromqueue(void);

/* Prime FESTRING output for a message line: tiny font, bg 0x2C, drop 0x40,
 * clip (0, msgLineTop, screenXRes, screenYRes), cursor at (2, msgLineTop),
 * text color 0x43. Raises dropflag. */
void msg_readymessage(void);

/* Close out a rendered message: auto-'.' when last_char != '?!:: ',
 * emit '\n', arm timers[TIMER_MSG] by msg_type, call msg_timeout, restore font. */
void msg_completemessage(uint16_t msg_type, char last_char);

/* Per-frame tick: timer-driven queue advance + debug frameticks overlay. */
void msg_messageupdate(void);

/* Reset the queue to empty (messagecnt=0, slot 0 template_idx=0xFFFF). */
void msg_clearmessagequeue(void);

/* Tick +1 on messagequeue[0].age (no-op when the slot is empty). */
void msg_updatemessageage(void);

/* Redraw the time-warp indicator 'T:<acceleratedtimesetting>x' in the
 * right portion of the message band. */
void msg_timeout(void);

/* FG arrival sighting: 'species [fg] [#N] at <clicks>'. Friendly side==1
 * uses templates 0xB6/183; hostile uses 45/46. */
void msg_reportfgcreation(uint16_t fg_idx, uint16_t species_idx);

/* Register a raw char* pointer at messageptrs[slot_idx] and tag
 * argtable[slot_idx] with the 0x8000 direct-pointer flag. Returns the
 * tagged value (slot_idx | 0x8000). */
uint16_t msg_addmessageptr(uint16_t slot_idx, char* ptr);

/* Speaker-labeled radio chatter by a specific craft. Template 95 (multi)
 * or 96 (single) based on fg.count. Uses spec.name_ptr as speaker. */
void msg_craftmessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id);

/* Speaker-labeled radio chatter + voice FX. cmdr_mode!=0 uses template 205
 * with fg.name only. cmdr_mode==0 uses template 110/111 with spec.short_name. */
int8_t msg_radiomessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id, uint16_t cmdr_mode);

/* Sitrep-style report. Template 120 (multi) or 121 (single).
 * Uses spec.short_name as speaker. */
void msg_reportmessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id);

/* Build a printable object name into out_buf: species/FG/#N for craft,
 * buoystr[] for buoys, warheadstrings[] for ordnance, buoystr[] for
 * static objects. use_official selects spec.name_ptr vs spec.short_name. */
int16_t msg_createobjectname(uint16_t obj_idx, int16_t use_official, char* out_buf);

/* Append src to dst; returns src + strlen(src) (unusual Watcom return,
 * callers ignore). */
char* msg_msgstrcat(char* src, char* dst);

/* Append a single char ch to the NUL-terminated dst; returns ch. */
char msg_msgstradd(char ch, char* dst);

/* --- Module globals (mirror the watdbg msg.c ownership) --- */

extern uint8_t fontcolors[32];      /* 0xD5118 - general font colors */
extern uint8_t fontcolorconvert[8]; /* 0xD5138 - msg_type -> text color */
extern uint8_t radiosidecolors[6];  /* 0xD5140 - type-1 sub-side colors ('0'..'3' + 2 unused) */
extern uint8_t eventsidecolors[6];  /* 0xD5146 - type-2 event side colors */
extern int32_t frameticksmsgflag;   /* 0xD514C - debug frame-timing overlay flag */
extern char* messageptrs[4];        /* 0xE3774 - direct char* pointers for '*' substitution */
extern int32_t msgLineRight;        /* 0xE3784 - right edge / time-warp column */
extern char** messagetable;         /* 0xE3788 - pointer to array of template char* */
extern int32_t msgLineBottom;       /* 0xE378C */
extern int32_t msgLineTop;          /* 0xE3790 */
extern MsgHistoryEntry messagequeue[MSG_QUEUE_SLOTS]; /* 0xE3794 - 11 * 82 = 902 bytes */
extern uint16_t dxtticks;                             /* 0xE3B1A - debug tick readout */
extern uint16_t oxtticks;                             /* 0xE3B1C */
extern uint16_t currentmessagesave; /* 0xE3B1E - saved template_idx for msg_messagerestore */

/* Pending voice-clip id selector. Callers set it to a soundhandles[]
 * index (edition-aware FSFX mission-voice range) before calling msg_messageprintf
 * with template_id 174 or 161; the value lands in entry.aux_flags_hi
 * and msg_messagedisplay fires fsfx_triggervoicesfx with it on first
 * display. Retail: word_D502C at 0xD502C. */
extern uint16_t pending_voice_id;
extern uint8_t messagecnt; /* 0xE3B20 - queue fill level (0..9) */

#endif
