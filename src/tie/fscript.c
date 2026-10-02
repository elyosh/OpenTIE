#include "tie/fscript.h"
#include "tie/fcallbk.h"
#include "tie_runtime/audio/imuse_api.h"
#include "tie_runtime/audio/imuse_session.h"

#include <imuse/filelist.h>
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <stdint.h>
#include <string.h>

enum {
	NUM_STATES = 12,
	SDP_STRIDE = 62,
	SDP_TERMINAL = 0x0C,
	SEQ_SMALLWIN = 2,
	PARAM_MARKER = 256,
};

/* SDP data arrays — extracted from TIE.EXE data segment */
// GLOBAL: TIE95 0xC1F4C
static SdpRecord introData[14] = { { "tro-01", "tro-01", 1, 0, { "tro-02", "", "", "" } },
								   { "tro-02", "tro-02", 1, 0, { "tro-03", "", "", "" } },
								   { "tro-03", "tro-03", 1, 0, { "tro-04", "", "", "" } },
								   { "tro-04", "tro-04", 1, 0, { "tro-05", "", "", "" } },
								   { "tro-05", "tro-05", 1, 0, { "tro-06", "", "", "" } },
								   { "tro-06", "tro-06", 1, 0, { "tro-07", "", "", "" } },
								   { "tro-07", "tro-07", 1, 0, { "tro-08", "", "", "" } },
								   { "tro-08", "tro-08", 1, 0, { "tro-01", "", "", "" } },
								   { "tro-in", "tro-in", 1, 0, { "tro-01", "", "", "" } },
								   { "wait-in", "wait-in", 1, 0, { "tro-01", "", "", "" } },
								   { "wait-seq", "wait-seq", 1, 0, { "tro-01", "", "", "" } },
								   { "", "", 1, 0, { "tro-in", "", "", "" } },
								   { "", "\x0c", 1, 0, { "wait-in", "", "", "" } },
								   { "", "\x0d", 1, 0, { "wait-seq", "", "", "" } } };
