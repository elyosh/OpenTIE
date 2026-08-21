#ifndef TIE_REPLAY_H
#define TIE_REPLAY_H

/* In-flight input recording, playback controls, and cinematic replay camera. */

#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * ReplayInputFrame -- 14-byte per-tick input packet (V5 wire format).
 *
 * Written once per TIE_doframe iteration by user_userinterface (while
 * recordingreplay is set), read back during playback in the same path.
 * `delta_us` is the complete admitted simulation interval. Playback routes
 * it to every synthetic-clock consumer before applying the frame.
 * `frameticks` is restored as the engine's elapsed PIT-tick count. When
 * camera.view_pitch_offset != 0 (zoom-out strategic view) the deltax /
 * deltay / deltaroll / buttons fields are zeroed at write time so only
 * the key, delta_us, and tick count are effective.
 *
 * The same physical buffer is re-purposed by user_inflightinfo when
 * the player opens/closes the in-flight info room: each "slot" of
 * side-payload is padded out to one record so the stream stays a flat
 * sequence of fixed-size records. See user.c for the per-slot layout.
 *
 * Version 2 stores analog roll from a second-stick axis at +12.
 *
 * The runtime layout is naturally aligned. The on-disk layout is the
 * fixed 14-byte little-endian record produced by `ReplayInputFrame_encode`
 * and consumed by `ReplayInputFrame_decode`.
 * -------------------------------------------------------------------------- */

typedef struct ReplayInputFrame {
	uint32_t delta_us;  /* complete admitted interval in µs          (+0) */
	uint16_t key;       /* inputkey                                   (+4) */
	int16_t deltax;     /* inputdeltax (joystick/mouse yaw delta)     (+6) */
	int16_t deltay;     /* inputdeltay (joystick/mouse pitch delta)   (+8) */
	uint8_t buttons;    /* inputbuttons & 0xFF                       (+10) */
	uint8_t frameticks; /* frameticks since previous frame (pacing)  (+11) */
	int16_t deltaroll;  /* inputdeltaroll (second-stick roll axis)   (+12) */
} ReplayInputFrame;

enum {
	REPLAYINPUTFRAME_DELTA_US_OFFSET = 0,
	REPLAYINPUTFRAME_KEY_OFFSET = 4,
	REPLAYINPUTFRAME_DELTAX_OFFSET = 6,
	REPLAYINPUTFRAME_DELTAY_OFFSET = 8,
	REPLAYINPUTFRAME_BUTTONS_OFFSET = 10,
	REPLAYINPUTFRAME_FRAMETICKS_OFFSET = 11,
	REPLAYINPUTFRAME_DELTAROLL_OFFSET = 12,
	REPLAYINPUTFRAME_DISK_SIZE = 14,
	REPLAY_INPUT_CHUNK_FRAMES = 0xBFF,
	REPLAY_INPUT_BUFFER_BYTES = REPLAY_INPUT_CHUNK_FRAMES * REPLAYINPUTFRAME_DISK_SIZE,
	REPLAY_MAX_TOTAL_RECORDS = 0x20000 * 7,
};

_Static_assert(REPLAYINPUTFRAME_DELTAROLL_OFFSET + 2 == REPLAYINPUTFRAME_DISK_SIZE,
			   "Replay input wire offsets must cover one complete record");

void ReplayInputFrame_decode(ReplayInputFrame* dst, const uint8_t* src);
void ReplayInputFrame_encode(uint8_t* dst, const ReplayInputFrame* src);

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/* Record-side helpers */
void replay_replaymessage(uint16_t msg_id);
void replay_drawreplaybutton(uint16_t btn_id);

/* Cursor/target widget helpers (populate the two in-flight info slots) */
int replay_getstatusnum(uint16_t obj_id);
void replay_outputobjectname(uint16_t obj_id);
void replay_outputclipname(void);

/* Playback-loop state mutators */
void replay_rewindreplay(void);
void replay_stopreplay(void);
int16_t replay_loadreplayinput(void);

/* Camera pose (called once per TIE_updatescreen while replayviewmode is set) */
void replay_calcreplayview(void);
void replay_movecambehind(uint16_t obj_id);

void replay_Push_DoReplayScreen_Task(void);
/* Returns true when the port must suspend playback and push the save task. */
bool replay_replayinput(void);

/* Clip load (from .CLP files in persistent user storage). */
int replay_loadreplay(void);

/* Generic byte-copy helper used by replay save/load (the binary's
 * REPLAY_copybytesinfile). Copies `count` bytes from src to dst using
 * fgetc/fputc; on EOF from either stream, fcloses both and returns 0.
 * Returns 1 on success (streams left open). */
int replay_copybytesinfile(uint16_t count, TieFile* src, TieFile* dst);

/* --------------------------------------------------------------------------
 * replay.c-owned globals. See watdbg ownership in docs/watdbg-prototypes.txt.
 * -------------------------------------------------------------------------- */

/* Button layout (38 entries: 0..0x11 for the cockpit layout + 0x12..0x23
 * for the stand-alone viewer with +18 offset). Retail promotes the two
 * coord tables to [38][2] with a per-resolution selector:
 *   column 0 = VGA (flightResolution == 0x13, 320x200)
 *   column 1 = SVGA (flightResolution == 0x101, 640x480)
 * Column 0 matches the demo verbatim. */
extern const uint16_t replaybuttontop[38][2];  /* Y coord per button */
extern const uint16_t replaybuttonleft[38][2]; /* X coord per button */

/* Music-pause bookkeeping: the viewer pauses iMUSE while not actively
 * stepping, saves the master volume, and restores it when resuming. */
extern uint8_t replaymusic;  /* 1 = iMUSE resumed, 0 = paused */
extern int16_t replayvolume; /* saved master volume for restore */

/* Replay-message overlay timer. Set to 944 ticks by replay_replaymessage
 * each time a message is posted; replay_doreplayscreen decays it with
 * frameticks and clears the message band on underflow. */
extern int16_t replaymsgtimer;

/* Target / chase-target bookkeeping shared with USER_* (watdbg places them
 * in replay.c's data section). */
extern uint8_t chasespecies;   /* species of the chase-camera target */
extern uint8_t trackspecies;   /* species of the track-target (info box) */
extern uint16_t trackobject;   /* 0xFFFF = no track, else object slot */
extern uint8_t reentersimflag; /* 1 = return to live sim from viewer */
extern uint8_t exitflag;       /* 1 = exit current replay loop */
extern int32_t cameraposstate; /* 0 = chase info hidden, 1 = visible */

#endif /* TIE_REPLAY_H */
