/* Animation pattern format (u16 entries):
 *   < 0x8000        -- 3D polymesh component idx (drawn as mesh)
 *   0x8000..0xFEFF  -- bitmap code: bit15 set; bits 7..14 species; bits 0..6 bitmap
 *   0xFF00..0xFFFC  -- jump-to-frame: new index = code & 0xFF
 *   0xFFFD          -- reset (animindex := 0)
 *   0xFFFE          -- delay frame (animindex advances by 1, no draw)
 *   0xFFFF          -- kill the parent object
 */

#include "tie/anim.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/display/classic_display.h"

#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/edition.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/gate.h"
#include "tie/logbuf2.h"
#include "tie/math2.h"
#include "tie/math2_wide.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/panel.h"
#include "tie/render_scene_tie98.h"
#include "tie/rotscale.h"
#include "tie/species.h"
#include "tie/starship.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_billboards.h" /* SNAPSHOT-ONLY billboard capture */
#include "tie_runtime/snapshot/snapshot_internal.h"   /* HYPER_FLASH event emit */
#endif

#include <imuse/lolevel.h>
#include <stddef.h>
#include <stdint.h>

/* Most patterns
 * open with [DELAY, JUMP(0)] (the 'idle parking state') and spawn at
 * anim_frame = 0; gameplay code bumps anim_frame past the header to arm
 * the animation. 'ember' / 'ember2' have no header: they play from frame 0.
 */

/* Big starfighter explosion: 11 bitmap frames (frame 6 doubled for extra
 * dwell on the mid-bloom) then kill. */
// GLOBAL: TIE95 0xC15BC
// GLOBAL: TIE98 0x4DF030
AnimOp bigexplo[13] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x7F, 0),
	ANIMOP_BITMAP(0x7F, 1),
	ANIMOP_BITMAP(0x7F, 2),
	ANIMOP_BITMAP(0x7F, 3),
	ANIMOP_BITMAP(0x7F, 4),
	ANIMOP_BITMAP(0x7F, 5),
	ANIMOP_BITMAP(0x7F, 6),
	ANIMOP_BITMAP(0x7F, 6),
	ANIMOP_BITMAP(0x7F, 7),
	ANIMOP_BITMAP(0x7F, 8),
	ANIMOP_KILL,
};

/* Small spark burst -- one-shot kill. */
// GLOBAL: TIE95 0xC15D6
// GLOBAL: TIE98 0x4DF050
AnimOp sparks[7] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x83, 0),
	ANIMOP_BITMAP(0x83, 1),
	ANIMOP_BITMAP(0x83, 2),
	ANIMOP_BITMAP(0x83, 3),
	ANIMOP_KILL,
};

/* Component-debris sparkle for the five-frame retail XACTSPARK09 asset. */
// GLOBAL: TIE95 0xC15E4
// GLOBAL: TIE98 0x4DF060
AnimOp sparks2[8] = {
	ANIMOP_DELAY,           ANIMOP_JUMP(0),         ANIMOP_BITMAP(0x84, 0), ANIMOP_BITMAP(0x84, 1),
	ANIMOP_BITMAP(0x84, 2), ANIMOP_BITMAP(0x84, 3), ANIMOP_BITMAP(0x84, 4), ANIMOP_KILL,
};

/* Single-frame sprites that loop forever (no header; play from frame 0). */
// GLOBAL: TIE95 0xC15F4
// GLOBAL: TIE98 0x4DF070
AnimOp ember[2] = { ANIMOP_BITMAP(0x85, 0), ANIMOP_JUMP(0) };
// GLOBAL: TIE95 0xC15F8
// GLOBAL: TIE98 0x4DF074
AnimOp ember2[2] = { ANIMOP_BITMAP(0x86, 0), ANIMOP_JUMP(0) };

/* Fire: 11 frames, ending with RESET (restart from frame 0, re-entering
 * the idle header -- effectively pauses until anim_frame is armed again). */
// GLOBAL: TIE95 0xC15FC
// GLOBAL: TIE98 0x4DF078
AnimOp fire[14] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x87, 0),
	ANIMOP_BITMAP(0x87, 1),
	ANIMOP_BITMAP(0x87, 2),
	ANIMOP_BITMAP(0x87, 3),
	ANIMOP_BITMAP(0x87, 4),
	ANIMOP_BITMAP(0x87, 5),
	ANIMOP_BITMAP(0x87, 6),
	ANIMOP_BITMAP(0x87, 7),
	ANIMOP_BITMAP(0x87, 8),
	ANIMOP_BITMAP(0x87, 9),
	ANIMOP_BITMAP(0x87, 10),
	ANIMOP_RESET,
};

/* Lightning: 10 intro bolts on species 0x87, then 6 doubled bolts on
 * species 0x88 looping -- the double slots slow the loop for that dwell.
 * Used per-Fuselage by draw_drawcraft and anim_updateanimation. */
// GLOBAL: TIE95 0xC1618
// GLOBAL: TIE98 0x4DF098
AnimOp lightning[25] = {
	ANIMOP_DELAY,           ANIMOP_JUMP(0),         ANIMOP_BITMAP(0x87, 0), ANIMOP_BITMAP(0x87, 1),
	ANIMOP_BITMAP(0x87, 2), ANIMOP_BITMAP(0x87, 3), ANIMOP_BITMAP(0x87, 4), ANIMOP_BITMAP(0x87, 5),
	ANIMOP_BITMAP(0x87, 6), ANIMOP_BITMAP(0x87, 7), ANIMOP_BITMAP(0x87, 8), ANIMOP_BITMAP(0x87, 9),
	ANIMOP_BITMAP(0x88, 0), ANIMOP_BITMAP(0x88, 0), ANIMOP_BITMAP(0x88, 1), ANIMOP_BITMAP(0x88, 1),
	ANIMOP_BITMAP(0x88, 2), ANIMOP_BITMAP(0x88, 2), ANIMOP_BITMAP(0x88, 3), ANIMOP_BITMAP(0x88, 3),
	ANIMOP_BITMAP(0x88, 4), ANIMOP_BITMAP(0x88, 4), ANIMOP_BITMAP(0x88, 5), ANIMOP_BITMAP(0x88, 5),
	ANIMOP_JUMP(12), /* loop back to the first ANIMOP_BITMAP(0x88, 0) */
};

/* Debris-chunk tumbling sprites. The 0x00..0x7F / 0x80..0xFF split in the
 * bitmap index is the sprite's flipped/unflipped pair (bit 6 of the 7-bit
 * index flips the blit). All four tables JUMP back to frame 2 to loop the
 * tumble body forever. */
// GLOBAL: TIE95 0xC164A
// GLOBAL: TIE98 0x4DF0D0
AnimOp debrischunk1[9] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x6E, 0x00),
	ANIMOP_BITMAP(0x6E, 0x01),
	ANIMOP_BITMAP(0x6E, 0x02),
	ANIMOP_BITMAP(0x6E, 0x03),
	ANIMOP_BITMAP(0x6E, 0x04),
	ANIMOP_BITMAP(0x6E, 0x05),
	ANIMOP_JUMP(2),
};
// GLOBAL: TIE95 0xC165C
// GLOBAL: TIE98 0x4DF0E8
AnimOp debrischunk2[11] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x6F, 0x00),
	ANIMOP_BITMAP(0x6F, 0x01),
	ANIMOP_BITMAP(0x6F, 0x02),
	ANIMOP_BITMAP(0x6F, 0x03),
	ANIMOP_BITMAP(0x6F, 0x04),
	ANIMOP_BITMAP(0x6F, 0x05),
	ANIMOP_BITMAP(0x6F, 0x06),
	ANIMOP_BITMAP(0x6F, 0x07),
	ANIMOP_JUMP(2),
};
// GLOBAL: TIE95 0xC1672
// GLOBAL: TIE98 0x4DF100
AnimOp debrischunk3[11] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x70, 0x00),
	ANIMOP_BITMAP(0x70, 0x01),
	ANIMOP_BITMAP(0x70, 0x02),
	ANIMOP_BITMAP(0x70, 0x03),
	ANIMOP_BITMAP(0x70, 0x04),
	ANIMOP_BITMAP(0x70, 0x05),
	ANIMOP_BITMAP(0x70, 0x06),
	ANIMOP_BITMAP(0x70, 0x07),
	ANIMOP_JUMP(2),
};
// GLOBAL: TIE95 0xC1688
// GLOBAL: TIE98 0x4DF118
AnimOp debrischunk4[9] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x71, 0x00),
	ANIMOP_BITMAP(0x71, 0x01),
	ANIMOP_BITMAP(0x71, 0x02),
	ANIMOP_BITMAP(0x71, 0x03),
	ANIMOP_BITMAP(0x71, 0x04),
	ANIMOP_BITMAP(0x71, 0x05),
	ANIMOP_JUMP(2),
};