// GLOBAL: TIE95 0xC22B0
static SdpRecord waitingData[12] = {
	{ "wait-01", "wait-01", 3, 0, { "wait-02", "wait-05", "wait-06", "" } },
	{ "wait-02", "wait-02", 4, 0, { "wait-03", "wait-04", "wait-05", "wait-06" } },
	{ "wait-03", "wait-03", 4, 0, { "wait-01", "wait-02", "wait-04", "wait-05" } },
	{ "wait-04", "wait-04", 4, 0, { "wait-01", "wait-02", "wait-05", "wait-06" } },
	{ "wait-05", "wait-05", 4, 0, { "wait-01", "wait-02", "wait-04", "wait-06" } },
	{ "wait-06", "wait-06", 4, 0, { "wait-01", "wait-02", "wait-04", "wait-05" } },
	{ "wait-in", "wait-in", 1, 0, { "wait-01", "", "", "" } },
	{ "wait-seq", "wait-seq", 1, 0, { "wait-04", "", "", "" } },
	{ "", "", 1, 0, { "wait-01", "", "", "" } },
	{ "", "\x01", 1, 0, { "wait-01", "", "", "" } },
	{ "", "\x0c", 1, 0, { "wait-in", "", "", "" } },
	{ "", "\x0d", 1, 0, { "wait-seq", "", "", "" } }
};
// GLOBAL: TIE95 0xC2598
static SdpRecord rebellionData[25] = {
	{ "reb-01", "reb-01", 4, 0, { "reb-02", "reb-03", "reb-04", "reb-13" } },
	{ "reb-02", "reb-02", 4, 0, { "reb-03", "reb-04", "reb-06", "reb-07" } },
	{ "reb-03", "reb-03", 4, 0, { "reb-04", "reb-04", "reb-06", "reb-08" } },
	{ "reb-04", "reb-04", 4, 0, { "reb-03", "reb-03", "reb-06", "reb-11" } },
	{ "reb-05", "reb-05", 4, 0, { "reb-01", "reb-04", "reb-06", "reb-08" } },
	{ "reb-06", "reb-06", 4, 0, { "reb-07", "reb-08", "reb-09", "reb-12" } },
	{ "reb-07", "reb-07", 4, 0, { "reb-08", "reb-09", "reb-10", "reb-12" } },
	{ "reb-08", "reb-08", 4, 0, { "reb-01", "reb-04", "reb-09", "reb-12" } },
	{ "reb-09", "reb-09", 4, 0, { "reb-08", "reb-10", "reb-12", "reb-13" } },
	{ "reb-10", "reb-10", 4, 0, { "reb-01", "reb-04", "reb-11", "reb-13" } },
	{ "reb-11", "reb-11", 3, 0, { "reb-04", "reb-06", "reb-13", "" } },
	{ "reb-12", "reb-12", 2, 0, { "reb-07", "reb-10", "", "" } },
	{ "reb-13", "reb-13", 4, 0, { "reb-01", "reb-03", "reb-14", "reb-14" } },
	{ "reb-14", "reb-14", 1, 0, { "reb-10", "", "", "" } },
	{ "chal-in", "chal-in", 1, 0, { "reb-01", "", "", "" } },
	{ "tro-out", "tro-out", 1, 0, { "reb-01", "", "", "" } },
	{ "wait-out", "wait-out", 1, 0, { "reb-01", "", "", "" } },
	{ "succ-out", "succ-out", 1, 0, { "reb-01", "", "", "" } },
	{ "fail-out", "fail-out", 1, 0, { "reb-01", "", "", "" } },
	{ "", "", 1, 0, { "chal-in", "", "", "" } },
	{ "", "\x01", 1, 0, { "tro-out", "", "", "" } },
	{ "", "\x02", 1, 0, { "wait-out", "", "", "" } },
	{ "", "\x0b", 1, 0, { "succ-out", "", "", "" } },
	{ "", "\x0a", 1, 0, { "fail-out", "", "", "" } },
	{ "", "\x0c", 3, 0, { "reb-01", "reb-06", "reb-13", "" } }
};
// GLOBAL: TIE95 0xC2BA6
static SdpRecord policeData[20] = { { "pol-01", "pol-01", 4, 0, { "pol-02", "pol-02", "pol-04", "pol-09" } },
									{ "pol-02", "pol-02", 1, 0, { "pol-03", "", "", "" } },
									{ "pol-03", "pol-03", 4, 0, { "pol-02", "pol-04", "pol-05", "pol-06" } },
									{ "pol-04", "pol-04", 4, 0, { "pol-02", "pol-03", "pol-03", "pol-05" } },
									{ "pol-05", "pol-05", 4, 0, { "pol-02", "pol-02", "pol-03", "pol-04" } },
									{ "pol-06", "pol-06", 1, 0, { "pol-07", "", "", "" } },
									{ "pol-07", "pol-07", 4, 0, { "pol-01", "pol-08", "pol-09", "pol-09" } },
									{ "pol-08", "pol-08", 3, 0, { "pol-02", "pol-04", "pol-06", "" } },
									{ "pol-09", "pol-09", 2, 0, { "pol-01", "pol-08", "", "" } },
									{ "chal-in", "chal-in", 1, 0, { "pol-01", "", "", "" } },
									{ "tro-out", "tro-out", 1, 0, { "pol-01", "", "", "" } },
									{ "wait-out", "wait-out", 1, 0, { "pol-01", "", "", "" } },
									{ "succ-out", "succ-out", 1, 0, { "pol-01", "", "", "" } },
									{ "fail-out", "fail-out", 1, 0, { "pol-01", "", "", "" } },
									{ "", "", 1, 0, { "chal-in", "", "", "" } },
									{ "", "\x01", 1, 0, { "tro-out", "", "", "" } },
									{ "", "\x02", 1, 0, { "wait-out", "", "", "" } },
									{ "", "\x0b", 1, 0, { "succ-out", "", "", "" } },
									{ "", "\x0a", 1, 0, { "fail-out", "", "", "" } },
									{ "", "\x0c", 3, 0, { "pol-01", "pol-03", "pol-08", "" } } };
