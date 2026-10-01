#include "tie/create.h"
#ifdef TIE_MODERN
#include "tie_runtime/storage/mission_records.h"
#endif
#include "tie/edition.h"
#include "tie/feinput.h"
#include "tie/paiman.h"
#include "tie_runtime/runtime/inflight_state.h"

#include "tie/backdrp2.h"
#include "tie/collide.h"
#include "tie/draw.h"
#include "tie/fediskio.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/score.h"
#include "tie/shell.h"
#include "tie/shipext.h"
#include "tie/spec.h"
#include "tie/species.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/trig2.h"
#include "tie/xtimer.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* CREATE-owned globals (watdbg: D:\GAMES\XTIE\CODE\create.c)         */
/* ------------------------------------------------------------------ */

// GLOBAL: TIE95 0xC17D8
// GLOBAL: TIE98 0x4DF7F0
uint8_t playerside = 1;

// GLOBAL: TIE95 0xC17DA
// GLOBAL: TIE98 0x4DF7F8
uint16_t skilltranslate[6] = { 0, 0x4000, 0x8000, 0xC000, 0xFFFF, 0xFFFF };
// GLOBAL: TIE95 0xC17E6
// GLOBAL: TIE98 0x4DF808
uint16_t aiupdatetranslate[6] = { 0x02C4, 0x01D8, 0x00EC, 0x0076, 0x003B, 0x001D };

// GLOBAL: TIE95 0xC17F2
// GLOBAL: TIE98 0x4DF818
uint8_t ordersldr[33] = {
	0x01, 0x2F, 0x03, 0x05, 0x27, 0x2A, 0x2B, 0x07, 0x08, 0x09, 0x14, 0x13, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
	0x21, 0x25, 0x42, 0x42, 0x38, 0x3A, 0x3B, 0x3C, 0x3C, 0x3E, 0x3F, 0x01, 0x35, 0x01, 0x22, 0x44,
};

/* Mission-file follower order to runtime order. */
// GLOBAL: TIE95 0xC1813
// GLOBAL: TIE98 0x4DF840
uint8_t ordersflw[33] = {
	0x02, 0x30, 0x04, 0x06, 0x29, 0x2A, 0x2B, 0x0E, 0x0E, 0x0E, 0x18, 0x0E, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
	0x1D, 0x04, 0x42, 0x42, 0x39, 0x3A, 0x3B, 0x39, 0x39, 0x39, 0x39, 0x02, 0x36, 0x02, 0x22, 0x44,
};

/* Mission-file species index to species_table index. */
// GLOBAL: TIE95 0xC1834
// GLOBAL: TIE98 0x4DF868
uint8_t speciesconvert[89] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x08, 0x08, 0x0C, 0x0D, 0x0E,
	0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
	0x1E, 0x1A, 0x20, 0x21, 0x22, 0x23, 0x20, 0x25, 0x26, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
	0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x37, 0x38, 0x39, 0x3A, 0x3B,
	0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
	0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x64, 0x57, 0xEB,
};

// GLOBAL: TIE95 0xC188D
// GLOBAL: TIE98 0x4DF8C8
uint8_t genusconvert[9] = { 0x00, 0x01, 0x03, 0x04, 0x02, 0x05, 0x08, 0x09, 0x02 };
// GLOBAL: TIE95 0xC1896
// GLOBAL: TIE98 0x4DF8D4
uint8_t familyconvert[4] = { 0x00, 0x01, 0x02, 0x00 };

// GLOBAL: TIE95 0xC189A
// GLOBAL: TIE98 0x4DF8D8
uint8_t warheadconvert[8] = { 0x00, 0x96, 0x97, 0x90, 0x8F, 0x95, 0x94, 0x98 };
// GLOBAL: TIE95 0xC18A2
// GLOBAL: TIE98 0x4DF8E0
uint16_t warheadadjust[8] = {
	0x0000, 0x4000, 0x8000, 0xFFFF, 0xC000, 0xFFFF, 0xC000, 0xFFFF,
};

// GLOBAL: TIE95 0xC18B2
// GLOBAL: TIE98 0x4DF8F0
uint8_t initialdamagestate[32] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x04, 0xFF, 0xFF, 0x40, 0xFF, 0x20, 0x30, 0x30, 0x30, 0x70, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x20, 0x30, 0x30, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// GLOBAL: TIE95 0xC18D2
// GLOBAL: TIE98 0x4DF910
uint8_t componentsgone[60] = {
	0x16, 0x17, 0x15, 0x14, 0x13, 0x05, 0x0F, 0x10, 0x11, 0x12, 0x18, 0x06, 0x03, 0x05, 0x1B,
	0x09, 0x0A, 0xFF, 0x01, 0x04, 0x1A, 0x07, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x05, 0x0D, 0x0F, 0x13, 0x14, 0x18, 0x0B, 0x0E, 0x06,
	0x11, 0x12, 0x17, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0xFF, 0x15, 0x16, 0x17, 0x18, 0x1E, 0xFF,
};

// GLOBAL: TIE95 0xC190E
// GLOBAL: TIE98 0x4DF950
uint8_t fgdiffmask[6] = { 0x07, 0x01, 0x02, 0x04, 0x06, 0x03 };
// GLOBAL: TIE95 0xC1914
// GLOBAL: TIE98 0x4DF958
uint8_t diffmask[4] = { 0x01, 0x02, 0x04, 0x00 };

// GLOBAL: TIE95 0xD3560
// GLOBAL: TIE98 0x62698C
int32_t fglocx;
// GLOBAL: TIE95 0xD3564
// GLOBAL: TIE98 0x626990
int32_t fglocy;
// GLOBAL: TIE95 0xD3568
// GLOBAL: TIE98 0x626994
int32_t fglocz;
// GLOBAL: TIE95 0xD356C
// GLOBAL: TIE98 0x626970
int16_t staging_static_x;
// GLOBAL: TIE95 0xD356E
// GLOBAL: TIE98 0x626972
int16_t staging_static_y;
// GLOBAL: TIE95 0xD3570
// GLOBAL: TIE98 0x626974
int16_t staging_static_z;
// GLOBAL: TIE95 0xD3572
// GLOBAL: TIE98 0x626976
int8_t staging_static_heading;
// GLOBAL: TIE95 0xD3573
// GLOBAL: TIE98 0x626977
int8_t staging_static_pitch;
// GLOBAL: TIE95 0xD3574
// GLOBAL: TIE98 0x626978
int8_t staging_static_roll;
// GLOBAL: TIE95 0xD3578
// GLOBAL: TIE98 0x626986
uint16_t craftcnt;
// GLOBAL: TIE95 0xD3576
// GLOBAL: TIE98 0x62699C
uint16_t fgcnt;
// GLOBAL: TIE95 0xD357A
// GLOBAL: TIE98 0x62697E
int16_t fgheadingxy;
// GLOBAL: TIE95 0xD357C
// GLOBAL: TIE98 0x626984
int16_t fgheadingz;
// GLOBAL: TIE95 0xD357F
// GLOBAL: TIE98 0x626981
uint8_t fgversion;
// GLOBAL: TIE95 0xD3580
// GLOBAL: TIE98 0x626982
uint8_t fghangar;
// GLOBAL: TIE95 0xD3581
// GLOBAL: TIE98 0x626980
uint8_t fghyperspace;
// GLOBAL: TIE95 0xD3582
// GLOBAL: TIE98 0x626998
uint8_t fggenus;
// GLOBAL: TIE95 0xD3586
// GLOBAL: TIE98 0x626979
uint8_t leaderflag;
// GLOBAL: TIE95 0xD3589
// GLOBAL: TIE98 0x626988
uint8_t fgspecies;
// GLOBAL: TIE95 0xD357E
// GLOBAL: TIE98 0x62697B
uint8_t fgseparation;
// GLOBAL: TIE95 0xD3583
// GLOBAL: TIE98 0x62699A
uint8_t fgformation;
// GLOBAL: TIE95 0xD3584
// GLOBAL: TIE98 0x626989
uint8_t fgflightflag;
// GLOBAL: TIE95 0xD3585
// GLOBAL: TIE98 0x62697D
uint8_t fgskill;
// GLOBAL: TIE95 0xD3587
// GLOBAL: TIE98 0x62697A
uint8_t fgside;
// GLOBAL: TIE95 0xD3588
// GLOBAL: TIE98 0x626999
uint8_t fgsidecreated;

/* staticobjects[] is tie.c-owned per watdbg; declared in tie.h. */

/* watdbg owner msg.c; kept here for clarity */

/* trig2 working globals (angular conversions write these). */

/* ============================================================== */
/*   Leaf functions                                                */
/* ============================================================== */

// FUNCTION: TIE95 0x19FA0
uint16_t create_maxrandom(uint16_t max) {
	if (!max)
		return 0;
	return (uint16_t)((uint16_t)math2_getrandom() % max);
}

// FUNCTION: TIE95 0x19CEC
uint16_t create_findslot(uint16_t genus_idx) {
	const uint16_t start = genus_table[genus_idx].start;
	const uint16_t end = genus_table[genus_idx].limit;
	uint16_t i;

	for (i = start; i < end; i++) {
		if (!objects[i].ship_idx) {
			objects[i].self_idx = 0;
			objects[i].damage_state = 0;
			return i;
		}
	}
	return 0xFFFF;
}

// FUNCTION: TIE95 0x19D44
uint16_t create_findstaticslot(void) {
	uint16_t i;

	for (i = 0; i < NUM_STATIC_OBJECTS; i++)
		if (!staticobjects[i].species)
			return i;
	return 0xFFFF;
}

/* Resolve a 16-bit object reference to the worldlocx/y/z globals. See
 * header comment for the encoding ranges. */
