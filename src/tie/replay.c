#include "tie/replay.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/input/input.h"

#include <stdint.h>
#include <string.h>

#include "tie/create.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_composite_tie98.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fview.h"
#include "tie/math2.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/panel.h"
#include "tie/panelrts.h"
#include "tie/render_scene_tie98.h"
#include "tie/replayio.h"
#include "tie/shipext.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_flight.h"

#include "../util/binio.h"
#include "tie/xtimer.h"
#include "tie_runtime/runtime/replay_format.h"
#include "tie_runtime/timing/flight_checkpoint.h"
#include "tie_runtime/timing/replay_timing.h"
#include <landru/task.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>

/* --------------------------------------------------------------------------
 * Module-owned globals (watdbg: replay.c's OBJ).
 * -------------------------------------------------------------------------- */

/* VGA (320x200) in column 0, SVGA (640x480) in column 1. Values copied
 * verbatim from Z_TIE__.EXE at 0xC7210 (top) and 0xC72A8 (left). */
const uint16_t replaybuttontop[38][2] = {
	{ 1, 2 },     { 1, 2 },     { 1, 2 },     { 1, 2 },     /*  0- 3 */
	{ 1, 2 },     { 1, 2 },     { 1, 2 },     { 1, 2 },     /*  4- 7 */
	{ 1, 2 },     { 1, 2 },     { 159, 386 }, { 159, 386 }, /*  8-11 */
	{ 178, 427 }, { 178, 427 }, { 178, 427 }, { 178, 427 }, /* 12-15 */
	{ 164, 394 }, { 164, 394 }, { 147, 147 }, { 147, 147 }, /* 16-19 */
	{ 147, 147 }, { 147, 147 }, { 147, 147 }, { 147, 147 }, /* 20-23 */
	{ 147, 147 }, { 147, 147 }, { 147, 147 }, { 147, 147 }, /* 24-27 */
	{ 160, 160 }, { 160, 160 }, { 178, 178 }, { 178, 178 }, /* 28-31 */
	{ 178, 178 }, { 178, 178 }, { 165, 165 }, { 165, 165 }, /* 32-35 */
	{ 147, 147 }, { 147, 147 },                             /* 36-37 */
};

const uint16_t replaybuttonleft[38][2] = {
	{ 47, 94 },   { 47, 94 },   { 82, 164 },  { 82, 164 },  /*  0- 3 */
	{ 117, 234 }, { 117, 234 }, { 210, 420 }, { 210, 420 }, /*  4- 7 */
	{ 245, 490 }, { 245, 490 }, { 31, 70 },   { 31, 70 },   /*  8-11 */
	{ 32, 64 },   { 32, 64 },   { 75, 150 },  { 75, 150 },  /* 12-15 */
	{ 75, 150 },  { 75, 150 },  { 37, 37 },   { 37, 37 },   /* 16-19 */
	{ 62, 62 },   { 62, 62 },   { 89, 89 },   { 89, 89 },   /* 20-23 */
	{ 174, 174 }, { 174, 174 }, { 258, 258 }, { 258, 258 }, /* 24-27 */
	{ 42, 42 },   { 42, 42 },   { 43, 43 },   { 43, 43 },   /* 28-31 */
	{ 86, 86 },   { 86, 86 },   { 86, 86 },   { 86, 86 },   /* 32-35 */
	{ 205, 205 }, { 205, 205 },                             /* 36-37 */
};

/* The replay panel loader stores its 38 sprites in the final slots of
 * the shared 265-entry shape table. */
enum { REPLAY_BUTTON_SHAPE_BASE = 0xE3 };

// GLOBAL: TIE 0xC7340
uint8_t replaymusic;
// GLOBAL: TIE 0xD5E5C
int16_t replayvolume;
int16_t replaymsgtimer;

// GLOBAL: TIE 0xD5E5A
uint8_t chasespecies;
// GLOBAL: TIE 0xD5E5E
uint8_t trackspecies;
// GLOBAL: TIE 0xD5E60
uint16_t trackobject;
// GLOBAL: TIE98 0x5FBC44
uint8_t reentersimflag;
// GLOBAL: TIE98 0x5FBC46
uint8_t exitflag;
int32_t cameraposstate;

// GLOBAL: TIE 0xE36FC
Camera replaycam;

/* Clip filename suffix (".clp"). Retail lowercased it from demo's ".CLP". */
static const char kClipSuffix[] = ".clp";

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/* REPLAY_copybytesinfile — copy `count` bytes from src to dst. Retail
 * chunks the copy into 256-byte blocks via fediskio_readfileblock + fwrite
 * (the demo was per-byte fgetc/fputc). On TIE_EOF / short write both streams
 * are fclosed and 0 is returned. Returns 1 on success (streams left
 * open). */
// FUNCTION: TIE 0x454F4
int replay_copybytesinfile(uint16_t count, TieFile* src, TieFile* dst) {
	uint8_t buf[256];
	uint32_t remaining = count;
	while (remaining > 0) {
		uint32_t chunk = remaining > 256u ? 256u : remaining;
		int16_t got = fediskio_readfileblock(buf, 1, chunk, src);
		if ((uint32_t)got != chunk || TieStorage_Write(buf, 1, got, dst) != (size_t)got) {
			TieStorage_Close(src);
			TieStorage_Close(dst);
			return 0;
		}
		remaining -= chunk;
	}
	return 1;
}

/* --------------------------------------------------------------------------
 * ReplayInputFrame on-disk codec. Used by user.c on the record/playback
 * paths so the buffer is always written in the DOS binary's little-endian
 * format regardless of host endianness.
 * -------------------------------------------------------------------------- */
void ReplayInputFrame_decode(ReplayInputFrame* dst, const uint8_t* src) {
	dst->delta_us = br_u32le(src + REPLAYINPUTFRAME_DELTA_US_OFFSET);
	dst->key = br_u16le(src + REPLAYINPUTFRAME_KEY_OFFSET);
	dst->deltax = br_i16le(src + REPLAYINPUTFRAME_DELTAX_OFFSET);
	dst->deltay = br_i16le(src + REPLAYINPUTFRAME_DELTAY_OFFSET);
	dst->buttons = br_u8(src + REPLAYINPUTFRAME_BUTTONS_OFFSET);
	dst->frameticks = br_u8(src + REPLAYINPUTFRAME_FRAMETICKS_OFFSET);
	dst->deltaroll = br_i16le(src + REPLAYINPUTFRAME_DELTAROLL_OFFSET);
}

void ReplayInputFrame_encode(uint8_t* dst, const ReplayInputFrame* src) {
	bw_u32le(dst + REPLAYINPUTFRAME_DELTA_US_OFFSET, src->delta_us);
	bw_u16le(dst + REPLAYINPUTFRAME_KEY_OFFSET, src->key);
	bw_i16le(dst + REPLAYINPUTFRAME_DELTAX_OFFSET, src->deltax);
	bw_i16le(dst + REPLAYINPUTFRAME_DELTAY_OFFSET, src->deltay);
	bw_u8(dst + REPLAYINPUTFRAME_BUTTONS_OFFSET, src->buttons);
	bw_u8(dst + REPLAYINPUTFRAME_FRAMETICKS_OFFSET, src->frameticks);
	bw_i16le(dst + REPLAYINPUTFRAME_DELTAROLL_OFFSET, src->deltaroll);
}

/* Compose "<clipname>.clp" from the module-global replayclipname into a
 * 16-byte caller buffer. */
static void build_clip_filename(char out[16]) {
	size_t n = 0;
	while (n < sizeof(replayclipname) && replayclipname[n]) {
		out[n] = replayclipname[n];
		++n;
	}
	size_t s = 0;
	while (n + 1 < 16 && kClipSuffix[s]) {
		out[n++] = kClipSuffix[s++];
	}
	out[n] = '\0';
}