// GLOBAL: TIE95 0xC307E
static SdpRecord intrigueData[21] = {
	{ "intr-01", "intr-01", 4, 0, { "intr-02", "intr-03", "intr-07", "intr-10" } },
	{ "intr-02", "intr-02", 3, 0, { "intr-03", "intr-04", "intr-09", "" } },
	{ "intr-03", "intr-03", 3, 0, { "intr-04", "intr-08", "intr-10", "" } },
	{ "intr-04", "intr-04", 4, 0, { "intr-05", "intr-06", "intr-07", "intr-08" } },
	{ "intr-05", "intr-05", 4, 0, { "intr-01", "intr-03", "intr-06", "intr-09" } },
	{ "intr-06", "intr-06", 4, 0, { "intr-01", "intr-03", "intr-07", "intr-08" } },
	{ "intr-07", "intr-07", 3, 0, { "intr-02", "intr-05", "intr-09", "" } },
	{ "intr-08", "intr-08", 3, 0, { "intr-02", "intr-05", "intr-07", "" } },
	{ "intr-09", "intr-09", 3, 0, { "intr-01", "intr-04", "intr-10", "" } },
	{ "intr-10", "intr-10", 4, 0, { "intr-02", "intr-04", "intr-06", "intr-07" } },
	{ "chal-in", "chal-in", 1, 0, { "intr-01", "", "", "" } },
	{ "tro-out", "tro-out", 1, 0, { "intr-01", "", "", "" } },
	{ "wait-out", "wait-out", 1, 0, { "intr-01", "", "", "" } },
	{ "succ-out", "succ-out", 1, 0, { "intr-01", "", "", "" } },
	{ "fail-out", "fail-out", 1, 0, { "intr-01", "", "", "" } },
	{ "", "", 1, 0, { "chal-in", "", "", "" } },
	{ "", "\x01", 1, 0, { "tro-out", "", "", "" } },
	{ "", "\x02", 1, 0, { "wait-out", "", "", "" } },
	{ "", "\x0b", 1, 0, { "succ-out", "", "", "" } },
	{ "", "\x0a", 1, 0, { "fail-out", "", "", "" } },
	{ "", "\x0c", 3, 0, { "intr-01", "intr-04", "intr-07", "" } }
};
// GLOBAL: TIE95 0xC3594
static SdpRecord challengeData[27] = {
	{ "chal-01", "chal-01", 4, 0, { "chal-07", "chal-08", "chal-10", "chal-12" } },
	{ "chal-02", "chal-02", 3, 0, { "chal-03", "chal-10", "chal-13", "" } },
	{ "chal-03", "chal-03", 4, 0, { "chal-04", "chal-05", "chal-11", "chal-14" } },
	{ "chal-04", "chal-04", 4, 0, { "chal-12", "chal-13", "chal-14", "chal-15" } },
	{ "chal-05", "chal-05", 4, 0, { "chal-01", "chal-02", "chal-09", "chal-15" } },
	{ "chal-06", "chal-06", 4, 0, { "chal-01", "chal-07", "chal-09", "chal-11" } },
	{ "chal-07", "chal-07", 4, 0, { "chal-03", "chal-05", "chal-10", "chal-12" } },
	{ "chal-08", "chal-08", 4, 0, { "chal-06", "chal-07", "chal-09", "chal-16" } },
	{ "chal-09", "chal-09", 4, 0, { "chal-01", "chal-02", "chal-04", "chal-07" } },
	{ "chal-10", "chal-10", 4, 0, { "chal-04", "chal-08", "chal-12", "chal-13" } },
	{ "chal-11", "chal-11", 4, 0, { "chal-04", "chal-10", "chal-12", "chal-14" } },
	{ "chal-12", "chal-12", 4, 0, { "chal-05", "chal-06", "chal-13", "chal-16" } },
	{ "chal-13", "chal-13", 4, 0, { "chal-03", "chal-05", "chal-11", "chal-15" } },
	{ "chal-14", "chal-14", 4, 0, { "chal-05", "chal-06", "chal-08", "chal-12" } },
	{ "chal-15", "chal-15", 3, 0, { "chal-01", "chal-08", "chal-16", "" } },
	{ "chal-16", "chal-16", 3, 0, { "chal-02", "chal-06", "chal-11", "" } },
	{ "chal-in", "chal-in", 1, 0, { "chal-01", "", "", "" } },
	{ "tro-out", "tro-out", 1, 0, { "chal-01", "", "", "" } },
	{ "wait-out", "wait-out", 1, 0, { "chal-01", "", "", "" } },
	{ "succ-out", "succ-out", 1, 0, { "chal-01", "", "", "" } },
	{ "fail-out", "fail-out", 1, 0, { "chal-01", "", "", "" } },
	{ "", "", 1, 0, { "chal-in", "", "", "" } },
	{ "", "\x01", 1, 0, { "tro-out", "", "", "" } },
	{ "", "\x02", 1, 0, { "wait-out", "", "", "" } },
	{ "", "\x0b", 1, 0, { "succ-out", "", "", "" } },
	{ "", "\x0a", 1, 0, { "fail-out", "", "", "" } },
	{ "", "\x0c", 3, 0, { "chal-01", "chal-08", "chal-12", "" } }
};
// GLOBAL: TIE95 0xC3C1E
static SdpRecord confidentData[25] = {
	{ "conf-01", "conf-01", 2, 0, { "conf-02", "conf-13", "", "" } },
	{ "conf-02", "conf-02", 4, 0, { "conf-03", "conf-06", "conf-09", "conf-13" } },
	{ "conf-03", "conf-03", 4, 0, { "conf-06", "conf-08", "conf-10", "conf-13" } },
	{ "conf-04", "conf-04", 4, 0, { "conf-02", "conf-05", "conf-09", "conf-13" } },
	{ "conf-05", "conf-05", 4, 0, { "conf-02", "conf-03", "conf-06", "conf-14" } },
	{ "conf-06", "conf-06", 4, 0, { "conf-01", "conf-04", "conf-07", "conf-08" } },
	{ "conf-07", "conf-07", 4, 0, { "conf-02", "conf-04", "conf-08", "conf-09" } },
	{ "conf-08", "conf-08", 1, 0, { "conf-09", "", "", "" } },
	{ "conf-09", "conf-09", 3, 0, { "conf-01", "conf-10", "conf-13", "" } },
	{ "conf-10", "conf-10", 2, 0, { "conf-11", "conf-12", "", "" } },
	{ "conf-11", "conf-11", 3, 0, { "conf-01", "conf-04", "conf-12", "" } },
	{ "conf-12", "conf-12", 4, 0, { "conf-01", "conf-06", "conf-08", "conf-13" } },
	{ "conf-13", "conf-13", 3, 0, { "conf-06", "conf-06", "conf-07", "" } },
	{ "conf-14", "conf-14", 2, 0, { "conf-02", "conf-09", "conf-13", "" } },
	{ "chal-in", "chal-in", 1, 0, { "conf-06", "", "", "" } },
	{ "tro-out", "tro-out", 1, 0, { "conf-06", "", "", "" } },
	{ "wait-out", "wait-out", 1, 0, { "conf-06", "", "", "" } },
	{ "succ-out", "succ-out", 1, 0, { "conf-06", "", "", "" } },
	{ "fail-out", "fail-out", 1, 0, { "conf-06", "", "", "" } },
	{ "", "", 1, 0, { "chal-in", "", "", "" } },
	{ "", "\x01", 1, 0, { "tro-out", "", "", "" } },
	{ "", "\x02", 1, 0, { "wait-out", "", "", "" } },
	{ "", "\x0b", 1, 0, { "succ-out", "", "", "" } },
	{ "", "\x0a", 1, 0, { "fail-out", "", "", "" } },
	{ "", "\x0c", 1, 0, { "conf-06", "", "", "" } }
};
// GLOBAL: TIE95 0xC422C
static SdpRecord panicData[23] = {
	{ "panic-01", "panic-01", 2, 0, { "panic-05", "panic-09", "", "" } },
	{ "panic-02", "panic-02", 2, 0, { "panic-01", "panic-03", "", "" } },
	{ "panic-03", "panic-03", 2, 0, { "panic-05", "panic-09", "", "" } },
	{ "panic-04", "panic-04", 2, 0, { "panic-08", "panic-10", "", "" } },
	{ "panic-05", "panic-05", 2, 0, { "panic-06", "panic-10", "", "" } },
	{ "panic-06", "panic-06", 3, 0, { "panic-10", "panic-11", "panic-12", "" } },
	{ "panic-07", "panic-07", 3, 0, { "panic-09", "panic-10", "panic-12", "" } },
	{ "panic-08", "panic-08", 2, 0, { "panic-05", "panic-12", "", "" } },
	{ "panic-09", "panic-09", 2, 0, { "panic-04", "panic-07", "", "" } },
	{ "panic-10", "panic-10", 2, 0, { "panic-06", "panic-11", "", "" } },
	{ "panic-11", "panic-11", 2, 0, { "panic-02", "panic-08", "", "" } },
	{ "panic-12", "panic-12", 2, 0, { "panic-05", "panic-10", "", "" } },
	{ "chal-in", "chal-in", 1, 0, { "panic-06", "", "", "" } },
	{ "tro-out", "tro-out", 1, 0, { "panic-06", "", "", "" } },
	{ "wait-out", "wait-out", 1, 0, { "panic-06", "", "", "" } },
	{ "succ-out", "succ-out", 1, 0, { "panic-06", "", "", "" } },
	{ "fail-out", "fail-out", 1, 0, { "panic-06", "", "", "" } },
	{ "", "", 1, 0, { "chal-in", "", "", "" } },
	{ "", "\x01", 1, 0, { "tro-out", "", "", "" } },
	{ "", "\x02", 1, 0, { "wait-out", "", "", "" } },
	{ "", "\x0b", 1, 0, { "succ-out", "", "", "" } },
	{ "", "\x0a", 1, 0, { "fail-out", "", "", "" } },
	{ "", "\x0c", 1, 0, { "panic-11", "", "", "" } }
};
// GLOBAL: TIE95 0xC47BE
static SdpRecord climaxData[18] = { { "clim-01", "clim-01", 1, 0, { "clim-02", "", "", "" } },
									{ "clim-02", "clim-02", 1, 0, { "clim-03", "", "", "" } },
									{ "clim-03", "clim-03", 1, 0, { "clim-04", "", "", "" } },
									{ "clim-04", "clim-04", 1, 0, { "clim-05", "", "", "" } },
									{ "clim-05", "clim-05", 1, 0, { "clim-06", "", "", "" } },
									{ "clim-06", "clim-06", 1, 0, { "clim-07", "", "", "" } },
									{ "clim-07", "clim-07", 1, 0, { "clim-01", "", "", "" } },
									{ "chal-in", "chal-in", 1, 0, { "clim-01", "", "", "" } },
									{ "tro-out", "tro-out", 1, 0, { "clim-01", "", "", "" } },
									{ "wait-out", "wait-out", 1, 0, { "clim-01", "", "", "" } },
									{ "succ-out", "succ-out", 1, 0, { "clim-01", "", "", "" } },
									{ "fail-out", "fail-out", 1, 0, { "clim-01", "", "", "" } },
									{ "", "", 1, 0, { "chal-in", "", "", "" } },
									{ "", "\x01", 1, 0, { "tro-out", "", "", "" } },
									{ "", "\x02", 1, 0, { "wait-out", "", "", "" } },
									{ "", "\x0b", 1, 0, { "succ-out", "", "", "" } },
									{ "", "\x0a", 1, 0, { "fail-out", "", "", "" } },
									{ "", "\x0c", 1, 0, { "clim-01", "", "", "" } } };