// FUNCTION: TIE95 0x196C0
void create_getworldposition(uint16_t obj_or_kind, int fg_idx) {
	int32_t wx, wy, wz;

	if (obj_or_kind < OBJ_REF_STATIC_BASE) {
		/* FlightObject slot: raw world XYZ (Q16.16). */
		const FlightObject* o = &objects[obj_or_kind];
		wx = o->world_x;
		wy = o->world_y;
		wz = o->world_z;
	} else if (obj_or_kind < OBJ_REF_WAYPOINT_BASE) {
		/* Static slot: (wx,wy,wz) = staticobjects[i].world_* << 8 (promote
		 * int16 grid-coord to Q16.8 world units). */
		const StaticObject* s = &staticobjects[obj_or_kind - OBJ_REF_STATIC_BASE];
		/* * 256 instead of << 8: same numeric result as the binary's
		 * `shl 8`, but well-defined for negative int16 coords. */
		wx = (int32_t)s->world_x * 256;
		wy = (int32_t)s->world_y * 256;
		wz = (int32_t)s->world_z * 256;
	} else {
		/* Waypoint reference: OBJ_REF_WAYPOINT_BASE is the sentinel
		 * "current waypoint" (indirects via fgstatus[fg].world_position,
		 * itself encoded the same way). Any other value encodes the
		 * specific waypoint index in the low bits. */
		uint16_t wp_code = obj_or_kind;
		uint16_t wp;
		const EFGStruct* f;

		if (wp_code == OBJ_REF_WAYPOINT_BASE)
			wp_code = fgstatus[fg_idx].world_position;
		/* Binary subtracts 0x8000 (HIBYTE += 0x80). Equivalent to the
		 * mask only when wp_code >= 0x8000; we preserve the subtract
		 * form so an out-of-range world_position (from a buggy FG state)
		 * resolves the same way the original code did. */
		wp = (uint16_t)(wp_code - OBJ_REF_WAYPOINT_BASE); /* 0..14 */
		f = &fg_array[fg_idx];
		/* * 256 instead of << 8 to avoid signed left-shift UB. Y is
		 * negated to match the binary's trailing `neg edx`. */
		wx = (int32_t)f->way_x[wp] * 256;
		wy = -(int32_t)f->way_y[wp] * 256;
		wz = (int32_t)f->way_z[wp] * 256;
	}
	worldlocx = wx;
	worldlocy = wy;
	worldlocz = wz;
}

/* ============================================================== */
/*   Static-object spawning                                        */
/* ============================================================== */

// FUNCTION: TIE95 0x195D0
int create_createstaticobject(uint16_t fg_idx, uint8_t ship_class, uint8_t species_idx) {
	const uint16_t slot = create_findstaticslot();
	StaticObject* s;

	if (slot == 0xFFFF)
		return 0xFFFF;

	s = &staticobjects[slot];
	s->world_x = staging_static_x;
	s->world_y = staging_static_y;
	s->world_z = staging_static_z;
	s->heading_byte = staging_static_heading;
	s->pitch_byte = staging_static_pitch;
	s->roll_byte = staging_static_roll;
	s->idnumber = idnumber;
	s->ship_class = ship_class;
	s->status_flags = 0x3FF;
	s->anim_frame = 0;
	s->mine_cooldown = 0;
	s->fg_idx = (uint8_t)fg_idx;
	s->species = species_idx;

	idnumber++;
	fgstatus[fg_idx].cond[0].detail++;
	return slot;
}

/* ============================================================== */
/*   Debris / ember / component spawn                              */
/* ============================================================== */

// FUNCTION: TIE95 0x19A6C
uint16_t create_createcomponent(uint16_t parent_obj, uint8_t mesh_idx) {
	const uint16_t slot = create_findslot(11);
	FlightObject* n;
	const FlightObject* p;

	if (slot == 0xFFFF)
		return 0xFFFF;

	n = &objects[slot];
	p = &objects[parent_obj];
	memcpy(n, p, sizeof(FlightObject));

	n->category = 3;
	n->genus = GENUS_DEBRIS;
	n->ship_idx = 89; /* sparks2 / mesh-debris base */
	n->damage_state = 0;
	n->ship_type_override = p->ship_idx;
	n->age_ticks = 0;
	n->death_timer = (int16_t)(236 * ((math2_getrandom() & 7) + 4));
	n->anim_frame_alt = 0;
	n->anim_frame = (uint8_t)(2 * mesh_idx);
	return slot;
}

// FUNCTION: TIE95 0x19B48
uint16_t create_createember(uint16_t parent_obj) {
	const uint16_t slot = create_findslot(GENUS_EXPLOSION);
	FlightObject* n;
	const FlightObject* p;
	uint16_t rh;
	int16_t heading_delta;
	uint16_t rp;
	int16_t pitch_delta;
	int32_t new_pitch;
	uint8_t rand_speed;

	if (slot == 0xFFFF)
		return 0xFFFF;

	n = &objects[slot];
	p = &objects[parent_obj];
	memcpy(n, p, sizeof(FlightObject));

	n->category = 5;
	n->genus = GENUS_EXPLOSION;
	n->damage_state = 0;
	/* Binary does `and al,1 ; add al,0x85` -> ship_idx in {0x85, 0x86}. */
	n->ship_idx = (uint8_t)((math2_getrandom() & 1) + 0x85);
	n->ship_type_override = p->ship_idx;

	/* Heading/pitch jitter. Watcom pattern is:
	 *     LOWORD(delta) = random16;
	 *     BYTE1(delta)  = (BYTE1(delta) & 7) + 1;
	 * which yields a 32-bit value whose low byte is a random 0..255 and
	 * whose byte 1 is 1..8 (with bytes 2-3 = 0). Net range: [256..2303]. */
	rh = (uint16_t)math2_getrandom();
	heading_delta = (int16_t)((rh & 0xFF) | ((((rh >> 8) & 7) + 1) << 8));
	rp = (uint16_t)math2_getrandom();
	pitch_delta = (int16_t)((rp & 0xFF) | ((((rp >> 8) & 7) + 1) << 8));
	if (math2_getrandom() & 1)
		heading_delta = -heading_delta;
	if (math2_getrandom() & 1)
		pitch_delta = -pitch_delta;

	n->heading += heading_delta;
	new_pitch = (int32_t)n->pitch + pitch_delta;
	n->pitch = (int16_t)new_pitch;
	if ((uint16_t)n->pitch >= 0x8000u) {
		/* Pitched past a pole: reflect the pitch and turn the heading by 180°. */
		n->pitch = (int16_t)-(int32_t)(uint16_t)new_pitch;
		n->heading = (int16_t)(n->heading + 0x8000);
	}

	n->orient_dirty = 1;
	n->move_dirty = 1;

	rand_speed = (uint8_t)math2_getrandom();
	n->age_ticks = 0;
	n->current_speed = (int16_t)(p->current_speed + rand_speed + 50);
	n->anim_frame = 0;
	n->death_timer = (int16_t)(236 * ((math2_getrandom() & 3) + 1));
	return slot;
}

// FUNCTION: TIE95 0x198A4
int16_t create_blowoffcomponent(uint16_t obj_idx, int16_t stop_after_first) {
	FlightObject* parent = &objects[obj_idx];
	uint16_t num_meshes;
	int16_t result;
	CraftData* cp;
	uint16_t mi;

	if (!TIE_FLIGHT_TIE98)
		draw_lockshipfileptrs(parent->ship_idx);

	num_meshes =
		TIE_FLIGHT_EDITION(objectblockptr->num_meshes, (uint16_t)modelmesh_getcount(parent->ship_idx));
	result = (int16_t)num_meshes;
	if (num_meshes <= 1)
		return result;

	cp = parent->craft_ptr;

	for (mi = 0; mi < num_meshes; ++mi) {
		uint16_t debris;
		uint16_t rs;
		int16_t spin;
		uint16_t rdh;
		int16_t dheading;
		uint16_t rdp;
		int16_t dpitch;
		FlightObject* d;
		int16_t heading_n;

		if (cp->mesh_state[mi] != MESH_STATE_VISIBLE)
			continue;
		if (TIE_FLIGHT_TIE98) {
			if (!modelmesh_isobjecttypemeshdamageable(parent->ship_idx, mi))
				continue;
		} else if (!(componentblockptr[mi].flags & 2)) {
			continue;
		}

		debris = create_createcomponent(obj_idx, (uint8_t)mi);
		result = (int16_t)debris;
		if (debris == 0xFFFF)
			break;

		/* Same LOWORD/BYTE1 pattern as createember: the delta is
		 * random_lo | ((random_hi_masked + K) << 8). */
		rs = (uint16_t)math2_getrandom();
		spin = (int16_t)((rs & 0xFF) | ((((rs >> 8) & 0x3F) + 64) << 8));
		rdh = (uint16_t)math2_getrandom();
		dheading = (int16_t)((rdh & 0xFF) | ((((rdh >> 8) & 0x07) + 4) << 8));
		rdp = (uint16_t)math2_getrandom();
		dpitch = (int16_t)((rdp & 0xFF) | ((((rdp >> 8) & 0x0F) + 4) << 8));
		if (math2_getrandom() & 1) {
			spin = -spin;
			dheading = -dheading;
		}
		if (math2_getrandom() & 1)
			dpitch = -dpitch;

		d = &objects[debris];
		d->spin_rate = spin;
		heading_n = (int16_t)(d->heading + dheading);
		d->pitch = (int16_t)(d->pitch + dpitch);
		d->heading = heading_n;
		if ((uint16_t)d->pitch >= 0x8000u) {
			d->pitch = (int16_t)-(int32_t)(uint16_t)d->pitch;
			d->heading = (int16_t)(d->heading + 0x8000);
		}
		d->orient_dirty = 1;
		d->move_dirty = 1;
		d->death_timer = (int16_t)(236 * ((math2_getrandom() & 1) + 1));
		d->anim_frame_alt = 2;

		cp->mesh_state[mi] = MESH_STATE_BLOWN_OFF;
		/* [num_meshes] is the overlaid lightning anim frame counter,
		 * not a per-mesh state. 2 = jump the bolt script to frame 2. */
		cp->mesh_state[num_meshes] = 2;
		result = (int16_t)num_meshes;

		if (stop_after_first)
			break;
	}
	return result;
}

/* Cycle through the 8 reusable debris slots (DEBRIS_FIRST_SLOT..NUM_OBJECTS-1).
 * Retail: 112..119; demo was 108..115. When the current slot has drifted
 * more than 0x800 fixed-point units from the player, respawn it with a
 * random spark/dust species at a random offset in the player's forward-
 * below-side frame. Called once per frame. */
