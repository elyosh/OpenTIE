#include "tie/fscript.h"
#include "tie/fcallbk.h"
#include "tie_runtime/audio/imuse_session.h"

#include <imuse/filelist.h>
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>

#include <stdint.h>
#include <string.h>

/* Debug logging is disabled. */
static inline void imuse_ImPrintf(imuse_t* im, const char* fmt, ...) {
	(void)im;
	(void)fmt;
}

#define NUM_STATES 12
#define SDP_STRIDE 62
#define SDP_TERMINAL 0x0C
#define SEQ_SMALLWIN 2
#define PARAM_MARKER 256

/* Full bitmask table: full_masks[n] = (1 << n) - 1, for n = 0..7 */
static const uint8_t full_masks[8] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F };

/* SDP data arrays — extracted from TIE.EXE data segment */
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
static char sequenceData[18][10] = { "",         "s-win-lg", "        ", "s-los-lg", "s-los-sm", "s-ob1-pa",
									 "s-ob1-fa", "s-ob2-pa", "s-ob2-fa", "s-ob3-pa", "s-emp-lg", "s-emp-sm",
									 "s-reb-lg", "s-reb-sm", "s-neu-lg", "s-neu-sm", "s-eject",  "s-hyper" };

/* Sequence priorities: indexed by seq_id */
// GLOBAL: TIE 0xC51E4
static int32_t sequencePriorities[18] = { 0, 10, 2, 9, 1, 15, 14, 13, 12 };

/* SmallWin SDP record: 4 random destinations */
static SdpRecord smallWin = {
	.num_dests = 4,
	.dest_names = { "s-win-1", "s-win-2", "s-win-3", "s-win-4" },
};

/* Channel buildup bitmasks — indexed by attributes[0] (buildup level).
 * Each bit enables a MIDI channel. Used by CbSetChannels in fcallbk.c. */
// GLOBAL: TIE 0xC526A
uint16_t introBuildup[6] = { 0x9DC3, 0xBDC3, 0xBD47, 0xFF46, 0xFF76, 0xFF7E };
// GLOBAL: TIE 0xC5276
uint16_t waitingBuildup[7] = { 0x7D83, 0xFDC3, 0xFDC5, 0xFFC5, 0xFBF5, 0xFBFD, 0x0000 };

/* SDP array pointers: sdpArrays[state] -> SdpRecord chain */
// GLOBAL: TIE 0xD4958
static SdpRecord* sdpArrays[NUM_STATES];

// GLOBAL: TIE 0xD4988
static void* initDataPtr;
// GLOBAL: TIE 0xD49A4
int32_t currentState;
// GLOBAL: TIE 0xD4998
int32_t playingState;
// GLOBAL: TIE 0xD49AC
void* currentID;
// GLOBAL: TIE 0xD49B0
void* nextID;
// GLOBAL: TIE 0xD499C
void* sequenceID;
// GLOBAL: TIE 0xD4994
int32_t currentSequence;
// GLOBAL: TIE 0xD49A8
int32_t sequencePri;
// GLOBAL: TIE 0xD49A0
static SdpRecord* currentSdp;
// GLOBAL: TIE 0xD498C
static int32_t rseed1;
// GLOBAL: TIE 0xD4990
static int32_t rseed2;
// GLOBAL: TIE 0xD49B8
int16_t attributes[2];

/* Forward declarations for internal functions */
static void change_state(int new_state);
static void play_sequence(int seq_id);
static SdpRecord* select_sdp(SdpRecord* sdp, int state);
static char* select_sequence(int seq_id);
static int choose_dest(SdpRecord* sdp);
static int16_t get_random(int16_t lo, int16_t hi);

/* Walk an SDP chain to the end (first record with name[0]==0) */
static SdpRecord* sdp_chain_end(SdpRecord* chain) {
	SdpRecord* p = chain;
	while (p->name[0])
		p++;
	return p;
}

/* ================================================================
 * Public API
 * ================================================================ */

// FUNCTION: TIE 0x23EF8
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
	rseed1 = (int32_t)(intptr_t)&rseed2;
	rseed2 = ~(int32_t)(intptr_t)&rseed1;

	return 0;
}

int16_t fscript_MsStopScript(void) {
	imuse_stop_all_sounds(im);
	imuse_filelist_unload_all(im);
	return 0;
}

int16_t fscript_MsSetCuePoint(void) { return 0; }

// FUNCTION: TIE 0x23FF4
int16_t fscript_MsRefreshScript(void) {
	if (currentState) {
		if (!nextID) {
			currentSdp = select_sdp(currentSdp, currentState);
			nextID = imuse_filelist_load(im, currentSdp->sound_name);
			if (!nextID) {
				imuse_stop_all_sounds(im);
				currentState = 0;
				imuse_ImPrintf(im, "Unable to load file ");
				imuse_ImPrintf(im, currentSdp->sound_name);
				imuse_ImPrintf(im, "\n");
			}
		}
		imuse_filelist_flush(im);
	}
	return 0;
}