// GLOBAL: TIE95 0xC4C1A
static SdpRecord failureData[10] = {
	{ "fail-01", "fail-01", 4, 0, { "fail-02", "fail-02", "fail-03", "fail-05" } },
	{ "fail-02", "fail-02", 4, 0, { "fail-03", "fail-03", "fail-04", "fail-05" } },
	{ "fail-03", "fail-03", 1, 0, { "fail-04", "", "", "" } },
	{ "fail-04", "fail-04", 2, 0, { "fail-01", "fail-05", "", "" } },
	{ "fail-05", "fail-05", 2, 0, { "fail-06", "fail-07", "", "" } },
	{ "fail-06", "fail-06", 1, 0, { "fail-07", "", "", "" } },
	{ "fail-07", "fail-07", 1, 0, { "fail-01", "", "", "" } },
	{ "fail-in", "fail-in", 1, 0, { "fail-01", "", "", "" } },
	{ "", "", 1, 0, { "fail-01", "", "", "" } },
	{ "", "\x0c", 1, 0, { "fail-in", "", "", "" } }
};
// GLOBAL: TIE95 0xC4E86
static SdpRecord successData[11] = {
	{ "succ-01", "succ-01", 1, 0, { "succ-02", "", "", "" } },
	{ "succ-02", "succ-02", 2, 0, { "succ-03", "succ-04", "", "" } },
	{ "succ-03", "succ-03", 2, 0, { "succ-01", "succ-04", "", "" } },
	{ "succ-04", "succ-04", 1, 0, { "succ-05", "", "", "" } },
	{ "succ-05", "succ-05", 3, 0, { "succ-01", "succ-06", "succ-08", "" } },
	{ "succ-06", "succ-06", 2, 0, { "succ-04", "succ-07", "", "" } },
	{ "succ-07", "succ-07", 4, 0, { "succ-01", "succ-02", "succ-04", "succ-08" } },
	{ "succ-08", "succ-08", 2, 0, { "succ-02", "succ-06", "", "" } },
	{ "succ-in", "succ-in", 1, 0, { "succ-01", "", "", "" } },
	{ "", "", 1, 0, { "succ-01", "", "", "" } },
	{ "", "\x0c", 1, 0, { "succ-in", "", "", "" } }
};

