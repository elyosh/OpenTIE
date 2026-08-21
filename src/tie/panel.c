#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/binio.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/fediskio.h"
#include "tie/festring.h"
#include "tie/filelen.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/gate.h"
#include "tie/laser.h" /* WarheadRecord, projectile_is_warhead_type — for PIP missile name path */
#include "tie/logbuf2.h"
#include "tie/math2.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/spec.h" /* spec_data — for target ship short_name */
#include "tie/spec.h"
#include "tie/static.h"
#include "tie/std3d_tie98.h"
#include "tie/sys2.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/trace2.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h"
#include "tie/xtrans2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_hud.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include "util/binio.h"

/* ------------------------------------------------------------------ */
/* Module-boundary externs                                            */
/* ------------------------------------------------------------------ */

/* Graphics function pointers (defined in tie.c; tie.h declares only a few). */

/* Separator glyph widths position suffixes after numeric fields. */
char separator_3_spaces[4] = { ' ', ' ', ' ', '\0' };        /* engine 0xC057C */
char separator_2_spaces[4] = { ' ', ' ', '\0', (char)0x1C }; /* engine 0xC0580 */
char separator_colon[4] = { '0', '0', ':', '\0' };           /* engine 0xC0584 */
char separator_period[4] = { '0', '0', '.', '\0' };          /* engine 0xC0588 */

/* user.c-owned. */
void user_resetview(void);

#define FARBUFF(i) ((const void*)farbufferptrs[(i)])

/* ------------------------------------------------------------------ */
/* PANEL-owned globals                                                */
/* ------------------------------------------------------------------ */

/* Bytes 2 and 3 are patched for the active cockpit resolution. */
// GLOBAL: TIE 0xC61A8
char cockpitdir[7] = "CP640/";
char parts[11] = { 0 };
/* LFD cockpit-palette section magic. */
char xpal_id[5] = "PLTT";
/* shieldcolor[11] -- palette-index ramp for the 4 shield-bar instruments
 * (19..22), indexed by lit-LED count 0..9; entry 10 is the damage-flash
 * override. Verified at byte_C61B4 in Z_TIE__.EXE. */
char shieldcolor[11] = { 0x2C, 0x35, 0x36, 0x37, 0x39, 0x3A, 0x3B, 0x3D, 0x3E, 0x3F, 0x2F };
/* beamcolors[4] -- palette-index ramp for the 9-LED beam-charge bar
 * (instrument 35), indexed by per-LED fill bucket. Entry 3 is the
 * fully-filled colour; entry 0 is the dim/empty colour. Verified at
 * byte_C61BF in Z_TIE__.EXE (immediately after shieldcolor[11]). */
char beamcolors[4] = { 0x30, 0x2D, 0x31, 0x32 };

/* CMD-CRT occlusion masks used by panel_update3Dcrt. */
#include "tie_formats/cockpit_masks.h"

char panelfilename[32];
char panelname[32];
// GLOBAL: TIE 0xD5518
PanelViewDef panelviewdefs[PANEL_NUM_VIEWS];

static int32_t panel_tie98_sar1(int32_t value);
static void calculate_tie98_pip_subsystem_offset(FlightObject* target, int mesh_index);
PanelViewPtrs panelviewptrs[PANEL_NUM_VIEWS];
/* Parallel array tracking the malloc'd LFD blob behind each preloaded
 * panelviewptrs[] entry. The binary used XMEMHDL_Free_Handle to release
 * the underlying buffer; we need a pointer-width slot to do the same. */
static void* panel_view_bufs[PANEL_NUM_VIEWS];
void* temppanelptr;
int32_t panelsloadedflag;

RadarBlip rightbliplist1[PANEL_NUM_BLIPS];
RadarBlip rightbliplist2[PANEL_NUM_BLIPS];
RadarBlip leftbliplist2[PANEL_NUM_BLIPS];
RadarBlip leftbliplist1[PANEL_NUM_BLIPS];
RadarBlip* oldrightbliplist;
RadarBlip* oldleftbliplist;
RadarBlip* newrightbliplist;
RadarBlip* newleftbliplist;

// GLOBAL: TIE 0xD5B10
HudInstrument instruments[PANEL_NUM_INSTRUMENTS];
// GLOBAL: TIE 0xD5D4A
int16_t oldinstruments[PANEL_NUM_INSTRUMENTS];

int16_t oldbracketx, oldbrackety;
int16_t radary, radarx;
int16_t bracketx, brackety;
int16_t blipboxx, blipboxy;
int16_t oldblipboxx, oldblipboxy;
// GLOBAL: TIE 0xD5E0E
int16_t oldleftlistsize, newrightlistsize;
int16_t blipcolor;
// GLOBAL: TIE 0xD5E22
int16_t oldrightlistsize, newleftlistsize;
// GLOBAL: TIE 0xD5E28
int16_t lasttargetnum;
// GLOBAL: TIE 0xD5E24
int16_t lastpilotpaneldraw;

// GLOBAL: TIE 0xD5E35
uint8_t lockflag;
// GLOBAL: TIE 0xD5E36
uint8_t bracketflag;
// GLOBAL: TIE 0xD5E37
uint8_t blipboxflag;
// GLOBAL: TIE 0xD5E3B
uint8_t blipptrflag;
// GLOBAL: TIE 0xD5E38
uint8_t initpanelflag;
// GLOBAL: TIE 0xD5E39
uint8_t searchpartsflag;
// GLOBAL: TIE 0xD5E3A
uint8_t panelpartsflag;
uint8_t panelmirrorflag;

void* panelpartsptr;

/* String-table pointers (filled by fediskio_loadstringdata). */
char** waypointstrings;
void* diststring;
void* shieldstring;
void* hullstring;
void* sysstring;
void* targetstring;
void* nonestring;
void* ourstring;
void* currentorderstring;
void* notargetstring;
void* curtargetstring;
void* curdeststring;
void* distfromtargetstring;
void* disttodeststring;
void* componentnames;
void* timeremstring;
void* timetotargetstring;
void* timetodeststring;

/* ================================================================== */
/* Dispatchers                                                        */
/* ================================================================== */

/* Copy a NUL-terminated string into a fixed-size buffer, truncating
 * to fit and always NUL-terminating. */
static void str_copy_bounded(char* dst, size_t dst_size, const uint8_t* src) {
	size_t n = 0;
	while (src[n] && n + 1 < dst_size) {
		dst[n] = (char)src[n];
		++n;
	}
	dst[n] = '\0';
}

/* Copy a festring into the snapshot representation consumed by the HD
 * compositor. Plain bytes are unchanged; 0xFE color operands are converted
 * from engine-logical colors to post-remap palette indices. */
static void str_copy_festring_remapped(char* dst, size_t dst_size, const uint8_t* src) {
	if (dst_size == 0)
		return;

	size_t out = 0;
	while (*src && out + 1 < dst_size) {
		uint8_t ch = *src++;
		if (ch == 0xFEu) {
			if (!*src || out + 2 >= dst_size)
				break;
			dst[out++] = (char)ch;
			uint8_t color = *src++;
			dst[out++] = (char)((color >= 0x40u) ? color_remap_table[color] : color);
		} else {
			dst[out++] = (char)ch;
		}
	}
	dst[out] = '\0';
}

/*
 * panel_initpanel -- reset HUD state, force a full redraw on the next
 * panel_updatepanel call.
 */
// FUNCTION: TIE 0x3FA80
void panel_initpanel(void) {
	initpanelflag = 1;

	for (int i = 0; i < 95; ++i)
		oldinstruments[i] = -2;

	replaypercent = -2;
	newleftlistsize = 0;
	newrightlistsize = 0;
	bracketflag = 0;
	blipptrflag = 0;
	blipboxflag = 0;
	lasttargetnum = -1;

	panel_updatecovers();
	panel_updatecockpitdamage();
	panel_updatepanel();

	initpanelflag = 0;
}

/*
 * panel_updatepanel -- top-of-frame HUD refresh. Validates the current
 * target, then dispatches to one of three render paths.
 */
// FUNCTION: TIE 0x3FB04
void panel_updatepanel(void) {
	uint16_t initial_obj = pstate.target_obj_idx;
	int16_t saved_obj = (int16_t)pstate.target_obj_idx;

	if (pstate.target_obj_idx != 0xFFFF) {
		int drop = 0;
		if (pstate.target_obj_idx >= 0x3800u) {
			uint16_t si = pstate.target_obj_idx - 14336;
			if (!staticobjects[si].species || staticobjects[si].ship_class == 13)
				drop = 1;
		} else {
			/* Live target: keep unless dead, exploding, OR a
			 * category-0 craft that's actively departing
			 * (flight_flag 3 = leaving, 4 = gone). The original
			 * inverted-logic branch dropped *every* category!=0
			 * target and *every* non-departing target — exactly
			 * the inverse of retail. */
			if (!objects[pstate.target_obj_idx].ship_idx ||
				objects[pstate.target_obj_idx].genus == GENUS_EXPLOSION) {
				drop = 1;
			} else if (objects[pstate.target_obj_idx].category == 0) {
				uint8_t ff = objects[pstate.target_obj_idx].craft_ptr->flight_flag;
				if (ff == 3 || ff == 4)
					drop = 1;
			}
		}
		if (drop)
			pstate.target_obj_idx = 0xFFFF;

		if ((pstate.player_craft->status_flags & 4) == 0)
			pstate.target_obj_idx = 0xFFFF;

		if (pstate.target_obj_idx == 0xFFFF) {
			pstate.radar_target0 = saved_obj;
			pstate.radar_target2 = 0;
			if (camera.pilotview == 20) {
				if (!replayviewmode) {
					camera.view_zoom_flag = 0;
					camera.view_heading_offset = 0;
					lasttargetnum = -2;
					targetblinkflag = 0;
					camera.view_target_obj = pstate.object_idx;
					user_resetview();
				}
				msg_messageprintf(MSG_TARGET_LOST);
			}
		}
	}

	(void)initial_obj;

	if (pstate.hyperin_state)
		return;

	if (camera.pilotview == 0) {
		panel_updateradar();
		panel_updatelasers();
		panel_updategunsight();
		panel_updatecmd();
		panel_updateweapons();
		panel_updateshields();
		panel_updatebeam();
		panel_updateclock();
		panel_updatespeed();
		panel_updatethrottle();
		panel_updatepower();
		panel_updateweaponwarnings();
		panel_updatereplaystuff();
	} else if (camera.pilotview == 19) {
		panel_updateradar();
		panel_updatelasers();
		panel_updategunsight();
		panel_updateweaponwarnings();
	} else {
		if (camera.pilotview == 20) {
			panel_updatethreatname();
			panel_updatethreatweapons();
		}
		fsfx_triggergunsightsfx(0);
	}
}

/*
 * panel_updateforwardpanel -- forward-only HUD (no cockpit chrome)
 * plus a numeric speed% readout at instrument 24 when the ship supports
 * it (capability bit 0x40).
 */
// FUNCTION: TIE 0x3FCF4
void panel_updateforwardpanel(void) {
	festring_setfontsize(2);
	panel_updateradar();
	panel_updatelasers();
	panel_updategunsight();
	panel_updatecmd();
	panel_updateweapons();
	panel_updateshields();
	panel_updatebeam();
	panel_updateclock();

	if ((pstate.player_craft->working_subsystems & 0x40) != 0) {
		festring_setbackcolor(0x40);
		uint16_t pct = math2_fraction((uint16_t)pstate.player->current_speed, 0x71C7u);
		panel_updatevalue(TIE_HUDI_SPEED_DIGITS, pct, 1);
	}

	panel_updatethrottle();
	panel_updatepower();
	panel_updateweaponwarnings();
	panel_updatereplaystuff();
}

/*
 * panel_updatefullforward -- radar + lasers + gunsight + warnings.
 * In the binary this falls through into panel_updateweaponwarnings;
 * we call it explicitly.
 */
void panel_updatefullforward(void) {
	panel_updateradar();
	panel_updatelasers();
	panel_updategunsight();
	panel_updateweaponwarnings();
}

/*
 * panel_updatethreatdisplay -- threat-view text + weapon icons.
 */
void panel_updatethreatdisplay(void) {
	panel_updatethreatname();
	panel_updatethreatweapons();
}

static uint16_t threat_blink_phase(void);

/*
 * panel_updateweaponwarnings -- three independent warning LEDs.
 *
 * 0x42 (incoming-fire): any active craft with ai_target_ref == player
 *      has an active ion-cannon laser group (laser_type 0x89 or
 *      0x8B) flagged via laser_owner_player[g].
 * 0x43 (lock-on warning): any active craft whose FG status (version)
 *      != 5 has a homing weapon slot (slot.type == 2) targeting the
 *      player.
 * 0x44 (missile-impact countdown): for crafts in maneuver 23 with
 *      ai_target_ref == player, take max(missile_count_total). Steady
 *      on if > 944, 1Hz flash if 0 < x <= 944, off otherwise.
 *
 * Active = ship_idx != 0 AND category == 0 (retail loop guard at
 * 0x42368/0x42375; not the usual single-condition check).
 *
 * The binary has a spurious EAX-mode parameter that is never read; we
 * match that by taking no arguments.
 */
// FUNCTION: TIE 0x3FDA4
void panel_updateweaponwarnings(void) {
	uint16_t warn_incoming = 0;
	uint16_t warn_lock = 0;

	for (uint16_t i = 0; i < NUM_CRAFTS; ++i) {
		if (!objects[i].ship_idx)
			continue;
		if (objects[i].category != 0)
			continue;
		CraftData* cp = objects[i].craft_ptr;

		/* Incoming-fire LED: only crafts whose ai_target_ref points at
		 * the player are considered. */
		if ((uint16_t)cp->ai_target_ref == pstate.object_idx) {
			for (uint8_t g = 0; g < cp->laser_group_cnt; ++g) {
				uint8_t t = cp->laser_type[g];
				if ((t == 0x8B || t == 0x89) && cp->laser_owner_player[g])
					warn_incoming = 1;
			}
		}

		/* Lock-on LED: gated on the FG-status byte at fg_array[].version
		 * != 5 (matches retail's `byte at fg_array[fg_idx]+0x34`). */
		uint8_t fg_status = fg_array[objects[i].fg_idx].version;
		if (fg_status != 5) {
			for (uint8_t g = 0; g < cp->weapon_group_cnt; ++g) {
				if (cp->weapon_slots[g].type == 2 && cp->weapon_slots[g].target_obj == pstate.object_idx)
					warn_lock = 1;
			}
		}
	}

	panel_updatelever(TIE_HUDI_WARN_INCOMING, warn_incoming);
	panel_updatelever(TIE_HUDI_WARN_LOCK, warn_lock);

	/* Impact-countdown: max missile_count_total across crafts in
	 * maneuver 23 targeting the player. */
	int16_t best_count = 0;
	for (uint16_t k = 0; k < NUM_CRAFTS; ++k) {
		if (!objects[k].ship_idx)
			continue;
		if (objects[k].category != 0)
			continue;
		CraftData* cp = objects[k].craft_ptr;
		if ((uint16_t)cp->ai_target_ref != pstate.object_idx)
			continue;
		if (cp->mode_byte != 23)
			continue;
		int16_t mc = (int16_t)cp->missile_count_total;
		if (best_count < mc)
			best_count = mc;
	}

	uint16_t v44_val;
	if (best_count > 944)
		v44_val = 2;
	else if (best_count > 0)
		v44_val = threat_blink_phase();
	else
		v44_val = 0;
	panel_updatelever(TIE_HUDI_WARN_IMPACT, v44_val);
}

/* ================================================================== */
/* Per-widget updaters                                                */
/* ================================================================== */

/*
 * panel_updatelever -- cached shape redraw.
 * farbufferptrs[instruments[idx].param1 + value] picks the frame.
 */
// FUNCTION: TIE 0x433E0
void panel_updatelever(uint16_t idx, uint16_t value) {
	if (value == (uint16_t)oldinstruments[idx])
		return;
	oldinstruments[idx] = (int16_t)value;
	drawshape(FARBUFF(instruments[idx].param1 + value), instruments[idx].x, instruments[idx].y,
			  instruments[idx].param2, 0);
}

/*
 * panel_updatemonolever -- monochrome variant: the shape is fixed, the
 * 'value' becomes the colour argument.
 */
// FUNCTION: TIE 0x4344C
void panel_updatemonolever(uint16_t idx, uint16_t value) {
	if (value == (uint16_t)oldinstruments[idx])
		return;
	oldinstruments[idx] = (int16_t)value;
	rtsvga2_drawmonoshapeVGA((const uint8_t*)FARBUFF(instruments[idx].param1), (int16_t)instruments[idx].x,
							 (int16_t)instruments[idx].y, instruments[idx].param2, (uint8_t)value);
}

/*
 * panel_updatevalue -- numeric HUD field. param1 = digit count,
 * param2 = default text colour. Override colours for critical /
 * warning / grayed-out states.
 */
