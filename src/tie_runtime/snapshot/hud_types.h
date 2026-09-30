#ifndef TIE_RUNTIME_HUD_TYPES_H
#define TIE_RUNTIME_HUD_TYPES_H

#include <stdint.h>

#define TIE_MAX_HUD_INSTRUMENTS 95
#define TIE_MAX_RADAR_BLIPS 48

/* Semantic IDs for the fixed 95-entry cockpit geometry table. */
typedef enum TieHudInstrumentId {
	TIE_HUDI_RADAR_LEFT = 0,
	TIE_HUDI_RADAR_RIGHT = 1,
	TIE_HUDI_CMD_3D_CRT = 2,
	/* 3..10: laser-LED row, one slot per active weapon group (engine
	 * caps in practice — all shipped craft have weapon_group_cnt ≤ 8). */
	TIE_HUDI_LASER_LED_FIRST = 3,
	TIE_HUDI_LASER_LED_LAST = 10,
	/* 11..14: missile-hardpoint ready levers (hp_idx 0..3). Engine writes
	 * via panel_updatehardpoint → panel_updatelever(hp_idx + 11, ...). */
	TIE_HUDI_MISSILE_HP_FIRST = 11,
	TIE_HUDI_MISSILE_HP_LAST = 14,
	/* 15..18: ammo-digit positions next to each hardpoint (text-pass). */
	TIE_HUDI_MISSILE_AMMO_FIRST = 15,
	TIE_HUDI_MISSILE_AMMO_LAST = 18,
	/* Forward / rear shield bars (lo + hi pair per half). */
	TIE_HUDI_SHIELD_FWD_NORMAL = 19,
	TIE_HUDI_SHIELD_FWD_OVER = 20,
	TIE_HUDI_SHIELD_REAR_NORMAL = 21,
	TIE_HUDI_SHIELD_REAR_OVER = 22,
	/* Hull-damage indicator — PANEL_updateshields balance section. */
	TIE_HUDI_HULL_DAMAGE_LEVER = 23,
	TIE_HUDI_SPEED_DIGITS = 24,
	TIE_HUDI_THROTTLE_DIGITS = 25,
	/* Power-distribution sliders (PANEL_updatepower). */
	TIE_HUDI_POWER_BALANCE = 26, /* engine residual */
	TIE_HUDI_POWER_LASERS = 27,
	TIE_HUDI_POWER_SHIELDS = 28,
	TIE_HUDI_POWER_BEAM = 29,
	TIE_HUDI_CLOCK_DIGITS = 30,
	TIE_HUDI_REC_LED = 31,
	TIE_HUDI_REC_PCT = 32,
	TIE_HUDI_VIEW17_TITLE = 33,
	TIE_HUDI_BEAM_ARC = 35,
	TIE_HUDI_GUNSIGHT = 36,
	/* 37..44: per-weapon-group fire-status lever. */
	TIE_HUDI_WEAPON_FIRE_FIRST = 37,
	TIE_HUDI_WEAPON_FIRE_LAST = 44,
	/* 45..57: 13 subsystem damage-crack overlays. */
	TIE_HUDI_DAMAGE_CRACK_FIRST = 45,
	TIE_HUDI_DAMAGE_CRACK_LAST = 57,
	/* CMD-readout target text/digits. */
	TIE_HUDI_TARGET_SUBSYSTEM_PCT = 58,
	TIE_HUDI_TARGET_DIST_KM_INT = 59,
	TIE_HUDI_TARGET_DIST_KM_FRAC = 60,
	TIE_HUDI_TARGET_SHIELD_PCT = 61,
	TIE_HUDI_TARGET_HULL_PCT = 62,
	TIE_HUDI_TARGET_CARGO = 63,
	TIE_HUDI_TARGET_SUBSYSTEM_FOCUS = 65,
	/* Weapon-warning LEDs. */
	TIE_HUDI_WARN_INCOMING = 66,
	TIE_HUDI_WARN_LOCK = 67,
	TIE_HUDI_WARN_IMPACT = 68,
	/* Threat-view target weapons + percentages. */
	TIE_HUDI_THREAT_DIST_KM_INT = 71,
	TIE_HUDI_THREAT_DIST_KM_FRAC = 72,
	TIE_HUDI_THREAT_ION = 73,
	TIE_HUDI_THREAT_TORP = 74,
	TIE_HUDI_THREAT_MISSILE = 75,
	TIE_HUDI_THREAT_BEAM = 76,
	TIE_HUDI_THREAT_SHIELD_PCT = 77,
	TIE_HUDI_THREAT_HULL_PCT = 78,
	/* Drop-down covers painted by PANEL_updatecovers when the
	 * corresponding subsystem is inactive (subsystem_active bit clear).
	 * Cover the shield-LED zone (83, gated on SF_SHIELDS) and the
	 * beam-charge zone (84+85, gated on SF_TRACTOR_BEAM). */
	TIE_HUDI_COVER_SHIELDS = 83,
	TIE_HUDI_COVER_BEAM_UP = 84,
	TIE_HUDI_COVER_BEAM_DOWN = 85,
	/* Beam fire-status lever. */
	TIE_HUDI_BEAM_FIRE = 91,
} TieHudInstrumentId;