/* Starship explosion (large-craft variant): 10 bitmap frames then kill. */
// GLOBAL: TIE95 0xC169A
// GLOBAL: TIE98 0x4DF130
AnimOp bigexplo2[14] = {
	ANIMOP_DELAY,
	ANIMOP_JUMP(0),
	ANIMOP_BITMAP(0x80, 0),
	ANIMOP_BITMAP(0x80, 1),
	ANIMOP_BITMAP(0x80, 2),
	ANIMOP_BITMAP(0x80, 3),
	ANIMOP_BITMAP(0x80, 4),
	ANIMOP_BITMAP(0x80, 5),
	ANIMOP_BITMAP(0x80, 6),
	ANIMOP_BITMAP(0x80, 7),
	ANIMOP_BITMAP(0x80, 8),
	ANIMOP_BITMAP(0x80, 9),
	ANIMOP_KILL,
};

/* ====================================================================== *
 * ANIM module globals
 * ====================================================================== */

// GLOBAL: TIE95 0xD3554
// GLOBAL: TIE98 0x6269D4
AnimOp* animptr; /* current pattern */
// GLOBAL: TIE95 0xD355A
// GLOBAL: TIE98 0x6269CE
uint16_t animindex; /* current frame index */
// GLOBAL: TIE95 0xD3558
// GLOBAL: TIE98 0x6269D0
uint16_t hyperfgnumber; /* mission_file_header.num_fg saved across the warp */
// GLOBAL: TIE95 0xD355C
// GLOBAL: TIE98 0x6269CC
uint8_t curgenus; /* genus byte cached during anim tick */

/* Static-ref encoding used by anim_add_bitmap_draw: static refs use
 * OBJ_REF_STATIC_BASE = 0x3800 (high byte 0x38). */

/* lolevel iMUSE -- stop a sound. The binary's LOLEVEL_ImStopSound takes
 * the sound 'pointer' as an integer (sound id 48 here). */

/* ====================================================================== *
 * anim_add_bitmap_draw
 * ----------------------------------------------------------------------------
 * Enqueue one sprite into drawitems[]. The queue holds at most 32 entries.
 * The binary (ANIM_add_bitmap_draw @ 0x106bc) writes slot = numbitmaps and
 * only increments numbitmaps while it is < 32 -- so on the 33rd+ call it
 * writes drawitems[32], one past the end. The sort/draw pass only ever reads
 * slots 0..numbitmaps-1 (== 0..31), so that 33rd write is dead in the binary's
 * contiguous data segment. Here drawitems is a tight [32] array, so writing
 * slot 32 corrupts the adjacent global (observed: it partially overwrote the
 * `drawshape` function pointer, crashing the HUD path). Drop the sprite when
 * the queue is full instead -- observably identical (the slot is never drawn).
 * ====================================================================== */
// FUNCTION: TIE95 0x106BC
void anim_add_bitmap_draw(uint16_t obj_idx_arg, uint16_t species_packed, uint16_t scale_factor,
						  int16_t screen_x, int16_t screen_y, int32_t eye_z, int16_t angle) {
	int16_t slot = numbitmaps;

	if (slot >= ANIM_DRAWITEMS_MAX)
		return;

	drawitems[slot].obj_idx = obj_idx_arg;
	drawitems[slot].species_packed = species_packed;
	drawitems[slot].scale_factor = scale_factor;
	drawitems[slot].screen_x = screen_x;
	drawitems[slot].screen_y = screen_y;
	drawitems[slot].eye_z = eye_z;
	drawitems[slot].angle = angle;

	numbitmaps = slot + 1;
}

/* ====================================================================== *
 * anim_sort_and_draw_bitmaps
 * ----------------------------------------------------------------------------
 * Bubble-sort drawitems[0..numbitmaps-1] descending by eye_z (farthest
 * first), drawing the last entry as it's pulled off so the array shrinks
 * each iteration. Then paint the player target reticle.
 *
 * The binary does a 4-DWORD swap of the 16-byte struct; in C a struct copy
 * does the same thing.
 * ====================================================================== */
// FUNCTION: TIE95 0x10720
int16_t anim_sort_and_draw_bitmaps(void) {
	int swapped = 1;
	while (--numbitmaps != -1) {
		if (swapped) {
			uint16_t i;

			swapped = 0;
			for (i = 0; i < (uint16_t)numbitmaps; ++i) {
				if (drawitems[i].eye_z > drawitems[i + 1].eye_z) {
					BitmapDrawEntry tmp = drawitems[i];
					drawitems[i] = drawitems[i + 1];
					drawitems[i + 1] = tmp;
					swapped = 1;
				}
			}
		}
		anim_draw_bitmap(&drawitems[numbitmaps]);
	}
	return user_targetonscreen(pstate.target_obj_idx);
}

// FUNCTION: TIE98 0x4012F0
void anim_sort_and_draw_bitmaps_tie98(int draw_target) {
	int swapped = 1;
	while (--numbitmaps != -1) {
		if (swapped) {
			uint16_t i;

			swapped = 0;
			for (i = 0; i < (uint16_t)numbitmaps; ++i) {
				if (drawitems[i].eye_z > drawitems[i + 1].eye_z) {
					BitmapDrawEntry temporary = drawitems[i];
					drawitems[i] = drawitems[i + 1];
					drawitems[i + 1] = temporary;
					swapped = 1;
				}
			}
		}
		anim_draw_bitmap_tie98(&drawitems[numbitmaps]);
	}
	numbitmaps = 0;
	if (draw_target)
		user_targetonscreen_tie98(pstate.target_obj_idx, pstate.radar_target1, 0x3b);
}

/* ====================================================================== *
 * anim_draw_bitmap
 * ----------------------------------------------------------------------------
 * Render one queued sprite. Two world-position paths:
 *
 *   static obj (high byte 0x38)  -- ADD the static's world coords (<<8 to
 *     match Q24.8) to the existing _worldx/y/z parent offset before
 *     subtracting the camera.
 *   flight obj                   -- write _worldx/y/z = objects[].world_x/y/z
 *     - camera (overwriting whatever was there).
 *
 * In both branches _parentobject is restored to the original packed
 * obj_idx so subsequent rendering (target bracket etc.) can still refer
 * to it.
 *
 * ====================================================================== */