// FUNCTION: TIE 0x434B0
void panel_updatevalue(uint16_t idx, uint16_t value, uint16_t flags) {
	if (value == (uint16_t)oldinstruments[idx])
		return;

	uint16_t left = instruments[idx].x;
	uint16_t y = instruments[idx].y;
	uint16_t digit_count = instruments[idx].param1;

	int16_t glyph_w = (flightResolution == TIE_FLIGHT_RES_VGA) ? 4 : 8;
	uint16_t bottom = y + fontheight;

	oldinstruments[idx] = (int16_t)value;
	festring_setbound((int16_t)left, (int16_t)y, (int16_t)(left + digit_count * glyph_w + 1),
					  (int16_t)bottom);

	uint16_t col;
	if (value == 0 && (idx == 61 || idx == 58 || idx == 77)) {
		col = 74; /* CRITICAL (red) */
	} else if (value <= 0x32u && (idx == 61 || idx == 58 || idx == 62 || idx == 77 || idx == 78)) {
		col = 78; /* WARNING (amber) */
	} else if (!pstate.player_craft->slam_active && (idx == 25 || idx == 24)) {
		col = 82; /* grayed (afterburner off) */
	} else {
		col = instruments[idx].param2; /* normal */
	}

	festring_settextcolor(col);
	festring_setcursor((int16_t)instruments[idx].x, (int16_t)instruments[idx].y);
	panelrts_outnum((int32_t)value, digit_count, flags);

	TieHudSnapshot_RecordInstrumentDisplay(idx, (int16_t)value, (uint8_t)col, (uint8_t)digit_count);
}

/*
 * panel_updatesetting -- vertical slider.
 * Each rung lit if rung < value; uses farbufferptrs[param1] (unlit) and
 * farbufferptrs[param1+1] (lit).
 */
// FUNCTION: TIE 0x422AC
void panel_updatesetting(uint16_t value, uint16_t idx, uint16_t count, int16_t step) {
	if (value == (uint16_t)oldinstruments[idx])
		return;

	oldinstruments[idx] = (int16_t)value;

	uint16_t y = instruments[idx].y;
	uint16_t x = instruments[idx].x;
	uint16_t shape_base = instruments[idx].param1;

	for (uint16_t rung = 0; rung < count; ++rung) {
		const void* shape = FARBUFF(shape_base + (rung < value ? 1 : 0));
		drawshape(shape, (int)x, (int)y, 253, 0);
		y = (uint16_t)((int16_t)y - step);
	}
}

/*
 * panel_updatecovers -- drop the shield-LED and beam-charge covers
 * when their subsystems are inactive. Each cover is a single cel at
 * `param1`; the engine writes value=0 unconditionally (covers have only
 * the one closed-state graphic). Skip non-view-0 and ship_idx==5
 * (escape pod).
 */
// FUNCTION: TIE 0x42634
void panel_updatecovers(void) {
	if (pstate.player->ship_idx == 5 || camera.pilotview)
		return;

	/* Beam covers — gated on SF_TRACTOR_BEAM (bit 0x100). */
	if ((pstate.player_craft->subsystem_active & 0x100) == 0) {
		panel_updatelever(TIE_HUDI_COVER_BEAM_UP, 0);
		panel_updatelever(TIE_HUDI_COVER_BEAM_DOWN, 0);
	}
	/* Shield cover — gated on SF_SHIELDS (bit 0x01). */
	if ((pstate.player_craft->subsystem_active & 1) == 0)
		panel_updatelever(TIE_HUDI_COVER_SHIELDS, 0);
}

/*
 * panel_updatecockpitdamage -- repaint the 13 subsystem-status icons at
 * instruments 45..57. For every installed subsystem, paint frame 0
 * (intact icon) or frame 13 (broken/cracked icon) based on the runtime
 * working_subsystems bit. NOT called from panel_updatepanel; only fires
 * on view-load (panel_initpanel) and on a subsystem knockout event
 * (collide.c). The icons sit underneath live widget redraws in the
 * framebuffer because panel_initpanel runs first.
 */
// FUNCTION: TIE 0x426A8
void panel_updatecockpitdamage(void) {
	if (camera.pilotview)
		return;

	uint16_t mask = 1;
	uint16_t idx = 45;
	for (int i = 0; i < 13; ++i) {
		uint16_t frame = (mask & pstate.player_craft->working_subsystems) ? 0 : 13;
		if (mask & pstate.player_craft->installed_subsystems)
			panel_updatelever(idx, frame);
		++idx;
		mask <<= 1;
	}
	lasttargetnum = -1;
}

/*
 * panel_updatereplaystuff -- REC LED + %remaining counter at
 * instrument 32.
 */
// FUNCTION: TIE 0x42518
void panel_updatereplaystuff(void) {
	panel_updatelever(TIE_HUDI_REC_LED, (uint16_t)recordingreplay);

	uint16_t x = instruments[TIE_HUDI_REC_PCT].x;
	uint16_t y = instruments[TIE_HUDI_REC_PCT].y;
	festring_setfontsize(2);

	int16_t w = (flightResolution == TIE_FLIGHT_RES_VGA) ? 12 : 18;
	festring_setbound((int16_t)x, (int16_t)y, (int16_t)(x + w), (int16_t)(y + fontheight));
	festring_setcursor((int16_t)x, (int16_t)y);
	festring_setbackcolor(0x40);

	if (recordingreplay) {
		uint16_t elapsed_pct = math2_longpercentage((uint32_t)replaytotalcnt, (uint32_t)replaymaxcnt);
		uint16_t remaining = 100 - math2_fraction(100, elapsed_pct);
		if (remaining > 99)
			remaining = 99;
		if (remaining != (uint16_t)replaypercent) {
			replaypercent = (int16_t)remaining;
			clearwindow();
			festring_settextcolor(0x4E);
			panelrts_outnum((int32_t)remaining, 3, 1);
		}
	} else if (replaypercent != -1) {
		replaypercent = -1;
		clearwindow();
	}
}

/* ================================================================== */
/* Flight-state indicators                                            */
/* ================================================================== */

/*
 * panel_updatespeed -- speed as % of MAX (29127 units ~ 111 MGLT).
 */
// FUNCTION: TIE 0x41F00
void panel_updatespeed(void) {
	if ((pstate.player_craft->working_subsystems & 0x40) == 0)
		return;
	festring_setbackcolor(0x40);
	uint16_t pct = math2_fraction((uint16_t)pstate.player->current_speed, 0x71C7u);
	panel_updatevalue(TIE_HUDI_SPEED_DIGITS, pct, 1);
}

/*
 * panel_updatethrottle -- /655 scale; slam-off mode doubles the
 * internal value so max still registers as 100.
 */
// FUNCTION: TIE 0x41F54
void panel_updatethrottle(void) {
	if ((pstate.player_craft->working_subsystems & 0x40) == 0)
		return;
	festring_setbackcolor(0x40);
	uint16_t raw = (uint16_t)(pstate.player_craft->throttle_speed / 655u);
	if (!pstate.player_craft->slam_active)
		raw *= 2;
	panel_updatevalue(TIE_HUDI_THROTTLE_DIGITS, raw, 1);
}

/*
 * panel_updateclock -- MM:SS display at instrument 30.
 * Training / combat = mtimer (countdown); else = mission elapsed
 * `_date.minute` / `_date.second`, ticked by tie_updatetime.
 */
// FUNCTION: TIE 0x41FBC
void panel_updateclock(void) {
	uint8_t min_v, sec_v;

	if (mission.train_craft_type) {
		min_v = mtimer_min;
		sec_v = mtimer_sec;
	} else {
		min_v = _date.minute;
		sec_v = _date.second;
	}

	int16_t total_secs = (int16_t)(60 * min_v + sec_v);
	if (total_secs == oldinstruments[TIE_HUDI_CLOCK_DIGITS])
		return;
	oldinstruments[TIE_HUDI_CLOCK_DIGITS] = total_secs;

	festring_setfontsize(2);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(0x40);
	/* VGA uses palette index 77 (0x4D) for the clock digits; SVGA's 8-bit
	 * paletted mode shifts everything by 1 and uses 78 (0x4E). */
	const uint8_t clock_color = (flightResolution == TIE_FLIGHT_RES_VGA) ? 0x4D : 0x4E;
	festring_settextcolor(clock_color);
	dropflag = 0;

	festring_setcursor((int16_t)instruments[TIE_HUDI_CLOCK_DIGITS].x,
					   (int16_t)instruments[TIE_HUDI_CLOCK_DIGITS].y);
	panelrts_outnum((int32_t)min_v, 2, 1);

	/* SVGA needs a 1-pixel x-bump after the colon glyph; VGA's narrower
	 * font already lands the SS digits flush with the colon. */
	int16_t glyph_w = sys2_calclength((uint8_t*)separator_colon);
	int16_t x_bump = (flightResolution == TIE_FLIGHT_RES_VGA) ? 0 : 1;
	festring_setcursor((int16_t)(instruments[TIE_HUDI_CLOCK_DIGITS].x + glyph_w + x_bump),
					   (int16_t)instruments[TIE_HUDI_CLOCK_DIGITS].y);
	panelrts_outnum((int32_t)sec_v, 2, 2);
}

/*
 * panel_updatepower -- 4 sliders (lasers, shields, beam, balance).
 */
// FUNCTION: TIE 0x42114
void panel_updatepower(void) {
	int16_t step = (flightResolution == TIE_FLIGHT_RES_VGA) ? 2 : 6;

	if (pstate.player_craft->working_subsystems & 0x200)
		panel_updatesetting((uint16_t)(3 * pstate.player_craft->laser_power), TIE_HUDI_POWER_LASERS, 12,
							step);

	if ((pstate.player_craft->working_subsystems & 0x800) && (pstate.player_craft->subsystem_active & 1))
		panel_updatesetting((uint16_t)(3 * pstate.player_craft->shield_power), TIE_HUDI_POWER_SHIELDS, 12,
							step);

	if ((pstate.player_craft->working_subsystems & 0x1000) && (pstate.player_craft->subsystem_active & 0x100))
		panel_updatesetting((uint16_t)(3 * pstate.player_craft->beam_power), TIE_HUDI_POWER_BEAM, 12, step);

	if (pstate.player_craft->working_subsystems & 0x400) {
		uint16_t v = (uint16_t)(2 - pstate.player_craft->laser_power + 6);
		if (pstate.player_craft->subsystem_active & 1)
			v += (uint16_t)(2 - pstate.player_craft->shield_power);
		if (pstate.player_craft->subsystem_active & 0x100)
			v += (uint16_t)(2 - pstate.player_craft->beam_power);
		panel_updatesetting(v, TIE_HUDI_POWER_BALANCE, 12, step);
	}
}

/* ================================================================== */
/* Weapons                                                            */
/* ================================================================== */

/*
 * panel_updatelasers -- per-weapon-group charge bars + fire levers.
 *
 * Draws 10 LEDs per group (row at instruments[group+3]) and a lever at
 * instrument 0x25+group. LED count cached in _oldinstruments[3+group].
 *
 * Lever values (retail PANEL_updatelever a3 argument):
 *   0 = off (uncharged, no laser power, missile mode, or non-active bank)
 *   1 = charging (active bank, group not the firing one)
 *   2 = in-range lock pending (firing group + bank cooldown active)
 *   3 = active fire group (firing this frame)
 *
 * Sets `lockflag` whenever any active-bank firing group's lookahead
 * line predicts an intersection with the current target -- consumed
 * by panel_updategunsight to flash the reticle.
 */
// FUNCTION: TIE 0x41418
void panel_updatelasers(void) {
	lockflag = 0;

	uint16_t species_idx = pstate.player_craft->species_idx;
	uint16_t weapon_groups = pstate.player_craft->weapon_group_cnt;
	if (!weapon_groups)
		return;

	uint16_t idx = 37;

	for (uint16_t g = 0; g < weapon_groups; ++g) {
		/* Per-group row coords. */
		uint16_t row_idx = g + 3;
		uint16_t x0 = instruments[row_idx].x;
		uint16_t y0 = instruments[row_idx].y;
		if (!(x0 + y0)) {
			++idx;
			continue;
		}

		uint16_t shape_base = instruments[row_idx].param1;
		int16_t charge = (int8_t)pstate.player_craft->weapon_slots[g].charge;
		int16_t led_count_new = 0;
		int16_t empty_frame = 0, filled_frame = 0;

		if (!camera.pilotview && (pstate.player_craft->working_subsystems & 2) &&
			(pstate.player_craft->working_subsystems & 4)) {
			if (charge > 0 && (pstate.player_craft->status_flags & 0x10)) {
				int16_t scaled = charge + 1;
				if (scaled > 64) {
					empty_frame = 1;
					filled_frame = 2;
					scaled -= 64;
				} else {
					empty_frame = 0;
					filled_frame = 1;
				}
				led_count_new = scaled / 6;
				if (led_count_new > 10)
					led_count_new = 10;
			}

			if (led_count_new != oldinstruments[3 + g]) {
				oldinstruments[3 + g] = led_count_new;
				int16_t step = (flightResolution == TIE_FLIGHT_RES_VGA) ? 3 : 6;
				uint8_t flip = 0;
				if (instruments[row_idx].param2) {
					step = (int16_t)-step;
					flip = 1;
				}
				int16_t led_x = (int16_t)x0;
				for (int16_t l = 0; l < 10; ++l) {
					int16_t shape_frame = (l >= led_count_new) ? empty_frame : filled_frame;
					drawshape(FARBUFF(shape_base + shape_frame), led_x, (int16_t)y0, 253, flip);
					led_x = (int16_t)(led_x + step);
				}

				/* `value` = lit-LED count (0..10); `color` overloads as
				 * the filled-frame index (1 normal-charge, 2 overcharge,
				 * 0 = no charge). HD reads both and reproduces the same
				 * 10-cel paint without re-deriving from `charge`. */
				TieHudSnapshot_RecordLaserCharge(row_idx, led_count_new, (uint8_t)filled_frame);
			}
		}

		/* The active weapon bank gates the lever; uncharged, missile-mode,
		 * and wrong-bank groups show the off state. */
		uint8_t bank = (g > spec_data[species_idx].laser_end[0]) ? 1u : 0u;
		int16_t status_val;
		if (charge <= 0 || !(pstate.player_craft->status_flags & 0x10)) {
			status_val = 0;
		} else if (pstate.player_weapon_mode || pstate.player_weapon_group != bank) {
			status_val = 0;
		} else {
			uint8_t wep_kind = pstate.player_craft->laser_owner_player[bank];
			if (wep_kind < 2) {
				status_val = 1;
				if (wep_kind == 1 && pstate.player_craft->laser_first_slot[bank] == g)
					status_val = 3;
			} else if (wep_kind == 2) {
				int16_t link = pstate.player_craft->laser_first_slot[bank];
				if (link == (int16_t)g)
					status_val = 3;
				else if (weapon_groups >= 4 && link + 2 == (int16_t)g)
					status_val = 3;
				else
					status_val = 1;
			} else if (wep_kind == 3) {
				status_val = 3;
			} else {
				status_val = 1;
			}

			/* Cooldown gating: bank-indexed (laser_cooldown[bank]).
			 * Retail downgrades a3 from 3 to 2 ("lock pending"), so
			 * neither the lever paint nor the in-range gate below
			 * trigger this frame. */
			if (status_val == 3 && pstate.player_craft->laser_cooldown[bank])
				status_val = 2;
		}

		panel_updatelever(idx, (uint16_t)status_val);

		/* In-range green-light: probe each active-bank group with its own
		 * hardpoint (hp[g]), not just hp[bank]. Retail loops the call
		 * across every group whose bank matches the active selection. */
		if ((pstate.player_craft->status_flags & 4) && status_val == 3 && pstate.target_obj_idx != 0xFFFF &&
			collide_targetinrange(pstate.object_idx, pstate.target_obj_idx, (uint8_t)g)) {
			lockflag = 1;
		}

		++idx;
	}
}

/*
 * panel_updateweapons -- draw the 4 missile-hardpoint icons.
 */
// FUNCTION: TIE 0x41840
void panel_updateweapons(void) {
	if ((pstate.player_craft->working_subsystems & 8) == 0)
		return;
	if (spec_data[pstate.player_spec_num].missile_count[0] +
			spec_data[pstate.player_spec_num].missile_count[1] ==
		0)
		return;

	panel_updatehardpoint(spec_data[pstate.player_spec_num].missile_start[0], 0, 0);
	panel_updatehardpoint(spec_data[pstate.player_spec_num].missile_end[0], 1, 0);

	/* B-wing-class: also draw the second bank. */
	if (pstate.player_spec_num == spec_getspecnum(12)) {
		panel_updatehardpoint(spec_data[pstate.player_spec_num].missile_start[1], 2, 1);
		panel_updatehardpoint(spec_data[pstate.player_spec_num].missile_end[1], 3, 1);
	}
}

/*
 * panel_updatehardpoint -- single missile-slot indicator: ammo count
 * (text) + ready lever.
 */