/* --------------------------------------------------------------------------
 * Message + widget helpers
 * -------------------------------------------------------------------------- */

/* replay_replaymessage — print a message from messagetable[msg_id] into
 * the in-flight message band. First byte of the template is a color/type
 * tag (< 8 indexes fontcolorconvert[], else default 0x42); '[' / ']'
 * nudge the text color up/down. Appends a '.' unless the final printable
 * character was one of '?','!',':',' '. Arms replaymsgtimer to 944 ticks. */
// FUNCTION: TIE 0x475F4, TIE98 0x474BC0
void replay_replaymessage(uint16_t msg_id) {
	msg_readymessage();

	const unsigned char* p = (const unsigned char*)messagetable[msg_id];
	unsigned char tag = *p;
	if (tag < 8) {
		festring_settextcolor(fontcolorconvert[tag]);
		++p;
	} else {
		festring_settextcolor(0x42);
	}

	unsigned char last = 'x';
	while (*p) {
		unsigned char c = *p;
		if (c == '[') {
			++textcolor;
			++p;
		} else if (c == ']') {
			--textcolor;
			++p;
		} else {
			outchar(c);
			last = c;
			++p;
		}
	}

	if (last != '?' && last != '!' && last != ':' && last != ' ') {
		outchar('.');
	}

	festring_setautofill(1);
	outchar('\n');
	festring_setautofill(0);
	festring_setfontsize(2);
	replaymsgtimer = 944;
}

/* replay_drawreplaybutton — repaint one widget of the replay HUD.
 * btn_id 0..0x11 = in-flight cockpit layout, 0..0x25 = stand-alone viewer
 * (same ids with +18 offset). Beyond the basic blit, 6 ids (14/32, 15/33,
 * 16/34, 17/35) toggle the track/chase info windows (name + status).
 *
 * Retail supports VGA and SVGA coordinate sets for both the cockpit and
 * stand-alone layouts. */
