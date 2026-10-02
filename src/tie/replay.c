#include "tie/replay.h"
#include "tie_runtime/audio/imuse_api.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/inflight_info_task.h"
#include "tie_runtime/runtime/replay_save_task.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/flight_requests.h"
#include "tie_runtime/runtime/replay_viewer_task.h"
#endif

#include "tie/create.h"
#include "tie/edition.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_composite_tie98.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fview.h"
#include "tie/math2.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/panel.h"
#include "tie/panelrts.h"
#include "tie/render_scene_tie98.h"
#include "tie/replayio.h"
#include "tie/shipext.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_flight.h"

#include "../util/binio.h"
#include "tie/xtimer.h"
#include "tie_runtime/runtime/replay_format.h"
#include "tie_runtime/timing/flight_checkpoint.h"
#include "tie_runtime/timing/replay_timing.h"

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <stdint.h>
#include <stdio.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#else
#include <conio.h>
#endif
#include <string.h>

/* --------------------------------------------------------------------------
 * Module-owned globals (watdbg: replay.c's OBJ).
 * -------------------------------------------------------------------------- */

/* VGA (320x200) in column 0, SVGA (640x480) in column 1. Values copied
 * verbatim from Z_TIE__.EXE at 0xC7210 (top) and 0xC72A8 (left). */
// GLOBAL: TIE95 0xC7210
const uint16_t replaybuttontop[38][2] = {
	{ 1, 2 },     { 1, 2 },     { 1, 2 },     { 1, 2 },     /*  0- 3 */
	{ 1, 2 },     { 1, 2 },     { 1, 2 },     { 1, 2 },     /*  4- 7 */
	{ 1, 2 },     { 1, 2 },     { 159, 386 }, { 159, 386 }, /*  8-11 */
	{ 178, 427 }, { 178, 427 }, { 178, 427 }, { 178, 427 }, /* 12-15 */
	{ 164, 394 }, { 164, 394 }, { 147, 147 }, { 147, 147 }, /* 16-19 */
	{ 147, 147 }, { 147, 147 }, { 147, 147 }, { 147, 147 }, /* 20-23 */
	{ 147, 147 }, { 147, 147 }, { 147, 147 }, { 147, 147 }, /* 24-27 */
	{ 160, 160 }, { 160, 160 }, { 178, 178 }, { 178, 178 }, /* 28-31 */
	{ 178, 178 }, { 178, 178 }, { 165, 165 }, { 165, 165 }, /* 32-35 */
	{ 147, 147 }, { 147, 147 },                             /* 36-37 */
};

// GLOBAL: TIE95 0xC72A8
const uint16_t replaybuttonleft[38][2] = {
	{ 47, 94 },   { 47, 94 },   { 82, 164 },  { 82, 164 },  /*  0- 3 */
	{ 117, 234 }, { 117, 234 }, { 210, 420 }, { 210, 420 }, /*  4- 7 */
	{ 245, 490 }, { 245, 490 }, { 31, 70 },   { 31, 70 },   /*  8-11 */
	{ 32, 64 },   { 32, 64 },   { 75, 150 },  { 75, 150 },  /* 12-15 */
	{ 75, 150 },  { 75, 150 },  { 37, 37 },   { 37, 37 },   /* 16-19 */
	{ 62, 62 },   { 62, 62 },   { 89, 89 },   { 89, 89 },   /* 20-23 */
	{ 174, 174 }, { 174, 174 }, { 258, 258 }, { 258, 258 }, /* 24-27 */
	{ 42, 42 },   { 42, 42 },   { 43, 43 },   { 43, 43 },   /* 28-31 */
	{ 86, 86 },   { 86, 86 },   { 86, 86 },   { 86, 86 },   /* 32-35 */
	{ 205, 205 }, { 205, 205 },                             /* 36-37 */
};

/* The replay panel loader stores its 38 sprites in the final slots of
 * the shared 265-entry shape table. */
enum { REPLAY_BUTTON_SHAPE_BASE = 0xE3 };

// GLOBAL: TIE95 0xC7340
// GLOBAL: TIE98 0x4EB010
uint8_t replaymusic;
// GLOBAL: TIE95 0xD5E5C
// GLOBAL: TIE98 0x5FBC4A
int16_t replayvolume;
// GLOBAL: TIE95 0xD5E58
// GLOBAL: TIE98 0x5FBC3E
int16_t replaymsgtimer;

// GLOBAL: TIE95 0xD5E5A
uint16_t chasespecies;
// GLOBAL: TIE95 0xD5E5E
uint16_t trackspecies;
// GLOBAL: TIE95 0xD5E60
// GLOBAL: TIE98 0x5FBC40
uint16_t trackobject;
// GLOBAL: TIE98 0x5FBC44
uint8_t reentersimflag;
// GLOBAL: TIE98 0x5FBC46
uint8_t exitflag;
// GLOBAL: TIE95 0xD5E64
// GLOBAL: TIE98 0x5FBC45
uint8_t cameraposstate;

// GLOBAL: TIE95 0xE36FC
Camera replaycam;

/* replay_loadreplayinput — refill one in-memory chunk from inputspoolfile.
 * Reads only the remaining valid records and shows "loading"
 * (MSG_CAMERA_LOADING) first. Returns 1 on success, 0 on I/O error.
 * Preserves fileptr across the call. */
// FUNCTION: TIE95 0x44F70
int16_t replay_loadreplayinput(void) {
	TieFile* saved;
	uint8_t* bufp;
	long frame_offset;
	uint32_t remaining;
	uint16_t frames_to_read;
	uint16_t frame;

	replay_replaymessage(MSG_CAMERA_LOADING);
	if (replaytotalcnt <= 0 || replaytotalcntdown >= (uint32_t)replaytotalcnt)
		return 0;

	saved = fileptr;
	bufp = (uint8_t*)replaybufferstart;
	memset(bufp, 0, REPLAY_INPUT_BUFFER_BYTES);

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "rb", 1)) {
		fileptr = saved;
		return 0;
	}

	/* The versioned header sits in front of the input frames. Validate first;
	 * the per-frame seek then targets `header + frame_size * idx`. */
	if (!TieReplayFormat_ReadHeader(fileptr, NULL)) {
		TieStorage_Close(fileptr);
		fileptr = saved;
		return 0;
	}

	frame_offset =
		(long)REPLAY_FORMAT_HEADER_SIZE + (long)REPLAYINPUTFRAME_DISK_SIZE * (long)replaytotalcntdown;
	if (TieStorage_Seek(fileptr, frame_offset, TIE_SEEK_SET)) {
		TieStorage_Close(fileptr);
		fileptr = saved;
		return 0;
	}

	remaining = (uint32_t)replaytotalcnt - replaytotalcntdown;
	frames_to_read =
		(uint16_t)(remaining < REPLAY_INPUT_CHUNK_FRAMES ? remaining : REPLAY_INPUT_CHUNK_FRAMES);
	for (frame = 0; frame < frames_to_read; ++frame) {
		size_t n = TieStorage_Read(bufp, 1, REPLAYINPUTFRAME_DISK_SIZE, fileptr);
		bufp += n;
		if (n != REPLAYINPUTFRAME_DISK_SIZE) {
			TieStorage_Close(fileptr);
			fileptr = saved;
			return 0;
		}
	}
	TieStorage_Close(fileptr);
	fileptr = saved;
	return 1;
}