// FUNCTION: TIE95 0x19D74
void create_checkdebris(void) {
	const uint16_t slot = currentdebrisslot++;
	FlightObject* o;
	FlightObject* pl;
	int32_t dx;
	int32_t dy;
	int32_t dz;
	int16_t ra;
	int16_t rb;
	int32_t sx_scaled;
	int32_t sy_scaled;
	int32_t sz_scaled;
	int32_t ux_scaled;
	int32_t uy_scaled;
	int32_t uz_scaled;
	int32_t below_x;
	int32_t below_y;
	int32_t below_z;

	if (currentdebrisslot == NUM_OBJECTS)
		currentdebrisslot = DEBRIS_FIRST_SLOT;

	o = &objects[slot];
	pl = pstate.player;
	dx = o->world_x - pl->world_x;
	dy = o->world_y - pl->world_y;
	dz = o->world_z - pl->world_z;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (dz < 0)
		dz = -dz;
	if (collide_roughdistance3du(dx, dy, dz) <= 0x800)
		return;

	o->ship_idx = (uint8_t)((math2_getrandom() & 3) + 110);
	o->genus = GENUS_DEBRIS;
	o->fg_idx = (uint8_t)-1;
	o->category = 3;

	if (pl->orient_dirty) {
		fview_calcrotatemove(pl->pitch, pl->heading, pl);
		fview_calcrotateorient(pl->roll, 0, pl);
	}

	/* Two Q8.8 random scalars in [-512, 0x3FF+1-512]: rand produces a
	 * signed int16 where the high byte is masked to 2 bits. `rand - 512`
	 * biases it around 0. Applied as a Q15 multiplier against the
	 * player's fwd/side/up vectors. */
	ra = (int16_t)(math2_getrandom() & 0x03FF); /* HIBYTE & 3 */
	ra = (int16_t)(ra - 512);
	rb = (int16_t)(math2_getrandom() & 0x03FF);
	rb = (int16_t)(rb - 512);

	/* Scatter perpendicular to fwd: side*ra + up*rb. */
	sx_scaled = ((int32_t)pl->side_x * ra) >> 15;
	sy_scaled = ((int32_t)pl->side_y * ra) >> 15;
	sz_scaled = ((int32_t)pl->side_z * ra) >> 15;
	ux_scaled = ((int32_t)pl->up_x * rb) >> 15;
	uy_scaled = ((int32_t)pl->up_y * rb) >> 15;
	uz_scaled = ((int32_t)pl->up_z * rb) >> 15;

	/* Forward displacement scaled 1/16 of the forward vector. */
	below_x = pl->fwd_x >> 4;
	below_y = pl->fwd_y >> 4;
	below_z = pl->fwd_z >> 4;

	o->world_x = pl->world_x + (int16_t)(below_x + sx_scaled + ux_scaled);
	o->world_y = pl->world_y + (int16_t)(below_y + sy_scaled + uy_scaled);
	o->world_z = pl->world_z + (int16_t)(below_z + sz_scaled + uz_scaled);
	o->anim_frame = 2;
}

/* ============================================================== */
/*   Mission lifecycle                                             */
/* ============================================================== */

// FUNCTION: TIE95 0x16B04
CraftData* create_createhyperin(void) {
	uint16_t i;

	create_createmission();
	for (i = 0; i < NUM_OBJECTS; i++) {
		if (i == pstate.object_idx)
			continue;
		objects[i].ship_idx = 0;
		objects[i].age_ticks = 0;
		objects[i].death_timer = 0;
	}
	for (i = 0; i < NUM_STATIC_OBJECTS; i++)
		staticobjects[i].species = 0;

	pstate.player->roll = 0;
	pstate.player->pitch = 0x4000;
	pstate.player_craft->orient_pitch = 0x4000;
	mission_file_header.num_fg = 0;
	pstate.player->heading = 0;
	return pstate.player_craft;
}

// FUNCTION: TIE95 0x16B8C
// FUNCTION: TIE98 0x411200
int16_t create_createmission(void) {
	uint16_t saved_fgcnt;
	int i;
	int si;

	idnumber = 0;

	for (fgcnt = 0; fgcnt < (uint16_t)mission_file_header.num_fg; fgcnt++) {
		FGStatus* st;
		EFGStruct* f;
		uint8_t sidx;
		uint16_t count;

		mission.primary_fg[fgcnt] = 0;
		mission.secondary_fg[fgcnt] = 0;
		mission.bonus_fg[fgcnt] = 0;

		st = &fgstatus[fgcnt];
		st->active = 0;
		st->arrival_triggered = 0;
		st->waves_remaining = 0;

		f = &fg_array[fgcnt];
		sidx = speciesconvert[f->species];
		count = f->count;
		if (species_table[sidx].ship_class == 8)
			count = (uint16_t)(count * count);
		if (f->link_flag) {
			const uint16_t sub = mission.mission_linked_data[f->link_code];
			count = (uint16_t)(count - sub);
			if (count & 0x8000u)
				count = 0;
		}

		/* Clear counts and spawned-craft progress from the previous mission. */
		memset(st->cond, 0, sizeof(st->cond));
		memset(st->cond_id, 0, sizeof(st->cond_id));

		if (diffmask[mission.difficulty] & fgdiffmask[f->difficulty]) {
			st->cond[0].count = (uint8_t)(count * (f->waves + 1));
			if (f->special_flag || f->special_craft < f->count)
				st->cond_id[0].count = (uint8_t)(f->waves + 1);
		}

		st->primary_status = 4;
		st->secondary_status = 4;
		st->fg_complete = 4;

		/* Immediate-spawn test: active, in-difficulty, no arrival trigger. */
		if (f->species && (diffmask[mission.difficulty] & fgdiffmask[f->difficulty]) && st->cond[0].count &&
			!f->start_cond[0].cond && !f->start_delay_min && !f->start_delay_sec) {
			create_startflightgroup(-1, (int16_t)fgcnt);
		}
	}

	/* A loaded panel cache is retained across replay restoration. */
	if (!panelsloadedflag) {
		fsfx_loadvoicelfd();
		panel_loadpaneldata();
	}

	saved_fgcnt = fgcnt;
	currentdebrisslot = DEBRIS_FIRST_SLOT;
	acceleratedtimesetting = 1;
	hyperspaceflag = 0;
	hyperabortflag = 1;
	currenttarget = 0xFFFF;
	targetblinkstate = 0;
	targetblinkflag = 0;
	entercombatflag = 0;
	pstate.hyperin_state = 0;
	pstate.radar_enable = 1;
	/* Preserve object_idx, which was bound while creating the player's craft. */
	pstate.target_obj_idx = 0xFFFFu;
	pstate.radar_target0 = pstate.radar_target1 = pstate.radar_target2 = -1;
	for (i = 0; i < 4; i++)
		pstate.target_presets[i] = 0xFFFFu;
	pstate.radar_subtarget_state = 0;
	pstate.radio_target = -1;

	for (i = 0; i < 10; i++) {
		pstate.subsystem_repair_priority[i] = (uint8_t)i;
		pstate.subsystem_health_percent[i] = 100;
		pstate.subsystem_repair_seconds[i] = 0;
	}
	for (i = 0; i < 20; i++)
		timers[i] = 0;

	camera.view_dir_dirty = 0;
	camera.side_angle = 0;
	camera.up_angle = 0;
	camera.view_camera_control = 0;
	camera.view_target_tracking = 0;
	camera.view_zoom_flag = 0;
	camera.view_zoom = 0x400;
	panel_forcenewviewdir(0);
	keypress = 0;

	/* Clear prev_x_roll_mode and the four adjacent input accumulators.
	 * Retail writes at 0xEB182, 0xEB184, 0xEB186, 0xEB188, 0xEB18A —
	 * pstate base + {0x2A, 0x2C, 0x2E, 0x30, 0x32}. msg_arg_obj_idx at
	 * +0x28 is intentionally preserved across the mission boundary
	 * (the binary's `& 0xFFFF` keeps it). */
	pstate.prev_x_roll_mode = 0;
	pstate.axis_x_accum = 0;
	pstate.axis_y_accum = 0;
	pstate.axis_roll_accum = 0;
	pstate.prev_inputbuttons = 0;
	pstate.double_tap_timer = 0;

	camera.view_target_obj = pstate.object_idx;
	thrustmastertopflag = 0;
	framerate = 15;
	if (TIE_FLIGHT_TIE98)
		g_flightInitialTextureCacheFlushPending = 1;
	fullupdateflag = 1;
	calcframerate = 1;
	xtimer_time_elapsed();
	tickcounter = 0;
	messagecnt = 0;

	for (si = 0; si < NUM_SPEC; si++) {
		int row;

		for (row = 0; row < 6; row++)
			mission.kills_losses[row][si] = 0;
		mission.kills_by_type[si] = 0;
		mission.captures_by_type[si] = 0;
	}
	for (i = 0; i < 16; i++)
		mission.radiomsg_triggered[i] = 0;

	mission.mission_new_rank = 0;
	mission.mission_medal = 0;
	mission.mission_secret_medal = 0;
	mission.player_status = 3;
	mission.end_flag = 0;
	mission.primary_complete = 0;
	mission.secondary_complete = 0;
	mission.bonus_complete = 0;
	mission.penalty_flag = 0;

	cheatingflag = (uint8_t)(inflight_invulnerable | inflight_unlimited);
	fgcnt = saved_fgcnt;
	return (int16_t)inflight_invulnerable;
}

/* ============================================================== */
/*   Mission file loader                                           */
/* ============================================================== */

// FUNCTION: TIE95 0x165C0
int16_t create_loadmission(const char* filename) {
	uint16_t i, species_index;
#ifdef TIE_MODERN
	uint8_t mfh_buf[MISSIONFILE_DISK_SIZE];
	uint8_t fg_buf[48 * EFGSTRUCT_DISK_SIZE];
#endif
	uint16_t wall_offsets[6];
	uint16_t saved_fgcnt = fgcnt;
	uint16_t n_fg;
	int16_t file_num_msg;
	uint16_t num_msg;
	int16_t file_num_goals;
	uint16_t num_goals;
	int16_t saved_seed;

	mission.train_craft_type = mission.train_craft_type_src;
	framerate = baseframerate;
	pstate.object_idx = 0xFF;
	playerside = 1;

	for (i = 0; i < NUM_OBJECTS; i++) {
		objects[i].ship_idx = 0;
		objects[i].death_timer = 0;
		objects[i].age_ticks = 0;
		objects[i].orient_dirty = 0;
	}
	for (i = 0; i < NUM_STATIC_OBJECTS; i++)
		staticobjects[i].species = 0;
	for (i = 0; i < NUM_CRAFTS; i++) /* craft slot side reset */
		objects[i].side = (uint8_t)-1;

	for (species_index = 0; species_index < NUM_SPECIES; species_index++)
		if (!species_table[species_index].flags)
			species_table[species_index].load_flags &= ~0x10u;

	/* Pre-clear the active flag on every radiomsg slot so the upcoming
	 * fediskio_readfileblock load doesn't carry over previous-mission
	 * stale entries when the new mission has fewer cut/radio cues. */
	for (i = 0; i < 16; i++)
		radiomsg[90 * i] = 0;

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb", 1)) {
		fgcnt = saved_fgcnt;
		return 0;
	}

	fediskio_readfileblock(&missionversion, 2, 1, fileptr);
	if ((int16_t)missionversion > 0)
		TieStorage_Seek(fileptr, 0, TIE_SEEK_SET);