// FUNCTION: TIE 0x2406C
int16_t fscript_MsSetState(int16_t new_state) {
	if (new_state >= 0 && new_state < NUM_STATES && new_state != currentState)
		change_state(new_state);
	return currentState;
}

// FUNCTION: TIE 0x2408C
int16_t fscript_MsSetSequence(int16_t seq_id) {
	if (seq_id > 0 && imuse_get_param(im, (intptr_t)currentID, PARAM_MARKER) > 0)
		play_sequence(seq_id);
	return currentSequence;
}

// FUNCTION: TIE 0x240BC
int16_t fscript_MsSetAttribute(int16_t attr_id, int16_t value) {
	if (attr_id >= 1)
		return 0;
	if (value >= 0)
		attributes[attr_id] = value;
	return attributes[attr_id];
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
static void change_state(int new_state) {
	if (currentState == 0) {
		/* Case 1: from idle — enter new state */
		currentState = new_state;
		SdpRecord* chain = sdpArrays[new_state];
		if (!chain)
			return;

		/* Walk to end of named records */
		SdpRecord* p = sdp_chain_end(chain);

		/* Find first playable or terminal record past the chain end */
		while (p->sound_name[0] && (uint8_t)p->sound_name[0] != SDP_TERMINAL)
			p++;

		currentSdp = p;
		SdpRecord* selected = select_sdp(p, new_state);
		currentSdp = selected;

		/* Load current sound */
		currentID = imuse_filelist_load(im, selected->sound_name);
		if (!currentID) {
			currentState = 0;
			imuse_ImPrintf(im, "Unable to load file ");
			imuse_ImPrintf(im, selected->sound_name);
			imuse_ImPrintf(im, "\n");
			return;
		}

		/* Select and preload next sound */
		SdpRecord* next_sdp = select_sdp(selected, new_state);
		currentSdp = next_sdp;
		nextID = imuse_filelist_load(im, next_sdp->sound_name);
		if (!nextID) {
			currentState = 0;
			imuse_ImPrintf(im, "Unable to load file ");
			imuse_ImPrintf(im, next_sdp->sound_name);
			imuse_ImPrintf(im, "\n");
			return;
		}

		/* Start current music and set trigger for callback-driven transition */
		imuse_start_music(im, currentID);
		imuse_filelist_unload(im, currentID);
		{
			ImuseCmd cb = { .opcode = (intptr_t)fcallbk_CbDoCallback };
			imuse_set_trigger(im, (intptr_t)currentID, 0, &cb);
		}
		playingState = currentState;
		fcallbk_CbSetChannels();
		return;
	}

	if (new_state == 0) {
		/* Case 2: from active to idle — stop everything */
		imuse_stop_all_sounds(im);
		imuse_filelist_unload_all(im);
		nextID = 0;
		sequenceID = 0;
		currentID = 0;
		playingState = 0;
		currentState = 0;
		currentSequence = 0;
		sequencePri = 0;
		return;
	}

	/* Active-state transition: search the new state's chain for the
	 * outgoing state or the generic terminal. */
	SdpRecord* chain = sdpArrays[new_state];

	/* Walk to end of named records in the NEW state's chain */
	SdpRecord* p = sdp_chain_end(chain);

	/* Find transition record matching the OUTGOING state or generic terminal */
	while ((uint8_t)p->sound_name[0] != (uint8_t)currentState && (uint8_t)p->sound_name[0] != SDP_TERMINAL)
		p++;

	SdpRecord* selected = select_sdp(p, new_state);
	if (selected == currentSdp)
		selected = select_sdp(selected, new_state);

	/* Load the transition sound */
	char* snd_name = selected->sound_name;
	void* new_handle = imuse_filelist_load(im, snd_name);
	if (!new_handle) {
		imuse_stop_all_sounds(im);
		currentState = 0;
		imuse_ImPrintf(im, "Unable to load file ");
		imuse_ImPrintf(im, snd_name);
		imuse_ImPrintf(im, "\n");
		return;
	}

	/* Swap next sound under pause */
	imuse_pause(im);
	imuse_filelist_unload(im, nextID);
	nextID = new_handle;
	currentSdp = selected;
	currentState = new_state;
	imuse_resume(im);
}

/*
 * Start a music sequence. Checks priority against current sequence,
 * unloads old if lower priority. For intro/waiting states, also sets
 * up the next SDP continuation sound.
 */
static void play_sequence(int seq_id) {
	imuse_pause(im);
	if (sequenceID) {
		if (sequencePri >= sequencePriorities[seq_id]) {
			imuse_resume(im);
			return;
		}
		imuse_filelist_unload(im, sequenceID);
		sequenceID = 0;
		currentSequence = 0;
	}
	imuse_resume(im);

	char* seq_name = select_sequence(seq_id);
	void* handle = imuse_filelist_load(im, seq_name);
	if (!handle) {
		imuse_stop_all_sounds(im);
		currentState = 0;
		imuse_ImPrintf(im, "Unable to load sequence ");
		imuse_ImPrintf(im, seq_name);
		imuse_ImPrintf(im, "\n");
		return;
	}

	/* Only start if current sound isn't already at a marker */
	if (imuse_get_param(im, (intptr_t)handle, PARAM_MARKER) > 0)
		return;

	sequenceID = handle;
	currentSequence = seq_id;
	sequencePri = sequencePriorities[seq_id];

	/* For intro (1) or waiting (2) states, set up continuation */
	if (currentState == 1 || currentState == 2) {
		SdpRecord* chain = sdpArrays[currentState];
		if (!chain)
			return;

		/* Walk to chain end, then find terminal (0x0D = 13) record */
		SdpRecord* p = sdp_chain_end(chain);
		while ((uint8_t)p->sound_name[0] != 13)
			p++;

		SdpRecord* cont = select_sdp(p, currentState);
		void* cont_handle = imuse_filelist_load(im, cont->sound_name);
		if (!cont_handle) {
			imuse_stop_all_sounds(im);
			currentState = 0;
			imuse_ImPrintf(im, "Unable to load file ");
			imuse_ImPrintf(im, cont->sound_name);
			imuse_ImPrintf(im, "\n");
			return;
		}

		imuse_pause(im);
		imuse_filelist_unload(im, nextID);
		nextID = cont_handle;
		currentSdp = cont;
		imuse_resume(im);
	}
}

/*
 * Select an SDP record from the state's chain. Uses ChooseDest to pick
 * a random destination, then searches the chain for a matching name.
 */
static SdpRecord* select_sdp(SdpRecord* sdp, int state) {
	if (!sdp->num_dests) {
		imuse_ImPrintf(im, "Script err: no destinations\n");
		return sdp;
	}

	int dest_idx = choose_dest(sdp);
	const char* dest_name = &sdp->dest_names[dest_idx][0];

	SdpRecord* chain = sdpArrays[state];
	SdpRecord* p = chain;
	while (p->name[0]) {
		if (strcmp(p->name, dest_name) == 0)
			return p;
		p++;
	}

	imuse_ImPrintf(im, "Unable to find sdp ");
	imuse_ImPrintf(im, dest_name);
	imuse_ImPrintf(im, "\n");
	return sdp;
}

/*
 * Select a sequence sound name. For seq_id 2 (smallWin), uses random
 * destination selection. For all others, returns sequenceData[seq_id].
 */
static char* select_sequence(int seq_id) {
	if (seq_id == SEQ_SMALLWIN) {
		int idx = choose_dest(&smallWin);
		return smallWin.dest_names[idx];
	}
	return sequenceData[seq_id];
}

/*
 * Random destination picker with exhaustive bitmask tracking.
 * sdp->num_dests = number of destinations (max 7).
 * sdp->used_mask = bitmask of remaining choices (reset when exhausted).
 * Picks uniformly from remaining, clears the chosen bit.
 */
static int choose_dest(SdpRecord* sdp) {
	/* Initialize mask on first use */
	if (!sdp->used_mask)
		sdp->used_mask = full_masks[sdp->num_dests];

	/* Count available destinations */
	int16_t avail = 0;
	uint8_t mask = 1;
	for (int i = 0; i < sdp->num_dests; i++) {
		if (mask & sdp->used_mask)
			avail++;
		mask <<= 1;
	}

	/* Pick a random index among available */
	int pick = get_random(0, avail - 1);

	/* Walk destinations, counting only available ones */
	uint8_t pick_mask = 1;
	int result;
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
		imuse_ImPrintf(im, "Script err: could not choose dest\n");
		return 0;
	}
	return result;
}