/* --------------------------------------------------------------------------
 * Replay clip save / load (.clp files)
 *
 * Retail file format:
 *   u32  replaytotalcnt        — number of 8-byte input frames
 *   u16  replayrandomseed      — RNG seed at record start
 *   savearrayptrs/sizes blocks — full u32-stride iteration
 *                                 (all 67 dynamic-state regions)
 *   0x36C0 fg_array            — byte dump
 *   0x5A0  radiomsg
 *   0x70   cut
 *   0x900  fgstatus
 *   0xA1   species_table.load_flags
 *   0x198  camera block
 *   modern timing checkpoint   — cadence and high-rate remainders
 *   N*14   input stream        — chunked reads/writes
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x45078
// FUNCTION: TIE98 0x4724E0
uint16_t replay_savereplay(void) {
#ifdef TIE_MODERN
	ReplaySaveTask* continuation = landru_task_top();
	uint8_t* name_input = continuation->name_input;
	char* filename = continuation->filename;
#else
	uint8_t name_input[40];
	char filename[16];
#endif
	TieFile* clip_fp;
	size_t i;
	size_t m;
#ifdef TIE_MODERN
	continuation->waiting = false;
	if (continuation->phase == REPLAY_SAVE_PHASE_BEGIN) {
#endif
		replay_replaymessage(MSG_ENTER_FILENAME);
		name_input[0] = 0;
		festring_setfontsize(1);
#ifdef TIE_MODERN
		continuation->phase = REPLAY_SAVE_PHASE_EDIT_NAME;
	}
	if (continuation->phase == REPLAY_SAVE_PHASE_EDIT_NAME) {
#endif
		if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			flightResolution == TIE_FLIGHT_RES_SVGA_D3D)
			replay_editstring(126, 456, 8, name_input, 0x2C);
		else
			replay_editstring(74, 190, 8, name_input, 0x2C);
#ifdef TIE_MODERN
		if (continuation->editor_active || continuation->waiting)
			return TIE_REPLAY_SAVE_PENDING;
#endif
		festring_setfontsize(2);
		if (!name_input[0])
			return MSG_REPLAY_NOT_SAVED;
		strcpy(filename, (const char*)name_input);
		strcat(filename, ".clp");
#ifdef TIE_MODERN
		continuation->phase = REPLAY_SAVE_PHASE_CHECK_FILE;
	}
	if (continuation->phase == REPLAY_SAVE_PHASE_CHECK_FILE) {
		TieFile* existing = TieStorage_Open(TIE_FILE_ROOT_USER, filename, "rb");
		if (existing) {
			TieStorage_Close(existing);
			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			replay_replaymessage(MSG_FILE_REPLACE);
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_PresentFrontSurface();
				FrontendDisplay_PresentFrame();
			}
			continuation->phase = REPLAY_SAVE_PHASE_CONFIRM_REPLACE;
			return TIE_REPLAY_SAVE_PENDING;
		}
		continuation->phase = REPLAY_SAVE_PHASE_WRITE;
		return TIE_REPLAY_SAVE_PENDING;
	}
	if (continuation->phase == REPLAY_SAVE_PHASE_CONFIRM_REPLACE) {
		int key;
		if (!TieInput_KeyPending()) {
			continuation->waiting = true;
			return TIE_REPLAY_SAVE_PENDING;
		}
		key = TieInput_ReadKey();
		if (key != 'y' && key != 'Y')
			return MSG_REPLAY_NOT_SAVED;
		continuation->phase = REPLAY_SAVE_PHASE_WRITE;
		return TIE_REPLAY_SAVE_PENDING;
	}
#else
	if (fediskio_tryopenfile(TIE_FILE_ROOT_USER, filename, "rb", 0)) {
		int key;
		fclose(fileptr);
		replay_replaymessage(MSG_FILE_REPLACE);
		key = (char)getch();
		if (key != 'y' && key != 'Y')
			return MSG_REPLAY_NOT_SAVED;
	}
#endif

#ifdef TIE_MODERN

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, filename, "wb", 0))
		return MSG_FILE_ERROR;

	/* The versioned header precedes the clip preamble. */
	if (!TieReplayFormat_WriteHeader(fileptr)) {
		TieStorage_Close(fileptr);
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}

	/* Clip preamble: little-endian u32 frame count + u16 seed. */
	TieStorage_Putc((int)(replaytotalcnt & 0xFF), fileptr);
	TieStorage_Putc((int)((replaytotalcnt >> 8) & 0xFF), fileptr);
	TieStorage_Putc((int)((replaytotalcnt >> 16) & 0xFF), fileptr);
	TieStorage_Putc((int)((replaytotalcnt >> 24) & 0xFF), fileptr);
	TieStorage_Putc(replayrandomseed & 0xFF, fileptr);
	TieStorage_Putc((replayrandomseed >> 8) & 0xFF, fileptr);

	clip_fp = fileptr;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, replaystartfile, "rb", 0)) {
		TieStorage_Close(fileptr);
		TieStorage_Close(clip_fp);
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}

	/* Retail walks savearraysizes as u32[] — full iteration, all 67 slots. */
	for (i = 0; savearrayptrs[i]; ++i) {
		if (!replay_copybytesinfile((uint16_t)savearraysizes[i], fileptr, clip_fp)) {
			TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
			return MSG_FILE_ERROR;
		}
	}
	if (!replay_copybytesinfile(0x36C0, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x5A0, fileptr, clip_fp) || !replay_copybytesinfile(0x70, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x900, fileptr, clip_fp) || !replay_copybytesinfile(0xA1, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x198, fileptr, clip_fp) ||
		!replay_copybytesinfile((uint16_t)TieFlightCheckpoint_Size(), fileptr, clip_fp)) {
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}
	TieStorage_Close(fileptr);

	/* Input stream — N fixed-size versioned records.
	 * When pulling from input.spl skip past the format header before
	 * copying records. */
	if (replayspoolflag) {
		uint32_t i;

		if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "rb", 0)) {
			TieStorage_Close(clip_fp);
			TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
			return MSG_FILE_ERROR;
		}
		if (!TieReplayFormat_ReadHeader(fileptr, NULL) ||
			TieStorage_Seek(fileptr, REPLAY_FORMAT_HEADER_SIZE, TIE_SEEK_SET) != 0) {
			TieStorage_Close(fileptr);
			TieStorage_Close(clip_fp);
			TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
			return MSG_FILE_ERROR;
		}
		for (i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (!replay_copybytesinfile(REPLAYINPUTFRAME_DISK_SIZE, fileptr, clip_fp)) {
				TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
				return MSG_FILE_ERROR;
			}
		}
		TieStorage_Close(fileptr);
	} else {
		const uint8_t* bufp = (const uint8_t*)replaybufferstart;
		uint32_t i;

		for (i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (TieStorage_Write(bufp, 1, REPLAYINPUTFRAME_DISK_SIZE, clip_fp) !=
				REPLAYINPUTFRAME_DISK_SIZE) {
				TieStorage_Close(clip_fp);
				TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
				return MSG_FILE_ERROR;
			}
			bufp += REPLAYINPUTFRAME_DISK_SIZE;
		}
	}

	if (TieStorage_Close(clip_fp) != 0) {
		TieStorage_Remove(TIE_FILE_ROOT_USER, filename);
		return MSG_FILE_ERROR;
	}

	memset(replayclipname, 0, sizeof(replayclipname));
	m = 0;
	while (m < sizeof(replayclipname) - 1 && name_input[m]) {
		replayclipname[m] = (char)name_input[m];
		++m;
	}
	return MSG_REPLAY_SAVED;