#ifdef TIE_MODERN
	fediskio_readfileblock(mfh_buf, MISSIONFILE_DISK_SIZE, 1, fileptr);
	MissionFile_decode(&mission_file_header, mfh_buf);
#else
	fediskio_readfileblock(&mission_file_header, MISSIONFILE_DISK_SIZE, 1, fileptr);
#endif
	if (mission_file_header.num_msg < 0 || mission_file_header.num_goals < 0)
		shell_programexit("Mission file has a negative message or goal count");

	/* Stage the FG records as the on-disk byte image, then decode each
	 * into the natively-aligned fg_array. The .TIE format hard-caps
	 * num_fg at 48; clamp here so a malformed file cannot overrun the
	 * buffer or fg_array (the original binary trusted num_fg blindly). */
	n_fg = (uint16_t)mission_file_header.num_fg;
	if (n_fg > 48u)
		n_fg = 48u;
#ifdef TIE_MODERN
	fediskio_readfileblock(fg_buf, EFGSTRUCT_DISK_SIZE, n_fg, fileptr);
	for (i = 0; i < n_fg; ++i)
		EFGStruct_decode(&fg_array[i], fg_buf + i * EFGSTRUCT_DISK_SIZE);
#else
	fediskio_readfileblock(fg_array, EFGSTRUCT_DISK_SIZE, n_fg, fileptr);
#endif
	mission_file_header.num_fg = (int16_t)n_fg;

	/* Mission files store messages before goals. Clamp both counts and skip
	 * excess message records so goal decoding remains aligned. */
	file_num_msg = mission_file_header.num_msg;
	num_msg = (uint16_t)file_num_msg;
	if (num_msg > 16u)
		num_msg = 16u;
	fediskio_readfileblock(radiomsg, 0x5A, num_msg, fileptr);
	if ((uint16_t)file_num_msg > num_msg)
		TieStorage_Seek(fileptr, (long)((uint16_t)file_num_msg - num_msg) * 0x5A, TIE_SEEK_CUR);
	mission_file_header.num_msg = (int16_t)num_msg;

	file_num_goals = mission_file_header.num_goals;
	num_goals = (uint16_t)file_num_goals;
	if (num_goals > 4u)
		num_goals = 4u;
	fediskio_readfileblock(cut, 0x1C, num_goals, fileptr);
	mission_file_header.num_goals = (int16_t)num_goals;

	math2_getrandom();

	pstate.player_fg_idx = 0xFF;
	for (i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
		EFGStruct* f = &fg_array[i];
		const uint8_t sp = speciesconvert[f->species];
		species_table[sp].load_flags |= 0x10u;
		if (sp == 100) { /* expansion-pack capital family */
			int k;

			for (k = 101; k <= 0x69; k++)
				species_table[k].load_flags |= 0x10u;
		}
		/* First flight group flagged as the player wins; later
		 * player_flag entries are ignored (matches retail's
		 * `player_fg_idx == 255` first-wins guard). */
		if (f->player_flag && pstate.player_fg_idx == 0xFF) {
			pstate.player_fg_idx = (uint8_t)i;
			pstate.player_spec_num = (uint8_t)spec_getspecnum(sp);
		}
		fgstatus[i].world_position = OBJ_REF_WAYPOINT_BASE;
		if (f->special_flag)
			f->special_craft = (uint8_t)create_maxrandom(f->count);
	}

	if (fediskio_tryclosefile(0)) {
		fgcnt = saved_fgcnt;
		return 0;
	}

	/* Backdrop: reseed RNG with the mission's stored seed so skybox is
	 * deterministic, then restore the live seed. */
	saved_seed = math2_randomseed;
	/* The signed backdrop byte is the skybox RNG seed. */
	math2_randomseed = (int16_t)(int8_t)mission_file_header.mission.backdrop;
	create_createbackdrop();
	math2_randomseed = saved_seed;

	/* Start offsets for [front, back, left, right, top, bottom] backdrop tiles. */
	wall_offsets[0] = 0;
	wall_offsets[1] = backdropfrontcnt;
	wall_offsets[2] = (uint16_t)(backdropfrontcnt + backdropbackcnt);
	wall_offsets[3] = (uint16_t)(wall_offsets[2] + backdropleftcnt);
	wall_offsets[4] = (uint16_t)(wall_offsets[3] + backdroprightcnt);
	wall_offsets[5] = (uint16_t)(wall_offsets[4] + backdroptopcnt);

	for (i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
		EFGStruct* f = &fg_array[i];
		uint16_t sp;
		uint16_t wall_kind;
		uint8_t tile_byte;
		uint16_t slot;

		if (!f->species)
			continue;

		sp = speciesconvert[f->species];
		if (!(species_table[sp].side & 0x20))
			continue;

		if (sp == 87) {
			/* Planet version selects both the resource variant and palette. */
			uint16_t p = 114;
			uint8_t version;

			while (p < NUM_OBJECTS && (species_table[p].load_flags & 0x10))
				p++;
			version = f->version;
			if ((int8_t)version < 8) {
				species_table[p].lfd_entry = (uint8_t)(version + species_table[sp].lfd_entry);
				species_table[p].bitmap_data = planetpalptrs[version];
			}
			sp = p;
		}

		species_table[sp].load_flags |= 0x10u;

		/* way_z[0] in the file is re-interpreted here as wall id (0..5). */
		wall_kind = (uint16_t)f->way_z[0];
		if (wall_kind > 5)
			wall_kind = 5;
		tile_byte = (uint8_t)((f->way_x[0] & 0x0F) | (((-(int8_t)f->way_y[0]) & 0x0F) << 4));
		slot = wall_offsets[wall_kind];
		backdropspecies[slot] = (uint8_t)sp;
		backdropposition[slot] = tile_byte;
		wall_offsets[wall_kind] = (uint16_t)(slot + 1);
	}

	/* Reset the mission elapsed clock; tie_updatetime ticks subsec each
	 * frame and cascades into second/minute/hour. Without this, the
	 * previous mission's elapsed time persists into the new mission and
	 * skews HUD readouts plus any `_date.minute >= N` triggers. The
	 * reserved_0..2 bytes are intentionally not cleared (binary parity).
	 * Matches retail's byte writes at 0x16A4B-0x16A69. */
	_date.hour = 0;
	_date.minute = 0;
	_date.second = 0;
	_date.subsec = 0;
	timeleft.hour = 0;

	if (mission.train_craft_type) {
		/* Training mode: swap in the player's chosen training ship. */
		fg_array[0].species = mission.train_craft_type_src;
		species_table[speciesconvert[mission.train_craft_type_src]].load_flags |= 0x10u;
		timeleft.second = 59;
		timeleft.minute = (uint8_t)(10 - mission.train_level);
	} else {
		/* Default the mission timer to 20:mission.time_sec when the
		 * .TIE record has time_min==0 && time_sec==0; otherwise use
		 * the file's specified time_min. Matches the binary's
		 * `time_min + time_sec` zero check at 0x16A85. */
		if (mission_file_header.mission.time_min + mission_file_header.mission.time_sec)
			timeleft.minute = mission_file_header.mission.time_min;
		else
			timeleft.minute = 20;
		timeleft.second = mission_file_header.mission.time_sec;
	}

	fgcnt = saved_fgcnt;
	return 1;
}

/* ============================================================== */
/*   Backdrop                                                      */
/* ============================================================== */

// FUNCTION: TIE95 0x197B4
void create_createbackdrop(void) {
	uint16_t i;

	backdropfrontcnt = 4;
	backdropbackcnt = 4;
	backdropleftcnt = 4;
	backdroprightcnt = 4;
	backdroptopcnt = 3;
	backdropbottomcnt = 3;

	/* 22 tile-direction descriptors: each is 16*lo_nibble + hi_nibble
	 * where each nibble is (rand & 0xE) + 4 looped until <= 0xC.
	 * Retail CREATE_createbackdrop uses the C library rand() (cosmetic starfield
	 * RNG), not MATH2_getrandom (mission-deterministic RNG). */
	for (i = 0; i < 22; i++) {
		int hi, lo;
		do {
			hi = (rand() & 0xE) + 4;
		} while (hi > 0xC);
		do {
			lo = (rand() & 0xE) + 4;
		} while (lo > 0xC);
		backdropposition[i] = (uint8_t)(hi + 16 * lo);
	}

	/* backdropspecies[0..21]: weighted pick -- 3/32 → planet (117),
	 * 9/32 → ramp 117..122, 20/32 → misc planets 125/126. */
	for (i = 0; i < 22; i++) {
		const int r = rand() & 0x1F;
		int pick;
		if (r < 3)
			pick = 117;
		else if (r >= 0xC)
			pick = 125 + (r & 1);
		else
			pick = (r / 3) + 117;
		backdropspecies[i] = (uint8_t)pick;
	}
}

/* ============================================================== */
/*   FG activation                                                 */
/* ============================================================== */

// FUNCTION: TIE95 0x1706C
int create_startflightgroup(int16_t craft_slot, int16_t fg_idx) {
	uint8_t sp;

	fgstatus[fgcnt].active = 1;
	sp = speciesconvert[fg_array[fgcnt].species];

	if (species_table[sp].side & 0x80) {
		create_createstaticflightgroup(craft_slot);
		fgstatus[fgcnt].waves_remaining = 0;
	} else {
		create_createflightgroup(craft_slot, fg_idx);
		fgstatus[fgcnt].waves_remaining = fg_array[fgcnt].waves;
	}
	return 1;
}

/* ============================================================== */
/*   Per-frame FG spawn driver                                     */
/* ============================================================== */