// FUNCTION: TIE 0x4194C
void panel_updatehardpoint(uint16_t slot, uint16_t hp_idx, uint16_t flags) {
	uint16_t ammo = 0;
	if (pstate.player_craft->missile_group_cnt)
		ammo = pstate.player_craft->weapon_slots[slot].ammo;

	if (ammo != (uint16_t)oldinstruments[15 + hp_idx]) {
		oldinstruments[15 + hp_idx] = (int16_t)ammo;
		festring_setfontsize(2);
		festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
		festring_setcursor((int16_t)instruments[15 + hp_idx].x, (int16_t)instruments[15 + hp_idx].y);
		festring_setbackcolor(0);
		festring_settextcolor(0x4A);
		dropflag = 0;

		uint16_t digits = (pstate.player_spec_num == spec_getspecnum(12)) ? 2 : 1;
		panelrts_outnum((int32_t)ammo, digits, 1);

		TieHudSnapshot_RecordInstrumentDisplay((uint16_t)(15 + hp_idx), (int16_t)ammo, 0x4A, (uint8_t)digits);
	}

	uint16_t lever_val;
	if (ammo && (pstate.player_craft->status_flags & 8)) {
		if (pstate.player_weapon_mode && pstate.player_weapon_group == flags) {
			uint8_t armed = pstate.player_craft->missile_armed[pstate.player_weapon_group];
			if ((armed & 0x7F) == 3) {
				lever_val = 2;
			} else if ((armed >> 7) == (hp_idx & 1)) {
				lever_val = 2;
			} else {
				lever_val = 1;
			}
		} else {
			lever_val = 1;
		}
	} else {
		lever_val = 0;
	}

	panel_updatelever((uint16_t)(hp_idx + 11), lever_val);
}

/*
 * panel_updateshields -- forward + rear shield bars + balance lever.
 *
 * Each bar has a 10-LED "normal" row (idx 0x13/0x15) and a 10-LED
 * "overcharge" row (idx 0x14/0x16). Damage flash override via
 * timers[TIMER_SHIELD_FLASH] (auto-decremented by TIE_updatetime).
 */

static void panel_shield_onehalf(int16_t shield_hp, uint16_t normal_idx, uint16_t over_idx,
								 int16_t flash_match) {
	int16_t hp = (shield_hp < 0) ? 0 : shield_hp;
	if ((pstate.player_craft->status_flags & 1) == 0)
		hp = 0;

	int16_t sp = spec_data[pstate.player_spec_num].shield_points;
	if (!mission.difficulty)
		sp *= 2;

	uint16_t lo_leds = 0, hi_leds = 0;
	if (hp < sp) {
		uint16_t pct = math2_percentage((uint16_t)hp, (uint16_t)sp);
		hi_leds = 0;
		lo_leds = math2_fraction(9, pct);
	} else {
		uint16_t over = math2_percentage((uint16_t)(hp - sp), (uint16_t)sp);
		lo_leds = 9;
		hi_leds = math2_fraction(9, over);
	}

	/* Damage flash. */
	if (timers[TIMER_SHIELD_FLASH] && shieldblink == (uint8_t)flash_match) {
		if (hi_leds)
			hi_leds = 10;
		else
			lo_leds = 10;
	}

	panel_updatemonolever(normal_idx, (uint16_t)(uint8_t)shieldcolor[lo_leds]);
	panel_updatemonolever(over_idx, (uint16_t)(uint8_t)shieldcolor[hi_leds]);
}

// FUNCTION: TIE 0x41ADC
void panel_updateshields(void) {
	if ((pstate.player_craft->working_subsystems & 0x20) == 0)
		return;

	panel_shield_onehalf(pstate.player_craft->forward_shield, TIE_HUDI_SHIELD_FWD_NORMAL,
						 TIE_HUDI_SHIELD_FWD_OVER, 0);
	panel_shield_onehalf(pstate.player_craft->rear_shield, TIE_HUDI_SHIELD_REAR_NORMAL,
						 TIE_HUDI_SHIELD_REAR_OVER, 1);

	uint16_t balance_val;
	if (timers[TIMER_SHIELD_OVERLOAD]) {
		balance_val = 3;
	} else {
		uint16_t third = (uint16_t)(pstate.player_craft->hull_max / 3);
		if (third == 0) {
			panel_updatelever(TIE_HUDI_HULL_DAMAGE_LEVER, 2);
			return;
		}
		balance_val = (uint16_t)(2 - pstate.player_craft->hull_damage / third);
	}
	panel_updatelever(TIE_HUDI_HULL_DAMAGE_LEVER, balance_val);
}

/*
 * panel_updatebeam -- 9-LED beam charge bar (drawn RTL) + fire lever.
 */
// FUNCTION: TIE 0x41D7C
void panel_updatebeam(void) {
	if ((pstate.player_craft->working_subsystems & 0x10) == 0)
		return;

	int16_t beam_charge = pstate.player_craft->beam_charge;
	if (beam_charge < 0)
		beam_charge = 0;
	if ((pstate.player_craft->status_flags & 0x100) == 0)
		beam_charge = 0;

	uint8_t fire = ((pstate.player_craft->beam_state & 0x80) != 0) ? 1 : 0;
	if ((pstate.player_craft->status_flags & 0x100) == 0)
		fire = 0;
	panel_updatelever(TIE_HUDI_BEAM_FIRE, fire);

	if ((uint16_t)oldinstruments[TIE_HUDI_BEAM_ARC] == (uint16_t)beam_charge)
		return;
	oldinstruments[TIE_HUDI_BEAM_ARC] = beam_charge;
	TieHudState* hud = TieSnapshotBuilder_HudMut();

	int16_t step_rev = 8;
	for (int i = 0; i < 9; ++i) {
		uint8_t led_color;
		if (beam_charge <= 1000 * (i + 1)) {
			int16_t excess = (int16_t)(beam_charge - 1000 * i);
			if (excess < 0) {
				led_color = (uint8_t)beamcolors[0];
			} else {
				if (excess > 1000)
					excess = 1000;
				led_color = (uint8_t)beamcolors[excess / 333];
			}
		} else {
			led_color = (uint8_t)beamcolors[3]; /* fully filled */
		}

		hud->beam_arc_led_colors[i] = led_color;

		int16_t led_x, led_y;
		if (tie_is_high_resolution_flight()) {
			led_x = (int16_t)(3 * step_rev + instruments[TIE_HUDI_BEAM_ARC].x);
			led_y = (int16_t)(3 * step_rev + instruments[TIE_HUDI_BEAM_ARC].y);
		} else {
			led_y = (int16_t)(step_rev + instruments[TIE_HUDI_BEAM_ARC].y);
			led_x = (int16_t)(2 * step_rev + instruments[TIE_HUDI_BEAM_ARC].x);
		}

		--step_rev;

		rtsvga2_drawmonoshapeVGA((const uint8_t*)FARBUFF(i + instruments[TIE_HUDI_BEAM_ARC].param1), led_x,
								 led_y, instruments[TIE_HUDI_BEAM_ARC].param2, led_color);
	}
}

/* ================================================================== */
/* Targeting / CMD                                                    */
/* ================================================================== */

/*
 * panel_updategunsight -- reticle state at instrument 0x24.
 *
 * Missile-mode is the only mode that drives the reticle; laser-mode
 * only leaves a trailing 'just-switched-away' flash via lockflag.
 *
 *   missile mode && !target  -> state 1 (armed, no spec)
 *   missile mode && target   -> state 2/3 (radar_subtarget_state + 1
 *                               = lock phase: 2=acquiring, 3=solid)
 *   laser mode   && lockflag -> state 4 (just-lost / red flash)
 *   otherwise                -> state 0 (off)
 *
 * lockflag mirrors the solid-lock condition (radar_subtarget_state==2)
 * so other drawers (laser fire, updatelasers) can flash red.
 */
// FUNCTION: TIE 0x413A0
void panel_updategunsight(void) {
	int16_t st;
	if (pstate.player_weapon_mode) {
		st = (pstate.target_obj_idx == 0xFFFF) ? 1 : (pstate.radar_subtarget_state + 1);
		lockflag = (pstate.radar_subtarget_state == 2) ? 1 : 0;
	} else if (lockflag) {
		st = 4;
	} else {
		st = 0;
	}
	fsfx_triggergunsightsfx(st);
	panel_updatelever(TIE_HUDI_GUNSIGHT, (uint16_t)st);
}

/*
 * panel_updateradar -- diff-draw radar blips + target bracket.
 */
// FUNCTION: TIE 0x3FE50
void panel_updateradar(void) {
	if (!(pstate.player_craft->working_subsystems & 0x80) ||
		!(pstate.player_craft->working_subsystems & 0x100))
		return;

	oldleftlistsize = newleftlistsize;
	oldrightlistsize = newrightlistsize;
	oldbracketx = bracketx;
	newleftlistsize = 0;
	oldbrackety = brackety;
	newrightlistsize = 0;

	if (blipptrflag) {
		oldleftbliplist = leftbliplist1;
		newleftbliplist = leftbliplist2;
		oldrightbliplist = rightbliplist1;
		newrightbliplist = rightbliplist2;
	} else {
		oldleftbliplist = leftbliplist2;
		newleftbliplist = leftbliplist1;
		oldrightbliplist = rightbliplist2;
		newrightbliplist = rightbliplist1;
	}

	/* Dynamic craft [0..NUM_CRAFTS) except self. */
	for (uint16_t i = 0; i < NUM_CRAFTS; ++i) {
		if (i == pstate.object_idx)
			continue;
		if (!(species_table[objects[i].ship_idx].side & 1))
			continue;
		if (objects[i].craft_ptr->flight_flag == 3)
			continue;
		panel_addbliptoradar(i);
	}

	/* Warheads [NUM_CRAFTS..WARHEAD_SLOT_END). */
	for (uint16_t j = NUM_CRAFTS; j < WARHEAD_SLOT_END; ++j) {
		if (!(species_table[objects[j].ship_idx].side & 1))
			continue;
		panel_addbliptoradar(j);
	}

	/* Static objects (mapped to 0x3800..0x383F). */
	for (uint16_t k = 0; k < 0x40u; ++k) {
		if (!(species_table[staticobjects[k].species].side & 1))
			continue;
		panel_addbliptoradar((uint16_t)(14336 + k));
	}

	if (bracketflag)
		rtsvga2_removebracket();

	if (oldleftlistsize)
		rtsvga2_removeblipsVGA(oldleftbliplist, (uint16_t)oldleftlistsize);
	if (newleftlistsize)
		rtsvga2_drawblipsVGA(newleftbliplist, (uint16_t)newleftlistsize);
	if (oldrightlistsize)
		rtsvga2_removeblipsVGA(oldrightbliplist, (uint16_t)oldrightlistsize);
	if (newrightlistsize)
		rtsvga2_drawblipsVGA(newrightbliplist, (uint16_t)newrightlistsize);

	blipboxflag = 0;
	if (pstate.target_obj_idx == 0xFFFF) {
		bracketflag = 0;
	} else {
		rtsvga2_drawbracket();
		bracketflag = 1;
	}
	blipptrflag ^= 1u;
}

/*
 * panel_addbliptoradar -- project one target into the radar display.
 *
 * TIE95 uses cached eye coordinates for craft and downscaled world coordinates
 * for other objects. TIE98 rotates every target's current full world position.
 */
// FUNCTION: TIE 0x400AC
// FUNCTION: TIE98 0x4637D0
void panel_addbliptoradar(uint16_t target_obj) {
	int32_t eye_x, eye_y_neg, eye_z;

	if (TieProfile_UsesTie98Logic()) {
		int32_t dx_world, dy_world, dz_world;
		FlightObject* pl = pstate.player;
		if (target_obj < 0x3800u) {
			dx_world = objects[target_obj].world_x - pl->world_x;
			dy_world = objects[target_obj].world_y - pl->world_y;
			dz_world = objects[target_obj].world_z - pl->world_z;
		} else {
			uint16_t si = target_obj - 14336;
			dx_world = (int32_t)staticobjects[si].world_x * 256 - pl->world_x;
			dy_world = (int32_t)staticobjects[si].world_y * 256 - pl->world_y;
			dz_world = (int32_t)staticobjects[si].world_z * 256 - pl->world_z;
		}

		if (pl->orient_dirty) {
			fview_calcrotatemove(pl->heading, pl->pitch, pl);
			fview_calcrotateorient(pl->roll, 0, pl);
		}

		eye_z = (int32_t)(((int64_t)pl->fwd_x * dx_world) >> 15) +
				(int32_t)(((int64_t)pl->fwd_y * dy_world) >> 15) +
				(int32_t)(((int64_t)pl->fwd_z * dz_world) >> 15);
		eye_x = (int32_t)(((int64_t)pl->side_x * dx_world) >> 15) +
				(int32_t)(((int64_t)pl->side_y * dy_world) >> 15) +
				(int32_t)(((int64_t)pl->side_z * dz_world) >> 15);
		eye_y_neg = -((int32_t)(((int64_t)pl->up_x * dx_world) >> 15) +
					  (int32_t)(((int64_t)pl->up_y * dy_world) >> 15) +
					  (int32_t)(((int64_t)pl->up_z * dz_world) >> 15));
	} else if (target_obj >= NUM_CRAFTS) {
		int32_t dx_world, dy_world, dz_world;
		FlightObject* pl = pstate.player;
		if (target_obj < 0x3800u) {
			dx_world = (objects[target_obj].world_x - pl->world_x) >> 8;
			dy_world = (objects[target_obj].world_y - pl->world_y) >> 8;
			dz_world = (objects[target_obj].world_z - pl->world_z) >> 8;
		} else {
			uint16_t si = target_obj - 14336;
			dx_world = (int32_t)staticobjects[si].world_x - (pl->world_x >> 8);
			dy_world = (int32_t)staticobjects[si].world_y - (pl->world_y >> 8);
			dz_world = (int32_t)staticobjects[si].world_z - (pl->world_z >> 8);
		}

		if (pl->orient_dirty) {
			fview_calcrotatemove(pl->heading, pl->pitch, pl);
			fview_calcrotateorient(pl->roll, 0, pl);
		}

		int16_t dx = (int16_t)dx_world;
		int16_t dy = (int16_t)dy_world;
		int16_t dz = (int16_t)dz_world;

		/* Rotate by player orientation: dot product with (fwd/side/up). */
		eye_z = ((int32_t)pl->fwd_z * dz + (int32_t)pl->fwd_y * dy + (int32_t)pl->fwd_x * dx) >> 15;
		eye_x = ((int32_t)pl->side_z * dz + (int32_t)pl->side_y * dy + (int32_t)pl->side_x * dx) >> 15;
		eye_y_neg = -(((int32_t)pl->up_z * dz + (int32_t)pl->up_x * dx + (int32_t)pl->up_y * dy) >> 15);
	} else {
		CraftData* cp = objects[target_obj].craft_ptr;
		/* Eye-space (camera-space) position cached every frame by
		 * tie_getobjecteyexyz via tie_updatescreen. */
		eye_x = cp->eye_x_cache;
		eye_y_neg = cp->eye_y_cache;
		eye_z = cp->eye_z_cache;
	}

	int is_forward = (eye_z >= 0);
	if (!is_forward)
		eye_z = -eye_z;

	/* Pick base colour. */
	if (target_obj >= 0x3800u) {
		blipcolor = 47;
	} else if (objects[target_obj].genus == 9) {
		/* genus 9 isn't in the documented list (species.c says
		 * 8/9/10 are unused) but the binary checks for it
		 * defensively -- treat as neutral/static color. */
		blipcolor = 47;
	} else if (objects[target_obj].category == 1) {
		blipcolor = 59;
	} else {
		uint8_t side = objects[target_obj].side;
		if (side == 0)
			blipcolor = 63;
		else if (side == 1 || side == 4)
			blipcolor = 55;
		else if (side == 2)
			blipcolor = 51;
		else
			blipcolor = 209;
	}

	/* Distance fade. */
	pai_roughdistancebetween(pstate.object_idx, target_obj);
	int32_t rough = roughdistance;
	if (rough > 122166) {
		if (blipcolor == 47)
			blipcolor = 45;
		else if (blipcolor == 209)
			blipcolor = 211;
		else
			blipcolor -= 2;
	} else if (rough > 61083) {
		if (blipcolor == 47)
			blipcolor = 46;
		else if (blipcolor == 209)
			blipcolor = 210;
		else
			blipcolor -= 1;
	}

	math2_getradarcoord(eye_x, eye_y_neg, eye_z);

	/* radarx/radary at this point = signed classic-px offset from the
	 * radar disc center (math2 clipped to the disc boundary). Capture
	 * the pre-anchor values for the HD snapshot's anchor-relative
	 * blip / bracket fields; the engine path below adds the disc
	 * anchor in-place to produce the absolute classic coords it
	 * draws to the FB. */
	const int16_t blip_off_x = radarx;
	const int16_t blip_off_y = radary;

	if (is_forward) {
		radary = (int16_t)(radary + instruments[TIE_HUDI_RADAR_LEFT].y);
		radarx = (int16_t)(radarx + instruments[TIE_HUDI_RADAR_LEFT].x);
		RadarBlip* entry = &newleftbliplist[newleftlistsize];
		entry->x = (uint16_t)radarx;
		entry->y = (uint16_t)radary;
		entry->color = (uint16_t)blipcolor;
		TieHudSnapshot_RecordRadarBlip(true, newleftlistsize, (uint8_t)blipcolor, blip_off_x, blip_off_y);
		if (newleftlistsize + 1 == 48)
			; /* cap: keep newleftlistsize (don't advance) */
		else
			newleftlistsize = (int16_t)(newleftlistsize + 1);
	} else {
		int16_t right_y = (int16_t)(instruments[TIE_HUDI_RADAR_RIGHT].y + radary);
		radarx = (int16_t)(radarx + instruments[TIE_HUDI_RADAR_RIGHT].x);
		RadarBlip* entry = &newrightbliplist[newrightlistsize];
		entry->x = (uint16_t)radarx;
		entry->y = (uint16_t)right_y;
		entry->color = (uint16_t)blipcolor;
		TieHudSnapshot_RecordRadarBlip(false, newrightlistsize, (uint8_t)blipcolor, blip_off_x, blip_off_y);
		if (newrightlistsize + 1 == 48)
			newrightlistsize = 47;
		else
			newrightlistsize = (int16_t)(newrightlistsize + 1);
		radary = right_y;
	}

	if (target_obj == pstate.target_obj_idx) {
		bracketx = radarx;
		brackety = radary;
		TieHudSnapshot_RecordRadarBracket(is_forward, blip_off_x, blip_off_y);
	}
}