#else

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, filename, "wb", 0))
		return MSG_FILE_ERROR;

	/* Clip preamble: little-endian u32 frame count + u16 seed. */
	fputc((int)(replaytotalcnt & 0xFF), fileptr);
	fputc((int)((replaytotalcnt >> 8) & 0xFF), fileptr);
	fputc((int)((replaytotalcnt >> 16) & 0xFF), fileptr);
	fputc((int)((replaytotalcnt >> 24) & 0xFF), fileptr);
	fputc(replayrandomseed & 0xFF, fileptr);
	fputc((replayrandomseed >> 8) & 0xFF, fileptr);

	clip_fp = fileptr;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, replaystartfile, "rb", 0)) {
		fclose(fileptr);
		fclose(clip_fp);
		fediskio_delfile(filename);
		return MSG_FILE_ERROR;
	}

	/* Retail walks savearraysizes as u32[] — full iteration, all 67 slots. */
	for (i = 0; savearraysizes[i]; ++i) {
		if (!replay_copybytesinfile((uint16_t)savearraysizes[i], fileptr, clip_fp)) {
			fediskio_delfile(filename);
			return MSG_FILE_ERROR;
		}
	}
	if (!replay_copybytesinfile(0x36C0, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x5A0, fileptr, clip_fp) || !replay_copybytesinfile(0x70, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x900, fileptr, clip_fp) || !replay_copybytesinfile(0xA1, fileptr, clip_fp) ||
		!replay_copybytesinfile(0x198, fileptr, clip_fp)) {
		fediskio_delfile(filename);
		return MSG_FILE_ERROR;
	}
	fclose(fileptr);

	/* Input stream — N fixed-size versioned records.
	 * When pulling from input.spl skip past the format header before
	 * copying records. */
	if (replayspoolflag) {
		uint32_t i;

		if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "rb", 0)) {
			fclose(clip_fp);
			fediskio_delfile(filename);
			return MSG_FILE_ERROR;
		}
		for (i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (!replay_copybytesinfile(8, fileptr, clip_fp)) {
				fediskio_delfile(filename);
				return MSG_FILE_ERROR;
			}
		}
		fclose(fileptr);
	} else {
		const uint8_t* bufp = (const uint8_t*)replaybufferstart;
		uint32_t i;

		for (i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (fwrite(bufp, 1, 8, clip_fp) != 8) {
				fclose(clip_fp);
				fediskio_delfile(filename);
				return MSG_FILE_ERROR;
			}
			bufp += 8;
		}
	}

	if (fclose(clip_fp) == -1) {
		fediskio_delfile(filename);
		return MSG_FILE_ERROR;
	}

	memset(replayclipname, 0, sizeof(replayclipname));
	m = 0;
	while (m < sizeof(replayclipname) - 1 && name_input[m]) {
		replayclipname[m] = (char)name_input[m];
		++m;
	}
	return MSG_REPLAY_SAVED;
#endif
}

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/* REPLAY_copybytesinfile — copy `count` bytes from src to dst. Retail
 * chunks the copy into 256-byte blocks via fediskio_readfileblock + fwrite
 * (the demo was per-byte fgetc/fputc). On TIE_EOF / short write both streams
 * are fclosed and 0 is returned. Returns 1 on success (streams left
 * open). */
// FUNCTION: TIE95 0x454F4
int replay_copybytesinfile(uint16_t count, TieFile* src, TieFile* dst) {
	uint8_t buf[256];
	uint32_t remaining = count;
	while (remaining > 0) {
		uint32_t chunk = remaining > 256u ? 256u : remaining;
		int16_t got = fediskio_readfileblock(buf, 1, chunk, src);
		if ((uint32_t)got != chunk || TieStorage_Write(buf, 1, got, dst) != (size_t)got) {
			TieStorage_Close(src);
			TieStorage_Close(dst);
			return 0;
		}
		remaining -= chunk;
	}
	return 1;
}

// FUNCTION: TIE95 0x455B4
int replay_loadreplay(void) {
	char filename[40];
	uint8_t preamble[6];
	uint32_t cnt;
	TieFile* clip_fp;
	size_t i;

	strcpy(filename, replayclipname);
	if (!filename[0])
		return 0;
	strcat(filename, ".clp");

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, filename, "rb", 0))
		return 0;

	/* Versioned format header guards the rest of the parse — anything
	 * else (legacy retail clip, mismatched build) is rejected here. */
	if (!TieReplayFormat_ReadHeader(fileptr, NULL)) {
		TieStorage_Close(fileptr);
		return 0;
	}

	if (TieStorage_Read(preamble, 1, sizeof preamble, fileptr) != sizeof preamble) {
		TieStorage_Close(fileptr);
		return 0;
	}
	cnt = br_u32le(preamble);
	if (!cnt || cnt > REPLAY_MAX_TOTAL_RECORDS) {
		TieStorage_Close(fileptr);
		return 0;
	}
	replaytotalcnt = (int32_t)cnt;
	replayrandomseed = br_u16le(preamble + 4);

	clip_fp = fileptr;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, replaystartfile, "wb", 0)) {
		TieStorage_Close(clip_fp);
		return 0;
	}

	/* Retail: full u32-stride iteration. */
	for (i = 0; savearrayptrs[i]; ++i) {
		if (!replay_copybytesinfile((uint16_t)savearraysizes[i], clip_fp, fileptr)) {
			return 0;
		}
	}
	if (!replay_copybytesinfile(0x36C0, clip_fp, fileptr) ||
		!replay_copybytesinfile(0x5A0, clip_fp, fileptr) || !replay_copybytesinfile(0x70, clip_fp, fileptr) ||
		!replay_copybytesinfile(0x900, clip_fp, fileptr) || !replay_copybytesinfile(0xA1, clip_fp, fileptr) ||
		!replay_copybytesinfile(0x198, clip_fp, fileptr) ||
		!replay_copybytesinfile((uint16_t)TieFlightCheckpoint_Size(), clip_fp, fileptr)) {
		return 0;
	}
	if (TieStorage_Close(fileptr) != 0) {
		TieStorage_Close(clip_fp);
		return 0;
	}

	if (replayspoolflag) {
		/* Reconstitute input.spl from the clip's input stream — open
		 * fresh and prefix with the current header so subsequent
		 * spool/load goes through the standard format path. */
		uint32_t i;
		int rc;

		if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "wb", 0)) {
			TieStorage_Close(clip_fp);
			return 0;
		}
		if (!TieReplayFormat_WriteHeader(fileptr)) {
			TieStorage_Close(fileptr);
			TieStorage_Close(clip_fp);
			return 0;
		}
		for (i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			if (!replay_copybytesinfile(REPLAYINPUTFRAME_DISK_SIZE, clip_fp, fileptr)) {
				return 0;
			}
		}
		rc = (TieStorage_Close(fileptr) == 0);
		TieStorage_Close(clip_fp);
		return rc;
	} else {
		uint8_t* bufp = (uint8_t*)replaybufferstart;
		uint32_t i;

		if ((uint32_t)replaytotalcnt > REPLAY_INPUT_CHUNK_FRAMES)
			replaytotalcnt = REPLAY_INPUT_CHUNK_FRAMES;
		for (i = 0; i < (uint32_t)replaytotalcnt; ++i) {
			int16_t got = fediskio_readfileblock(bufp, 1, REPLAYINPUTFRAME_DISK_SIZE, clip_fp);
			bufp += got;
			if ((uint16_t)got != REPLAYINPUTFRAME_DISK_SIZE) {
				TieStorage_Close(clip_fp);
				return 0;
			}
		}
		TieStorage_Close(clip_fp);
		return 1;
	}
}