// FUNCTION: TIE95 0x1713C
void create_updatefgstatus(void) {
	CraftData* saved_cp = craftptr;

	if (!timers[TIMER_FG_ARRIVAL]) {
		uint16_t i;

		timers[TIMER_FG_ARRIVAL] = 236;
		for (i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
			FGStatus* st = &fgstatus[i];
			EFGStruct* f = &fg_array[i];

			if (!st->active && !st->arrival_triggered) {
				/* Arrival-condition check for dormant FG. */
				const uint8_t m = (uint8_t)(diffmask[mission.difficulty] & fgdiffmask[f->difficulty]);
				const ECondStruct* c0;
				const ECondStruct* c1;
				int8_t ok0;
				int8_t ok1;
				int8_t combined;

				craftptr = saved_cp;
				if (!m || !st->cond[0].count)
					continue;

				/* start_cond holds two ECondStruct records; start_op == 1
				 * selects OR, anything else selects AND. */
				c0 = &f->start_cond[0];
				c1 = &f->start_cond[1];
				fgcnt = i;
				ok0 = score_checkcondition(c0->cond, c0->type, c0->id, c0->pct, 1);
				ok1 = score_checkcondition(c1->cond, c1->type, c1->id, c1->pct, 1);
				combined = (f->start_op == 1) ? (ok0 | ok1) : (ok0 & ok1);
				i = fgcnt;
				if (combined & 1) {
					const int16_t delay = (int16_t)(60 * f->start_delay_min + f->start_delay_sec);
					fgstatus[i].arrival_triggered = 1;
					fgstatus[i].arrival_delay = (uint16_t)delay;
				}
			} else {
				/* Live FG: respawn its wave when every craft is dead. */
				const uint8_t wr = st->waves_remaining;
				int all_dead;
				uint16_t j;
				uint8_t new_wr;

				craftptr = saved_cp;
				if (!wr || !st->cond[0].count)
					continue;

				all_dead = 1;
				for (j = 0; j < NUM_CRAFTS; j++) {
					if (objects[j].ship_idx && objects[j].fg_idx == i) {
						all_dead = 0;
						break;
					}
				}
				if (!all_dead)
					continue;

				fgcnt = i;
				create_createflightgroup(-1, (int16_t)i);
				i = fgcnt;
				new_wr = fgstatus[fgcnt].waves_remaining;
				if (new_wr)
					fgstatus[fgcnt].waves_remaining = (uint8_t)(new_wr - 1);
			}
		}
	}

	if (!timers[TIMER_FG_SPAWN]) {
		uint16_t i;

		timers[TIMER_FG_SPAWN] = 236;
		for (i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
			FGStatus* st;

			fgcnt = i;
			st = &fgstatus[i];
			craftptr = saved_cp;
			if (st->active)
				continue;
			if (st->arrival_triggered != 1)
				continue;
			if (st->arrival_delay)
				st->arrival_delay--;
			else
				create_startflightgroup(-1, (int16_t)i);
		}
	}

	craftptr = saved_cp;
}

/* ============================================================== */
/*   Dynamic flight-group spawn                                    */
/* ============================================================== */

// FUNCTION: TIE95 0x17460
int create_createflightgroup(int16_t craft_slot, int16_t fg_idx) {
	EFGStruct* f = &fg_array[fgcnt];
	uint8_t mission_clock_started;
	uint8_t form_spacing;

	fghyperspace = 0;
	fghangar = 0;

	/* Binary's `dword_E6388 | byte_E6387 | BYTE1(dword_E6388)` test:
	 * any byte of the wall-clock cluster non-zero. The cluster is the
	 * hour/minute/second/subsec fields of MissionClock — once
	 * tie_updatetime has rolled the sub-second counter once, this is
	 * true forever. Semantic: arriving FGs run their hyperspace-in /
	 * hangar-launch animation only after the mission clock has started
	 * (i.e. not on the very first frame). */
	mission_clock_started = (uint8_t)(_date.hour | _date.minute | _date.second | (uint8_t)_date.subsec |
									  (uint8_t)(_date.subsec >> 8));

	/* Branch 1: carrier-spawn via a fleet leader FG (hangar launch). */
	if (f->start_fg_used && mission_clock_started && !mission.train_craft_type && craft_slot == -1) {
		const int16_t carrier_fg = (int16_t)(int8_t)f->start_fg;
		uint16_t anchor = 0xFFFF;
		uint16_t j;
		FlightObject* ao;
		uint16_t sidx;
		const SpecData* sp;

		for (j = 0; j < NUM_CRAFTS; j++) {
			if (!objects[j].ship_idx)
				continue;
			craftptr = objects[j].craft_ptr;
			if (objects[j].fg_idx == carrier_fg && craftptr->leader_obj_idx == 255) {
				anchor = j;
				break;
			}
		}
		if (anchor == 0xFFFF)
			return 0;

		ao = &objects[anchor];
		sidx = ao->craft_ptr->species_idx;
		sp = &spec_data[sidx];
		craftptr = ao->craft_ptr;

		/* Two rotated points relative to the carrier: drop anchor
		 * (cockpit_x, cockpit_y, cockpit_z) and approach vector
		 * (engine_x, engine_y, engine_z). Difference feeds heading.
		 * Retail's HIWORD reads on dword_C7B8A/C7B8E/C7B92 resolve to
		 * the i16 fields TWO bytes after the named base (the unaligned
		 * dword load pattern), so the cockpit/engine fields are the
		 * right ones here, not dock_active_heavy. */
		pai_calcrotatedpoint(ao, sp->cockpit_x, sp->cockpit_y, sp->cockpit_z);
		fglocx = rotatedx + ao->world_x;
		fglocy = rotatedy + ao->world_y;
		fglocz = rotatedz + ao->world_z;
		pai_calcrotatedpoint(ao, sp->engine_x, sp->engine_y, sp->engine_z);
		worldlocx = rotatedx + ao->world_x;
		worldlocy = rotatedy + ao->world_y;
		worldlocz = rotatedz + ao->world_z;
		trig2_ctop(worldlocx - fglocx, worldlocy - fglocy, worldlocz - fglocz);
		fgheadingxy = trig2_xyangle;
		fgheadingz = trig2_zangle;

		fghangar = 1;
		fgformation = (f->count > 3) ? 6 : 0;
		form_spacing = 0;
	} else {
		/* Branch 2: waypoint-anchored spawn. Use waypoint 0 (live) and
		 * heading derived from waypoint 4 when set; else default heading. */
		int16_t z_angle;

		create_getworldposition(OBJ_REF_WAYPOINT_BASE, fgcnt);
		fglocx = worldlocx;
		fglocy = worldlocy;
		fglocz = worldlocz;

		if (f->way_used[4]) {
			create_getworldposition(0x8004, fgcnt);
			trig2_ctop(worldlocx - fglocx, worldlocy - fglocy, worldlocz - fglocz);
			fgheadingxy = trig2_xyangle;
			z_angle = trig2_zangle;
		} else {
			z_angle = 0x4000;
			trig2_xyangle = 0;
			fgheadingxy = 0;
			trig2_zangle = 0x4000;
		}
		fgheadingz = z_angle;

		/* First-frame "hyper-in" arrival: step 8x further behind the FG
		 * along the reversed approach vector so the jump-in animation has
		 * travel distance. */
		if (!mission.train_craft_type && !f->start_fg_used && mission_clock_started && craft_slot == -1) {
			trig2_xyangle = (int16_t)(trig2_xyangle + 0x8000);
			trig2_zangle = (int16_t)(0x8000 - trig2_zangle);
			trig2_movexyz(0xFFFF, trig2_xyangle, (uint16_t)trig2_zangle);
			trig2_xmovedist *= 8;
			trig2_ymovedist *= 8;
			trig2_zmovedist *= 8;
			fglocx += trig2_xmovedist;
			fglocy += trig2_ymovedist;
			fglocz += trig2_zmovedist;
			fghyperspace = 1;
		}
		form_spacing = f->form_spacing;
		fgformation = f->formation;
	}

	fgseparation = form_spacing;
	fgside = f->side;
	fgversion = f->version;
	fgflightflag = 0;
	fggenus = species_table[speciesconvert[f->species]].ship_class;
	fgskill = f->skill;

	/* Easy-mode friendly-side skill bump and hostile-craft skill dock.
	 * Fighter (gen 0) orders 19 (rendezvous) skip the dock. */
	if (!mission.difficulty) {
		int adjust_if_hostile = 1;
		if (fggenus == GENUS_STARSHIP || fggenus == GENUS_PLATFORM) {
			adjust_if_hostile = 1;
		} else if (fggenus == GENUS_FIGHTER) {
			int k;

			for (k = 0; k < 3; k++) {
#ifdef TIE_MODERN
				// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
				if (f->ai[k].order < sizeof(ordersldr) && ordersldr[f->ai[k].order] == 19)
#else
				if (ordersldr[f->ai[k].order] == 19)
#endif
					adjust_if_hostile = 0;
			}
		}
		if (fgside == 1) {
			fgskill++;
			if (fgskill >= 5)
				fgskill = 4;
		} else if (adjust_if_hostile && (fgside == 0 || fgside == 4) && fgskill && fgskill < 5) {
			fgskill--;
		}
	}

	/* Spawn loop: either the whole FG, or just one craft index. */
	if (craft_slot == -1) {
		leaderflag = 0xFF;
		for (craftcnt = 0; craftcnt < f->count; craftcnt++) {
			FGStatus* st = &fgstatus[fgcnt];
			if (st->cond[0].detail >= st->cond[0].count)
				continue;
			if (create_createcraft() == 0xFFFF)
				return 0;
			st->cond[0].detail++;
			if (craftcnt == f->special_craft)
				st->cond_id[0].detail++;
		}
	} else {
		FGStatus* st = &fgstatus[fgcnt];
		if (st->cond[0].detail < st->cond[0].count) {
			craftcnt = (uint16_t)craft_slot;
			if (create_createcraft() == 0xFFFF)
				return 0;
			st->cond[0].detail++;
			if (craftcnt == f->special_craft)
				st->cond_id[0].detail++;
		}
	}

	fgstatus[fgcnt].cond[8].detail = 0;

	/* Reinforcement chatter: radio report + MS sequence selector fires
	 * for FGs that arrive AFTER the mission has been running for at
	 * least one frame, so the initial-wave spawns at mission load are
	 * silent and only later arrivals are announced. */
	if (mission_clock_started && craft_slot == -1) {
		const uint16_t spec_num = spec_getspecnum(fgspecies);
		int16_t seq;

		msg_reportfgcreation(fgcnt, spec_num);

		if (fgsidecreated) {
			if (fgsidecreated == 1 || fgsidecreated == 4)
				seq = (fggenus == GENUS_STARSHIP) ? 10 : 11;
			else
				seq = (fggenus == GENUS_STARSHIP) ? 14 : 15;
		} else {
			seq = (fggenus == GENUS_STARSHIP) ? 12 : 13;
		}
		fscript_MsSetSequence(seq);
		(void)fg_idx;
	}
	return 1;
}