// FUNCTION: TIE 0x459B8
void replay_drawreplaybutton(uint16_t btn_id) {
	uint16_t idx = btn_id;
	int res = 0; /* 0 = VGA / demo layout */
	int16_t track_name_y, track_name_h, track_stat_y, track_stat_h;
	int16_t track_name_left, track_name_right;
	int16_t track_stat_left, track_stat_right;
	int16_t chase_name_y, chase_name_h, chase_stat_y, chase_stat_h;
	int16_t chase_name_left, chase_name_right;
	int16_t chase_stat_left, chase_stat_right;

	if (maingameflag) {
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			res = 0;
			track_name_left = 127;
			track_name_right = 201;
			track_stat_left = 230;
			track_stat_right = 268;
			track_name_y = 181;
			track_name_h = 186;
			track_stat_y = 181;
			track_stat_h = 186;
			chase_name_left = 127;
			chase_name_right = 201;
			chase_stat_left = 230;
			chase_stat_right = 268;
			chase_name_y = 167;
			chase_name_h = 172;
			chase_stat_y = 167;
			chase_stat_h = 172;
		} else {
			res = 1;
			track_name_left = 260;
			track_name_right = 397;
			track_stat_left = 464;
			track_stat_right = 532;
			track_name_y = 436;
			track_name_h = 445;
			track_stat_y = 436;
			track_stat_h = 445;
			chase_name_left = 260;
			chase_name_right = 397;
			chase_stat_left = 464;
			chase_stat_right = 532;
			chase_name_y = 402;
			chase_name_h = 411;
			chase_stat_y = 402;
			chase_stat_h = 411;
		}
	} else {
		/* Stand-alone viewer remaps 0..0x11 -> 0x12..0x23. */
		if (btn_id < 0x12u)
			idx = btn_id + 18;
		if (tie_is_high_resolution_flight()) {
			res = 1;
			track_name_left = 278;
			track_name_right = 423;
			track_stat_left = 484;
			track_stat_right = 556;
			track_name_y = 437;
			track_name_h = 448;
			track_stat_y = 437;
			track_stat_h = 448;
			chase_name_left = 278;
			chase_name_right = 423;
			chase_stat_left = 484;
			chase_stat_right = 556;
			chase_name_y = 400;
			chase_name_h = 412;
			chase_stat_y = 400;
			chase_stat_h = 412;
		} else {
			res = 0;
			track_name_left = 139;
			track_name_right = 211;
			track_stat_left = 241;
			track_stat_right = 279;
			track_name_y = 181;
			track_name_h = 186;
			track_stat_y = 181;
			track_stat_h = 186;
			chase_name_left = 139;
			chase_name_right = 211;
			chase_stat_left = 241;
			chase_stat_right = 279;
			chase_name_y = 167;
			chase_name_h = 172;
			chase_stat_y = 167;
			chase_stat_h = 172;
		}
	}

	/* Only cockpit mode blits the sprite (stand-alone viewer draws its
	 * HUD from the panel backdrop). */
	if (maingameflag) {
		drawshape(farbufferptrs[REPLAY_BUTTON_SHAPE_BASE + idx], replaybuttonleft[idx][res],
				  replaybuttontop[idx][res], 0, 0);
	}

	if (idx == 14 || idx == 32) {
		festring_setbound(track_name_left, track_name_y, track_name_right, track_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setbound(track_stat_left, track_stat_y, track_stat_right, track_stat_h);
		clearwindow();
	}
	if (idx == 15 || idx == 33) {
		festring_setbound(track_name_left, track_name_y, track_name_right, track_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setcursor(track_name_left, track_name_y);
		replay_outputobjectname(trackobject);

		uint8_t sp = (trackobject >= 0x3800u) ? staticobjects[trackobject - 14336].species
											  : objects[trackobject].ship_idx;
		trackspecies = sp;

		festring_setbound(track_stat_left, track_stat_y, track_stat_right, track_stat_h);
		clearwindow();
		festring_setcursor(track_stat_left, track_stat_y);
		festring_settextcolor(0x4E);
		int st = replay_getstatusnum(trackobject);
		festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
	}
	if (idx == 16 || idx == 34) {
		festring_setbound(chase_name_left, chase_name_y, chase_name_right, chase_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setbound(chase_stat_left, chase_stat_y, chase_stat_right, chase_stat_h);
		clearwindow();
		cameraposstate = 0;
	}
	if (idx == 17 || idx == 35) {
		festring_setbound(chase_name_left, chase_name_y, chase_name_right, chase_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setcursor(chase_name_left, chase_name_y);
		replay_outputobjectname(pstate.target_obj_idx);

		uint8_t sp = (pstate.target_obj_idx >= 0x3800u) ? staticobjects[pstate.target_obj_idx - 14336].species
														: objects[pstate.target_obj_idx].ship_idx;
		chasespecies = sp;

		festring_setbound(chase_stat_left, chase_stat_y, chase_stat_right, chase_stat_h);
		clearwindow();
		festring_setcursor(chase_stat_left, chase_stat_y);
		festring_settextcolor(0x4E);
		int st = replay_getstatusnum(pstate.target_obj_idx);
		festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
		cameraposstate = 1;
	}
}

/* --------------------------------------------------------------------------
 * Object classifier + label formatters
 * -------------------------------------------------------------------------- */

/* replay_getstatusnum — classify obj_id into a "status-bar" code 0..6.
 *    0 = normal (fighter / has shields)
 *    2 = all status flags clear (disabled application)
 *    3 = docking in progress
 *    4 = satellite (species 143/144) without species_idx    (no personnel)
 *    5 = satellite with species_idx                          (boarded)
 *    6 = disabled-and-drained (all shields zero, non-fighter)
 *
 * Static objects (obj_id >= 0x3800) never reach any craft_ptr deref
 * below: staticobjects[].species is in the buoy range (70..84). */
// FUNCTION: TIE 0x462BC
int replay_getstatusnum(uint16_t obj_id) {
	if (obj_id >= 0x3800u) {
		uint16_t species = staticobjects[obj_id - 14336].species;
		if (species == 144 || species == 143)
			return 4;
		if (species_table[species].category)
			return 0;
		return 0;
	}

	uint16_t species = objects[obj_id].ship_idx;
	CraftData* craft_ptr = objects[obj_id].craft_ptr;

	if (species == 144 || species == 143) {
		return craft_ptr->species_idx ? 5 : 4;
	}

	if (species_table[species].category) {
		return 0;
	}
	if (craft_ptr->dock_state_flags) {
		return 3;
	}
	if (!craft_ptr->status_flags) {
		return 2;
	}
	if ((int32_t)craft_ptr->rear_shield + (int32_t)craft_ptr->forward_shield != 0 ||
		objects[obj_id].genus == GENUS_FIGHTER) {
		return 0;
	}
	return 6;
}

/* replay_outputobjectname — format obj_id's display name into tempstring
 * and outstring-center it. Identical to demo. */
// FUNCTION: TIE 0x46094
void replay_outputobjectname(uint16_t obj_id) {
	if (obj_id >= 0x3800u) {
		festring_settextcolor(0x43);
		uint16_t species = staticobjects[obj_id - 14336].species;
		if (species >= 70 && species <= 84) {
			festring_farstrcpy(((char**)buoystr)[species - 70]);
		}
		festring_outstringcenter((const uint8_t*)tempstring);
		return;
	}

	uint8_t side = objects[obj_id].side;
	uint16_t head_color;
	if (side == 0)
		head_color = 'Q';
	else if (side == 1 || side == 4)
		head_color = 'I';
	else if (side == 2)
		head_color = 'E';
	else
		head_color = 'U';
	festring_settextcolor(head_color);

	if (objects[obj_id].category) {
		uint16_t ship = objects[obj_id].ship_idx;
		if (ship >= 143 && ship <= 154) {
			festring_farstrcpy(((char**)warheadstrings)[ship - 143]);
		}
		festring_outstringcenter((const uint8_t*)tempstring);
		return;
	}

	CraftData* cp = objects[obj_id].craft_ptr;
	festring_farstrcpy(spec_data[cp->species_idx].short_name);
	festring_farstradd(':');
	festring_farstradd(' ');
	festring_farstradd((char)254);

	uint8_t s = objects[obj_id].side;
	char tag;
	if (s == 0)
		tag = 'R';
	else if (s == 1 || s == 4)
		tag = 'J';
	else if (s == 2)
		tag = 'F';
	else
		tag = 'V';
	festring_farstradd(tag);

	festring_farstrcat(fg_array[objects[obj_id].fg_idx].name);
	if (fg_array[objects[obj_id].fg_idx].count > 1) {
		festring_farstradd(' ');
		festring_farstradd((char)(cp->craft_idx_in_fg + '1'));
	}
	festring_outstringcenter((const uint8_t*)tempstring);
}

/* replay_outputclipname — paint the current clip name centered in the
 * info strip. Retail added an SVGA cockpit strip (341, 13, 411, 22). */
// FUNCTION: TIE 0x463BC
void replay_outputclipname(void) {
	festring_setbackcolor(0x40);
	int16_t cx, cy;
	if (maingameflag) {
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			festring_setbound(169, 5, 206, 11);
			cx = 169;
			cy = 5;
		} else {
			festring_setbound(341, 13, 411, 22);
			cx = 341;
			cy = 13;
		}
	} else if (tie_is_high_resolution_flight()) {
		festring_setbound(260, 360, 339, 373);
		cx = 260;
		cy = 360;
	} else {
		festring_setbound(130, 150, 169, 155);
		cx = 130;
		cy = 150;
	}
	festring_setcursor(cx, cy);
	clearwindow();
	festring_settextcolor(0x43);
	festring_outstringcenter((const uint8_t*)replayclipname);
}

/* --------------------------------------------------------------------------
 * Playback-state mutators
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE 0x4596C
void replay_stopreplay(void) {
	TieReplayTiming_Reset();
	updateactionflag = 0;
	replay_replaymessage(MSG_FILM_END);
	replay_drawreplaybutton(2);
	if (replaymusic == 1) {
		replayvolume = (int16_t)imuse_get_master_vol(im);
		imuse_set_master_vol(im, 0);
		imuse_pause(im);
		replaymusic = 0;
	}
}

// FUNCTION: TIE 0x458DC
void replay_rewindreplay(void) {
	TieReplayTiming_Reset();
	if (replaymusic == 1) {
		replayvolume = (int16_t)imuse_get_master_vol(im);
		imuse_set_master_vol(im, 0);
		imuse_pause(im);
		replaymusic = 0;
	}
	replayio_copyfromsave(replaystartfile);
	replaytotalcntdown = 0;
	replaypercent = -1;
	math2_randomseed = (int16_t)replayrandomseed;
	if (replayspoolflag) {
		if (!replay_loadreplayinput()) {
			replaytotalcnt = 0;
			replaytotalcntdown = 0;
			replay_stopreplay();
			return;
		}
	}
	replaybuffercnt = 0;
	replayptr = replaybufferstart;
	updateactionflag = 0;
	replay_drawreplaybutton(2);
}

/* replay_loadreplayinput — refill one in-memory chunk from inputspoolfile.
 * Reads only the remaining valid records and shows "loading"
 * (MSG_CAMERA_LOADING) first. Returns 1 on success, 0 on I/O error.
 * Preserves fileptr across the call. */
// FUNCTION: TIE 0x44F70
int16_t replay_loadreplayinput(void) {
	replay_replaymessage(MSG_CAMERA_LOADING);
	if (replaytotalcnt <= 0 || replaytotalcntdown >= (uint32_t)replaytotalcnt)
		return 0;

	TieFile* saved = fileptr;
	uint8_t* bufp = (uint8_t*)replaybufferstart;
	memset(bufp, 0, REPLAY_INPUT_BUFFER_BYTES);

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "rb", 1)) {
		fileptr = saved;
		return 0;
	}

	/* The versioned header sits in front of the input frames. Validate first;
	 * the per-frame seek then targets `header + frame_size * idx`. */
	if (!TieReplayFormat_ReadHeader(fileptr, NULL)) {
		TieStorage_Close(fileptr);
		fileptr = saved;
		return 0;
	}

	long frame_offset =
		(long)REPLAY_FORMAT_HEADER_SIZE + (long)REPLAYINPUTFRAME_DISK_SIZE * (long)replaytotalcntdown;
	if (TieStorage_Seek(fileptr, frame_offset, TIE_SEEK_SET)) {
		TieStorage_Close(fileptr);
		fileptr = saved;
		return 0;
	}

	const uint32_t remaining = (uint32_t)replaytotalcnt - replaytotalcntdown;
	const uint16_t frames_to_read =
		(uint16_t)(remaining < REPLAY_INPUT_CHUNK_FRAMES ? remaining : REPLAY_INPUT_CHUNK_FRAMES);
	for (uint16_t frame = 0; frame < frames_to_read; ++frame) {
		size_t n = TieStorage_Read(bufp, 1, REPLAYINPUTFRAME_DISK_SIZE, fileptr);
		bufp += n;
		if (n != REPLAYINPUTFRAME_DISK_SIZE) {
			TieStorage_Close(fileptr);
			fileptr = saved;
			return 0;
		}
	}
	TieStorage_Close(fileptr);
	fileptr = saved;
	return 1;
}