// FUNCTION: TIE95 0x107D4
int16_t anim_draw_bitmap(const BitmapDrawEntry* entry) {
	/* species_packed is the same bitfield emitted by ANIMOP_BITMAP;
	 * decode with the shared accessors. */
	uint8_t species_idx = (uint8_t)((entry->species_packed & 0x7FFFu) >> 7);
	uint8_t bitmap_idx = (uint8_t)(entry->species_packed & 0x7Fu);

	int16_t scale;
	LandruHandle handle;
	const uint8_t* blob;
	uint32_t tbl_off;
	uint32_t sub_off;
	const uint8_t* v12;

	reverseflag = 1;
	parentobject = entry->obj_idx;

	if ((entry->obj_idx & 0xFF00u) == OBJ_REF_STATIC_BASE) {
		uint16_t static_slot = (uint16_t)(parentobject - OBJ_REF_STATIC_BASE);
		int16_t sx = staticobjects[static_slot].world_x;
		int16_t sy = staticobjects[static_slot].world_y;
		int16_t sz = staticobjects[static_slot].world_z;

		/* HIBYTE(parentobject) += 56 in the binary -- restore the 0x38
		 * tag we cleared with the subtract above. parentobject ends up
		 * back at its original packed value 0x38xx. */
		parentobject = (uint16_t)(static_slot + OBJ_REF_STATIC_BASE);

		/* Add the static position to the parent offset, then subtract the camera. */
		worldx = ((int32_t)sx << 8) + worldx - camera.x;
		worldy = ((int32_t)sy << 8) + worldy - camera.y;
		worldz = ((int32_t)sz << 8) + worldz - camera.z;
	} else {
		uint16_t flight_slot = parentobject;
		worldx = objects[flight_slot].world_x - camera.x;
		worldy = objects[flight_slot].world_y - camera.y;
		worldz = objects[flight_slot].world_z - camera.z;
	}

	scale = rotscale_calcscale(entry->eye_z, species_table[species_idx].bound_hwidth, entry->scale_factor);

	handle = species_table[species_idx].model_handle;
	blob = (const uint8_t*)xmemhdl_Lock_Handle(handle);
	xmemhdl_Unlock_Handle(handle);
	if (!blob)
		return 0;

	/* Retail ANIM_draw_bitmap (0x107d4) computes the per-frame image
	 * pointer via:
	 *   v12 = blob + *(blob + *(blob + 16) + 4*bitmap_idx)
	 * Header offset 16 holds a u32 to an offset table; each table entry
	 * is a u32 byte-offset from `blob` to that frame's sub-image. The
	 * demo version had a u16 offset table at blob+2; retail moved it to
	 * u32 entries at blob + header[16]. The same v12 is then passed to
	 * BOTH preparecolor and rotatescaleimage. */
	tbl_off = *(const uint32_t*)(blob + 16);
	sub_off = *(const uint32_t*)(blob + tbl_off + 4 * bitmap_idx);
	v12 = blob + sub_off;

	rotscale_prepare_fastdraw((uint16_t)entry->angle);
	rotscale_prepare_color((const char*)v12);

	return rotscale_rotate_scale_image(entry->screen_x, entry->screen_y, (uint16_t)scale, v12);
}

// FUNCTION: TIE98 0x401410
void anim_draw_bitmap_tie98(const BitmapDrawEntry* entry) {
	const uint8_t species_idx = (uint8_t)((entry->species_packed & 0x7FFFu) >> 7);
	const uint8_t bitmap_idx = (uint8_t)(entry->species_packed & 0x7Fu);
	uint16_t scale;
	LandruHandle handle;
	const uint8_t* blob;
	uint32_t table_offset;
	uint32_t frame_offset;

	reverseflag = 1;
	parentobject = entry->obj_idx;

	if ((entry->obj_idx & 0xFF00u) == OBJ_REF_STATIC_BASE) {
		const uint16_t static_slot = (uint16_t)(parentobject - OBJ_REF_STATIC_BASE);
		parentobject = (uint16_t)(static_slot + OBJ_REF_STATIC_BASE);
		worldx += ((int32_t)(uint16_t)staticobjects[static_slot].world_x << 8) - camera.x;
		worldy += ((int32_t)(uint16_t)staticobjects[static_slot].world_y << 8) - camera.y;
		worldz += ((int32_t)(uint16_t)staticobjects[static_slot].world_z << 8) - camera.z;
	} else {
		worldx = objects[parentobject].world_x - camera.x;
		worldy = objects[parentobject].world_y - camera.y;
		worldz = objects[parentobject].world_z - camera.z;
	}
	objecteyez = entry->eye_z;
	scale = (uint16_t)rotscale_calcscale(entry->eye_z, species_table[species_idx].bound_hwidth,
										 entry->scale_factor);
	handle = species_table[species_idx].model_handle;
	blob = (const uint8_t*)xmemhdl_Lock_Handle(handle);
	xmemhdl_Unlock_Handle(handle);
	table_offset = *(const uint32_t*)(blob + 16);
	frame_offset = *(const uint32_t*)(blob + table_offset + 4 * bitmap_idx);
	if (TIE_DISPLAY_DX5 && g_useHardware3D) {
		RenderQuad_DrawRotatedSprite(entry->angle, entry->screen_x, entry->screen_y, scale,
									 blob + frame_offset);
	} else {
		rotscale_prepare_fastdraw((uint16_t)entry->angle);
		rotscale_prepare_color((const char*)(blob + frame_offset));
		rotscale_rotate_scale_image(entry->screen_x, entry->screen_y, scale, blob + frame_offset);
	}
}

/* ====================================================================== *
 * anim_drawverysimpleobject
 * ----------------------------------------------------------------------------
 * Per-frame draw call for genus<=5 objects whose visible form is driven
 * by a frame-list animation pattern (small props, embers, sparks, debris).
 *
 * For ship_idx == 89 the sprite anim runs off anim_frame_alt instead of
 * anim_frame; the polymesh path falls through into a sparks2[] read after
 * the mesh draw to enqueue a sparkle layer.
 * ====================================================================== */
// FUNCTION: TIE95 0x10390
int16_t anim_drawverysimpleobject(uint16_t obj_idx_arg) {
	uint16_t ship_type = (uint16_t)objects[obj_idx_arg].ship_idx;

	AnimOp op;
	int16_t result;

	curgenus = objects[obj_idx_arg].genus;
	parentobject = obj_idx_arg;
	animptr = (AnimOp*)species_table[ship_type].draw_data;

	if (ship_type == 89) {
		/* Component-debris: anim_frame_alt drives display, scaled /2
		 * because anim_frame_alt advances at 2x rate (createcomponent
		 * sets it to 2*mesh_idx). */
		op = (AnimOp)(objects[obj_idx_arg].anim_frame >> 1);
	} else {
		if (!animptr)
			return 0;
		animindex = objects[obj_idx_arg].anim_frame;
		op = animptr[objects[obj_idx_arg].anim_frame];
	}

	result = (int16_t)op;

	/* MESH opcode -- draw the polymesh component through DRAWPOL. */
	if (op < 0x8000u) {
		uint16_t mesh_ship = ship_type;
		int32_t saved_eyex;
		int32_t saved_eyey;
		int32_t saved_eyez;
		uint8_t saved_marks;
		ShipMeshLOD* lod;

		if (ship_type == 89)
			mesh_ship = objects[obj_idx_arg].ship_type_override;
		draw_lockshipfileptrs(mesh_ship);

		saved_eyex = objecteyex;
		saved_eyey = objecteyey;
		saved_eyez = objecteyez;
		solidindex = op;

		/* Per-instance markings override for ship_idx 17 + mesh_type 2:
		 * suppress markings when objects[a].side != 0 so allied capital
		 * ships render with their identifying decals while enemies stay
		 * plain. drawmarkingsflag is restored after drawpol returns. */
		saved_marks = drawmarkingsflag;
		if (mesh_ship == 17 && componentblockptr[op].mesh_type == 2)
			drawmarkingsflag = (objects[obj_idx_arg].side == 0);

		lod = (ShipMeshLOD*)draw_getcompdetailptr(&componentblockptr[op], objecteyez);
		drawpol_drawpolyobject((const uint16_t*)lod, saved_eyex, saved_eyey, saved_eyez);
		drawmarkingsflag = saved_marks;

		/* ship_idx 89 meshes also emit a sparkle bitmap layer read
		 * straight from sparks2[anim_frame_alt]; reassign op so the
		 * BITMAP branch below can pick it up on the same tick. */
		if (ship_type == 89) {
			uint8_t alt = objects[obj_idx_arg].anim_frame_alt;
			animindex = alt;
			op = sparks2[alt];
			result = (int16_t)op;
		}
	}

	/* BITMAP opcode -- billboarded sprite at projected (screen_x, screen_y). */
	if ((op >= 0x8000u && op < 0xFF00u) && objecteyez >= 0) {
		/* Pick the world-to-eye axis pair (A or B) most horizontal in
		 * eye space; its planar atan2 gives the billboard rotation. */
		int32_t abs_a3 = rotworldeyeA3 < 0 ? -rotworldeyeA3 : rotworldeyeA3;
		int32_t abs_b3 = rotworldeyeB3 < 0 ? -rotworldeyeB3 : rotworldeyeB3;
		int32_t ax_x, ax_y;
		int16_t angle;
		uint32_t sx_full;
		int16_t sx_lo;
		int32_t sx_hi;

		if (abs_a3 >= abs_b3) {
			ax_x = rotworldeyeB1;
			ax_y = rotworldeyeB2;
		} else {
			ax_x = rotworldeyeA1;
			ax_y = rotworldeyeA2;
		}

		if (ax_x >= 0)
			angle = (int16_t)(-(int32_t)trig2_arctan(ax_y, ax_x));
		else
			angle = trig2_arctan(ax_y, -ax_x);

		sx_full = (uint32_t)transfm2_getscreenx(objecteyex, objecteyez);
		sx_lo = (int16_t)sx_full;
		sx_hi = (int32_t)sx_full >> 16;
		if (sx_hi <= 0 && sx_hi >= -1) {
			int32_t sy_full = transfm2_getscreeny(objecteyey, objecteyez);
			int32_t sy_hi = sy_full >> 16;
			if (sy_hi <= 0 && sy_hi >= -1) {
				int16_t sy = (int16_t)((int32_t)pixelsdeep - sy_full);

				/* Damage ramps the scale: full health = 256, then
				 * (damage_state * 64) + 1 once any damage is on. */
				uint8_t dmg = objects[obj_idx_arg].damage_state;
				uint16_t scale_q8;
				if (dmg) {
					scale_q8 = (uint16_t)(dmg << 6);
					if (scale_q8 >= 0x100u)
						scale_q8 = (uint16_t)(scale_q8 + 0x100u);
				} else {
					scale_q8 = 256;
				}
				anim_add_bitmap_draw(parentobject, op, scale_q8, sx_lo, sy, objecteyez, angle);
#ifdef TIE_MODERN
				TieBillboardCapture_Flight(obj_idx_arg, op, objecteyez, scale_q8, angle);
#endif
				result = (int16_t)scale_q8;
			}
		}
	}
	return result;
}

