#include "tie/panel.h"
#include "../util/binio.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/edition.h"
#include "tie/fediskio.h"
#include "tie/festring.h"
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
#include "tie/panelrts.h"
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
#include "tie_runtime/runtime/panel_view_buffers.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/wide_arithmetic.h"
#include "tie_runtime/snapshot/snapshot_hud.h"
#include "tie_runtime/storage/storage.h"
#include "util/binio.h"

/* ------------------------------------------------------------------ */
/* Module-boundary externs                                            */
/* ------------------------------------------------------------------ */

/* Graphics function pointers (defined in tie.c; tie.h declares only a few). */

/* user.c-owned. */
void user_resetview(void);

/* ------------------------------------------------------------------ */
/* PANEL-owned globals                                                */
/* ------------------------------------------------------------------ */

/* Bytes 2 and 3 are patched for the active cockpit resolution. */
// GLOBAL: TIE95 0xC61A8
// GLOBAL: TIE98 0x4E6D80
char cockpitdir[7] = "CP640/";
/* LFD cockpit-palette section magic. */
// GLOBAL: TIE95 0xC61AF
// GLOBAL: TIE98 0x4E6D88
char xpal_id[5] = "PLTT";
/* shieldcolor[11] -- palette-index ramp for the 4 shield-bar instruments
 * (19..22), indexed by lit-LED count 0..9; entry 10 is the damage-flash
 * override. */
// GLOBAL: TIE95 0xC61B4
// GLOBAL: TIE98 0x4E6D90
char shieldcolor[11] = { 0x2C, 0x35, 0x36, 0x37, 0x39, 0x3A, 0x3B, 0x3D, 0x3E, 0x3F, 0x2F };
/* beamcolors[4] -- palette-index ramp for the 9-LED beam-charge bar
 * (instrument 35), indexed by per-LED fill bucket. Entry 3 is the
 * fully-filled colour; entry 0 is the dim/empty colour. */
// GLOBAL: TIE95 0xC61BF
// GLOBAL: TIE98 0x4E6D9C
char beamcolors[4] = { 0x30, 0x2D, 0x31, 0x32 };

/* CMD-CRT occlusion masks used by panel_update3Dcrt. */
#include "tie_formats/cockpit_masks.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: TIE95 0xD54F8
// GLOBAL: TIE98 0x5FC860
char panelfilename[32];
// GLOBAL: TIE95 0xD5908
// GLOBAL: TIE98 0x5FBFC0
char panelname[32];
// GLOBAL: TIE95 0xD5518
// GLOBAL: TIE98 0x5FC280
PanelViewDef panelviewdefs[PANEL_NUM_VIEWS];

// GLOBAL: TIE95 0xD5928
// GLOBAL: TIE98 0x5FC6A0
PanelViewPtrs panelviewptrs[PANEL_NUM_VIEWS];
// GLOBAL: TIE95 0xD5AB0
// GLOBAL: TIE98 0x5FC000
uint8_t* temppanelptr;
// GLOBAL: TIE95 0xD5B0C
// GLOBAL: TIE98 0x5FC678
int32_t panelsloadedflag;

// GLOBAL: TIE95 0xD5078
// GLOBAL: TIE98 0x5FCD00
RadarBlip rightbliplist1[PANEL_NUM_BLIPS];
// GLOBAL: TIE95 0xD5198
// GLOBAL: TIE98 0x5FCBE0
RadarBlip rightbliplist2[PANEL_NUM_BLIPS];
// GLOBAL: TIE95 0xD52B8
// GLOBAL: TIE98 0x5FC020
RadarBlip leftbliplist2[PANEL_NUM_BLIPS];
// GLOBAL: TIE95 0xD53D8
// GLOBAL: TIE98 0x5FC140
RadarBlip leftbliplist1[PANEL_NUM_BLIPS];
// GLOBAL: TIE95 0xD5AFC
// GLOBAL: TIE98 0x5FC828
RadarBlip* oldrightbliplist;
// GLOBAL: TIE95 0xD5B00
// GLOBAL: TIE98 0x5FBFF4
RadarBlip* oldleftbliplist;
// GLOBAL: TIE95 0xD5B04
// GLOBAL: TIE98 0x5FBFE4
RadarBlip* newrightbliplist;
// GLOBAL: TIE95 0xD5B08
// GLOBAL: TIE98 0x5FC680
RadarBlip* newleftbliplist;

// GLOBAL: TIE95 0xD5B10
// GLOBAL: TIE98 0x5FC8C0
HudInstrument instruments[PANEL_NUM_INSTRUMENTS];
// GLOBAL: TIE95 0xD5D4A
uint16_t oldinstruments[PANEL_NUM_INSTRUMENTS];

// GLOBAL: TIE95 0xD5E08
// GLOBAL: TIE98 0x5FC00C
uint16_t oldleftlistsize;
// GLOBAL: TIE95 0xD5E0A
// GLOBAL: TIE98 0x5FC8A2
int16_t oldbracketx;
// GLOBAL: TIE95 0xD5E0C
// GLOBAL: TIE98 0x5FC8A0
int16_t oldbrackety;
// GLOBAL: TIE95 0xD5E0E
// GLOBAL: TIE98 0x5FC89E
uint16_t newrightlistsize;
// GLOBAL: TIE95 0xD5E10
// GLOBAL: TIE98 0x5FBFE8
uint16_t oldrightlistsize;
// GLOBAL: TIE95 0xD5E12
// GLOBAL: TIE98 0x5FC89A
int16_t radarx;
// GLOBAL: TIE95 0xD5E14
// GLOBAL: TIE98 0x5FC896
int16_t radary;
// GLOBAL: TIE95 0xD5E16
// GLOBAL: TIE98 0x5FCB02
int16_t bracketx;
// GLOBAL: TIE95 0xD5E18
// GLOBAL: TIE98 0x5FCB04
int16_t brackety;
// GLOBAL: TIE95 0xD5E22
// GLOBAL: TIE98 0x5FC67C
uint16_t newleftlistsize;
// GLOBAL: TIE95 0xD5E26
// GLOBAL: TIE98 0x5FBFFE
uint16_t blipcolor;
// GLOBAL: TIE95 0xD5E2A
// GLOBAL: TIE98 0x5FC830
char parts[11];
// GLOBAL: TIE95 0xD5E28
uint16_t lasttargetnum;
// GLOBAL: TIE95 0xD5E24
int16_t lastpilotpaneldraw;

// GLOBAL: TIE95 0xD5E35
uint8_t lockflag;
// GLOBAL: TIE95 0xD5E36
// GLOBAL: TIE98 0x5FC005
uint8_t bracketflag;
// GLOBAL: TIE95 0xD5E37
// GLOBAL: TIE98 0x5FC89C
uint8_t blipboxflag;
// GLOBAL: TIE95 0xD5E3B
// GLOBAL: TIE98 0x5FBFFC
uint8_t blipptrflag;
// GLOBAL: TIE95 0xD5E38
uint8_t initpanelflag;
// GLOBAL: TIE95 0xD5E39
uint8_t searchpartsflag;
// GLOBAL: TIE95 0xD5E3A
uint8_t panelpartsflag;
// GLOBAL: TIE95 0xD5E3C
// GLOBAL: TIE98 0x5FC260
uint8_t panelmirrorflag;

/* String-table pointers (filled by fediskio_loadstringdata). */
// GLOBAL: TIE95 0xD5AC0
// GLOBAL: TIE98 0x5FC008
char** waypointstrings;
// GLOBAL: TIE95 0xD5AEC
// GLOBAL: TIE98 0x5FBFF0
void* diststring;
// GLOBAL: TIE95 0xD5ACC
// GLOBAL: TIE98 0x5FCE20
void* shieldstring;
// GLOBAL: TIE95 0xD5AE8
// GLOBAL: TIE98 0x5FCE24
void* hullstring;
// GLOBAL: TIE95 0xD5AC8
// GLOBAL: TIE98 0x5FBFEC
void* sysstring;
// GLOBAL: TIE95 0xD5AD8
// GLOBAL: TIE98 0x5FCAFC
void* targetstring;
// GLOBAL: TIE95 0xD5AB8
// GLOBAL: TIE98 0x5FC890
void* nonestring;
// GLOBAL: TIE95 0xD5AF4
// GLOBAL: TIE98 0x5FBFF8
void* ourstring;
// GLOBAL: TIE95 0xD5AD4
// GLOBAL: TIE98 0x5FBFE0
void* currentorderstring;
// GLOBAL: TIE95 0xD5AD0
// GLOBAL: TIE98 0x5FC880
void* notargetstring;
// GLOBAL: TIE95 0xD5AE4
// GLOBAL: TIE98 0x5FC8A4
void* curtargetstring;
// GLOBAL: TIE95 0xD5ADC
// GLOBAL: TIE98 0x5FC840
void* curdeststring;
// GLOBAL: TIE95 0xD5AC4
// GLOBAL: TIE98 0x5FC83C
void* distfromtargetstring;
// GLOBAL: TIE95 0xD5AF0
// GLOBAL: TIE98 0x5FC674
void* disttodeststring;
// GLOBAL: TIE95 0xD5AE0
// GLOBAL: TIE98 0x5FC884
void* componentnames;
// GLOBAL: TIE95 0xD5AF8
// GLOBAL: TIE98 0x5FC268
void* timeremstring;
// GLOBAL: TIE95 0xD5ABC
// GLOBAL: TIE98 0x5FC26C
void* timetotargetstring;
// GLOBAL: TIE95 0xD5AB4
// GLOBAL: TIE98 0x5FC888
void* timetodeststring;

/* ================================================================== */
/* Dispatchers                                                        */
/* ================================================================== */

/*
 * panel_initpanel -- reset HUD state, force a full redraw on the next
 * panel_updatepanel call.
 */
// FUNCTION: TIE95 0x3FA80
void panel_initpanel(void) {
	uint16_t i;

	initpanelflag = 1;

	for (i = 0; i < PANEL_NUM_INSTRUMENTS; i++)
		oldinstruments[i] = 0xFFFE;

	replaypercent = 0xFFFE;
	newleftlistsize = 0;
	newrightlistsize = 0;
	bracketflag = 0;
	blipboxflag = 0;
	blipptrflag = 0;
	lasttargetnum = 0xFFFF;

	panel_updatecovers();
	panel_updatecockpitdamage();
	panel_updatepanel();

	initpanelflag = 0;
}

/*
 * panel_updatepanel -- top-of-frame HUD refresh. Validates the current
 * target, then dispatches to one of three render paths.
 */
// FUNCTION: TIE95 0x3FB04
void panel_updatepanel(void) {
	if (pstate.target_obj_idx != 0xFFFF) {
		uint16_t target = pstate.target_obj_idx;

		if (target < 0x3800) {
			/* Live target: drop if dead or exploding, or if it is a
			 * category-0 craft that is departing (flight_flag 3 =
			 * leaving, 4 = gone). */
			if (!objects[target].ship_idx || objects[target].genus == GENUS_EXPLOSION ||
				(objects[target].category == 0 && (objects[target].craft_ptr->flight_flag == 3 ||
												   objects[target].craft_ptr->flight_flag == 4)))
				pstate.target_obj_idx = 0xFFFF;
		} else if (!staticobjects[target - 0x3800].species ||
				   staticobjects[target - 0x3800].ship_class == 13) {
			pstate.target_obj_idx = 0xFFFF;
		}

		if ((pstate.player_craft->status_flags & 4) == 0)
			pstate.target_obj_idx = 0xFFFF;

		if (pstate.target_obj_idx == 0xFFFF) {
			pstate.radar_target0 = target;
			pstate.radar_target2 = 0;
			if (camera.pilotview == 20) {
				if (!replayviewmode) {
					camera.view_zoom_flag = 0;
					lasttargetnum = 0xFFFE;
					camera.view_target_obj = pstate.object_idx;
					camera.view_target_tracking = 0;
					targetblinkflag = 0;
					camera.view_camera_control = 0;
					panelrts_setnewpilotview(0);
					camera.side_angle = 0;
					camera.up_angle = 0;
				}
				msg_messageprintf(MSG_TARGET_LOST);
			}
		}
	}

	if (pstate.hyperin_state)
		return;

	if (camera.pilotview == 0) {
		festring_setfontsize(2);
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
		panel_updateweaponwarnings(0);
		panel_updatereplaystuff();
	} else if (camera.pilotview == 19) {
		panel_updateradar();
		panel_updatelasers();
		panel_updategunsight();
		panel_updateweaponwarnings(1);
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
// FUNCTION: TIE95 0x3FCF4
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
		uint16_t pct;

		festring_setbackcolor(0x40);
		pct = math2_fraction((uint16_t)pstate.player->current_speed, 0x71C7u);
		panel_updatevalue(TIE_HUDI_SPEED_DIGITS, pct, 1);
	}

	panel_updatethrottle();
	panel_updatepower();
	panel_updateweaponwarnings(0);
	panel_updatereplaystuff();
}

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
 * Callers pass a mode in EAX (0 from the full panels, 1 from pilot
 * view 19) that the body never reads.
 */
// FUNCTION: TIE95 0x3FDA4
void panel_updateweaponwarnings(int mode) {
	uint16_t warn_incoming = 0;
	uint16_t warn_lock = 0;

	uint16_t i;
	int16_t best_count;
	uint16_t k;
	uint16_t v44_val;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		CraftData* cp;
		uint8_t fg_status;

		if (!objects[i].ship_idx)
			continue;
		if (objects[i].category != 0)
			continue;
		cp = objects[i].craft_ptr;

		/* Incoming-fire LED: only crafts whose ai_target_ref points at
		 * the player are considered. */
		if ((uint16_t)cp->ai_target_ref == pstate.object_idx) {
			uint8_t g;

			for (g = 0; g < cp->laser_group_cnt; ++g) {
				uint8_t t = cp->laser_type[g];
				if ((t == 0x8B || t == 0x89) && cp->laser_owner_player[g])
					warn_incoming = 1;
			}
		}

		/* Lock-on LED: gated on the FG-status byte at fg_array[].version
		 * != 5 (matches retail's `byte at fg_array[fg_idx]+0x34`). */
		fg_status = fg_array[objects[i].fg_idx].version;
		if (fg_status != 5) {
			uint8_t g;

			for (g = 0; g < cp->weapon_group_cnt; ++g) {
				if (cp->weapon_slots[g].type == 2 && cp->weapon_slots[g].target_obj == pstate.object_idx)
					warn_lock = 1;
			}
		}
	}

	panel_updatelever(TIE_HUDI_WARN_INCOMING, warn_incoming);
	panel_updatelever(TIE_HUDI_WARN_LOCK, warn_lock);

	/* Impact-countdown: max missile_count_total across crafts in
	 * maneuver 23 targeting the player. */
	best_count = 0;
	for (k = 0; k < NUM_CRAFTS; ++k) {
		CraftData* cp;
		int16_t mc;

		if (!objects[k].ship_idx)
			continue;
		if (objects[k].category != 0)
			continue;
		cp = objects[k].craft_ptr;
		if ((uint16_t)cp->ai_target_ref != pstate.object_idx)
			continue;
		if (cp->mode_byte != 23)
			continue;
		mc = (int16_t)cp->missile_count_total;
		if (best_count < mc)
			best_count = mc;
	}

	if (best_count > 944)
		v44_val = 2;
	else if (best_count > 0)
		v44_val = (uint16_t)(((uint16_t)date.subsec / 59u) & 1u);
	else
		v44_val = 0;
	panel_updatelever(TIE_HUDI_WARN_IMPACT, v44_val);
}

