#ifndef __ANIM_H__
#define __ANIM_H__

#include <stdint.h>

/* Bitmap queue, frame-list animation, and hyperspace transition state. */

/* ====================================================================== *
 * Bitmap draw queue
 * ====================================================================== */

#define ANIM_DRAWITEMS_MAX 32

/*
 * BitmapDrawEntry -- one queued sprite. 16 bytes, fixed layout matched
 * to the binary so binary-shaped consumers (sort, draw_bitmap) work
 * without translation.
 *
 *   obj_idx        : parent object reference (high byte 0x38 = static)
 *   species_packed : bit15 = is_bitmap flag (set by producer); bits 7..14
 *                    = species_idx (0..255); bits 0..6 = bitmap_idx within
 *                    that species' model blob.
 *   scale_factor   : extra multiplier for rotscale_calcscale (256 = 1.0).
 *   screen_x/y     : pixel coordinates passed straight to ROTSCALE.
 *   eye_z          : signed depth, used both for the depth sort and as
 *                    rotscale_calcscale's depth input.
 *   angle          : 16-bit rotation key passed to rotscale_prepare_fastdraw.
 */
typedef struct BitmapDrawEntry {
	uint16_t obj_idx;
	uint16_t species_packed;
	uint16_t scale_factor;
	int16_t screen_x;
	int16_t screen_y;
	int32_t eye_z;
	int16_t angle;
} BitmapDrawEntry;

extern BitmapDrawEntry drawitems[ANIM_DRAWITEMS_MAX];

/* Enqueue one sprite for drawing this frame. Slot count saturates at
 * ANIM_DRAWITEMS_MAX (further calls overwrite the last slot). */
void anim_add_bitmap_draw(uint16_t obj_idx, uint16_t species_packed, uint16_t scale_factor, int16_t screen_x,
						  int16_t screen_y, int32_t eye_z, int16_t angle);

/* Depth-sort drawitems[0..numbitmaps-1] (back-to-front), draw each via
 * anim_draw_bitmap, then paint the target reticle via user_targetonscreen.
 * Returns the value forwarded from user_targetonscreen. */
int16_t anim_sort_and_draw_bitmaps(void);
void anim_sort_and_draw_bitmaps_tie98(int draw_target);
void anim_draw_bitmap_tie98(const BitmapDrawEntry* entry);

/* Render one queued sprite. Static-object obj_idx (high byte == 0x38) takes
 * the alternate world-position path that adds the static's world coords to
 * the existing _worldx/_worldy/_worldz globals before subtracting camera. */
int16_t anim_draw_bitmap(const BitmapDrawEntry* entry);

/* ====================================================================== *
 * Frame-list animation patterns and tick
 * ====================================================================== */

/*
 * Animation bytecode -- one opcode per u16, one opcode per tick.
 *
 *   [0x0000, 0x8000)   draw polymesh component  (operand = code)
 *   [0x8000, 0xFF00)   draw bitmap              (bit15 | species<<7 | idx)
 *   [0xFF00, 0xFFFD]   jump-to-frame            (operand = code & 0xFF)
 *          0xFFFD      reset to frame 0         (does not advance)
 *          0xFFFE      delay                    (advance, draw nothing)
 *          0xFFFF      kill parent              (advance, zero ship_idx/species)
 *
 * The [DELAY, JUMP(0)] header that prefixes most one-shot/looping patterns
 * is the 'idle state': a freshly-spawned object with anim_frame=0 parks on
 * frame 0 (DELAY draws nothing) and JUMP(0) keeps it there until something
 * bumps anim_frame past the header.
 */
typedef uint16_t AnimOp;

/* ---- producers -------------------------------------------------------- */
#define ANIMOP_MESH(n) ((AnimOp)(n))
#define ANIMOP_BITMAP(spc, idx) ((AnimOp)(0x8000u | ((spc) << 7) | (idx)))
#define ANIMOP_JUMP(frame) ((AnimOp)(0xFF00u | ((frame) & 0xFFu)))
#define ANIMOP_RESET ((AnimOp)0xFFFD)
#define ANIMOP_DELAY ((AnimOp)0xFFFE)
#define ANIMOP_KILL ((AnimOp)0xFFFF)