// FUNCTION: TIE98 0x401070
void anim_drawverysimpleobject_tie98(uint16_t object_index) {
	FlightObject* object = &objects[object_index];
	uint16_t model_type = object->ship_idx;
	AnimOp frame;
	int32_t abs_a3;
	int32_t abs_b3;
	int32_t axis_x;
	int32_t axis_y;
	int16_t angle;
	int32_t screen_x;
	int32_t screen_y;
	uint16_t scale;

	parentobject = object_index;
	curgenus = object->genus;
	animptr = (AnimOp*)species_table[model_type].draw_data;

	if (model_type == 89) {
		frame = (AnimOp)(object->anim_frame >> 1);
	} else {
		if (animptr == NULL)
			return;
		animindex = object->anim_frame;
		frame = animptr[animindex];
	}
	if (frame >= 0xFF00u)
		return;

	if (frame < 0x8000u) {
		solidindex = frame;
		FlightModel_Draw_Object_Mesh(object, frame);
		if (model_type == 89) {
			animindex = object->anim_frame_alt;
			frame = sparks2[animindex];
		}
	}
	if (!(frame >= 0x8000u && frame < 0xFF00u) || objecteyez < 0)
		return;

	abs_a3 = rotworldeyeA3 < 0 ? -rotworldeyeA3 : rotworldeyeA3;
	abs_b3 = rotworldeyeB3 < 0 ? -rotworldeyeB3 : rotworldeyeB3;
	axis_x = abs_a3 >= abs_b3 ? rotworldeyeB1 : rotworldeyeA1;
	axis_y = abs_a3 >= abs_b3 ? rotworldeyeB2 : rotworldeyeA2;
	angle = axis_x >= 0 ? (int16_t)-(int32_t)trig2_arctan(axis_y, axis_x) : trig2_arctan(axis_y, -axis_x);
	screen_x = transfm2_getscreenx(objecteyex, objecteyez);
	if ((screen_x >> 16) > 0 || (screen_x >> 16) < -1)
		return;
	screen_y = transfm2_getscreeny(objecteyey, objecteyez);
	if ((screen_y >> 16) > 0 || (screen_y >> 16) < -1)
		return;

	scale = 256;
	if (object->damage_state != 0) {
		scale = (uint16_t)(object->damage_state << 6);
		if (scale >= 256)
			scale = (uint16_t)(scale + 256);
	}
	anim_add_bitmap_draw(parentobject, frame, scale, (int16_t)screen_x,
						 (int16_t)((int32_t)pixelsdeep - screen_y), objecteyez, angle);
#ifdef TIE_MODERN
	TieBillboardCapture_Flight(object_index, frame, objecteyez, scale, angle);
#endif
}

/* ====================================================================== *
 * anim_updateanimstate -- pattern VM single-step
 * ----------------------------------------------------------------------------
 * Peek the NEXT opcode (animptr[animindex+1]) and decide where animindex
 * ends up after this tick:
 *
 *   KILL   -- zero the parent slot, advance past the KILL opcode.
 *   RESET  -- snap animindex to 0; no advance (do NOT skip past RESET).
 *   JUMP   -- animindex := operand.
 *   DELAY / MESH / BITMAP -- plain advance (animindex += 1).
 *
 * obj_or_kind is needed only by KILL, which uses the high byte to pick
 * staticobjects[] vs objects[]. If animptr is NULL the function is a
 * no-op (animindex stays put).
 * ====================================================================== */
// FUNCTION: TIE95 0x11224
int16_t anim_updateanimstate(uint16_t obj_or_kind) {
	uint16_t next;
	AnimOp op;

	if (!animptr)
		return 0;

	next = (uint16_t)(animindex + 1);
	op = animptr[next];

	if (op == ANIMOP_RESET) {
		animindex = 0;
		return (int16_t)op;
	}

	if (op == ANIMOP_KILL) {
		uint8_t slot = (uint8_t)obj_or_kind;
		if ((obj_or_kind & 0xFF00u) == OBJ_REF_STATIC_BASE)
			staticobjects[slot].species = 0;
		else
			objects[slot].ship_idx = 0;
		/* Fall through: still advance past the KILL. */
	} else if ((op >= 0xFF00u && op <= 0xFFFCu)) {
		next = (uint8_t)op;
	}
	/* DELAY / MESH / BITMAP: advance, no side effect. */

	animindex = next;
	return (int16_t)op;
}