// FUNCTION: TIE95 0x458DC
void replay_rewindreplay(void) {
	TieReplayTiming_Reset();
	if (replaymusic == 1) {
		replayvolume = (int16_t)hilevel_ImGetMasterVol();
		hilevel_ImSetMasterVol(0);
		lolevel_ImPause();
		replaymusic = 0;
	}
	replayio_copyfromsave(replaystartfile);
	replaytotalcntdown = 0;
	replaypercent = -1;
	math2_randomseed = (int16_t)replayrandomseed;
	if (replayspoolflag) {
		if (!replay_loadreplayinput()) {
			replaytotalcnt = 0;
			replaytotalcntdown = 0;
			replay_stopreplay();
			return;
		}
	}
	replaybuffercnt = 0;
	replayptr = replaybufferstart;
	updateactionflag = 0;
	replay_drawreplaybutton(2);
}

/* --------------------------------------------------------------------------
 * Playback-state mutators
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x4596C
void replay_stopreplay(void) {
	TieReplayTiming_Reset();
	updateactionflag = 0;
	replay_replaymessage(MSG_FILM_END);
	replay_drawreplaybutton(2);
	if (replaymusic == 1) {
		replayvolume = (int16_t)hilevel_ImGetMasterVol();
		hilevel_ImSetMasterVol(0);
		lolevel_ImPause();
		replaymusic = 0;
	}
}

/* replay_drawreplaybutton — repaint one widget of the replay HUD.
 * btn_id 0..0x11 = in-flight cockpit layout, 0..0x25 = stand-alone viewer
 * (same ids with +18 offset). Beyond the basic blit, 6 ids (14/32, 15/33,
 * 16/34, 17/35) toggle the track/chase info windows (name + status).
 *
 * Retail supports VGA and SVGA coordinate sets for both the cockpit and
 * stand-alone layouts. */
// FUNCTION: TIE95 0x459B8
void replay_drawreplaybutton(uint16_t btn_id) {
	uint16_t idx = btn_id;
	int res = 0; /* 0 = VGA / demo layout */
	int16_t track_name_y, track_name_h, track_stat_y, track_stat_h;
	int16_t track_name_left, track_name_right;
	int16_t track_stat_left, track_stat_right;
	int16_t chase_name_y, chase_name_h, chase_stat_y, chase_stat_h;
	int16_t chase_name_left, chase_name_right;
	int16_t chase_stat_left, chase_stat_right;

	if (maingameflag) {
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			res = 0;
			track_name_left = 127;
			track_name_right = 201;
			track_stat_left = 230;
			track_stat_right = 268;
			track_name_y = 181;
			track_name_h = 186;
			track_stat_y = 181;
			track_stat_h = 186;
			chase_name_left = 127;
			chase_name_right = 201;
			chase_stat_left = 230;
			chase_stat_right = 268;
			chase_name_y = 167;
			chase_name_h = 172;
			chase_stat_y = 167;
			chase_stat_h = 172;
		} else {
			res = 1;
			track_name_left = 260;
			track_name_right = 397;
			track_stat_left = 464;
			track_stat_right = 532;
			track_name_y = 436;
			track_name_h = 445;
			track_stat_y = 436;
			track_stat_h = 445;
			chase_name_left = 260;
			chase_name_right = 397;
			chase_stat_left = 464;
			chase_stat_right = 532;
			chase_name_y = 402;
			chase_name_h = 411;
			chase_stat_y = 402;
			chase_stat_h = 411;
		}
	} else {
		/* Stand-alone viewer remaps 0..0x11 -> 0x12..0x23. */
		if (btn_id < 0x12u)
			idx = btn_id + 18;
		if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
			res = 1;
			track_name_left = 278;
			track_name_right = 423;
			track_stat_left = 484;
			track_stat_right = 556;
			track_name_y = 437;
			track_name_h = 448;
			track_stat_y = 437;
			track_stat_h = 448;
			chase_name_left = 278;
			chase_name_right = 423;
			chase_stat_left = 484;
			chase_stat_right = 556;
			chase_name_y = 400;
			chase_name_h = 412;
			chase_stat_y = 400;
			chase_stat_h = 412;
		} else {
			res = 0;
			track_name_left = 139;
			track_name_right = 211;
			track_stat_left = 241;
			track_stat_right = 279;
			track_name_y = 181;
			track_name_h = 186;
			track_stat_y = 181;
			track_stat_h = 186;
			chase_name_left = 139;
			chase_name_right = 211;
			chase_stat_left = 241;
			chase_stat_right = 279;
			chase_name_y = 167;
			chase_name_h = 172;
			chase_stat_y = 167;
			chase_stat_h = 172;
		}
	}

	/* Only cockpit mode blits the sprite (stand-alone viewer draws its
	 * HUD from the panel backdrop). */
	if (maingameflag) {
		drawshape(farbufferptrs[REPLAY_BUTTON_SHAPE_BASE + idx], replaybuttonleft[idx][res],
				  replaybuttontop[idx][res], 0, 0);
	}

	if (idx == 14 || idx == 32) {
		festring_setbound(track_name_left, track_name_y, track_name_right, track_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setbound(track_stat_left, track_stat_y, track_stat_right, track_stat_h);
		clearwindow();
	}
	if (idx == 15 || idx == 33) {
		uint8_t sp;
		int st;

		festring_setbound(track_name_left, track_name_y, track_name_right, track_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setcursor(track_name_left, track_name_y);
		replay_outputobjectname(trackobject);

		sp = (trackobject >= 0x3800u) ? staticobjects[trackobject - 14336].species
									  : objects[trackobject].ship_idx;
		trackspecies = sp;

		festring_setbound(track_stat_left, track_stat_y, track_stat_right, track_stat_h);
		clearwindow();
		festring_setcursor(track_stat_left, track_stat_y);
		festring_settextcolor(0x4E);
		st = replay_getstatusnum(trackobject);
		festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
	}
	if (idx == 16 || idx == 34) {
		festring_setbound(chase_name_left, chase_name_y, chase_name_right, chase_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setbound(chase_stat_left, chase_stat_y, chase_stat_right, chase_stat_h);
		clearwindow();
		cameraposstate = 0;
	}
	if (idx == 17 || idx == 35) {
		uint8_t sp;
		int st;

		festring_setbound(chase_name_left, chase_name_y, chase_name_right, chase_name_h);
		festring_setbackcolor(0x40);
		clearwindow();
		festring_setcursor(chase_name_left, chase_name_y);
		replay_outputobjectname(replaycam.view_target_obj);

		sp = (replaycam.view_target_obj >= 0x3800u) ? staticobjects[replaycam.view_target_obj - 14336].species
													: objects[replaycam.view_target_obj].ship_idx;
		chasespecies = sp;

		festring_setbound(chase_stat_left, chase_stat_y, chase_stat_right, chase_stat_h);
		clearwindow();
		festring_setcursor(chase_stat_left, chase_stat_y);
		festring_settextcolor(0x4E);
		st = replay_getstatusnum(replaycam.view_target_obj);
		festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
		cameraposstate = 1;
	}
}

/* replay_outputobjectname — format obj_id's display name into tempstring
 * and outstring-center it. Identical to demo. */
// FUNCTION: TIE95 0x46094
void replay_outputobjectname(uint16_t obj_id) {
	CraftData* cp;
	uint16_t type;

	if (obj_id < 0x3800) {
		if (objects[obj_id].side == 0)
			festring_settextcolor('Q');
		else if (objects[obj_id].side == 1 || objects[obj_id].side == 4)
			festring_settextcolor('I');
		else if (objects[obj_id].side == 2)
			festring_settextcolor('E');
		else
			festring_settextcolor('U');

		if (!objects[obj_id].category) {
			cp = objects[obj_id].craft_ptr;
			festring_farstrcpy(spec_data[cp->species_idx].short_name);
			festring_farstradd(':');
			festring_farstradd(' ');
			festring_farstradd((char)254);

			if (objects[obj_id].side == 0)
				festring_farstradd('R');
			else if (objects[obj_id].side == 1 || objects[obj_id].side == 4)
				festring_farstradd('J');
			else if (objects[obj_id].side == 2)
				festring_farstradd('F');
			else
				festring_farstradd('V');

			festring_farstrcat(fg_array[objects[obj_id].fg_idx].name);
			if ((int8_t)fg_array[objects[obj_id].fg_idx].count > 1) {
				festring_farstradd(' ');
				festring_farstradd((char)(cp->craft_idx_in_fg + '1'));
			}
		} else {
			type = objects[obj_id].ship_idx;
			if (type >= 143 && type <= 154) {
				festring_farstrcpy(((char**)warheadstrings)[type - 143]);
			}
		}
	} else {
		festring_settextcolor(0x43);
		type = staticobjects[obj_id - 14336].species;
		if (type >= 70 && type <= 84) {
			festring_farstrcpy(((char**)buoystr)[type - 70]);
		}
	}
	festring_outstringcenter((const uint8_t*)tempstring);
}

/* --------------------------------------------------------------------------
 * Object classifier + label formatters
 * -------------------------------------------------------------------------- */

/* replay_getstatusnum — classify obj_id into a "status-bar" code 0..6.
 *    0 = normal (fighter / has shields)
 *    2 = all status flags clear (disabled application)
 *    3 = docking in progress
 *    4 = satellite (species 143/144) without species_idx    (no personnel)
 *    5 = satellite with species_idx                          (boarded)
 *    6 = disabled-and-drained (all shields zero, non-fighter)
 *
 * Static objects (obj_id >= 0x3800) never reach any craft_ptr deref
 * below: staticobjects[].species is in the buoy range (70..84). */
// FUNCTION: TIE95 0x462BC
int replay_getstatusnum(uint16_t obj_id) {
	uint16_t species;
	CraftData* craft_ptr;
	CraftData* sat_craft;

	if (obj_id < 0x3800) {
		species = objects[obj_id].ship_idx;
		craft_ptr = objects[obj_id].craft_ptr;
		sat_craft = craft_ptr;
	} else {
		species = staticobjects[obj_id - 0x3800].species;
	}

	if (species == 144u || species == 143) {
		if (sat_craft->species_idx)
			return 5;
		return 4;
	}
	if (!species_table[species].category) {
		if (craft_ptr->dock_state_flags)
			return 3;
		if (!craft_ptr->status_flags)
			return 2;
		if ((int32_t)craft_ptr->forward_shield + (int32_t)craft_ptr->rear_shield == 0 &&
			objects[obj_id].genus != GENUS_FIGHTER)
			return 6;
	}
	return 0;
}

/* replay_outputclipname — paint the current clip name centered in the
 * info strip. Retail added an SVGA cockpit strip (341, 13, 411, 22). */
// FUNCTION: TIE95 0x463BC
void replay_outputclipname(void) {
	int16_t cx, cy;

	festring_setbackcolor(0x40);

	if (maingameflag) {
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			festring_setbound(169, 5, 206, 11);
			cx = 169;
			cy = 5;
		} else {
			festring_setbound(341, 13, 411, 22);
			cx = 341;
			cy = 13;
		}
	} else if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			   flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
		festring_setbound(260, 360, 339, 373);
		cx = 260;
		cy = 360;
	} else {
		festring_setbound(130, 150, 169, 155);
		cx = 130;
		cy = 150;
	}
	festring_setcursor(cx, cy);
	clearwindow();
	festring_settextcolor(0x43);
	festring_outstringcenter((const uint8_t*)replayclipname);
}