/*
 * panel_buildobjectname -- format target name into tempstring using
 * FESTRING color-escape prefixes (0xFE). flags bit 0 = ship name,
 * bit 1 = FG name + group number suffix.
 */
static char pick_color_primary(uint8_t side) {
	if (side == 0)
		return 0x51; /* Q neutral */
	if (side == 1 || side == 4)
		return 0x49; /* I ally    */
	if (side == 2)
		return 0x45; /* E enemy   */
	return 0x55;     /* U unknown */
}
static char pick_color_secondary(uint8_t side) {
	if (side == 0)
		return 0x52; /* R */
	if (side == 1 || side == 4)
		return 0x4A; /* J */
	if (side == 2)
		return 0x46; /* F */
	return 0x56;     /* V */
}

// FUNCTION: TIE 0x40E94
void panel_buildobjectname(uint16_t target_obj, uint8_t flags) {
	tempstring[0] = 0;

	if (target_obj >= 0x3800u) {
		if ((int16_t)target_obj < 0) { /* waypoint: ref with high bit set */
			if ((flags & 1) == 0)
				return;
			uint16_t wp = target_obj + 0x8000u; /* clear msb */
			festring_farstradd((char)0xFE);
			festring_farstradd((char)0x43); /* 'C' */
			if (waypointstrings && waypointstrings[wp])
				festring_farstrcat(waypointstrings[wp]);
			return;
		}

		uint16_t si = target_obj - 14336;
		uint16_t species_id = staticobjects[si].species;

		festring_farstradd((char)0xFE);
		uint8_t fg_side = fg_array[staticobjects[si].fg_idx].side;
		festring_farstradd(pick_color_primary(fg_side));

		if ((flags & 1) && species_id >= 0x46u && species_id <= 0x55u) {
			festring_farstrcat(((char**)buoystr)[species_id - 70]);
		}

		if ((flags & 3) == 3) {
			festring_farstradd(':');
			festring_farstradd(' ');
		}

		if (flags & 2) {
			festring_farstradd((char)0xFE);
			festring_farstradd(pick_color_secondary(fg_side));
			festring_farstrcat(fg_array[staticobjects[si].fg_idx].name);
		}
		return;
	}

	/* Dynamic-object branch. */
	uint8_t ship_idx = objects[target_obj].ship_idx;

	festring_farstradd((char)0xFE);
	festring_farstradd(pick_color_primary(objects[target_obj].side));

	if (!objects[target_obj].category) {
		CraftData* cp = objects[target_obj].craft_ptr;
		if (flags & 1)
			festring_farstrcat(spec_data[cp->species_idx].short_name);

		if ((flags & 3) == 3) {
			festring_farstradd(':');
			festring_farstradd(' ');
		}

		if (flags & 2) {
			festring_farstradd((char)0xFE);
			festring_farstradd(pick_color_secondary(objects[target_obj].side));
			festring_farstrcat(fg_array[objects[target_obj].fg_idx].name);

			/* Multi-craft FG: append 1-based index.
			 * Binary reads `*(int *)&special_craft >> 24` -- that dword
			 * starts at EFGStruct+0x30 and its top byte is EFGStruct.count
			 * (at +0x33). So the test is "FG has more than one craft". */
			if (fg_array[objects[target_obj].fg_idx].count > 1) {
				festring_farstradd(' ');
				festring_farstradd((char)(cp->craft_idx_in_fg + '1'));
			}
		}
		return;
	}

	if ((flags & 1) == 0)
		return;

	if (ship_idx >= 0x8Fu && ship_idx <= 0x9Au) {
		festring_farstrcat(((char**)warheadstrings)[ship_idx - 0x8F]);
		return;
	}
	if (ship_idx >= 0x46u && ship_idx <= 0x54u) {
		festring_farstrcat(((char**)buoystr)[ship_idx - 70]);
	}
}

/*
 * panel_getcraftstatus -- status code for the target-CRT color.
 */
// FUNCTION: TIE 0x41288
uint16_t panel_getcraftstatus(uint16_t target_obj) {
	CraftData* cp = objects[target_obj].craft_ptr;
	if (!cp->status_flags)
		return 2;
	if (cp->dock_state_flags)
		return 3; /* docking / boarding */
	if (cp->hull_damage >= cp->hull_strength)
		return 7;
	/* Shield generator present but both shield banks drained to zero
	 * (has_shields species with empty forward+rear). Status 6 = CRT
	 * color for "shields destroyed / capturable". */
	if (spec_data[cp->species_idx].has_shields && (cp->forward_shield + cp->rear_shield) == 0) {
		return 6;
	}
	uint8_t order = cp->current_order;
	if (order == 44 || order == 64)
		return 8;
	return 0;
}

/*
 * panel_outputdistance -- polar_dist (Q? fixed-point) -> km.cm at
 * instruments 0x3B / 0x3C. Clamped to <= 9999.99 km.
 */
// FUNCTION: TIE 0x41334
void panel_outputdistance(int32_t polar_dist) {
	uint32_t scaled = (uint32_t)(161 * polar_dist) >> 16;
	if (scaled >= 0x2710u)
		scaled = 9999;
	panel_updatevalue(0x3B, (uint16_t)(scaled / 100u), 1);
	panel_updatevalue(0x3C, (uint16_t)(scaled % 100u), 2);
}

/* ================================================================== */
/* Threat-view (pilotview 20)                                         */
/* ================================================================== */

/*
 * panel_updatethreatweapons -- shield/hull pct + 4 ability levers.
 *
 * Levers at 0x49..0x4C convey weapons status of the TARGET (not the
 * player). Blink cadence uses the mission time counter's low bits.
 */
static uint16_t threat_blink_phase(void) {
	/* Binary reads `(dword_E6388 >> 16) / 59 & 1` — the upper word of the
	 * wall-clock cluster is `_date.subsec`. The quotient /59 toggles four
	 * times per mission-second, giving the threat-flash its ~250 ms cadence. */
	return (uint16_t)(((uint16_t)_date.subsec / 59u) & 1u);
}

// FUNCTION: TIE 0x3FDC0
void panel_updatethreatweapons(void) {
	uint16_t shield_pct = 0;

	if (pstate.target_obj_idx < NUM_CRAFTS) {
		CraftData* cp = objects[pstate.target_obj_idx].craft_ptr;
		uint16_t shield_avg = (uint16_t)(cp->rear_shield + cp->forward_shield) >> 1;
		uint16_t sp = (uint16_t)(2 * spec_data[cp->species_idx].shield_points);
		if (!mission.difficulty) {
			uint8_t side = objects[pstate.target_obj_idx].side;
			if (side == 1) {
				/* Enemy side 1 on easy: 3 * shield_points.
				 * Binary computes `(dword>>17) + (dword>>16)` where the
				 * dword starts at spec.field_10 and its HIWORD is
				 * shield_points (at spec+0x12). So the expression is
				 * `(shield_points/2) + shield_points` = 1.5*sp, then *2
				 * at the outer level => 3*shield_points. */
				int shield_pts = spec_data[cp->species_idx].shield_points;
				sp = (uint16_t)(3 * shield_pts);
			} else if (side == 0 || side == 4) {
				/* Easy difficulty for hostile/neutral side: 1.25x shield_points.
				 * 2 * MATH2_fraction(shield_points, 0xA000) = 2 * 0.625 * sp. */
				int16_t adj = math2_fraction((uint16_t)spec_data[cp->species_idx].shield_points, 0xA000u);
				sp = (uint16_t)(2 * adj);
			}
		}
		if (sp)
			shield_pct = (uint16_t)(2 * (math2_percentage(shield_avg, sp) / 0x28Fu));
	}
	panel_updatevalue(TIE_HUDI_THREAT_SHIELD_PCT, shield_pct, 1);

	uint16_t hull_pct = 0;
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		CraftData* cp = objects[pstate.target_obj_idx].craft_ptr;
		hull_pct = math2_percentage((uint16_t)(cp->hull_max - cp->hull_damage), cp->hull_max) / 0x28Fu;
	}
	panel_updatevalue(TIE_HUDI_THREAT_HULL_PCT, hull_pct, 1);

	/* Ion (137/139) / torp (141) / missile / beam levers. */
	uint16_t ion = 0, torp = 0, missile = 0, beam = 0;
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		CraftData* cp = objects[pstate.target_obj_idx].craft_ptr;
		for (uint8_t i = 0; i < cp->laser_group_cnt; ++i) {
			uint8_t t = cp->laser_type[i];
			if (t == 139 || t == 137)
				ion = cp->laser_owner_player[i] ? (uint16_t)(threat_blink_phase() + 1u) : 1;
			if (t == 141)
				torp = cp->laser_owner_player[i] ? (uint16_t)(threat_blink_phase() + 1u) : 1;
		}
		if (cp->mode_byte == 23) {
			for (uint8_t k = 0; k < cp->missile_group_cnt; ++k) {
				if (cp->warhead_type[k]) {
					missile =
						((int16_t)cp->missile_count_total <= 0) ? 1u : (uint16_t)(threat_blink_phase() + 1u);
				}
			}
		}
		beam = (cp->beam_type != 0);
	}
	panel_updatelever(TIE_HUDI_THREAT_ION, ion);
	panel_updatelever(TIE_HUDI_THREAT_TORP, torp);
	panel_updatelever(TIE_HUDI_THREAT_MISSILE, missile);
	panel_updatelever(TIE_HUDI_THREAT_BEAM, beam);
}

// FUNCTION: TIE 0x42734
void panel_updatethreatname(void) {
	dropflag = 0;
	festring_setbackcolor(0x2C);
	festring_setfontsize(2);

	int16_t left, right;
	if (flightResolution == TIE_FLIGHT_RES_VGA) {
		left = 66;
		right = 246;
	} else {
		left = 132;
		right = 492;
	}

	int force_name_repaint = 0;
	int16_t prev_target = lasttargetnum;
	if (lasttargetnum != (int16_t)pstate.target_obj_idx) {
		lasttargetnum = (int16_t)pstate.target_obj_idx;

		oldinstruments[70] = -1;
		oldinstruments[71] = -1;
		oldinstruments[72] = -1;
		oldinstruments[79] = -1;
		oldinstruments[80] = -1;
		oldinstruments[81] = -1;
		oldinstruments[82] = -1;

		TieHudState* hud = TieSnapshotBuilder_HudMut();
		hud->target_order_text[0] = '\0';
		hud->target_link_target_label[0] = '\0';
		hud->target_link_name[0] = '\0';
		hud->target_link_dist_label[0] = '\0';
		hud->target_link_dist_text[0] = '\0';
		hud->target_eta_label[0] = '\0';
		hud->target_eta_text[0] = '\0';

		int16_t name_w = (flightResolution == TIE_FLIGHT_RES_VGA) ? 76 : 114;
		festring_setbound((int16_t)instruments[69].x, (int16_t)instruments[69].y,
						  (int16_t)(instruments[69].x + name_w),
						  (int16_t)(instruments[69].y + fontheight + 1));
		clearwindow();
		festring_setcursor((int16_t)instruments[69].x, (int16_t)instruments[69].y);
		festring_setautofill(1);
		force_name_repaint = 1;

		if (pstate.target_obj_idx != 0xFFFF) {
			panel_buildobjectname(pstate.target_obj_idx, 3);
			festring_outstringcenter((const uint8_t*)tempstring);
			/* Base color for the threat target name. 0xFE color escapes
			 * inside the name string override per-glyph; HD honours them. */
			hud->instruments[69].color = 0x49;
		}

		if ((uint16_t)lasttargetnum >= NUM_CRAFTS) {
			festring_setbound(left, (int16_t)instruments[79].y, right,
							  (int16_t)(instruments[82].y + fontheight));
			clearwindow();
		}
	}
	(void)force_name_repaint;

	if (pstate.target_obj_idx == 0xFFFF)
		return;

	/* Target distance line. */
	int16_t dx = (int16_t)instruments[71].x;
	int16_t dy = (int16_t)instruments[71].y;
	if (prev_target == -1) {
		festring_setcursor(dx, dy);
		festring_settextcolor(0x49);
		festring_outstring((const uint8_t*)diststring);
	}
	pai_distancebetween(pstate.object_idx, (uint16_t)lasttargetnum);

	int16_t fw = (flightResolution == TIE_FLIGHT_RES_VGA) ? 54 : 81;
	festring_setbound(dx, dy, (int16_t)(dx + fw), (int16_t)(dy + fontheight + 1));
	festring_settextcolor(0x4A);

	int32_t scaled = (trig2_polardistance * 161) >> 16;
	if ((uint32_t)(trig2_polardistance >> 16) >= 0x2710u)
		scaled = 9999;

	uint16_t km_int = (uint16_t)scaled / 100u;
	uint16_t km_frac = (uint16_t)scaled % 100u;
	/* These fields bypass panel_updatevalue in the classic renderer, so
	 * publish their numeric metadata explicitly for the HD text pass. */
	TieHudInstrument* distance_int = &TieSnapshotBuilder_HudMut()->instruments[TIE_HUDI_THREAT_DIST_KM_INT];
	distance_int->value = (int16_t)km_int;
	distance_int->color = 0x4A;
	distance_int->digits = 2;
	TieHudInstrument* distance_frac = &TieSnapshotBuilder_HudMut()->instruments[TIE_HUDI_THREAT_DIST_KM_FRAC];
	distance_frac->value = (int16_t)km_frac;
	distance_frac->color = 0x4A;
	distance_frac->digits = 2;
	if ((int16_t)km_int != oldinstruments[71]) {
		oldinstruments[71] = (int16_t)km_int;
		festring_setcursor(dx, dy);
		panelrts_outnum((int32_t)km_int, 2, 1);
	}
	if ((int16_t)km_frac != oldinstruments[72]) {
		oldinstruments[72] = (int16_t)km_frac;
		int16_t sep_w = sys2_calclength((uint8_t*)&separator_period[0]);
		festring_setcursor((int16_t)(dx + sep_w + 1), dy);
		panelrts_outnum((int32_t)km_frac, 2, 2);
	}

	/* Cargo / species tag. */
	int16_t cargo_kind = 2;
	const char* cargo_str = (const char*)nonestring;
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		CraftData* cp = objects[pstate.target_obj_idx].craft_ptr;
		if (spec_data[cp->species_idx].has_cargo) {
			if (cp->inspected) {
				cargo_str = cp->cargo;
				cargo_kind = 1;
				if (!cp->cargo[0]) {
					cargo_kind = 2;
					cargo_str = (const char*)nonestring;
				}
			} else {

				cargo_str = (const char*)unknownstring;
				cargo_kind = 0;
			}
		}
	}
	if (cargo_kind != oldinstruments[70]) {
		oldinstruments[70] = cargo_kind;
		int16_t cw = (flightResolution == TIE_FLIGHT_RES_VGA) ? 50 : 75;
		festring_setbound((int16_t)instruments[70].x, (int16_t)instruments[70].y,
						  (int16_t)(instruments[70].x + cw), (int16_t)(instruments[70].y + fontheight + 1));
		clearwindow();
		festring_setcursor((int16_t)instruments[70].x, (int16_t)instruments[70].y);
		festring_settextcolor(0x46);
		festring_outstring((const uint8_t*)cargo_str);
		TieSnapshotBuilder_HudMut()->instruments[70].color = 0x46;
	}

	/* Order / link / ETA text block (only for dynamic craft). */
	if (pstate.target_obj_idx >= NUM_CRAFTS)
		return;

	CraftData* cp = objects[pstate.target_obj_idx].craft_ptr;
	uint16_t cur_order = cp->current_order;
	if (!cp->status_flags) {
		cur_order = 42;
	} else if (!objects[pstate.target_obj_idx].current_speed) {
		if (cp->current_order >= 0x2Du && cp->current_order <= 54)
			cur_order = 66;
	}

	if ((int16_t)cur_order != oldinstruments[79]) {
		oldinstruments[79] = (int16_t)cur_order;
		int16_t ow = (flightResolution == TIE_FLIGHT_RES_VGA) ? 190 : 285;
		uint16_t yy = instruments[79].y;
		festring_setbound(left, (int16_t)yy, (int16_t)(left + ow), (int16_t)(yy + fontheight + 1));
		clearwindow();
		festring_setcursor(left, (int16_t)yy);
		festring_settextcolor(0x45);
		festring_outstring((const uint8_t*)currentorderstring);
		festring_settextcolor(0x4E);

		const uint8_t* order_text = (const uint8_t*)messagetable[convertmessage[cur_order]];
		festring_outstring(order_text);

		int16_t eta_w = (flightResolution == TIE_FLIGHT_RES_VGA) ? 100 : 150;
		uint16_t eta_y = instruments[82].y;
		festring_setbound(left, (int16_t)eta_y, (int16_t)(left + eta_w), (int16_t)(eta_y + fontheight + 1));
		clearwindow();
		festring_setcursor(left, (int16_t)eta_y);
		festring_settextcolor(0x45);

		const uint8_t* eta_label;
		if (objects[pstate.target_obj_idx].current_speed) {
			eta_label = ((uint16_t)cp->ai_target_ref >= 0x8000u) ? (const uint8_t*)timetodeststring
																 : (const uint8_t*)timetotargetstring;
		} else {
			eta_label = (const uint8_t*)timeremstring;
		}
		festring_outstring(eta_label);

		TieHudState* hud = TieSnapshotBuilder_HudMut();
		str_copy_bounded(hud->target_order_text, sizeof hud->target_order_text, order_text);
		str_copy_bounded(hud->target_eta_label, sizeof hud->target_eta_label, eta_label);
		hud->instruments[79].color = 0x4E;
	}

	int16_t link = cp->status_flags ? cp->ai_target_ref : -1;
	if (link != oldinstruments[80]) {
		oldinstruments[80] = link;
		festring_settextcolor(0x45);
		festring_setbound(left, (int16_t)instruments[80].y, right, (int16_t)(instruments[80].y + fontheight));
		clearwindow();
		festring_setcursor(left, (int16_t)instruments[80].y);
		const uint8_t* link_target_label =
			((uint16_t)link >= 0x8000u) ? (const uint8_t*)curdeststring : (const uint8_t*)curtargetstring;
		festring_outstring(link_target_label);
		festring_setbound(left, (int16_t)instruments[81].y, right, (int16_t)(instruments[81].y + fontheight));
		clearwindow();
		festring_setcursor(left, (int16_t)instruments[81].y);
		const uint8_t* link_dist_label = ((uint16_t)link >= 0x8000u) ? (const uint8_t*)disttodeststring
																	 : (const uint8_t*)distfromtargetstring;
		festring_outstring(link_dist_label);

		/* Link target name. */
		int16_t name_w = (flightResolution == TIE_FLIGHT_RES_VGA) ? 100 : 150;
		uint16_t nx = instruments[80].x;
		festring_setbound((int16_t)nx, (int16_t)instruments[80].y, (int16_t)(nx + name_w),
						  (int16_t)(instruments[80].y + fontheight + 1));
		clearwindow();
		festring_setcursor((int16_t)nx, (int16_t)instruments[80].y);
		const uint8_t* link_name;
		if (link == 0xFF || link == -1) {
			link_name = (const uint8_t*)notargetstring;
		} else {
			panel_buildobjectname((uint16_t)link, 3);
			link_name = (const uint8_t*)tempstring;
		}
		festring_outstring(link_name);

		TieHudState* hud = TieSnapshotBuilder_HudMut();
		str_copy_bounded(hud->target_link_target_label, sizeof hud->target_link_target_label,
						 link_target_label);
		str_copy_bounded(hud->target_link_dist_label, sizeof hud->target_link_dist_label, link_dist_label);
		str_copy_festring_remapped(hud->target_link_name, sizeof hud->target_link_name, link_name);
		hud->instruments[80].color = 0x45;
	}

	/* Target-distance line (instruments[81]) and ETA clock
	 * (instruments[82]) -- mirrors decomp branches. Numeric cache slots
	 * are threat_cache_tgt_dist_frac (81) and threat_cache_eta_secs (82). */
	if (link != -1) {
		ai.active_obj_idx = pstate.target_obj_idx;
		craftptr = (cp->mode_byte == 10) ? objects[cp->leader_obj_idx].craft_ptr : cp;
		pai_targetdistance();

		int16_t tx = (int16_t)instruments[81].x;
		int16_t ty = (int16_t)instruments[81].y;
		int32_t saved_polar = trig2_polardistance;

		int16_t tw = (flightResolution == TIE_FLIGHT_RES_VGA) ? 54 : 81;
		festring_setbound(tx, ty, (int16_t)(tx + tw), (int16_t)(ty + fontheight + 1));
		festring_settextcolor(0x4A);

		int32_t t_scaled = (trig2_polardistance * 161) >> 16;
		if ((uint32_t)(trig2_polardistance >> 16) >= 0x2710u)
			t_scaled = 9999;
		uint16_t t_int = (uint16_t)t_scaled / 100u;
		uint16_t t_frac = (uint16_t)t_scaled % 100u;
		if ((int16_t)t_frac != oldinstruments[81]) {
			oldinstruments[81] = (int16_t)t_frac;
			festring_setcursor(tx, ty);
			panelrts_outnum((int32_t)t_int, 2, 1);
			outchar('.');
			panelrts_outnum((int32_t)t_frac, 2, 2);

			TieHudState* hud = TieSnapshotBuilder_HudMut();
			snprintf(hud->target_link_dist_text, sizeof hud->target_link_dist_text, "%u.%02u",
					 (unsigned)t_int, (unsigned)t_frac);
			hud->instruments[81].color = 0x4A;
		}
		(void)saved_polar;
	}

	/* ETA clock -- mm:ss (instruments[82]). */
	int16_t ew = (flightResolution == TIE_FLIGHT_RES_VGA) ? 54 : 81;
	uint16_t ey = instruments[82].y;
	uint16_t ex = instruments[82].x;
	festring_setbound((int16_t)ex, (int16_t)ey, (int16_t)(ex + ew), (int16_t)(ey + fontheight + 1));
	festring_settextcolor(0x52);
	festring_setcursor((int16_t)ex, (int16_t)ey);

	int16_t cur_speed = objects[pstate.target_obj_idx].current_speed;
	uint16_t secs, mins;
	if (cur_speed) {
		uint16_t tot = (uint16_t)(trig2_polardistance / (uint16_t)(18 * cur_speed));
		mins = tot / 60u;
		secs = tot % 60u;
	} else {
		uint16_t ord = cp->current_order;
		if (ord != 35 && ord != 66) {

			festring_outstring((const uint8_t*)unknownstring);
			TieHudState* hud_u = TieSnapshotBuilder_HudMut();
			str_copy_bounded(hud_u->target_eta_text, sizeof hud_u->target_eta_text,
							 (const uint8_t*)unknownstring);
			hud_u->instruments[82].color = 0x52;
			return;
		}
		/* Hyperspace countdown: the shared maneuver_timer (ticks at the
		 * PIT rate of ~236 Hz) is the one PAIMAN_intohyperspacemaneuver
		 * seeded at 0x674. Divide by 236 for seconds, then mm:ss. */
		uint16_t tot = (uint16_t)((uint32_t)cp->maneuver_timer / 236u);
		mins = tot / 60u;
		secs = tot % 60u;
	}

	if ((int16_t)secs == oldinstruments[82])
		return;
	oldinstruments[82] = (int16_t)secs;
	clearwindow();
	panelrts_outnum((int32_t)mins, 2, 1);
	outchar(':');
	panelrts_outnum((int32_t)secs, 2, 2);

	TieHudState* hud_e = TieSnapshotBuilder_HudMut();
	snprintf(hud_e->target_eta_text, sizeof hud_e->target_eta_text, "%02u:%02u", (unsigned)mins,
			 (unsigned)secs);
	hud_e->instruments[82].color = 0x52;
}