/* ============================================================== */
/*   Per-craft spawn                                               */
/* ============================================================== */

// FUNCTION: TIE95 0x17BF8
uint16_t create_createcraft(void) {
	int k;
	SpecData* sp;
	EFGStruct* f = &fg_array[fgcnt];
	const uint16_t ship_idx = speciesconvert[f->species];
	uint16_t gstart;
	uint16_t gend;
	uint16_t obj_slot;
	FlightObject* o;
	CraftData* c;
	uint16_t hw;
	uint8_t spec_num;
	uint8_t laser_total;
	int bank;
	uint8_t order_ldr;
	uint8_t order_flw;
	uint16_t throttle;
	uint16_t init_speed;

	int32_t form_x, form_y, form_z;

	fgspecies = (uint8_t)ship_idx;

	/* Find a free FlightObject slot in the genus range. */
	gstart = genus_table[fggenus].start;
	gend = genus_table[fggenus].limit;

	for (obj_slot = gstart; obj_slot < gend; obj_slot++)
		if (!objects[obj_slot].ship_idx)
			break;
	if (obj_slot >= gend)
		return 0xFFFF;

	/* Bind player if this is the training/player craft index. */
	if (f->player_flag && craftcnt == (uint16_t)(f->player_flag - 1)) {
		pstate.object_idx = obj_slot;
		pstate.player = &objects[obj_slot];
	}

	o = &objects[obj_slot];
	c = &crafts[obj_slot];

	o->ship_idx = (uint8_t)ship_idx;
	o->idnumber = idnumber++;
	o->craft_ptr = c;
	craftptr = c;

	hw = species_table[ship_idx].bound_hwidth;
	o->collision_radius = (hw >= 0x2000u) ? 0x7FFF : (int16_t)(4 * hw);

	spec_num = (uint8_t)spec_getspecnum(ship_idx);
	c->species_idx = spec_num;
	fgsidecreated = fgside;
	o->side = fgside;
	o->genus = fggenus;
	o->category = species_table[ship_idx].category;
	o->decal_color = f->camoflage;
	o->self_idx = (int16_t)obj_slot;
	o->ship_type_override = (uint8_t)ship_idx;
	o->fg_idx = (uint8_t)fgcnt;

	c->leader_obj_idx = leaderflag;
	if (leaderflag == 0xFF)
		leaderflag = (uint8_t)obj_slot;

	c->formation = fgformation;
	c->craft_idx_in_fg = (uint8_t)craftcnt;
	c->formation_separation = (uint8_t)(fghangar ? 0 : fgseparation);
	c->push_accum_x = c->push_accum_y = c->push_accum_z = 0;

	/* Formation-relative spawn offset from leader craft. */
	form_x = 0;
	form_y = 0;
	form_z = 0;
	if (c->leader_obj_idx != 0xFF) {
		const SpecData* sp = &spec_data[spec_num];
		const int form_idx = craftcnt + 6 * fgformation; /* 0..77 */
		const int16_t sep = (int16_t)(c->formation_separation + 1);
		form_x = (int16_t)(sep * _formposx[form_idx] * sp->bound_width);
		form_y = (int16_t)(sep * _formposy[form_idx] * sp->bound_depth);
		form_z = (int16_t)(sep * _formposz[form_idx] * sp->bound_height);
		if (sep == 1) {
			/* Dense-formation refinement: add bound_* / 2 (x,z) or
			 * bound_depth / 4 (y) times the same _formposx/y/z element
			 * (Watcom's unaligned DWORD-HIWORD re-read collapses to a
			 * direct re-access). */
			form_x += (int32_t)_formposx[form_idx] * (sp->bound_width / 2);
			form_z += (int32_t)_formposz[form_idx] * (sp->bound_height / 2);
			form_y += (int32_t)_formposy[form_idx] * (sp->bound_depth / 4);
		}
		pai_calcrotatedpoint(&objects[c->leader_obj_idx], (int16_t)form_x, (int16_t)form_z, (int16_t)form_y);
		if (sp->model_scale_shift) {
			/* Binary emits `shl reg, cl` (sign-agnostic); shifting a
			 * negative int32_t in C is UB, so route through uint32_t. */
			const int shift = sp->model_scale_shift;
			rotatedx = (int32_t)((uint32_t)rotatedx << shift);
			rotatedy = (int32_t)((uint32_t)rotatedy << shift);
			rotatedz = (int32_t)((uint32_t)rotatedz << shift);
		}
	} else {
		rotatedx = rotatedy = rotatedz = 0;
	}

	o->world_x = o->world_x_prev = rotatedx + fglocx;
	o->world_y = o->world_y_prev = rotatedy + fglocy;
	o->world_z = o->world_z_prev = rotatedz + fglocz;

	/* Cargo: special_craft index picks cargo[1] (unique name); others
	 * get cargo[0] (group name). */
	{
		const char* src = (craftcnt == f->special_craft) ? f->contents[1] : f->contents[0];
		int k;

		for (k = 0; k < 16; k++)
			c->cargo[k] = src[k];
	}

	c->boarding_state = 0;
	c->subsystem_active = 0x03FF;
	c->installed_subsystems = 0x1FFF;

	/* Pose. */
	o->heading = fgheadingxy;
	c->orient_heading = (uint16_t)fgheadingxy;
	o->pitch = fgheadingz;
	c->orient_pitch = (uint16_t)fgheadingz;
	o->roll = 0;
	o->spin_rate = 0;
	o->orient_dirty = 1;
	o->move_dirty = 1;

	/* AI var clears. */
	c->ai_roll_state = 0;
	c->ai_pitch_state = c->ai_climb_state = 0;
	c->ai_dive_state = c->ai_pitch_force = 0;
	c->ai_heading_state = 0;
	c->ai_target_a = c->ai_target_b = c->ai_target_c = c->ai_target_d = -1;

	/* Cached spec stats (queried each AI tick; readonly after createcraft). */
	sp = &spec_data[spec_num];
	c->roll_rate_cache = sp->roll_rate;
	c->pitch_rate_cache = sp->pitch_rate;
	c->heading_rate_cache = sp->heading_rate;
	c->max_speed_cache = sp->max_speed;

	/* Skill tier used by AI, turret cooldowns, and targeting jitter. */
	c->skill_value = skilltranslate[fgskill];

	/* Laser banks. */
	c->laser_group_cnt = 0;
	laser_total = 0;
	for (bank = 0; bank < 2; bank++) {
		uint8_t start;
		uint8_t end;
		uint8_t k;

		c->laser_type[bank] = sp->laser_type[bank];
		c->laser_owner_player[bank] = (uint8_t)(obj_slot == pstate.object_idx);
		c->laser_burst_remaining[bank] = 0;
		c->laser_cooldown[bank] = 0;
		c->laser_first_slot[bank] = 0;

		if (!c->laser_type[bank])
			continue;

		start = sp->laser_start[bank];
		end = sp->laser_end[bank];
		laser_total = (uint8_t)(laser_total + sp->laser_count[bank]);
		if (sp->laser_fire_mode[bank] != 2) {
			c->laser_group_cnt++;
			c->laser_first_slot[bank] = start;
		}
		for (k = start; k <= end; k++) {
			c->weapon_slots[k].type = (uint8_t)((sp->laser_fire_mode[bank] == 2) ? 2 : c->laser_type[bank]);
			c->weapon_slots[k].charge = 127;
			c->weapon_slots[k].ammo = 0;
			c->weapon_slots[k].target_obj = 0xFFFF;
		}
	}
	c->laser_power = 2;
	c->weapon_group_cnt = laser_total;
	if (!c->laser_group_cnt)
		c->subsystem_active ^= 0x10u;

	/* Missile banks. */
	c->missile_group_cnt = 0;
	for (bank = 0; bank < 2; bank++) {
		/* Only missile-boat (spec 12) gets two missile banks. */
		uint8_t ms;
		uint8_t me;
		uint8_t k;

		if (bank == 1 && spec_getspecnum(0x0C) != spec_num) {
			c->warhead_type[bank] = 0;
		} else if (obj_slot == pstate.object_idx && mission.mission_mode == 4) {
			c->warhead_type[bank] = warheadconvert[mission.torp_used];
		} else {
			/* Binary reads byte at EFG +0x35 (warhead), not species. */
			c->warhead_type[bank] = warheadconvert[f->warhead];
		}
		/* Missile boat gets an override torpedo in bank 1 in combat mode. */
		if (bank == 1 && spec_getspecnum(0x0C) == spec_num && !mission.train_craft_type)
			c->warhead_type[1] = (uint8_t)-107; /* 0x95 = magpulse */
		c->missile_armed[bank] = 1;
		c->missile_state[bank] = 0;

		if (!c->warhead_type[bank])
			continue;
		c->missile_group_cnt++;

		ms = sp->missile_start[bank];
		me = sp->missile_end[bank];
		for (k = ms; k <= me; k++) {
			const uint8_t warhead = c->warhead_type[bank];
			WeaponSlot* ws = &c->weapon_slots[k];
			uint8_t base;
			uint8_t torp;
			uint8_t count;

			ws->target_obj = 0xFFFF;
			ws->charge = 127;
			ws->type = warhead;

			/* missile_fire_mode mirrors laser_fire_mode but
			 * FEDISKIO_fillinspec never populates it -- always
			 * BSS-zero. math2_fraction(0, ...) returns 0 and the
			 * `if (!count) count = 1` fallback below substitutes 1.
			 * Retail reads byte_C7AFB[species*236 + bank] here. */
			base = sp->missile_fire_mode[bank];

			if (pstate.object_idx == obj_slot && mission.mission_mode == 4)
				torp = mission.torp_used;
			else
				torp = f->warhead;
			if (bank == 1 && spec_getspecnum(0x0C) == spec_num)
				torp = 5;
			count = (uint8_t)math2_fraction(base, warheadadjust[torp]);
			if (!count)
				count = 1;
			if (fgversion == 1)
				count = (uint8_t)(count * 2);
			else if (fgversion == 2)
				count = (uint8_t)(count >> 1);
			if (!count)
				count = 1;
			/* Player cap: 9 warheads except the missile boat. */
			if (obj_slot == pstate.object_idx && count > 9 &&
				pstate.player_spec_num != (uint8_t)spec_getspecnum(0x0C))
				count = 9;
			ws->ammo = count;
		}
	}
	c->missile_count_total = 0;
	if (!c->missile_group_cnt)
		c->subsystem_active ^= 0x08u;

	/* Reset mission counters. */
	c->laser_hit = c->missile_hit = c->warhead_hit = 0;
	c->total_kills = 0;
	c->laser_fired = c->laser_hit;
	c->missile_fired = c->missile_hit;
	c->warhead_fired = c->warhead_hit;
	memset(c->kills_by_species, 0, sizeof(c->kills_by_species));

	if (obj_slot == pstate.object_idx) {
		pstate.player_laser_hit = 0;
		pstate.player_laser_fired = (uint16_t)(pstate.object_idx ^ obj_slot); /* always 0; binary parity */
		pstate.player_missile_hit = 0;
		pstate.player_missile_fired = 0;
		pstate.player_warhead_hit = 0;
		pstate.player_warhead_fired = 0;
		pstate.player_total_kills = 0;
		pstate.friendly_kill_count = 0;
		memset(pstate.player_kills_per_species, 0, sizeof(pstate.player_kills_per_species));
	}

	/* Shields. */
	c->hull_max = (uint16_t)sp->hull_max;
	c->hull_damage = 0;
	c->dead_0B0 = 0;
	c->ion_drain_timer = 0;
	c->pad_0B4 = c->was_hit_flag = c->pad_0B6 = c->dock_state_flags = 0;
	c->hull_strength = (uint16_t)sp->hull_strength;
	c->ai_anim_flags = 0;
	c->beam_state = 0;

	/* Auto-identify: same-side craft are pre-known on IFF, and any
	 * fighter (own or hostile) is identified by silhouette. Everything
	 * else (freighters, transports, capital ships, neutrals) starts
	 * un-inspected and must be scanned to reveal cargo + bump the
	 * inspection counter. */
	if (o->side != playerside && o->genus != GENUS_FIGHTER) {
		c->inspected = 0;
	} else {
		c->inspected = 1;
		fgstatus[fgcnt].cond[4].detail++;
		if (craftcnt == f->special_craft)
			fgstatus[fgcnt].cond_id[4].detail++;
	}

	if (obj_slot == pstate.object_idx && !mission.difficulty) {
		c->hull_max = (uint16_t)(c->hull_max * 3);
		c->hull_strength = (uint16_t)(c->hull_strength * 3);
	}

	/* Hyperdrive subsystem gating: clear SF_HYPER_DRIVE (0x80) for species
	 * without a hyperdrive, and for fg version 6 (no-hyper variant). */
	if (!sp->has_hyperdrive && fgversion != 9)
		c->subsystem_active ^= 0x80u;
	if (fgversion == 6)
		c->subsystem_active ^= 0x80u;

	c->forward_shield = sp->shield_points;
	if (obj_slot == pstate.object_idx) {
		c->rear_shield = sp->shield_points;
		c->is_player_craft = 1;
		if (!mission.difficulty) {
			c->forward_shield = (int16_t)(c->forward_shield * 2);
			c->rear_shield = (int16_t)(c->rear_shield * 2);
		}
	} else {
		c->rear_shield = 0;
		c->is_player_craft = 0;
		c->forward_shield = (int16_t)(sp->shield_points + c->forward_shield);
		if (!mission.difficulty) {
			if (fgside == 1) {
				/* +50%. */
				c->forward_shield = (int16_t)(c->forward_shield + (c->forward_shield >> 1));
			} else if (fgside == 0 || fgside == 4) {
				c->forward_shield = (int16_t)math2_fraction((uint16_t)c->forward_shield, 0xA000u);
			}
		}
		if (c->forward_shield < 0)
			c->forward_shield = 30000;
	}

	/* Per-version shield / subsystem adjustments. */
	switch (fgversion) {
		case 3:
			c->forward_shield = 0;
			c->rear_shield = 0;
			c->subsystem_active ^= 0x01u;
			break;
		case 4:
			c->forward_shield = (int16_t)(c->forward_shield >> 1);
			c->rear_shield = (int16_t)(c->rear_shield >> 1);
			c->subsystem_active ^= 0x01u;
			break;
		case 7:
			c->forward_shield = 0;
			c->rear_shield = 0;
			break;
	}

	c->shield_power = 2;

	/* Craft without a shield generator (has_shields == 0) zeroes the shield
	 * capacity and clears SF_SHIELDS. fgversion 8 = override/bypass. */
	if (!sp->has_shields && fgversion != 8) {
		c->forward_shield = 0;
		c->rear_shield = 0;
		c->subsystem_active ^= 0x01u;
		c->installed_subsystems ^= 0x0800u; /* HIBYTE ^= 8 */
	}

	/* Beam weapon selection + state init. */
	c->beam_type = (obj_slot == pstate.object_idx) ? mission.beam_used : f->beam;
	if (fggenus == GENUS_PLATFORM && fgspecies >= 0x3C && fgspecies < 0x41)
		c->beam_type = 0; /* capital class: no beam */
	c->beam_power = 2;
	c->beam_charge = 9999;
	if (!c->beam_type) {
		c->beam_charge = 0;
		c->subsystem_active ^= 0x0100u; /* HIBYTE ^= 1 */
		c->installed_subsystems ^= 0x1010u;
	}

	c->status_flags = c->subsystem_active;
	c->working_subsystems = c->installed_subsystems;

	/* Sprite anim slots zero out. */
	o->anim_frame = 0;
	o->anim_frame_alt = 0;

	/* Mesh state: default damaged-capable meshes preloaded from
	 * initialdamagestate[mesh_type]; fgversion 5 marks beam turrets as
	 * already destroyed; capital-class (genus 5, special range) destroys
	 * an explicit componentsgone[] list. */
	for (k = 0; k < 40; k++) {
		c->mesh_component_hp[k] = 0xFF;
		c->mesh_state[k] = MESH_STATE_VISIBLE;
		c->mesh_rotation[k] = 0;
	}
	if (TIE_FLIGHT_TIE98) {
		int mesh_count;
		int mesh;

		modelmesh_require_craft_capacity(ship_idx);
		mesh_count = modelmesh_getcount(ship_idx);
		for (mesh = 0; mesh < mesh_count; ++mesh) {
			const int mesh_type = modelmesh_gettype(ship_idx, mesh);
			if (modelmesh_isobjecttypemeshdamageable(ship_idx, mesh))
				// HARDENING: mesh types outside the table keep full damage state.
				c->mesh_component_hp[mesh] = mesh_type < 32 ? initialdamagestate[mesh_type] : 0xFF;
			if (fgversion == 5 && (mesh_type == TIE_MESH_GUN_TURRET || mesh_type == TIE_MESH_SMALL_GUN ||
								   mesh_type == TIE_MESH_ROTARY_GUN_TURRET)) {
				c->mesh_component_hp[mesh] = 0;
				c->mesh_state[mesh] = MESH_STATE_BLOWN_OFF;
			}
		}
	} else {
		ShipModelMesh* cb;
		uint16_t mi;

		draw_lockshipfileptrs((uint16_t)ship_idx);
		cb = componentblockptr;
		for (mi = 0; mi < objectblockptr->num_meshes; mi++, cb++) {
			if (cb->flags & 2)
				c->mesh_component_hp[mi] = initialdamagestate[cb->mesh_type];
			if (fgversion == 5 && (cb->mesh_type == 4 || cb->mesh_type == 5 || cb->mesh_type == 21)) {
				c->mesh_component_hp[mi] = 0;
				c->mesh_state[mi] = MESH_STATE_BLOWN_OFF;
			}
		}
	}
	if (fggenus == GENUS_PLATFORM && fgspecies >= 0x3C && fgspecies < 0x41 && f->beam) {
		const int base = 12 * (fgspecies - 60);
		const int len = (f->count == 1) ? 6 : 12;
		int k;

		for (k = base; k < base + len; k++) {
			const uint8_t comp = componentsgone[k];
			if (comp == 0xFF)
				continue;
			c->mesh_component_hp[comp] = 0;
			c->mesh_state[comp] = MESH_STATE_BLOWN_OFF;
		}
	}

	/* AI orders: leader + follower both indexed by ai[0].order.
	 * Hyper/hangar states override with fixed opcodes 52/50. */
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	order_ldr = f->ai[0].order < sizeof(ordersldr) ? ordersldr[f->ai[0].order] : 0;
	order_flw = f->ai[0].order < sizeof(ordersflw) ? ordersflw[f->ai[0].order] : 0;
#else
	order_ldr = ordersldr[f->ai[0].order];
	order_flw = ordersflw[f->ai[0].order];
#endif
	c->default_order_ldr = order_ldr;
	if (fghyperspace)
		c->current_order = 52;
	else if (fghangar)
		c->current_order = 50;
	else if (c->leader_obj_idx == 0xFF)
		c->current_order = order_ldr;
	else
		c->current_order = order_flw;

	/* Throttle: pick from _throttleconvert[ai[0].speed] unless order is
	 * 20 (hold position -> full thrust). Orders 0..2 and order 42 stop
	 * at the start waypoint (throttle = 0) EXCEPT for the player. */

	if ((order_ldr > 2 && order_ldr != 42) || obj_slot == pstate.object_idx)
		throttle = (order_ldr == 20) ? 0x8000u : _throttleconvert[f->ai[0].speed];
	else
		throttle = 0;

	c->flight_flag = fgflightflag;
	c->slam_active = 0xFFFF;
	c->throttle_speed = throttle;

	init_speed = math2_fraction((uint16_t)sp->max_speed, throttle);
	o->current_speed = (int16_t)init_speed;
	o->speed_remainder = 0;

	if (obj_slot == pstate.object_idx) {
		pstate.player_weapon_group = 0;
		pstate.player_weapon_mode = 0;
		pstate.player_spec_field_0F = sp->field_0F;
		pstate.player_craft = c;
		pstate.player_spec_num = spec_num;
	}

	/* Clear the 6 AI-preamble bytes. */
	for (k = 0; k < 3; k++) {
		c->ai_complete_state[k] = 0;
		c->ai_goal_progress[k] = 0;
	}
	c->ai_state_1C = 0;
	c->ai_target_ref = (int16_t)0xFF;
	c->link_target_2E = -1;
	c->spin_done_flag = 0xFFFF;
	c->escortee_fg_idx = 0xFF;
	c->special_order_flag = 0;
	c->hit_count = 0;
	c->board_count = 0;
	c->capture_count = 0;
	c->waypoint_x_cache = 0;
	c->pending_radio_command = c->ai_target_ref;
	c->tow_slave_ref = c->link_target_2E;
	if (obj_slot == pstate.object_idx)
		c->current_order = 0;

	c->active_waypoint_idx = 4;
	c->ai_update_rate = aiupdatetranslate[fgskill];

	pai_setupcraftaivars(obj_slot);
	pai_initplan();
	return obj_slot;
}