/* ---- classifiers ------------------------------------------------------ */
static inline int animop_is_mesh(AnimOp op) { return op < 0x8000u; }
static inline int animop_is_bitmap(AnimOp op) { return op >= 0x8000u && op < 0xFF00u; }
static inline int animop_is_jump(AnimOp op) { return op >= 0xFF00u && op <= 0xFFFCu; }
static inline int animop_is_reset(AnimOp op) { return op == ANIMOP_RESET; }
static inline int animop_is_delay(AnimOp op) { return op == ANIMOP_DELAY; }
static inline int animop_is_kill(AnimOp op) { return op == ANIMOP_KILL; }

/* ---- operand accessors ------------------------------------------------ */
/* Bitmap opcode: species and bitmap-within-species indices. */
static inline uint8_t animop_bitmap_species(AnimOp op) { return (uint8_t)((op & 0x7FFFu) >> 7); }
static inline uint8_t animop_bitmap_index(AnimOp op) { return (uint8_t)(op & 0x7Fu); }
/* Jump opcode: destination frame index. */
static inline uint8_t animop_jump_target(AnimOp op) { return (uint8_t)op; }

extern AnimOp bigexplo[13];
extern AnimOp sparks[7];
extern AnimOp sparks2[8];
extern AnimOp ember[2];
extern AnimOp ember2[2];
extern AnimOp fire[14];
extern AnimOp lightning[25];
extern AnimOp debrischunk1[9];
extern AnimOp debrischunk2[11];
extern AnimOp debrischunk3[11];
extern AnimOp debrischunk4[9];
extern AnimOp bigexplo2[14];

/* ANIM module globals. */
extern void* animarrayptr;     /* unused in shipped binary */
extern AnimOp* animptr;        /* current pattern ptr */
extern uint16_t animindex;     /* current frame index */
extern uint16_t hyperfgnumber; /* mission_file_header.num_fg saved across hyperspace */
extern uint8_t curgenus;       /* genus byte cached during anim tick */

/*
 * Per-frame draw call for genus<=5 simple objects. Looks up
 * species[ship_idx].draw_data, picks frame by anim_frame, draws either a
 * polymesh component or enqueues a billboarded bitmap (rotation derived
 * from world->eye matrix). For ship_idx == 89 reads anim_frame_alt and
 * the sparks2[] pattern.
 */
int16_t anim_drawverysimpleobject(uint16_t obj_idx);
void anim_drawverysimpleobject_tie98(uint16_t obj_idx);

/*
 * Tick the pattern by one step. Caller has set animptr to the pattern and
 * animindex to the current frame; this updates animindex per the sentinel
 * semantics above. obj_or_kind is needed for the kill sentinel (high byte
 * 0x38 = static slot). Return value is the next frame code (callers ignore;
 * the side effect on animindex is what matters).
 */
int16_t anim_updateanimstate(uint16_t obj_or_kind);

/*
 * Per-frame entry. Ticks GATE animations every call (if a training-style
 * mission is active). The heavy per-craft pass (turrets aim, fuselage
 * lightning, S-foil opening/closing, ember/explosion spawns, debris/static
 * frame advance) only fires when timers[TIMER_ANIM_UPDATE] hits 0; resets
 * it to 29 ticks. TIE_updatetime decrements the slot every frame.
 */
void anim_updateanimation(void);
void anim_updateanimation_tie98(void);

/* ====================================================================== *
 * Hyperspace state machine
 * ====================================================================== */

/*
 * Six-phase hyperspace 'jump' driven by hyperspaceflag 1..6. Other values
 * are no-ops. Per-phase behaviour documented in anim.c.
 */
void anim_dohyperspace(void);

#endif