/*
 * panel_updatecmd -- center-console target CRT + textual target info.
 * Implements the full target-change invalidation + 5 data lines
 * (shield/hull/dist/system/cargo + subsystem focus).
 */
// FUNCTION: TIE 0x40530
void panel_updatecmd(void) {
	dropflag = 0;
	if (mission.train_craft_type) {
		gate_trainingupdatecrt((int16_t)instruments[2].x, (int16_t)instruments[2].y);
		return;
	}

	if ((pstate.player_craft->working_subsystems & 1) == 0)
		return;

	int16_t force_redraw = 0;
	int16_t text_width, name_width;
	if (flightResolution == TIE_FLIGHT_RES_VGA) {
		text_width = 40;
		name_width = 80;
	} else {
		text_width = 70;
		name_width = 160;
	}

	if (lasttargetnum != (int16_t)pstate.target_obj_idx) {
		int16_t prev_target = lasttargetnum;
		oldinstruments[45] = -1;
		oldinstruments[58] = -1;
		oldinstruments[59] = -1;
		oldinstruments[60] = -1;
		oldinstruments[61] = -1;
		oldinstruments[62] = -1;
		oldinstruments[63] = -1;
		oldinstruments[64] = -1;
		lasttargetnum = (int16_t)pstate.target_obj_idx;
		oldinstruments[65] = -1;
		festring_setfontsize(2);
		festring_setbackcolor(0x30);
		festring_setautofill(1);
		force_redraw = 1;

		if (prev_target == (int16_t)0xFFFF) {
			/* Paint static labels. Engine picks color 0x45 (VGA) or
			 * 0x46 (SVGA) — see PANEL_updatecmd at 0x40696. The two
			 * remap to different physical palette entries so SVGA
			 * fidelity needs the 0x46 branch. */
			festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
			festring_setcursor((int16_t)instruments[88].x, (int16_t)instruments[88].y);
			const uint8_t label_color = (flightResolution == TIE_FLIGHT_RES_VGA) ? 0x45 : 0x46;
			festring_settextcolor(label_color);
			festring_outstring((const uint8_t*)shieldstring);
			festring_setcursor((int16_t)(instruments[61].x + sys2_calclength((uint8_t*)separator_3_spaces)),
							   (int16_t)instruments[61].y);
			outchar('%');

			festring_setcursor((int16_t)instruments[89].x, (int16_t)instruments[89].y);
			festring_outstring((const uint8_t*)hullstring);
			festring_setcursor((int16_t)(instruments[62].x + sys2_calclength((uint8_t*)separator_3_spaces)),
							   (int16_t)instruments[62].y);
			outchar('%');

			festring_setcursor((int16_t)instruments[87].x, (int16_t)instruments[87].y);
			festring_outstring((const uint8_t*)diststring);
			festring_setcursor((int16_t)(instruments[59].x + sys2_calclength((uint8_t*)separator_2_spaces)),
							   (int16_t)instruments[59].y);
			outchar('.');

			festring_setcursor((int16_t)instruments[86].x, (int16_t)instruments[86].y);
			festring_outstring((const uint8_t*)sysstring);
			festring_setcursor((int16_t)(instruments[58].x + sys2_calclength((uint8_t*)separator_3_spaces)),
							   (int16_t)instruments[58].y);
			outchar('%');

			TieHudInstrument* hi = TieSnapshotBuilder_HudMut()->instruments;
			hi[86].color = label_color;
			hi[87].color = label_color;
			hi[88].color = label_color;
			hi[89].color = label_color;
			/* Engine leaves textcolor at label_color for the target-name
			 * paint at instrument[90]; 0xFE escapes in the name override
			 * per-glyph, this is the fallback base. */
			hi[90].color = label_color;
		}

		if (pstate.target_obj_idx == 0xFFFF) {
			panel_updatelever(TIE_HUDI_DAMAGE_CRACK_FIRST, 0);
		} else {
			/* Target-name field. */
			festring_setbound((int16_t)instruments[90].x, (int16_t)instruments[90].y,
							  (int16_t)(instruments[90].x + name_width),
							  (int16_t)(instruments[90].y + fontheight + 1));
			clearwindow();
			panel_buildobjectname(pstate.target_obj_idx, 3);
			festring_setcursor((int16_t)instruments[90].x, (int16_t)instruments[90].y);
			festring_outstringcenter((const uint8_t*)tempstring);

			/* Missile and warhead targets show their current target in the
			 * component-name field.
			 *   - homing missile aimed at someone else → that target's
			 *     FG name via panel_buildobjectname(target, 2)
			 *   - homing missile aimed at the player    → `ourstring`
			 *     (the "us" string)
			 *   - non-homing missile                    → componentnames[32]
			 *     (the same fallback the cargo line uses) */
			if (pstate.target_obj_idx >= NUM_CRAFTS && pstate.target_obj_idx < NUM_OBJECTS) {
				const uint8_t mship_idx = objects[pstate.target_obj_idx].ship_idx;
				if (mship_idx >= WEAPON_SPECIES_BASE &&
					mship_idx < WEAPON_SPECIES_BASE + WEAPON_SPECIES_COUNT &&
					projectile_is_warhead_type[mship_idx - WEAPON_SPECIES_BASE]) {
					const WarheadRecord* wh = (const WarheadRecord*)objects[pstate.target_obj_idx].craft_ptr;
					if (wh->homing_tier) {
						if (wh->target_obj != pstate.object_idx)
							panel_buildobjectname(wh->target_obj, 2);
						else
							festring_farstrcpy((const char*)ourstring);
					} else {
						festring_farstrcpy((const char*)((char**)componentnames)[32]);
					}
					festring_setbound((int16_t)instruments[65].x, (int16_t)instruments[65].y,
									  (int16_t)(instruments[65].x + text_width),
									  (int16_t)(instruments[65].y + fontheight + 1));
					clearwindow();
					festring_setcursor((int16_t)instruments[65].x, (int16_t)instruments[65].y);
					festring_settextcolor(0x4E);
					festring_outstringright((const uint8_t*)tempstring);
					{
						TieHudState* hud = TieSnapshotBuilder_HudMut();
						str_copy_bounded(hud->target_subsystem_text, sizeof hud->target_subsystem_text,
										 (const uint8_t*)tempstring);
						hud->instruments[65].color = 0x4E;
					}
				}
			}
		}
	}

	if (pstate.target_obj_idx == 0xFFFF)
		return;

	festring_setbackcolor(0x30);
	/* TIE98 renders the CRT later from TIE_Update_Screen for both backends. */
	if (!TieProfile_UsesTie98Logic()) {
		panel_update3Dcrt(instruments[2].x, instruments[2].y, instruments[2].param1, instruments[2].param2,
						  force_redraw);
	}

	CraftData* tgt = (pstate.target_obj_idx < 0x3800u) ? objects[pstate.target_obj_idx].craft_ptr : NULL;

	/* Shield % (0x3D). */
	uint16_t shield_pct = 0;
	if (pstate.target_obj_idx < NUM_CRAFTS && tgt) {
		uint16_t sum = (uint16_t)(tgt->rear_shield + tgt->forward_shield);
		uint16_t sp = (uint16_t)(2 * spec_data[tgt->species_idx].shield_points);
		if (!mission.difficulty) {
			uint8_t side = objects[pstate.target_obj_idx].side;
			if (side == 1) {
				/* Enemy side 1 on easy: `(dword>>17) + (dword>>16)` at
				 * spec.field_10 => 1.5*shield_points; *2 outside => 3*. */
				int shield_pts = spec_data[tgt->species_idx].shield_points;
				sp = (uint16_t)(3 * shield_pts);
			} else if (side == 0 || side == 4) {
				int16_t adj = math2_fraction((uint16_t)spec_data[tgt->species_idx].shield_points, 0xC000u);
				sp = (uint16_t)(2 * adj);
			}
		}
		if (sp) {
			shield_pct = (uint16_t)(2 * (math2_percentage((uint16_t)(sum >> 1), sp) / 0x28Fu));
			if (sum && !shield_pct)
				shield_pct = 1;
		}
	}
	panel_updatevalue(TIE_HUDI_TARGET_SHIELD_PCT, shield_pct, 1);

	/* Hull % (0x3E). */
	uint16_t hull_pct;
	if (pstate.target_obj_idx >= NUM_CRAFTS) {
		hull_pct = 100;
	} else if (tgt && tgt->hull_damage <= tgt->hull_max) {
		hull_pct = math2_percentage((uint16_t)(tgt->hull_max - tgt->hull_damage), tgt->hull_max) / 0x28Fu;
		if (!hull_pct)
			hull_pct = 1;
	} else {
		hull_pct = 1;
	}
	panel_updatevalue(TIE_HUDI_TARGET_HULL_PCT, hull_pct, 1);

	/* Subsystem % (0x3A). */
	uint16_t sys_pct;
	if (pstate.target_obj_idx >= NUM_CRAFTS) {
		sys_pct =
			(pstate.target_obj_idx < 0x3800u || staticobjects[pstate.target_obj_idx - 14336].status_flags)
				? 100
				: 0;
	} else if (tgt) {
		uint16_t capable = 0, alive = 0;
		uint16_t cap_mask = tgt->subsystem_active;
		uint16_t stat_mask = tgt->status_flags;
		for (int b = 0; b < 16; ++b) {
			if (cap_mask & 1)
				++capable;
			if (stat_mask & 1)
				++alive;
			cap_mask >>= 1;
			stat_mask >>= 1;
		}
		sys_pct = capable ? (uint16_t)(100 * alive / capable) : 0;
		if (sys_pct > 25 && tgt->ion_drain_timer)
			sys_pct = 25;
	} else {
		sys_pct = 0;
	}
	panel_updatevalue(TIE_HUDI_TARGET_SUBSYSTEM_PCT, sys_pct, 1);

	pai_distancebetween(pstate.object_idx, pstate.target_obj_idx);
	panel_outputdistance(trig2_polardistance);

	/* Cargo display (0x3F -> instrument[63]). Initial string is
	 * componentnames[32] (retail @ 0x40c87..0x40c99); the fighter
	 * if-body overrides for craft targets only. */
	int16_t cargo_kind = 2;
	const uint8_t* cargo_str = (const uint8_t*)((char**)componentnames)[32];
	if (pstate.target_obj_idx < NUM_CRAFTS && !objects[pstate.target_obj_idx].category && tgt) {
		if (tgt->inspected) {
			cargo_str = (const uint8_t*)tgt->cargo;
			cargo_kind = 1;
			if (!tgt->cargo[0]) {
				cargo_kind = 2;
				cargo_str = (const uint8_t*)nonestring;
			}
		} else {

			cargo_str = (const uint8_t*)unknownstring;
			cargo_kind = 0;
		}
	}
	if (cargo_kind != oldinstruments[63]) {
		oldinstruments[63] = cargo_kind;
		festring_setbound((int16_t)instruments[63].x, (int16_t)instruments[63].y,
						  (int16_t)(instruments[63].x + text_width),
						  (int16_t)(instruments[63].y + fontheight + 1));
		clearwindow();
		festring_setcursor((int16_t)instruments[63].x, (int16_t)instruments[63].y);
		festring_settextcolor(0x46);
		festring_outstringright(cargo_str);
		{
			TieHudState* hud = TieSnapshotBuilder_HudMut();
			str_copy_bounded(hud->target_cargo, sizeof hud->target_cargo, cargo_str);
			hud->instruments[63].color = 0x46;
		}
	}

	/* Subsystem focus (instrument[65]). */
	if (pstate.target_obj_idx < NUM_CRAFTS || pstate.target_obj_idx >= 0x3800u) {
		int16_t focus = (pstate.target_obj_idx >= NUM_CRAFTS) ? 40 : pstate.radar_target1;
		festring_settextcolor(0x4E);
		if (focus != oldinstruments[65]) {
			oldinstruments[65] = focus;
			festring_setbound((int16_t)instruments[65].x, (int16_t)instruments[65].y,
							  (int16_t)(instruments[65].x + text_width),
							  (int16_t)(instruments[65].y + fontheight + 1));
			clearwindow();
			festring_setcursor((int16_t)instruments[65].x, (int16_t)instruments[65].y);

			const uint8_t* s;
			if (focus == 40) {
				s = ((const uint8_t**)componentnames)[32];
			} else {
				const uint8_t model_type = objects[pstate.target_obj_idx].ship_idx;
				if (!TieProfile_UsesTie98Logic())
					draw_lockshipfileptrs(model_type);
				uint16_t mt = TieProfile_UsesTie98Logic()
								  ? modelmesh_gettype(model_type, pstate.radar_target1)
								  : componentblockptr[(uint16_t)pstate.radar_target1].mesh_type;
				/* Fighters display mesh type 7 with component label 26. */
				if (objects[pstate.target_obj_idx].genus == GENUS_FIGHTER && mt == 7)
					mt = 26;
				s = ((const uint8_t**)componentnames)[mt];
			}
			festring_outstringright(s);
			{
				TieHudState* hud = TieSnapshotBuilder_HudMut();
				str_copy_bounded(hud->target_subsystem_text, sizeof hud->target_subsystem_text, s);
				hud->instruments[65].color = 0x4E;
			}
		}
	}
}