/* ============================================================== */
/*   Static flight-group spawn (mines / planets / asteroids)      */
/* ============================================================== */

// FUNCTION: TIE95 0x19054
int create_createstaticflightgroup(int16_t craft_slot) {
	int result = fgcnt;
	FGStatus* st = &fgstatus[fgcnt];
	EFGStruct* f;
	uint8_t species_idx;
	uint8_t ship_class;

	if (!st->cond[0].count) {
		st->waves_remaining = 0;
		return result * 48;
	}

	f = &fg_array[fgcnt];
	species_idx = speciesconvert[f->species];
	if (!(species_table[species_idx].side & 0x80)) {
		st->waves_remaining = 0;
		return result * 48;
	}

	ship_class = species_table[species_idx].ship_class;

	if (ship_class == 9) {
		/* Planet: single object anchored on waypoint 0 (negated Y). */
		staging_static_x = f->way_x[0];
		staging_static_y = (int16_t)(-f->way_y[0]);
		staging_static_z = f->way_z[0];
		staging_static_heading = (int8_t)f->heading;
		staging_static_pitch = (int8_t)f->pitch;
		staging_static_roll = (int8_t)f->rotation;
		create_createstaticobject(fgcnt, 9, species_idx);
	} else if (ship_class == 8) {
		/* Mine grid: count x count cube oriented per fg.version & 3. */
		int16_t step_x = 0, step_y = 0, step_z = 0, step_z2 = 0;
		const uint8_t axis_pair = (uint8_t)(f->version & 3);
		int16_t side_m1;
		int16_t x_base;
		int16_t y_base;
		int16_t z_base;
		int obj_seq;
		uint16_t row;

		int16_t y_accum, z_accum;

		if (!axis_pair) {
			step_x = 64;
			step_y = 64;
		} else if (axis_pair == 1) {
			step_y = 64;
			step_z = 64;
		} else if (axis_pair == 2) {
			step_x = 64;
			step_z2 = 64;
		}

		side_m1 = (int16_t)(f->count - 1);
		x_base = (int16_t)(f->way_x[0] - side_m1 * step_x / 2);
		y_base = (int16_t)(-f->way_y[0] - side_m1 * step_y / 2);
		z_base = (int16_t)(f->way_z[0] - side_m1 * step_z / 2 - side_m1 * step_z2 / 2);
		staging_static_heading = (int8_t)f->heading;
		staging_static_pitch = (int8_t)f->pitch;
		staging_static_roll = (int8_t)f->rotation;

		obj_seq = 0;
		y_accum = 0;
		z_accum = 0;
		for (row = 0; row < f->count; row++) {
			uint16_t col;

			for (col = 0; col < f->count; col++) {
				if ((craft_slot == -1 || craft_slot == obj_seq) && st->cond[0].detail < st->cond[0].count) {
					staging_static_x = (int16_t)(x_base + col * step_x);
					staging_static_y = (int16_t)(y_base + y_accum);
					staging_static_z = (int16_t)(z_base + col * step_z + z_accum);
					create_createstaticobject(fgcnt, 8, species_idx);
				}
				obj_seq++;
			}
			z_accum = (int16_t)(z_accum + step_z2);
			y_accum = (int16_t)(y_accum + step_y);
		}
	} else if (ship_class == 10) {
		/* Asteroid cloud: count rocks in a ±256 cube around waypoint 0,
		 * skipping positions that overlap an existing static. */
		const int16_t anchor_x = f->way_x[0];
		const int16_t anchor_y = (int16_t)(-f->way_y[0]);
		const int16_t anchor_z = f->way_z[0];

		uint16_t ast;

		for (ast = 0; ast < f->count; ast++) {
			int16_t ax, ay, az;
			int collision;
			uint8_t r_species;

			do {
				int j;

				ax = (int16_t)(anchor_x + (math2_getrandom() & 0x01FF) - 256);
				ay = (int16_t)(anchor_y + (math2_getrandom() & 0x01FF) - 256);
				az = (int16_t)(anchor_z + (math2_getrandom() & 0x01FF) - 256);
				collision = 0;
				for (j = 0; j < NUM_STATIC_OBJECTS; j++) {
					const StaticObject* s = &staticobjects[j];
					if (s->species && ax == s->world_x && ay == s->world_y && az == s->world_z) {
						collision = 1;
						break;
					}
				}
			} while (collision);

			staging_static_x = ax;
			staging_static_y = ay;
			staging_static_z = az;
			staging_static_heading = 0;
			staging_static_pitch = 0;
			staging_static_roll = 0;
			r_species = (uint8_t)((uint16_t)math2_getrandom() % 6 + 100);
			create_createstaticobject(fgcnt, 10, r_species);
		}
	}

	st->waves_remaining = 0;
	return fgcnt * 48;
}