/* Cockpit and HUD state shared with the classic panel. Zeroed when no
 * player craft is active. */
/* Radar blip — anchor-relative.
 *
 *   radar_idx  — 0 = left radar (TIE_HUDI_RADAR_LEFT), 1 = right
 *                (TIE_HUDI_RADAR_RIGHT). Renderer looks up the disc's
 *                authored anchor in the layout's instruments table.
 *   offset_x/y — signed classic-px from the disc center. Renderer
 *                composes `disc_anchor_ref + offset × (radar_radius_ref
 *                / radar_classic_radius)` so blips track per-radar
 *                geometry under hand-authored HD layouts where each
 *                disc may be relocated / resized independently. */
typedef struct TieHudBlip {
	uint16_t color;
	uint8_t radar_idx;
	int16_t offset_x;
	int16_t offset_y;
} TieHudBlip;

/* (x, y, param1, param2) mirror panel.c's HudInstrument 1:1 — the
 * static layout loaded from the .INT file (reflows on VGA↔SVGA
 * resolution switch, hence shipped per-tick). param1/param2 are
 * widget-specific layout payloads (digit count, default color,
 * shape index, etc. — see panel.h for the polysemy).
 *
 * value/color carry the current painted state (the engine's
 * oldinstruments[idx] + the festring color panel_updatevalue picked).
 * value defaults to -1 ("never painted"); color to 0. */
typedef struct TieHudInstrument {
	uint16_t x;
	uint16_t y;
	uint8_t param1;
	uint8_t param2;
	int16_t value;
	uint8_t color;
	uint8_t digits;
} TieHudInstrument;

#define TIE_MAX_WEAPON_GROUPS 12