/* ================================================================== */
/* Resource / view loader                                             */
/* ================================================================== */

/*
 * panel_loadpaneldata -- panelname = cockpitdir + spec.internal_name,
 * then preload every view.
 */
// FUNCTION: TIE 0x43628
void panel_loadpaneldata(void) {
	strcpy(panelname, cockpitdir);
	strcat(panelname, spec_data[pstate.player_spec_num].internal_name);

	searchpartsflag = 0;
	panel_loadpanelviewdefs(panelname);
	panel_tryEMSforpanels();
	/* panelflag=1 handled by legacy stub. */
}

/*
 * panel_forcenewviewdir -- invalidate cockpit state and switch view.
 */
// FUNCTION: TIE 0x436E4
void panel_forcenewviewdir(uint16_t view_idx) {
	lastpilotpaneldraw = -1;
	camera.pilotview = 0xFF;
	panelpartsflag = 0xFF;
	panelrts_setnewpilotview(view_idx);
	msg_messageinit();
}

/*
 * panel_loadcontrolpanel -- read N sections of an LFD into
 * temppanelptr, recording each section's start in section_ptrs[].
 */
// FUNCTION: TIE 0x43B6C
void panel_loadcontrolpanel(char* name, void** section_ptrs, uint16_t count) {
	strcpy(panelfilename, cockpitdir);
	strcat(panelfilename, name);
	strcat(panelfilename, ".LFD");

	fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, panelfilename, _readmode, 1);

	for (uint16_t i = 0; i < count; ++i) {
		uint8_t header[16];
		section_ptrs[i] = temppanelptr;

		fediskio_readfileblock(header, 16, 1, fileptr);
		int is_palt = 1;
		for (int j = 0; j < 4; ++j)
			if (header[j] != (uint8_t)xpal_id[j])
				is_palt = 0;

		uint32_t size = br_u32le(header + 12); /* size field at +0x0C */
		fediskio_readfileblock(temppanelptr, size, 1, fileptr);

		uint8_t* end = (uint8_t*)temppanelptr + size;
		if (is_palt) {
			uint8_t* p = (uint8_t*)temppanelptr;
			for (uint32_t n = size; n--; ++p)
				*p >>= 2;
			section_ptrs[i] = (uint8_t*)section_ptrs[i] + 2;
		}
		temppanelptr = end;
	}
	fediskio_tryclosefile(0);
}

/*
 * panel_tryEMSforpanels -- preload every defined view slot.
 */
// FUNCTION: TIE 0x43CF4
void panel_tryEMSforpanels(void) {
	/* Binary: XMEMHDL_Alloc_Handle -> malloc; Lock/Unlock -> no-op.
	 * handle field repurposed as a "loaded" flag (1 = loaded, 0 = empty). */
	for (uint16_t i = 0; i < PANEL_NUM_VIEWS; ++i) {
		panelviewptrs[i].handle = 0;
		if (panelviewdefs[i].flags == 1) {
			strcpy(panelfilename, cockpitdir);
			strcat(panelfilename, panelviewdefs[i].name);
			strcat(panelfilename, ".LFD");

			fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, panelfilename, _readmode, 1);
			if (fileptr) {
				int32_t sz = filelen_filelength(fileptr);
				fediskio_tryclosefile(0);
				fediskio_UnlockGlobals();
				void* buf = malloc((size_t)sz);
				fediskio_RelockGlobals();
				if (buf) {
					panelviewptrs[i].handle = 1;
					panel_view_bufs[i] = buf;
					temppanelptr = buf;
					panel_loadcontrolpanel(panelviewdefs[i].name, &panelviewptrs[i].image, 3);
				}
			}
		}
	}
	panelsloadedflag = 1;
}

/* Release every preloaded panel-view buffer. Called from fediskio's
 * FreeFlightHandles teardown path. */
void panel_freeviewbufs(void) {
	for (uint16_t i = 0; i < PANEL_NUM_VIEWS; ++i) {
		free(panel_view_bufs[i]);
		panel_view_bufs[i] = NULL;
		panelviewptrs[i].handle = 0;
		panelviewptrs[i].image = NULL;
		panelviewptrs[i].mask = NULL;
		panelviewptrs[i].palette = NULL;
	}
	panelsloadedflag = 0;
}

/*
 * panel_resetpilotview -- wipe screen + force full view reload.
 * In the binary this tail-calls panel_loadpanelviewdefs.
 */
void panel_resetpilotview(void) {
	blank();
	searchpartsflag = 0;
	panel_loadpanelviewdefs(panelfilename);
}

/*
 * PanelViewDef on-disk codec.
 *
 * Layout (36 bytes total, identical packed and naturally aligned because
 * the 1+9-byte prefix lands at +0x0A which is already 2-aligned):
 *   +0x00 flags (u8)
 *   +0x01 name[9]
 *   +0x0A pos_x  (u16)
 *   +0x0C pos_y  (u16)
 *   +0x0E width  (u16)
 *   +0x10 depth  (u16)
 *   +0x12 yoffset (i16, signed -- main view uses -19)
 *   +0x14 title[16]
 */
void PanelViewDef_decode(PanelViewDef* dst, const uint8_t* src) {
	dst->flags = src[0x00];
	memcpy(dst->name, src + 0x01, 9);
	dst->pos_x = br_u16le(src + 0x0A);
	dst->pos_y = br_u16le(src + 0x0C);
	dst->width = br_u16le(src + 0x0E);
	dst->depth = br_u16le(src + 0x10);
	dst->yoffset = br_i16le(src + 0x12);
	memcpy(dst->title, src + 0x14, 16);
}

/*
 * HudInstrument on-disk codec (decode only -- HUD widget table is read
 * from .INT files but never written by the runtime).
 *   +0x00 x      (u16)   +0x02 y      (u16)
 *   +0x04 param1 (u8)    +0x05 param2 (u8)
 * See HudInstrument's doc-comment in panel.h for the per-widget
 * interpretation of param1/param2.
 */
void HudInstrument_decode(HudInstrument* dst, const uint8_t* src) {
	dst->x = br_u16le(src + 0x00);
	dst->y = br_u16le(src + 0x02);
	dst->param1 = src[0x04];
	dst->param2 = src[0x05];
}

/* Decode .INT records without relying on host alignment or byte order. */
// FUNCTION: TIE 0x43F4C
void panel_loadpanelviewdefs(char* base_name) {
	strcpy(panelfilename, base_name);
	strcat(panelfilename, ".INT");

	fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, panelfilename, _readmode, 1);
	uint8_t views_buf[PANEL_NUM_VIEWS * PANELVIEWDEF_DISK_SIZE];
	fediskio_readfileblock(views_buf, PANELVIEWDEF_DISK_SIZE, PANEL_NUM_VIEWS, fileptr);
	for (int i = 0; i < PANEL_NUM_VIEWS; ++i)
		PanelViewDef_decode(&panelviewdefs[i], views_buf + i * PANELVIEWDEF_DISK_SIZE);
	uint8_t instr_buf[PANEL_NUM_INSTRUMENTS * HUDINSTRUMENT_DISK_SIZE];
	fediskio_readfileblock(instr_buf, HUDINSTRUMENT_DISK_SIZE, PANEL_NUM_INSTRUMENTS, fileptr);
	for (int i = 0; i < PANEL_NUM_INSTRUMENTS; ++i)
		HudInstrument_decode(&instruments[i], instr_buf + i * HUDINSTRUMENT_DISK_SIZE);
	fediskio_readfileblock(parts, 11, 1, fileptr);
	fediskio_tryclosefile(0);
}

/*
 * panel_dosetnewpilotview -- install view `view_idx`. Handles the
 * flags-0x80 / 0xC0 mirror tables, lazy bitmap load, and the final
 * buffer-dim + mask-copy sequence.
 */
// FUNCTION: TIE 0x43710, TIE98 0x466B70
void panel_dosetnewpilotview(uint16_t view_idx) {
	const bool tie98 = TieProfile_UsesTie98Logic();
	panelmirrorflag = 0;
	uint16_t panel_x = 0;
	uint16_t dest = view_idx;

	if (view_idx != 18) {
		uint8_t f = panelviewdefs[view_idx].flags;
		if (f >= 0xC0u) {
			dest = (uint8_t)(f - 0xC0);
			panelmirrorflag = 1;
			lastpilotpaneldraw = -1;
			panel_x = (uint16_t)(screenXRes - 1);
		} else if (f < 0x80u) {
			dest = view_idx;
		} else {
			dest = (uint8_t)(f - 0x80);
		}
	}

	if (dest != (uint16_t)lastpilotpaneldraw) {
		festring_hidescreen();

		if (panelpartsflag != searchpartsflag) {
			farbufferptr = (uint8_t*)panelpartsptr;
			strcpy(panelname, cockpitdir);
			strcat(panelname, parts);
			strcat(panelname, ".PNL");
			/* Retail asm at 0x43855 zero-extends each byte and SUMS them
			 * (mov al, parts[9]; mov dl, parts[10]; add eax, edx; mov bx, ax).
			 * Both bytes seem to encode a small size; sum equals the
			 * little-endian u16 only when parts[10]==0. Match retail. */
			fediskio_loadbufferdata(panelname, 0, (uint16_t)((uint8_t)parts[9] + (uint8_t)parts[10]), 0);
			panelpartsflag = searchpartsflag;
		}

		if (camera.pilotview == 18) {
			festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)maxPixelsDeep);
			if (tie98)
				backcolor = deepspacecolor;
			else
				festring_setbackcolor(0);
			clearwindow();
			if (tie98)
				logbuf2_setbufferdimensions_tie98((uint16_t)screenXRes, (uint16_t)maxPixelsDeep, 0, 0);
			else
				logbuf2_setbufferdimensions((uint16_t)screenXRes, (uint16_t)maxPixelsDeep, 0);
			panel_clearmaskdata(pixelswide, pixelsdeep);
			transfm2_screenyoffset = 0;
		} else {
			if (!panelviewptrs[dest].handle || !panelsloadedflag) {
				temppanelptr = newbuf;
				panel_loadcontrolpanel(panelviewdefs[dest].name, &panelviewptrs[dest].image, 3);
			}
			buildpalette((const uint8_t*)panelviewptrs[dest].palette, 0, 64);
			/* TIE98 leaves palette index 0 transparent over a deep-space clear;
			 * TIE95 cockpit shapes use palette index 253 as their skip color. */
			uint16_t skip_color = 253;
			if (tie98) {
				backcolor = deepspacecolor;
				festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)maxPixelsDeep);
				clearwindow();
				skip_color = 0;
			}
			drawshape(panelviewptrs[dest].image, panel_x, 0, skip_color, panelmirrorflag);

			uint16_t geom = (panelmirrorflag == 1) ? view_idx : dest;
			/* displaycorner is the full framebuffer offset of the viewport origin:
			 * bytes for TIE98 and pixels for TIE95. */
			uint32_t displaycorner_off = calcposition(panelviewdefs[geom].pos_x, panelviewdefs[geom].pos_y);
			if (tie98)
				logbuf2_setbufferdimensions_tie98(panelviewdefs[geom].width, panelviewdefs[geom].depth, 0,
												  displaycorner_off);
			else
				logbuf2_setbufferdimensions(panelviewdefs[geom].width, panelviewdefs[geom].depth,
											displaycorner_off);
			panel_copymaskdata((char*)panelviewptrs[dest].mask, pixelswide, pixelsdeep, panelmirrorflag);
			transfm2_screenyoffset = panelviewdefs[geom].yoffset;
		}

		if (panelmirrorflag == 1)
			lastpilotpaneldraw = -1;
		else if (camera.pilotview == 18)
			lastpilotpaneldraw = 18;
		else
			lastpilotpaneldraw = (int16_t)dest;

		panel_initpanel();
		if (tie98)
			g_flightInitialTextureCacheFlushPending = 1;
		fullupdateflag = 1;
		festring_showscreen();
		calcframerate = 0;
	}

	if (dest == 17) {
		festring_setfontsize(2);
		int16_t y = (int16_t)instruments[33].y;
		int16_t y2 = (int16_t)(instruments[33].y + fontheight + 1);
		uint16_t half = (uint16_t)screenXRes >> 1;
		uint16_t hw = panel_AdjustXForRes(40);
		festring_setbound((int16_t)(half - hw), y, (int16_t)(half + hw), y2);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_settextcolor(0x43);
		festring_setcursor(0, y);
		festring_outstringcenter((const uint8_t*)panelviewdefs[camera.pilotview].title);
	}
}

/* ================================================================== */
/* Mask / 3D CRT / camera                                             */
/* ================================================================== */

/*
 * panel_copymaskdata -- RLE-decompress the occlusion mask into
 * xtransdataptr + maskbufptr. `mirror != 0` inverts the run order.
 *
 * Retail re-encodes the byte after a 0-escape as (b + 1) so the
 * downstream xtrans2 mask_read_delta yields delta = (b+1) + 256
 * (matching the encoder convention used by panel_clearmaskdata).
 * The b == 0xFF (SVGA-only) and b == 0 (nested escape) paths are
 * rewritten to a 3-byte [0, 0, x] form because (b+1) would overflow
 * into another escape. VGA (320 px) skips the SVGA-only nested arms.
 */
