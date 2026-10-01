#include "tie/pai.h"
#include "tie/create.h"
#include "tie/fview.h"
#include "tie/math2.h"
#include "tie/math2_wide.h"
#include "tie/paifight.h"
#include "tie/paiman.h"   /* paiman_initmaneuver */
#include "tie/paiorder.h" /* paiorder_waitfor*order */
#include "tie/score.h"
#include "tie/tie.h"
#include "tie/trig2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Owned by tie.c; written by every rough-distance helper below. */

/* ======================================================================
 *                           Module-owned globals
 * ====================================================================== */

/* Single 52-byte AI context block — owner of the per-craft AI tick
 * state. Field layout mirrors the watdbg _ai[52] symbol at 0xF8F48.
 * PAIMAN boarding/dropoff snapshot-restores the entire struct when
 * re-homing craftptr to a docked target craft. */
// GLOBAL: TIE95 0xEB104
AiContext ai;

/* ======================================================================
 *                        Plan bytecode tables
 *
 * Each plan is [wpt_selector, initial_mode, (order_op, next_order)*, 0].
 * Demo symbols place these in paiplan.c; retail layout verified in both IDBs.
 * planptrs[69] references 51 unique plans (multiple slots alias the same
 * byte sequence; aliasing preserved so current_order indexing is
 * bit-identical to the shipped ROM).
 * ====================================================================== */

/* Plan bodies from Z_TIE__.EXE .data at 0xC5D50..0xC6090 (TIE98 0x4E6860..0x4E6C66),
 * in binary layout order. */