/* ================================================================== */
/* Threat-view (pilotview 20)                                         */
/* ================================================================== */

/*
 * panel_updatethreatweapons -- shield/hull pct + 4 ability levers.
 *
 * Levers at 0x49..0x4C convey weapons status of the TARGET (not the
 * player). Blink cadence is (date.subsec / 59) & 1: the quotient toggles
 * four times per mission-second (~250 ms).
 */
// FUNCTION: TIE95 0x3FDC0
void panel_updatethreatweapons(void) {
	CraftData* cp;
	uint16_t shield_avg;
	uint16_t shield_max;
	uint16_t value;
	uint16_t i;
	uint8_t species;

	if (pstate.target_obj_idx < NUM_CRAFTS) {
		cp = objects[pstate.target_obj_idx].craft_ptr;
		shield_avg = cp->forward_shield + cp->rear_shield;
		shield_avg >>= 1;
		species = cp->species_idx;
		shield_max = 2 * spec_data[species].shield_points;
		if (!mission.difficulty) {
			/* Easy difficulty: side 1 gets 3x shield points, sides 0/4
			 * get 1.25x (2 * 0.625). */
			if (objects[pstate.target_obj_idx].side == 1) {
				int shield_points = spec_data[species].shield_points;
				shield_max = 2 * (shield_points + (shield_points >> 1));
			} else if (objects[pstate.target_obj_idx].side == 0 || objects[pstate.target_obj_idx].side == 4) {
				shield_max = 2 * math2_fraction(spec_data[cp->species_idx].shield_points, 0xA000u);
			}
		}
		if (shield_max) {
			value = math2_percentage(shield_avg, shield_max);
			value /= 0x28F;
			value *= 2;
		} else {
			value = 0;
		}
	} else {
		value = 0;
	}
	panel_updatevalue(TIE_HUDI_THREAT_SHIELD_PCT, value, 1);

	if (pstate.target_obj_idx < NUM_CRAFTS) {
		value = math2_percentage(cp->hull_max - cp->hull_damage, cp->hull_max);
		value /= 0x28F;
	} else
		value = 0;
	panel_updatevalue(TIE_HUDI_THREAT_HULL_PCT, value, 1);

	/* Levers 0x49..0x4C: ion, torpedo, missile, beam. Weapons owned by
	 * the player blink with cadence (date.subsec / 59) & 1. */
	value = 0;
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		for (i = 0; i < cp->laser_group_cnt; ++i) {
			if (cp->laser_type[i] == 139 || cp->laser_type[i] == 137) {
				if (cp->laser_owner_player[i])
					value = (date.subsec / 59 & 1) + 1;
				else
					value = 1;
			}
		}
	}
	panel_updatelever(TIE_HUDI_THREAT_ION, value);

	value = 0;
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		for (i = 0; i < cp->laser_group_cnt; ++i) {
			if (cp->laser_type[i] == 141) {
				if (cp->laser_owner_player[i])
					value = (date.subsec / 59 & 1) + 1;
				else
					value = 1;
			}
		}
	}
	panel_updatelever(TIE_HUDI_THREAT_TORP, value);

	value = 0;
	if (pstate.target_obj_idx < NUM_CRAFTS && cp->mode_byte == 23) {
		for (i = 0; i < cp->missile_group_cnt; ++i) {
			if (cp->warhead_type[i]) {
				if ((int16_t)cp->missile_count_total > 0)
					value = (date.subsec / 59 & 1) + 1;
				else
					value = 1;
			}
		}
	}
	panel_updatelever(TIE_HUDI_THREAT_MISSILE, value);

	value = 0;
	if (pstate.target_obj_idx < NUM_CRAFTS && cp->beam_type)
		value = 1;
	panel_updatelever(TIE_HUDI_THREAT_BEAM, value);
}

/*
 * panel_updateradar -- diff-draw radar blips + target bracket.
 */