/* Sequence names: 10-byte entries indexed by seq_id */
// GLOBAL: TIE95 0xC5130
static char sequenceData[18][10] = { "",         "s-win-lg", "        ", "s-los-lg", "s-los-sm", "s-ob1-pa",
									 "s-ob1-fa", "s-ob2-pa", "s-ob2-fa", "s-ob3-pa", "s-emp-lg", "s-emp-sm",
									 "s-reb-lg", "s-reb-sm", "s-neu-lg", "s-neu-sm", "s-eject",  "s-hyper" };

/* Sequence priorities: indexed by seq_id */
// GLOBAL: TIE95 0xC51E4
static int32_t sequencePriorities[18] = { 0, 10, 2, 9, 1, 15, 14, 13, 12, 11, 8, 5, 7, 4, 6, 3, 20, 20 };

/* SmallWin SDP record: 4 random destinations */
// GLOBAL: TIE95 0xC522C
static SdpRecord smallWin = { "", "", 4, 0, { "s-win-1", "s-win-2", "s-win-3", "s-win-4" } };

/* Channel buildup bitmasks — indexed by attributes[0] (buildup level).
 * Each bit enables a MIDI channel. Used by CbSetChannels in fcallbk.c. */
// GLOBAL: TIE95 0xC526A
uint16_t introBuildup[6] = { 0x9DC3, 0xBDC3, 0xBD47, 0xFF46, 0xFF76, 0xFF7E };
// GLOBAL: TIE95 0xC5276
uint16_t waitingBuildup[7] = { 0x7D83, 0xFDC3, 0xFDC5, 0xFFC5, 0xFBF5, 0xFBFD, 0x0000 };

/* SDP array pointers: sdpArrays[state] -> SdpRecord chain */
// GLOBAL: TIE95 0xD4958
static SdpRecord* sdpArrays[NUM_STATES];