// FUNCTION: TIE98 0x401600
// ANIM_updateanimation
void anim_updateanimation_tie98(void) {
	uint16_t object_index;
	uint16_t index;

	if (mission.train_craft_type)
		gate_updategateanimations();
	if (timers[TIMER_ANIM_UPDATE])
		return;
	timers[TIMER_ANIM_UPDATE] = 29;
	for (object_index = 0; object_index < NUM_OBJECTS; ++object_index) {
		FlightObject* object = &objects[object_index];
		if (!object->ship_idx)
			continue;
		curgenus = object->genus;
		animptr = (AnimOp*)species_table[object->ship_idx].draw_data;
		if (curgenus == GENUS_DEBRIS || curgenus == GENUS_EXPLOSION) {
			if (object->ship_idx == 89) {
				object->anim_frame_alt = 0;
				if ((uint16_t)math2_getrandom() < 0x800u)
					create_createember(object_index);
			} else {
				animindex = object->anim_frame;
				anim_updateanimstate(object_index);
				object->anim_frame = (uint8_t)animindex;
			}
		} else if (curgenus <= GENUS_PLATFORM) {
			const uint8_t model_type = object->ship_idx;
			int mesh_count;
			int rotary_wing_moved;
			int mesh;

			craftptr = object->craft_ptr;
			mesh_count = modelmesh_getcount(model_type);
			rotary_wing_moved = 0;
			if (craftptr->status_flags && craftptr->current_order >= 3) {
				uint16_t weapon;

				for (weapon = 0; weapon < craftptr->weapon_group_cnt; ++weapon) {
					uint8_t mesh_index;
					uint16_t target;
					const TieModelRotationScale* rotation;
					int32_t local_forward;
					int32_t local_up;
					int32_t local_side;
					int32_t x;
					int32_t y;
					int32_t z;
					int32_t up;
					int32_t direction;

					if (craftptr->weapon_slots[weapon].type != 2)
						continue;
					mesh_index = spec_data[craftptr->species_idx].hp[weapon].component;
					if (!craftptr->mesh_component_hp[mesh_index] ||
						modelmesh_gettype(model_type, mesh_index) != TIE_MESH_ROTARY_GUN_TURRET)
						continue;

					target = craftptr->weapon_slots[weapon].target_obj;
					if (target == 0xFFFFu) {
						if (craftptr->mesh_rotation[mesh_index] & 1u)
							craftptr->mesh_rotation[mesh_index] += 4;
						else
							craftptr->mesh_rotation[mesh_index] -= 4;
						if ((uint16_t)math2_getrandom() < 0x600u)
							craftptr->mesh_rotation[mesh_index] ^= 1u;
						continue;
					}

					rotation = modelmesh_getrotscaledata(model_type, mesh_index);
					create_getworldposition(target, 0);
					worldlocx -= object->world_x;
					worldlocy -= object->world_y;
					worldlocz -= object->world_z;
					if (object->orient_dirty) {
						fview_calcrotatemove(object->heading, object->pitch, object);
						fview_calcrotateorient(object->roll, 0, object);
					}

					local_forward = -((uint32_t)math2_mul_q15(object->fwd_y, worldlocy) +
									  (uint32_t)math2_mul_q15(object->fwd_x, worldlocx) +
									  (uint32_t)math2_mul_q15(object->fwd_z, worldlocz));
					local_up = (uint32_t)math2_mul_q15(object->up_x, worldlocy) +
							   (uint32_t)math2_mul_q15(object->side_z, worldlocx) +
							   (uint32_t)math2_mul_q15(object->up_y, worldlocz);
					local_side = (uint32_t)math2_mul_q15(object->side_x, worldlocy) +
								 (uint32_t)math2_mul_q15(object->fwd_z, worldlocx) +
								 (uint32_t)math2_mul_q15(object->side_y, worldlocz);
					x = local_side - (int32_t)rotation->pivot.x;
					y = local_forward - (int32_t)rotation->pivot.y;
					z = local_up - (int32_t)rotation->pivot.z;
					up = (uint32_t)math2_mul_q15(rotation->up_axis.x, x) +
						 (uint32_t)math2_mul_q15(rotation->up_axis.y, y) +
						 (uint32_t)math2_mul_q15(rotation->up_axis.z, z);
					direction = (uint32_t)math2_mul_q15(rotation->direction_axis.x, x) +
								(uint32_t)math2_mul_q15(rotation->direction_axis.y, y) +
								(uint32_t)math2_mul_q15(rotation->direction_axis.z, z);
					craftptr->mesh_rotation[mesh_index] =
						(uint8_t)((uint16_t)trig2_arctan(up, direction) >> 8);
				}
			}

			for (mesh = 0; mesh < mesh_count; ++mesh) {
				const int mesh_type = modelmesh_gettype(model_type, mesh);
				if (mesh_type == TIE_MESH_FUSELAGE) {
					animptr = lightning;
					animindex = craftptr->mesh_state[mesh_count];
					anim_updateanimstate(object_index);
					craftptr->mesh_state[mesh_count] = (uint8_t)animindex;
				}
				if (craftptr->flight_flag == 3) {
					if (species_table[model_type].bound_hwidth <= 0x578u) {
						create_blowoffcomponent(object_index, 0);
						if ((uint16_t)math2_getrandom() < 0x1800u)
							create_createember(object_index);
					} else {
						starship_createstarshipexplo(object_index, 0);
						mesh += 3;
						continue;
					}
				}
				if (craftptr->status_flags && craftptr->current_order >= 3 &&
					(mesh_type == TIE_MESH_COMMUNICATIONS || mesh_type == TIE_MESH_ROTARY_COMMUNICATIONS ||
					 mesh_type == TIE_MESH_BEAM_SYSTEM || mesh_type == TIE_MESH_ROTARY_BEAM_SYSTEM ||
					 mesh_type == TIE_MESH_COMMAND_BEAM || mesh_type == TIE_MESH_ROTARY_COMMAND_BEAM)) {
					if (craftptr->mesh_rotation[mesh] & 1u)
						craftptr->mesh_rotation[mesh] += 4;
					else
						craftptr->mesh_rotation[mesh] -= 4;
					if ((uint16_t)math2_getrandom() < 0x200u)
						craftptr->mesh_rotation[mesh] ^= 1u;
				}
				if (mesh_type == TIE_MESH_ROTARY_WING && (craftptr->ai_anim_flags & 1u)) {
					uint8_t* angle = &craftptr->mesh_rotation[mesh];
					if (craftptr->ai_anim_flags & 2u) {
						if (model_type == 1) {
							const uint8_t limit = modelmesh_getcenterz(1, mesh) < 0 ? 8 : 12;
							if (*angle < limit) {
								++*angle;
								rotary_wing_moved = 1;
							}
						} else if (model_type == 4 && *angle < 0x40u) {
							*angle += 3;
							rotary_wing_moved = 1;
						}
					} else if (*angle) {
						*angle -= model_type == 4 ? 3 : 1;
						rotary_wing_moved = 1;
					}
				}
			}
			if (craftptr->ai_anim_flags & 1u) {
				if ((craftptr->ai_anim_flags & 2u) && !rotary_wing_moved) {
					craftptr->ai_anim_flags = 2;
					argtable[0] = MSG_SFOIL_CLOSED;
					msg_messageprintf(MSG_SFOIL_AT_POS);
				} else if (!(craftptr->ai_anim_flags & 2u) && !rotary_wing_moved) {
					craftptr->ai_anim_flags = 0;
					argtable[0] = MSG_SFOIL_OPEN;
					msg_messageprintf(MSG_SFOIL_AT_POS);
				}
			}
		}
	}
	for (index = 0; index < NUM_STATIC_OBJECTS; ++index) {
		if (!staticobjects[index].species)
			continue;
		animptr = (AnimOp*)species_table[staticobjects[index].species].draw_data;
		if (!animptr)
			continue;
		animindex = staticobjects[index].anim_frame;
		anim_updateanimstate(OBJ_REF_STATIC_BASE | index);
		staticobjects[index].anim_frame = (uint8_t)animindex;
	}
}

/* Timer-gated animation update for turrets, articulated craft meshes, debris,
 * explosions, and static objects. Static animation references use the packed
 * OBJ_REF_STATIC_BASE namespace. */