// FUNCTION: TIE95 0x3FE50
void panel_updateradar(void) {
	uint16_t i;
	uint16_t j;
	uint16_t k;
	uint16_t static_obj;

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
	for (i = 0; i < NUM_CRAFTS; ++i) {
		if (i == pstate.object_idx)
			continue;
		if (!(species_table[objects[i].ship_idx].side & 1))
			continue;
		if (objects[i].craft_ptr->flight_flag == 3)
			continue;
		panel_addbliptoradar(i);
	}

	/* Warheads [NUM_CRAFTS..WARHEAD_SLOT_END). */
	for (j = NUM_CRAFTS; j < WARHEAD_SLOT_END; ++j) {
		if (!(species_table[objects[j].ship_idx].side & 1))
			continue;
		panel_addbliptoradar(j);
	}

	/* Static objects (mapped to 0x3800..0x383F). */
	static_obj = 0x3800;
	for (k = 0; k < 0x40; ++static_obj, ++k) {
		if (!(species_table[staticobjects[k].species].side & 1))
			continue;
		panel_addbliptoradar(static_obj);
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
	if (pstate.target_obj_idx != 0xFFFF) {
		rtsvga2_drawbracket();
		bracketflag = 1;
	} else {
		bracketflag = 0;
	}
	blipptrflag ^= 1u;
}

/*
 * panel_addbliptoradar -- project one target into the radar display.
 *
 * TIE95 uses cached eye coordinates for craft and downscaled world coordinates
 * for other objects. TIE98 rotates every target's current full world position.
 */
// FUNCTION: TIE95 0x400AC
// FUNCTION: TIE98 0x4637D0
void panel_addbliptoradar(uint16_t target_obj) {
	int32_t eye_x, eye_y_neg, eye_z;
	int is_forward;
#ifdef TIE_MODERN
	int16_t blip_off_x;
	int16_t blip_off_y;
#endif

	if (TIE_FLIGHT_TIE98) {
		int32_t dx_world, dy_world, dz_world;

		if (target_obj >= 0x3800) {
			dx_world = (int32_t)staticobjects[target_obj - 0x3800].world_x << 8;
			dy_world = (int32_t)staticobjects[target_obj - 0x3800].world_y << 8;
			dz_world = (int32_t)staticobjects[target_obj - 0x3800].world_z << 8;
		} else {
			dx_world = objects[target_obj].world_x;
			dy_world = objects[target_obj].world_y;
			dz_world = objects[target_obj].world_z;
		}
		dx_world -= pstate.player->world_x;
		dy_world -= pstate.player->world_y;
		dz_world -= pstate.player->world_z;

		if (pstate.player->orient_dirty) {
			fview_calcrotatemove(pstate.player->pitch, pstate.player->heading, pstate.player);
			fview_calcrotateorient(pstate.player->roll, 0, pstate.player);
		}

		eye_z = math2_mul_q15(dx_world, pstate.player->fwd_x) +
				math2_mul_q15(dy_world, pstate.player->fwd_y) + math2_mul_q15(dz_world, pstate.player->fwd_z);
		eye_x = math2_mul_q15(dx_world, pstate.player->side_x) +
				math2_mul_q15(dy_world, pstate.player->side_y) +
				math2_mul_q15(dz_world, pstate.player->side_z);
		eye_y_neg =
			-(math2_mul_q15(dx_world, pstate.player->up_x) + math2_mul_q15(dy_world, pstate.player->up_y) +
			  math2_mul_q15(dz_world, pstate.player->up_z));
	} else if (target_obj < NUM_CRAFTS) {
		/* Eye-space (camera-space) position cached every frame by
		 * tie_getobjecteyexyz via tie_updatescreen. */
		eye_x = objects[target_obj].craft_ptr->eye_x_cache;
		eye_y_neg = objects[target_obj].craft_ptr->eye_y_cache;
		eye_z = objects[target_obj].craft_ptr->eye_z_cache;
	} else {
		int16_t dy, dx, dz;

		if (target_obj >= 0x3800) {
			dx = staticobjects[target_obj - 0x3800].world_x;
			dy = staticobjects[target_obj - 0x3800].world_y;
			dz = staticobjects[target_obj - 0x3800].world_z;
			dx -= (int16_t)(pstate.player->world_x >> 8);
			dy -= (int16_t)(pstate.player->world_y >> 8);
			dz -= (int16_t)(pstate.player->world_z >> 8);
		} else {
			dx = (objects[target_obj].world_x - pstate.player->world_x) >> 8;
			dy = (objects[target_obj].world_y - pstate.player->world_y) >> 8;
			dz = (objects[target_obj].world_z - pstate.player->world_z) >> 8;
		}

		if (pstate.player->orient_dirty) {
			fview_calcrotatemove(pstate.player->pitch, pstate.player->heading, pstate.player);
			fview_calcrotateorient(pstate.player->roll, 0, pstate.player);
		}

		/* Rotate by player orientation: dot product with (fwd/side/up). */
		eye_z = math2_mul16_q15(dx, pstate.player->fwd_x) + math2_mul16_q15(dy, pstate.player->fwd_y) +
				math2_mul16_q15(dz, pstate.player->fwd_z);
		eye_x = math2_mul16_q15(dx, pstate.player->side_x) + math2_mul16_q15(dy, pstate.player->side_y) +
				math2_mul16_q15(dz, pstate.player->side_z);
		eye_y_neg = -(math2_mul16_q15(dx, pstate.player->up_x) + math2_mul16_q15(dy, pstate.player->up_y) +
					  math2_mul16_q15(dz, pstate.player->up_z));
	}

	if (eye_z < 0) {
		eye_z = -eye_z;
		is_forward = 0;
	} else {
		is_forward = 1;
	}

	/* Pick base colour. */
	if (target_obj >= 0x3800) {
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
	if (roughdistance > 122166) {
		if (blipcolor == 47)
			blipcolor = 45;
		else if (blipcolor == 209)
			blipcolor += 2;
		else
			blipcolor -= 2;
	} else if (roughdistance > 61083) {
		if (blipcolor == 47)
			blipcolor = 46;
		else if (blipcolor == 209)
			blipcolor++;
		else
			blipcolor--;
	}

	math2_getradarcoord(eye_x, eye_y_neg, eye_z);

#ifdef TIE_MODERN
	/* radarx/radary at this point = signed classic-px offset from the
	 * radar disc center (math2 clipped to the disc boundary). Capture
	 * the pre-anchor values for the HD snapshot's anchor-relative
	 * blip / bracket fields; the engine path below adds the disc
	 * anchor in-place to produce the absolute classic coords it
	 * draws to the FB. */
	blip_off_x = radarx;
	blip_off_y = radary;
#endif

	if (is_forward) {
		radary += instruments[TIE_HUDI_RADAR_LEFT].y;
		radarx += instruments[TIE_HUDI_RADAR_LEFT].x;
		newleftbliplist[newleftlistsize].x = radarx;
		newleftbliplist[newleftlistsize].y = radary;
		newleftbliplist[newleftlistsize].color = blipcolor;
#ifdef TIE_MODERN
		TieHudSnapshot_RecordRadarBlip(true, newleftlistsize, (uint8_t)blipcolor, blip_off_x, blip_off_y);
#endif
		newleftlistsize++;
		if (newleftlistsize == 48)
			newleftlistsize--;
	} else {
		radarx += instruments[TIE_HUDI_RADAR_RIGHT].x;
		radary += instruments[TIE_HUDI_RADAR_RIGHT].y;
		newrightbliplist[newrightlistsize].x = radarx;
		newrightbliplist[newrightlistsize].y = radary;
		newrightbliplist[newrightlistsize].color = blipcolor;
#ifdef TIE_MODERN
		TieHudSnapshot_RecordRadarBlip(false, newrightlistsize, (uint8_t)blipcolor, blip_off_x, blip_off_y);
#endif
		newrightlistsize++;
		if (newrightlistsize == 48)
			newrightlistsize--;
	}

	if (target_obj == pstate.target_obj_idx) {
		bracketx = radarx;
		brackety = radary;
#ifdef TIE_MODERN
		TieHudSnapshot_RecordRadarBracket(is_forward, blip_off_x, blip_off_y);
#endif
	}
}

/*
 * panel_updatecmd -- center-console target CRT + textual target info.
 * Implements the full target-change invalidation + 5 data lines
 * (shield/hull/dist/system/cargo + subsystem focus).
 */
// FUNCTION: TIE95 0x40530
void panel_updatecmd(void) {
	uint16_t focus;
	uint16_t shield_pct;
	int name_width;
	uint16_t shield_max;
	const uint8_t* cargo_str;
	uint16_t model_type;
	int text_width;
	CraftData* tgt;
	uint16_t alive;
	uint16_t hull_pct;
	uint16_t sys_pct;
	int16_t cargo_kind;
	uint16_t shield_avg;
	uint16_t capable;
	uint16_t x;
	int force_redraw;
	uint16_t sum;
	uint16_t cap_mask;
	uint16_t y;
	uint16_t stat_mask;
	uint16_t b;

	dropflag = 0;
	if (mission.train_craft_type) {
		gate_trainingupdatecrt(instruments[2].x, instruments[2].y);
		return;
	}

	if ((pstate.player_craft->working_subsystems & 1) == 0)
		return;

	force_redraw = 0;

	switch (flightResolution) {
		case TIE_FLIGHT_RES_VGA:
			text_width = 40;
			name_width = 80;
			break;
		default:
			text_width = 70;
			name_width = 160;
			break;
	}

	if (lasttargetnum != pstate.target_obj_idx) {
		uint16_t prev_target = lasttargetnum;
		oldinstruments[45] = -1;
		oldinstruments[58] = -1;
		oldinstruments[59] = -1;
		oldinstruments[60] = -1;
		oldinstruments[61] = -1;
		oldinstruments[62] = -1;
		oldinstruments[63] = -1;
		oldinstruments[64] = -1;
		lasttargetnum = pstate.target_obj_idx;
		oldinstruments[65] = -1;
		festring_setfontsize(2);
		festring_setbackcolor(0x30);
		festring_setautofill(1);
		force_redraw = 1;

		if (prev_target == 0xFFFF) {
			/* Paint static labels. Engine picks color 0x45 (VGA) or
			 * 0x46 (SVGA) — see PANEL_updatecmd at 0x40696. The two
			 * remap to different physical palette entries so SVGA
			 * fidelity needs the 0x46 branch. */
			festring_setbound(0, 0, (uint16_t)screenXRes, (uint16_t)screenYRes);
			festring_setcursor(instruments[88].x, instruments[88].y);
			if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
				festring_settextcolor(0x45);
			else
				festring_settextcolor(0x46);
			festring_outstring((const uint8_t*)shieldstring);
			festring_setcursor(instruments[61].x + sys2_calclength((uint8_t*)"   "), instruments[61].y);
			outchar('%');

			festring_setcursor(instruments[89].x, instruments[89].y);
			festring_outstring((const uint8_t*)hullstring);
			festring_setcursor(instruments[62].x + sys2_calclength((uint8_t*)"   "), instruments[62].y);
			outchar('%');

			festring_setcursor(instruments[87].x, instruments[87].y);
			festring_outstring((const uint8_t*)diststring);
			festring_setcursor(instruments[59].x + sys2_calclength((uint8_t*)"  "), instruments[59].y);
			outchar('.');

			festring_setcursor(instruments[86].x, instruments[86].y);
			festring_outstring((const uint8_t*)sysstring);
			festring_setcursor(instruments[58].x + sys2_calclength((uint8_t*)"   "), instruments[58].y);
			outchar('%');

#ifdef TIE_MODERN
			{
				uint8_t label_color = (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA) ? 0x45 : 0x46;
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
#endif
		}

		if (pstate.target_obj_idx != 0xFFFF) {
			if (pstate.target_obj_idx < 0x3800)
				model_type = objects[pstate.target_obj_idx].ship_idx;
			else
				model_type = staticobjects[pstate.target_obj_idx - 0x3800].species;

			/* Target-name field. */
			x = instruments[90].x;
			y = instruments[90].y;
			festring_setbound(x, y, x + name_width, y + fontheight + 1);
			clearwindow();
			panel_buildobjectname(pstate.target_obj_idx, 3);
			festring_setcursor(x, y);
			festring_outstringcenter((const uint8_t*)tempstring);

			/* Warhead targets show their current target in the
			 * component-name field.
			 *   - homing warhead aimed at someone else → that target's
			 *     FG name via panel_buildobjectname(target, 2)
			 *   - homing warhead aimed at the player    → `ourstring`
			 *     (the "us" string)
			 *   - non-homing warhead                    → componentnames[32]
			 *     (the same fallback the cargo line uses) */
			if (pstate.target_obj_idx >= NUM_CRAFTS && pstate.target_obj_idx < WARHEAD_SLOT_END &&
				projectile_is_warhead_type[model_type - WEAPON_SPECIES_BASE]) {
				const WarheadRecord* wh = (const WarheadRecord*)objects[pstate.target_obj_idx].craft_ptr;
				if (wh->homing_tier) {
					if (wh->target_obj == pstate.object_idx)
						festring_farstrcpy((const char*)ourstring);
					else
						panel_buildobjectname(wh->target_obj, 2);
				} else {
					festring_farstrcpy((const char*)((char**)componentnames)[32]);
				}
				x = instruments[65].x;
				y = instruments[65].y;
				festring_setbound(x, y, x + text_width, y + fontheight + 1);
				clearwindow();
				festring_setcursor(x, y);
				festring_settextcolor(0x4E);
				festring_outstringright((const uint8_t*)tempstring);
#ifdef TIE_MODERN
				{
					TieHudState* hud = TieSnapshotBuilder_HudMut();
					TieHudSnapshot_CopyText(hud->target_subsystem_text, sizeof hud->target_subsystem_text,
											(const uint8_t*)tempstring);
					hud->instruments[65].color = 0x4E;
				}
#endif
			}
		} else {
			panel_updatelever(TIE_HUDI_DAMAGE_CRACK_FIRST, 0);
		}
	}

	if (pstate.target_obj_idx == 0xFFFF)
		return;

	festring_setbackcolor(0x30);
	/* TIE98 renders the CRT later from TIE_Update_Screen for both backends. */
	if (!TIE_FLIGHT_TIE98) {
		panel_update3Dcrt(instruments[2].x, instruments[2].y, instruments[2].param1, instruments[2].param2,
						  force_redraw);
	}

	tgt = (pstate.target_obj_idx < 0x3800) ? objects[pstate.target_obj_idx].craft_ptr : NULL;

	/* Shield % (0x3D). Easy difficulty: side 1 gets 3x shield points,
	 * sides 0/4 get 1.25x. */
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		sum = tgt->forward_shield + tgt->rear_shield;
		shield_max = 2 * spec_data[tgt->species_idx].shield_points;
		shield_avg = sum;

		shield_avg >>= 1;
		if (!mission.difficulty) {
			if (objects[pstate.target_obj_idx].side == 1) {
				int shield_points = spec_data[tgt->species_idx].shield_points;
				shield_max = 2 * (shield_points + (shield_points >> 1));
			} else if (objects[pstate.target_obj_idx].side == 0 || objects[pstate.target_obj_idx].side == 4) {
				shield_max = math2_fraction(2 * spec_data[tgt->species_idx].shield_points, 0xA000u);
			}
		}
		if (shield_max) {
			shield_pct = math2_percentage(shield_avg, shield_max);
			shield_pct /= 0x28F;
			shield_pct *= 2;
			if (sum && !shield_pct)
				shield_pct = 1;
		} else {
			shield_pct = 0;
		}
	} else {
		shield_pct = 0;
	}
	panel_updatevalue(TIE_HUDI_TARGET_SHIELD_PCT, shield_pct, 1);

	/* Hull % (0x3E). */
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		if (tgt->hull_damage > tgt->hull_max) {
			hull_pct = 1;
		} else {
			hull_pct = math2_percentage(tgt->hull_max - tgt->hull_damage, tgt->hull_max);
			hull_pct /= 0x28F;
			if (!hull_pct)
				hull_pct = 1;
		}
	} else {
		hull_pct = 100;
	}
	panel_updatevalue(TIE_HUDI_TARGET_HULL_PCT, hull_pct, 1);

	/* Subsystem % (0x3A). */
	if (pstate.target_obj_idx < NUM_CRAFTS) {
		capable = 0;
		alive = 0;
		cap_mask = tgt->subsystem_active;
		stat_mask = tgt->status_flags;

		for (b = 0; b < 16; ++b) {
			if (cap_mask & 1)
				++capable;
			if (stat_mask & 1)
				++alive;
			cap_mask >>= 1;
			stat_mask >>= 1;
		}
		if (!capable)
			sys_pct = 0;
		else
			sys_pct = 100 * alive / capable;
		if (sys_pct > 25 && tgt->ion_drain_timer)
			sys_pct = 25;
	} else if (pstate.target_obj_idx < 0x3800 || staticobjects[pstate.target_obj_idx - 0x3800].status_flags) {
		sys_pct = 100;
	} else {
		sys_pct = 0;
	}
	panel_updatevalue(TIE_HUDI_TARGET_SUBSYSTEM_PCT, sys_pct, 1);

	pai_distancebetween(pstate.object_idx, pstate.target_obj_idx);
	panel_outputdistance(trig2_polardistance);

	/* Cargo display (0x3F -> instrument[63]). Initial string is
	 * componentnames[32] (retail @ 0x40c87..0x40c99); the fighter
	 * if-body overrides for craft targets only. */
	cargo_kind = 2;
	cargo_str = (const uint8_t*)((char**)componentnames)[32];
	if (pstate.target_obj_idx < NUM_CRAFTS && !objects[pstate.target_obj_idx].category) {
		if (tgt->inspected) {
			cargo_str = (const uint8_t*)tgt->cargo;
			cargo_kind = 1;
			if (!cargo_str[0]) {
				cargo_kind = 2;
				cargo_str = (const uint8_t*)nonestring;
			}
		} else {
			cargo_str = (const uint8_t*)unknownstring;
			cargo_kind = 0;
		}
	}
	if (cargo_kind != (int16_t)oldinstruments[63]) {
		oldinstruments[63] = cargo_kind;
		x = instruments[63].x;
		y = instruments[63].y;
		festring_setbound(x, y, x + text_width, y + fontheight + 1);
		clearwindow();
		festring_setcursor(x, y);
		festring_settextcolor(0x46);
		festring_outstringright(cargo_str);
#ifdef TIE_MODERN
		{
			TieHudState* hud = TieSnapshotBuilder_HudMut();
			TieHudSnapshot_CopyText(hud->target_cargo, sizeof hud->target_cargo, cargo_str);
			hud->instruments[63].color = 0x46;
		}
#endif
	}

	/* Subsystem focus (instrument[65]). */
	if (pstate.target_obj_idx >= NUM_CRAFTS && pstate.target_obj_idx < 0x3800)
		return;

	if (pstate.target_obj_idx < NUM_CRAFTS)
		focus = pstate.radar_target1;
	else
		focus = 40;
	festring_settextcolor(0x4E);
	if (focus != oldinstruments[65]) {
		const uint8_t* s;

		oldinstruments[65] = focus;
		x = instruments[65].x;
		y = instruments[65].y;
		festring_setbound(x, y, x + text_width, y + fontheight + 1);
		clearwindow();
		festring_setcursor(x, y);

		if (focus == 40) {
			s = ((const uint8_t**)componentnames)[32];
		} else {
			uint16_t mt;

			if (!TIE_FLIGHT_TIE98)
				draw_Lockshipfileptrs(objects[pstate.target_obj_idx].ship_idx);
			mt = TIE_FLIGHT_EDITION(
				componentblockptr[(uint16_t)pstate.radar_target1].mesh_type,
				modelmesh_gettype(objects[pstate.target_obj_idx].ship_idx, pstate.radar_target1));
			/* Fighters display mesh type 7 with component label 26. */
			if (objects[pstate.target_obj_idx].genus == GENUS_FIGHTER && mt == 7)
				mt = 26;
			s = ((const uint8_t**)componentnames)[mt];
		}
		festring_outstringright(s);
#ifdef TIE_MODERN
		{
			TieHudState* hud = TieSnapshotBuilder_HudMut();
			TieHudSnapshot_CopyText(hud->target_subsystem_text, sizeof hud->target_subsystem_text, s);
			hud->instruments[65].color = 0x4E;
		}
#endif
	}
}

/*
 * panel_buildobjectname -- format target name into tempstring using
 * FESTRING color-escape prefixes (0xFE). flags bit 0 = ship name,
 * bit 1 = FG name + group number suffix.
 */
// FUNCTION: TIE95 0x40E94
void panel_buildobjectname(uint16_t target_obj, uint16_t flags) {
	uint16_t ship_idx;
	CraftData* cp;
	uint8_t fg_side;

	tempstring[0] = 0;

	if (target_obj < 0x3800) {
		ship_idx = objects[target_obj].ship_idx;

		festring_farstradd((char)0xFE);
		if (objects[target_obj].side == 0)
			festring_farstradd(0x51);
		else if (objects[target_obj].side == 1 || objects[target_obj].side == 4)
			festring_farstradd(0x49);
		else if (objects[target_obj].side == 2)
			festring_farstradd(0x45);
		else
			festring_farstradd(0x55);

		if (!objects[target_obj].category) {
			cp = objects[target_obj].craft_ptr;
			if (flags & 1)
				festring_farstrcat(spec_data[cp->species_idx].short_name);

			if ((flags & 3) == 3) {
				festring_farstradd(':');
				festring_farstradd(' ');
			}

			if (flags & 2) {
				festring_farstradd((char)0xFE);
				if (objects[target_obj].side == 0)
					festring_farstradd(0x52);
				else if (objects[target_obj].side == 1 || objects[target_obj].side == 4)
					festring_farstradd(0x4A);
				else if (objects[target_obj].side == 2)
					festring_farstradd(0x46);
				else
					festring_farstradd(0x56);
				festring_farstrcat(fg_array[objects[target_obj].fg_idx].name);

				/* Multi-craft FG: append the 1-based craft index. */
				if ((int8_t)fg_array[objects[target_obj].fg_idx].count > 1) {
					festring_farstradd(' ');
					festring_farstradd((char)(cp->craft_idx_in_fg + '1'));
				}
			}
			return;
		}

		if ((flags & 1) == 0)
			return;
		if (ship_idx >= 0x8F && ship_idx <= 0x9A)
			festring_farstrcat(((char**)warheadstrings)[ship_idx - 0x8F]);
		else if (ship_idx >= 0x46 && ship_idx <= 0x54)
			festring_farstrcat(((char**)buoystr)[ship_idx - 70]);
	} else if (target_obj < 0x8000) {
		target_obj -= 0x3800;
		ship_idx = staticobjects[target_obj].species;

		festring_farstradd((char)0xFE);
		fg_side = fg_array[staticobjects[target_obj].fg_idx].side;
		if (fg_side == 0)
			festring_farstradd(0x51);
		else if (fg_side == 1 || fg_side == 4)
			festring_farstradd(0x49);
		else if (fg_side == 2)
			festring_farstradd(0x45);
		else
			festring_farstradd(0x55);

		if ((flags & 1) && ship_idx >= 0x46 && ship_idx <= 0x55)
			festring_farstrcat(((char**)buoystr)[ship_idx - 70]);

		if ((flags & 3) == 3) {
			festring_farstradd(':');
			festring_farstradd(' ');
		}

		if (flags & 2) {
			festring_farstradd((char)0xFE);
			if (fg_side == 0)
				festring_farstradd(0x52);
			else if (fg_side == 1 || fg_side == 4)
				festring_farstradd(0x4A);
			else if (fg_side == 2)
				festring_farstradd(0x46);
			else
				festring_farstradd(0x56);
			festring_farstrcat(fg_array[staticobjects[target_obj].fg_idx].name);
		}
	} else if (flags & 1) { /* waypoint: ref with high bit set */
		festring_farstradd((char)0xFE);
		festring_farstradd('C');
		target_obj += 0x8000; /* clear msb */
#ifdef TIE_MODERN
		if (waypointstrings && waypointstrings[target_obj])
#endif
			festring_farstrcat(waypointstrings[target_obj]);
	}
}

/*
 * panel_getcraftstatus -- status code for the target-CRT color.
 */
// FUNCTION: TIE95 0x41288
uint16_t panel_getcraftstatus(uint16_t target_obj) {
	CraftData* cp = objects[target_obj].craft_ptr;
	unsigned order;

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
	order = cp->current_order;
	if (order == 44 || order == 64)
		return 8;
	return 0;
}

/*
 * panel_outputdistance -- polar_dist (Q? fixed-point) -> km.cm at
 * instruments 0x3B / 0x3C. Clamped to <= 9999.99 km.
 */
// FUNCTION: TIE95 0x41334
void panel_outputdistance(int32_t polar_dist) {
	uint16_t scaled;
	uint16_t km;

	scaled = (uint32_t)polar_dist * 161 >> 16;
	if (scaled >= 10000)
		scaled = 9999;
	km = scaled / 100;
	panel_updatevalue(0x3B, km, 1);
	panel_updatevalue(0x3C, scaled - km * 100, 2);
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
// FUNCTION: TIE95 0x413A0
void panel_updategunsight(void) {
	uint16_t st;
	if (!pstate.player_weapon_mode) {
		if (lockflag) {
			st = 4;
		} else {
			st = 0;
		}
	} else {
		if (pstate.target_obj_idx != 0xFFFF) {
			st = pstate.radar_subtarget_state + 1;
		} else {
			st = 1;
		}
		if (pstate.radar_subtarget_state == 2) {
			lockflag = 1;
		} else {
			lockflag = 0;
		}
	}
	fsfx_triggergunsightsfx(st);
	panel_updatelever(TIE_HUDI_GUNSIGHT, st);
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
// FUNCTION: TIE95 0x41418
void panel_updatelasers(void) {
	uint16_t species_idx;
	uint16_t g;
	uint16_t weapon_groups;
	uint16_t status;
	uint16_t lever;

	lockflag = 0;

	species_idx = pstate.player_craft->species_idx;
	weapon_groups = pstate.player_craft->weapon_group_cnt;

	for (g = 0; g < weapon_groups; ++g) {
		uint16_t bank = g > spec_data[species_idx].laser_end[0];
		int16_t x0 = instruments[g + 3].x;
		uint16_t y0 = instruments[g + 3].y;
		uint16_t shape_base;
		int16_t charge;
		uint16_t led_count;
		uint16_t empty_frame;
		uint16_t filled_frame;

		if (!(x0 + y0))
			continue;

		shape_base = instruments[g + 3].param1;
		charge = (int8_t)pstate.player_craft->weapon_slots[g].charge;

		if (!camera.pilotview && (pstate.player_craft->working_subsystems & 2) &&
			(pstate.player_craft->working_subsystems & 4)) {
			if (charge <= 0 || !(pstate.player_craft->status_flags & 0x10)) {
				filled_frame = empty_frame = led_count = 0;
			} else {
				led_count = ++charge;
				if (charge <= 64) {
					empty_frame = 0;
					filled_frame = 1;
				} else {
					empty_frame = 1;
					filled_frame = 2;
					led_count = charge - 64;
				}
				led_count /= 6;
				if (led_count >= 11)
					led_count = 10;
			}

			if (led_count != oldinstruments[g + 3]) {
				int step;
				uint16_t flip;
				uint16_t l;

				oldinstruments[g + 3] = led_count;
				if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
					step = 3;
				else
					step = 6;
				if (instruments[g + 3].param2) {
					flip = 1;
					step = -step;
				} else {
					flip = 0;
				}
				for (l = 0; l < 10; ++l) {
					uint16_t frame = (l < led_count) ? filled_frame : empty_frame;
					drawshape(farbufferptrs[frame + shape_base], x0 + l * step, y0, 253, flip);
				}

#ifdef TIE_MODERN
				/* `value` = lit-LED count (0..10); `color` overloads as
				 * the filled-frame index (1 normal-charge, 2 overcharge,
				 * 0 = no charge). HD reads both and reproduces the same
				 * 10-cel paint without re-deriving from `charge`. */
				TieHudSnapshot_RecordLaserCharge((uint16_t)(g + 3), (int16_t)led_count,
												 (uint8_t)filled_frame);
#endif
			}
		}

		/* The active weapon bank gates the lever; uncharged, missile-mode,
		 * and wrong-bank groups show the off state. */
		if (charge <= 0 || !(pstate.player_craft->status_flags & 0x10)) {
			status = 0;
			lever = 0;
		} else if (!pstate.player_weapon_mode && pstate.player_weapon_group == bank) {
			switch (pstate.player_craft->laser_owner_player[bank]) {
				case 3:
					status = 3;
					break;
				case 1:
					if (pstate.player_craft->laser_first_slot[bank] == g)
						status = 3;
					else
						status = 1;
					break;
				case 2:
					if (pstate.player_craft->laser_first_slot[bank] == g)
						status = 3;
					else if (weapon_groups >= 4 && pstate.player_craft->laser_first_slot[bank] + 2 == g)
						status = 3;
					else
						status = 1;
					break;
			}
			lever = status;

			/* Cooldown gating: bank-indexed (laser_cooldown[bank]).
			 * Retail downgrades the lever from 3 to 2 ("lock pending"). */
			if (lever == 3 && pstate.player_craft->laser_cooldown[bank]) {
				status = 5;
				lever = 2;
			}
		} else {
			status = 1;
			lever = 0;
		}
		if (filled_frame == 2)
			++status;

		panel_updatelever(g + 37, lever);

		/* In-range green-light: probe each active-bank group with its own
		 * hardpoint (hp[g]), not just hp[bank]. */
		if ((pstate.player_craft->status_flags & 4) && lever == 3) {
			if (pstate.target_obj_idx != 0xFFFF &&
				collide_targetinrange(pstate.object_idx, pstate.target_obj_idx, g)) {
				lockflag = 1;
				lever = 2;
			} else {
				lever = 1;
			}
		} else if (!(pstate.player_craft->status_flags & 4)) {
			lever = 0;
		} else if (lever == 2) {
			lever = 1;
		}
		if (lever == 3)
			lever = 3;
	}
}

/*
 * panel_updateweapons -- draw the 4 missile-hardpoint icons.
 */
// FUNCTION: TIE95 0x41840
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
// FUNCTION: TIE95 0x4194C
void panel_updatehardpoint(uint16_t slot, uint16_t hp_idx, uint16_t flags) {
	uint16_t ammo = 0;
	uint16_t lever_val;

	if (pstate.player_craft->missile_group_cnt)
		ammo = pstate.player_craft->weapon_slots[slot].ammo;

	if (ammo != (uint16_t)oldinstruments[15 + hp_idx]) {
		uint16_t digits;

		oldinstruments[15 + hp_idx] = (int16_t)ammo;
		festring_setfontsize(2);
		festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
		festring_setcursor((int16_t)instruments[15 + hp_idx].x, (int16_t)instruments[15 + hp_idx].y);
		festring_setbackcolor(0);
		festring_settextcolor(0x4A);
		dropflag = 0;

		digits = (pstate.player_spec_num == spec_getspecnum(12)) ? 2 : 1;
		panelrts_outnum((int32_t)ammo, digits, 1);

		TieHudSnapshot_RecordInstrumentDisplay((uint16_t)(15 + hp_idx), (int16_t)ammo, 0x4A, (uint8_t)digits);
	}

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

// FUNCTION: TIE95 0x41ADC
void panel_updateshields(void) {
	int16_t shield_hp;
	int16_t shield_pts;
	uint16_t pct;
	uint16_t lo_leds, hi_leds;
	uint16_t balance_val;

	if ((pstate.player_craft->working_subsystems & 0x20) == 0)
		return;

	shield_hp = pstate.player_craft->forward_shield;
	if (shield_hp < 0)
		shield_hp = 0;
	if ((pstate.player_craft->status_flags & 1) == 0)
		shield_hp = 0;
	shield_pts = spec_data[pstate.player_spec_num].shield_points;
	if (!mission.difficulty)
		shield_pts *= 2;
	if (shield_hp >= shield_pts) {
		pct = math2_percentage((uint16_t)(shield_hp - shield_pts), (uint16_t)shield_pts);
		lo_leds = 9;
		hi_leds = math2_fraction(9, pct);
	} else {
		pct = math2_percentage((uint16_t)shield_hp, (uint16_t)shield_pts);
		hi_leds = 0;
		lo_leds = math2_fraction(9, pct);
	}
	/* Damage flash. */
	if (timers[TIMER_SHIELD_FLASH] && shieldblink == 0) {
		if (hi_leds == 0)
			lo_leds = 10;
		else
			hi_leds = 10;
	}
	panel_updatemonolever(TIE_HUDI_SHIELD_FWD_NORMAL, (uint16_t)(uint8_t)shieldcolor[lo_leds]);
	panel_updatemonolever(TIE_HUDI_SHIELD_FWD_OVER, (uint16_t)(uint8_t)shieldcolor[hi_leds]);

	shield_hp = pstate.player_craft->rear_shield;
	if (shield_hp < 0)
		shield_hp = 0;
	if ((pstate.player_craft->status_flags & 1) == 0)
		shield_hp = 0;
	shield_pts = spec_data[pstate.player_spec_num].shield_points;
	if (!mission.difficulty)
		shield_pts *= 2;
	if (shield_hp >= shield_pts) {
		pct = math2_percentage((uint16_t)(shield_hp - shield_pts), (uint16_t)shield_pts);
		lo_leds = 9;
		hi_leds = math2_fraction(9, pct);
	} else {
		pct = math2_percentage((uint16_t)shield_hp, (uint16_t)shield_pts);
		hi_leds = 0;
		lo_leds = math2_fraction(9, pct);
	}
	/* Damage flash. */
	if (timers[TIMER_SHIELD_FLASH] && shieldblink == 1) {
		if (hi_leds == 0)
			lo_leds = 10;
		else
			hi_leds = 10;
	}
	panel_updatemonolever(TIE_HUDI_SHIELD_REAR_NORMAL, (uint16_t)(uint8_t)shieldcolor[lo_leds]);
	panel_updatemonolever(TIE_HUDI_SHIELD_REAR_OVER, (uint16_t)(uint8_t)shieldcolor[hi_leds]);

	if (timers[TIMER_SHIELD_OVERLOAD]) {
		balance_val = 3;
	} else {
		uint16_t third = (uint16_t)(pstate.player_craft->hull_max / 3);
		if (third == 0)
			balance_val = 2;
		else
			balance_val = (uint16_t)(2 - pstate.player_craft->hull_damage / third);
	}
	panel_updatelever(TIE_HUDI_HULL_DAMAGE_LEVER, balance_val);
}

/*
 * panel_updatebeam -- 9-LED beam charge bar (drawn RTL) + fire lever.
 */
// FUNCTION: TIE95 0x41D7C
void panel_updatebeam(void) {
	int16_t beam_charge;
	uint16_t fire;
	int16_t i;

	if ((pstate.player_craft->working_subsystems & 0x10) == 0)
		return;

	beam_charge = pstate.player_craft->beam_charge;
	if (beam_charge < 0)
		beam_charge = 0;
	if ((pstate.player_craft->status_flags & 0x100) == 0)
		beam_charge = 0;

	fire = ((pstate.player_craft->beam_state & 0x80) != 0) ? 1 : 0;
	if ((pstate.player_craft->status_flags & 0x100) == 0)
		fire = 0;
	panel_updatelever(TIE_HUDI_BEAM_FIRE, fire);

	if (oldinstruments[TIE_HUDI_BEAM_ARC] == beam_charge)
		return;
	oldinstruments[TIE_HUDI_BEAM_ARC] = beam_charge;

	for (i = 0; i < 9; ++i) {
		uint16_t led_color;
		uint16_t led_x, led_y;

		if (beam_charge > 1000 * (i + 1)) {
			led_color = (uint8_t)beamcolors[3]; /* fully filled */
		} else {
			int16_t excess = (int16_t)(beam_charge - 1000 * i);
			if (excess < 0) {
				led_color = (uint8_t)beamcolors[0];
			} else {
				if (excess > 1000)
					excess = 1000;
				excess /= 333;
				led_color = (uint8_t)beamcolors[excess];
			}
		}

#ifdef TIE_MODERN
		TieSnapshotBuilder_HudMut()->beam_arc_led_colors[i] = (uint8_t)led_color;
#endif

		led_x = instruments[TIE_HUDI_BEAM_ARC].x;
		led_y = instruments[TIE_HUDI_BEAM_ARC].y;
		if (flightResolution == (int16_t)TIE_FLIGHT_RES_SVGA ||
			(TIE_DISPLAY_DX5 &&
			 (flightResolution == TIE_FLIGHT_RES_SVGA_16 || flightResolution == TIE_FLIGHT_RES_SVGA_D3D))) {
			led_x += 3 * (8 - i);
			led_y += 3 * (8 - i);
		} else {
			led_x += 2 * (8 - i);
			led_y += 8 - i;
		}

		rtsvga2_drawmonoshapeVGA((const uint8_t*)farbufferptrs[i + instruments[TIE_HUDI_BEAM_ARC].param1],
								 led_x, led_y, instruments[TIE_HUDI_BEAM_ARC].param2, led_color);
	}
}

/* ================================================================== */
/* Flight-state indicators                                            */
/* ================================================================== */

/*
 * panel_updatespeed -- speed as % of MAX (29127 units ~ 111 MGLT).
 */
// FUNCTION: TIE95 0x41F00
void panel_updatespeed(void) {
	uint16_t pct;

	if ((pstate.player_craft->working_subsystems & 0x40) == 0)
		return;
	festring_setbackcolor(0x40);
	pct = math2_fraction((uint16_t)pstate.player->current_speed, 0x71C7u);
	panel_updatevalue(TIE_HUDI_SPEED_DIGITS, pct, 1);
}

/*
 * panel_updatethrottle -- /655 scale; slam-off mode doubles the
 * internal value so max still registers as 100.
 */
// FUNCTION: TIE95 0x41F54
void panel_updatethrottle(void) {
	uint16_t raw;

	if ((pstate.player_craft->working_subsystems & 0x40) == 0)
		return;
	festring_setbackcolor(0x40);
	raw = pstate.player_craft->throttle_speed / 655;
	if (!pstate.player_craft->slam_active)
		raw *= 2;
	panel_updatevalue(TIE_HUDI_THROTTLE_DIGITS, raw, 1);
}

/*
 * panel_updateclock -- MM:SS display at instrument 30.
 * Training / combat = mtimer (countdown); else = mission elapsed
 * `date.minute` / `date.second`, ticked by tie_updatetime.
 */
// FUNCTION: TIE95 0x41FBC
void panel_updateclock(void) {
	uint16_t total_secs;

	if (mission.train_craft_type)
		total_secs = (uint16_t)(60 * timeleft.minute + timeleft.second);
	else
		total_secs = (uint16_t)(date.minute * 60 + date.second);

	if (total_secs == oldinstruments[TIE_HUDI_CLOCK_DIGITS])
		return;
	oldinstruments[TIE_HUDI_CLOCK_DIGITS] = total_secs;

	festring_setfontsize(2);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(0x40);
	/* VGA uses palette index 77 (0x4D) for the clock digits; SVGA's 8-bit
	 * paletted mode shifts everything by 1 and uses 78 (0x4E). */
	if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
		festring_settextcolor(0x4D);
	else
		festring_settextcolor(0x4E);
	dropflag = 0;

	festring_setcursor((int16_t)instruments[TIE_HUDI_CLOCK_DIGITS].x,
					   (int16_t)instruments[TIE_HUDI_CLOCK_DIGITS].y);
	if (mission.train_craft_type)
		panelrts_outnum(timeleft.minute, 2, 1);
	else
		panelrts_outnum(date.minute, 2, 1);

	/* SVGA needs a 1-pixel x-bump after the colon glyph; VGA's narrower
	 * font already lands the SS digits flush with the colon. */
	if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
		festring_setcursor((int16_t)(sys2_calclength((uint8_t*)"00:") + instruments[TIE_HUDI_CLOCK_DIGITS].x),
						   (int16_t)instruments[TIE_HUDI_CLOCK_DIGITS].y);
	else
		festring_setcursor(
			(int16_t)(sys2_calclength((uint8_t*)"00:") + instruments[TIE_HUDI_CLOCK_DIGITS].x + 1),
			(int16_t)instruments[TIE_HUDI_CLOCK_DIGITS].y);
	if (mission.train_craft_type)
		panelrts_outnum(timeleft.second, 2, 2);
	else
		panelrts_outnum(date.second, 2, 2);
}

/*
 * panel_updatepower -- 4 sliders (lasers, shields, beam, balance).
 */
// FUNCTION: TIE95 0x42114
void panel_updatepower(void) {
	uint16_t step = (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA) ? 2 : 6;

	if (pstate.player_craft->working_subsystems & 0x200)
		panel_updatesetting((uint16_t)(3 * pstate.player_craft->laser_power), TIE_HUDI_POWER_LASERS, 12,
							step);

	if ((pstate.player_craft->working_subsystems & 0x800) && (pstate.player_craft->subsystem_active & 1))
		panel_updatesetting((uint16_t)(3 * pstate.player_craft->shield_power), TIE_HUDI_POWER_SHIELDS, 12,
							step);

	if ((pstate.player_craft->working_subsystems & 0x1000) && (pstate.player_craft->subsystem_active & 0x100))
		panel_updatesetting((uint16_t)(3 * pstate.player_craft->beam_power), TIE_HUDI_POWER_BEAM, 12, step);

	if (pstate.player_craft->working_subsystems & 0x400) {
		int16_t v = (int16_t)(2 - pstate.player_craft->laser_power + 6);
		if (pstate.player_craft->subsystem_active & 1)
			v += (int16_t)(2 - pstate.player_craft->shield_power);
		if (pstate.player_craft->subsystem_active & 0x100)
			v += (int16_t)(2 - pstate.player_craft->beam_power);
		panel_updatesetting((uint16_t)v, TIE_HUDI_POWER_BALANCE, 12, step);
	}
}

/*
 * panel_updatesetting -- vertical slider.
 * Each rung lit if rung < value; uses farbufferptrs[param1] (unlit) and
 * farbufferptrs[param1+1] (lit).
 */
// FUNCTION: TIE95 0x422AC
void panel_updatesetting(uint16_t value, uint16_t idx, uint16_t count, uint16_t step) {
	uint16_t y;
	uint16_t x;
	uint16_t shape_base;
	uint16_t rung;

	if (value == (uint16_t)oldinstruments[idx])
		return;

	oldinstruments[idx] = (int16_t)value;

	y = instruments[idx].y;
	x = instruments[idx].x;
	shape_base = instruments[idx].param1;

	for (rung = 0; rung < count; ++rung) {
		const void* shape = farbufferptrs[shape_base + (rung < value ? 1 : 0)];
		drawshape(shape, (int)x, (int)y, 253, 0);
		y -= step;
	}
}

/*
 * panel_updatereplaystuff -- REC LED + %remaining counter at
 * instrument 32.
 */
// FUNCTION: TIE95 0x42518
void panel_updatereplaystuff(void) {
	uint16_t x;
	uint16_t y;
	uint16_t w;

	panel_updatelever(TIE_HUDI_REC_LED, (uint16_t)recordingreplay);

	x = instruments[TIE_HUDI_REC_PCT].x;
	y = instruments[TIE_HUDI_REC_PCT].y;
	festring_setfontsize(2);

	if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
		w = 12;
	else
		w = 18;
	festring_setbound(x, y, x + w, y + fontheight);
	festring_setcursor((int16_t)x, (int16_t)y);
	festring_setbackcolor(0x40);

	if (!recordingreplay) {
		if (replaypercent != (uint16_t)0xFFFF) {
			replaypercent = 0xFFFF;
			clearwindow();
		}
	} else {
		uint16_t remaining =
			100 - math2_fraction(100, math2_longpercentage((uint32_t)replaytotalcnt, (uint32_t)replaymaxcnt));
		if (remaining > 99)
			remaining = 99;
		if (remaining != replaypercent) {
			replaypercent = remaining;
			clearwindow();
			festring_settextcolor(0x4E);
			panelrts_outnum((int32_t)remaining, 3, 1);
#ifdef TIE_MODERN
			TieHudSnapshot_RecordInstrumentDisplay(TIE_HUDI_REC_PCT, (int16_t)remaining, 0x4E, 3);
#endif
		}
	}
}

/*
 * panel_updatecovers -- drop the shield-LED and beam-charge covers
 * when their subsystems are inactive. Each cover is a single cel at
 * `param1`; the engine writes value=0 unconditionally (covers have only
 * the one closed-state graphic). Skip non-view-0 and ship_idx==5
 * (TIE Fighter).
 */
// FUNCTION: TIE95 0x42634
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
// FUNCTION: TIE95 0x426A8
void panel_updatecockpitdamage(void) {
	uint16_t mask;
	uint16_t idx;
	int i;

	if (camera.pilotview)
		return;

	mask = 1;
	idx = 45;
	for (i = 0; i < 13; ++i) {
		uint16_t frame = (mask & pstate.player_craft->working_subsystems) ? 0 : 13;
		if (mask & pstate.player_craft->installed_subsystems)
			panel_updatelever(idx, frame);
		++idx;
		mask <<= 1;
	}
	lasttargetnum = -1;
}

// FUNCTION: TIE95 0x42734
void panel_updatethreatname(void) {
	uint16_t prev_target;
	uint16_t cur_order;
	CraftData* cp;
	uint16_t x82, y82;
	uint16_t width;
	uint16_t link;
	uint16_t mins, secs;
	const uint8_t* cargo_str;
	uint16_t left, right;
	int32_t polar;
	uint16_t distance;
	uint16_t whole, frac;
	uint16_t cargo_kind;
	uint16_t x, y;
	const uint8_t* text;

	dropflag = 0;
	festring_setbackcolor(0x2C);
	festring_setfontsize(2);

	if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA) {
		left = 66;
		right = 246;
	} else {
		left = 132;
		right = 492;
	}

#ifdef TIE_MODERN
	/* The original leaves this unset when the target is unchanged. */
	prev_target = lasttargetnum;
#endif
	if (lasttargetnum != pstate.target_obj_idx) {
		prev_target = lasttargetnum;
		lasttargetnum = pstate.target_obj_idx;

		oldinstruments[70] = 0xFFFF;
		oldinstruments[71] = 0xFFFF;
		oldinstruments[72] = 0xFFFF;
		oldinstruments[79] = 0xFFFF;
		oldinstruments[82] = 0xFFFF;
		oldinstruments[80] = 0xFFFE;
		oldinstruments[81] = 0xFFFE;

#ifdef TIE_MODERN
		{
			TieHudState* hud = TieSnapshotBuilder_HudMut();
			hud->target_order_text[0] = '\0';
			hud->target_link_target_label[0] = '\0';
			hud->target_link_name[0] = '\0';
			hud->target_link_dist_label[0] = '\0';
			hud->target_link_dist_text[0] = '\0';
			hud->target_eta_label[0] = '\0';
			hud->target_eta_text[0] = '\0';
		}
#endif

		x = instruments[69].x;
		y = instruments[69].y;
		width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 76 : 114;
		festring_setbound(x, y, x + width, y + fontheight + 1);
		clearwindow();
		festring_setcursor(x, y);
		festring_setautofill(1);

		if (pstate.target_obj_idx != 0xFFFF) {
			panel_buildobjectname(pstate.target_obj_idx, 3);
			festring_outstringcenter((const uint8_t*)tempstring);
#ifdef TIE_MODERN
			/* Base color for the threat target name. 0xFE color escapes
			 * inside the name string override per-glyph; HD honours them. */
			TieSnapshotBuilder_HudMut()->instruments[69].color = 0x49;
#endif
		}

		if (lasttargetnum >= NUM_CRAFTS) {
			festring_setbound(left, instruments[79].y, right, instruments[82].y + fontheight);
			clearwindow();
		}
	}

	if (pstate.target_obj_idx == 0xFFFF)
		return;

	/* Target distance line. */
	x = instruments[71].x;
	y = instruments[71].y;
	if (prev_target == 0xFFFF) {
		festring_setcursor(x, y);
		festring_settextcolor(0x49);
		festring_outstring(diststring);
	}
	pai_distancebetween(pstate.object_idx, lasttargetnum);
#ifdef TIE_MODERN
	/* The original reads the ETA distance from an uninitialized stack
	 * slot when the target has no link; seed it with the target range. */
	polar = trig2_polardistance;
#endif

	width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 54 : 81;
	festring_setbound(x, y, x + width, y + fontheight + 1);
	festring_settextcolor(0x4A);

	trig2_polardistance *= 161;
	distance = trig2_polardistance >> 16;
	if (distance >= 10000)
		distance = 9999;

	whole = distance / 100;
	if (whole != oldinstruments[71]) {
		oldinstruments[71] = whole;
		festring_setcursor(x, y);
		panelrts_outnum(whole, 2, 1);
	}
	frac = distance - whole * 100;
	if (frac != oldinstruments[72]) {
		oldinstruments[72] = frac;
		festring_setcursor(x + sys2_calclength((const uint8_t*)"00.") + 1, y);
		panelrts_outnum(frac, 2, 2);
	}
#ifdef TIE_MODERN
	/* These fields bypass panel_updatevalue in the classic renderer, so
	 * publish their numeric metadata explicitly for the HD text pass. */
	{
		TieHudInstrument* field = &TieSnapshotBuilder_HudMut()->instruments[TIE_HUDI_THREAT_DIST_KM_INT];
		field->value = (int16_t)whole;
		field->color = 0x4A;
		field->digits = 2;
		field = &TieSnapshotBuilder_HudMut()->instruments[TIE_HUDI_THREAT_DIST_KM_FRAC];
		field->value = (int16_t)frac;
		field->color = 0x4A;
		field->digits = 2;
	}
#endif

	/* Cargo tag. */
	cargo_kind = 2;
	cargo_str = nonestring;
	if (pstate.target_obj_idx < NUM_CRAFTS && !objects[pstate.target_obj_idx].category) {
		CraftData* craft = objects[pstate.target_obj_idx].craft_ptr;

		if (craft->inspected) {
			cargo_kind = 1;
			cargo_str = (const uint8_t*)craft->cargo;
			if (!craft->cargo[0]) {
				cargo_kind = 2;
				cargo_str = nonestring;
			}
		} else {
			cargo_str = unknownstring;
			cargo_kind = 0;
		}
	}
	if (cargo_kind != oldinstruments[70]) {
		oldinstruments[70] = cargo_kind;
		x = instruments[70].x;
		y = instruments[70].y;
		width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 50 : 75;
		festring_setbound(x, y, x + width, y + fontheight + 1);
		clearwindow();
		festring_setcursor(x, y);
		festring_settextcolor(0x46);
		festring_outstring(cargo_str);
#ifdef TIE_MODERN
		{
			TieHudState* hud = TieSnapshotBuilder_HudMut();
			TieHudSnapshot_CopyText(hud->threat_cargo, sizeof hud->threat_cargo, cargo_str);
			hud->instruments[70].color = 0x46;
		}
#endif
	}

	/* Order / link / ETA text block (only for dynamic craft). */
	if (pstate.target_obj_idx >= NUM_CRAFTS || pstate.target_obj_idx == 0xFFFF)
		return;

	cp = objects[pstate.target_obj_idx].craft_ptr;
	cur_order = cp->current_order;
	if (!cp->status_flags)
		cur_order = 42;
	else if (!objects[pstate.target_obj_idx].current_speed && cur_order >= 45 && cur_order <= 54)
		cur_order = 66;

	if (cur_order != oldinstruments[79]) {
		oldinstruments[79] = cur_order;
		y = instruments[79].y;
		width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 190 : 285;
		festring_setbound(left, y, left + width, y + fontheight + 1);
		clearwindow();
		festring_setcursor(left, y);
		if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
			festring_settextcolor(0x45);
		else
			festring_settextcolor(0x46);
		festring_outstring(currentorderstring);
		festring_settextcolor(0x4E);
		festring_outstring((const uint8_t*)messagetable[convertmessage[cur_order]]);
#ifdef TIE_MODERN
		{
			TieHudState* hud = TieSnapshotBuilder_HudMut();
			TieHudSnapshot_CopyText(hud->target_order_text, sizeof hud->target_order_text,
									(const uint8_t*)messagetable[convertmessage[cur_order]]);
			hud->instruments[79].color = 0x4E;
		}
#endif

		y = instruments[82].y;
		width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 100 : 150;
		festring_setbound(left, y, left + width, y + fontheight + 1);
		clearwindow();
		festring_setcursor(left, y);
		if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
			festring_settextcolor(0x45);
		else
			festring_settextcolor(0x46);
		if (!objects[pstate.target_obj_idx].current_speed)
			text = timeremstring;
		else if ((uint16_t)cp->ai_target_ref < 0x8000)
			text = timetotargetstring;
		else
			text = timetodeststring;
		festring_outstring(text);
#ifdef TIE_MODERN
		TieHudSnapshot_CopyText(TieSnapshotBuilder_HudMut()->target_eta_label,
								sizeof TieSnapshotBuilder_HudMut()->target_eta_label, text);
#endif
	}

	link = cp->ai_target_ref;
	if (!cp->status_flags)
		link = 0xFFFF;
	if (cur_order == 66)
		link = 0xFFFF;

	if (link != oldinstruments[80]) {
		oldinstruments[80] = link;
		if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
			festring_settextcolor(0x45);
		else
			festring_settextcolor(0x46);
		festring_setbound(left, instruments[80].y, right, instruments[80].y + fontheight);
		clearwindow();
		festring_setcursor(left, instruments[80].y);
		text = link < 0x8000 ? curtargetstring : curdeststring;
		festring_outstring(text);
#ifdef TIE_MODERN
		TieHudSnapshot_CopyText(TieSnapshotBuilder_HudMut()->target_link_target_label,
								sizeof TieSnapshotBuilder_HudMut()->target_link_target_label, text);
#endif
		festring_setbound(left, instruments[81].y, right, instruments[81].y + fontheight);
		clearwindow();
		festring_setcursor(left, instruments[81].y);
		text = link < 0x8000 ? distfromtargetstring : disttodeststring;
		festring_outstring(text);
#ifdef TIE_MODERN
		TieHudSnapshot_CopyText(TieSnapshotBuilder_HudMut()->target_link_dist_label,
								sizeof TieSnapshotBuilder_HudMut()->target_link_dist_label, text);
#endif

		/* Link target name. */
		x = instruments[80].x;
		y = instruments[80].y;
		width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 100 : 150;
		festring_setbound(x, y, x + width, y + fontheight + 1);
		clearwindow();
		festring_setcursor(x, y);
		if (link != 0xFF && link != 0xFFFF) {
			panel_buildobjectname(link, 3);
			text = (const uint8_t*)tempstring;
		} else {
			text = notargetstring;
		}
		festring_outstring(text);
#ifdef TIE_MODERN
		TieHudSnapshot_CopyFestringText(TieSnapshotBuilder_HudMut()->target_link_name,
										sizeof TieSnapshotBuilder_HudMut()->target_link_name, text);
		TieSnapshotBuilder_HudMut()->instruments[80].color = 0x45;
#endif
	}

	/* Link distance line. */
	if (link != 0xFFFF) {
		ai.active_obj_idx = pstate.target_obj_idx;
		if (cp->mode_byte == 10)
			craftptr = objects[cp->leader_obj_idx].craft_ptr;
		else
			craftptr = cp;
		pai_targetdistance();
		polar = trig2_polardistance;

		x = instruments[81].x;
		y = instruments[81].y;
		width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 54 : 81;
		festring_setbound(x, y, x + width, y + fontheight + 1);
		festring_settextcolor(0x4A);

		trig2_polardistance *= 161;
		distance = trig2_polardistance >> 16;
		if (distance >= 10000)
			distance = 9999;
		whole = distance / 100;
		frac = distance - whole * 100;
		if (frac != oldinstruments[81]) {
			oldinstruments[81] = frac;
			festring_setcursor(x, y);
			panelrts_outnum(whole, 2, 1);
			outchar('.');
			panelrts_outnum(frac, 2, 2);
#ifdef TIE_MODERN
			{
				TieHudState* hud = TieSnapshotBuilder_HudMut();
				snprintf(hud->target_link_dist_text, sizeof hud->target_link_dist_text, "%u.%02u",
						 (unsigned)whole, (unsigned)frac);
				hud->instruments[81].color = 0x4A;
			}
#endif
		}
	}

	/* ETA clock -- mm:ss. */
	x82 = instruments[82].x;
	y82 = instruments[82].y;
	width = flightResolution == (int16_t)TIE_FLIGHT_RES_VGA ? 54 : 81;
	festring_setbound(x82, y82, x82 + width, y82 + fontheight + 1);
	festring_settextcolor(0x52);
	festring_setcursor(x82, y82);

	if (!objects[pstate.target_obj_idx].current_speed) {
		if (cp->current_order == 35 || cp->current_order == 66) {
			/* Hyperspace countdown: maneuver_timer ticks at ~236 Hz. */
			distance = cp->maneuver_timer / 236;
			mins = distance / 60;
			secs = distance - mins * 60;
			if (secs == oldinstruments[82])
				return;
		} else {
			festring_outstring(unknownstring);
#ifdef TIE_MODERN
			{
				TieHudState* hud = TieSnapshotBuilder_HudMut();
				TieHudSnapshot_CopyText(hud->target_eta_text, sizeof hud->target_eta_text, unknownstring);
				hud->instruments[82].color = 0x52;
			}
#endif
			return;
		}
	} else {
		distance = (uint32_t)polar / (uint16_t)(objects[pstate.target_obj_idx].current_speed * 18);
		mins = distance / 60;
		secs = distance - mins * 60;
		if (secs == oldinstruments[82])
			return;
	}
	oldinstruments[82] = secs;
	clearwindow();
	panelrts_outnum(mins, 2, 1);
	outchar(':');
	panelrts_outnum(secs, 2, 2);
#ifdef TIE_MODERN
	{
		TieHudState* hud = TieSnapshotBuilder_HudMut();
		snprintf(hud->target_eta_text, sizeof hud->target_eta_text, "%02u:%02u", (unsigned)mins,
				 (unsigned)secs);
		hud->instruments[82].color = 0x52;
	}
#endif
}

/* ================================================================== */
/* Per-widget updaters                                                */
/* ================================================================== */

/*
 * panel_updatelever -- cached shape redraw.
 * farbufferptrs[instruments[idx].param1 + value] picks the frame.
 */
// FUNCTION: TIE95 0x433E0
void panel_updatelever(uint16_t idx, uint16_t value) {
	if (value == (uint16_t)oldinstruments[idx])
		return;
	oldinstruments[idx] = (int16_t)value;
	drawshape(farbufferptrs[instruments[idx].param1 + value], instruments[idx].x, instruments[idx].y,
			  instruments[idx].param2, 0);
}

/*
 * panel_updatemonolever -- monochrome variant: the shape is fixed, the
 * 'value' becomes the colour argument.
 */
// FUNCTION: TIE95 0x4344C
void panel_updatemonolever(uint16_t idx, uint16_t value) {
	if (value == (uint16_t)oldinstruments[idx])
		return;
	oldinstruments[idx] = (int16_t)value;
	rtsvga2_drawmonoshapeVGA((const uint8_t*)farbufferptrs[instruments[idx].param1],
							 (int16_t)instruments[idx].x, (int16_t)instruments[idx].y,
							 instruments[idx].param2, value);
}

/*
 * panel_updatevalue -- numeric HUD field. param1 = digit count,
 * param2 = default text colour. Override colours for critical /
 * warning / grayed-out states.
 */
// FUNCTION: TIE95 0x434B0
void panel_updatevalue(uint16_t idx, uint16_t value, uint16_t flags) {
	uint16_t left;
	uint16_t y;
	uint16_t digit_count;
	int16_t glyph_w;
	uint16_t bottom;
	uint16_t col;

	if (value == (uint16_t)oldinstruments[idx])
		return;

	left = instruments[idx].x;
	y = instruments[idx].y;
	digit_count = instruments[idx].param1;

	glyph_w = (flightResolution == TIE_FLIGHT_RES_VGA) ? 4 : 8;
	bottom = y + fontheight;

	oldinstruments[idx] = (int16_t)value;
	festring_setbound((int16_t)left, (int16_t)y, (int16_t)(left + digit_count * glyph_w + 1),
					  (int16_t)bottom);

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

/* ================================================================== */
/* Resource / view loader                                             */
/* ================================================================== */

/*
 * panel_loadpaneldata -- panelname = cockpitdir + spec.internal_name,
 * then preload every view.
 */
// FUNCTION: TIE95 0x43628
void panel_loadpaneldata(void) {
	char name[16];
	char* src;
	char* dst;

	strcpy(panelname, cockpitdir);
	src = spec_data[pstate.player_spec_num].internal_name;
	dst = name;
	while (*src)
		*dst++ = *src++;
	*dst = *src;
	strcat(panelname, name);

	searchpartsflag = 0;
	panel_loadpanelviewdefs(panelname);
	panel_tryEMSforpanels(pstate.player_spec_num);
	panelflag = 1;
}

/*
 * panel_forcenewviewdir -- invalidate cockpit state and switch view.
 */
// FUNCTION: TIE95 0x436E4
void panel_forcenewviewdir(uint16_t view_idx) {
	lastpilotpaneldraw = -1;
	camera.pilotview = 0xFF;
	panelpartsflag = 0xFF;
	panelrts_setnewpilotview(view_idx);
	msg_messageinit();
}

/*
 * panel_dosetnewpilotview -- install view `view_idx`. Handles the
 * flags-0x80 / 0xC0 mirror tables, lazy bitmap load, and the final
 * buffer-dim + mask-copy sequence.
 */
// FUNCTION: TIE95 0x43710
// FUNCTION: TIE98 0x466B70
void panel_dosetnewpilotview(uint16_t view_idx) {
	uint16_t panel_x;
	uint16_t dest;

	panelmirrorflag = 0;
	panel_x = 0;
	dest = view_idx;

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
			farbufferptr = (uint8_t*)xmemhdl_Lock_Handle(panelpartshandle);
			xmemhdl_Unlock_Handle(panelpartshandle);
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
			if (TIE_FLIGHT_TIE98)
				backcolor = deepspacecolor;
			else
				festring_setbackcolor(0);
			clearwindow();
			if (TIE_FLIGHT_TIE98)
				logbuf2_setbufferdimensions_tie98((uint16_t)screenXRes, (uint16_t)maxPixelsDeep, 0, 0);
			else
				logbuf2_setbufferdimensions((uint16_t)screenXRes, (uint16_t)maxPixelsDeep, 0);
			panel_clearmaskdata(pixelswide, pixelsdeep);
			transfm2_screenyoffset = 0;
		} else {
			uint16_t skip_color;
			uint16_t geom;
			uint32_t displaycorner_off;

			if (!panelviewptrs[dest].handle || !panelsloadedflag) {
				temppanelptr = newbuf;
				panel_loadcontrolpanel(panelviewdefs[dest].name, &panelviewptrs[dest].image, 3);
			}
			buildpalette((const uint8_t*)panelviewptrs[dest].palette, 0, 64);
			/* TIE98 leaves palette index 0 transparent over a deep-space clear;
			 * TIE95 cockpit shapes use palette index 253 as their skip color. */
			skip_color = 253;
			if (TIE_FLIGHT_TIE98) {
				backcolor = deepspacecolor;
				festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)maxPixelsDeep);
				clearwindow();
				skip_color = 0;
			}
			drawshape(panelviewptrs[dest].image, panel_x, 0, skip_color, panelmirrorflag);

			geom = (panelmirrorflag == 1) ? view_idx : dest;
			/* displaycorner is the full framebuffer offset of the viewport origin:
			 * bytes for TIE98 and pixels for TIE95. */
			displaycorner_off = calcposition(panelviewdefs[geom].pos_x, panelviewdefs[geom].pos_y);
			if (TIE_FLIGHT_TIE98)
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
		if (TIE_FLIGHT_TIE98)
			g_flightInitialTextureCacheFlushPending = 1;
		fullupdateflag = 1;
		festring_showscreen();
		calcframerate = 0;
	}

	if (dest == 17) {
		int16_t y;
		int16_t y2;
		uint16_t half;
		uint16_t hw;

		festring_setfontsize(2);
		y = (int16_t)instruments[33].y;
		y2 = (int16_t)(instruments[33].y + fontheight + 1);
		half = (uint16_t)screenXRes >> 1;
		hw = panel_AdjustXForRes(40);
		festring_setbound((int16_t)(half - hw), y, (int16_t)(half + hw), y2);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_settextcolor(0x43);
		festring_setcursor(0, y);
		festring_outstringcenter((const uint8_t*)panelviewdefs[camera.pilotview].title);
	}
}

/*
 * panel_loadcontrolpanel -- read N sections of an LFD into
 * temppanelptr, recording each section's start in section_ptrs[].
 */
// FUNCTION: TIE95 0x43B6C
void panel_loadcontrolpanel(char* name, uint8_t** section_ptrs, uint16_t count) {
	uint16_t i;

	strcpy(panelfilename, cockpitdir);
	strcat(panelfilename, name);
	strcat(panelfilename, ".LFD");

	fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, panelfilename, readmode, 1);

	for (i = 0; i < count; ++i) {
		struct {
			char type[4];
			char name[8];
			uint32_t size;
		} header;
		uint16_t is_palt;
		uint16_t j;
		uint32_t size;

		section_ptrs[i] = temppanelptr;

		fediskio_readfileblock(&header, 16, 1, fileptr);
		is_palt = 1;
		for (j = 0; j < 4; ++j)
			if (header.type[j] != xpal_id[j])
				is_palt = 0;

#ifdef TIE_MODERN
		size = br_u32le((const uint8_t*)&header.size);
#else
		size = header.size;
#endif
		fediskio_readfileblock(temppanelptr, size, 1, fileptr);

		if (is_palt) {
			while (size--)
				*temppanelptr++ >>= 2;
		} else {
			temppanelptr += size;
		}
		if (is_palt)
			section_ptrs[i] += 2;
	}
	fediskio_tryclosefile(0);
}