// GLOBAL: TIE95 0xD4988
static void* initDataPtr;
// GLOBAL: TIE95 0xD49A4
int32_t currentState;
// GLOBAL: TIE95 0xD4998
int32_t playingState;
// GLOBAL: TIE95 0xD49AC
intptr_t currentID;
// GLOBAL: TIE95 0xD49B0
intptr_t nextID;
// GLOBAL: TIE95 0xD499C
intptr_t sequenceID;
// GLOBAL: TIE95 0xD4994
int32_t currentSequence;
// GLOBAL: TIE95 0xD49A8
int32_t sequencePri;
// GLOBAL: TIE95 0xD49A0
static SdpRecord* currentSdp;
// GLOBAL: TIE95 0xD498C
static int32_t rseed1;
// GLOBAL: TIE95 0xD4990
static int32_t rseed2;
// GLOBAL: TIE95 0xD49B8
int16_t attributes[2];

/* Forward declarations for internal functions */
static void fscript_ChangeState(int new_state);
static void fscript_PlaySequence(int seq_id);
static SdpRecord* fscript_SelectSdp(SdpRecord* sdp, int state);
static char* fscript_SelectSequence(int seq_id);
static int fscript_ChooseDest(SdpRecord* sdp);
static int16_t fscript_GetRandom(int16_t lo, int16_t hi);

/* ================================================================
 * Public API
 * ================================================================ */

// FUNCTION: TIE95 0x23EF8
int16_t fscript_MsStartScript(void* init_data) {
	fcallbk_CbInitialize();

	initDataPtr = init_data;
	sdpArrays[0] = NULL;
	sdpArrays[1] = introData;
	sdpArrays[2] = waitingData;
	sdpArrays[3] = rebellionData;
	sdpArrays[4] = policeData;
	sdpArrays[5] = intrigueData;
	sdpArrays[6] = challengeData;
	sdpArrays[7] = confidentData;
	sdpArrays[8] = panicData;
	sdpArrays[9] = climaxData;
	sdpArrays[10] = failureData;
	sdpArrays[11] = successData;

	sequenceID = 0;
	nextID = 0;
	currentID = 0;
	playingState = 0;
	currentState = 0;
	currentSequence = 0;
	sequencePri = 0;

	/* Seed PRNG from stack addresses (non-deterministic) */
	rseed1 = TieImuse_AddressSeed(&rseed2);
	rseed2 = ~TieImuse_AddressSeed(&rseed1);

	return 0;
}

// FUNCTION: TIE95 0x23FF4
int16_t fscript_MsRefreshScript(void) {
	if (currentState) {
		if (!nextID) {
			currentSdp = fscript_SelectSdp(currentSdp, currentState);
			nextID = filelist_ImLoadSound(currentSdp->sound_name);
			if (!nextID) {
				lolevel_ImStopAllSounds();
				currentState = 0;
				lolevel_ImPrintf("Unable to load file ");
				lolevel_ImPrintf(currentSdp->sound_name);
				lolevel_ImPrintf("...");
			}
		}
		filelist_ImFlushSounds();
	}
	return 0;
}

// FUNCTION: TIE95 0x2406C
int16_t fscript_MsSetState(int16_t new_state) {
	if (new_state >= 0 && new_state < 12 && new_state != currentState)
		fscript_ChangeState(new_state);
	return currentState;
}

// FUNCTION: TIE95 0x2408C
int16_t fscript_MsSetSequence(int16_t seq_id) {
	if (seq_id > 0 && lolevel_ImGetParam(currentID, PARAM_MARKER) > 0)
		fscript_PlaySequence(seq_id);
	return currentSequence;
}

// FUNCTION: TIE95 0x240BC
int16_t fscript_MsSetAttribute(int16_t attr_id, int16_t value) {
	if (attr_id < 1) {
		if (value >= 0)
			attributes[attr_id] = value;
		return attributes[attr_id];
	}
	return 0;
}

/* ================================================================
 * Internal functions
 * ================================================================ */

/*
 * Core state transition logic. Three cases:
 *   (1) From idle (currentState==0): enter new state, walk SDP chain to
 *       find initial + next sounds, start music with trigger callback.
 *   (2) From active to 0: stop everything, zero all state.
 *   (3) From active to different state: find transition SDP matching the
 *       target, preload it, swap next sound under pause.
 */