// FUNCTION: TIE 0x44008
void panel_copymaskdata(char* mask_src, uint16_t width, uint16_t height, uint8_t mirror) {
	uint8_t* out = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	int is_svga = (screenXRes != 320);

	for (uint16_t row = 0; row < height; ++row) {
		if (mirror) {
			/* Mirror: build a rearranged scratch buffer, then walk it
			 * backwards. Retail's trick is to swap each escape group's
			 * byte order in-place ([0, X] → [X, 0], [0, 0, Y] → [Y, 0, 0])
			 * so the reverse walk encounters the trailing 0 sentinel first
			 * and can resolve back to the original group's byte order on
			 * output. Single-byte segments (always non-zero, since 0 means
			 * escape) need no rearrangement. */
			uint8_t scratch[256];
			uint8_t first = *mask_src++;
			uint16_t pos = 0;
			uint8_t* sp = scratch;
			int seg = 0;

			while (pos < width) {
				uint8_t r = *mask_src++;
				if (!r) {
					uint8_t b = *mask_src++;
					pos += 255;
					if (is_svga && b == 0) {
						/* Triple [0, 0, Y]: store [Y+1, 0, 0]. */
						pos += 256;
						uint8_t b2 = *mask_src++;
						uint8_t adj = (uint8_t)(b2 + 1);
						*sp++ = adj;
						*sp++ = 0;
						*sp++ = 0;
						pos += adj;
					} else if (is_svga && b == 0xFF) {
						/* SVGA-only [0, 0xFF] special: store [0, 0, 0].
						 * Equivalent to the Y+1==0 case of the triple. */
						*sp++ = 0;
						*sp++ = 0;
						*sp++ = 0;
						pos += 256;
					} else {
						/* Pair [0, X]: store [X+1, 0]. */
						uint8_t adj = (uint8_t)(b + 1);
						*sp++ = adj;
						*sp++ = 0;
						pos += adj;
					}
					seg++;
				} else {
					*sp++ = r;
					pos += r;
					seg++;
				}
			}

			/* State byte: negate when segment count is even so the
			 * reversed-order run preserves the open/closed parity. */
			*out++ = (seg & 1) ? first : (uint8_t)-first;

			/* Reverse walk. A trailing 0 in scratch is always an escape
			 * sentinel from a rearranged group; read 1 or 2 more bytes
			 * to capture the data byte (and the inner 0 for triples). */
			while (sp > scratch) {
				uint8_t v24 = *--sp;
				*out++ = v24;
				if (v24 == 0) {
					uint8_t v25 = *--sp;
					*out++ = v25;
					if (v25 == 0) {
						*out++ = *--sp;
					}
				}
			}
		} else {
			uint8_t first = *mask_src++;
			*out++ = first;
			uint16_t pos = 0;
			while (pos < width) {
				uint8_t r = *mask_src++;
				if (!r) {
					*out++ = 0;
					pos += 255;
					uint8_t b = *mask_src++;
					if (is_svga && b == 0) {
						*out++ = 0;
						pos += 256;
						uint8_t b2 = *mask_src++;
						*out++ = (uint8_t)(b2 + 1);
						pos += (uint8_t)(b2 + 1);
					} else if (is_svga && b == 0xFF) {
						*out++ = 0;
						*out++ = 0;
						pos += 256;
					} else {
						*out++ = (uint8_t)(b + 1);
						pos += (uint8_t)(b + 1);
					}
				} else {
					*out++ = r;
					pos += r;
				}
			}
		}
	}
}

/*
 * panel_clearmaskdata -- one full-width run per row (mask = fully open).
 *
 * Retail emits a 3-tier encoding:
 *   width <  256 : [1, width]
 *   width <  511 : [1, 0, (uint8_t)(width + 1)]
 *   width >= 511 : [1, 0, 0, (uint8_t)(width + 1)]
 * The +1 on the trailing byte after the first 0 escape is paired with
 * the +1 that panel_copymaskdata adds when consuming the byte after a
 * 0; round-trip is exact. SVGA (640) is the only place width >= 511
 * triggers in practice; VGA (320) never reaches the double-escape arm.
 */
// FUNCTION: TIE 0x441EC
void panel_clearmaskdata(uint16_t width, uint16_t height) {
	uint8_t* out = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	for (uint16_t row = 0; row < height; ++row) {
		*out++ = 1;
		if (width >= 256) {
			*out++ = 0;
			if (width >= 511)
				*out++ = 0;
			*out++ = (uint8_t)(width + 1);
		} else {
			*out++ = (uint8_t)width;
		}
	}
}

/*
 * panel_update3Dcrt -- rotating target silhouette on the CMD CRT.
 */
// FUNCTION: TIE 0x4427C
void panel_update3Dcrt(uint16_t x, uint16_t y, uint16_t width, uint16_t depth, int16_t clear_runs) {
#if 0
	{
		static int dbg_once = 0;
		if (!dbg_once) {
			dbg_once = 1;
			TieDiagnostics_Log(TIE_LOG_INFO,
			        "[3Dcrt] instruments[2] x=%u y=%u w=%u d=%u | "
			        "screenXRes=%d screenYRes=%d screenMemWidth=%d | "
			        "saved pixelswide=%u pixelsdeep=%u displaycorner=%u | "
			        "x+w=%d y+d=%d\n",
			        x, y, width, depth,
			        (int)screenXRes, (int)screenYRes, (int)screenMemWidth,
			        (unsigned)pixelswide, (unsigned)pixelsdeep,
			        (unsigned)displaycorner,
			        (int)(x + width), (int)(y + depth));
		}
	}
#endif

	int32_t save_transfm2_screenyoffset = transfm2_screenyoffset;
	int32_t save_camera_x = camera.x;
	int32_t save_camera_y = camera.y;
	int32_t save_camera_z = camera.z;
	int16_t save_currenttarget = (int16_t)currenttarget;

	transfm2_screenyoffset = 0;
	uint32_t pos = rtsvga2_calcpositionVGA(x, y);
	logbuf2_startPIP(width, depth, clear_runs, pos);

	if (clear_runs) {
		/* Pick the right mask template for this ship/resolution. The
		 * SVGA path is a 7-way switch in the retail binary -- specs 7
		 * and 8 each have their own mask, and specs 4 and 5 do too;
		 * the VGA path collapses 7|8 into a single mask. */
		const uint8_t* src;
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			switch (pstate.player_spec_num) {
				case 15:
					src = gunboatcmdmaskdata;
					break;
				case 7:
				case 8:
					src = tieadvcmdmaskdata;
					break;
				case 11:
					src = missileboatcmdmaskdata;
					break;
				default:
					src = cmdmaskdata;
					break;
			}
			uint8_t* dst = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
			memcpy(dst, src, 200);
		} else {
			switch (pstate.player_spec_num) {
				case 15:
					src = gunboatcmd640maskdata;
					break;
				case 7:
					src = tieadv7cmd640maskdata;
					break;
				case 8:
					src = tieadv8cmd640maskdata;
					break;
				case 5:
					src = spec5cmd640maskdata;
					break;
				case 4:
					src = spec4cmd640maskdata;
					break;
				case 11:
					src = missileboatcmd640maskdata;
					break;
				default:
					src = cmd640maskdata;
					break;
			}
			uint8_t* dst = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
			memcpy(dst, src, 480);
		}
	}

	panel_pointcamera(pstate.target_obj_idx, 1);

	TieHudSnapshot_RecordPipCamera(pstate.target_obj_idx);

	if (pstate.radar_enable)
		currenttarget |= 0x0200u;
	else
		currenttarget |= 0x0400u;

	xtrans2_initxtrans();
	/* numbitmaps = 0 */
	numbitmaps = 0;

	worldx = worldlocx - camera.x;
	worldy = worldlocy - camera.y;
	worldz = worldlocz - camera.z;

	int32_t sub_world_x = 0, sub_world_y = 0, sub_world_z = 0;
	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		const uint8_t model_type = objects[pstate.target_obj_idx].ship_idx;
		if (TieProfile_UsesTie98Logic()) {
			calculate_tie98_pip_subsystem_offset(&objects[pstate.target_obj_idx], pstate.radar_target1);
			sub_world_x = rotatedx + worldlocx - camera.x;
			sub_world_y = worldlocy + rotatedy - camera.y;
			sub_world_z = worldlocz + rotatedz - camera.z;
			TieHudSnapshot_RecordPipSubsystem(pstate.target_obj_idx);
		} else {
			draw_lockshipfileptrs(model_type);
			ShipModelMesh* mesh = &componentblockptr[(uint16_t)pstate.radar_target1];
			int16_t side, up, fwd_neg;
			/* Binary uses unaligned dword-HIWORD reads; each >>17 decodes
			 * to the NEXT int16 field, shifted right by 1. Direct accesses
			 * (resolved via side/fwd/up names matching the LFD CRFT order):
			 *   >17 on &pos_side      -> pos_fwd>>1
			 *   >17 on &pos_fwd       -> pos_up>>1
			 *   >17 on &has_position  -> pos_side>>1
			 *   >17 on &center_side   -> center_fwd>>1
			 *   >17 on &center_fwd    -> center_up>>1
			 *   >17 on &draw_distance+2 -> center_side>>1 */
			if (mesh->has_position && (mesh->has_position != 1 || mesh->mesh_type == 1 /* MESH_MainHull */)) {
				side = (int16_t)(mesh->pos_side >> 1);
				up = (int16_t)(mesh->pos_up >> 1);
				fwd_neg = (int16_t)(-(mesh->pos_fwd >> 1));
			} else {
				side = (int16_t)(mesh->center_side >> 1);
				up = (int16_t)(mesh->center_up >> 1);
				fwd_neg = (int16_t)(-(mesh->center_fwd >> 1));
			}
			pai_calcrotatedpoint(&objects[pstate.target_obj_idx], side, up, fwd_neg);
			/* Watcom emitted SAR-on-unaligned-dword to extract the model_scale_shift byte
			 * at +0x1E; read the field directly. Shifts run in the unsigned domain
			 * so a negative coordinate doesn't trip the C left-shift UB rule. */
			uint8_t shift = objectblockptr->model_scale_shift;
			rotatedx = (int32_t)((uint32_t)rotatedx << shift);
			rotatedy = (int32_t)((uint32_t)rotatedy << shift);
			rotatedz = (int32_t)((uint32_t)rotatedz << shift);
			sub_world_x = rotatedx + worldlocx - camera.x;
			sub_world_y = worldlocy + rotatedy - camera.y;
			sub_world_z = worldlocz + rotatedz - camera.z;

			/* Cache the exact rotated subsystem offset, not an absolute world
			 * coordinate. The PIP renderer consumes it directly in the target-
			 * local frame. */
			TieHudSnapshot_RecordPipSubsystem(pstate.target_obj_idx);
		}
	}

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	objecteyez = transfm2_geteyez(worldx, worldy, worldz);

	if (pstate.target_obj_idx >= 0x3800u) {
		uint16_t si = pstate.target_obj_idx - 14336;
		uint16_t sc = staticobjects[si].ship_class;
		if (sc >= 8 && sc <= 11) {
			fview_newcalcrotate((int16_t)(staticobjects[si].roll_byte << 8),
								(int16_t)(staticobjects[si].yaw_byte << 8),
								(int16_t)(staticobjects[si].pitch_byte << 8), 0, NULL);
			lightflag = 1;
			static_drawstaticobject(si);
		}
	} else {
		FlightObject* op = &objects[pstate.target_obj_idx];
		switch (op->genus) {
			case GENUS_FIGHTER:
			case GENUS_TRANSPORT:
			case GENUS_UTILITY:
			case GENUS_FREIGHTER:
			case GENUS_STARSHIP:
			case GENUS_PLATFORM:
				craftptr = op->craft_ptr;
				fview_newcalcrotate(op->roll, op->heading, op->pitch, 0, op);
				draw_drawcomplexobject(pstate.target_obj_idx);
				break;
			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC:
				fview_newcalcrotate(op->roll, op->heading, op->pitch, 0, op);
				draw_drawlaser(pstate.target_obj_idx);
				break;
			default:
				break;
		}
	}

	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		objecteyex = transfm2_geteyex(sub_world_x, sub_world_y, sub_world_z);
		objecteyey = transfm2_geteyey(sub_world_x, sub_world_y, sub_world_z);
		objecteyez = transfm2_geteyez(sub_world_x, sub_world_y, sub_world_z);
		int16_t sx = (int16_t)transfm2_getscreencoordx(objecteyex, objecteyez);
		int16_t sy = (int16_t)transfm2_getscreencoordy(objecteyey, objecteyez) - 2;
		panel_drawboxinxtrans((int16_t)(sx - 2), sy, 4, 4, 0xCE);
	}

	deepspacecolor = 48;
	xtrans2_drawxtrans();
	deepspacecolor = (uint8_t)-5;
	logbuf2_finishPIP();

	currenttarget = (uint16_t)save_currenttarget;
	camera.x = save_camera_x;
	camera.y = save_camera_y;
	camera.z = save_camera_z;
	transfm2_screenyoffset = save_transfm2_screenyoffset;
}

// FUNCTION: TIE98 0x467570 PANEL_update3Dcrt
void panel_update3Dcrt_tie98(int x, int y, uint16_t width, uint16_t depth, int clear_runs) {
	int32_t save_screenyoffset = transfm2_screenyoffset;
	int32_t save_camera_x = camera.x;
	int32_t save_camera_y = camera.y;
	int32_t save_camera_z = camera.z;
	uint16_t save_currenttarget = currenttarget;

	transfm2_screenyoffset = 0;
	uint32_t position = rtsvga2_calcpositionVGA_tie98((uint16_t)x, (uint16_t)y);
	logbuf2_startPIP_tie98(width, depth, clear_runs, position);

	if ((uint16_t)clear_runs != 0) {
		const uint8_t* source;
		uint8_t* destination = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			switch (pstate.player_spec_num) {
				case 15:
					source = gunboatcmdmaskdata;
					break;
				case 7:
				case 8:
					source = tieadvcmdmaskdata;
					break;
				case 11:
					source = missileboatcmdmaskdata;
					break;
				default:
					source = cmdmaskdata;
					break;
			}
			memcpy(destination, source, 200);
		} else {
			switch (pstate.player_spec_num) {
				case 15:
					source = gunboatcmd640maskdata;
					break;
				case 7:
					source = tieadv7cmd640maskdata;
					break;
				case 8:
					source = tieadv8cmd640maskdata;
					break;
				case 5:
					source = spec5cmd640maskdata;
					break;
				case 4:
					source = spec4cmd640maskdata;
					break;
				case 11:
					source = missileboatcmd640maskdata;
					break;
				default:
					source = cmd640maskdata;
					break;
			}
			memcpy(destination, source, 480);
		}
	}

	if (g_useHardware3D)
		Renderer_ClearCockpitCrtZBuffer();
	panel_pointcamera_tie98(pstate.target_obj_idx, 1);
	TieHudSnapshot_RecordPipCamera(pstate.target_obj_idx);
	if (pstate.radar_enable)
		currenttarget |= 0x0200u;
	else
		currenttarget |= 0x0400u;
	RenderScene_Initialize_tie98(1);
	numbitmaps = 0;

	worldx = worldlocx - camera.x;
	worldy = worldlocy - camera.y;
	worldz = worldlocz - camera.z;
	int32_t target_world_x = y;
	int32_t target_world_y = y;
	int32_t target_world_z = y;
	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		FlightObject* target = &objects[pstate.target_obj_idx];
		const int mesh_index = pstate.radar_target1;
		calculate_tie98_pip_subsystem_offset(target, mesh_index);
		target_world_x = rotatedx + worldlocx - camera.x;
		target_world_y = rotatedy + worldlocy - camera.y;
		target_world_z = rotatedz + worldlocz - camera.z;
		TieHudSnapshot_RecordPipSubsystem(pstate.target_obj_idx);
	}

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	if (pstate.target_obj_idx >= OBJ_REF_STATIC_BASE) {
		const uint16_t static_index = pstate.target_obj_idx - OBJ_REF_STATIC_BASE;
		StaticObject* object = &staticobjects[static_index];
		if (object->ship_class >= 8 && object->ship_class <= 11) {
			fview_newcalcrotate((int16_t)((uint16_t)object->roll_byte << 8),
								(int16_t)((uint16_t)object->yaw_byte << 8),
								(int16_t)((uint16_t)object->pitch_byte << 8), 0, NULL);
			lightflag = 1;
			static_drawstaticobject_tie98(static_index);
		}
	} else {
		FlightObject* object = &objects[pstate.target_obj_idx];
		switch (object->genus) {
			case GENUS_FIGHTER:
			case GENUS_TRANSPORT:
			case GENUS_UTILITY:
			case GENUS_FREIGHTER:
			case GENUS_STARSHIP:
			case GENUS_PLATFORM:
				craftptr = object->craft_ptr;
				fview_newcalcrotate(object->roll, object->heading, object->pitch, 0, object);
				draw_process_object_components_tie98(pstate.target_obj_idx);
				FlightModel_Draw_Object(object);
				break;
			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC:
				fview_newcalcrotate(object->roll, object->heading, object->pitch, 0, object);
				draw_drawlaser_tie98(pstate.target_obj_idx);
				break;
			default:
				break;
		}
	}

	RenderScene_DrawVisibleFaces();
	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		objecteyex = transfm2_geteyex(target_world_x, target_world_y, target_world_z);
		objecteyey = transfm2_geteyey(target_world_x, target_world_y, target_world_z);
		objecteyez = transfm2_geteyez(target_world_x, target_world_y, target_world_z);
		const int screen_x = transfm2_getscreencoordx(objecteyex, objecteyez);
		const int screen_y = transfm2_getscreencoordy(objecteyey, objecteyez);
		panel_drawboxinxtrans_tie98(screen_x - 2, screen_y - 2, 4, 4, 0xce);
	}
	RenderScene_UnlockSceneBuffers_tie98();
	deepspacecolor = (uint8_t)-5;
	logbuf2_finishPIP_tie98();

	currenttarget = save_currenttarget;
	camera.x = save_camera_x;
	camera.y = save_camera_y;
	camera.z = save_camera_z;
	transfm2_screenyoffset = save_screenyoffset;
}

// FUNCTION: TIE98 0x463400 PANEL_Update3DCrtIfVisible
void PANEL_Update3DCrtIfVisible(void) {
	if (pstate.target_obj_idx == 0xffff)
		return;
	if (pstate.target_obj_idx >= OBJ_REF_STATIC_BASE) {
		StaticObject* object = &staticobjects[pstate.target_obj_idx - OBJ_REF_STATIC_BASE];
		if (object->species == 0 || object->ship_class == 13)
			return;
	} else {
		FlightObject* object = &objects[pstate.target_obj_idx];
		if (object->ship_idx == 0 || object->genus == GENUS_EXPLOSION)
			return;
		if (object->category == 0 &&
			(object->craft_ptr->flight_flag == 3 || object->craft_ptr->flight_flag == 4))
			return;
	}
	if ((pstate.player_craft->status_flags & 4) != 0 && pstate.hyperin_state == 0 && mission.end_flag == 0 &&
		camera.pilotview == 0 && (pstate.player_craft->working_subsystems & 1) != 0) {
		panel_update3Dcrt_tie98(instruments[2].x, instruments[2].y, instruments[2].param1,
								instruments[2].param2, 1);
	}
}