/* ============================================================== */
/*   Drop-position resolver                                        */
/* ============================================================== */

// FUNCTION: TIE95 0x19FD0
int create_getdropposition(uint16_t fg_idx, uint16_t craft_index, uint16_t anchor_obj) {
	EFGStruct* f = &fg_array[fg_idx];
	const uint16_t species_idx = speciesconvert[f->species];
	const uint8_t spec_num = species_table[species_idx].spec_num;

	int16_t z_drop;

	if (!TIE_FLIGHT_TIE98)
		draw_lockshipfileptrs(species_idx);
	z_drop = TIE_FLIGHT_EDITION((int16_t)(objectblockptr->speed_default >> 17),
								(int16_t)modelbounds_getmaxz(species_idx));
	create_getworldposition(OBJ_REF_WAYPOINT_BASE, fg_idx);

	if (species_table[species_idx].side & 0x80) {
		const uint8_t ship_class = species_table[species_idx].ship_class;
		if (ship_class == 9) {
			/* Planet: waypoint[0] with negated Y and << 8 scaling. */
			worldlocx = (int32_t)f->way_x[0] << 8;
			worldlocy = -(int32_t)f->way_y[0] << 8;
			worldlocz = (int32_t)f->way_z[0] << 8;
		} else if (ship_class == 8) {
			/* Mine grid: walk to the requested index within count*count. */
			int16_t step_x = 0, step_y = 0, step_z = 0, step_z2 = 0;
			const uint8_t axis_pair = (uint8_t)(f->version & 3);
			int16_t side_m1;
			int16_t x_base;
			int16_t y_base;
			int16_t z_base;
			int seq;
			uint16_t r;

			if (!axis_pair) {
				step_x = 64;
				step_y = 64;
			} else if (axis_pair == 1) {
				step_y = 64;
				step_z = 64;
			} else if (axis_pair == 2) {
				step_x = 64;
				step_z2 = 64;
			}

			side_m1 = (int16_t)(f->count - 1);
			x_base = (int16_t)(f->way_x[0] - side_m1 * step_x / 2);
			y_base = (int16_t)(-f->way_y[0] - side_m1 * step_y / 2);
			z_base = (int16_t)(f->way_z[0] - side_m1 * step_z / 2 - side_m1 * step_z2 / 2);
			seq = 0;
			for (r = 0; r < f->count; r++) {
				uint16_t col;

				for (col = 0; col < f->count; col++) {
					if (seq == craft_index) {
						worldlocx = (int32_t)(x_base + col * step_x) << 8;
						worldlocy = (int32_t)(r * step_y + y_base) << 8;
						worldlocz = ((int32_t)(step_z2 * r + z_base + col * step_z) << 8) + z_drop;
						return z_drop;
					}
					seq++;
				}
			}
		}
	} else if (craft_index) {
		/* Non-static craft at a formation offset. */
		const SpecData* sp = &spec_data[spec_num];
		const int form_idx = craft_index + 6 * f->formation;
		int16_t sep = (int16_t)(f->form_spacing + 1);
		int32_t ox = (int32_t)sep * _formposx[form_idx] * sp->bound_width;
		int32_t oy = (int32_t)sep * _formposy[form_idx] * sp->bound_depth;
		int32_t oz = (int32_t)sep * _formposz[form_idx] * sp->bound_height;
		if (sep == 1) {
			ox += (int32_t)_formposx[form_idx] * (sp->bound_width / 2);
			oz += (int32_t)_formposz[form_idx] * (sp->bound_height / 2);
			oy += (int32_t)_formposy[form_idx] * (sp->bound_depth / 4);
		}
		if (anchor_obj == 0xFFFF) {
			create_getworldposition(OBJ_REF_WAYPOINT_BASE, fg_idx);
			rotatedx = rotatedy = rotatedz = 0;
		} else {
			pai_calcrotatedpoint(&objects[anchor_obj], (int16_t)ox, (int16_t)oz, (int16_t)oy);
		}
		if (sp->model_scale_shift) {
			/* Binary emits `shl reg, cl` (sign-agnostic); shifting a
			 * negative int32_t in C is UB, so route through uint32_t. */
			const int shift = sp->model_scale_shift;
			rotatedx = (int32_t)((uint32_t)rotatedx << shift);
			rotatedy = (int32_t)((uint32_t)rotatedy << shift);
			rotatedz = (int32_t)((uint32_t)rotatedz << shift);
		}
		worldlocx += rotatedx;
		worldlocy += rotatedy;
		worldlocz += rotatedz;
	} else {
		/* craft_index == 0: use waypoint 0 as-is. */
		create_getworldposition(OBJ_REF_WAYPOINT_BASE, fg_idx);
		rotatedx = rotatedy = rotatedz = 0;
	}

	worldlocz += z_drop;
	return z_drop;
}