/* --------------------------------------------------------------------------
 * Camera pose
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE 0x474B4
void replay_movecambehind(uint16_t obj_id) {
	create_getworldposition(obj_id, 0);
	replaycam.x = worldlocx;
	replaycam.y = worldlocy;
	replaycam.z = worldlocz;

	uint16_t sp = (obj_id >= 0x3800u) ? staticobjects[obj_id - 14336].species : objects[obj_id].ship_idx;
	uint16_t quarter_w = species_table[sp].bound_hwidth >> 2;

	int32_t push_x =
		((worldeyeA3 * replaycam.view_zoom) >> 15) + 4 * ((worldeyeA3 * (int32_t)quarter_w) >> 15);
	int32_t push_y =
		((worldeyeB3 * replaycam.view_zoom) >> 15) + 4 * ((worldeyeB3 * (int32_t)quarter_w) >> 15);
	int32_t push_z =
		((worldeyeC3 * replaycam.view_zoom) >> 15) + 4 * ((worldeyeC3 * (int32_t)quarter_w) >> 15);
	replaycam.x -= push_x;
	replaycam.y -= push_y;
	replaycam.z -= push_z;
}

// FUNCTION: TIE 0x47190
void replay_calcreplayview(void) {
	uint8_t chase_sp = (pstate.target_obj_idx >= 0x3800u)
						   ? staticobjects[pstate.target_obj_idx - 14336].species
						   : objects[pstate.target_obj_idx].ship_idx;
	if (chase_sp != chasespecies) {
		pstate.target_obj_idx = pstate.object_idx;
		replay_drawreplaybutton(0x11);
	}

	if (replaycam.view_pitch_offset) {
		replaycam.view_pitch_offset = 1;
		camera.x = replaycam.x;
		camera.roll = 0;
		camera.up_angle = 0;
		camera.y = replaycam.y;
		camera.z = replaycam.z;
		camera.side_angle = 0;
		fview_newcalcview(0, (int16_t)camera.cam_heading, (int16_t)camera.cam_pitch, 0, 0, 0, NULL);
		TieFlightSnapshot_RecordCameraBasis();
	} else {
		uint16_t new_heading, new_pitch;
		if (pstate.target_obj_idx >= 0x3800u) {
			replaycam.roll = (int16_t)(staticobjects[pstate.target_obj_idx - 14336].roll_byte << 8);
			new_pitch = (uint16_t)(staticobjects[pstate.target_obj_idx - 14336].pitch_byte << 8);
			new_heading = (uint16_t)(staticobjects[pstate.target_obj_idx - 14336].yaw_byte << 8);
		} else {
			replaycam.roll = objects[pstate.target_obj_idx].roll;
			new_pitch = (uint16_t)objects[pstate.target_obj_idx].pitch;
			new_heading = (uint16_t)objects[pstate.target_obj_idx].heading;
		}
		camera.cam_heading = new_heading;
		camera.cam_pitch = new_pitch;
		camera.roll = replaycam.roll;
		camera.up_angle = replaycam.up_angle;
		camera.side_angle = replaycam.side_angle;
		fview_newcalcview(replaycam.roll, (int16_t)new_heading, (int16_t)new_pitch, 0, replaycam.side_angle,
						  replaycam.up_angle, NULL);
		TieFlightSnapshot_RecordCameraBasis();
		replay_movecambehind(pstate.target_obj_idx);
		camera.x = replaycam.x;
		camera.y = replaycam.y;
		camera.z = replaycam.z;
	}

	if (trackobject != 0xFFFFu) {
		uint8_t track_sp = (trackobject >= 0x3800u) ? staticobjects[trackobject - 14336].species
													: objects[trackobject].ship_idx;
		if (track_sp == trackspecies) {
			create_getworldposition(trackobject, 0);
			trig2_ctop(worldlocx - camera.x, worldlocy - camera.y, worldlocz - camera.z);
			camera.roll = 0;
			replaycam.roll = 0;
			camera.cam_heading = (uint16_t)trig2_zangle;
			camera.cam_pitch = (uint16_t)trig2_xyangle;
			camera.up_angle = 0;
			camera.side_angle = 0;
			fview_newcalcview(0, trig2_zangle, trig2_xyangle, 0, 0, 0, NULL);
			TieFlightSnapshot_RecordCameraBasis();
		} else {
			trackobject = 0xFFFFu;
			replay_drawreplaybutton(0xC);
			replay_drawreplaybutton(0xE);
		}
	}
}

/* --------------------------------------------------------------------------
 * Modal text entry
 * -------------------------------------------------------------------------- */

typedef enum ReplaySavePhase {
	REPLAY_SAVE_PHASE_BEGIN = 0,
	REPLAY_SAVE_PHASE_EDIT_NAME,
	REPLAY_SAVE_PHASE_CHECK_FILE,
	REPLAY_SAVE_PHASE_CONFIRM_REPLACE,
	REPLAY_SAVE_PHASE_WRITE,
	REPLAY_SAVE_PHASE_FINISH,
} ReplaySavePhase;

typedef struct ReplaySaveTask {
	uint8_t name_input[40];
	char filename[16];
	int16_t prompt_cx;
	int16_t prompt_cy;
	uint16_t result;
	uint8_t position;
	uint8_t editor_active;
	uint8_t front_surface_route;
	ReplaySavePhase phase;
} ReplaySaveTask;

static uint16_t replay_savereplay_file(const uint8_t* name_input, const char* filename);

static void replay_save_present_front(void) {
	if (!TieClassicDisplay_UsesDx5())
		return;
	FrontendDisplay_PresentFrontSurface();
	FrontendDisplay_PresentFrame();
}

static void replay_save_draw_editor(const ReplaySaveTask* t) {
	festring_setcursor(t->prompt_cx, t->prompt_cy);
	festring_outstring(t->name_input);
	festring_setbackcolor(0x4A);
	outchar(' ');
	festring_setbackcolor(0x2C);
	outchar('\n');
}

/* FUNCTION: TIE 0x476CC, TIE98 0x474CA0 (task-split recovery)
 * PORT: the original polls inside REPLAY_editstring until Enter. Host input is
 * queued once per application frame, so this state machine consumes one key
 * per step and yields whenever the queue is empty. */
static bool replay_save_edit_name_step(ReplaySaveTask* t) {
	feinput_getinput();

	if (keypress == 1)
		keypress = 8;
	if (keypress == 8 && t->position > 0)
		--t->position;

	if (keypress >= 48) {
		if ((keypress < 65 && keypress >= 58) || (keypress < 97 && keypress >= 91) || (keypress >= 123))
			keypress = 1;
	} else if (keypress != 13 && keypress != 45 && keypress != 0) {
		keypress = 1;
	}

	if (keypress != 1 && keypress != 13 && keypress != 0 && t->position < 8) {
		uint8_t ch = (uint8_t)keypress;
		if (ch >= 'a' && ch <= 'z')
			ch -= 32;
		t->name_input[t->position++] = ch;
	}
	t->name_input[t->position] = 0;

	if (keypress) {
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Lock();
		replay_save_draw_editor(t);
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Unlock();
		replay_save_present_front();
	}
	return keypress == 13;
}

static void replay_save_finish_editor(ReplaySaveTask* t) {
	if (TieClassicDisplay_UsesDx5())
		FlightSurface_Lock();
	festring_setcursor(t->prompt_cx, t->prompt_cy);
	festring_outstring(t->name_input);
	outchar('\n');
	if (TieClassicDisplay_UsesDx5())
		FlightSurface_Unlock();
	festring_setautofill(0);
	festring_setfontsize(2);
	t->editor_active = 0;
	replay_save_present_front();
}

static void replay_save_build_filename(ReplaySaveTask* t) {
	size_t n = 0;
	while (n < sizeof(t->filename) - 1 && t->name_input[n]) {
		t->filename[n] = (char)t->name_input[n];
		++n;
	}
	size_t s = 0;
	while (n < sizeof(t->filename) - 1 && kClipSuffix[s])
		t->filename[n++] = kClipSuffix[s++];
	t->filename[n] = '\0';
}