// FUNCTION: TIE98 0x467CE0 PANEL_drawboxinxtrans
int16_t panel_drawboxinxtrans_tie98(int x, int y, int width, int height, uint8_t color) {
	return Hud_DrawBoxInXTrans(x, y, width, height, color, 1);
}

/*
 * panel_drawboxinxtrans -- 4-edge hollow rectangle in the xtrans buffer.
 */
/* flatcolors/flatx/flaty/flatz/flatparentobj/flatcomponentnum/flatobjnum
 * are declared in xtrans2.h (already included). */

// FUNCTION: TIE 0x447F8
void panel_drawboxinxtrans(int16_t left_x, int16_t top_y, uint16_t width, uint16_t height, uint8_t color) {
	uint16_t n = flatobjnum;
	flatcolors[n] = color;
	flatx[n] = 0;
	flaty[n] = 0;
	flatcomponentnum[n] = 0;
	flatz[n] = 0;
	flatparentobj[n] = 0x200;

	if (top_y + (int16_t)height < 0)
		return;
	if (left_x + (int16_t)width < 0)
		return;
	if (left_x >= (int16_t)pixelswide)
		return;
	if (top_y >= (int16_t)pixelsdeep)
		return;

	int16_t lx = (left_x < 0) ? 0 : left_x;
	int16_t top = top_y, span = (int16_t)height;
	if (top_y < 0) {
		top = 0;
		span = top_y + (int16_t)height;
	}
	if (span > 0)
		trace2_enterflatvertical(lx, top, span);

	int16_t xc = (int16_t)(left_x + 1);
	if (xc < 0)
		xc = 0;
	int16_t ty = (int16_t)(top_y + 1);
	int16_t sp = (int16_t)(height - 2);
	if (ty < 0) {
		sp += ty;
		ty = 0;
	}
	if (sp > 0)
		trace2_enterflatvertical(xc, ty, sp);

	if (left_x + (int16_t)width <= (int16_t)pixelswide) {
		int16_t tyb = (int16_t)(top_y + 1);
		int16_t sp2 = (int16_t)(height - 2);
		if (tyb < 0) {
			sp2 += tyb;
			tyb = 0;
		}
		if (sp2 > 0)
			trace2_enterflatvertical((int16_t)(left_x + width), tyb, sp2);

		int16_t rx = (int16_t)(width + left_x + 1);
		if (rx <= (int16_t)pixelswide) {
			int16_t topb = top_y;
			int16_t sp3 = (int16_t)height;
			if (top_y < 0) {
				topb = 0;
				sp3 = top_y + (int16_t)height;
			}
			if (sp3 > 0)
				trace2_enterflatvertical(rx, topb, sp3);
		}
	}
	++flatobjnum;
}

/* RECOVERY HELPER: removes the three repeated wrapped dot-product and clamp
 * blocks in the recovered TIE98 PANEL_pointcamera. */
static int32_t panel_tie98_clamped_dot(int16_t axis_x, int16_t axis_y, int16_t axis_z, int16_t x, int16_t y,
									   int16_t z) {
	uint32_t bits = (uint32_t)((int64_t)axis_z * z);
	bits += (uint32_t)((int64_t)axis_y * y);
	bits += (uint32_t)((int64_t)axis_x * x);
	int64_t value = bits <= INT32_MAX ? bits : (int64_t)bits - 0x100000000LL;
	if (value >= 0x40000000)
		value = 0x3fffffff;
	if (value <= -0x40000000)
		value = -0x3fff0000;
	if (value >= 0)
		return (int32_t)(value / 0x8000);
	return -(int32_t)((-value + 0x7fff) / 0x8000);
}

/* RECOVERY HELPER: removes the repeated defined-C form of TIE98's three
 * wrapped doubled world-coordinate deltas. */
static int32_t panel_tie98_double_delta(int32_t left, int32_t right) {
	const uint32_t bits = ((uint32_t)left - (uint32_t)right) << 1;
	int32_t result;
	memcpy(&result, &bits, sizeof result);
	return result;
}

/* RECOVERY HELPER: removes the repeated arithmetic-right-shift expression
 * used for the three TIE98 camera deltas. */
static int32_t panel_tie98_sar1(int32_t value) {
	return value >= 0 ? value / 2 : -(int32_t)((-(int64_t)value + 1) / 2);
}

static void calculate_tie98_pip_subsystem_offset(FlightObject* target, int mesh_index) {
	const int model_type = target->ship_idx;
	const int target_id = modelmesh_gettargetid(model_type, mesh_index);
	if (target_id != 0 &&
		(target_id != 1 || modelmesh_getobjecttypemeshtype(model_type, mesh_index) == TIE_MESH_MAIN_HULL)) {
		pai_calcrotatedpoint(target, (int16_t)modelmesh_getcomponentfocusx(model_type, mesh_index),
							 (int16_t)modelmesh_getcomponentfocusz(model_type, mesh_index),
							 -modelmesh_getcomponentfocusy(model_type, mesh_index));
	} else if (model_type == 53) {
		pai_calcrotatedpoint(target, (int16_t)panel_tie98_sar1(modelmesh_getcenterx(model_type, mesh_index)),
							 (int16_t)panel_tie98_sar1(modelmesh_getcenterz(model_type, mesh_index)),
							 (int16_t)panel_tie98_sar1(-modelmesh_getcentery(model_type, mesh_index)));
		rotatedx *= 2;
		rotatedy *= 2;
		rotatedz *= 2;
	} else {
		pai_calcrotatedpoint(target, (int16_t)modelmesh_getcenterx(model_type, mesh_index),
							 (int16_t)modelmesh_getcenterz(model_type, mesh_index),
							 -modelmesh_getcentery(model_type, mesh_index));
	}
}

// FUNCTION: TIE98 0x467D10 PANEL_pointcamera
void panel_pointcamera_tie98(uint16_t target_obj, int16_t use_hud_size) {
	FlightObject* player = pstate.player;
	create_getworldposition(target_obj, 0);

	int32_t delta_x = panel_tie98_double_delta(worldlocx, player->world_x);
	int32_t delta_y = panel_tie98_double_delta(worldlocy, player->world_y);
	int32_t delta_z = panel_tie98_double_delta(worldlocz, player->world_z);
	int32_t magnitude_x = (int16_t)((uint32_t)delta_x >> 16);
	int32_t magnitude_y = (int16_t)((uint32_t)delta_y >> 16);
	int32_t magnitude_z = (int16_t)((uint32_t)delta_z >> 16);
	if (magnitude_x < 0)
		magnitude_x = -magnitude_x;
	if (magnitude_y < 0)
		magnitude_y = -magnitude_y;
	if (magnitude_z < 0)
		magnitude_z = -magnitude_z;
	do {
		do {
			magnitude_y = (uint16_t)magnitude_y >> 1;
			magnitude_z = (uint16_t)magnitude_z >> 1;
			magnitude_x = (uint16_t)magnitude_x >> 1;
			delta_x = panel_tie98_sar1(delta_x);
			delta_y = panel_tie98_sar1(delta_y);
			delta_z = panel_tie98_sar1(delta_z);
		} while ((uint16_t)magnitude_x != 0);
	} while ((uint16_t)magnitude_y != 0 || (uint16_t)magnitude_z != 0);

	const int16_t x = (int16_t)panel_tie98_sar1(delta_x);
	const int16_t y = (int16_t)panel_tie98_sar1(delta_y);
	const int16_t z = (int16_t)panel_tie98_sar1(delta_z);
	if (player->orient_dirty) {
		fview_calcrotatemove(player->heading, player->pitch, player);
		fview_calcrotateorient(player->roll, 0, player);
	}
	const int32_t side = panel_tie98_clamped_dot(player->side_x, player->side_y, player->side_z, x, y, z);
	const int32_t forward = panel_tie98_clamped_dot(player->fwd_x, player->fwd_y, player->fwd_z, x, y, z);
	const int32_t up = panel_tie98_clamped_dot(player->up_x, player->up_y, player->up_z, x, y, z);
	trig2_ctop(side, forward, up);
	fview_newcalcview(player->roll, player->heading, player->pitch, 0, (int16_t)(0x4000 - trig2_zangle),
					  trig2_xyangle, NULL);

	uint32_t bound_hwidth;
	if (target_obj >= OBJ_REF_STATIC_BASE) {
		bound_hwidth = species_table[staticobjects[target_obj - OBJ_REF_STATIC_BASE].species].bound_hwidth;
	} else {
		SpecData* spec = &spec_data[objects[target_obj].craft_ptr->species_idx];
		const int16_t bound_x = spec->bound_width;
		const int16_t bound_up = spec->bound_height;
		const int16_t bound_forward = spec->bound_depth;
		if (bound_up >= bound_x) {
			if (bound_x <= bound_forward) {
				bound_hwidth = (uint32_t)(bound_up + bound_forward) >> 1;
			} else if (bound_up > bound_x) {
				bound_hwidth = (uint32_t)(bound_up + bound_x) >> 1;
			} else if (bound_up > bound_forward) {
				bound_hwidth = (uint32_t)(bound_up + bound_x) >> 1;
			} else {
				bound_hwidth = (uint32_t)(bound_x + bound_forward) >> 1;
			}
		} else if (bound_up > bound_forward) {
			bound_hwidth = (uint32_t)(bound_up + bound_x) >> 1;
		} else {
			bound_hwidth = (uint32_t)(bound_x + bound_forward) >> 1;
		}
		bound_hwidth <<= spec->model_scale_shift;
	}

	const uint16_t pixels = use_hud_size                             ? instruments[2].param2
							: flightResolution == TIE_FLIGHT_RES_VGA ? 60
																	 : 144;
	uint32_t distance = (bound_hwidth << perspShift) / pixels;
	uint8_t shift = 0;
	while (distance > 0x3fff) {
		distance >>= 1;
		++shift;
	}
	if (flightResolution != TIE_FLIGHT_RES_VGA)
		distance += distance >> 2;
	const int32_t offset_x = (int32_t)(((int64_t)worldeyeA3 * distance) >> 15);
	const int32_t offset_y = (int32_t)(((int64_t)worldeyeB3 * distance) >> 15);
	const int32_t offset_z = (int32_t)(((int64_t)worldeyeC3 * distance) >> 15);
	camera.x = worldlocx - (int32_t)((uint32_t)offset_x << shift);
	camera.y = worldlocy - (int32_t)((uint32_t)offset_y << shift);
	camera.z = worldlocz - (int32_t)((uint32_t)offset_z << shift);
}

/*
 * panel_pointcamera -- position the 3D CRT's camera to frame the target
 * with auto-zoom sized on bound_hwidth.
 */
// FUNCTION: TIE 0x4499C
void panel_pointcamera(uint16_t target_obj, int16_t use_hud_size) {
	FlightObject* pl = pstate.player;
	create_getworldposition(target_obj, 0);

	int32_t dx = 2 * (worldlocx - pl->world_x);
	int32_t dy = 2 * (worldlocy - pl->world_y);
	int32_t dz = 2 * (worldlocz - pl->world_z);

	/* Track the high-word magnitude of each axis before reducing the vector. */
	int32_t ax = (int16_t)(dx >> 16);
	if (ax < 0)
		ax = -ax;
	int32_t ay = (int16_t)(dy >> 16);
	if (ay < 0)
		ay = -ay;
	int32_t az = (int16_t)(dz >> 16);
	if (az < 0)
		az = -az;

	/* Reduce all axes together until each high-word magnitude fits. */
	while ((ax & 0xFFFF) || (ay & 0xFFFF) || (az & 0xFFFF)) {
		ax >>= 1;
		ay >>= 1;
		az >>= 1;
		dx >>= 1;
		dy >>= 1;
		dz >>= 1;
	}

	int32_t ex = (int16_t)(dx >> 1);
	int32_t ey = (int16_t)(dy >> 1);
	int32_t ez = (int16_t)(dz >> 1);

	/* Project the player->target delta onto the player's body axes.
	 * trig2_ctop(x, y, z) computes xyangle = atan2(x, y) (with a fixed
	 * +90 deg offset), so the bearing-to-target the binary feeds in is
	 * (side, fwd, up) -- not (fwd, side, up). Swapping these two
	 * rotates the PIP camera 90 deg around the player's up axis. */
	int32_t side_proj = (int32_t)pl->side_z * ez + (int32_t)pl->side_y * ey + (int32_t)pl->side_x * ex;
	if (side_proj >= 0x40000000)
		side_proj = 0x3FFF0000;
	if (side_proj <= -0x40000000)
		side_proj = -0x3FFF0000;
	side_proj >>= 15;

	int32_t fwd_proj = (int32_t)pl->fwd_z * ez + (int32_t)pl->fwd_y * ey + (int32_t)pl->fwd_x * ex;
	if (fwd_proj >= 0x40000000)
		fwd_proj = 0x3FFF0000;
	if (fwd_proj <= -0x40000000)
		fwd_proj = -0x3FFF0000;
	fwd_proj >>= 15;

	int32_t up_proj = (int32_t)pl->up_z * ez + (int32_t)pl->up_y * ey + (int32_t)pl->up_x * ex;
	if (up_proj >= 0x40000000)
		up_proj = 0x3FFF0000;
	if (up_proj <= -0x40000000)
		up_proj = -0x3FFF0000;
	up_proj >>= 15;

	trig2_ctop(side_proj, fwd_proj, up_proj);

	fview_newcalcview(pl->roll, pl->heading, pl->pitch, 0, (int16_t)(0x4000 - trig2_zangle),
					  (uint16_t)trig2_xyangle, NULL);

	uint32_t bound_hwidth;
	if (target_obj >= 0x3800u) {
		bound_hwidth = species_table[staticobjects[target_obj - 14336].species].bound_hwidth;
	} else {
		SpecData* sp = &spec_data[objects[target_obj].craft_ptr->species_idx];
		/* Binary picks the two SMALLEST of (bound_width, bound_depth,
		 * bound_height) via HIWORD reads of unaligned dwords starting
		 * at model_scale_shift/bound_width/bound_height (offsets 0xE4/0xE6/0xE8,
		 * each HIWORD lands on the NEXT int16):
		 *   dword @ &model_scale_shift    -> >>16 = bound_width
		 *   dword @ &bound_width  -> >>16 = bound_height
		 *   dword @ &bound_height -> >>16 = bound_depth
		 * so the three cases collapse to: avg(depth,width), avg(width,
		 * height), avg(depth,height). */
		int16_t small_a, small_b;
		if (sp->bound_width > sp->bound_depth || sp->bound_width > sp->bound_height) {
			if (sp->bound_depth > sp->bound_width || sp->bound_depth > sp->bound_height) {
				small_a = sp->bound_depth;
				small_b = sp->bound_width;
			} else {
				small_a = sp->bound_width;
				small_b = sp->bound_height;
			}
		} else {
			small_a = sp->bound_depth;
			small_b = sp->bound_height;
		}
		bound_hwidth = (uint32_t)(((int32_t)small_a + (int32_t)small_b) >> 1) << sp->model_scale_shift;
	}

	uint16_t pix;
	if (use_hud_size)
		pix = instruments[2].param2;
	else if (flightResolution == TIE_FLIGHT_RES_VGA)
		pix = 60;
	else
		pix = 144;

	/* Species 52 uses tighter framing. Static targets never use this adjustment. */
	uint8_t species_special = 0;
	if (target_obj < NUM_CRAFTS && objects[target_obj].craft_ptr &&
		objects[target_obj].craft_ptr->species_idx == 52)
		species_special = 1;
	uint16_t z = (uint16_t)(((bound_hwidth << perspShift) / pix) >> (species_special + 4));
	/* The 640x480 flight modes shift the framing back another 1.25x. */
	if (tie_is_high_resolution_flight())
		z = (uint16_t)((z >> 2) + z);
	uint8_t s = (uint8_t)(species_special + 4);

	/* Back-step along the world-space camera Z basis. */
	int32_t off_x = (worldeyeA3 * z) >> 15;
	int32_t off_y = (worldeyeB3 * z) >> 15;
	int32_t off_z = (worldeyeC3 * z) >> 15;

	/* Shift in the unsigned domain: the binary emits a bitwise SHL on
	 * EDX so a negative offset stays well-defined; signed << of a
	 * negative value is UB in C. */
	camera.x = worldlocx - (int32_t)((uint32_t)off_x << s);
	camera.y = worldlocy - (int32_t)((uint32_t)off_y << s);
	camera.z = worldlocz - (int32_t)((uint32_t)off_z << s);
}

/*
 * panel_AdjustXForRes -- scale X coord from 320-design to current res.
 */
// FUNCTION: TIE 0x44E00
uint16_t panel_AdjustXForRes(uint16_t x) {
	if (flightResolution == TIE_FLIGHT_RES_VGA)
		return x;
	return (uint16_t)(x + x / 2);
}