/*
 * panel_tryEMSforpanels -- preload every defined view slot.
 */
// FUNCTION: TIE95 0x43CF4
void panel_tryEMSforpanels(int spec_num) {
	/* Binary: XMEMHDL_Alloc_Handle -> malloc; Lock/Unlock -> no-op.
	 * handle field repurposed as a "loaded" flag (1 = loaded, 0 = empty). */
	uint16_t i;

	for (i = 0; i < PANEL_NUM_VIEWS; ++i) {
		panelviewptrs[i].handle = 0;
		if (panelviewdefs[i].flags == 1) {
			strcpy(panelfilename, cockpitdir);
			strcat(panelfilename, panelviewdefs[i].name);
			strcat(panelfilename, ".LFD");

			fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, panelfilename, readmode, 1);
			if (fileptr) {
				int32_t sz = TieStorage_FileLength(fileptr);
				void* buf;

				fediskio_tryclosefile(0);
				fediskio_UnlockGlobals();
				buf = malloc((size_t)sz);
				fediskio_RelockGlobals();
				if (buf) {
					panelviewptrs[i].handle = 1;
					TiePanelViewBuffers_Set(i, buf);
					temppanelptr = buf;
					panel_loadcontrolpanel(panelviewdefs[i].name, &panelviewptrs[i].image, 3);
				}
			}
		}
	}
	panelsloadedflag = 1;
}