/*
 * LFSR-based PRNG. Advances two 32-bit seeds (23 + 37 iterations)
 * with cross-feedback XOR, then scales to [lo..hi] range.
 */
static int16_t get_random(int16_t lo, int16_t hi) {
	/* LFSR step: shift left by 1, OR in a tap-XOR feedback bit.
	 * Done in uint32 so the doubling matches the binary's `shl`/`add`
	 * with modular wraparound — signed `2 * rseed` is UB once the
	 * value exceeds INT32_MAX/2, which is reached almost immediately
	 * because the seeds at init are 32-bit-truncated host pointers. */
	for (int i = 0; i < 23; i++)
		rseed1 = (int32_t)(2u * (uint32_t)rseed1 +
						   (uint32_t)(((rseed2 & 0x20000000) == 0) ^ ((rseed1 & 0x40000000) != 0)));

	for (int i = 0; i < 37; i++)
		rseed2 = (int32_t)(2u * (uint32_t)rseed2 +
						   (uint32_t)(((rseed1 & 0x20000000) == 0) ^ ((rseed2 & 0x40000000) != 0)));

	/* Sum in uint32 -- signed int32+int32 routinely overflows once
	 * the LFSR has spun. Modular wraparound matches the binary. */
	uint16_t raw = (uint16_t)((uint32_t)rseed2 + (uint32_t)rseed1);
	return (int16_t)(((uint32_t)raw * (hi - lo + 1)) >> 16) + lo;
}