static LandruTaskStepResult replay_save_task_step(void* self) {
	ReplaySaveTask* t = (ReplaySaveTask*)self;
	const bool tie98_display = TieClassicDisplay_UsesDx5();

	switch (t->phase) {
		case REPLAY_SAVE_PHASE_BEGIN:
			if (replaymusic == 1) {
				replaymusic = 0;
				replayvolume = (int16_t)imuse_get_master_vol(im);
				imuse_set_master_vol(im, 0);
				imuse_pause(im);
			}
			if (tie98_display) {
				FrontendDisplay_PresentFrame();
				FrontendDisplay_PresentFrontSurface();
				g_flightDrawToOffscreenSurface = 0;
				t->front_surface_route = 1;
				FlightSurface_Lock();
			}
			replay_drawreplaybutton(7);
			replay_replaymessage(MSG_ENTER_FILENAME);
			festring_setfontsize(1);
			festring_setautofill(1);
			t->editor_active = 1;
			if (tie_is_high_resolution_flight()) {
				t->prompt_cx = 126;
				t->prompt_cy = 456;
			} else {
				t->prompt_cx = 74;
				t->prompt_cy = 190;
			}
			replay_save_draw_editor(t);
			if (tie98_display)
				FlightSurface_Unlock();
			replay_save_present_front();
			t->phase = REPLAY_SAVE_PHASE_EDIT_NAME;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAY_SAVE_PHASE_EDIT_NAME:
			if (!TieInput_KeyPending())
				return LANDRU_TASK_STEP_YIELD;
			if (!replay_save_edit_name_step(t))
				return LANDRU_TASK_STEP_CONTINUE;
			replay_save_finish_editor(t);
			if (!t->name_input[0]) {
				t->result = MSG_REPLAY_NOT_SAVED;
				t->phase = REPLAY_SAVE_PHASE_FINISH;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			replay_save_build_filename(t);
			t->phase = REPLAY_SAVE_PHASE_CHECK_FILE;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAY_SAVE_PHASE_CHECK_FILE: {
			TieFile* existing = TieStorage_Open(TIE_FILE_ROOT_USER, t->filename, "rb");
			if (!existing) {
				t->phase = REPLAY_SAVE_PHASE_WRITE;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			TieStorage_Close(existing);
			if (tie98_display)
				FlightSurface_Lock();
			replay_replaymessage(MSG_FILE_REPLACE);
			if (tie98_display)
				FlightSurface_Unlock();
			replay_save_present_front();
			t->phase = REPLAY_SAVE_PHASE_CONFIRM_REPLACE;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case REPLAY_SAVE_PHASE_CONFIRM_REPLACE: {
			if (!TieInput_KeyPending())
				return LANDRU_TASK_STEP_YIELD;
			const int key = TieInput_ReadKey();
			if (key != 'y' && key != 'Y') {
				t->result = MSG_REPLAY_NOT_SAVED;
				t->phase = REPLAY_SAVE_PHASE_FINISH;
			} else {
				t->phase = REPLAY_SAVE_PHASE_WRITE;
			}
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case REPLAY_SAVE_PHASE_WRITE:
			t->result = replay_savereplay_file(t->name_input, t->filename);
			t->phase = REPLAY_SAVE_PHASE_FINISH;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAY_SAVE_PHASE_FINISH:
			if (t->front_surface_route) {
				g_flightDrawToOffscreenSurface = 1;
				t->front_surface_route = 0;
				FlightSurface_Lock();
			}
			replay_replaymessage(t->result);
			if (!tie98_display)
				replay_drawreplaybutton(6);
			replay_outputclipname();
			if (tie98_display)
				FlightSurface_Unlock();
			return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_DONE;
}

static void replay_save_task_end(void* self) {
	ReplaySaveTask* t = (ReplaySaveTask*)self;
	/* PORT: forced task-stack teardown must not leave the recovered renderer
	 * routed to the TIE98 front surface or retain modal text state. */
	if (t->front_surface_route)
		g_flightDrawToOffscreenSurface = 1;
	if (t->editor_active) {
		festring_setautofill(0);
		festring_setfontsize(2);
	}
}

static const LandruTaskVtable replay_save_task_vt = {
	.step = replay_save_task_step,
	.end = replay_save_task_end,
};

static bool replay_Push_SaveReplay_Task(void) {
	ReplaySaveTask* t = (ReplaySaveTask*)landru_task_push(&replay_save_task_vt);
	if (!t)
		return false;
	memset(t, 0, sizeof *t);
	t->phase = REPLAY_SAVE_PHASE_BEGIN;
	return true;
}

/* --------------------------------------------------------------------------
 * Replay clip save / load (.clp files)
 *
 * Retail file format:
 *   u32  replaytotalcnt        — number of 8-byte input frames
 *   u16  replayrandomseed      — RNG seed at record start
 *   savearrayptrs/sizes blocks — full u32-stride iteration
 *                                 (all 67 dynamic-state regions)
 *   0x36C0 fg_array            — byte dump
 *   0x5A0  radiomsg
 *   0x70   cut
 *   0x900  fgstatus
 *   0xA1   species_table.load_flags
 *   0x198  camera block
 *   modern timing checkpoint   — cadence and high-rate remainders
 *   N*14   input stream        — chunked reads/writes
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE 0x45078, TIE98 0x4724E0 (file-write portion)
static uint16_t replay_savereplay_file(const uint8_t* name_input, const char* filename) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, filename, "wb", 0))
		return MSG_FILE_ERROR;

	/* The versioned header precedes the clip preamble. */
	if (!TieReplayFormat_WriteHeader(fileptr)) {
		TieStorage_Close(fileptr);
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}

	/* Clip preamble: little-endian u32 frame count + u16 seed. */
	TieStorage_Putc((int)(replaytotalcnt & 0xFF), fileptr);
	TieStorage_Putc((int)((replaytotalcnt >> 8) & 0xFF), fileptr);
	TieStorage_Putc((int)((replaytotalcnt >> 16) & 0xFF), fileptr);
	TieStorage_Putc((int)((replaytotalcnt >> 24) & 0xFF), fileptr);
	TieStorage_Putc(replayrandomseed & 0xFF, fileptr);
	TieStorage_Putc((replayrandomseed >> 8) & 0xFF, fileptr);

	TieFile* clip_fp = fileptr;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, replaystartfile, "rb", 0)) {
		TieStorage_Close(fileptr);
		TieStorage_Close(clip_fp);
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}

	/* Retail walks savearraysizes as u32[] — full iteration, all 67 slots. */
	for (size_t i = 0; savearrayptrs[i]; ++i) {
		if (!replay_copybytesinfile((uint16_t)savearraysizes[i], fileptr, clip_fp)) {
			TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
			return MSG_FILE_ERROR;
		}
	}
	if (!replay_copybytesinfile(0x36C0, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x5A0, fileptr, clip_fp) || !replay_copybytesinfile(0x70, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x900, fileptr, clip_fp) || !replay_copybytesinfile(0xA1, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x198, fileptr, clip_fp) ||
		!replay_copybytesinfile((uint16_t)TieFlightCheckpoint_Size(), fileptr, clip_fp)) {
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}
	TieStorage_Close(fileptr);

	/* Input stream — N fixed-size versioned records.
	 * When pulling from input.spl skip past the format header before
	 * copying records. */
	if (replayspoolflag) {
		if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "rb", 0)) {
			TieStorage_Close(clip_fp);
			TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
			return MSG_FILE_ERROR;
		}
		if (!TieReplayFormat_ReadHeader(fileptr, NULL) ||
			TieStorage_Seek(fileptr, REPLAY_FORMAT_HEADER_SIZE, TIE_SEEK_SET) != 0) {
			TieStorage_Close(fileptr);
			TieStorage_Close(clip_fp);
			TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
			return MSG_FILE_ERROR;
		}
		for (uint32_t i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (!replay_copybytesinfile(REPLAYINPUTFRAME_DISK_SIZE, fileptr, clip_fp)) {
				TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
				return MSG_FILE_ERROR;
			}
		}
		TieStorage_Close(fileptr);
	} else {
		const uint8_t* bufp = (const uint8_t*)replaybufferstart;
		for (uint32_t i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (TieStorage_Write(bufp, 1, REPLAYINPUTFRAME_DISK_SIZE, clip_fp) !=
				REPLAYINPUTFRAME_DISK_SIZE) {
				TieStorage_Close(clip_fp);
				TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
				return MSG_FILE_ERROR;
			}
			bufp += REPLAYINPUTFRAME_DISK_SIZE;
		}
	}

	if (TieStorage_Close(clip_fp) != 0) {
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}

	memset(replayclipname, 0, sizeof(replayclipname));
	size_t m = 0;
	while (m < sizeof(replayclipname) - 1 && name_input[m]) {
		replayclipname[m] = (char)name_input[m];
		++m;
	}
	return MSG_REPLAY_SAVED;
}

