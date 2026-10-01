#include "tie/mfscript.h"
#include "tie_runtime/audio/imuse_api.h"
#include "tie_runtime/audio/imuse_session.h"

#include <imuse/filelist.h>
#include <imuse/lolevel.h>
#include <string.h>

/* Interactive music transition opcodes:
 *   1 = ChgXfade (crossfade between sounds)
 *   2 = ChgJumpMrk (jump at MIDI marker)
 *   4 = ChgXfade + volume reset trigger
 *   5 = Resume (restore volume param)
 *   6 = ChgJumpMrk + second hook
 *   7 = ChgJumpOnBeat + second hook
 */

enum {
	NUM_STATES = 23,
	NUM_SOUND_NAMES = 39,
	MAX_STATE_CHANGES = 7,
	MAX_SEQ_CHANGES = 2,
};

enum {
	IM_PARAM_VOLUME = 0x200,
	IM_PARAM_VOLALT = 0x600,
	IM_PARAM_CHUNK = 0xB00,
	IM_PARAM_MEASURE = 0xC00,
	IM_PARAM_TICK = 0xE00,
	IM_PARAM_ATTR = 0xF00,
};

// GLOBAL: TIE95 0xD1880
// GLOBAL: TIE98 0x4E4D70
static char soundNames[NUM_SOUND_NAMES][9] = {
	"",         "drone",    "POINK",    "title",    "tocity",   "battle",   "stately", "bridge",
	"briefmap", "secret",   "launch",   "awe",      "register", "mainmen",  "emperor", "phew",
	"trainpod", "fightpod", "evilmonk", "fightmap", "medals",   "tieshow",  "harkspy", "harktalk",
	"harkkill", "thrawny",  "starlog",  "ceremony", "medical",  "funeral",  "bweapon", "bummer",
	"perelogo", "2battle",  "2thrawny", "2emperor", "empshort", "cloaktst", "kablam"
};