// FUNCTION: TIE95 0x109BC
void anim_updateanimation(void) {
	uint16_t obj;
	uint16_t static_packed;
	uint16_t s;

	if (TIE_FLIGHT_TIE98) {
		anim_updateanimation_tie98();
		return;
	}
	if (mission.train_craft_type)
		gate_updategateanimations();

	if (timers[TIMER_ANIM_UPDATE])
		return;
	timers[TIMER_ANIM_UPDATE] = 29;

	for (obj = 0; obj < NUM_OBJECTS; ++obj) {
		uint8_t ship_idx_b = objects[obj].ship_idx;
		CraftData* cd;
		uint16_t num_meshes;
		int16_t rotwing_animated;
		uint16_t w;
		ShipModelMesh* mesh_iter;
		uint16_t i;

		if (!ship_idx_b)
			continue;

		curgenus = objects[obj].genus;
		animptr = (AnimOp*)species_table[ship_idx_b].draw_data;
		cd = objects[obj].craft_ptr;

		if (curgenus >= GENUS_DEBRIS) {
			/* genus 11+13 -- debris / embers. Other genera ignored. */
			if (curgenus > GENUS_DEBRIS && curgenus != GENUS_EXPLOSION)
				continue;

			if (objects[obj].ship_idx == 89) {
				animindex = objects[obj].anim_frame_alt;
				animptr = sparks2;
				anim_updateanimstate(obj);
				objects[obj].anim_frame_alt = (uint8_t)animindex;
				if ((uint16_t)math2_getrandom() < 0x800u)
					create_createember(obj);
			} else {
				animindex = objects[obj].anim_frame;
				anim_updateanimstate(obj);
				objects[obj].anim_frame = (uint8_t)animindex;
			}
			continue;
		}

		/* genus <= 5 -- real ship (fighter through platform). */
		if (curgenus > GENUS_PLATFORM)
			continue;

		draw_lockshipfileptrs(ship_idx_b);
		num_meshes = objectblockptr->num_meshes;
		rotwing_animated = 0;
		craftptr = objects[obj].craft_ptr;

		/* --- Turret-aim pass: every weapon slot of type 2. ----------- */
		for (w = 0; w < craftptr->weapon_group_cnt; ++w) {
			uint8_t mesh_idx;
			uint16_t target;

			if (craftptr->weapon_slots[w].type != 2)
				continue;

			mesh_idx = spec_data[craftptr->species_idx].hp[w].component;
			if (!craftptr->mesh_component_hp[mesh_idx])
				continue;

			target = craftptr->weapon_slots[w].target_obj;
			if (target == 0xFFFFu) {
				/* Idle sweep. mesh_rotation[idx] += 4 or -= 4 based on
				 * bit 0 (toggles direction); ~9% chance/tick to flip. */
				if (craftptr->mesh_rotation[mesh_idx] & 1u)
					craftptr->mesh_rotation[mesh_idx] += 4;
				else
					craftptr->mesh_rotation[mesh_idx] -= 4;
				if ((uint16_t)math2_getrandom() < 0x600u)
					craftptr->mesh_rotation[mesh_idx] ^= 1u;
			} else {
				/* Aim turret: transform target world pos into the parent
				 * craft's local frame, then through the mesh's
				 * rotation_offset block, then atan2 -> mesh_rotation byte. */
				FlightObject* parent = &objects[obj];
				ShipModelMesh* mesh = &componentblockptr[mesh_idx];
				const TurretRotData* rot =
					(const TurretRotData*)((const uint8_t*)mesh + mesh->rotation_offset);

				int32_t rel_y;
				int32_t fwd_x;
				int32_t fwd_y;
				int32_t fwd_z;
				int32_t side_x;
				int32_t side_y;
				int32_t side_z;
				int32_t up_x;
				int32_t up_y;
				int32_t rel_z_eye;
				int32_t eye_y;
				int32_t local_y;
				int32_t aim_x;
				int32_t aim_y;

				create_getworldposition(target, 0);

				/* Position relative to parent (we'll need this in the
				 * local frame next). */
				worldlocx -= parent->world_x;
				rel_y = worldlocy - parent->world_y;
				worldlocz -= parent->world_z;
				worldlocy = rel_y;

				if (parent->orient_dirty) {
					fview_calcrotatemove(parent->heading, parent->pitch, parent);
					fview_calcrotateorient(parent->roll, 0, parent);
				}

				/* Parent's orient matrix entries (Watcom emitted
				 * unaligned-dword-HIWORD reads starting at orient_dirty;
				 * each one decodes to the next int16 field). */
				fwd_x = parent->fwd_x;
				fwd_y = parent->fwd_y;
				fwd_z = parent->fwd_z;
				side_x = parent->side_x;
				side_y = parent->side_y;
				side_z = parent->side_z;
				up_x = parent->up_x;
				up_y = parent->up_y;

				rel_z_eye =
					-((uint32_t)math2_mul_q15(fwd_y, worldlocy) + (uint32_t)math2_mul_q15(fwd_x, worldlocx) +
					  (uint32_t)math2_mul_q15(fwd_z, worldlocz));
				eye_y = (uint32_t)math2_mul_q15(up_x, worldlocy) +
						(uint32_t)math2_mul_q15(side_z, worldlocx) + (uint32_t)math2_mul_q15(up_y, worldlocz);
				worldlocx = (uint32_t)math2_mul_q15(side_x, worldlocy) +
							(uint32_t)math2_mul_q15(fwd_z, worldlocx) +
							(uint32_t)math2_mul_q15(side_y, worldlocz) - (rot->origin_x_q15 >> 1);

				local_y = rel_z_eye - (rot->origin_y_q15 >> 1);
				worldlocz = eye_y - (rot->origin_z_q15 >> 1);
				worldlocy = local_y;

				/* 2x3 projection matrix at +0xC..+0x17 maps the turret-
				 * local point to (aim_x, aim_y); trig2_arctan -> heading. */
				aim_x = (uint32_t)math2_mul_q15(rot->aim_x_ly, local_y) +
						(uint32_t)math2_mul_q15(rot->aim_x_wx, worldlocx) +
						(uint32_t)math2_mul_q15(rot->aim_x_wz, worldlocz);
				aim_y = (uint32_t)math2_mul_q15(rot->aim_y_ly, local_y) +
						(uint32_t)math2_mul_q15(rot->aim_y_wx, worldlocx) +
						(uint32_t)math2_mul_q15(rot->aim_y_wz, worldlocz);
				craftptr->mesh_rotation[mesh_idx] = (uint8_t)((uint16_t)trig2_arctan(aim_y, aim_x) >> 8);
			}
		}

		/* --- Per-mesh pass: lightning, explosions, idle sweep, S-foils. */
		mesh_iter = componentblockptr;
		for (i = 0; i < num_meshes; ++i, ++mesh_iter) {
			uint16_t mt;

			if (mesh_iter->mesh_type == 3 /* MESH_Fuselage */) {
				/* Lightning anim slot for this fuselage uses
				 * mesh_state[num_meshes] (one PAST the per-mesh state
				 * range; 40-byte array gives room). */
				animptr = lightning;
				animindex = cd->mesh_state[num_meshes];
				anim_updateanimstate(obj);
				cd->mesh_state[num_meshes] = (uint8_t)animindex;
			}

			if (craftptr->flight_flag == 3 /* DEAD */) {
				if (species_table[ship_idx_b].bound_hwidth <= 0x578u) {
					/* Small ship: pop a random component, sometimes
					 * spawn an ember. */
					create_blowoffcomponent(obj, 0);
					if ((uint16_t)math2_getrandom() < 0x1800u)
						create_createember(obj);
				} else {
					/* Starship: trigger explosion macro and skip 3
					 * mesh slots ahead. */
					i += 3;
					mesh_iter += 3;
					starship_createstarshipexplo(obj, 0);
				}
			}

			mt = mesh_iter->mesh_type;
			if (mt == 11 /*CommSys*/ || mt == 23 /*RotCommSys*/ || mt == 12 /*BeamSys*/ ||
				mt == 24 /*RotBeamSys*/ || mt == 13 /*CmdVBeam*/ || mt == 25 /*RotCmdBeam*/) {
				/* Idle antenna/dish sweep, identical pattern to the
				 * idle turret branch above. */
				if (craftptr->mesh_rotation[i] & 1u)
					craftptr->mesh_rotation[i] += 4;
				else
					craftptr->mesh_rotation[i] -= 4;
				if ((uint16_t)math2_getrandom() < 0x200u)
					craftptr->mesh_rotation[i] ^= 1u;
			}

			if (mesh_iter->mesh_type == 20 /* MESH_RotWing */ && (craftptr->ai_anim_flags & 1u)) {
				/* S-foils opening or closing. bit 1 of ai_anim_flags
				 * picks direction: set = closing (mesh_rotation
				 * INCREASES toward limit), clear = opening (decreases
				 * toward 0). */
				if (craftptr->ai_anim_flags & 2u) {
					/* Moving toward closed. Per-ship limit. */
					if (ship_idx_b == 1) {
						/* X-Wing: top wing closes to 12, bottom to 8.
						 * mesh.center_up sign picks top vs bottom. */
						uint16_t limit = (mesh_iter->center_up >= 0) ? 12u : 8u;
						uint8_t pos = craftptr->mesh_rotation[i];
						if (pos < limit) {
							rotwing_animated = 1;
							craftptr->mesh_rotation[i] = (uint8_t)(pos + 1);
						}
					} else if (ship_idx_b == 4) {
						/* Y-Wing: closes to 0x40, step 3. */
						uint8_t pos = craftptr->mesh_rotation[i];
						if (pos < 0x40u) {
							rotwing_animated = 1;
							craftptr->mesh_rotation[i] = (uint8_t)(pos + 3);
						}
					}
				} else {
					/* Moving toward open (rotation -> 0). */
					if (ship_idx_b == 1) {
						uint8_t pos = craftptr->mesh_rotation[i];
						if (pos) {
							rotwing_animated = 1;
							craftptr->mesh_rotation[i] = (uint8_t)(pos - 1);
						}
					} else if (ship_idx_b == 4) {
						uint8_t pos = craftptr->mesh_rotation[i];
						if (pos) {
							rotwing_animated = 1;
							craftptr->mesh_rotation[i] = (uint8_t)(pos - 3);
						}
					}
				}
			}
		}

		/* When the S-foil animation completes (no motion this tick),
		 * clear bit 0 (in-motion), keep bit 1 set if we just reached
		 * closed (or clear all if we reached open), then announce the
		 * new resting state. */
		if (craftptr->ai_anim_flags & 1u) {
			if (craftptr->ai_anim_flags & 2u) {
				if (!rotwing_animated) {
					craftptr->ai_anim_flags = 2;
					argtable[0] = MSG_SFOIL_CLOSED;
					msg_messageprintf(MSG_SFOIL_AT_POS);
				}
			} else if (!rotwing_animated) {
				craftptr->ai_anim_flags = 0;
				argtable[0] = MSG_SFOIL_OPEN;
				msg_messageprintf(MSG_SFOIL_AT_POS);
			}
		}
	}

	/* --- Static-object frame advance ------------------------------- */
	static_packed = OBJ_REF_STATIC_BASE;
	for (s = 0; s < NUM_STATIC_OBJECTS; ++s, ++static_packed) {
		if (!staticobjects[s].species)
			continue;
		animptr = (AnimOp*)species_table[staticobjects[s].species].draw_data;
		if (!animptr)
			continue;
		animindex = staticobjects[s].anim_frame;
		anim_updateanimstate(static_packed);
		staticobjects[s].anim_frame = (uint8_t)animindex;
	}
}