// FUNCTION: TIE95 0x43F4C
void panel_loadpanelviewdefs(char* base_name) {
	strcpy(panelfilename, base_name);
	strcat(panelfilename, ".INT");

	fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, panelfilename, readmode, 1);

	fediskio_readfileblock(panelviewdefs, sizeof(PanelViewDef), PANEL_NUM_VIEWS, fileptr);
	fediskio_readfileblock(instruments, sizeof(HudInstrument), PANEL_NUM_INSTRUMENTS, fileptr);
	fediskio_readfileblock(parts, 11, 1, fileptr);
	fediskio_tryclosefile(0);
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
// FUNCTION: TIE95 0x44008
void panel_copymaskdata(char* mask_src, uint16_t width, uint16_t height, uint16_t mirror) {
	uint8_t* out = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	uint8_t scratch[100];
	uint8_t* sp;
	uint16_t row;

	for (row = 0; row < height; row++) {
		uint16_t pos;
		uint8_t c;

		if (!mirror) {
			*out++ = *mask_src++;
			for (pos = 0; pos < width;) {
				c = *mask_src++;
				if (c == 0) {
					pos += 255;
					*out++ = c;
					c = *mask_src++;
					if (screenXRes != 320) {
						if (c == 0) {
							pos += 256;
							*out++ = c;
							c = *mask_src++;
							c++;
						} else if (c == 0xFF) {
							pos += 256;
							*out++ = 0;
							c = 0;
						} else {
							c++;
						}
					} else {
						c++;
					}
				}
				pos += c;
				*out++ = c;
			}
		} else {
			/* Mirror: copy the row into scratch, swap each escape group's
			 * byte order ([0, X] -> [X, 0], [0, 0, Y] -> [Y, 0, 0]), then
			 * walk it backwards so each group comes out in original order. */
			uint16_t count;
			uint16_t seg;

			sp = scratch;
			*sp++ = *mask_src++;
			for (pos = 0; pos < width;) {
				c = *mask_src++;
				if (c == 0) {
					pos += 255;
					*sp++ = c;
					c = *mask_src++;
					if (screenXRes != 320) {
						if (c == 0) {
							pos += 256;
							*sp++ = c;
							c = *mask_src++;
							c++;
						} else if (c == 0xFF) {
							pos += 256;
							*sp++ = 0;
							c = 0;
						} else {
							c++;
						}
					} else {
						c++;
					}
				}
				pos += c;
				*sp++ = c;
			}

			sp = scratch;
			*out = *sp++;
			count = 0;
			for (pos = 0; pos < width;) {
				uint8_t* group = sp;

				c = *sp++;
				if (c == 0) {
					c = *sp;
					*sp++ = 0;
					pos += 255;
					*group = c;
					if (c == 0) {
						c = *sp;
						*sp++ = 0;
						pos += 256;
						*group = c;
					}
				}
				pos += c;
				count++;
			}
			sp--;

			/* Negate the state byte when the segment count is even so the
			 * reversed run order preserves the open/closed parity. */
			if ((count & 1) == 0)
				*out = -*out;
			out++;

			for (seg = 0; seg < count; seg++) {
				c = *sp--;
				*out++ = c;
				if (c == 0) {
					c = *sp--;
					*out++ = c;
					if (c == 0)
						*out++ = *sp--;
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
// FUNCTION: TIE95 0x441EC
void panel_clearmaskdata(uint16_t width, uint16_t height) {
	uint8_t* out = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	uint16_t row;

	for (row = 0; row < height; ++row) {
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
// FUNCTION: TIE95 0x4427C
void panel_update3Dcrt(uint16_t x, uint16_t y, uint16_t width, uint16_t depth, uint16_t clear_runs) {
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

	uint32_t pos;
	int32_t sub_world_z, sub_world_y, sub_world_x;

	transfm2_screenyoffset = 0;
	pos = rtsvga2_calcpositionVGA(x, y);
	logbuf2_startPIP(width, depth, clear_runs, pos);

	if (clear_runs) {
		/* Pick the right mask template for this ship/resolution. The
		 * SVGA path distinguishes specs 7 and 8 and has its own masks for
		 * specs 4 and 5; the VGA path collapses 7|8 into a single mask. */
		uint8_t* dst = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
		const uint8_t* src;
		uint16_t i;

		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			if (pstate.player_spec_num == 15)
				src = gunboatcmdmaskdata;
			else if (pstate.player_spec_num == 7 || pstate.player_spec_num == 8)
				src = tieadvcmdmaskdata;
			else if (pstate.player_spec_num == 11)
				src = missileboatcmdmaskdata;
			else
				src = cmdmaskdata;
			for (i = 0; i < 200; i++)
				*dst++ = *src++;
		} else {
			if (pstate.player_spec_num == 15)
				src = gunboatcmd640maskdata;
			else if (pstate.player_spec_num == 7)
				src = tieadv7cmd640maskdata;
			else if (pstate.player_spec_num == 8)
				src = tieadv8cmd640maskdata;
			else if (pstate.player_spec_num == 5)
				src = spec5cmd640maskdata;
			else if (pstate.player_spec_num == 4)
				src = spec4cmd640maskdata;
			else if (pstate.player_spec_num == 11)
				src = missileboatcmd640maskdata;
			else
				src = cmd640maskdata;
			for (i = 0; i < 480; i++)
				*dst++ = *src++;
		}
	}

	panel_pointcamera(pstate.target_obj_idx, 1);

#ifdef TIE_MODERN
	TieHudSnapshot_RecordPipCamera(pstate.target_obj_idx);
#endif

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

	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		const uint8_t model_type = objects[pstate.target_obj_idx].ship_idx;
		if (TIE_FLIGHT_TIE98) {
			const int target_id = modelmesh_gettargetid(model_type, pstate.radar_target1);

			if (target_id != 0 &&
				(target_id != 1 ||
				 modelmesh_getobjecttypemeshtype(model_type, pstate.radar_target1) == TIE_MESH_MAIN_HULL)) {
				pai_calcrotatedpoint(&objects[pstate.target_obj_idx],
									 (int16_t)modelmesh_getcomponentfocusx(model_type, pstate.radar_target1),
									 (int16_t)modelmesh_getcomponentfocusz(model_type, pstate.radar_target1),
									 -modelmesh_getcomponentfocusy(model_type, pstate.radar_target1));
			} else if (model_type == 53) {
				pai_calcrotatedpoint(&objects[pstate.target_obj_idx],
									 (int16_t)(modelmesh_getcenterx(model_type, pstate.radar_target1) >> 1),
									 (int16_t)(modelmesh_getcenterz(model_type, pstate.radar_target1) >> 1),
									 (int16_t)(-modelmesh_getcentery(model_type, pstate.radar_target1) >> 1));
				rotatedx *= 2;
				rotatedy *= 2;
				rotatedz *= 2;
			} else {
				pai_calcrotatedpoint(&objects[pstate.target_obj_idx],
									 (int16_t)modelmesh_getcenterx(model_type, pstate.radar_target1),
									 (int16_t)modelmesh_getcenterz(model_type, pstate.radar_target1),
									 -modelmesh_getcentery(model_type, pstate.radar_target1));
			}
			sub_world_x = rotatedx + worldlocx - camera.x;
			sub_world_y = worldlocy + rotatedy - camera.y;
			sub_world_z = worldlocz + rotatedz - camera.z;
#ifdef TIE_MODERN
			TieHudSnapshot_RecordPipSubsystem(pstate.target_obj_idx);
#endif
		} else {
			ShipModelMesh* mesh;
			int16_t side, up, fwd_neg;
			int8_t shift;

			draw_Lockshipfileptrs(model_type);
			mesh = &componentblockptr[(uint16_t)pstate.radar_target1];

			/* Binary uses unaligned dword-HIWORD reads; each >>17 decodes
			 * to the NEXT int16 field, shifted right by 1. Direct accesses
			 * (resolved via side/fwd/up names matching the LFD CRFT order):
			 *   >17 on &pos_side      -> pos_fwd>>1
			 *   >17 on &pos_fwd       -> pos_up>>1
			 *   >17 on &has_position  -> pos_side>>1
			 *   >17 on &center_side   -> center_fwd>>1
			 *   >17 on &center_fwd    -> center_up>>1
			 *   >17 on &draw_distance+2 -> center_side>>1 */
			if (mesh->has_position == 0 ||
				(mesh->has_position == 1 && mesh->mesh_type != 1 /* MESH_MainHull */)) {
				fwd_neg = (int16_t)(-(mesh->center_fwd >> 1));
				up = (int16_t)(mesh->center_up >> 1);
				side = (int16_t)(mesh->center_side >> 1);
			} else {
				fwd_neg = (int16_t)(-(mesh->pos_fwd >> 1));
				up = (int16_t)(mesh->pos_up >> 1);
				side = (int16_t)(mesh->pos_side >> 1);
			}
			pai_calcrotatedpoint(&objects[pstate.target_obj_idx], side, up, fwd_neg);
			/* Watcom emitted SAR-on-unaligned-dword to extract the model_scale_shift byte
			 * at +0x1E; read the field directly. Shifts run in the unsigned domain
			 * so a negative coordinate doesn't trip the C left-shift UB rule. */
			shift = (int8_t)objectblockptr->model_scale_shift;
			rotatedx = (int32_t)((uint32_t)rotatedx << shift);
			rotatedy = (int32_t)((uint32_t)rotatedy << shift);
			rotatedz = (int32_t)((uint32_t)rotatedz << shift);
			sub_world_x = rotatedx + worldlocx - camera.x;
			sub_world_y = rotatedy + worldlocy - camera.y;
			sub_world_z = rotatedz + worldlocz - camera.z;

			/* Cache the exact rotated subsystem offset, not an absolute world
			 * coordinate. The PIP renderer consumes it directly in the target-
			 * local frame. */
#ifdef TIE_MODERN
			TieHudSnapshot_RecordPipSubsystem(pstate.target_obj_idx);
#endif
		}
	}

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	objecteyez = transfm2_geteyez(worldx, worldy, worldz);

	if (pstate.target_obj_idx < 0x3800) {
		FlightObject* op = &objects[pstate.target_obj_idx];
		switch ((uint16_t)op->genus) {
			case GENUS_FIGHTER:
			case GENUS_TRANSPORT:
			case GENUS_UTILITY:
			case GENUS_FREIGHTER:
			case GENUS_STARSHIP:
			case GENUS_PLATFORM:
				craftptr = objects[pstate.target_obj_idx].craft_ptr;
				fview_newcalcrotate(op->roll, op->pitch, op->heading, 0, op);
				draw_drawcomplexobject(pstate.target_obj_idx);
				break;
			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC:
				fview_newcalcrotate(op->roll, op->pitch, op->heading, 0, op);
				draw_drawlaser(pstate.target_obj_idx);
				break;
			default:
				break;
		}
	} else {
		uint16_t si = pstate.target_obj_idx - 14336;
		uint16_t sc = staticobjects[si].ship_class;
		if (sc >= 8u && sc <= 11u) {
			fview_newcalcrotate((int16_t)(staticobjects[si].roll_byte << 8),
								(int16_t)(staticobjects[si].pitch_byte << 8),
								(int16_t)(staticobjects[si].heading_byte << 8), 0, NULL);
			lightflag = 1;
			static_drawstaticobject(si);
		}
	}

	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		int16_t sx;
		int16_t sy;

		objecteyex = transfm2_geteyex(sub_world_x, sub_world_y, sub_world_z);
		objecteyey = transfm2_geteyey(sub_world_x, sub_world_y, sub_world_z);
		objecteyez = transfm2_geteyez(sub_world_x, sub_world_y, sub_world_z);
		sx = (int16_t)transfm2_getscreenx(objecteyex, objecteyez);
		sy = (int16_t)transfm2_getscreeny(objecteyey, objecteyez);
		panel_drawboxinxtrans(sx - 2, sy - 2, 4, 4, 0xCE);
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

// FUNCTION: TIE98 0x467570
// PANEL_update3Dcrt
void panel_update3Dcrt_tie98(uint16_t x, uint16_t y, uint16_t width, uint16_t depth, int clear_runs) {
	int32_t save_screenyoffset = transfm2_screenyoffset;
	int32_t save_camera_x = camera.x;
	int32_t save_camera_y = camera.y;
	int32_t save_camera_z = camera.z;
	uint16_t save_currenttarget = currenttarget;

	uint32_t position;
	int32_t target_world_x;
	int32_t target_world_y;
	int32_t target_world_z;

	transfm2_screenyoffset = 0;
	position = rtsvga2_calcpositionVGA_tie98(x, y);
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
	target_world_x = y;
	target_world_y = y;
	target_world_z = y;
	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		FlightObject* target = &objects[pstate.target_obj_idx];
		const int mesh_index = pstate.radar_target1;
		const int model_type = target->ship_idx;
		const int target_id = modelmesh_gettargetid(model_type, mesh_index);

		if (target_id != 0 && (target_id != 1 || modelmesh_getobjecttypemeshtype(model_type, mesh_index) ==
													 TIE_MESH_MAIN_HULL)) {
			pai_calcrotatedpoint(target, (int16_t)modelmesh_getcomponentfocusx(model_type, mesh_index),
								 (int16_t)modelmesh_getcomponentfocusz(model_type, mesh_index),
								 -modelmesh_getcomponentfocusy(model_type, mesh_index));
		} else if (model_type == 53) {
			pai_calcrotatedpoint(target, (int16_t)(modelmesh_getcenterx(model_type, mesh_index) >> 1),
								 (int16_t)(modelmesh_getcenterz(model_type, mesh_index) >> 1),
								 (int16_t)(-modelmesh_getcentery(model_type, mesh_index) >> 1));
			rotatedx *= 2;
			rotatedy *= 2;
			rotatedz *= 2;
		} else {
			pai_calcrotatedpoint(target, (int16_t)modelmesh_getcenterx(model_type, mesh_index),
								 (int16_t)modelmesh_getcenterz(model_type, mesh_index),
								 -modelmesh_getcentery(model_type, mesh_index));
		}
		target_world_x = rotatedx + worldlocx - camera.x;
		target_world_y = rotatedy + worldlocy - camera.y;
		target_world_z = rotatedz + worldlocz - camera.z;
#ifdef TIE_MODERN
		TieHudSnapshot_RecordPipSubsystem(pstate.target_obj_idx);
#endif
	}

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	if (pstate.target_obj_idx >= OBJ_REF_STATIC_BASE) {
		const uint16_t static_index = pstate.target_obj_idx - OBJ_REF_STATIC_BASE;
		StaticObject* object = &staticobjects[static_index];
		if (object->ship_class >= 8 && object->ship_class <= 11) {
			fview_newcalcrotate((int16_t)((uint16_t)object->roll_byte << 8),
								(int16_t)((uint16_t)object->pitch_byte << 8),
								(int16_t)((uint16_t)object->heading_byte << 8), 0, NULL);
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
				fview_newcalcrotate(object->roll, object->pitch, object->heading, 0, object);
				draw_process_object_components_tie98(pstate.target_obj_idx);
				FlightModel_Draw_Object(object);
				break;
			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC:
				fview_newcalcrotate(object->roll, object->pitch, object->heading, 0, object);
				draw_drawlaser_tie98(pstate.target_obj_idx);
				break;
			default:
				break;
		}
	}

	RenderScene_DrawVisibleFaces();
	if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS) {
		int screen_x;
		int screen_y;

		objecteyex = transfm2_geteyex(target_world_x, target_world_y, target_world_z);
		objecteyey = transfm2_geteyey(target_world_x, target_world_y, target_world_z);
		objecteyez = transfm2_geteyez(target_world_x, target_world_y, target_world_z);
		screen_x = transfm2_getscreenx(objecteyex, objecteyez);
		screen_y = transfm2_getscreeny(objecteyey, objecteyez);
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

// FUNCTION: TIE98 0x463400
// panel_Update3DCrtIfVisible
void panel_Update3DCrtIfVisible(void) {
	if (pstate.target_obj_idx == 0xffff)
		return;
	if (pstate.target_obj_idx < OBJ_REF_STATIC_BASE) {
		if (objects[pstate.target_obj_idx].ship_idx == 0 ||
			objects[pstate.target_obj_idx].genus == GENUS_EXPLOSION)
			return;
		if (objects[pstate.target_obj_idx].category == 0 &&
			(objects[pstate.target_obj_idx].craft_ptr->flight_flag == 3 ||
			 objects[pstate.target_obj_idx].craft_ptr->flight_flag == 4))
			return;
	} else {
		if (staticobjects[pstate.target_obj_idx - OBJ_REF_STATIC_BASE].species == 0 ||
			staticobjects[pstate.target_obj_idx - OBJ_REF_STATIC_BASE].ship_class == 13)
			return;
	}
	if ((pstate.player_craft->status_flags & 4) != 0 && pstate.hyperin_state == 0 && mission.end_flag == 0 &&
		camera.pilotview == 0 && (objects[pstate.object_idx].craft_ptr->working_subsystems & 1) != 0) {
		panel_update3Dcrt_tie98(instruments[2].x, instruments[2].y, instruments[2].param1,
								instruments[2].param2, 1);
	}
}

// FUNCTION: TIE98 0x467CE0
// PANEL_drawboxinxtrans
int16_t panel_drawboxinxtrans_tie98(int x, int y, int width, int height, uint8_t color) {
	return Hud_DrawBoxInXTrans(x, y, width, height, color, 1);
}

/*
 * panel_drawboxinxtrans -- 4-edge hollow rectangle in the xtrans buffer.
 */
/* flatcolors/flatx/flaty/flatz/flatparentobj/flatcomponentnum/flatobjnum
 * are declared in xtrans2.h (already included). */

// FUNCTION: TIE95 0x447F8
void panel_drawboxinxtrans(int left_x, int top_y, uint16_t width, uint16_t height, uint8_t color) {
	int16_t left;
	int16_t top;
	int16_t span;
	int16_t inner_left;
	int16_t inner_top;
	int16_t inner_span;
	int16_t right;
	int16_t right_top;
	int16_t right_span;
	int16_t outer_top;
	int16_t outer_span;

	flatcolors[flatobjnum] = color;
	flatx[flatobjnum] = 0;
	flaty[flatobjnum] = 0;
	flatcomponentnum[flatobjnum] = 0;
	flatz[flatobjnum] = 0;
	flatparentobj[flatobjnum] = 0x200;

	if (top_y + height < 0)
		return;
	if (left_x + width < 0)
		return;
	if (left_x >= pixelswide)
		return;
	if (top_y >= pixelsdeep)
		return;

	left = left_x;
	if (left < 0)
		left = 0;
	top = top_y;
	span = height;
	if (top < 0) {
		top = 0;
		span = height + top_y;
	}
	if (span > 0)
		trace2_enterflatvertical(left, top, span);

	inner_left = left_x + 1;
	if (inner_left < 0)
		inner_left = 0;
	inner_top = top_y + 1;
	inner_span = height - 2;
	if (inner_top < 0) {
		inner_top = 0;
		inner_span += top_y + 1;
	}
	if (inner_span > 0)
		trace2_enterflatvertical(inner_left, inner_top, inner_span);

	right = left_x + width;
	if (right <= pixelswide) {
		right_top = top_y + 1;
		right_span = height - 2;
		if (right_top < 0) {
			right_top = 0;
			right_span += top_y + 1;
		}
		if (right_span > 0)
			trace2_enterflatvertical(right, right_top, right_span);

		right = left_x + width + 1;
		if (right <= pixelswide) {
			outer_top = top_y;
			outer_span = height;
			if (outer_top < 0) {
				outer_top = 0;
				outer_span = height + top_y;
			}
			if (outer_span > 0)
				trace2_enterflatvertical(right, outer_top, outer_span);
		}
	}
	++flatobjnum;
}

// FUNCTION: TIE98 0x467D10
// PANEL_pointcamera
void panel_pointcamera_tie98(uint16_t target_obj, int16_t use_hud_size) {
	int32_t dx;
	int32_t dy;
	int32_t dz;
	uint16_t hx;
	uint16_t hy;
	uint16_t hz;
	int16_t ex;
	int16_t ey;
	int16_t ez;
	int32_t side_proj;
	int32_t fwd_proj;
	int32_t up_proj;
	uint32_t bound_hwidth;
	uint16_t species;
	uint16_t pix;
	uint16_t z;
	uint16_t shift;
	int target_idx;

	target_idx = target_obj;
	create_getworldposition(target_idx, 0);

	dx = worldlocx - pstate.player->world_x;
	dy = worldlocy - pstate.player->world_y;
	dz = worldlocz - pstate.player->world_z;
	dx *= 2;
	dy *= 2;
	dz *= 2;

	/* Track the high-word magnitude of each axis while reducing the vector. */
	hx = (uint16_t)(dx >> 16);
	hy = (uint16_t)(dy >> 16);
	hz = (uint16_t)(dz >> 16);
	if (hx & 0x8000)
		hx = -hx;
	if (hy & 0x8000)
		hy = -hy;
	if (hz & 0x8000)
		hz = -hz;
	do {
		hx >>= 1;
		hy >>= 1;
		hz >>= 1;
		dx >>= 1;
		dy >>= 1;
		dz >>= 1;
	} while (hx || hy || hz);

	dx >>= 1;
	dy >>= 1;
	dz >>= 1;
	ex = (int16_t)dx;
	ey = (int16_t)dy;
	ez = (int16_t)dz;

	if (objects[pstate.object_idx].orient_dirty) {
		fview_calcrotatemove(objects[pstate.object_idx].pitch, objects[pstate.object_idx].heading,
							 &objects[pstate.object_idx]);
		fview_calcrotateorient(objects[pstate.object_idx].roll, 0, &objects[pstate.object_idx]);
	}

	/* Project the player->target delta onto the player's body axes. */
	side_proj = math2_dot3_q15_clamped(ex, ey, ez, pstate.player->side_x, pstate.player->side_y,
									   pstate.player->side_z);
	fwd_proj =
		math2_dot3_q15_clamped(ex, ey, ez, pstate.player->fwd_x, pstate.player->fwd_y, pstate.player->fwd_z);
	up_proj =
		math2_dot3_q15_clamped(ex, ey, ez, pstate.player->up_x, pstate.player->up_y, pstate.player->up_z);

	trig2_ctop(side_proj, fwd_proj, up_proj);

	fview_newcalcview(pstate.player->roll, pstate.player->pitch, pstate.player->heading, 0,
					  (int16_t)(0x4000 - trig2_zangle), trig2_xyangle, NULL);

	if (target_obj < OBJ_REF_STATIC_BASE) {
		species = objects[target_obj].craft_ptr->species_idx;
		if (spec_data[species].bound_width <= spec_data[species].bound_depth &&
			spec_data[species].bound_width <= spec_data[species].bound_height) {
			bound_hwidth = (uint32_t)(spec_data[species].bound_height + spec_data[species].bound_depth) >>
						   1 << spec_data[species].model_scale_shift;
		} else if (spec_data[species].bound_depth <= spec_data[species].bound_width &&
				   spec_data[species].bound_depth <= spec_data[species].bound_height) {
			bound_hwidth = (uint32_t)(spec_data[species].bound_height + spec_data[species].bound_width) >>
						   1 << spec_data[species].model_scale_shift;
		} else {
			bound_hwidth = (uint32_t)(spec_data[species].bound_width + spec_data[species].bound_depth) >>
						   1 << spec_data[species].model_scale_shift;
		}
	} else {
		bound_hwidth = species_table[staticobjects[target_idx - OBJ_REF_STATIC_BASE].species].bound_hwidth;
	}

	if (use_hud_size)
		pix = instruments[2].param2;
	else
		pix = flightResolution == TIE_FLIGHT_RES_VGA ? 60 : 144;

	bound_hwidth = (bound_hwidth << perspShift) / pix;
	shift = 0;
	while (bound_hwidth > 0x3fff) {
		bound_hwidth >>= 1;
		++shift;
	}
	z = (uint16_t)bound_hwidth;
	if (flightResolution != TIE_FLIGHT_RES_VGA)
		z += z >> 2;

	/* Back-step along the world-space camera Z basis. */
	camera.x = math2_mul_q15(z, worldeyeA3);
	camera.y = math2_mul_q15(z, worldeyeB3);
	camera.z = math2_mul_q15(z, worldeyeC3);
	camera.x = worldlocx - (int32_t)((uint32_t)camera.x << shift);
	camera.y = worldlocy - (int32_t)((uint32_t)camera.y << shift);
	camera.z = worldlocz - (int32_t)((uint32_t)camera.z << shift);
}

/*
 * panel_pointcamera -- position the 3D CRT's camera to frame the target
 * with auto-zoom sized on bound_hwidth.
 */
// FUNCTION: TIE95 0x4499C
void panel_pointcamera(uint16_t target_obj, int16_t use_hud_size) {
	int32_t dx;
	int32_t dy;
	int32_t dz;
	uint16_t hx;
	uint16_t hy;
	uint16_t hz;
	int16_t ex;
	int16_t ey;
	int16_t ez;
	int32_t side_proj;
	int32_t fwd_proj;
	int32_t up_proj;
	uint32_t bound_hwidth;
	uint16_t species;
	uint16_t pix;
	uint16_t z;
	uint16_t shift;

	create_getworldposition(target_obj, 0);

	dx = worldlocx - pstate.player->world_x;
	dy = worldlocy - pstate.player->world_y;
	dz = worldlocz - pstate.player->world_z;
	dx *= 2;
	dy *= 2;
	dz *= 2;

	/* Track the high-word magnitude of each axis while reducing the vector. */
	hx = (uint16_t)(dx >> 16);
	hy = (uint16_t)(dy >> 16);
	hz = (uint16_t)(dz >> 16);
	if (hx & 0x8000)
		hx = -hx;
	if (hy & 0x8000)
		hy = -hy;
	if (hz & 0x8000)
		hz = -hz;
	do {
		hx >>= 1;
		hy >>= 1;
		hz >>= 1;
		dx >>= 1;
		dy >>= 1;
		dz >>= 1;
	} while (hx || hy || hz);

	dx >>= 1;
	dy >>= 1;
	dz >>= 1;
	ez = (int16_t)dz;
	ey = (int16_t)dy;
	ex = (int16_t)dx;

	/* Project the player->target delta onto the player's body axes.
	 * trig2_ctop(x, y, z) computes xyangle = atan2(x, y) (with a fixed
	 * +90 deg offset), so the bearing-to-target the binary feeds in is
	 * (side, fwd, up) -- not (fwd, side, up). Swapping these two
	 * rotates the PIP camera 90 deg around the player's up axis. */
	side_proj = math2_dot3_q15_clamped(ex, ey, ez, pstate.player->side_x, pstate.player->side_y,
									   pstate.player->side_z);
	fwd_proj =
		math2_dot3_q15_clamped(ex, ey, ez, pstate.player->fwd_x, pstate.player->fwd_y, pstate.player->fwd_z);
	up_proj =
		math2_dot3_q15_clamped(ex, ey, ez, pstate.player->up_x, pstate.player->up_y, pstate.player->up_z);

	trig2_ctop(side_proj, fwd_proj, up_proj);

	fview_newcalcview(pstate.player->roll, pstate.player->pitch, pstate.player->heading, 0,
					  (int16_t)(0x4000 - trig2_zangle), (uint16_t)trig2_xyangle, NULL);

	if (target_obj < 0x3800) {
		species = objects[target_obj].craft_ptr->species_idx;
		if (spec_data[species].bound_width <= spec_data[species].bound_depth &&
			spec_data[species].bound_width <= spec_data[species].bound_height)
			bound_hwidth = spec_data[species].bound_depth + spec_data[species].bound_height;
		else if (spec_data[species].bound_depth <= spec_data[species].bound_width &&
				 spec_data[species].bound_depth <= spec_data[species].bound_height)
			bound_hwidth = spec_data[species].bound_width + spec_data[species].bound_height;
		else
			bound_hwidth = spec_data[species].bound_depth + spec_data[species].bound_width;
		bound_hwidth = bound_hwidth >> 1 << (uint16_t)spec_data[species].model_scale_shift;
	} else {
		// SPECIES0
		bound_hwidth = species_table[staticobjects[target_obj - 0x3800].species].bound_hwidth;
	}

	if (use_hud_size)
		pix = instruments[2].param2;
	else if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
		pix = 60;
	else
		pix = 144;

	/* Species 52 uses tighter framing. */
	bound_hwidth = (bound_hwidth << perspShift) / pix;
	shift = (species == 52) + 4;
	z = (uint16_t)(bound_hwidth >> shift);
	/* 640x480 shifts the framing back another 1.25x. */
	if (flightResolution == (int16_t)TIE_FLIGHT_RES_SVGA)
		z += z >> 2;

	/* Back-step along the world-space camera Z basis. Shift in the
	 * unsigned domain so a negative offset stays well-defined. */
	camera.x = math2_mul16_q15(z, worldeyeA3);
	camera.y = math2_mul16_q15(z, worldeyeB3);
	camera.z = math2_mul16_q15(z, worldeyeC3);
	camera.x = (int32_t)((uint32_t)camera.x << shift);
	camera.y = (int32_t)((uint32_t)camera.y << shift);
	camera.z = (int32_t)((uint32_t)camera.z << shift);
	camera.x = worldlocx - camera.x;
	camera.y = worldlocy - camera.y;
	camera.z = worldlocz - camera.z;
}

/*
 * panel_AdjustXForRes -- scale X coord from 320-design to current res.
 */
// FUNCTION: TIE95 0x44E00
uint16_t panel_AdjustXForRes(uint16_t x) {
	if (flightResolution == (int16_t)TIE_FLIGHT_RES_VGA)
		return x;
	return (uint16_t)(x + x / 2);
}