// GLOBAL: TIE95 0xD19E0
// GLOBAL: TIE98 0x4E4ED0
static StateRef stateRefs[NUM_STATES] = {
	/* [ 0] */ { 0,
				 0,
				 { { 0, 1, 0, 0, 0, 0 },
				   { 0, 0, 0, 0, 0, 0 },
				   { 0, 0, 0, 0, 0, 0 },
				   { 0, 0, 0, 0, 0, 0 },
				   { 0, 0, 0, 0, 0, 0 },
				   { 0, 0, 0, 0, 0, 0 },
				   { 0, 0, 0, 0, 0, 0 } },
				 { { 0, 1, 0, 0, 0, 0 }, { 0, 4, 0, 0, 0, 0 } } },
	/* [ 1] */
	{ 0,
	  12,
	  { { 2, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [ 2] */
	{ 0,
	  13,
	  { { 5, 1, 0, 0, 0, 0 },
		{ 12, 6, 1, 1, 3, 0 },
		{ 8, 1, 0, 0, 0, 0 },
		{ 13, 1, 0, 0, 0, 0 },
		{ 3, 2, 1, 1, 0, 0 },
		{ 4, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 2, 7, 3, 1, 0, 0 }, { 3, 7, 3, 1, 0, 0 } } },
	/* [ 3] */
	{ 0,
	  6,
	  { { 0, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [ 4] */
	{ 0,
	  1,
	  { { 0, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [ 5] */
	{ 0,
	  16,
	  { { 2, 1, 120, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [ 6] */
	{ 0,
	  15,
	  { { 2, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [ 7] */
	{ 0,
	  15,
	  { { 18, 2, 3, 0, 0, 0 },
		{ 19, 2, 2, 1, 0, 0 },
		{ 2, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [ 8] */
	{ 0,
	  17,
	  { { 0, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [ 9] */
	{ 0,
	  15,
	  { { 2, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [10] */
	{ 0,
	  19,
	  { { 0, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [11] */
	{ 0,
	  15,
	  { { 18, 2, 3, 0, 0, 0 },
		{ 19, 2, 2, 1, 0, 0 },
		{ 2, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [12] */
	{ 0,
	  7,
	  { { 2, 6, 1, 1, 2, 0 },
		{ 13, 6, 1, 1, 2, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [13] */
	{ 0,
	  13,
	  { { 14, 6, 1, 1, 2, 0 },
		{ 15, 2, 1, 1, 0, 0 },
		{ 16, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 5, 2, 1, 1, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [14] */
	{ 0,
	  7,
	  { { 13, 6, 1, 1, 2, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [15] */
	{ 0,
	  9,
	  { { 13, 6, 1, 1, 3, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [16] */
	{ 0,
	  8,
	  { { 13, 6, 1, 1, 2, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [17] */
	{ 0,
	  15,
	  { { 18, 2, 3, 0, 0, 0 },
		{ 19, 2, 2, 1, 0, 0 },
		{ 13, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [18] */
	{ 0,
	  15,
	  { { 17, 6, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [19] */
	{ 0,
	  11,
	  { { 13, 1, 120, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [20] */
	{ 0,
	  31,
	  { { 21, 2, 3, 0, 0, 0 },
		{ 22, 2, 2, 1, 0, 0 },
		{ 13, 2, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [21] */
	{ 0,
	  31,
	  { { 20, 6, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
	/* [22] */
	{ 0,
	  18,
	  { { 20, 1, 120, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0 } },
	  { { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 } } },
};

/* CueRef sequence arrays — one per game situation.
 *
 * openingSeq drives the intro cutscene music. Retail Collector's CD
 * differs from the demo sampler at entries 8-17 (cue times and
 * change-ref targets were retuned to the retail `title` MIDI shipped
 * in tiemus2.lfd). Using demo timings against retail audio produces
 * out-of-sync cue firings. Bytes copied verbatim from Z_TIE__.EXE
 * at 0xD2420 (SHA256 593820de…). */
// GLOBAL: TIE95 0xD2420
// GLOBAL: TIE98 0x4E5910
static CueRef openingSeq[20] = {
	{ 0, 1, { 0, 1, 400, 0, 0, 0 } },   { 0, 2, { 0, 1, 400, 0, 0, 0 } },
	{ 0, 32, { 0, 1, 400, 0, 0, 0 } },  { 0, 0, { 0, 1, 0, 0, 0, 0 } },
	{ 0, 3, { 0, 1, 200, 0, 0, 0 } },   { 0, 4, { 0, 1, 0, 0, 263, 267 } },
	{ 0, 4, { 0, 1, 0, 0, 270, 275 } }, { 0, 4, { 0, 1, 0, 0, 0, 0 } },
	{ 0, 4, { 0, 2, 2, 0, 0, 0 } },     { 0, 4, { 0, 2, 1, 1, 284, 290 } },
	{ 0, 5, { 0, 2, 0, 0, 0, 0 } },     { 0, 5, { 0, 2, 0, 0, 267, 274 } },
	{ 0, 5, { 0, 2, 2, 0, 283, 287 } }, { 0, 5, { 0, 2, 3, 0, 287, 292 } },
	{ 0, 5, { 0, 2, 5, 0, 299, 305 } }, { 0, 5, { 0, 2, 5, 0, 299, 310 } },
	{ 0, 5, { 0, 2, 6, 0, 0, 0 } },     { 0, 5, { 0, 2, 6, 0, 0, 0 } },
	{ 0, 5, { 0, 6, 1, 1, 2, 0 } },     { 0, 16, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2588
// GLOBAL: TIE98 0x4E5A78
static CueRef trainPodSeq[1] = { { 0, 16, { 5, 1, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD259C
// GLOBAL: TIE98 0x4E5A90
static CueRef combatPodSeq[1] = { { 0, 17, { 8, 1, 60, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD25B0
// GLOBAL: TIE98 0x4E5AA8
static CueRef launchSeq[1] = { { 0, 10, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD25C4
// GLOBAL: TIE98 0x4E5AC0
static CueRef medalsSeq[9] = {
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 2, 2, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 17, 1, 120, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2668
// GLOBAL: TIE98 0x4E5B68
static CueRef cut1Seq[1] = { { 0, 22, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD267C
// GLOBAL: TIE98 0x4E5B80
static CueRef cut2Seq[1] = { { 0, 23, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD2690
// GLOBAL: TIE98 0x4E5B98
static CueRef cut3Seq[1] = { { 0, 25, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD26A4
// GLOBAL: TIE98 0x4E5BB0
static CueRef cut4Seq[2] = { { 0, 21, { 0, 5, 0, 0, 0, 0 } }, { 0, 21, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD26C8
// GLOBAL: TIE98 0x4E5BD8
static CueRef cut5Seq[2] = { { 0, 24, { 0, 5, 0, 0, 0, 0 } }, { 0, 24, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD26EC
// GLOBAL: TIE98 0x4E5C00
static CueRef cut6Seq[1] = { { 0, 30, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD2700
// GLOBAL: TIE98 0x4E5C18
static CueRef cut7Seq[11] = {
	{ 0, 27, { 0, 0, 0, 0, 0, 0 } }, { 0, 27, { 0, 0, 0, 0, 0, 0 } }, { 0, 27, { 0, 2, 1, 1, 265, 268 } },
	{ 0, 27, { 0, 0, 0, 0, 0, 0 } }, { 0, 27, { 0, 6, 1, 1, 2, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 1, 200, 0, 0, 0 } },
	{ 0, 3, { 0, 6, 1, 1, 2, 0 } },  { 0, 16, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD27C8
// GLOBAL: TIE98 0x4E5CE0
static CueRef emperorSeq[7] = {
	{ 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 14, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2848
// GLOBAL: TIE98 0x4E5D60
static CueRef medicalSeq[3] = {
	{ 0, 28, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 28, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 28, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2880
// GLOBAL: TIE98 0x4E5D98
static CueRef capturedSeq[1] = { { 0, 26, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD2894
// GLOBAL: TIE98 0x4E5DB0
static CueRef deathSeq[1] = { { 0, 29, { 0, 0, 0, 0, 0, 0 } } };
// GLOBAL: TIE95 0xD28A8
// GLOBAL: TIE98 0x4E5DC8
static CueRef cut8Seq[6] = {
	{ 0, 33, { 0, 2, 1, 0, 0, 0 } }, { 0, 33, { 0, 0, 0, 0, 0, 0 } }, { 0, 33, { 0, 0, 0, 0, 278, 287 } },
	{ 0, 33, { 0, 0, 0, 0, 0, 0 } }, { 0, 33, { 0, 2, 2, 0, 0, 0 } }, { 0, 33, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2914
// GLOBAL: TIE98 0x4E5E38
static CueRef cut9Seq[5] = {
	{ 0, 34, { 0, 0, 0, 0, 0, 0 } }, { 0, 34, { 0, 2, 1, 0, 0, 0 } }, { 0, 34, { 0, 0, 0, 0, 273, 275 } },
	{ 0, 34, { 0, 2, 2, 0, 0, 0 } }, { 0, 34, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2970
// GLOBAL: TIE98 0x4E5E98
static CueRef cut10Seq[6] = {
	{ 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 35, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 36, { 0, 0, 0, 0, 0, 0 } },
};

/* Retail-only sequences (cases 24-27 in GetSequence). Reference the
 * three extra soundNames entries (empshort/cloaktst/kablam) added in
 * the Collector's CD expansion content. Bytes copied from
 * Z_TIE__.EXE @ 0xD29E0 (cut11) / 0xD2A28 (cut12) / 0xD2A60 (cut13) /
 * 0xD2ABC (cut14). Demo sampler does not have these. */
// GLOBAL: TIE95 0xD29E0
// GLOBAL: TIE98 0x4E5F10
static CueRef cut11Seq[4] = {
	/* case 24, 'cloaktst' */
	{ 0, 37, { 0, 2, 1, 0, 271, 274 } },
	{ 0, 37, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 37, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 37, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2A28
// GLOBAL: TIE98 0x4E5F58
static CueRef cut12Seq[3] = {
	/* case 25, 'empshort' */
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2A60
// GLOBAL: TIE98 0x4E5F90
static CueRef cut13Seq[5] = {
	/* case 26, 'kablam' */
	{ 0, 38, { 0, 2, 1, 0, 0, 0 } }, { 0, 38, { 0, 2, 2, 0, 0, 0 } }, { 0, 38, { 0, 2, 3, 0, 0, 0 } },
	{ 0, 38, { 0, 0, 0, 0, 0, 0 } }, { 0, 38, { 0, 0, 0, 0, 0, 0 } },
};
// GLOBAL: TIE95 0xD2ABC
// GLOBAL: TIE98 0x4E5FF0
static CueRef cut14Seq[4] = {
	/* case 27, 'empshort' */
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
};

/* Runtime state */
// GLOBAL: TIE95 0xFB608
// GLOBAL: TIE98 0x584D00
static int32_t rseed1;
// GLOBAL: TIE95 0xFB60C
// GLOBAL: TIE98 0x584D04
static int32_t rseed2;
// GLOBAL: TIE95 0xFB614
// GLOBAL: TIE98 0x584D0C
static int16_t currentCuePoint;
// GLOBAL: TIE95 0xFB610
// GLOBAL: TIE98 0x584D08
static int16_t currentSequence;
// GLOBAL: TIE95 0xFB612
// GLOBAL: TIE98 0x584CFC
static int16_t currentState;

/* --- Internal helpers --- */

static int16_t mfscript_GetRandom(int16_t lo, int16_t hi);
static CueRef* mfscript_GetSequence(void);
static ChangeRef* mfscript_GetDefaultChangeRef(void);
static void mfscript_DoChange(ChangeRef* cgp, intptr_t sound1, intptr_t sound2);
static void mfscript_DoJumpStart(ChangeRef* cgp, intptr_t sound);
static void mfscript_ChgXfade(intptr_t sound1, intptr_t sound2, int16_t fadeOut, int16_t fadeIn);
static void mfscript_ChgJumpMrk(intptr_t sound1, intptr_t sound2, int16_t jumpHook1, int16_t marker,
								int16_t jumpHook2);
static void mfscript_ChgJumpOnBeat(intptr_t sound1, intptr_t sound2, int16_t endChunk, int16_t marker,
								   int16_t jumpHook2);

/* --- Public API --- */

// FUNCTION: TIE95 0x87BE0
int16_t mfscript_MfStartScript(void* idp) {
	(void)idp;
	currentState = 0;
	currentSequence = 0;
	currentCuePoint = -1;
	/* Seed PRNG from stack addresses (deterministic per run) */
	rseed1 = TieImuse_AddressSeed(&rseed2);
	rseed2 = ~TieImuse_AddressSeed(&rseed1);
	return 0;
}

// FUNCTION: TIE95 0x87C28
int16_t mfscript_MfStopScript(void) {
	lolevel_ImPrintf("Stop script!...");
	lolevel_ImStopAllSounds();
	filelist_ImUnloadAll();
	return 0;
}

// FUNCTION: TIE95 0x87C44
int16_t mfscript_MfRefreshScript(void) {
	filelist_ImFlushSounds();
	return 0;
}

// FUNCTION: TIE95 0x87C50
int16_t mfscript_MfSetState(int16_t state) {
	StateRef *oldSrp, *newSrp;
	ChangeRef* cgp;
	int i;

	if (state == -1 || state == currentState)
		return currentState;

	if (state > NUM_STATES - 1) {
		lolevel_ImStopAllSounds();
		currentState = state;
		return state;
	}

	if (currentSequence || currentState > NUM_STATES - 1) {
		currentState = state;
		return state;
	}

	oldSrp = &stateRefs[currentState];
	newSrp = &stateRefs[state];

	/* Load the new state's sound if it has one */
	if (newSrp->nameIndex) {
		newSrp->sound = 0;
		if (newSrp->nameIndex == oldSrp->nameIndex)
			newSrp->sound = filelist_ImFindSound(soundNames[newSrp->nameIndex]);
		if (!newSrp->sound)
			newSrp->sound = filelist_ImLoadSound(soundNames[newSrp->nameIndex]);
		if (!newSrp->sound) {
			lolevel_ImStopAllSounds();
			return currentState;
		}
	}

	/* Find matching state transition */
	cgp = oldSrp->stateChanges;
	for (i = 0; i < MAX_STATE_CHANGES && cgp->target != state && cgp->target; i++)
		cgp++;

	mfscript_DoChange(cgp, oldSrp->sound, newSrp->sound);
	currentState = state;
	return state;
}

// FUNCTION: TIE95 0x87D74
int16_t mfscript_MfSetSequence(int16_t sequence) {
	CueRef *sqp, *crp;
	StateRef* srp;
	int16_t newState = 0;

	if (sequence == -1 || sequence == currentSequence)
		return currentSequence;

	if (sequence) {
		/* Start a new sequence */
		currentSequence = sequence;
		currentCuePoint = -1;
		mfscript_MfSetCuePoint(0);
	} else if (currentSequence) {
		/* End the current sequence — transition back to state */
		if (currentCuePoint == -1) {
			lolevel_ImPrintf("CUE ERR!...");
			newState = currentState;
		} else {
			sqp = mfscript_GetSequence();
			if (!sqp)
				return currentSequence;
			crp = &sqp[currentCuePoint];
			lolevel_ImPrintf("Auto set state %lx...", (long)currentState);
			srp = &stateRefs[currentState];

			if (crp->cueChange.target) {
				srp->sound = 0;
				if (crp->nameIndex == srp->nameIndex)
					srp->sound = filelist_ImFindSound(soundNames[crp->nameIndex]);
				if (!srp->sound)
					srp->sound = filelist_ImLoadSound(soundNames[srp->nameIndex]);
				mfscript_DoChange(&crp->cueChange, crp->sound, srp->sound);
			} else {
				lolevel_ImPrintf("Force state...");
				newState = currentState;
			}
		}

		currentSequence = 0;
		currentCuePoint = -1;

		if (newState) {
			lolevel_ImStopAllSounds();
			currentState = 0;
			mfscript_MfSetState(newState);
		}
	}
	return currentSequence;
}

// FUNCTION: TIE95 0x87EEC
int16_t mfscript_MfSetCuePoint(int16_t cuePoint) {
	CueRef *sqp, *crp, *nextCrp;
	ChangeRef* cgp;
	intptr_t sound;
	int i;

	if (cuePoint == -1 || !currentSequence)
		return currentCuePoint;

	if (cuePoint == currentCuePoint) {
		if (currentCuePoint)
			lolevel_ImPrintf("ERR: redundant Cue Point!...");
		return currentCuePoint;
	}

	sqp = mfscript_GetSequence();
	if (!sqp)
		return currentCuePoint;

	if (cuePoint) {
		/* Advancing within a sequence */
		if (cuePoint != currentCuePoint + 1)
			currentCuePoint = cuePoint - 1;

		crp = &sqp[currentCuePoint];
		cgp = &crp->cueChange;

		if (!crp->sound)
			crp->sound = filelist_ImFindSound(soundNames[crp->nameIndex]);
		sound = crp->sound;
	} else {
		/* Transition from state into sequence (cue 0) */
		StateRef* srp;

		lolevel_ImPrintf("Trans from state %lx...", (long)currentState);
		srp = &stateRefs[currentState];
		cgp = srp->seqChanges;
		for (i = 0; i < MAX_SEQ_CHANGES && cgp->target != currentSequence && cgp->target; i++)
			cgp++;

		if (cgp->target == currentSequence)
			sound = srp->sound;
		else {
			sound = 0;
			cgp = mfscript_GetDefaultChangeRef();
		}
	}

	/* Load the next cue's sound */
	nextCrp = &sqp[cuePoint];
	if (nextCrp->nameIndex) {
		nextCrp->sound = filelist_ImFindSound(soundNames[nextCrp->nameIndex]);
		if (!nextCrp->sound || nextCrp->sound != sound)
			nextCrp->sound = filelist_ImLoadSound(soundNames[nextCrp->nameIndex]);
		lolevel_ImPrintf("loading ");
		lolevel_ImPrintf(soundNames[nextCrp->nameIndex]);
		lolevel_ImPrintf("...");

		if (!nextCrp->sound) {
			lolevel_ImPrintf("Sound Load ERR 2!...");
			lolevel_ImStopAllSounds();
			return currentCuePoint;
		}
	}

	if (sound)
		mfscript_DoJumpStart(cgp, sound);
	mfscript_DoChange(cgp, sound, nextCrp->sound);
	currentCuePoint = cuePoint;
	return cuePoint;
}

// FUNCTION: TIE98 0x454B50
int16_t mfscript_MfSetAttribute(int16_t number, int16_t val) {
	(void)number;
	(void)val;
	return 0; /* stub */
}

/* --- Transition implementations --- */

// FUNCTION: TIE95 0x8810C
static void mfscript_ChgXfade(intptr_t sound1, intptr_t sound2, int16_t fadeOut, int16_t fadeIn) {
	lolevel_ImPrintf("chg Xfade...");
	if (sound1 != sound2) {
		if (sound1)
			lolevel_ImFadeParam((intptr_t)sound1, IM_PARAM_VOLALT, 0, fadeOut);
		if (sound2) {
			int16_t result;

			lolevel_ImPause();
			result = (int16_t)lolevel_ImStartSound((intptr_t)sound2, 0);
			if (result)
				lolevel_ImPrintf("ERR start sound returned %lx...", (long)result);
			lolevel_ImSetParam((intptr_t)sound2, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
			if (fadeIn) {
				lolevel_ImSetParam((intptr_t)sound2, IM_PARAM_VOLALT, 0);
				lolevel_ImFadeParam((intptr_t)sound2, IM_PARAM_VOLALT, 127, fadeIn);
			}
			lolevel_ImResume();
		}
	}
}

// FUNCTION: TIE95 0x881B4
static void mfscript_ChgJumpMrk(intptr_t sound1, intptr_t sound2, int16_t jumpHook1, int16_t marker,
								int16_t jumpHook2) {
	int16_t result;

	lolevel_ImPrintf("JumpMrk to %lx...", (intptr_t)sound2);
	if (jumpHook1) {
		lolevel_ImPrintf("Set 1st hook %lx...", (long)jumpHook1);
		result = (int16_t)lolevel_ImSetHook((intptr_t)sound1, jumpHook1);
		if (result)
			lolevel_ImPrintf("Hook ERR %lx...", (long)result);
	}

	if (!sound2 || sound1 == sound2)
		return;
	if (!sound1) {
		lolevel_ImPrintf("Start sound 2 with hook...");
		lolevel_ImPause();
		lolevel_ImStartSound((intptr_t)sound2, 0);
		lolevel_ImSetParam((intptr_t)sound2, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
		if (jumpHook2) {
			result = (int16_t)lolevel_ImSetHook((intptr_t)sound2, jumpHook2);
			if (result)
				lolevel_ImPrintf("Hook ERR %lx...", (long)result);
		}
		lolevel_ImResume();
		return;
	}

	lolevel_ImPrintf("Set trig %lx...", (long)marker);
	lolevel_ImPause();
	/* When sound1 reaches the marker: start sound2 in the dipped group and
	 * fade sound1 out over 60 ticks, then optionally set sound2's hook. */
	lolevel_ImSetTrigger((intptr_t)sound1, marker, IMUSE_CMD_START_SOUND, (intptr_t)sound2);
	lolevel_ImSetTrigger((intptr_t)sound1, marker, IMUSE_CMD_SET_PARAM, (intptr_t)sound2,
						 IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
	lolevel_ImSetTrigger((intptr_t)sound1, marker, IMUSE_CMD_FADE_PARAM, (intptr_t)sound1, IM_PARAM_VOLALT, 0,
						 60);
	if (jumpHook2) {
		lolevel_ImPrintf("2nd hook %lx...", (long)jumpHook2);
		lolevel_ImSetTrigger((intptr_t)sound1, marker, IMUSE_CMD_SET_HOOK, (intptr_t)sound2, jumpHook2);
	}
	lolevel_ImResume();
	if (lolevel_ImGetParam((intptr_t)sound1, IMUSE_PARAM_SOUND_PLAY_COUNT) <= 0)
		lolevel_ImPrintf("Sound 1 NOT PLAYING!!!!");
}

// FUNCTION: TIE95 0x88310
static void mfscript_ChgJumpOnBeat(intptr_t sound1, intptr_t sound2, int16_t endChunk, int16_t marker,
								   int16_t jumpHook2) {
	int16_t tick;
	int16_t result;

	lolevel_ImPrintf("JumpOnBeat to %lx...", (intptr_t)sound2);
	lolevel_ImPause();
	tick = (int16_t)lolevel_ImGetParam((intptr_t)sound1, IM_PARAM_TICK);
	lolevel_ImPrintf("Jump to end chunk %lx...", (long)endChunk);
	lolevel_ImJumpMidi((intptr_t)sound1, endChunk, 1, 4, tick, 1);
	lolevel_ImResume();

	if (!sound2 || sound1 == sound2)
		return;
	if (!sound1) {
		lolevel_ImPrintf("Start sound 2 with hook...");
		lolevel_ImPause();
		lolevel_ImStartSound((intptr_t)sound2, 0);
		lolevel_ImSetParam((intptr_t)sound2, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
		if (jumpHook2) {
			result = (int16_t)lolevel_ImSetHook((intptr_t)sound2, jumpHook2);
			if (result)
				lolevel_ImPrintf("Hook ERR %lx...", (long)result);
		}
		lolevel_ImResume();
		return;
	}

	lolevel_ImPrintf("Set trig %lx...", (long)marker);
	lolevel_ImPause();
	lolevel_ImSetTrigger((intptr_t)sound1, marker, IMUSE_CMD_START_SOUND, (intptr_t)sound2);
	lolevel_ImSetTrigger((intptr_t)sound1, marker, IMUSE_CMD_SET_PARAM, (intptr_t)sound2,
						 IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
	if (jumpHook2) {
		lolevel_ImPrintf("2nd hook %lx...", (long)jumpHook2);
		lolevel_ImSetTrigger((intptr_t)sound1, marker, IMUSE_CMD_SET_HOOK, (intptr_t)sound2, jumpHook2);
	}
	lolevel_ImResume();
}

// FUNCTION: TIE95 0x88470
static void mfscript_DoChange(ChangeRef* cgp, intptr_t sound1, intptr_t sound2) {
	if (!cgp->opcode) {
		filelist_ImUnloadSound(sound2);
		return;
	}
	{
		/* Validate sound1 is playing */
		if (sound1 && lolevel_ImGetParam(sound1, IMUSE_PARAM_SOUND_PLAY_COUNT) <= 0) {
			lolevel_ImPrintf("ERR: Sound1 not playing...");
			sound1 = 0;
		}

		if (!sound1) {
			/* Try to find any playing sound that isn't sound2.
			 * GetNextSound returns intptr_t; must NOT be narrowed
			 * through `int` or the 64-bit sound-id truncation
			 * creates a stable iteration point and the loop hangs. */
			do {
				sound1 = lolevel_ImGetNextSound(sound1);
			} while (sound1 > 0 && sound1 != sound2);

			if (sound1 == sound2) {
				lolevel_ImPrintf("doChange punt...");
				lolevel_ImSetHook(sound2, 0);
				return;
			}
			lolevel_ImStopAllSounds();
			sound1 = 0;
		}

		lolevel_ImSetHook(sound1, 0);
		lolevel_ImClearTrigger(-1, -1, -1);

		switch (cgp->opcode) {
			case 1:
				mfscript_ChgXfade(sound1, sound2, cgp->arg1, cgp->arg2);
				break;
			case 2:
				mfscript_ChgJumpMrk(sound1, sound2, cgp->arg1, cgp->arg2, 0);
				break;
			case 6:
				mfscript_ChgJumpMrk(sound1, sound2, cgp->arg1, cgp->arg2, cgp->arg3);
				break;
			case 7:
				mfscript_ChgJumpOnBeat(sound1, sound2, cgp->arg1, cgp->arg2, cgp->arg3);
				break;
			case 4:
				mfscript_ChgXfade(sound1, sound2, cgp->arg1, cgp->arg2);
				lolevel_ImSetTrigger((intptr_t)sound2, 1, IMUSE_CMD_SET_PARAM, (intptr_t)sound2,
									 IM_PARAM_ATTR, 0);
				break;
			case 5:
				lolevel_ImPrintf("resume...");
				lolevel_ImSetParam(sound1, IM_PARAM_ATTR, 64);
				break;
			default:
				lolevel_ImPrintf("Default change!...");
				mfscript_ChgXfade(sound1, sound2, 100, 0);
				break;
		}
		filelist_ImUnloadSound(sound2);
	}
}

// FUNCTION: TIE95 0x88638
static void mfscript_DoJumpStart(ChangeRef* cgp, intptr_t sound) {
	/* arg3/arg4 each pack a (chunk, measure) byte pair:
	 *   arg3 = (thresholdChunk << 8) | thresholdMeas
	 *   arg4 = (scanTargetChunk << 8) | scanTargetMeas
	 * Watcom emitted this as unaligned-dword loads + SAR; do NOT
	 * read arg3/arg4 as whole shorts — that produced a 263-chunk
	 * scan and the "Sq couldn't find chunk 263" sequencer error. */
	int16_t chunk, meas, jsc, jsm;
	int16_t thresholdChunk, thresholdMeas;

	if (!cgp->arg3)
		return;

	chunk = lolevel_ImGetParam(sound, IM_PARAM_CHUNK);
	if (chunk < 0)
		return;
	meas = lolevel_ImGetParam(sound, IM_PARAM_MEASURE);
	if (meas < 0)
		return;

	thresholdChunk = (int16_t)(int8_t)((cgp->arg3 >> 8) & 0xFF);
	thresholdMeas = (int16_t)(uint8_t)(cgp->arg3 & 0xFF);
	if (chunk <= thresholdChunk && meas <= thresholdMeas) {
		lolevel_ImSetParam(sound, IM_PARAM_VOLALT, 0);
		jsc = (int16_t)(int8_t)((cgp->arg4 >> 8) & 0xFF);
		jsm = (int16_t)(uint8_t)(cgp->arg4 & 0xFF);
		lolevel_ImPrintf("Scan to chk %lx...", (long)jsc);
		lolevel_ImPrintf("meas %lx...", (long)jsm);
		lolevel_ImScanMidi(sound, jsc, jsm, 1, 0);
		lolevel_ImFadeParam(sound, IM_PARAM_VOLALT, 127, 30);
	}
}

// FUNCTION: TIE95 0x88764
// FUNCTION: TIE98 0x455180
static CueRef* mfscript_GetSequence(void) {
	switch (currentSequence) {
		case 1:
			return openingSeq;
		case 2:
			return trainPodSeq;
		case 3:
			return combatPodSeq;
		case 5:
			return launchSeq;
		case 6:
			return cut1Seq;
		case 7:
			return cut2Seq;
		case 8:
			return cut3Seq;
		case 9:
			return cut4Seq;
		case 10:
			return cut5Seq;
		case 11:
			return cut6Seq;
		case 12:
			return emperorSeq;
		case 14:
		case 15:
			return cut7Seq;
		case 16:
		case 17:
		case 18:
			return medalsSeq;
		case 13:
			return medicalSeq;
		case 19:
			return capturedSeq;
		case 20:
			return deathSeq;
		case 21:
			return cut8Seq;
		case 22:
			return cut9Seq;
		case 23:
			return cut10Seq;
		/* Retail Collector's CD only — expansion-pack cue sequences. */
		case 24:
			return cut11Seq;
		case 25:
			return cut12Seq;
		case 26:
			return cut13Seq;
		case 27:
			return cut14Seq;
		default:
			lolevel_ImPrintf("GET SEQ ERR!...");
			return NULL;
	}
}

// FUNCTION: TIE95 0x8881C
// FUNCTION: TIE98 0x4552A0
static ChangeRef* mfscript_GetDefaultChangeRef(void) {
	if (currentSequence >= 9 && currentSequence <= 10)
		return &stateRefs[0].seqChanges[1];
	return &stateRefs[0].seqChanges[0];
}

/* Unreferenced in retail. */
// FUNCTION: TIE95 0x8883C
static int16_t mfscript_GetRandom(int16_t lo, int16_t hi) {
	int i, c;

	uint16_t raw;

	for (i = 0; i < 23; i++) {
		c = ((rseed2 & 0x20000000) == 0) ^ ((rseed1 & 0x40000000) != 0);
		rseed1 = rseed1 * 2 + c;
	}
	for (i = 0; i < 37; i++) {
		c = ((rseed1 & 0x20000000) == 0) ^ ((rseed2 & 0x40000000) != 0);
		rseed2 = rseed2 * 2 + c;
	}

	raw = (uint16_t)(rseed1 + rseed2);
	return (int16_t)(((uint32_t)raw * (hi - lo + 1)) >> 16) + lo;
}