/* Six-phase hyperspace sequence: align and validate the route, replace the
 * scene with stars, stretch streaks, traverse, retract streaks, then settle
 * and complete the mission transition. hyperticks is the absolute phase clock. */
// FUNCTION: TIE95 0x112EC
// FUNCTION: TIE98 0x401E80
void anim_dohyperspace(void) {
	int16_t tilt_speed;

	hyperticks += frameticks;

	if ((uint8_t)(hyperspaceflag - 1) > 5u)
		return;

	tilt_speed = (int16_t)(224 * frameticks);

	switch (hyperspaceflag) {
		case 1: {
			uint16_t roll = (uint16_t)pstate.player->roll;
			uint16_t pitch = (uint16_t)pstate.player->pitch;
			uint16_t heading = (uint16_t)pstate.player->heading;

			if (roll || heading != 0x4000u || pstate.player->pitch != 0) {
				/* Still leveling -- nudge each axis toward its target. */
				int16_t lvl = (int16_t)(20 * frameticks);
				int16_t src_roll = pstate.player->roll;
				int16_t src_pitch = pstate.player->pitch;

				if (roll >= 0x8000u) {
					roll = (uint16_t)(roll + lvl);
					if ((uint16_t)(lvl + src_roll) < 0x8000u)
						roll = 0;
				} else {
					roll = (uint16_t)(roll - lvl);
					if ((uint16_t)(src_roll - lvl) >= 0x8000u)
						roll = 0;
				}
				if (pitch >= 0x8000u) {
					pitch = (uint16_t)(pitch + lvl);
					if ((uint16_t)(lvl + src_pitch) < 0x8000u)
						pitch = 0;
				} else {
					pitch = (uint16_t)(pitch - lvl);
					if ((uint16_t)(src_pitch - lvl) >= 0x8000u)
						pitch = 0;
				}
				if (heading >= 0xC000u || (uint16_t)pstate.player->heading <= 0x4000u) {
					if (heading != 0x4000u) {
						uint32_t h = (uint16_t)(lvl + heading);
						heading = (uint16_t)h;
						if ((uint16_t)h > 0x4000u && h < 49152u)
							heading = 0x4000u;
					}
				} else {
					heading = (uint16_t)(heading - lvl);
					if ((uint16_t)(pstate.player->heading - lvl) < 0x4000u)
						heading = 0x4000u;
				}
			} else if (hyperticks < 0x49Cu) {
				if (hyperticks >= 0x2C4u) {
					/* Aligned and inside the launch window -- look for
					 * blocking objects in the path. */
					int16_t blocked = 0;
					uint16_t i;

					for (i = 0; i < NUM_CRAFTS; ++i) {
						int32_t dx;
						int32_t dy;
						int32_t dz;
						int32_t b_obj;
						int32_t b_player;

						if (!objects[i].ship_idx)
							continue;
						dx = objects[i].world_x - pstate.player->world_x;
						dy = objects[i].world_y - pstate.player->world_y;
						dz = objects[i].world_z - pstate.player->world_z;
						if (dx < 0)
							dx = -dx;
						if (dz < 0)
							dz = -dz;
						b_obj = species_table[objects[i].ship_idx].bound_hwidth;
						b_player = species_table[pstate.player->ship_idx].bound_hwidth;
						if (dx - b_obj < b_player && dz - b_obj < b_player && dy > 0 && dy < 0x40000) {
							blocked = 1;
							break;
						}
					}
					if (!blocked) {
						uint16_t static_packed = OBJ_REF_STATIC_BASE;
						int16_t i;

						for (i = 0; i < NUM_STATIC_OBJECTS; ++i, ++static_packed) {
							int32_t dx;
							int32_t dy;
							int32_t dz;
							int32_t b_st;
							int32_t b_player;

							if (!staticobjects[i].species)
								continue;
							create_getworldposition(static_packed, 0);
							dx = worldlocx - pstate.player->world_x;
							dy = worldlocy - pstate.player->world_y;
							dz = worldlocz - pstate.player->world_z;
							if (dx < 0)
								dx = -dx;
							if (dz < 0)
								dz = -dz;
							b_st = species_table[staticobjects[i].species].bound_hwidth;
							b_player = species_table[pstate.player->ship_idx].bound_hwidth;
							if (dx - b_st < b_player && dz - b_st < b_player && dy > 0 && dy < 0x40000) {
								blocked = 1;
								break;
							}
						}
					}
					if (blocked) {
						msg_messageprintf(MSG_HYPER_OBJECT_BLOCK);
						fsfx_triggersfx(0x27u, 0xFFFFu);
						hyperspaceflag = 0;
						pstate.player->current_speed = 10;
					}
					pstate.player->world_y += 1792 * frameticks;
				}
			} else {
				user_checkreplaycamera();
				pstate.player->current_speed = 100;
				if (replayviewmode) {
					hyperspaceflag = 0;
				} else {
					hyperspaceflag = 2;
					msg_messageprintf(MSG_ENTERING_HYPER_NOW);
				}
			}

			pstate.player->orient_dirty = 1;
			pstate.player->roll = (int16_t)roll;
			pstate.player->pitch = (int16_t)pitch;
			pstate.player->heading = (int16_t)heading;
			pstate.player->move_dirty = pstate.player->orient_dirty;
			pstate.player_craft->orient_heading = heading;
			break;
		}

		case 2: {
			int16_t i;
			int16_t j;
			int16_t saved_seed;
			uint16_t s;

			hyperticks = 1180;
			for (i = 0; i < (int16_t)NUM_OBJECTS; ++i) {
				if (i == (int16_t)pstate.object_idx)
					continue;
				objects[i].ship_idx = 0;
				objects[i].age_ticks = 0;
				objects[i].death_timer = 0;
			}
			for (j = 0; j < 64; ++j)
				staticobjects[j].species = 0;

			hyperfgnumber = (uint16_t)mission_file_header.num_fg;
			mission_file_header.num_fg = 0;

			/* Re-seed 64 staticobject slots as starfield positions. Force
			 * the LFSR seed to the retail-fixed 29287 so the starfield is
			 * deterministic across runs, then restore the live seed so the
			 * mission's RNG sequence isn't perturbed by this 192-call
			 * burst. */
			saved_seed = math2_randomseed;
			math2_randomseed = 29287;
			for (s = 0; s < 64; ++s) {
				uint16_t r1 = (uint16_t)math2_getrandom();
				int16_t sx = (int16_t)math2_fraction(r1, pixelswide) - (int16_t)(pixelswide / 2);
				uint16_t r2 = (uint16_t)math2_getrandom();
				int16_t sy = (int16_t)(((r2 >> 4) & 0x1F) + 0x100);
				uint16_t r3 = (uint16_t)math2_getrandom();
				int16_t sz = (int16_t)math2_fraction(r3, pixelsdeep) - (int16_t)(pixelsdeep / 2);
				staticobjects[s].world_y = sy;
				staticobjects[s].world_z = sz;
				staticobjects[s].world_x = sx;
				staticobjects[s].species = 0; /* binary clears each slot */
			}
			math2_randomseed = saved_seed;

			hypertemp1 = drawbackdropflag;
			hypertemp2 = drawdebrisflag;
			drawbackdropflag = 0;
			drawdebrisflag = 0;
			if (TIE_FLIGHT_TIE98)
				g_hyperspaceStreakLength = 32256;
			else
				hyperstarlength = 32256;
			pstate.player->world_x = 0;
			fullupdateflag = 1;
			if (TIE_FLIGHT_TIE98)
				g_flightInitialTextureCacheFlushPending = 1;
			pstate.player->world_y = 0;
			hyperspaceflag = 3;
			pstate.player->world_z = 0;
			*(int16_t*)&hyperstardata[6] = 32272;
			*(int16_t*)&hyperstardata[12] = 32256;
			fsfx_triggersfx(0x30u, 0xFFFFu);
#ifdef TIE_MODERN
			{
				/* HYPER_FLASH event on phase 3 entry (jump in).
				 * Position at the player's world origin (the engine pinned
				 * world_x/y/z to 0 just above). param0=0 marks the "in"
				 * direction; the F4 effects pass uses this to gate the
				 * one-frame white flash. */
				TieEvent ev = {
					.kind = TIE_EVENT_HYPER_FLASH,
					.actor_id = pstate.player->idnumber,
					.world_pos = { 0, 0, 0 },
					.param0 = 0,
					.param1 = 0,
				};
				TieSnapshotBuilder_PushEvent(&ev);
			}
#endif
			break;
		}

		case 3:
			if (hyperticks < 0x588u) {
				if (TIE_FLIGHT_TIE98) {
					g_hyperspaceStreakLength += 224 * (uint32_t)frameticks;
				} else {
					/* Clamp the unsigned wrapped endpoint inside (0x7E00, 0x8200). */
					uint16_t s = (uint16_t)(*(int16_t*)&hyperstardata[12] - tilt_speed);
					hyperstarlength = s;
					if (s > 0x7E00u && s < 0x8200u)
						hyperstarlength = (uint16_t)-32256;
					*(int16_t*)&hyperstardata[12] = (int16_t)hyperstarlength;
				}
			} else {
				pstate.player->world_y += 224 * frameticks;
				if (!TIE_FLIGHT_TIE98) {
					*(int16_t*)&hyperstardata[12] = -32256;
					*(int16_t*)&hyperstardata[6] = 32272;
				}
			}
			if (hyperticks >= 0x674u) {
				hyperticks = 1652;
				camera.view_target_obj = 0xFFFFu;
				camera.x = 128;
				camera.y = -896;
				camera.z = 128;
				camera.view_zoom_flag = 1;
				pstate.player->world_y = 0;
				panelrts_setnewpilotview(0x12u);
				camera.side_angle = -2048;
				camera.up_angle = -2048;
				hyperspaceflag = 4;
				imuse_stop_sound(im, (intptr_t)48);
				fsfx_triggersfx(0x31u, 0xFFFFu);
				if (objects[pstate.object_idx].ship_idx == 16)
					fsfx_triggersfx(0x2Cu, 0xFFFFu);
			}
			break;

		case 4:
			pstate.player->world_y += 224 * frameticks;
			if (hyperticks >= 0x760u) {
				camera.view_target_obj = pstate.object_idx;
				camera.view_zoom_flag = 0;
				hyperticks = 1888;
				panelrts_setnewpilotview(0);
				camera.side_angle = 0;
				camera.up_angle = 0;
				hyperspaceflag = 5;
				fsfx_triggersfx(0x32u, 0xFFFFu);
#ifdef TIE_MODERN
				{
					/* HYPER_FLASH event on phase 5 entry (jump out).
					 * param0=1 marks the "out" direction. */
					TieEvent ev = {
					.kind     = TIE_EVENT_HYPER_FLASH,
					.actor_id = pstate.player->idnumber,
						.world_pos = {
							pstate.player->world_x,
							pstate.player->world_y,
							pstate.player->world_z,
					},
					.param0   = 1,
					.param1   = 0,
				};
					TieSnapshotBuilder_PushEvent(&ev);
				}
#endif
			}
			break;

		case 5:
			if (hyperticks < 0x84Cu) {
				pstate.player->world_y -= 224 * frameticks;
			} else if (TIE_FLIGHT_TIE98) {
				g_hyperspaceStreakLength -= 224 * (uint32_t)frameticks;
				if (g_hyperspaceStreakLength < 0x8200u)
					g_hyperspaceStreakLength = 0x8200u;
			} else {
				/* Preserve the unsigned endpoint wrap while retracting the streak. */
				uint16_t s = (uint16_t)(tilt_speed + *(int16_t*)&hyperstardata[12]);
				hyperstarlength = s;
				if (s > 0x7E00u && s < 0x8200u)
					hyperstarlength = (uint16_t)-32256;
				*(int16_t*)&hyperstardata[12] = (int16_t)hyperstarlength;
			}
			if (hyperticks >= 0x938u) {
				int32_t y;

				hyperticks = 2360;
				camera.view_target_obj = 0xFFFFu;
				camera.x = -128;
				camera.view_zoom_flag = 1;
				camera.y = 896;
				camera.z = 128;
				panelrts_setnewpilotview(0x12u);
				y = pstate.player->world_y;
				camera.side_angle = -2048;
				camera.up_angle = 30720;
				pstate.player->world_y = y - 52864;
				drawbackdropflag = (uint8_t)hypertemp1;
				hyperspaceflag = 6;
				drawdebrisflag = (uint8_t)hypertemp2;
			}
			break;

		case 6: {
			int32_t y = pstate.player->world_y;
			pstate.player->current_speed = 30;
			if (-768 - y > 0) {
				int32_t step = (-768 - y) >> 6;
				if (step > 224)
					step = 224;
				pstate.player->world_y += frameticks * step;
			} else if (y > 0) {
				int16_t saved_zoom_hi = camera.view_zoom;
				int16_t y_after;

				hyperspaceflag = 0;
				mission_file_header.num_fg = (int16_t)hyperfgnumber;
				pstate.player->current_speed = 0;
				y_after = (int16_t)pstate.player->world_y;
				mission.end_flag = 2;
				camera.view_zoom = (int16_t)(saved_zoom_hi - y_after);
				msg_messageprintf(MSG_HYPER_COMPLETED);
			}
			break;
		}
	}
}