// GLOBAL: TIE95 0xC5D50
// GLOBAL: TIE98 0x4E6860
static const uint8_t nullplan[] = { 0x80, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5D53
// GLOBAL: TIE98 0x4E6868
static const uint8_t formflyldr1plan[] = { 0xFC, 0x06, 0x26, 0x2D, 0x27, 0x2D, 0x28, 0x41, 0x2B, 0x41,
										   0x01, 0x03, 0x06, 0x00, 0x0F, 0x2D, 0x16, 0x2D, 0x00 };
// GLOBAL: TIE95 0xC5D66
// GLOBAL: TIE98 0x4E6880
static const uint8_t formflyflw1plan[] = { 0xFC, 0x0A, 0x29, 0x41, 0x01, 0x04, 0x0C, 0x03,
										   0x06, 0x00, 0x0F, 0x2D, 0x13, 0x2E, 0x00 };
// GLOBAL: TIE95 0xC5D75
// GLOBAL: TIE98 0x4E6890
static const uint8_t formflyevadeldr1plan[] = { 0xFC, 0x06, 0x26, 0x2F, 0x27, 0x2F, 0x28, 0x41, 0x2B,
												0x41, 0x24, 0x03, 0x01, 0x03, 0x02, 0x00, 0x03, 0x03,
												0x06, 0x00, 0x0F, 0x2F, 0x16, 0x2F, 0x00 };
// GLOBAL: TIE95 0xC5D8E
// GLOBAL: TIE98 0x4E68B0
static const uint8_t formflyevadeflw1plan[] = { 0xFC, 0x0A, 0x29, 0x41, 0x24, 0x04, 0x01,
												0x04, 0x0C, 0x03, 0x02, 0x00, 0x03, 0x04,
												0x06, 0x00, 0x0F, 0x2F, 0x13, 0x30, 0x00 };
// GLOBAL: TIE95 0xC5DA3
// GLOBAL: TIE98 0x4E68C8
static const uint8_t capldr1plan[] = { 0xFC, 0x06, 0x26, 0x2F, 0x27, 0x2F, 0x28, 0x41, 0x2B, 0x41,
									   0x24, 0x07, 0x01, 0x07, 0x09, 0x0A, 0x2A, 0x41, 0x02, 0x00,
									   0x03, 0x07, 0x06, 0x00, 0x0F, 0x2F, 0x22, 0x0D, 0x00 };
// GLOBAL: TIE95 0xC5DC0
// GLOBAL: TIE98 0x4E68E8
static const uint8_t capldr2plan[] = { 0xFF, 0x0B, 0x26, 0x2F, 0x2B, 0x41, 0x1E, 0x0C, 0x01,
									   0x0A, 0x10, 0x00, 0x0B, 0x07, 0x0A, 0x0B, 0x05, 0x00,
									   0x06, 0x00, 0x0F, 0x2F, 0x22, 0x0D, 0x23, 0x07, 0x00 };
// GLOBAL: TIE95 0xC5DDB
// GLOBAL: TIE98 0x4E6908
static const uint8_t capldr3plan[] = { 0xFF, 0x0C, 0x26, 0x2F, 0x2B, 0x41, 0x24, 0x0B, 0x05,
									   0x00, 0x01, 0x0B, 0x1F, 0x00, 0x0B, 0x07, 0x06, 0x00,
									   0x07, 0x00, 0x0F, 0x2F, 0x22, 0x0D, 0x23, 0x07, 0x00 };
// GLOBAL: TIE95 0xC5DF6
// GLOBAL: TIE98 0x4E6928
static const uint8_t capldr4plan[] = { 0xFF, 0x17, 0x26, 0x2F, 0x2B, 0x41, 0x24, 0x0C, 0x05,
									   0x00, 0x01, 0x0C, 0x1F, 0x00, 0x0B, 0x07, 0x06, 0x00,
									   0x07, 0x00, 0x0F, 0x2F, 0x22, 0x0D, 0x23, 0x07, 0x00 };
// GLOBAL: TIE95 0xC5E11
// GLOBAL: TIE98 0x4E6948
static const uint8_t capldr5plan[] = { 0xFF, 0x1B, 0x01, 0x07, 0x00 };
// GLOBAL: TIE95 0xC5E16
// GLOBAL: TIE98 0x4E6950
static const uint8_t capflw1plan[] = { 0xFC, 0x0A, 0x29, 0x41, 0x24, 0x0E, 0x01, 0x0E, 0x0C,
									   0x07, 0x0D, 0x0F, 0x0E, 0x0F, 0x02, 0x00, 0x03, 0x0E,
									   0x06, 0x00, 0x0F, 0x2F, 0x13, 0x30, 0x22, 0x12, 0x00 };
// GLOBAL: TIE95 0xC5E31
// GLOBAL: TIE98 0x4E6970
static const uint8_t capflw2plan[] = { 0xFF, 0x0B, 0x29, 0x41, 0x1E, 0x0C, 0x01, 0x0F, 0x0B,
									   0x0E, 0x0A, 0x10, 0x02, 0x00, 0x03, 0x0E, 0x06, 0x00,
									   0x0F, 0x2F, 0x22, 0x12, 0x23, 0x0E, 0x00 };
// GLOBAL: TIE95 0xC5E4A
// GLOBAL: TIE98 0x4E6990
static const uint8_t capflw3plan[] = { 0xFF, 0x0C, 0x29, 0x41, 0x24, 0x10, 0x01, 0x0E, 0x0B, 0x0E, 0x05, 0x00,
									   0x06, 0x00, 0x07, 0x00, 0x0F, 0x2F, 0x22, 0x12, 0x23, 0x0E, 0x00 };
// GLOBAL: TIE95 0xC5E61
// GLOBAL: TIE98 0x4E69A8
static const uint8_t capflw4plan[] = { 0xFF, 0x17, 0x29, 0x41, 0x24, 0x11, 0x01, 0x0E, 0x10, 0x00, 0x0B, 0x0E,
									   0x06, 0x00, 0x07, 0x00, 0x0F, 0x2F, 0x22, 0x12, 0x23, 0x0E, 0x00 };
// GLOBAL: TIE95 0xC5E78
// GLOBAL: TIE98 0x4E69C0
static const uint8_t capflw5plan[] = { 0xFF, 0x1B, 0x06, 0x00, 0x01, 0x0E, 0x00 };
// GLOBAL: TIE95 0xC5E7F
// GLOBAL: TIE98 0x4E69C8
static const uint8_t escortldr1plan[] = { 0xFF, 0x11, 0x26, 0x2F, 0x2B, 0x41, 0x24, 0x14, 0x12,
										  0x2F, 0x01, 0x14, 0x17, 0x15, 0x02, 0x00, 0x03, 0x14,
										  0x06, 0x00, 0x0F, 0x2F, 0x22, 0x17, 0x00 };
// GLOBAL: TIE95 0xC5E98
// GLOBAL: TIE98 0x4E69E8
static const uint8_t escortldr2plan[] = { 0xFF, 0x0B, 0x2B, 0x41, 0x01, 0x15, 0x10, 0x00,
										  0x0B, 0x14, 0x0A, 0x16, 0x05, 0x00, 0x06, 0x00,
										  0x0F, 0x2F, 0x22, 0x17, 0x23, 0x14, 0x00 };
// GLOBAL: TIE95 0xC5EAF
// GLOBAL: TIE98 0x4E6A00
static const uint8_t escortldr3plan[] = { 0xFF, 0x0C, 0x2B, 0x41, 0x24, 0x16, 0x05, 0x00, 0x01, 0x16, 0x0B,
										  0x14, 0x06, 0x00, 0x0F, 0x2F, 0x22, 0x17, 0x23, 0x14, 0x00 };
// GLOBAL: TIE95 0xC5EC4
// GLOBAL: TIE98 0x4E6A18
static const uint8_t escortldr4plan[] = { 0xFF, 0x1B, 0x06, 0x00, 0x01, 0x14, 0x00 };
// GLOBAL: TIE95 0xC5ECB
// GLOBAL: TIE98 0x4E6A20
static const uint8_t escortflw1plan[] = { 0xFC, 0x0A, 0x29, 0x41, 0x24, 0x18, 0x01, 0x18, 0x0C,
										  0x14, 0x0D, 0x19, 0x0E, 0x19, 0x02, 0x00, 0x03, 0x18,
										  0x06, 0x00, 0x0F, 0x2F, 0x13, 0x30, 0x22, 0x1B, 0x00 };
// GLOBAL: TIE95 0xC5EE6
// GLOBAL: TIE98 0x4E6A40
static const uint8_t escortflw2plan[] = { 0xFF, 0x0B, 0x29, 0x41, 0x01, 0x19, 0x0B, 0x18,
										  0x0A, 0x1A, 0x02, 0x00, 0x03, 0x18, 0x06, 0x00,
										  0x0F, 0x2F, 0x22, 0x1B, 0x23, 0x18, 0x00 };
// GLOBAL: TIE95 0xC5EFD
// GLOBAL: TIE98 0x4E6A58
static const uint8_t escortflw3plan[] = { 0xFF, 0x0C, 0x29, 0x41, 0x24, 0x1A, 0x01, 0x18, 0x0B, 0x18, 0x05,
										  0x00, 0x06, 0x00, 0x0F, 0x2F, 0x22, 0x1B, 0x23, 0x18, 0x00 };
// GLOBAL: TIE95 0xC5F12
// GLOBAL: TIE98 0x4E6A70
static const uint8_t escortflw4plan[] = { 0xFF, 0x1B, 0x06, 0x00, 0x01, 0x18, 0x00 };
// GLOBAL: TIE95 0xC5F19
// GLOBAL: TIE98 0x4E6A78
static const uint8_t board1plan[] = { 0xFF, 0x19, 0x0F, 0x2F, 0x26, 0x2F, 0x27, 0x2F, 0x28, 0x41, 0x2B,
									  0x41, 0x18, 0x23, 0x2A, 0x41, 0x1F, 0x00, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5F2E
// GLOBAL: TIE98 0x4E6A90
static const uint8_t board2plan[] = { 0xFF, 0x12, 0x0F, 0x2F, 0x2B, 0x41, 0x01,
									  0x24, 0x19, 0x24, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5F3B
// GLOBAL: TIE98 0x4E6AA0
static const uint8_t board3plan[] = { 0xFF, 0x07, 0x0F, 0x2F, 0x26, 0x2F, 0x27, 0x2F, 0x28,
									  0x41, 0x2B, 0x41, 0x18, 0x23, 0x1A, 0x1C, 0x01, 0x00,
									  0x24, 0x24, 0x1F, 0x00, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5F54
// GLOBAL: TIE98 0x4E6AC0
static const uint8_t rendezvous1plan[] = { 0xF9, 0x06, 0x0F, 0x2F, 0x26, 0x2F, 0x24, 0x27, 0x1D,
										   0x28, 0x01, 0x00, 0x1F, 0x00, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5F65
// GLOBAL: TIE98 0x4E6AD8
static const uint8_t rendezvous2plan[] = { 0xFF, 0x13, 0x0F, 0x2F, 0x01, 0x00, 0x1B, 0x00, 0x0C,
										   0x28, 0x27, 0x2F, 0x28, 0x41, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5F76
// GLOBAL: TIE98 0x4E6AF0
static const uint8_t rendezvousflw1plan[] = { 0xFC, 0x0A, 0x0F, 0x2F, 0x0C, 0x27, 0x06, 0x00, 0x26,
											  0x2F, 0x1D, 0x28, 0x01, 0x00, 0x1F, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5F87
// GLOBAL: TIE98 0x4E6B08
static const uint8_t disabledplan[] = { 0xFF, 0x13, 0x1C, 0x00, 0x1B, 0x00, 0x0C,
										0x2A, 0x27, 0x2F, 0x28, 0x41, 0x00 };
// GLOBAL: TIE95 0xC5F94
// GLOBAL: TIE98 0x4E6B18
static const uint8_t waitplan[] = { 0xFF, 0x1D, 0x0F, 0x2F, 0x26, 0x2F, 0x06, 0x00, 0x08,
									0x00, 0x0C, 0x42, 0x27, 0x2F, 0x28, 0x41, 0x00 };
// GLOBAL: TIE95 0xC5FA5
// GLOBAL: TIE98 0x4E6B30
static const uint8_t flyhomeevadeplan[] = { 0xFF, 0x05, 0x14, 0x33, 0x01, 0x2F, 0x04, 0x31, 0x2E, 0x33,
											0x24, 0x2F, 0x1F, 0x00, 0x03, 0x2F, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5FB8
// GLOBAL: TIE98 0x4E6B48
static const uint8_t followhomeevadeplan[] = { 0xFF, 0x0A, 0x01, 0x30, 0x1F, 0x00, 0x03, 0x30,
											   0x14, 0x33, 0x0C, 0x2F, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5FC7
// GLOBAL: TIE98 0x4E6B58
static const uint8_t flyhomeplan[] = { 0xFF, 0x05, 0x14, 0x33, 0x01, 0x2D, 0x24, 0x2D, 0x04, 0x31, 0x00 };
// GLOBAL: TIE95 0xC5FD2
// GLOBAL: TIE98 0x4E6B68
static const uint8_t followhomeplan[] = { 0xFF, 0x0A, 0x0C, 0x2D, 0x01, 0x30, 0x14, 0x33, 0x00 };
// GLOBAL: TIE95 0xC5FDB
// GLOBAL: TIE98 0x4E6B78
static const uint8_t enterhangarplan[] = { 0xFF, 0x14, 0x15, 0x2F, 0x01, 0x31, 0x00 };
/* exithangarplan and outofhyperspaceplan are NOT const: PAIMAN_outofhangar-
 * maneuver and PAIMAN_outofhyperspacemaneuver write the next-order opcode
 * (ordersldr/ordersflw[ai[0].order]) into byte[3] of the plan when the
 * maneuver completes. The plan VM (PAI_updatecraftplan) then reads byte[3]
 * as the transition target. Without the write the plan VM never sees a
 * non-zero next_order byte, current_order stays at 50/52 forever, and the
 * craft is stuck in mode 22/26 — followers never decelerate to formation
 * speed (visible bug in B1M1FM, FG 9 "Onece"). */
// GLOBAL: TIE95 0xC5FE2
// GLOBAL: TIE98 0x4E6B80
uint8_t exithangarplan[] = { 0xFF, 0x1A, 0x01, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5FE7
// GLOBAL: TIE98 0x4E6B88
static const uint8_t intohyperspaceplan[] = { 0xFE, 0x15, 0x01, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5FEC
// GLOBAL: TIE98 0x4E6B90
uint8_t outofhyperspaceplan[] = { 0xFF, 0x16, 0x01, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5FF1
// GLOBAL: TIE98 0x4E6B98
static const uint8_t starshipintohyperplan[] = { 0xFE, 0x15, 0x25, 0x37, 0x01, 0x00, 0x06, 0x00, 0x00 };
// GLOBAL: TIE95 0xC5FFA
// GLOBAL: TIE98 0x4E6BA8
static const uint8_t starshipfollowhomeplan[] = { 0xFF, 0x0A, 0x25, 0x37, 0x0C, 0x35, 0x01, 0x00,
												  0x06, 0x00, 0x08, 0x00, 0x14, 0x35, 0x00 };
// GLOBAL: TIE95 0xC6009
// GLOBAL: TIE98 0x4E6BB8
static const uint8_t starshipstatplan[] = { 0xFF, 0x19, 0x06, 0x00, 0x08, 0x00, 0x00 };
// GLOBAL: TIE95 0xC6010
// GLOBAL: TIE98 0x4E6BC0
static const uint8_t starshipformplan[] = { 0xFC, 0x06, 0x0F, 0x35, 0x26, 0x35, 0x27, 0x35,
											0x28, 0x41, 0x2B, 0x41, 0x01, 0x00, 0x06, 0x00,
											0x07, 0x00, 0x08, 0x00, 0x16, 0x35, 0x00 };
// GLOBAL: TIE95 0xC6027
// GLOBAL: TIE98 0x4E6BD8
static const uint8_t starshipfollowplan[] = { 0xFC, 0x0A, 0x0F, 0x35, 0x29, 0x41, 0x0C, 0x38, 0x01, 0x00,
											  0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x14, 0x35, 0x00 };
// GLOBAL: TIE95 0xC603A
// GLOBAL: TIE98 0x4E6BF0
static const uint8_t starshipwaitreturnplan[] = { 0xFF, 0x19, 0x0F, 0x35, 0x26, 0x35, 0x27, 0x35, 0x28,
												  0x41, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00 };
// GLOBAL: TIE95 0xC604B
// GLOBAL: TIE98 0x4E6C08
static const uint8_t starshipwaitcreateplan[] = { 0xFF, 0x19, 0x0F, 0x35, 0x26, 0x35, 0x27, 0x35, 0x28,
												  0x41, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00 };
// GLOBAL: TIE95 0xC605C
// GLOBAL: TIE98 0x4E6C20
static const uint8_t craftwaitforgoplan[] = { 0xFF, 0x19, 0x02, 0x00, 0x03, 0x2C, 0x00 };
// GLOBAL: TIE95 0xC6063
// GLOBAL: TIE98 0x4E6C28
static const uint8_t starshipwaitforgoplan[] = { 0xFF, 0x19, 0x0F, 0x35, 0x06, 0x00, 0x08, 0x00, 0x00 };
// GLOBAL: TIE95 0xC606C
// GLOBAL: TIE98 0x4E6C38
static const uint8_t repairwaitplan[] = { 0xFF, 0x19, 0x2C, 0x00, 0x00 };
// GLOBAL: TIE95 0xC6071
// GLOBAL: TIE98 0x4E6C40
static const uint8_t dropoffldr1plan[] = { 0xFC, 0x05, 0x26, 0x2D, 0x27, 0x2D, 0x28, 0x41, 0x01,
										   0x00, 0x2D, 0x26, 0x06, 0x00, 0x0F, 0x2D, 0x00 };
// GLOBAL: TIE95 0xC6082
// GLOBAL: TIE98 0x4E6C58
static const uint8_t dropoffldr2plan[] = { 0xFF, 0x1E, 0x26, 0x2D, 0x27, 0x2D, 0x28, 0x41,
										   0x01, 0x26, 0x06, 0x00, 0x0F, 0x2D, 0x00 };

/* 69-slot table indexed by CraftData.current_order. Aliases reproduce the
 * shipped ROM layout (entries 0..2 all point at nullplan, 28..34 + 68 share
 * board1plan, 60..63 share starshipformplan, 65 repeats nullplan). */
// GLOBAL: TIE95 0xC6094
// GLOBAL: TIE98 0x4E6C68
const uint8_t* planptrs[69] = {
	nullplan,
	nullplan,
	nullplan, /* 0..2 */
	formflyldr1plan,
	formflyflw1plan, /* 3..4 */
	formflyevadeldr1plan,
	formflyevadeflw1plan, /* 5..6 */
	capldr1plan,
	capldr1plan,
	capldr1plan, /* 7..9 */
	capldr2plan,
	capldr3plan,
	capldr4plan,
	capldr5plan, /* 10..13 */
	capflw1plan,
	capflw2plan,
	capflw3plan,
	capflw4plan,
	capflw5plan, /* 14..18 */
	capldr1plan, /* 19 */
	escortldr1plan,
	escortldr2plan,
	escortldr3plan,
	escortldr4plan, /* 20..23 */
	escortflw1plan,
	escortflw2plan,
	escortflw3plan,
	escortflw4plan, /* 24..27 */
	board1plan,
	board1plan,
	board1plan,
	board1plan, /* 28..31 */
	board1plan,
	board1plan,
	board1plan, /* 32..34 */
	board2plan,
	board3plan, /* 35..36 */
	dropoffldr1plan,
	dropoffldr2plan, /* 37..38 */
	rendezvous1plan,
	rendezvous2plan,
	rendezvousflw1plan, /* 39..41 */
	disabledplan,
	disabledplan,       /* 42..43 */
	craftwaitforgoplan, /* 44 */
	flyhomeplan,
	followhomeplan,
	flyhomeevadeplan,
	followhomeevadeplan, /* 45..48 */
	enterhangarplan,
	exithangarplan, /* 49..50 */
	intohyperspaceplan,
	outofhyperspaceplan, /* 51..52 */
	starshipintohyperplan,
	starshipfollowhomeplan, /* 53..54 */
	starshipstatplan,
	starshipformplan,
	starshipfollowplan, /* 55..57 */
	starshipwaitreturnplan,
	starshipwaitcreateplan, /* 58..59 */
	starshipformplan,
	starshipformplan,
	starshipformplan,
	starshipformplan,      /* 60..63 */
	starshipwaitforgoplan, /* 64 */
	nullplan,              /* 65 */
	waitplan,
	repairwaitplan, /* 66..67 */
	board1plan,     /* 68 */
};

/* ======================================================================
 *                   Order-handler dispatch table (47 slots)
 *
 * The real handlers live in PAIORDER / PAIFIGHT / PAIMAN. They all take
 * no arguments, read PAI context globals, and return 0 (stay) or non-zero
 * (transition). Stubs in paiorder.c / paifight.c return 0 until the real
 * implementations land. The table itself is owned by pai.c because the
 * plan VM (pai_updatecraftplan) is the sole consumer.
 * ====================================================================== */

/* Forward declarations for every handler referenced in ordersfunctionptrs. */
int16_t paiorder_nullorder(void);
int16_t paiorder_updatecourseorder(void);
int16_t paiorder_underattackorder(void);
int16_t paiorder_stillattackorder(void);
int16_t paiorder_flyhomeorder(void);
int16_t paiorder_waitrunorder(void);
int16_t paiorder_breakofforder(void);
int16_t paiorder_leaderdeadorder(void);
int16_t paiorder_abortatkorder(void);
int16_t paiorder_ontailorder(void);
int16_t paiorder_alwaysorder(void);
int16_t paiorder_leadergohomeorder(void);
int16_t paiorder_hyperspaceorder(void);
int16_t paiorder_enterhangarorder(void);
int16_t paiorder_mothershiporder(void);
int16_t paiorder_lookfordisableorder(void);
int16_t paiorder_abortboardorder(void);
int16_t paiorder_returnboardorder(void);
int16_t paiorder_awaitboardorder(void);
int16_t paiorder_makedisabledorder(void);
int16_t paiorder_neartargetorder(void);
int16_t paiorder_rocketsonboardorder(void);
int16_t paiorder_avoidhitorder(void);
int16_t paiorder_evasiveorder(void);
int16_t paiorder_newtargetorder(void);
int16_t paiorder_avoidstarshiporder(void);
int16_t paiorder_checkhyperorder(void);
int16_t paiorder_stopgohomeorder(void);
int16_t paiorder_completegohomeorder(void);
int16_t paiorder_completegootherorder(void);
int16_t paiorder_completefolloworder(void);
int16_t paiorder_waitgootherorder(void);
int16_t paiorder_orderswitchorder(void);
int16_t paiorder_dropoffdestorder(void);
int16_t paiorder_mothershipreadyorder(void);

// GLOBAL: TIE95 0xC5C94
OrderFunc ordersfunctionptrs[47] = {
	paiorder_nullorder,              /*  0 */
	paiorder_updatecourseorder,      /*  1 */
	paiorder_underattackorder,       /*  2 */
	paiorder_stillattackorder,       /*  3 */
	paiorder_flyhomeorder,           /*  4 */
	paifight_fightershootorder,      /*  5 */
	paifight_gunnerselfdefenseorder, /*  6 */
	paifight_gunneroffenseorder,     /*  7 */
	paifight_missiledefenseorder,    /*  8 */
	paifight_scanfortargetorder,     /*  9 */
	paiorder_waitrunorder,           /* 10 */
	paiorder_breakofforder,          /* 11 */
	paiorder_leaderdeadorder,        /* 12 */
	paifight_coverleaderorder,       /* 13 */
	paifight_followleadatkorder,     /* 14 */
	paiorder_abortatkorder,          /* 15 */
	paiorder_ontailorder,            /* 16 */
	paiorder_alwaysorder,            /* 17 */
	paifight_checkescortorder,       /* 18 */
	paiorder_leadergohomeorder,      /* 19 */
	paiorder_hyperspaceorder,        /* 20 */
	paiorder_enterhangarorder,       /* 21 */
	paiorder_mothershiporder,        /* 22 */
	paifight_escorttargetorder,      /* 23 */
	paiorder_lookfordisableorder,    /* 24 */
	paiorder_abortboardorder,        /* 25 */
	paiorder_returnboardorder,       /* 26 */
	paiorder_awaitboardorder,        /* 27 */
	paiorder_makedisabledorder,      /* 28 */
	paiorder_neartargetorder,        /* 29 */
	paiorder_rocketsonboardorder,    /* 30 */
	paiorder_avoidhitorder,          /* 31 */
	paiorder_waitforkidsorder,       /* 32 */
	paiorder_waitforallcreateorder,  /* 33 */
	paiorder_evasiveorder,           /* 34 */
	paiorder_newtargetorder,         /* 35 */
	paiorder_avoidstarshiporder,     /* 36 */
	paiorder_checkhyperorder,        /* 37 */
	paiorder_stopgohomeorder,        /* 38 */
	paiorder_completegohomeorder,    /* 39 */
	paiorder_completegootherorder,   /* 40 */
	paiorder_completefolloworder,    /* 41 */
	paiorder_waitgootherorder,       /* 42 */
	paiorder_orderswitchorder,       /* 43 */
	paiorder_nullorder,              /* 44: retail no-op used by repairwaitplan */
	paiorder_dropoffdestorder,       /* 45 */
	paiorder_mothershipreadyorder,   /* 46 (retail sub_3F934) */
};

/* ======================================================================
 *                            Leaf helpers
 * ====================================================================== */

/* Skill roll tier (0/1/2). Unreferenced in the shipped binary. */
// FUNCTION: TIE95 0x35A34
// FUNCTION: TIE98 0x459460
int pai_getprof(uint16_t skill) {
	if (skill < 0x8000)
		return 0;
	if (skill < 0xC000)
		return 1;
	return 2;
}

/* Unused getter — hull_max of objects[obj_idx]'s craft. */
// FUNCTION: TIE95 0x36258
uint16_t pai_getcraftdoomedlevel(uint16_t obj_idx) { return objects[obj_idx].craft_ptr->hull_max; }

/* ======================================================================
 *                       Distance / proximity helpers
 * ====================================================================== */

// FUNCTION: TIE95 0x36048
void pai_distancebetween(uint16_t a_ref, uint16_t b_ref) {
	int32_t bx, by, bz;
	create_getworldposition(b_ref, 0);
	bx = worldlocx;
	by = worldlocy;
	bz = worldlocz;
	create_getworldposition(a_ref, 0);
	trig2_ctop(bx - worldlocx, by - worldlocy, bz - worldlocz);
}

// FUNCTION: TIE95 0x360A0
void pai_roughdistancebetween(uint16_t a_ref, uint16_t b_ref) {
	int32_t ax, ay, az;
	int32_t dx, dy, dz;
	int32_t xy_sum;

	create_getworldposition(a_ref, 0);
	ax = worldlocx;
	ay = worldlocy;
	az = worldlocz;
	create_getworldposition(b_ref, 0);
	dx = ax - worldlocx;
	dy = ay - worldlocy;
	dz = az - worldlocz;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (dz < 0)
		dz = -dz;

	/* Chebyshev-weighted Manhattan: halve the smaller of (dx, dy), sum,
	 * then halve the smaller of that sum and dz. */
	if (dx <= dy)
		dx >>= 1;
	else
		dy >>= 1;
	xy_sum = dx + dy;
	if (xy_sum <= dz)
		xy_sum >>= 1;
	else
		dz >>= 1;
	roughdistance = dz + xy_sum;
}

// FUNCTION: TIE95 0x3627C
void pai_targetdistance(void) {
	trig2_ctop(craftptr->waypoint_x_cache - objects[ai.active_obj_idx].world_x,
			   craftptr->waypoint_y_cache - objects[ai.active_obj_idx].world_y,
			   craftptr->waypoint_z_cache - objects[ai.active_obj_idx].world_z);
}

// FUNCTION: TIE95 0x35E9C
int16_t pai_roughproximitycheck(uint16_t obj_ref, int32_t radius_24_8) {
	int32_t tx, ty, tz;
	int32_t dx, dy, dz;
	int32_t xy_sum;

	if (obj_ref >= 0x3800u) {
		/* Static slot: 16-bit world coords scaled up by <<8. Bound the
		 * range — pai_checkcombatarea reaches here without first filtering
		 * via pai_worthytarget, so waypoint refs would OOB. */
		uint32_t static_idx = (uint32_t)obj_ref - 0x3800u;
		const StaticObject* s;

		if (static_idx >= NUM_STATIC_OBJECTS)
			return 0;
		s = &staticobjects[static_idx];
		tx = (int32_t)s->world_x << 8;
		ty = (int32_t)s->world_y << 8;
		tz = (int32_t)s->world_z << 8;
	} else {
		/* Flight slot: world coords already 24.8. */
		tx = objects[obj_ref].world_x;
		ty = objects[obj_ref].world_y;
		tz = objects[obj_ref].world_z;
	}
	dx = ai.world_x - tx;
	dy = ai.world_y - ty;
	dz = ai.world_z - tz;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (dz < 0)
		dz = -dz;
	if (dx <= dy)
		dx >>= 1;
	else
		dy >>= 1;
	xy_sum = dx + dy;
	if (xy_sum <= dz)
		xy_sum >>= 1;
	else
		dz >>= 1;
	roughdistance = dz + xy_sum;
	return (radius_24_8 > roughdistance) ? 1 : 0;
}

// FUNCTION: TIE95 0x35C34
char pai_checkcombatarea(uint16_t obj_ref) {
	/* Skill-tiered combat-zone radius:
	 *   tier 0 -> 2560    tier 1 -> 2880    tier 2 -> 3200    tier 3 -> 3520 */
	uint16_t skill_bonus = math2_fraction(0x500u, skilltranslate[(uint16_t)ai.skill_tier]);
	int32_t radius = ((int32_t)skill_bonus + 2560) << 8;
	return (pai_roughproximitycheck(obj_ref, radius) == 1) ? 1 : 0;
}

/* ======================================================================
 *                        Target eligibility filters
 * ====================================================================== */

// FUNCTION: TIE95 0x35B3C
int16_t pai_worthytarget(uint16_t obj_ref) {
	int idx = obj_ref;

	if (idx == 0xFF || idx == 0xFFFF)
		return 0;
	if (idx < 0x3800) {
		CraftData* c;
		uint16_t flag;

		if (!objects[idx].ship_idx)
			return 0;
		if (idx < NUM_CRAFTS) {
			c = objects[idx].craft_ptr;
			/* Hyperspacing in/out. */
			if (c->mode_byte == 21 && c->mode_subbyte)
				return 0;
			flag = c->flight_flag;
			/* Binary quirk: hull_damage vs hull_max test dereferences the
			 * SAME craft twice (craftptr vs objects[a1].craft_ptr); treated as
			 * a no-op "never-trigger" filter on healthy ships. Preserved for
			 * behavioural parity. */
			if (flag == 1 || flag == 3 || flag == 4)
				return 0;
			if (obj_ref == pstate.object_idx || objects[obj_ref].craft_ptr->hull_max >= c->hull_damage)
				return 1;
			return 0;
		}
		return 1;
	} else {
#ifdef TIE_MODERN
		/* Static slots: bound the index — PAI_initplan writes waypoint refs
		 * of the form (0x80 << 8) | active_waypoint_idx into ai_target_ref
		 * for every AI craft, and paiorder_breakofforder/lookfordisableorder
		 * read it back and pass it here. The binary lacks this bound and
		 * reads staticobjects out of range. */
		if (idx - 0x3800 >= NUM_STATIC_OBJECTS)
			return 0;
#endif
		/* Static slots: species != 0 means the slot is occupied. */
		if (!staticobjects[idx - 0x3800].species)
			return 0;
		return 1;
	}
}

// FUNCTION: TIE95 0x35ABC
int16_t pai_checktargetforattack(uint16_t attacker_ref, uint16_t obj_ref, int16_t pursue_hot) {
	int32_t radius;

	if (pai_worthytarget(obj_ref)) {
		radius = (int32_t)math2_fraction(0x500u, skilltranslate[(uint16_t)ai.skill_tier]) + 2560;
		if (pursue_hot) {
			/* +1/3 radius extension for aggressive pursuit (0x5555 ≈ 1/3 of 0x10000). */
			radius += math2_fraction((uint16_t)radius, 0x5555u);
		}
		if (pai_roughproximitycheck(obj_ref, radius << 8) == 1)
			return 1;
	}
	return 0;
}

// FUNCTION: TIE95 0x368A8
int16_t pai_isobjectvalidtarget(uint16_t obj_ref) {
	int16_t in_pri;
	int16_t in_sec;
	int16_t goal_match;
	int16_t in_t0;
	int16_t in_t1;
	int16_t target_match;

	in_pri = score_objectmemberofgroup(obj_ref, (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_type,
									   (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_id);
	in_sec = score_objectmemberofgroup(obj_ref, (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].sec_type,
									   (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].sec_id);
	if ((int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_sec_op == 1)
		goal_match = in_pri | in_sec;
	else
		goal_match = in_pri & in_sec;

	in_t0 = score_objectmemberofgroup(obj_ref, (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_type[0],
									   (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_id[0]);
	in_t1 = score_objectmemberofgroup(obj_ref, (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_type[1],
									   (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_id[1]);
	if ((int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_op == 1)
		target_match = in_t0 | in_t1;
	else
		target_match = in_t0 & in_t1;

	if (goal_match || target_match)
		return 1;
	return 0;
}

/* ======================================================================
 *                          Group / FG scans
 * ====================================================================== */

// FUNCTION: TIE95 0x35A58
uint16_t pai_searchformother(uint16_t fg_idx) {
	uint16_t i;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		int ff;

		if (!objects[i].ship_idx)
			continue;
		/* Skip motherships that are themselves departing or hyperspaced
		 * (flight_flag 3 = leaving, 4 = gone). */
		ff = objects[i].craft_ptr->flight_flag;
		if (ff == 4 || ff == 3)
			continue;
		if (objects[i].fg_idx != fg_idx)
			continue;
		if (objects[i].craft_ptr->leader_obj_idx == 0xFFu)
			return i;
	}
	return 0xFFFFu;
}

// FUNCTION: TIE95 0x35C80
int pai_searchforcraftingroup(uint8_t group_type1, uint16_t group_id1, int16_t combine_op,
							  uint8_t group_type2, uint16_t group_id2) {
	/* Flight objects. */
	uint16_t i;
	uint16_t j;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		int in1;
		int in2;
		int miss;

		if (!objects[i].ship_idx)
			continue;
		in1 = score_objectmemberofgroup(i, group_type1, group_id1);
		in2 = score_objectmemberofgroup(i, group_type2, group_id2);
		miss = (combine_op == 1) ? (!in1 && !in2) : (!in1 || !in2);
		if (miss)
			continue;
		if (ai.live_target_only && !objects[i].craft_ptr->status_flags)
			continue;
		return 1;
	}
	/* Static slots. */
	for (j = 0; j < 0x40u; ++j) {
		int in1;
		int in2;
		int miss;

		if (!staticobjects[j].species)
			continue;
		in1 = score_objectmemberofgroup(j, group_type1, group_id1);
		in2 = score_objectmemberofgroup(j, group_type2, group_id2);
		miss = (combine_op == 1) ? (!in1 && !in2) : (!in1 || !in2);
		if (miss)
			continue;
		if (ai.live_target_only && !staticobjects[j].status_flags)
			continue;
		return 1;
	}
	return 0;
}

// FUNCTION: TIE95 0x35DB8
int16_t pai_lookfordisableswitch(uint16_t fg_idx) {
	return pai_checkfortargetstodisable(fg_idx) != 0xFFFFu;
}

// FUNCTION: TIE95 0x35DD4
uint16_t pai_checkfortargetstodisable(uint16_t ai_entry) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai_entry];

	/* Pass 1: goal selectors (pri/sec) combined by pri_sec_op. */
	uint16_t r = pai_finddisabledingroup(cur_ai->pri_type, cur_ai->pri_id, (int16_t)cur_ai->pri_sec_op,
										 cur_ai->sec_type, cur_ai->sec_id);
	if (r != 0xFFFFu)
		return r;

	/* Pass 2: explicit target_type/target_id pair combined by target_op. */
	return pai_finddisabledingroup(cur_ai->target_type[0], cur_ai->target_id[0], (int16_t)cur_ai->target_op,
								   cur_ai->target_type[1], cur_ai->target_id[1]);
}

// FUNCTION: TIE95 0x362D4
uint16_t pai_finddisabledingroup(uint16_t group_type1, uint16_t group_id1, uint16_t combine_op,
								 uint16_t group_type2, uint16_t group_id2) {
	uint16_t b_ref;
	uint16_t i;
	uint16_t best_obj = 0xFFFFu;
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t sj;

	/* Flight objects: eligibility + closest-available. */
	for (i = 0; i < NUM_CRAFTS; ++i) {
		int16_t in1;
		int16_t in2;
		int16_t in_group;
		CraftData* c;
		int16_t eligible;
		int16_t taken;
		uint16_t k;
		uint16_t j;

		if (!objects[i].ship_idx)
			continue;

		in1 = score_objectmemberofgroup(i, group_type1, group_id1);
		in2 = score_objectmemberofgroup(i, group_type2, group_id2);
		if (combine_op == 1)
			in_group = in1 | in2;
		else
			in_group = in1 & in2;
		if (!in_group)
			continue;

		c = objects[i].craft_ptr;
		eligible = 0;
		if (c->default_order_ldr <= 2 || objects[i].genus == GENUS_PLATFORM) {
			eligible = 1;
		} else if (craftptr->default_order_ldr == 31 || craftptr->default_order_ldr == 32) {
			if (!c->status_flags)
				eligible = 1;
		} else if (!c->status_flags || c->mode_byte == 19 || c->mode_byte == 25) {
			eligible = 1;
		}
		if (!eligible)
			continue;

		/* Skip if another active craft already targets this object or if
		 * we've already captured it. */
		taken = 0;
		for (k = 0; k < NUM_CRAFTS; ++k) {
			CraftData* oc;

			if (!objects[k].ship_idx || k == ai.active_obj_idx)
				continue;
			oc = objects[k].craft_ptr;
			if ((oc->default_order_ldr >= 0x1C && oc->default_order_ldr <= 0x22) || oc->default_order_ldr == 68) {
				if ((uint16_t)oc->ai_target_ref == i)
					++taken;
			}
		}
		for (j = 0; j < craftptr->capture_count; ++j) {
			if (craftptr->capture_list[j] == objects[i].idnumber)
				++taken;
		}
		if (taken)
			continue;

		pai_roughdistancebetween(ai.active_obj_idx, i);
		if ((uint32_t)roughdistance < best_dist) {
			best_dist = (uint32_t)roughdistance;
			best_obj = i;
		}
	}

	/* Static slots (0x3800 + sj). */
	for (sj = 0, b_ref = 0x3800u; sj < 0x40; ++b_ref, ++sj) {
		uint16_t sp;
		int16_t in1;
		int16_t in2;
		int16_t in_group;
		int16_t taken;
		uint16_t k;
		uint16_t m;

		sp = staticobjects[sj].species;
		if (!sp)
			continue;
		if (!(species_table[sp].side & 2))
			continue;
		in1 = score_fgmemberofgroup(staticobjects[sj].fg_idx, group_type1, group_id1);
		in2 = score_fgmemberofgroup(staticobjects[sj].fg_idx, group_type2, group_id2);
		if (combine_op == 1)
			in_group = in1 | in2;
		else
			in_group = in1 & in2;
		if (!in_group)
			continue;

		/* Exclusion scan (by flight-object ai_target_ref, re-indexed through
		 * objects[] to match the binary's loop which mis-guards with
		 * staticobjects[k].species but dereferences objects[k]). */
		taken = 0;
		for (k = 0; k < NUM_CRAFTS; ++k) {
			CraftData* oc;

			if (!staticobjects[k].species || k == ai.active_obj_idx)
				continue;
			oc = objects[k].craft_ptr;
			if ((oc->default_order_ldr >= 0x1C && oc->default_order_ldr <= 0x20) || oc->default_order_ldr == 34 ||
				oc->default_order_ldr == 68) {
				if ((uint16_t)oc->ai_target_ref == sj + 0x3800)
					++taken;
			}
		}
		for (m = 0; m < craftptr->capture_count; ++m) {
			if (staticobjects[sj].idnumber == craftptr->capture_list[m])
				++taken;
		}
		if (taken)
			continue;

		pai_roughdistancebetween(ai.active_obj_idx, b_ref);
		if ((uint32_t)roughdistance < best_dist) {
			best_dist = (uint32_t)roughdistance;
			best_obj = b_ref;
		}
	}

	return best_obj;
}

/* ======================================================================
 *                      AI context + target / formation
 * ====================================================================== */

// FUNCTION: TIE95 0x35F7C
void pai_settarget(void) {
	uint16_t obj_idx = ai.active_obj_idx;

	create_getworldposition((uint16_t)craftptr->ai_target_ref, objects[obj_idx].fg_idx);
	craftptr->waypoint_x_cache = worldlocx;
	craftptr->waypoint_y_cache = worldlocy;
	craftptr->waypoint_z_cache = worldlocz;
}

// FUNCTION: TIE95 0x35FD4
void pai_setformation(uint16_t leader_obj_idx, uint8_t formation, uint8_t separation) {
	/* Leader receives the intended (formation, separation). */
	uint16_t i;

	ai.leader_craft->formation = formation;
	ai.leader_craft->formation_separation = separation;

	/* Followers in the same FG inherit. Binary quirk: the inner loop
	 * writes craft->formation = craft->leader_obj_idx (i.e. the compared
	 * byte still in BL), NOT the formation code. The shipped function has
	 * no callers, so the bug is preserved verbatim for parity. */
	for (i = 0; i < NUM_CRAFTS; ++i) {
		CraftData* c;
		uint8_t other_leader;

		if (!objects[i].ship_idx)
			continue;
		c = objects[i].craft_ptr;
		other_leader = c->leader_obj_idx;
		if (other_leader != (uint8_t)leader_obj_idx)
			continue;
		c->formation = other_leader; /* Watcom quirk, not (formation) */
		c->formation_separation = separation;
	}
}

// FUNCTION: TIE95 0x36134
void pai_calcrotatedpoint(FlightObject* obj, int16_t side_arg, int16_t up_arg, int16_t fwd_arg) {
	/* Refresh the local-frame basis from heading/pitch/roll if dirty. */
	if (obj->orient_dirty) {
		fview_calcrotatemove(obj->pitch, obj->heading, obj);
		fview_calcrotateorient(obj->roll, 0, obj);
	}

	/* Rotate (side_arg, up_arg, fwd_arg) by the 3x3 orientation basis.
	 * Each product is shifted >>15 *before* summing (matches retail's
	 * Watcom emit: three independent arithmetic shifts then add). Sum-
	 * before-shift accumulates rounding loss for negative products. */
	rotatedx = (((int32_t)obj->side_x * side_arg) >> 15) + (((int32_t)obj->up_x * up_arg) >> 15) +
			   (((int32_t)obj->fwd_x * fwd_arg) >> 15);
	rotatedy = (((int32_t)obj->side_y * side_arg) >> 15) + (((int32_t)obj->up_y * up_arg) >> 15) +
			   (((int32_t)obj->fwd_y * fwd_arg) >> 15);
	rotatedz = (((int32_t)obj->side_z * side_arg) >> 15) + (((int32_t)obj->up_z * up_arg) >> 15) +
			   (((int32_t)obj->fwd_z * fwd_arg) >> 15);
}

// FUNCTION: TIE98 0x45A3C0
int32_t pai_RotateLocalVectorToWorldScratch(FlightObject* obj, int side_arg, int up_arg, int fwd_arg) {
	if (obj->orient_dirty) {
		fview_calcrotatemove(obj->pitch, obj->heading, obj);
		fview_calcrotateorient(obj->roll, 0, obj);
	}
	rotatedx = math2_mul_q15(obj->side_x, side_arg);
	rotatedx += math2_mul_q15(obj->up_x, up_arg);
	rotatedx += math2_mul_q15(obj->fwd_x, fwd_arg);
	rotatedy = math2_mul_q15(obj->side_y, side_arg);
	rotatedy += math2_mul_q15(obj->up_y, up_arg);
	rotatedy += math2_mul_q15(obj->fwd_y, fwd_arg);
	rotatedz = math2_mul_q15(obj->side_z, side_arg);
	rotatedz += math2_mul_q15(obj->up_z, up_arg);
	rotatedz += math2_mul_q15(obj->fwd_z, fwd_arg);
	return rotatedz;
}

/* ======================================================================
 *                        Per-craft context cache
 * ====================================================================== */

// FUNCTION: TIE95 0x3591C
uint8_t* pai_setupcraftaivars(uint16_t obj_idx) {
	uint16_t co;
	const uint8_t* plan;

	ai.active_obj_idx = obj_idx;
	ai.active_craft = objects[obj_idx].craft_ptr;
	ai.leader_obj_idx = ai.active_craft->leader_obj_idx;
	/* leader_obj_idx == 0xFF means "self is leader / no leader". The
	 * retail binary unconditionally read objects[0xFF].craft_ptr here
	 * -- a 135-slot OOB read into adjacent DOS BSS -- and stored the
	 * resulting wild pointer into ai.leader_craft. It worked only
	 * because every consumer gates on the sentinel before
	 * dereferencing leader_craft. We fall back to active_craft so any
	 * unguarded access reads self's data instead of triggering UB. */
	ai.leader_craft = (ai.leader_obj_idx == 0xFF) ? ai.active_craft : objects[ai.leader_obj_idx].craft_ptr;
	ai.fg_idx = objects[obj_idx].fg_idx;
	ai.ai_entry_count = ai.active_craft->ai_state_1C;

	create_getworldposition(obj_idx, ai.fg_idx);
	ai.world_x = worldlocx;
	ai.world_y = worldlocy;
	ai.world_z = worldlocz;

	/* Skill tier from craft_ptr->skill_value thresholds. */
	{
		uint16_t sv = ai.active_craft->skill_value;
		if (sv < 0x8000u)
			ai.skill_tier = 0;
		else if (sv < 0xC000u)
			ai.skill_tier = 1;
		else
			ai.skill_tier = 2;
	}

	/* Plan header: [waypoint_selector, order_tag, body...]. order_tag
	 * (stored in ai.plan_order) is the CraftData.mode_byte value the
	 * plan implements; handlers such as paifight_scanfortargetorder
	 * bail when the craft drifts to a different mode. The body pointer
	 * skips past both header bytes. */
	co = ai.active_craft->current_order;
	if (co >= 69u) {
		TieDiagnostics_Log(TIE_LOG_INFO,
						   "[pai] setupcraftaivars: current_order=%u out of range for obj=%u "
						   "(defaulting to nullplan)\n",
						   (unsigned)co, (unsigned)obj_idx);
		co = 0;
		ai.active_craft->current_order = 0;
	}
	plan = planptrs[co];
	ai.plan_order = (uint16_t)plan[1];
	ai.plan_ptr = (uint8_t*)(plan + 2);

	ai.live_target_only = 0;
	ai.staged_next_order = 0;

	return ai.plan_ptr;
}

/* ======================================================================
 *                           Plan init + VM step
 * ====================================================================== */

// FUNCTION: TIE95 0x356B0
void pai_initplan(uint16_t obj_idx) {
	const uint8_t* plan;
	uint16_t wpt_sel;
	uint8_t init_mode;
	unsigned target_ref;

#ifdef TIE_MODERN
	if (craftptr->current_order >= 69u) {
		TieDiagnostics_Log(TIE_LOG_INFO,
						   "[pai] initplan: current_order=%u out of range (defaulting to nullplan)\n",
						   (unsigned)craftptr->current_order);
		craftptr->current_order = 0;
	}
#endif
	plan = planptrs[craftptr->current_order];
	wpt_sel = *plan++;

	/* 0xFF skips waypoint initialization but still starts the plan's
	 * maneuver and resets its runtime state below. */
	if (wpt_sel != 0xFF) {
		if (wpt_sel == 0xFD) {
			/* home waypoint, guarded by way_used[12]. */
			if (fg_array[objects[ai.active_obj_idx].fg_idx].way_used[12])
				craftptr->ai_target_ref = (int16_t)0x800C;
			else
				craftptr->ai_target_ref = (int16_t)0x8000;
		} else if (wpt_sel == 0xFE) {
			/* hyper waypoint, guarded by way_used[13]. */
			if (fg_array[objects[ai.active_obj_idx].fg_idx].way_used[13])
				craftptr->ai_target_ref = (int16_t)0x800D;
			else
				craftptr->ai_target_ref = (int16_t)0x8000;
		} else if (wpt_sel == 0xF9) {
			/* unconditional home-waypoint. */
			craftptr->ai_target_ref = (int16_t)0x800C;
		} else {
			/* Use this craft's current active_waypoint_idx if the FG has one. */
			if (fg_array[objects[ai.active_obj_idx].fg_idx].way_used[craftptr->active_waypoint_idx])
				craftptr->ai_target_ref = (uint16_t)(craftptr->active_waypoint_idx + 0x8000);
			else
				craftptr->ai_target_ref = (int16_t)0x8000;
		}

		/* Resolve the waypoint to a world position for the current tick. */
		target_ref = (uint16_t)craftptr->ai_target_ref;
		if (target_ref != 0xFFu) {
			create_getworldposition(target_ref, objects[ai.active_obj_idx].fg_idx);
			craftptr->waypoint_x_cache = worldlocx;
			craftptr->waypoint_y_cache = worldlocy;
			craftptr->waypoint_z_cache = worldlocz;
		}
	}

	craftptr->ai_plan_state = 0;
	init_mode = *plan;
	if (init_mode != 0xFF) {
		craftptr->mode_byte = init_mode;
		paiman_initmaneuver();
	}
	craftptr->attacker_idx = 0xFFu;
	craftptr->ai_update_rate_copy = craftptr->ai_update_rate;
}

// FUNCTION: TIE95 0x35870
void pai_updatecraftplan(void) {
	uint8_t opcode;

	/* Player-craft escort override: re-evaluate escort targets before
	 * running the handler loop. */
	if (ai.active_obj_idx == pstate.object_idx && craftptr->default_order_ldr == 20) {
		paifight_checkescortorder();
	}

	while ((opcode = *ai.plan_ptr++) != 0) {
		OrderFunc handler = ordersfunctionptrs[opcode];

		if (handler() && *ai.plan_ptr) {
			/* 0x41 is the wildcard next-order = ai.staged_next_order. */
			if (*ai.plan_ptr == 0x41)
				craftptr->current_order = ai.staged_next_order;
			else
				craftptr->current_order = *ai.plan_ptr;
			pai_setupcraftaivars(ai.active_obj_idx);
			pai_initplan(ai.active_obj_idx);
			TIE_FLIGHT_TRACE_AI_TRANSITION(opcode);
			return;
		}
		/* No transition: skip the next_order byte. */
		++ai.plan_ptr;
	}
}

/* ======================================================================
 *                        Top-level per-frame tick
 * ====================================================================== */

// FUNCTION: TIE95 0x35640
void pai_updateplaneai(void) {
	uint16_t i;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		CraftData* c;

		if (!objects[i].ship_idx)
			continue;
		if (objects[i].category)
			continue; /* debris / ember / etc. */

		c = objects[i].craft_ptr;
		/* Set the module-scope craftptr so order handlers see the right
		 * context. pai_setupcraftaivars below overwrites the rest. */
		craftptr = c;

		/* Skip craft that are docking / destroyed. */
		if (c->flight_flag == 3 || c->flight_flag == 4)
			continue;

		/* Skill-paced: only advance when the per-craft countdown expires.
		 * The countdown is signed; <= 0 means "time to run the tick". */
		if ((int16_t)c->ai_update_rate_copy > 0)
			continue;

		pai_setupcraftaivars(i);
		TIE_FLIGHT_TRACE_AI_BEFORE(i);
		pai_updatecraftplan();
		craftptr->ai_update_rate_copy += craftptr->ai_update_rate;
		TIE_FLIGHT_TRACE_AI_AFTER(i);
	}
}

/* ======================================================================
 *                       Order-completion evaluator
 * ====================================================================== */

// FUNCTION: TIE95 0x36654
int pai_aicompletioncheck(uint16_t order_code, uint16_t ai_entry) {
	int done = 0;

	/* Compare the per-AI-entry goal counter (CraftData +0x20..+0x22)
	 * against the signed threshold bytes of the AI record. */
	switch (order_code) {
		case 3:
		case 5:
		case 0x27: /* 39 */
		case 0x2A: /* 42 */
		case 0x2B: /* 43 */
		case 0x38: /* 56 */
			if (craftptr->ai_goal_progress[ai_entry] >=
				(int8_t)fg_array[ai.fg_idx].ai[ai_entry].var[0])
				done = 1;
			break;

		/* Target scan: no future targets and all current targets gone. */
		case 7:
		case 8:
		case 19:
		case 0x3E: /* 62 */
		case 0x3F: /* 63 */
			if (!paifight_scanfortargetsallgone(ai_entry) &&
				!paifight_checkforfuturetargets(ai_entry))
				done = 1;
			break;

		case 28:
		case 29:
		case 30:
		case 31:
		case 32:
		case 33:
		case 0x22: /* 34 */
		case 68:
			if (craftptr->ai_goal_progress[ai_entry] >=
				(int8_t)fg_array[ai.fg_idx].ai[ai_entry].var[1])
				done = 1;
			break;

		/* Counter reaches the referenced flight group's status count. */
		case 37:
			if (craftptr->ai_goal_progress[ai_entry] >=
				fgstatus[(uint16_t)((int8_t)fg_array[ai.fg_idx].ai[ai_entry].var[1] - 1)].cond[0].count)
				done = 1;
			break;

		/* Order 66: maneuver-timer expiry. */
		case 0x42:
			if (craftptr->maneuver_timer == 0)
				done = 1;
			break;

		/* Order 58: wait-for-kids subroutine. */
		case 58:
			if (paiorder_waitforkidsorder())
				done = 1;
			break;

		/* Order 59: wait-for-all-create subroutine. */
		case 0x3B:
			if (paiorder_waitforallcreateorder())
				done = 1;
			break;
	}
	return done;
}
