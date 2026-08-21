/*
 * frontend_stubs.c — stub implementations for modules and globals excluded
 * from the frontend-only build (3D engine, flight, in-flight SFX).
 *
 * Screen stubs return to the main menu. Function stubs are no-ops.
 * Globals are zero-initialized placeholders for extern references from
 * stub-only modules (panel.c, gate.c, etc.).
 */

#include "tie/shellext.h"
#include <stdint.h>

/* ================================================================
 * Excluded screen entry points — redirect to main menu
 * ================================================================ */

// FUNCTION: TIE 0x6B4E8
int16_t train_Train(SceneHeadStruct* head) {
	(void)head;
	return SCENE_MAIN_MENU;
}
// FUNCTION: TIE 0x6CB50
int16_t combat_Combat(SceneHeadStruct* head) {
	(void)head;
	return SCENE_MAIN_MENU;
}
// FUNCTION: TIE 0x6E0E0
int16_t blueprnt_Blueprint(SceneHeadStruct* head) {
	(void)head;
	return SCENE_MAIN_MENU;
}
// FUNCTION: TIE 0x71690
int16_t filmview_FilmView(SceneHeadStruct* head) {
	(void)head;
	return SCENE_MAIN_MENU;
}

/* ================================================================
 * In-flight SFX stubs (FSFX — 19 functions, only 2 called by frontend)
 * ================================================================ */

// FUNCTION: TIE 0x247D8
int16_t fsfx_loadsfx(const char* filename) {
	(void)filename;
	return 0;
}
// FUNCTION: TIE 0x24744
void fsfx_freesfx(void) {}

// FUNCTION: TIE 0x25EA4
int8_t fsfx_speakorderack(int32_t target_idx, int32_t order_char, uint16_t cmdr_mode) {
	(void)target_idx;
	(void)order_char;
	(void)cmdr_mode;
	return 0;
}

/* CREATE stub: frontend never loads a mission. MSG_reportfgcreation calls
 * this to resolve FG spawn position; in the frontend it's a no-op since
 * no MSG writer actually fires. */
// FUNCTION: TIE 0x196C0
void create_getworldposition(uint16_t obj_or_kind, int fg_idx) {
	(void)obj_or_kind;
	(void)fg_idx;
}

/* ================================================================
 * Other missing module stubs
 * ================================================================ */

void lbpflight_Load_Flight_Craft(void) {}

/* ================================================================
 * In-flight globals — transfm2/create are excluded from the frontend
 * build but their globals are referenced by shared .c files (logbuf2,
 * goals). Frontend never enters flight/goals rooms so the values are
 * never read.
 * ================================================================ */

/* transfm2.c globals (logbuf2 PIP save/restore) */
// GLOBAL: TIE 0xEC174
// GLOBAL: TIE 0xEC178
// GLOBAL: TIE 0xEC17C
int32_t worldeyeA1, worldeyeA2, worldeyeA3;
// GLOBAL: TIE 0xEC18C
// GLOBAL: TIE 0xEC190
// GLOBAL: TIE 0xEC194
int32_t worldeyeB1, worldeyeB2, worldeyeB3;
// GLOBAL: TIE 0xEC180
// GLOBAL: TIE 0xEC184
// GLOBAL: TIE 0xEC188
int32_t worldeyeC1, worldeyeC2, worldeyeC3;

/* create.c globals (goals.c mission-goals room) */
uint8_t diffmask[4];
uint8_t fgdiffmask[6];
uint8_t genusconvert[9];
uint8_t familyconvert[4];