// FUNCTION: TIE 0x455B4
int replay_loadreplay(void) {
	char filename[16];
	build_clip_filename(filename);
	if (!filename[0])
		return 0;

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, filename, "rb", 0))
		return 0;

	/* Versioned format header guards the rest of the parse — anything
	 * else (legacy retail clip, mismatched build) is rejected here. */
	if (!TieReplayFormat_ReadHeader(fileptr, NULL)) {
		TieStorage_Close(fileptr);
		return 0;
	}

	uint8_t preamble[6];
	if (TieStorage_Read(preamble, 1, sizeof preamble, fileptr) != sizeof preamble) {
		TieStorage_Close(fileptr);
		return 0;
	}
	uint32_t cnt = br_u32le(preamble);
	if (!cnt || cnt > REPLAY_MAX_TOTAL_RECORDS) {
		TieStorage_Close(fileptr);
		return 0;
	}
	replaytotalcnt = (int32_t)cnt;
	replayrandomseed = br_u16le(preamble + 4);

	TieFile* clip_fp = fileptr;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, replaystartfile, "wb", 0)) {
		TieStorage_Close(clip_fp);
		return 0;
	}

	/* Retail: full u32-stride iteration. */
	for (size_t i = 0; savearrayptrs[i]; ++i) {
		if (!replay_copybytesinfile((uint16_t)savearraysizes[i], clip_fp, fileptr)) {
			return 0;
		}
	}
	if (!replay_copybytesinfile(0x36C0, clip_fp, fileptr) ||
		!replay_copybytesinfile(0x5A0, clip_fp, fileptr) || !replay_copybytesinfile(0x70, clip_fp, fileptr) ||
		!replay_copybytesinfile(0x900, clip_fp, fileptr) || !replay_copybytesinfile(0xA1, clip_fp, fileptr) ||
		!replay_copybytesinfile(0x198, clip_fp, fileptr) ||
		!replay_copybytesinfile((uint16_t)TieFlightCheckpoint_Size(), clip_fp, fileptr)) {
		return 0;
	}
	if (TieStorage_Close(fileptr) != 0) {
		TieStorage_Close(clip_fp);
		return 0;
	}

	if (replayspoolflag) {
		/* Reconstitute input.spl from the clip's input stream — open
		 * fresh and prefix with the current header so subsequent
		 * spool/load goes through the standard format path. */
		if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "wb", 0)) {
			TieStorage_Close(clip_fp);
			return 0;
		}
		if (!TieReplayFormat_WriteHeader(fileptr)) {
			TieStorage_Close(fileptr);
			TieStorage_Close(clip_fp);
			return 0;
		}
		for (uint32_t i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (!replay_copybytesinfile(REPLAYINPUTFRAME_DISK_SIZE, clip_fp, fileptr)) {
				return 0;
			}
		}
		int rc = (TieStorage_Close(fileptr) == 0);
		TieStorage_Close(clip_fp);
		return rc;
	} else {
		uint8_t* bufp = (uint8_t*)replaybufferstart;
		if ((uint32_t)replaytotalcnt > REPLAY_INPUT_CHUNK_FRAMES)
			replaytotalcnt = REPLAY_INPUT_CHUNK_FRAMES;
		for (uint32_t i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			int16_t got = fediskio_readfileblock(bufp, 1, REPLAYINPUTFRAME_DISK_SIZE, clip_fp);
			bufp += got;
			if ((uint16_t)got != REPLAYINPUTFRAME_DISK_SIZE) {
				TieStorage_Close(clip_fp);
				return 0;
			}
		}
		TieStorage_Close(clip_fp);
		return 1;
	}
}

typedef enum {
	REPLAY_DOSCREEN_PHASE_INIT = 0,
	REPLAY_DOSCREEN_PHASE_POLL,
} ReplayDoScreenPhase;

typedef struct ReplayDoScreenTask {
	uint16_t last_chase_status;
	uint16_t last_track_status;
	TieFlightScreen previous_screen;
	ReplayDoScreenPhase phase;
} ReplayDoScreenTask;

static void replay_doreplayscreen_init(ReplayDoScreenTask* t) {
	TieReplayTiming_Reset();
	t->last_chase_status = 0xFFFF;
	t->last_track_status = 0xFFFF;

	pstate.target_obj_idx = pstate.object_idx;
	replaycam.view_zoom_flag = 1;
	replaycam.view_zoom = 1280;
	replaycam.view_pitch_offset = 0;
	replaycam.up_angle = 0;
	replaycam.side_angle = 0;
	fullupdateflag = 1;
	trackobject = 0xFFFFu;
	if (TieProfile_UsesTie98Logic())
		g_flightInitialTextureCacheFlushPending = 1;
	tie_updatescreen();
	if (TieClassicDisplay_UsesDx5())
		FlightSurface_Lock();
	updateactionflag = 0;
	fastforwardflag = 0;
	fastforwardtimer = 0;
	replay_drawreplaybutton(0xA);
	replay_drawreplaybutton(0x11);
	replay_drawreplaybutton(0xC);
	replay_drawreplaybutton(0xE);
	replay_outputclipname();
	if (TieClassicDisplay_UsesDx5())
		FlightSurface_Unlock();
	exitflag = 0;
}

