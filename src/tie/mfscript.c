#include "tie/mfscript.h"
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

#define NUM_STATES 23
#define NUM_SOUND_NAMES 39
#define MAX_STATE_CHANGES 7
#define MAX_SEQ_CHANGES 2

#define IM_PARAM_PRIORITY 0x100
#define IM_PARAM_VOLUME 0x200
#define IMUSE_PARAM_SOUND_GROUP 0x400
#define IM_PARAM_VOLALT 0x600
#define IM_PARAM_CHUNK 0xB00
#define IM_PARAM_MEASURE 0xC00
#define IM_PARAM_TICK 0xE00
#define IM_PARAM_ATTR 0xF00
#define IMUSE_GROUP_DIPPED 4

static char soundNames[NUM_SOUND_NAMES][9] = {
	"",         "drone",    "POINK",    "title",    "tocity",   "battle",   "stately", "bridge",
	"briefmap", "secret",   "launch",   "awe",      "register", "mainmen",  "emperor", "phew",
	"trainpod", "fightpod", "evilmonk", "fightmap", "medals",   "tieshow",  "harkspy", "harktalk",
	"harkkill", "thrawny",  "starlog",  "ceremony", "medical",  "funeral",  "bweapon", "bummer",
	"perelogo", "2battle",  "2thrawny", "2emperor", "empshort", "cloaktst", "kablam"
};

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
static CueRef trainPodSeq[1] = { { 0, 16, { 5, 1, 0, 0, 0, 0 } } };
static CueRef combatPodSeq[1] = { { 0, 17, { 8, 1, 60, 0, 0, 0 } } };
static CueRef launchSeq[1] = { { 0, 10, { 0, 0, 0, 0, 0, 0 } } };
static CueRef medalsSeq[9] = {
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 2, 2, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 17, 1, 120, 0, 0, 0 } },
};
static CueRef cut1Seq[1] = { { 0, 22, { 0, 0, 0, 0, 0, 0 } } };
static CueRef cut2Seq[1] = { { 0, 23, { 0, 0, 0, 0, 0, 0 } } };
static CueRef cut3Seq[1] = { { 0, 25, { 0, 0, 0, 0, 0, 0 } } };
static CueRef cut4Seq[2] = { { 0, 21, { 0, 5, 0, 0, 0, 0 } }, { 0, 21, { 0, 0, 0, 0, 0, 0 } } };
static CueRef cut5Seq[2] = { { 0, 24, { 0, 5, 0, 0, 0, 0 } }, { 0, 24, { 0, 0, 0, 0, 0, 0 } } };
static CueRef cut6Seq[1] = { { 0, 30, { 0, 0, 0, 0, 0, 0 } } };
static CueRef cut7Seq[11] = {
	{ 0, 27, { 0, 0, 0, 0, 0, 0 } }, { 0, 27, { 0, 0, 0, 0, 0, 0 } }, { 0, 27, { 0, 2, 1, 1, 265, 268 } },
	{ 0, 27, { 0, 0, 0, 0, 0, 0 } }, { 0, 27, { 0, 6, 1, 1, 2, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 0, 0, 0, 0, 0 } }, { 0, 20, { 0, 1, 200, 0, 0, 0 } },
	{ 0, 3, { 0, 6, 1, 1, 2, 0 } },  { 0, 16, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef emperorSeq[7] = {
	{ 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } }, { 0, 14, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 14, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef medicalSeq[3] = {
	{ 0, 28, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 28, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 28, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef capturedSeq[1] = { { 0, 26, { 0, 0, 0, 0, 0, 0 } } };
static CueRef deathSeq[1] = { { 0, 29, { 0, 0, 0, 0, 0, 0 } } };
static CueRef cut8Seq[6] = {
	{ 0, 33, { 0, 2, 1, 0, 0, 0 } }, { 0, 33, { 0, 0, 0, 0, 0, 0 } }, { 0, 33, { 0, 0, 0, 0, 278, 287 } },
	{ 0, 33, { 0, 0, 0, 0, 0, 0 } }, { 0, 33, { 0, 2, 2, 0, 0, 0 } }, { 0, 33, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef cut9Seq[5] = {
	{ 0, 34, { 0, 0, 0, 0, 0, 0 } }, { 0, 34, { 0, 2, 1, 0, 0, 0 } }, { 0, 34, { 0, 0, 0, 0, 273, 275 } },
	{ 0, 34, { 0, 2, 2, 0, 0, 0 } }, { 0, 34, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef cut10Seq[6] = {
	{ 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 35, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 35, { 0, 0, 0, 0, 0, 0 } }, { 0, 36, { 0, 0, 0, 0, 0, 0 } },
};

/* Retail-only sequences (cases 24-27 in GetSequence). Reference the
 * three extra soundNames entries (empshort/cloaktst/kablam) added in
 * the Collector's CD expansion content. Bytes copied from
 * Z_TIE__.EXE @ 0xD29E0 (cut11) / 0xD2A28 (cut12) / 0xD2A60 (cut13) /
 * 0xD2ABC (cut14). Demo sampler does not have these. */
static CueRef cut11Seq[4] = {
	/* case 24, 'cloaktst' */
	{ 0, 37, { 0, 2, 1, 0, 271, 274 } },
	{ 0, 37, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 37, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 37, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef cut12Seq[3] = {
	/* case 25, 'empshort' */
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef cut13Seq[5] = {
	/* case 26, 'kablam' */
	{ 0, 38, { 0, 2, 1, 0, 0, 0 } }, { 0, 38, { 0, 2, 2, 0, 0, 0 } }, { 0, 38, { 0, 2, 3, 0, 0, 0 } },
	{ 0, 38, { 0, 0, 0, 0, 0, 0 } }, { 0, 38, { 0, 0, 0, 0, 0, 0 } },
};
static CueRef cut14Seq[4] = {
	/* case 27, 'empshort' */
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
	{ 0, 36, { 0, 0, 0, 0, 0, 0 } },
};

/* Runtime state */
// GLOBAL: TIE 0xD498C
static int32_t rseed1;
// GLOBAL: TIE 0xD4990
static int32_t rseed2;
static int16_t currentCuePoint;
// GLOBAL: TIE 0xD4994
static int16_t currentSequence;
// GLOBAL: TIE 0xD49A4
static int16_t currentState;

/* --- Internal helpers --- */

static int16_t GetRandom(int16_t lo, int16_t hi);
static CueRef* GetSequence(void);
static ChangeRef* GetDefaultChangeRef(void);
static void DoChange(ChangeRef* cgp, void* sound1, void* sound2);
static void DoJumpStart(ChangeRef* cgp, void* sound);
static void ChgXfade(void* sound1, void* sound2, int16_t fadeOut, int16_t fadeIn);
static void ChgJumpMrk(void* sound1, void* sound2, int16_t jumpHook1, int16_t marker, int16_t jumpHook2);
static void ChgJumpOnBeat(void* sound1, void* sound2, int16_t endChunk, int16_t marker, int16_t jumpHook2);
static void DoCallback(void);

/* --- Public API --- */

// FUNCTION: TIE 0x87BE0
int16_t mfscript_MfStartScript(void* idp) {
	(void)idp;
	currentState = 0;
	currentSequence = 0;
	currentCuePoint = -1;
	/* Seed PRNG from stack addresses (deterministic per run) */
	rseed1 = (int32_t)(intptr_t)&rseed2;
	rseed2 = ~(int32_t)(intptr_t)&rseed1;
	return 0;
}

// FUNCTION: TIE 0x87C28
int16_t mfscript_MfStopScript(void) {
	imuse_stop_all_sounds(im);
	imuse_filelist_unload_all(im);
	return 0;
}

// FUNCTION: TIE 0x87C44
int16_t mfscript_MfRefreshScript(void) {
	imuse_filelist_flush(im);
	return 0;
}

// FUNCTION: TIE 0x87C50
int16_t mfscript_MfSetState(int16_t state) {
	StateRef *oldSrp, *newSrp;
	ChangeRef* cgp;
	int i;

	if (state == -1 || state == currentState)
		return currentState;

	if (state > NUM_STATES - 1) {
		imuse_stop_all_sounds(im);
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
			newSrp->sound = (intptr_t)imuse_filelist_find(im, soundNames[newSrp->nameIndex]);
		if (!newSrp->sound)
			newSrp->sound = (intptr_t)imuse_filelist_load(im, soundNames[newSrp->nameIndex]);
		if (!newSrp->sound) {
			imuse_stop_all_sounds(im);
			return currentState;
		}
	}

	/* Find matching state transition */
	cgp = oldSrp->stateChanges;
	for (i = 0; i < MAX_STATE_CHANGES && cgp->target != state && cgp->target; i++)
		cgp++;

	DoChange(cgp, (void*)oldSrp->sound, (void*)newSrp->sound);
	currentState = state;
	return state;
}

// FUNCTION: TIE 0x87D74
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
			newState = currentState;
		} else {
			sqp = GetSequence();
			if (!sqp)
				return currentSequence;
			crp = &sqp[currentCuePoint];
			srp = &stateRefs[currentState];

			if (crp->cueChange.target) {
				srp->sound = 0;
				if (crp->nameIndex == srp->nameIndex)
					srp->sound = (intptr_t)imuse_filelist_find(im, soundNames[crp->nameIndex]);
				if (!srp->sound)
					srp->sound = (intptr_t)imuse_filelist_load(im, soundNames[srp->nameIndex]);
				DoChange(&crp->cueChange, (void*)crp->sound, (void*)srp->sound);
			} else {
				newState = currentState;
			}
		}

		currentSequence = 0;
		currentCuePoint = -1;

		if (newState) {
			imuse_stop_all_sounds(im);
			currentState = 0;
			mfscript_MfSetState(newState);
		}
	}
	return currentSequence;
}

// FUNCTION: TIE 0x87EEC
int16_t mfscript_MfSetCuePoint(int16_t cuePoint) {
	CueRef *sqp, *crp, *nextCrp;
	ChangeRef* cgp;
	void* sound;
	int i;

	if (cuePoint == -1 || !currentSequence)
		return currentCuePoint;

	if (cuePoint == currentCuePoint) {
		return currentCuePoint;
	}

	sqp = GetSequence();
	if (!sqp)
		return currentCuePoint;

	if (cuePoint) {
		/* Advancing within a sequence */
		if (cuePoint != currentCuePoint + 1)
			currentCuePoint = cuePoint - 1;

		crp = &sqp[currentCuePoint];
		cgp = &crp->cueChange;

		if (!crp->sound)
			crp->sound = (intptr_t)imuse_filelist_find(im, soundNames[crp->nameIndex]);
		sound = (void*)crp->sound;
	} else {
		/* Transition from state into sequence (cue 0) */
		StateRef* srp = &stateRefs[currentState];
		cgp = srp->seqChanges;
		for (i = 0; i < MAX_SEQ_CHANGES && cgp->target != currentSequence && cgp->target; i++)
			cgp++;

		if (cgp->target == currentSequence)
			sound = (void*)srp->sound;
		else {
			sound = NULL;
			cgp = GetDefaultChangeRef();
		}
	}

	/* Load the next cue's sound */
	nextCrp = &sqp[cuePoint];
	if (nextCrp->nameIndex) {
		nextCrp->sound = (intptr_t)imuse_filelist_find(im, soundNames[nextCrp->nameIndex]);
		if (!nextCrp->sound || (void*)nextCrp->sound != sound)
			nextCrp->sound = (intptr_t)imuse_filelist_load(im, soundNames[nextCrp->nameIndex]);

		if (!nextCrp->sound) {
			imuse_stop_all_sounds(im);
			return currentCuePoint;
		}
	}

	if (sound)
		DoJumpStart(cgp, sound);
	DoChange(cgp, sound, (void*)nextCrp->sound);
	currentCuePoint = cuePoint;
	return cuePoint;
}

int16_t mfscript_MfSetAttribute(int16_t number, int16_t val) {
	(void)number;
	(void)val;
	return 0; /* stub */
}

/* --- Transition implementations --- */

static void ChgXfade(void* sound1, void* sound2, int16_t fadeOut, int16_t fadeIn) {
	if (sound1 != sound2) {
		if (sound1)
			imuse_fade_param(im, (intptr_t)sound1, IM_PARAM_VOLALT, 0, fadeOut);
		if (sound2) {
			imuse_pause(im);
			imuse_start_sound(im, (intptr_t)sound2, 0);
			imuse_set_param(im, (intptr_t)sound2, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
			if (fadeIn) {
				imuse_set_param(im, (intptr_t)sound2, IM_PARAM_VOLALT, 0);
				imuse_fade_param(im, (intptr_t)sound2, IM_PARAM_VOLALT, 127, fadeIn);
			}
			imuse_resume(im);
		}
	}
}

static void ChgJumpMrk(void* sound1, void* sound2, int16_t jumpHook1, int16_t marker, int16_t jumpHook2) {
	if (jumpHook1)
		imuse_set_hook(im, (intptr_t)sound1, jumpHook1);

	if (sound2 && sound1 != sound2) {
		if (sound1) {
			imuse_pause(im);
			/* When sound1 hits `marker`: start sound2, route to DIPPED
			 * group, fade sound1 to silence over 60 ticks, optionally
			 * arm a hook on sound2. Each trigger packs a different
			 * IMUSE_CMD_* opcode + replay args. */
			ImuseCmd t_start = {
				.opcode = IMUSE_CMD_START_SOUND,
				.args[0] = (intptr_t)sound2,
			};
			ImuseCmd t_group = {
				.opcode = IMUSE_CMD_SET_PARAM,
				.args[0] = (intptr_t)sound2,
				.args[1] = IMUSE_PARAM_SOUND_GROUP,
				.args[2] = IMUSE_GROUP_DIPPED,
			};
			ImuseCmd t_fade = {
				.opcode = IMUSE_CMD_FADE_PARAM,
				.args[0] = (intptr_t)sound1,
				.args[1] = IM_PARAM_VOLALT,
				.args[2] = 0,
				.args[3] = 60,
			};
			imuse_set_trigger(im, (intptr_t)sound1, marker, &t_start);
			imuse_set_trigger(im, (intptr_t)sound1, marker, &t_group);
			imuse_set_trigger(im, (intptr_t)sound1, marker, &t_fade);
			if (jumpHook2) {
				ImuseCmd t_hook = {
					.opcode = IMUSE_CMD_SET_HOOK,
					.args[0] = (intptr_t)sound2,
					.args[1] = jumpHook2,
				};
				imuse_set_trigger(im, (intptr_t)sound1, marker, &t_hook);
			}
		} else {
			imuse_pause(im);
			imuse_start_sound(im, (intptr_t)sound2, 0);
			imuse_set_param(im, (intptr_t)sound2, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
			if (jumpHook2)
				imuse_set_hook(im, (intptr_t)sound2, jumpHook2);
		}
		imuse_resume(im);
	}
}

static void ChgJumpOnBeat(void* sound1, void* sound2, int16_t endChunk, int16_t marker, int16_t jumpHook2) {
	int16_t tick;

	imuse_pause(im);
	tick = imuse_get_param(im, (intptr_t)sound1, IM_PARAM_TICK);
	imuse_midi_jump(im, (intptr_t)sound1, endChunk, 1, 4, tick, 1);
	imuse_resume(im);

	if (sound2 && sound1 != sound2) {
		if (sound1) {
			imuse_pause(im);
			ImuseCmd t_start = {
				.opcode = IMUSE_CMD_START_SOUND,
				.args[0] = (intptr_t)sound2,
			};
			ImuseCmd t_group = {
				.opcode = IMUSE_CMD_SET_PARAM,
				.args[0] = (intptr_t)sound2,
				.args[1] = IMUSE_PARAM_SOUND_GROUP,
				.args[2] = IMUSE_GROUP_DIPPED,
			};
			imuse_set_trigger(im, (intptr_t)sound1, marker, &t_start);
			imuse_set_trigger(im, (intptr_t)sound1, marker, &t_group);
			if (jumpHook2) {
				ImuseCmd t_hook = {
					.opcode = IMUSE_CMD_SET_HOOK,
					.args[0] = (intptr_t)sound2,
					.args[1] = jumpHook2,
				};
				imuse_set_trigger(im, (intptr_t)sound1, marker, &t_hook);
			}
		} else {
			imuse_pause(im);
			imuse_start_sound(im, (intptr_t)sound2, 0);
			imuse_set_param(im, (intptr_t)sound2, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED);
			if (jumpHook2)
				imuse_set_hook(im, (intptr_t)sound2, jumpHook2);
		}
		imuse_resume(im);
	}
}

static void DoChange(ChangeRef* cgp, void* sound1, void* sound2) {
	void* s1 = sound1;

	if (cgp->opcode) {
		/* Validate sound1 is playing */
		if (s1 && imuse_get_param(im, (intptr_t)s1, IM_PARAM_PRIORITY) <= 0)
			s1 = NULL;

		if (!s1) {
			/* Try to find any playing sound that isn't sound2.
			 * GetNextSound returns intptr_t; must NOT be narrowed
			 * through `int` or the 64-bit sound-id truncation
			 * creates a stable iteration point and the loop hangs. */
			do {
				s1 = (void*)imuse_next_sound(im, (intptr_t)s1);
			} while (s1 && (intptr_t)s1 != -1 && s1 != sound2);

			if (s1 == sound2) {
				imuse_set_hook(im, (intptr_t)s1, 0);
				return;
			}
			s1 = NULL;
			imuse_stop_all_sounds(im);
		}

		imuse_set_hook(im, (intptr_t)s1, 0);
		imuse_clear_trigger(im, (intptr_t)-1, -1, -1);

		switch (cgp->opcode) {
			case 1:
				ChgXfade(s1, sound2, cgp->arg1, cgp->arg2);
				break;
			case 2:
				ChgJumpMrk(s1, sound2, cgp->arg1, cgp->arg2, 0);
				break;
			case 4:
				ChgXfade(s1, sound2, cgp->arg1, cgp->arg2);
				{
					ImuseCmd t_attr = {
						.opcode = IMUSE_CMD_SET_PARAM,
						.args[0] = (intptr_t)sound2,
						.args[1] = IM_PARAM_ATTR,
						.args[2] = 0,
					};
					imuse_set_trigger(im, (intptr_t)sound2, 1, &t_attr);
				}
				break;
			case 5:
				imuse_set_param(im, (intptr_t)s1, IM_PARAM_ATTR, 64);
				break;
			case 6:
				ChgJumpMrk(s1, sound2, cgp->arg1, cgp->arg2, cgp->arg3);
				break;
			case 7:
				ChgJumpOnBeat(s1, sound2, cgp->arg1, cgp->arg2, cgp->arg3);
				break;
			default:
				ChgXfade(s1, sound2, 100, 0);
				break;
		}
	}
	imuse_filelist_unload(im, sound2);
}

static void DoJumpStart(ChangeRef* cgp, void* sound) {
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

	chunk = imuse_get_param(im, (intptr_t)sound, IM_PARAM_CHUNK);
	if (chunk < 0)
		return;
	meas = imuse_get_param(im, (intptr_t)sound, IM_PARAM_MEASURE);
	if (meas < 0)
		return;

	thresholdChunk = (int16_t)(int8_t)((cgp->arg3 >> 8) & 0xFF);
	thresholdMeas = (int16_t)(uint8_t)(cgp->arg3 & 0xFF);
	if (chunk <= thresholdChunk && meas <= thresholdMeas) {
		imuse_set_param(im, (intptr_t)sound, IM_PARAM_VOLALT, 0);
		jsc = (int16_t)(int8_t)((cgp->arg4 >> 8) & 0xFF);
		jsm = (int16_t)(uint8_t)(cgp->arg4 & 0xFF);
		imuse_midi_scan(im, (intptr_t)sound, jsc, jsm, 1, 0);
		imuse_fade_param(im, (intptr_t)sound, IM_PARAM_VOLALT, 127, 30);
	}
}

static CueRef* GetSequence(void) {
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
		case 13:
			return medicalSeq;
		case 14:
		case 15:
			return cut7Seq;
		case 16:
		case 17:
		case 18:
			return medalsSeq;
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
			return NULL;
	}
}

static ChangeRef* GetDefaultChangeRef(void) {
	if (currentSequence >= 9 && currentSequence <= 10)
		return &stateRefs[0].seqChanges[1];
	return &stateRefs[0].seqChanges[0];
}

__attribute__((unused)) static void DoCallback(void) {
	/* empty stub — placeholder for user callback mechanism */
}

__attribute__((unused)) static int16_t GetRandom(int16_t lo, int16_t hi) {
	int i, c;

	for (i = 0; i < 23; i++) {
		c = ((rseed2 & 0x20000000) == 0) ^ ((rseed1 & 0x40000000) != 0);
		rseed1 = rseed1 * 2 + c;
	}
	for (i = 0; i < 37; i++) {
		c = ((rseed1 & 0x20000000) == 0) ^ ((rseed2 & 0x40000000) != 0);
		rseed2 = rseed2 * 2 + c;
	}

	uint16_t raw = (uint16_t)(rseed1 + rseed2);
	return (int16_t)(((uint32_t)raw * (hi - lo + 1)) >> 16) + lo;
}