// FUNCTION: TIE95 0x240E4
static void fscript_ChangeState(int new_state) {
	SdpRecord* sdp;
	intptr_t new_handle;

	if (currentState == 0) {
		/* Case 1: from idle — enter new state */
		currentState = new_state;

		/* Walk to end of named records, then to the first playable or
		 * terminal record */
		currentSdp = sdpArrays[new_state];
		while (currentSdp->name[0])
			currentSdp++;
		while (currentSdp->sound_name[0] && (int8_t)currentSdp->sound_name[0] != SDP_TERMINAL)
			currentSdp++;

		/* Load current sound */
		currentSdp = fscript_SelectSdp(currentSdp, new_state);
		currentID = filelist_ImLoadSound(currentSdp->sound_name);
		if (!currentID) {
			currentState = 0;
			lolevel_ImPrintf("Unable to load file ");
			lolevel_ImPrintf(currentSdp->sound_name);
			lolevel_ImPrintf("...");
			return;
		}

		/* Select and preload next sound */
		currentSdp = fscript_SelectSdp(currentSdp, new_state);
		nextID = filelist_ImLoadSound(currentSdp->sound_name);
		if (!nextID) {
			currentState = 0;
			lolevel_ImPrintf("Unable to load file ");
			lolevel_ImPrintf(currentSdp->sound_name);
			lolevel_ImPrintf("...");
			return;
		}

		/* Start current music and set trigger for callback-driven transition */
		hilevel_ImStartMusic(currentID, 0);
		filelist_ImUnloadSound(currentID);
		lolevel_ImSetTrigger((intptr_t)currentID, 0, (intptr_t)fcallbk_CbDoCallback);
		playingState = currentState;
		fcallbk_CbSetChannels();
		return;
	}

	if (new_state == 0) {
		/* Case 2: from active to idle — stop everything */
		lolevel_ImStopAllSounds();
		filelist_ImUnloadAll();
		sequenceID = 0;
		nextID = 0;
		currentID = 0;
		playingState = 0;
		currentState = 0;
		currentSequence = 0;
		sequencePri = 0;
		return;
	}

	/* Active-state transition: walk to end of named records in the new
	 * state's chain, then find the transition record matching the
	 * outgoing state or the generic terminal. */
	sdp = sdpArrays[new_state];
	while (sdp->name[0])
		sdp++;
	for (;;) {
		int code = (int8_t)sdp->sound_name[0];

		if (code == currentState || code == SDP_TERMINAL)
			break;
		sdp++;
	}

	sdp = fscript_SelectSdp(sdp, new_state);
	if (sdp == currentSdp)
		sdp = fscript_SelectSdp(sdp, new_state);

	/* Load the transition sound */
	new_handle = filelist_ImLoadSound(sdp->sound_name);
	if (!new_handle) {
		lolevel_ImStopAllSounds();
		currentState = 0;
		lolevel_ImPrintf("Unable to load file ");
		lolevel_ImPrintf(sdp->sound_name);
		lolevel_ImPrintf("...");
		return;
	}

	/* Swap next sound under pause */
	lolevel_ImPause();
	filelist_ImUnloadSound(nextID);
	nextID = new_handle;
	currentSdp = sdp;
	currentState = new_state;
	lolevel_ImResume();
}

/*
 * Start a music sequence. Checks priority against current sequence,
 * unloads old if lower priority. For intro/waiting states, also sets
 * up the next SDP continuation sound.
 */
// FUNCTION: TIE95 0x24370
static void fscript_PlaySequence(int seq_id) {
	char* seq_name;
	intptr_t handle;

	lolevel_ImPause();
	if (sequenceID) {
		if (sequencePri >= sequencePriorities[seq_id]) {
			lolevel_ImResume();
			return;
		}
		filelist_ImUnloadSound(sequenceID);
		sequenceID = 0;
		currentSequence = 0;
	}
	lolevel_ImResume();

	seq_name = fscript_SelectSequence(seq_id);
	handle = filelist_ImLoadSound(seq_name);
	if (!handle) {
		lolevel_ImStopAllSounds();
		currentState = 0;
		lolevel_ImPrintf("Unable to load sequence ");
		lolevel_ImPrintf(seq_name);
		lolevel_ImPrintf("...");
		return;
	}

	/* Only start if current sound isn't already at a marker */
	if (lolevel_ImGetParam(handle, PARAM_MARKER) > 0)
		return;

	sequenceID = handle;
	currentSequence = seq_id;
	sequencePri = sequencePriorities[seq_id];

	/* For intro (1) or waiting (2) states, set up continuation */
	if (currentState == 1 || currentState == 2) {
		SdpRecord* chain = sdpArrays[currentState];
		SdpRecord* p;
		SdpRecord* cont;
		intptr_t cont_handle;

		if (!chain)
			return;

		/* Walk to chain end, then find terminal (0x0D = 13) record */
		p = chain;
		while (p->name[0])
			p++;
		while ((uint8_t)p->sound_name[0] != 13)
			p++;

		cont = fscript_SelectSdp(p, currentState);
		cont_handle = filelist_ImLoadSound(cont->sound_name);
		if (!cont_handle) {
			lolevel_ImStopAllSounds();
			currentState = 0;
			lolevel_ImPrintf("Unable to load file ");
			lolevel_ImPrintf(cont->sound_name);
			lolevel_ImPrintf("...");
			return;
		}

		lolevel_ImPause();
		filelist_ImUnloadSound(nextID);
		nextID = cont_handle;
		currentSdp = cont;
		lolevel_ImResume();
	}
}

