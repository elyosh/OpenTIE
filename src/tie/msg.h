#ifndef TIE_MSG_H
#define TIE_MSG_H

#include "tie/msg_templates.h"
#include "tie/tie.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Retail queue/history record: 84 bytes in both editions. */
typedef struct MsgHistoryEntry {
	uint16_t template_idx; /* +0x00: messagetable index */
	uint16_t voice_id;     /* +0x02: voice cue played on first display, or zero */
	int16_t subsecond;     /* +0x04: mission clock tick countdown */
	uint8_t seconds;       /* +0x06: mission clock seconds */
	uint8_t minutes;       /* +0x07: mission clock minutes */
	uint8_t hours;         /* +0x08: mission clock hours */
	uint8_t msg_type;      /* +0x09: template prefix, clamped to 6 when >= 8 */
	uint16_t side;         /* +0x0A: faction side for event-message colors */
	uint8_t age;           /* +0x0C: incremented by msg_updatemessageage */
	uint8_t display_count; /* +0x0D: incremented by msg_messagedisplay */
	char body[70];         /* +0x0E: expanded text, including the color prefix */
} MsgHistoryEntry;

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
void msg_messageprintf(uint16_t template_id);

/* Shift messagequeue[0..messagecnt] to [1..messagecnt+1], bump messagecnt.
 * No-op when the current slot has age != 0 or display_count >= 2. */
void msg_movecurrentmessageinqueue(void);

/* Pop messagequeue[0] by shifting [1..messagecnt] down, then decrement the count. */
void msg_getmessagefromqueue(void);

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
void msg_radiomessage(uint16_t obj_idx, CraftData* craft, uint16_t msg_template_id, uint16_t cmdr_mode);

/* Sitrep-style report. Template 120 (multi) or 121 (single).
 * Uses spec.short_name as speaker. */
void msg_reportmessage(int obj_idx, CraftData* craft, uint16_t msg_template_id);

/* Build a printable object name into out_buf: species/FG/#N for craft,
 * buoystr[] for buoys, warheadstrings[] for ordnance, buoystr[] for
 * static objects. use_official selects spec.name_ptr vs spec.short_name. */
void msg_createobjectname(uint16_t obj_idx, int16_t use_official, char* out_buf);

/* Append src to dst and terminate the destination. */
void msg_msgstrcat(const char* src, char* dst);

/* Append a single char ch to the NUL-terminated dst. */
void msg_msgstradd(char ch, char* dst);

/* --- Module globals (mirror the watdbg msg.c ownership) --- */

extern uint8_t fontcolors[32];
extern uint8_t fontcolorconvert[8]; /* Message type to text color. */
extern uint8_t radiosidecolors[6];  /* Radio selectors '0'..'3' use the first four entries. */
extern uint8_t eventsidecolors[6];
extern uint8_t frameticksmsgflag;
extern char* messageptrs[4];
extern int32_t msgLineRight;
extern char** messagetable;
extern int32_t msgLineBottom;
extern int32_t msgLineTop;
extern MsgHistoryEntry messagequeue[MSG_QUEUE_SLOTS];
extern uint16_t dxtticks;
extern uint16_t oxtticks;
extern uint16_t currentmessagesave;
/* Voice cue copied into templates 174 and 161, played on first display. */
extern uint16_t pending_voice_id;
extern uint8_t messagecnt;

#ifdef __cplusplus
}
#endif

#endif