static bool replay_doreplayscreen_body(ReplayDoScreenTask* t) {
	uint16_t last_chase_status = t->last_chase_status;
	uint16_t last_track_status = t->last_track_status;
	bool advance_message_timer = false;
	bool pushed_subtask = false;
	const bool tie98_display = TieClassicDisplay_UsesDx5();

	{
		if (tie98_display)
			FlightSurface_Lock();
		const bool save_requested = replay_replayinput();
		if (save_requested) {
			if (tie98_display)
				FlightSurface_Unlock();
			if (replay_Push_SaveReplay_Task())
				return true;
			if (tie98_display)
				FlightSurface_Lock();
		}

		if (!updateactionflag || fastforwardflag) {
			if (replaymusic == 1) {
				replaymusic = 0;
				replayvolume = (int16_t)imuse_get_master_vol(im);
				imuse_set_master_vol(im, 0);
				imuse_pause(im);
			}
		} else if (!replaymusic) {
			replaymusic = 1;
			imuse_set_master_vol(im, (uint16_t)replayvolume);
			imuse_resume(im);
		}

		const bool advance_replay = updateactionflag && TieReplayTiming_IsFrameDue();
		if (advance_replay) {
			advance_message_timer = true;
			/* In replay mode tie_doframe never returns false (the
			 * tickcounter budget gate is bypassed by the replayviewmode
			 * branch); cast to void to acknowledge the unused result. */
			if (tie98_display)
				FlightSurface_Unlock();
			(void)tie_doframe();
			TieReplayTiming_ConsumeFrame();
			const int32_t info_screen = user_consume_info_room_request();
			if (info_screen >= 0) {
				user_Push_InflightInfo_Task(info_screen);
				pushed_subtask = true;
			}
			if (tie98_display)
				FlightSurface_Lock();
			if (cameraposstate) {
				festring_setbackcolor(0x40);
				int16_t cx, cy;
				if (maingameflag) {
					if (flightResolution == TIE_FLIGHT_RES_VGA) {
						festring_setbound(230, 167, 268, 172);
						cx = 230;
						cy = 167;
					} else {
						festring_setbound(464, 402, 532, 411);
						cx = 464;
						cy = 402;
					}
				} else if (tie_is_high_resolution_flight()) {
					festring_setbound(484, 400, 556, 412);
					cx = 484;
					cy = 400;
				} else {
					festring_setbound(241, 167, 279, 172);
					cx = 241;
					cy = 167;
				}
				festring_setcursor(cx, cy);
				uint16_t st = (uint16_t)replay_getstatusnum(pstate.target_obj_idx);
				if (st != last_chase_status) {
					clearwindow();
					festring_settextcolor(0x4E);
					last_chase_status = st;
					festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
				}
			}
			if (trackobject != 0xFFFFu) {
				festring_setbackcolor(0x40);
				int16_t cx, cy;
				if (maingameflag) {
					if (flightResolution == TIE_FLIGHT_RES_VGA) {
						festring_setbound(230, 181, 268, 186);
						cx = 230;
						cy = 181;
					} else {
						festring_setbound(464, 436, 532, 445);
						cx = 464;
						cy = 436;
					}
				} else if (tie_is_high_resolution_flight()) {
					festring_setbound(484, 437, 556, 448);
					cx = 484;
					cy = 437;
				} else {
					festring_setbound(241, 181, 279, 186);
					cx = 241;
					cy = 181;
				}
				festring_setcursor(cx, cy);
				uint16_t st = (uint16_t)replay_getstatusnum(trackobject);
				if (st != last_track_status) {
					clearwindow();
					festring_settextcolor(0x4E);
					last_track_status = st;
					festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
				}
			}
		} else if (!updateactionflag) {
			advance_message_timer = true;
			/* Paused branch: repaint once, recompute framerate from
			 * XTIMER delta spent repainting. */
			uint16_t t0 = tickcounter;
			if (tie98_display)
				FlightSurface_Unlock();
			tie_updatescreen();
			if (tie98_display) {
				FrontendDisplay_PresentFrame();
				if (g_useHardware3D)
					RenderScene_ClearFrameBuffers();
				else
					FrontendDisplay_BlitOffscreenToRenderSurface();
				FlightSurface_Lock();
			}
			tickcounter += (uint16_t)xtimer_time_elapsed();
			frameticks = tickcounter - t0;
			if (tickcounter == t0)
				frameticks = 1;
			framerate = 236 / frameticks;
			if (!framerate)
				framerate = 1;
		}

		festring_setfontsize(2);
		int16_t pct_cx, pct_cy;
		if (maingameflag) {
			if (flightResolution == TIE_FLIGHT_RES_VGA) {
				festring_setbound(154, 5, 165, 11);
				pct_cx = 156;
				pct_cy = 5;
			} else {
				festring_setbound(310, 13, 327, 22);
				pct_cx = 314;
				pct_cy = 13;
			}
		} else if (tie_is_high_resolution_flight()) {
			festring_setbound(230, 360, 251, 373);
			pct_cx = 234;
			pct_cy = 360;
		} else {
			festring_setbound(115, 150, 126, 155);
			pct_cx = 117;
			pct_cy = 150;
		}
		festring_setcursor(pct_cx, pct_cy);
		festring_setbackcolor(0x40);

		uint16_t pct_fwd = math2_longpercentage(replaytotalcntdown, (uint32_t)replaymaxcnt);
		uint16_t pct = 100 - math2_fraction(100, pct_fwd);
		if (pct > 99)
			pct = 99;
		if (pct != replaypercent) {
			replaypercent = (int16_t)pct;
			clearwindow();
			festring_settextcolor(0x43);
			panelrts_outnum(pct, 2, 2);
		}

		if (advance_message_timer && frameticks >= (uint16_t)replaymsgtimer) {
			if (replaymsgtimer) {
				festring_setbackcolor(0x2C);
				/* Retail widens the clear rect to 640x480 in SVGA. */
				if (tie_is_high_resolution_flight()) {
					festring_setbound(0, 457, 640, 480);
				} else {
					festring_setbound(0, 190, 320, 200);
				}
				clearwindow();
			}
			replaymsgtimer = 0;
		} else if (advance_message_timer) {
			replaymsgtimer -= frameticks;
		}
	}
	if (tie98_display)
		FlightSurface_Unlock();

	t->last_chase_status = last_chase_status;
	t->last_track_status = last_track_status;
	return pushed_subtask;
}