// FUNCTION: TIE95 0x4646C
// FUNCTION: TIE98 0x473AA0
void replay_doreplayscreen(void) {
	uint16_t last_chase_status;
	uint16_t last_track_status;
#ifdef TIE_MODERN
	ReplayScreenState* continuation = landru_task_top();
	last_chase_status = continuation->last_chase_status;
	last_track_status = continuation->last_track_status;
	continuation->pushed_subtask = false;
	if (!continuation->started)
#endif
	{
#ifdef TIE_MODERN
		TieReplayTiming_Reset();
#endif
		last_chase_status = 0xFFFF;
		last_track_status = 0xFFFF;

		replaycam.view_target_obj = pstate.object_idx;
		replaycam.view_zoom_flag = 1;
		replaycam.view_zoom = 1280;
		replaycam.view_camera_control = 0;
		replaycam.up_angle = 0;
		replaycam.side_angle = 0;
		fullupdateflag = 1;
		trackobject = 0xFFFFu;
		if (TIE_FLIGHT_TIE98)
			g_flightInitialTextureCacheFlushPending = 1;
		tie_updatescreen();
		if (TIE_DISPLAY_DX5)
			FlightSurface_Lock();
		updateactionflag = 0;
		fastforwardflag = 0;
		fastforwardtimer = 0;
		replay_drawreplaybutton(0xA);
		replay_drawreplaybutton(0x11);
		replay_drawreplaybutton(0xC);
		replay_drawreplaybutton(0xE);
		replay_outputclipname();
		if (TIE_DISPLAY_DX5)
			FlightSurface_Unlock();
		exitflag = 0;
#ifdef TIE_MODERN
		continuation->last_chase_status = last_chase_status;
		continuation->last_track_status = last_track_status;
		continuation->started = true;
		return;
#endif
	}
	while (!exitflag) {
#ifdef TIE_MODERN
		bool advance_message_timer = false;
		bool pushed_subtask = false;
#endif

		{
#ifdef TIE_MODERN
			bool save_requested;
#endif
			bool advance_replay;
			int16_t pct_cx, pct_cy;
			uint16_t pct_fwd;
			uint16_t pct;

			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
#ifdef TIE_MODERN
			replay_replayinput();
			save_requested = continuation->save_requested;
			if (save_requested) {
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
				if (TieReplaySave_Begin()) {
					continuation->pushed_subtask = true;
					return;
				}
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
			}

#else
			replay_replayinput();
#endif
			if (!updateactionflag || fastforwardflag) {
				if (replaymusic == 1) {
					replaymusic = 0;
					replayvolume = (int16_t)hilevel_ImGetMasterVol();
					hilevel_ImSetMasterVol(0);
					lolevel_ImPause();
				}
			} else if (!replaymusic) {
				replaymusic = 1;
				hilevel_ImSetMasterVol((uint16_t)replayvolume);
				lolevel_ImResume();
			}

#ifdef TIE_MODERN
			advance_replay = updateactionflag && TieReplayTiming_IsFrameDue();
#else
			advance_replay = updateactionflag != 0;
#endif
			if (advance_replay) {
#ifdef TIE_MODERN
				int32_t info_screen;
#endif

#ifdef TIE_MODERN
				advance_message_timer = true;
#endif
				/* In replay mode tie_doframe never returns false (the
				 * tickcounter budget gate is bypassed by the replayviewmode
				 * branch); cast to void to acknowledge the unused result. */
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
				(void)tie_doframe();
#ifdef TIE_MODERN
				TieReplayTiming_ConsumeFrame();
				info_screen = TieFlightRequest_ConsumeInfoRoom();
				if (info_screen >= 0) {
					TieInflightInfo_Begin(info_screen);
					pushed_subtask = true;
				}
#endif
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
				if (cameraposstate) {
					int16_t cx, cy;
					uint16_t st;

					festring_setbackcolor(0x40);

					if (maingameflag) {
						if (flightResolution == TIE_FLIGHT_RES_VGA) {
							festring_setbound(230, 167, 268, 172);
							cx = 230;
							cy = 167;
						} else {
							festring_setbound(464, 402, 532, 411);
							cx = 464;
							cy = 402;
						}
					} else if (flightResolution == TIE_FLIGHT_RES_SVGA ||
							   flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
							   flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
						festring_setbound(484, 400, 556, 412);
						cx = 484;
						cy = 400;
					} else {
						festring_setbound(241, 167, 279, 172);
						cx = 241;
						cy = 167;
					}
					festring_setcursor(cx, cy);
					st = (uint16_t)replay_getstatusnum(replaycam.view_target_obj);
					if (st != last_chase_status) {
						clearwindow();
						festring_settextcolor(0x4E);
						last_chase_status = st;
						festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
					}
				}
				if (trackobject != 0xFFFFu) {
					int16_t cx, cy;
					uint16_t st;

					festring_setbackcolor(0x40);

					if (maingameflag) {
						if (flightResolution == TIE_FLIGHT_RES_VGA) {
							festring_setbound(230, 181, 268, 186);
							cx = 230;
							cy = 181;
						} else {
							festring_setbound(464, 436, 532, 445);
							cx = 464;
							cy = 436;
						}
					} else if (flightResolution == TIE_FLIGHT_RES_SVGA ||
							   flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
							   flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
						festring_setbound(484, 437, 556, 448);
						cx = 484;
						cy = 437;
					} else {
						festring_setbound(241, 181, 279, 186);
						cx = 241;
						cy = 181;
					}
					festring_setcursor(cx, cy);
					st = (uint16_t)replay_getstatusnum(trackobject);
					if (st != last_track_status) {
						clearwindow();
						festring_settextcolor(0x4E);
						last_track_status = st;
						festring_outstringcenter((const uint8_t*)((const char**)statusstrings)[st]);
					}
				}
			} else if (!updateactionflag) {
				uint16_t t0;

#ifdef TIE_MODERN
				advance_message_timer = true;
#endif
				/* Paused branch: repaint once, recompute framerate from
				 * XTIMER delta spent repainting. */
				t0 = tickcounter;
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
				tie_updatescreen();
				if (TIE_DISPLAY_DX5) {
					FrontendDisplay_PresentFrame();
					if (g_useHardware3D)
						RenderScene_ClearFrameBuffers();
					else
						FrontendDisplay_BlitOffscreenToRenderSurface();
					FlightSurface_Lock();
				}
				tickcounter += (uint16_t)xtimer_Time_Elapsed();
				frameticks = tickcounter - t0;
				if (tickcounter == t0)
					frameticks = 1;
				framerate = 236 / frameticks;
				if (!framerate)
					framerate = 1;
			}

			festring_setfontsize(2);

			if (maingameflag) {
				if (flightResolution == TIE_FLIGHT_RES_VGA) {
					festring_setbound(154, 5, 165, 11);
					pct_cx = 156;
					pct_cy = 5;
				} else {
					festring_setbound(310, 13, 327, 22);
					pct_cx = 314;
					pct_cy = 13;
				}
			} else if (flightResolution == TIE_FLIGHT_RES_SVGA ||
					   flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
					   flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
				festring_setbound(230, 360, 251, 373);
				pct_cx = 234;
				pct_cy = 360;
			} else {
				festring_setbound(115, 150, 126, 155);
				pct_cx = 117;
				pct_cy = 150;
			}
			festring_setcursor(pct_cx, pct_cy);
			festring_setbackcolor(0x40);

			pct_fwd = math2_longpercentage(replaytotalcntdown, (uint32_t)replaymaxcnt);
			pct = 100 - math2_fraction(100, pct_fwd);
			if (pct > 99)
				pct = 99;
			if (pct != replaypercent) {
				replaypercent = (int16_t)pct;
				clearwindow();
				festring_settextcolor(0x43);
				panelrts_outnum(pct, 2, 2);
			}

#ifdef TIE_MODERN
			if (advance_message_timer && frameticks >= (uint16_t)replaymsgtimer) {
#else
			if (frameticks >= (uint16_t)replaymsgtimer) {
#endif
				if (replaymsgtimer) {
					festring_setbackcolor(0x2C);
					/* Retail widens the clear rect to 640x480 in SVGA. */
					if (flightResolution == TIE_FLIGHT_RES_SVGA ||
						flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
						flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
						festring_setbound(0, 457, 640, 480);
					} else {
						festring_setbound(0, 190, 320, 200);
					}
					clearwindow();
				}
				replaymsgtimer = 0;
#ifdef TIE_MODERN
			} else if (advance_message_timer) {
#else
			} else {
#endif
				replaymsgtimer -= frameticks;
			}
		}
		if (TIE_DISPLAY_DX5)
			FlightSurface_Unlock();

#ifdef TIE_MODERN
		continuation->last_chase_status = last_chase_status;
		continuation->last_track_status = last_track_status;
		continuation->pushed_subtask = pushed_subtask;
		return;
#endif
	}
}

/* replay_replayinput — read one hardware tick of input and dispatch. */
// FUNCTION: TIE95 0x46928
// FUNCTION: TIE98 0x474040
void replay_replayinput(void) {
	int16_t btn_val;
#ifdef TIE_MODERN
	ReplayScreenState* continuation = landru_task_top();
	continuation->save_requested = false;
#endif

	feinput_getrawinput();
	feinput_checkinput();

	if (inputkey) {
		switch (inputkey) {

			case 0x1B:
				if (!maingameflag) {
					updateactionflag = 0;
					exitflag = 1;
					mission.player_status = 16;
				}
				break;

			case KEY_A:
			case KEY_a:
				if (fastforwardflag) {
					fastforwardflag = 0;
					fastforwardtimer = 0;
					replay_replaymessage(MSG_FILM_ADVANCE_OFF);
					replay_drawreplaybutton(4);
				} else if (replaytotalcntdown < (uint32_t)replaytotalcnt) {
					fastforwardflag = 1;
					fastforwardtimer = 236;
					updateactionflag = 1;
					replay_replaymessage(MSG_FILM_ADVANCE_ON);
					replay_drawreplaybutton(5);
					replay_drawreplaybutton(3);
				} else {
					replay_replaymessage(MSG_FILM_END);
				}
				break;

			case KEY_C:
			case KEY_c: {
				uint16_t saved = pstate.object_idx;
				uint16_t cur = replaycam.view_target_obj;
				int32_t dir;

				pstate.object_idx = 0xFFFEu;
				dir = (inputkey == KEY_C) ? -1 : +1;
				replaycam.view_target_obj = user_picknexttarget(cur, dir);
				pstate.object_idx = saved;
				replay_drawreplaybutton(0x11);
				if (replaycam.view_camera_control) {
					replay_movecambehind(replaycam.view_target_obj);
				}
			} break;

			case KEY_E:
			case KEY_e:
				replay_drawreplaybutton(9);
				updateactionflag = 0;
				mission.player_status = 15;
				exitflag = 1;
				break;

			case KEY_F:
			case KEY_f:
				if (replaycam.view_camera_control) {
					replaycam.view_camera_control = 0;
					replay_replaymessage(MSG_CAMERA_FOLLOW);
					replay_drawreplaybutton(0xA);
					replay_drawreplaybutton(0x11);
				} else {
					replaycam.view_camera_control = 1;
					create_getworldposition(replaycam.view_target_obj, 0);
					trig2_ctop(worldlocx - camera.x, worldlocy - camera.y, worldlocz - camera.z);
					camera.cam_pitch = (uint16_t)trig2_zangle;
					camera.cam_heading = (uint16_t)trig2_xyangle;
					replaycam.roll = 0;
					replay_replaymessage(MSG_CAMERA_FREE);
					replay_drawreplaybutton(0xB);
					replay_drawreplaybutton(0x10);
				}
				break;

			case KEY_L:
			case KEY_l:
				if (!maingameflag) {
					replay_drawreplaybutton(0x19);
					exitflag = 1;
					updateactionflag = 0;
					mission.player_status = 14;
				}
				break;

			case KEY_O:
			case KEY_o: {
				uint16_t saved = pstate.object_idx;
				int32_t dir;

				pstate.object_idx = 0xFFFEu;
				dir = (inputkey == KEY_O) ? -1 : +1;
				trackobject = user_picknexttarget(trackobject, dir);
				pstate.object_idx = saved;
				replay_drawreplaybutton(0xD);
				replay_drawreplaybutton(0xF);
			} break;

			case KEY_P:
			case KEY_p:
				if (updateactionflag) {
					updateactionflag = 0;
					replay_replaymessage(MSG_FILM_STOPPED);
					replay_drawreplaybutton(2);
				} else if (replaytotalcntdown < (uint32_t)replaytotalcnt) {
					updateactionflag = 1;
					replay_replaymessage(MSG_FILM_STARTED);
					replay_drawreplaybutton(3);
				} else {
					replay_replaymessage(MSG_FILM_END);
				}
				break;

			case KEY_R:
			case KEY_r:
				replay_drawreplaybutton(1);
				replay_rewindreplay();
				replay_replaymessage(MSG_FILM_REWOUND);
				replay_drawreplaybutton(0);
				break;

			case KEY_S:
			case KEY_s:
				if (maingameflag) {
#ifdef TIE_MODERN
					continuation->save_requested = true;
#else
					uint16_t result;
					if (replaymusic == 1) {
						replaymusic = 0;
						replayvolume = (int16_t)hilevel_ImGetMasterVol();
						hilevel_ImSetMasterVol(0);
						lolevel_ImPause();
					}
					replay_drawreplaybutton(7);
					result = replay_savereplay();
					replay_replaymessage(result);
					replay_drawreplaybutton(6);
					replay_outputclipname();
#endif
				} else {
					/* Retail: ship_idx 12 (TIE Advanced) joined 5..9 + 16 as
					 * valid "re-enter simulator" types. */
					uint16_t sidx = pstate.player->ship_idx;
					int is_tie = (sidx >= 5u && sidx <= 9u) || sidx == 12u || sidx == 16u;
					if (is_tie && !pstate.player_craft->flight_flag) {
						replay_drawreplaybutton(0x25);
						updateactionflag = 0;
						exitflag = 1;
						reentersimflag = 1;
					} else {
						replay_replaymessage(MSG_NO_SIM_AVAILABLE);
					}
				}
				break;

			case KEY_T:
			case KEY_t:
				if (trackobject == 0xFFFFu) {
					uint16_t saved = pstate.object_idx;
					pstate.object_idx = 0xFFFEu;
					trackobject = user_picknexttarget(0xFFFFu, 1);
					pstate.object_idx = saved;
					replay_drawreplaybutton(0xD);
					replay_drawreplaybutton(0xF);
				} else {
					trackobject = 0xFFFFu;
					replay_drawreplaybutton(0xC);
					replay_drawreplaybutton(0xE);
				}
				break;

			default:
				break;
		}
	}

	/* Mouse translation: unchanged between demo and retail. */
	btn_val = inputbuttons & 0xF;
	if (btn_val == 1 || btn_val == 2) {
		replaycam.view_zoom_rate += 128;
		if ((uint16_t)replaycam.view_zoom_rate > 0x6000u)
			replaycam.view_zoom_rate = 24576;
		if (btn_val == 1) {
			replaycam.view_zoom -= user_framerateadjust(replaycam.view_zoom_rate);
			if (replaycam.view_zoom < 0)
				replaycam.view_zoom = 0;
		} else {
			replaycam.view_zoom += user_framerateadjust(replaycam.view_zoom_rate);
			if (replaycam.view_zoom > 5120)
				replaycam.view_zoom = 5120;
		}
	} else {
		replaycam.view_zoom_rate = 64;
	}

	if (replaycam.view_camera_control) {
		int16_t rate;
		int16_t dx;
		int16_t dy;
		int16_t dz;

		fview_calcrotatemove((int16_t)camera.cam_pitch, (int16_t)camera.cam_heading, NULL);
		rate = user_framerateadjust(replaycam.view_zoom_rate);
		dx = (int16_t)((craftmoveX * rate) >> 15);
		dy = (int16_t)((craftmoveY * rate) >> 15);
		dz = (int16_t)((craftmoveZ * rate) >> 15);
		if (btn_val == 2) {
			dx = -dx;
			dy = -dy;
			dz = -dz;
		}
		if (btn_val == 1 || btn_val == 2) {
			replaycam.x += dx;
			if (replaycam.x < -16777216)
				replaycam.x = -16777216;
			if (replaycam.x > 0x1000000)
				replaycam.x = 0x1000000;
			replaycam.y += dy;
			if (replaycam.y < -16777216)
				replaycam.y = -16777216;
			if (replaycam.y > 0x1000000)
				replaycam.y = 0x1000000;
			replaycam.z += dz;
			if (replaycam.z < -16777216)
				replaycam.z = -16777216;
			if (replaycam.z > 0x1000000)
				replaycam.z = 0x1000000;
		}
		camera.cam_heading += (uint16_t)user_framerateadjust(inputdeltax);
		camera.cam_pitch -= (uint16_t)user_framerateadjust(inputdeltay);
	} else {
		replaycam.up_angle += user_framerateadjust(inputdeltax);
		replaycam.side_angle += user_framerateadjust(inputdeltay);
	}
}

// FUNCTION: TIE95 0x47190
void replay_calcreplayview(void) {
	uint16_t species;

	if (replaycam.view_target_obj < 0x3800)
		species = objects[replaycam.view_target_obj].ship_idx;
	else
		species = staticobjects[replaycam.view_target_obj - 14336].species;
	if (species != chasespecies) {
		replaycam.view_target_obj = pstate.object_idx;
		replay_drawreplaybutton(0x11);
	}

	if (!replaycam.view_camera_control) {
		if (replaycam.view_target_obj < 0x3800) {
			replaycam.roll = objects[replaycam.view_target_obj].roll;
			replaycam.cam_pitch = (uint16_t)objects[replaycam.view_target_obj].pitch;
			replaycam.cam_heading = (uint16_t)objects[replaycam.view_target_obj].heading;
		} else {
			replaycam.roll = (uint16_t)(staticobjects[replaycam.view_target_obj - 14336].roll_byte << 8);
			replaycam.cam_pitch =
				(uint16_t)(staticobjects[replaycam.view_target_obj - 14336].pitch_byte << 8);
			replaycam.cam_heading =
				(uint16_t)(staticobjects[replaycam.view_target_obj - 14336].heading_byte << 8);
		}
		camera.cam_pitch = replaycam.cam_pitch;
		camera.cam_heading = replaycam.cam_heading;
		camera.roll = replaycam.roll;
		camera.up_angle = replaycam.up_angle;
		camera.side_angle = replaycam.side_angle;
		fview_newcalcview(camera.roll, camera.cam_pitch, camera.cam_heading, 0, camera.side_angle,
						  camera.up_angle, NULL);
#ifdef TIE_MODERN
		TieFlightSnapshot_RecordCameraBasis();
#endif
		replay_movecambehind(replaycam.view_target_obj);
		camera.x = replaycam.x;
		camera.y = replaycam.y;
		camera.z = replaycam.z;
	} else {
		replaycam.view_camera_control = 1;
		camera.x = replaycam.x;
		camera.roll = 0;
		camera.up_angle = 0;
		camera.y = replaycam.y;
		camera.z = replaycam.z;
		camera.cam_pitch = replaycam.cam_pitch;
		camera.cam_heading = replaycam.cam_heading;
		camera.side_angle = 0;
		fview_newcalcview(0, camera.cam_pitch, camera.cam_heading, 0, 0, 0, NULL);
#ifdef TIE_MODERN
		TieFlightSnapshot_RecordCameraBasis();
#endif
	}

	if (trackobject != (uint16_t)0xFFFF) {
		if (trackobject < 0x3800)
			species = objects[trackobject].ship_idx;
		else
			species = staticobjects[trackobject - 14336].species;
		if (species != trackspecies) {
			trackobject = 0xFFFFu;
			replay_drawreplaybutton(0xC);
			replay_drawreplaybutton(0xE);
		} else {
			create_getworldposition(trackobject, 0);
			trig2_ctop(worldlocx - camera.x, worldlocy - camera.y, worldlocz - camera.z);
			replaycam.roll = camera.roll = 0;
			replaycam.cam_pitch = camera.cam_pitch = trig2_zangle;
			camera.up_angle = 0;
			camera.side_angle = 0;
			replaycam.cam_heading = camera.cam_heading = trig2_xyangle;
			fview_newcalcview(0, camera.cam_pitch, camera.cam_heading, 0, 0, 0, NULL);
#ifdef TIE_MODERN
			TieFlightSnapshot_RecordCameraBasis();
#endif
		}
	}
}

/* --------------------------------------------------------------------------
 * Camera pose
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x474B4
void replay_movecambehind(uint16_t obj_id) {
	uint16_t sp;
	uint16_t quarter_w;
	int32_t push_x;
	int32_t push_y;
	int32_t push_z;

	create_getworldposition(obj_id, 0);
	replaycam.x = worldlocx;
	replaycam.y = worldlocy;
	replaycam.z = worldlocz;

	if ((int)obj_id < 0x3800) {
		sp = objects[obj_id].ship_idx;
	} else {
		sp = staticobjects[obj_id - 0x3800].species;
	}
	quarter_w = species_table[sp].bound_hwidth;
	quarter_w >>= 2;

	push_x = 4 * ((quarter_w * worldeyeA3) >> 15);
	push_y = 4 * ((quarter_w * worldeyeB3) >> 15);
	push_z = 4 * ((quarter_w * worldeyeC3) >> 15);
	push_x += (replaycam.view_zoom * worldeyeA3) >> 15;
	push_y += (replaycam.view_zoom * worldeyeB3) >> 15;
	push_z += (replaycam.view_zoom * worldeyeC3) >> 15;
	replaycam.x -= push_x;
	replaycam.y -= push_y;
	replaycam.z -= push_z;
}

/* --------------------------------------------------------------------------
 * Message + widget helpers
 * -------------------------------------------------------------------------- */

/* replay_replaymessage — print a message from messagetable[msg_id] into
 * the in-flight message band. First byte of the template is a color/type
 * tag (< 8 indexes fontcolorconvert[], else default 0x42); '[' / ']'
 * nudge the text color up/down. Appends a '.' unless the final printable
 * character was one of '?','!',':',' '. Arms replaymsgtimer to 944 ticks. */
// FUNCTION: TIE95 0x475F4
// FUNCTION: TIE98 0x474BC0
void replay_replaymessage(uint16_t msg_id) {
	const unsigned char* p;
	unsigned char tag;
	unsigned char last;

	msg_readymessage();

	p = (const unsigned char*)messagetable[msg_id];
	tag = *p;
	if (tag < 8) {
		festring_settextcolor(fontcolorconvert[tag]);
		++p;
	} else {
		festring_settextcolor(0x42);
	}

	last = 'x';
	while (*p) {
		unsigned char c = *p;
		if (c == '[') {
			++textcolor;
			++p;
		} else if (c == ']') {
			--textcolor;
			++p;
		} else {
			outchar(c);
			last = c;
			++p;
		}
	}

	if (last != '?' && last != '!' && last != ':' && last != ' ') {
		outchar('.');
	}

	festring_setautofill(1);
	outchar('\n');
	festring_setautofill(0);
	festring_setfontsize(2);
	replaymsgtimer = 944;
}

// FUNCTION: TIE95 0x476CC
// FUNCTION: TIE98 0x474CA0
void replay_editstring(int16_t x, int16_t y, uint8_t limit, uint8_t* text, uint8_t background) {
	uint8_t position;
#ifdef TIE_MODERN
	ReplaySaveTask* continuation = landru_task_top();
	position = continuation->position;
	if (!continuation->editor_active) {
#endif
		festring_setautofill(1);
		for (position = 0; text[position] && text[position] != '\n' && position < limit; ++position)
			;
		festring_setcursor(x, y);
		festring_outstring(text);
		festring_setbackcolor(0x4A);
		outchar(' ');
		festring_setbackcolor(background);
		outchar('\n');
#ifdef TIE_MODERN
		continuation->position = position;
		continuation->editor_active = 1;
		if (TIE_DISPLAY_DX5) {
			FlightSurface_Unlock();
			FrontendDisplay_PresentFrontSurface();
			FrontendDisplay_PresentFrame();
		}
		return;
	}
	if (!TieInput_KeyPending()) {
		continuation->waiting = true;
		return;
	}
#endif
	do {
		feinput_getinput();
		if (keypress == 1)
			keypress = 8;
		if (keypress == 8 && position)
			--position;
		if (keypress >= 48) {
			if ((keypress < 65 && keypress >= 58) || (keypress < 97 && keypress >= 91) || keypress >= 123)
				keypress = 1;
		} else if (keypress != 13 && keypress != 45 && keypress != 0) {
			keypress = 1;
		}
		if (keypress != 1 && keypress != 13 && keypress != 0 && position < limit) {
			text[position] = (uint8_t)keypress;
			if (text[position] >= 'a' && text[position] <= 'z')
				text[position] -= 32;
			++position;
		}
		text[position] = 0;
		if (keypress) {
#ifdef TIE_MODERN
			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
#endif
			festring_setcursor(x, y);
			festring_outstring(text);
			festring_setbackcolor(0x4A);
			outchar(' ');
			festring_setbackcolor(background);
			outchar('\n');
#ifdef TIE_MODERN
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_PresentFrontSurface();
				FrontendDisplay_PresentFrame();
			}
#endif
		}
#ifdef TIE_MODERN
		continuation->position = position;
		if (keypress != 13)
			return;
#endif
	} while (keypress != 13);
#ifdef TIE_MODERN
	if (TIE_DISPLAY_DX5)
		FlightSurface_Lock();
#endif
	festring_setcursor(x, y);
	festring_outstring(text);
	outchar('\n');
#ifdef TIE_MODERN
	if (TIE_DISPLAY_DX5)
		FlightSurface_Unlock();
#endif
	festring_setautofill(0);
#ifdef TIE_MODERN
	continuation->editor_active = 0;
	if (TIE_DISPLAY_DX5) {
		FrontendDisplay_PresentFrontSurface();
		FrontendDisplay_PresentFrame();
	}
#endif
}