typedef struct TieHudState {
	/* Player-craft summary. */
	int16_t hull_damage; /* 0..hull_max: cumulative damage past shields */
	int16_t hull_max;    /* kill threshold; also denominator for hull-damage lever */
	/* Subsystem loadout / status (mirrored from CraftData). 13-bit
	 * mask, both fields use the same layout. installed_subsystems is
	 * set once at create-time; working_subsystems clears bits on damage
	 * and re-sets on repair. working_subsystems ⊆ installed_subsystems. */
	uint16_t installed_subsystems;
	uint16_t working_subsystems;
	uint16_t subsystem_active; /* on/off */
	/* Engine `CraftData.status_flags` (+0x0AE). Distinct from
	 * subsystem_active. Bit 4 (0x10) = lasers powered, bit 2 (0x04) =
	 * target lock-on enabled, bit 3 (0x08) = missiles enabled, bit 0
	 * (0x01) = shields up, bit 8 (0x100) = beam armed.
	 * Consumed by panel_updatelasers / shields / beam / etc. */
	uint16_t status_flags;
	/* Active player weapon-group count (cp->weapon_group_cnt). Bounds
	 * the LASER_LED_ROW iteration at indices 3..3+N-1. */
	uint8_t weapon_group_cnt;
	/* Hardpoints painted by panel_updateweapons: 0, 2, or 4, from the craft spec. */
	uint8_t missile_hardpoint_count;
	uint8_t beam_type;
	/* Per-LED palette index for the 9-LED beam-arc bar (idx 35).
	 * panel_updatebeam picks one of beamcolors[0..3] per LED based on
	 * the cumulative beam_charge bucket; the snapshot mirrors those
	 * choices so the HD draw doesn't replicate the bucketing math. */
	uint8_t beam_arc_led_colors[9];
	/* Power-distribution settings (`CraftData.laser_power` / shield_power /
	 * beam_power, +0x0D1 / +0x0CE / shared bank). Each 0..4 with 2 the
	 * neutral default; F8/F9/F10 cycle them. Drive the four vertical
	 * sliders at HUD instruments 26..29 via panel_updatepower. */
	uint8_t laser_power;
	uint8_t shield_power;
	uint8_t beam_power;
	uint8_t ion_drained; /* 1 if ion_drain_timer > 0 */
	/* `pstate.player_craft->slam_active` mirror. The HUD speed (idx 24)
	 * and throttle (idx 25) fields render in 0x52 grey when this is 0,
	 * mirroring panel_updatevalue's slam-off path. */
	uint8_t slam_active;
	uint16_t throttle_speed;
	/* Targeting. */
	uint16_t target_obj_slot; /* 0xFFFF = none */
	/* panel_buildobjectname output with 0xFE+color escape pairs
	 * preserved. Two-color split: 0xFE <primary_color> SHORT_NAME
	 * [": " 0xFE <secondary_color> FG_NAME [" " <count_digit>]]. The
	 * compose_text festring path consumes 0xFE escapes inline so the
	 * application ships the raw buffer to one TieUIText record. Empty when
	 * no target / no name resolved. */
	char target_name[32];
	uint16_t target_status;
	/* UI overlays. Bracket is anchor-relative — same convention as
	 * TieHudBlip.{radar_idx,offset_x,offset_y}. It sits on top of the
	 * targeted craft's blip, so it lives on the same radar disc. */
	int16_t blipbox_x, blipbox_y;
	uint8_t bracket_present;
	uint8_t bracket_radar_idx;
	int16_t bracket_offset_x;
	int16_t bracket_offset_y;
	uint8_t blipbox_present;
	uint8_t lock_present;
	uint16_t blip_count_left;
	uint16_t blip_count_right;
	/* Engine's radar disc radius in classic-px (44 for SVGA, 18 for VGA
	 * — math2_getradarcoord's boundary table max). Renderer uses this
	 * as the denominator when mapping blip / bracket offsets into the
	 * layout's ref-frame radar radius. */
	uint16_t radar_classic_radius;
	/* Instrument records (cockpit-relative position + the two state
	 * bytes panel_updatevalue / panel_updatelever drive). One-to-one
	 * with panel.c's instruments[] table. */
	TieHudInstrument instruments[TIE_MAX_HUD_INSTRUMENTS];
	/* Digit-field state painted by panel_updatevalue, indexed by the
	 * same idx the engine uses. Value is the raw uint the engine
	 * formatted; color is the resolved festring color (CRITICAL /
	 * WARNING / grayed / param2-default) the engine picked. */
	uint16_t instrument_value[TIE_MAX_HUD_INSTRUMENTS];
	uint8_t instrument_value_color[TIE_MAX_HUD_INSTRUMENTS];
	/* Radar blips, hemisphere-flattened. */
	TieHudBlip blips_left[TIE_MAX_RADAR_BLIPS];
	TieHudBlip blips_right[TIE_MAX_RADAR_BLIPS];

	/* Pre-formatted "MM:SS" painted into instrument[30] by
	 * panel_updateclock. */
	char mission_clock_text[6];

	/* Resolved string painted into instrument[65]. Empty = no text. */
	char target_subsystem_text[24];

	/* World-space inputs for aspect-independent target and subsystem boxes. */
	uint16_t target_bound_hwidth;      /* avg of spec_data extents <<mss;
										* 1995-look HD reads this. */
	uint16_t target_bound_hwidth_1998; /* selected component extent or whole-object
										* species bound; 1998-look HD reads this. */
	int32_t target_box_center_offset_1998[3];
	uint8_t target_box_engine_ok;
	uint8_t target_box_inputs_ok;
	/* Integral native offset relative to the target ship position. */
	int32_t target_subsystem_offset[3];
	uint8_t target_subsystem_box_engine_ok;

	/* Resolved string painted into instrument[63]. Empty = no text. */
	char target_cargo[24];
	/* Cargo painted by panel_updatethreatname at instrument 70. */
	char threat_cargo[24];

	/* Threat-view (pilotview 20) resolved text — engine paints these
	 * strings into the threat-view text slots; the snapshot mirrors
	 * what the engine wrote. Empty = no text (line not painted). */
	char target_order_text[48];        /* idx 79 value */
	char target_link_target_label[32]; /* idx 80 left col */
	char target_link_name[24];         /* idx 80 right col */
	char target_link_dist_label[32];   /* idx 81 left col */
	char target_link_dist_text[12];    /* idx 81 right col, "K.FF" */
	char target_eta_label[32];         /* idx 82 left col */
	char target_eta_text[8];           /* idx 82 right col, "MM:SS" / "UNKNOWN" */

	/* Training-mission CRT replaces the standard CMD readout
	 * when mission.train_craft_type != 0. Mirrors gate_trainingupdatecrt
	 * (gate.c:858-985) + gate_updatebonuspoints (gate.c:634-665).
	 * Renderer composes 5 label+digit pairs (LEVEL / REMAIN / PASSED /
	 * TARGETS / SCORE) plus a separate MM:SS timer + 5-digit bonus
	 * row. `player_spec_num` picks the left-vs-right-of-origin layout
	 * the engine selects from pstate.player_spec_num. */
	struct {
		uint8_t active;          /* mission.train_craft_type != 0 */
		uint8_t level;           /* mission.train_level */
		uint8_t player_spec_num; /* pstate.player_spec_num */
		uint8_t timer_min;       /* timeleft.minute */
		uint8_t timer_sec;       /* timeleft.second */
		/* gate.c's bonus_countdown_active — true only while the
		 * per-section countdown task is running. HD bonus bar
		 * emits exclusively when set; outside this window the
		 * classic cockpit-bitmap paint leaves the region bare. */
		uint8_t bonus_active;
		/* FlightObject slot for the player craft. Remaster-only consumers
		 * use the slot to resolve the coherent pose in flights[]. */
		uint16_t player_object_slot;
		uint16_t gates_remaining; /* mission.train_gates_remaining */
		uint16_t gates_passed;    /* mission.train_gates_passed */
		uint16_t targets_hit;     /* mission.train_targets */
		int32_t score;            /* mission.mission_score */
		int32_t bonus;            /* mission.train_bonus */
	} training;

	/* In-flight message banner — see src/tie/msg.c. Mirrors
	 * messagequeue[0] (the currently-displayed slot) plus the
	 * msgLineTop/Bottom/Right band geometry msg_messageinit set up.
	 * msg_messageprintf has already expanded the template +
	 * argtable / messageptrs into body[] by the time we snapshot,
	 * so the HD renderer just walks the bytes — no string-table
	 * lookup needed application-side. */
	struct {
		/* SVGA cockpit-coord (or VGA classic-coord) band rectangle:
		 *   line_top..line_bottom = main message area (backcolor 0x2C)
		 *   line_top - 1          = 1-px separator strip (color 0x2D)
		 *   line_right            = x where the time-warp indicator
		 *                           begins ("T:<N>x"). */
		uint16_t line_top;
		uint16_t line_bottom;
		uint16_t line_right;
		/* messagequeue[0].template_idx != 0xFFFF. When 0 the band is
		 * still painted (backcolor + separator + time-warp) but no
		 * message text is drawn. */
		uint8_t present;
		/* messagequeue[0].msg_type (clamped 0..7; 6 represents the
		 * template-byte-≥8 case). */
		uint8_t msg_type;
		/* messagequeue[0].side — faction side used to colour
		 * msg_type==2 (event) messages via eventsidecolors[side]. */
		uint16_t side;
		/* messagequeue[0].body[] verbatim. body[0] is the raw type
		 * prefix (renderer skips it; ≥8 case keeps body[0..] as text);
		 * body[1] may carry a '0'..'3' sub-side selector for type==1;
		 * '[' / ']' bytes in the remainder are color nudges (skipped
		 * and emit a dim/brighten color step). Max 70 emitted chars;
		 * the extra trailing byte gives the renderer a guaranteed
		 * NUL terminator on a 70-char body. */
		char body[71];
		/* acceleratedtimesetting — drives the "T:<N>x" warp indicator
		 * at line_right. 1 = normal speed; 2..N = accelerated. */
		uint8_t accelerated_time;
	} msg_bar;
} TieHudState;

#endif