// FUNCTION: TIE 0x4646C, TIE98 0x473AA0 (task-split recovery)
static LandruTaskStepResult replay_doreplayscreen_task_step(void* self) {
	ReplayDoScreenTask* t = (ReplayDoScreenTask*)self;

	if (t->phase == REPLAY_DOSCREEN_PHASE_INIT) {
		replay_doreplayscreen_init(t);
		t->phase = REPLAY_DOSCREEN_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (exitflag)
		return LANDRU_TASK_STEP_DONE;

	const bool pushed_subtask = replay_doreplayscreen_body(t);
	if (pushed_subtask)
		return LANDRU_TASK_STEP_CONTINUE;

	/* YIELD to keep replay paced at one body-call per TieRuntime_Tick
	 * (one body invocation reads + applies one replay packet). Under
	 * the multi-step run_frame driver, returning CONTINUE here would
	 * fast-forward through recorded packets within a single TieRuntime_Tick. */
	return exitflag ? LANDRU_TASK_STEP_DONE : LANDRU_TASK_STEP_YIELD;
}

static uint64_t TieReplay_NextWakeDelayUs(const void* self) {
	(void)self;
	return TieReplayTiming_NextWakeDelayUs();
}

static void replay_doreplayscreen_task_end(void* self) {
	ReplayDoScreenTask* t = (ReplayDoScreenTask*)self;
	TieFlightScreen_SetActive(t->previous_screen);
}

static const LandruTaskVtable replay_doreplayscreen_task_vt = {
	.step = replay_doreplayscreen_task_step,
	.end = replay_doreplayscreen_task_end,
	.next_wake_delay_us = TieReplay_NextWakeDelayUs,
};

void replay_Push_DoReplayScreen_Task(void) {
	ReplayDoScreenTask* t = (ReplayDoScreenTask*)landru_task_push(&replay_doreplayscreen_task_vt);
	if (!t)
		return;
	/* PORT: Film Room playback bypasses the mission task that normally
	 * marks the snapshot as flight-owned. The replay screen's classic
	 * fallback remains selected through its TieFlightScreen value. */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
	t->previous_screen = TieFlightScreen_SetActive(TIE_FLIGHT_SCREEN_REPLAY_VIEWER);
	t->last_chase_status = 0xFFFF;
	t->last_track_status = 0xFFFF;
	t->phase = REPLAY_DOSCREEN_PHASE_INIT;
}

/* replay_replayinput — read one hardware tick of input and dispatch. */
// FUNCTION: TIE 0x46928, TIE98 0x474040
/* PORT: the boolean return is the child-task handoff; retail returns void. */
bool replay_replayinput(void) {
	bool save_requested = false;
	feinput_getrawinput();
	feinput_checkinput();

	if (inputkey) {
		switch (inputkey) {

			case 0x1B:
				if (!maingameflag) {
					updateactionflag = 0;
					exitflag = 1;
					mission.player_status = 16;
				}
				break;

			case KEY_A:
			case KEY_a:
				if (fastforwardflag) {
					fastforwardflag = 0;
					fastforwardtimer = 0;
					replay_replaymessage(MSG_FILM_ADVANCE_OFF);
					replay_drawreplaybutton(4);
				} else if (replaytotalcntdown < (uint32_t)replaytotalcnt) {
					fastforwardflag = 1;
					fastforwardtimer = 236;
					updateactionflag = 1;
					replay_replaymessage(MSG_FILM_ADVANCE_ON);
					replay_drawreplaybutton(5);
					replay_drawreplaybutton(3);
				} else {
					replay_replaymessage(MSG_FILM_END);
				}
				break;

			case KEY_C:
			case KEY_c: {
				uint16_t saved = pstate.object_idx;
				uint16_t cur = pstate.target_obj_idx;
				pstate.object_idx = 0xFFFEu;
				int32_t dir = (inputkey == KEY_C) ? -1 : +1;
				pstate.target_obj_idx = user_picknexttarget(cur, dir);
				pstate.object_idx = saved;
				replay_drawreplaybutton(0x11);
				if (replaycam.view_pitch_offset) {
					replay_movecambehind(pstate.target_obj_idx);
				}
			} break;

			case KEY_E:
			case KEY_e:
				replay_drawreplaybutton(9);
				updateactionflag = 0;
				mission.player_status = 15;
				exitflag = 1;
				break;

			case KEY_F:
			case KEY_f:
				if (replaycam.view_pitch_offset) {
					replaycam.view_pitch_offset = 0;
					replay_replaymessage(MSG_CAMERA_FOLLOW);
					replay_drawreplaybutton(0xA);
					replay_drawreplaybutton(0x11);
				} else {
					replaycam.view_pitch_offset = 1;
					create_getworldposition(pstate.target_obj_idx, 0);
					trig2_ctop(worldlocx - camera.x, worldlocy - camera.y, worldlocz - camera.z);
					camera.cam_heading = (uint16_t)trig2_zangle;
					camera.cam_pitch = (uint16_t)trig2_xyangle;
					replaycam.roll = 0;
					replay_replaymessage(MSG_CAMERA_FREE);
					replay_drawreplaybutton(0xB);
					replay_drawreplaybutton(0x10);
				}
				break;

			case KEY_L:
			case KEY_l:
				if (!maingameflag) {
					replay_drawreplaybutton(0x19);
					exitflag = 1;
					updateactionflag = 0;
					mission.player_status = 14;
				}
				break;

			case KEY_O:
			case KEY_o: {
				uint16_t saved = pstate.object_idx;
				pstate.object_idx = 0xFFFEu;
				int32_t dir = (inputkey == KEY_O) ? -1 : +1;
				trackobject = user_picknexttarget(trackobject, dir);
				pstate.object_idx = saved;
				replay_drawreplaybutton(0xD);
				replay_drawreplaybutton(0xF);
			} break;

			case KEY_P:
			case KEY_p:
				if (updateactionflag) {
					updateactionflag = 0;
					replay_replaymessage(MSG_FILM_STOPPED);
					replay_drawreplaybutton(2);
				} else if (replaytotalcntdown < (uint32_t)replaytotalcnt) {
					updateactionflag = 1;
					replay_replaymessage(MSG_FILM_STARTED);
					replay_drawreplaybutton(3);
				} else {
					replay_replaymessage(MSG_FILM_END);
				}
				break;

			case KEY_R:
			case KEY_r:
				replay_drawreplaybutton(1);
				replay_rewindreplay();
				replay_replaymessage(MSG_FILM_REWOUND);
				replay_drawreplaybutton(0);
				break;

			case KEY_S:
			case KEY_s:
				if (maingameflag) {
					/* PORT: the recovered save dialog blocks for keyboard input.
					 * Let ReplayDoScreenTask push its non-blocking child task after
					 * releasing the flight surface. */
					save_requested = true;
				} else {
					/* Retail: ship_idx 12 (TIE Advanced) joined 5..9 + 16 as
					 * valid "re-enter simulator" types. */
					uint16_t sidx = pstate.player->ship_idx;
					int is_tie = (sidx >= 5u && sidx <= 9u) || sidx == 12u || sidx == 16u;
					if (is_tie && !pstate.player_craft->flight_flag) {
						replay_drawreplaybutton(0x25);
						updateactionflag = 0;
						exitflag = 1;
						reentersimflag = 1;
					} else {
						replay_replaymessage(MSG_NO_SIM_AVAILABLE);
					}
				}
				break;

			case KEY_T:
			case KEY_t:
				if (trackobject == 0xFFFFu) {
					uint16_t saved = pstate.object_idx;
					pstate.object_idx = 0xFFFEu;
					trackobject = user_picknexttarget(0xFFFFu, 1);
					pstate.object_idx = saved;
					replay_drawreplaybutton(0xD);
					replay_drawreplaybutton(0xF);
				} else {
					trackobject = 0xFFFFu;
					replay_drawreplaybutton(0xC);
					replay_drawreplaybutton(0xE);
				}
				break;

			default:
				break;
		}
	}

	/* Mouse translation: unchanged between demo and retail. */
	int16_t btn_val = inputbuttons & 0xF;
	if (btn_val == 1 || btn_val == 2) {
		replaycam.view_zoom_rate += 128;
		if ((uint16_t)replaycam.view_zoom_rate > 0x6000u)
			replaycam.view_zoom_rate = 24576;
		if (btn_val == 1) {
			replaycam.view_zoom -= user_framerateadjust(replaycam.view_zoom_rate);
			if (replaycam.view_zoom < 0)
				replaycam.view_zoom = 0;
		} else {
			replaycam.view_zoom += user_framerateadjust(replaycam.view_zoom_rate);
			if (replaycam.view_zoom > 5120)
				replaycam.view_zoom = 5120;
		}
	} else {
		replaycam.view_zoom_rate = 64;
	}

	if (replaycam.view_pitch_offset) {
		fview_calcrotatemove((int16_t)camera.cam_heading, (int16_t)camera.cam_pitch, NULL);
		int16_t rate = user_framerateadjust(replaycam.view_zoom_rate);
		int16_t dx = (int16_t)((craftmoveX * rate) >> 15);
		int16_t dy = (int16_t)((craftmoveY * rate) >> 15);
		int16_t dz = (int16_t)((craftmoveZ * rate) >> 15);
		if (btn_val == 2) {
			dx = -dx;
			dy = -dy;
			dz = -dz;
		}
		if (btn_val == 1 || btn_val == 2) {
			replaycam.x += dx;
			if (replaycam.x < -16777216)
				replaycam.x = -16777216;
			if (replaycam.x > 0x1000000)
				replaycam.x = 0x1000000;
			replaycam.y += dy;
			if (replaycam.y < -16777216)
				replaycam.y = -16777216;
			if (replaycam.y > 0x1000000)
				replaycam.y = 0x1000000;
			replaycam.z += dz;
			if (replaycam.z < -16777216)
				replaycam.z = -16777216;
			if (replaycam.z > 0x1000000)
				replaycam.z = 0x1000000;
		}
		camera.cam_pitch += (uint16_t)user_framerateadjust(inputdeltax);
		camera.cam_heading -= (uint16_t)user_framerateadjust(inputdeltay);
	} else {
		replaycam.up_angle += user_framerateadjust(inputdeltax);
		replaycam.side_angle += user_framerateadjust(inputdeltay);
	}
	return save_requested;
}