/*
 * Select an SDP record from the state's chain. Uses ChooseDest to pick
 * a random destination, then searches the chain for a matching name.
 */
// FUNCTION: TIE95 0x244DC
static SdpRecord* fscript_SelectSdp(SdpRecord* sdp, int state) {
	const char* dest_name;
	SdpRecord* current = sdp;
	int i;

	if (!sdp->num_dests) {
		lolevel_ImPrintf("Script Err: no dest count...");
		return sdp;
	}

	dest_name = sdp->dest_names[fscript_ChooseDest(sdp)];

	sdp = sdpArrays[state];
	while (sdp->name[0]) {
		i = 0;
		while (sdp->name[i] && sdp->name[i] == dest_name[i])
			i++;
		if (!sdp->name[i] && !dest_name[i])
			return sdp;
		sdp++;
	}

	lolevel_ImPrintf("Unable to find sdp for ");
	lolevel_ImPrintf(dest_name);
	lolevel_ImPrintf("...");
	return current;
}

/*
 * Select a sequence sound name. For seq_id 2 (smallWin), uses random
 * destination selection. For all others, returns sequenceData[seq_id].
 */
// FUNCTION: TIE95 0x24588
static char* fscript_SelectSequence(int seq_id) {
	SdpRecord* sdp;

	if (seq_id != SEQ_SMALLWIN)
		return sequenceData[seq_id];
	sdp = &smallWin;
	return sdp->dest_names[fscript_ChooseDest(sdp)];
}

/*
 * Random destination picker with exhaustive bitmask tracking.
 * sdp->num_dests = number of destinations (max 7).
 * sdp->used_mask = bitmask of remaining choices (reset when exhausted).
 * Picks uniformly from remaining, clears the chosen bit.
 */
// FUNCTION: TIE95 0x245C0
static int fscript_ChooseDest(SdpRecord* sdp) {
	/* Full bitmask table: full_masks[n] = (1 << n) - 1, for n = 0..7 */
	uint8_t full_masks[8] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F };
	int16_t avail;
	uint8_t mask;
	int i;
	int pick;
	uint8_t pick_mask;
	int result;

	if (!sdp->used_mask)
		sdp->used_mask = full_masks[sdp->num_dests];

	/* Count available destinations */
	avail = 0;
	mask = 1;
	for (i = 0; i < sdp->num_dests; i++) {
		if (mask & sdp->used_mask)
			avail++;
		mask <<= 1;
	}

	/* Pick a random index among available */
	pick = fscript_GetRandom(0, avail - 1);

	/* Walk destinations, counting only available ones */
	pick_mask = 1;

	for (result = 0; result < sdp->num_dests; result++) {
		if (pick_mask & sdp->used_mask) {
			if (pick == 0) {
				uint8_t inv = ~pick_mask;
				sdp->used_mask &= inv;
				/* Reset mask when exhausted */
				if (!sdp->used_mask)
					sdp->used_mask = inv & full_masks[sdp->num_dests];
				break;
			}
			pick--;
		}
		pick_mask <<= 1;
	}

	if (pick) {
		lolevel_ImPrintf("Script Err: couldn't find bit");
		return 0;
	}
	return result;
}

/*
 * LFSR-based PRNG. Advances two 32-bit seeds (23 + 37 iterations)
 * with cross-feedback XOR, then scales to [lo..hi] range.
 */
// FUNCTION: TIE95 0x2467C
static int16_t fscript_GetRandom(int16_t lo, int16_t hi) {
	/* LFSR step: shift left by 1, OR in a tap-XOR feedback bit.
	 * Done in uint32 so the doubling matches the binary's `shl`/`add`
	 * with modular wraparound — signed `2 * rseed` is UB once the
	 * value exceeds INT32_MAX/2, which is reached almost immediately
	 * because the seeds at init are 32-bit-truncated host pointers. */
	int i, c;
	uint16_t raw;

	for (i = 0; i < 23; i++) {
		c = ((rseed1 & 0x40000000) != 0) ^ ((rseed2 & 0x20000000) == 0);
		rseed1 = (int32_t)((uint32_t)rseed1 * 2);
		rseed1 += c;
	}
	for (i = 0; i < 37; i++) {
		c = ((rseed2 & 0x40000000) != 0) ^ ((rseed1 & 0x20000000) == 0);
		rseed2 = (int32_t)((uint32_t)rseed2 * 2);
		rseed2 += c;
	}

	/* Sum the low halves; only 16 bits of the seed sum are used. */
	return lo + (((uint32_t)(hi - lo + 1) * (uint16_t)((uint16_t)rseed2 + (uint16_t)rseed1)) >> 16);
}
