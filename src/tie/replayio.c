#include "tie/replayio.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/replay_session_task.h"
#endif
#ifdef TIE_MODERN
#include "tie_runtime/storage/mission_records.h"
#endif
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/flight_task.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/replay_format.h"
#include "tie_runtime/runtime/replay_viewer_task.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/flight_checkpoint.h"

#include "tie/backdrp2.h" /* backdrop* arrays */
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"  /* blastcount, blastqueue */
#include "tie/gate.h"  /* gatepreviousx/y/z/..., currentgate, gatetimer */
#include "tie/laser.h" /* warheads[] */
#include "tie/logbuf2.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/msgroom.h" /* lasthistorymsg, numhistorymsgs */
#include "tie/panel.h"
#include "tie/panelrts.h"
#include "tie/replay.h"
#include "tie/rtsvga2.h" /* stardetaillevel */
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/user.h"                           /* soundvolflag, musicvolflag */
#include "tie_runtime/runtime/inflight_state.h" /* inflight_* volumes + flags */
#include "tie_runtime/runtime/profile.h"

#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <landru/error.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#endif
#include <stdint.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Module-owned globals (watdbg: replayio.c's OBJ).
 * -------------------------------------------------------------------------- */

/* Static (pointer, size) table of dynamic-state regions serialized by
 * replayio_copytosave / copyfromsave and (retail only) replay_save-
 * replay / loadreplay. 67 entries + a NULL terminator. The ordering
 * matches the retail binary's Z_TIE__.EXE data section at 0xC7344 /
 * 0xC7454 — replay clip files are only compatible across builds that
 * preserve this order. */
void* savearrayptrs[68] = {
	objects,       /* [ 0] FlightObject[NUM_OBJECTS]    */
	staticobjects, /* [ 1] StaticObject[...]            */
	crafts,        /* [ 2] CraftData[NUM_CRAFTS]        */
	warheads,      /* [ 3] WarheadRecord[...]           */
	&_date,        /* [ 4] mission clock (8 bytes)      */
	timeleft,      /* [ 5] mission time-left (8 bytes)  */
#ifdef TIE_MODERN
	TieReplayMissionHeaderImage, /* [6] native encoded mission header */
#else
	&mission_file_header, /* [6] original mission header */
#endif
	&mission,                /* [ 7] RUNTIME_MissionState         */
	&pstate,                 /* [ 8] 292-byte player-state block  */
	&music_state,            /* [ 9]                              */
	&music_intensity,        /* [10]                              */
	&musicflag,              /* [11]                              */
	&currenttarget,          /* [12]                              */
	&currenttargetcomp,      /* [13]                              */
	&bluetarget,             /* [14]                              */
	&targetblinkstate,       /* [15]                              */
	&targetblinkflag,        /* [16]                              */
	&blinkticks,             /* [17]                              */
	timers,                  /* [18] timers[20] (40 bytes)        */
	&currentdebrisslot,      /* [19]                              */
	backdropposition,        /* [20] backdrop skybox slot map     */
	backdropspecies,         /* [21] skybox-species table         */
	&backdropfrontcnt,       /* [22]                              */
	&backdropbackcnt,        /* [23]                              */
	&backdroptopcnt,         /* [24]                              */
	&backdropbottomcnt,      /* [25]                              */
	&backdropleftcnt,        /* [26]                              */
	&backdroprightcnt,       /* [27]                              */
	&stardetaillevel,        /* [28]                              */
	&drawbackdropflag,       /* [29]                              */
	&drawdebrisflag,         /* [30]                              */
	&starshipexplodetail,    /* [31]                              */
	&starshipdetail,         /* [32]                              */
	&hyperspacedetail,       /* [33]                              */
	&shipdetailvalue,        /* [34]                              */
	&shipdetailpolycnt,      /* [35]                              */
	&drawmarkingsflag,       /* [36]                              */
	&detaillevel,            /* [37]                              */
	&cheatingflag,           /* [38]                              */
	&inflight_music_vol,     /* [39]                              */
	&inflight_sound_vol,     /* [40]                              */
	&inflight_speech_vol,    /* [41]                              */
	&soundvolflag,           /* [42]                              */
	&musicvolflag,           /* [43]                              */
	&inflight_unlimited,     /* [44]                              */
	&inflight_invulnerable,  /* [45]                              */
	&inflight_collision,     /* [46]                              */
	&acceleratedtimectr,     /* [47]                              */
	&acceleratedtimesetting, /* [48]                              */
	&musicenabled,           /* [49]                              */
	&sfxenabled,             /* [50]                              */
	&voiceenabled,           /* [51]                              */
	&palette_cycle_user,     /* [52] = demo's colorcycleuserflag  */
	&gouraudflag,            /* [53]                              */
	&blastcount,             /* [54]                              */
	blastqueue,              /* [55] blastqueue (32 bytes)        */
	&idnumber,               /* [56]                              */
	&lasthistorymsg,         /* [57]                              */
	&numhistorymsgs,         /* [58]                              */
	gatepreviousx,           /* [59] gatepreviousx[4]             */
	gatepreviousy,           /* [60] gatepreviousy[4]             */
	gatepreviousz,           /* [61] gatepreviousz[4]             */
	gatepreviousroll,        /* [62] gatepreviousroll[4]          */
	gatepreviouspitch,       /* [63] gatepreviouspitch[4]         */
	gatepreviousheading,     /* [64] gatepreviousheading[4]       */
	&currentgate,            /* [65]                              */
	gatetimer,               /* [66] gatetimer[3]                 */
	NULL                     /* [67] terminator                   */
};

uint32_t savearraysizes[68] = {
	sizeof(objects),       /* [ 0] */
	sizeof(staticobjects), /* [ 1] */
	sizeof(crafts),        /* [ 2] */
	sizeof(warheads),      /* [ 3] */
	sizeof(_date),         /* [ 4] = 8 */
	sizeof(timeleft),      /* [ 5] = 8 */
#ifdef TIE_MODERN
	sizeof(TieReplayMissionHeaderImage), /* [6] native encoded mission header */
#else
	206, /* [6] DOS retail saved prefix */
#endif
	sizeof(mission),                /* [ 7] */
	sizeof(pstate),                 /* [ 8] = 292 (300 on 64-bit) */
	sizeof(music_state),            /* [ 9] = 2 */
	sizeof(music_intensity),        /* [10] = 2 */
	sizeof(musicflag),              /* [11] = 1 */
	sizeof(currenttarget),          /* [12] = 2 */
	sizeof(currenttargetcomp),      /* [13] = 2 */
	sizeof(bluetarget),             /* [14] = 2 */
	sizeof(targetblinkstate),       /* [15] = 2 */
	sizeof(targetblinkflag),        /* [16] = 2 */
	sizeof(blinkticks),             /* [17] = 2 */
	sizeof(timers),                 /* [18] = 40 */
	sizeof(currentdebrisslot),      /* [19] = 2 */
	sizeof(backdropposition),       /* [20] = 64 */
	sizeof(backdropspecies),        /* [21] = 64 */
	sizeof(backdropfrontcnt),       /* [22] = 2 */
	sizeof(backdropbackcnt),        /* [23] = 2 */
	sizeof(backdroptopcnt),         /* [24] = 2 */
	sizeof(backdropbottomcnt),      /* [25] = 2 */
	sizeof(backdropleftcnt),        /* [26] = 2 */
	sizeof(backdroprightcnt),       /* [27] = 2 */
	sizeof(stardetaillevel),        /* [28] = 2 */
	sizeof(drawbackdropflag),       /* [29] = 1 */
	sizeof(drawdebrisflag),         /* [30] = 1 */
	sizeof(starshipexplodetail),    /* [31] = 2 */
	sizeof(starshipdetail),         /* [32] = 2 */
	sizeof(hyperspacedetail),       /* [33] = 2 */
	sizeof(shipdetailvalue),        /* [34] = 2 */
	sizeof(shipdetailpolycnt),      /* [35] = 2 */
	sizeof(drawmarkingsflag),       /* [36] = 1 */
	sizeof(detaillevel),            /* [37] = 2; host replay format */
	sizeof(cheatingflag),           /* [38] = 1 */
	sizeof(inflight_music_vol),     /* [39] = 1 */
	sizeof(inflight_sound_vol),     /* [40] = 1 */
	sizeof(inflight_speech_vol),    /* [41] = 1 */
	sizeof(soundvolflag),           /* [42] = 1 */
	sizeof(musicvolflag),           /* [43] = 1 */
	sizeof(inflight_unlimited),     /* [44] = 1 */
	sizeof(inflight_invulnerable),  /* [45] = 1 */
	sizeof(inflight_collision),     /* [46] = 1 */
	sizeof(acceleratedtimectr),     /* [47] = 1 */
	sizeof(acceleratedtimesetting), /* [48] = 1 */
	sizeof(musicenabled),           /* [49] = 1 */
	sizeof(sfxenabled),             /* [50] = 1 */
	sizeof(voiceenabled),           /* [51] = 1 */
	sizeof(palette_cycle_user),     /* [52] = 1 */
	sizeof(gouraudflag),            /* [53] = 1 */
	sizeof(blastcount),             /* [54] = 1 */
	sizeof(blastqueue),             /* [55] = 32 */
	sizeof(idnumber),               /* [56] = 2 */
	sizeof(lasthistorymsg),         /* [57] = 2 */
	sizeof(numhistorymsgs),         /* [58] = 2 */
	sizeof(gatepreviousx),          /* [59] = 16 */
	sizeof(gatepreviousy),          /* [60] = 16 */
	sizeof(gatepreviousz),          /* [61] = 16 */
	sizeof(gatepreviousroll),       /* [62] = 8 */
	sizeof(gatepreviouspitch),      /* [63] = 8 */
	sizeof(gatepreviousheading),    /* [64] = 8 */
	sizeof(currentgate),            /* [65] = 2 */
	sizeof(gatetimer),              /* [66] = 6 */
	0                               /* [67] terminator */
};
uint8_t replayviewptr[16];

/* Panel section pointers filled in by panel_loadcontrolpanel. In retail
 * the CAMERA / FILM panels still only have 3 sections (image, mask,
 * palette), so three slots suffice. */
static void* section_ptrs[3];

/* Disk filename for the replay input-buffer checkpoint. */
static const char kBufferTempFile[] = "rpybuff.tmp";

enum {
	REPLAY_BUFFER_TEMP_HEADER_SIZE = 8,
	REPLAY_BUFFER_TEMP_VERSION = 2,
};

/* --------------------------------------------------------------------------
 * Checkpoint I/O (savegame.rpy / start.rpy)
 *
 * Format:
 *   each (ptr, size) of savearrayptrs/sizes until nullptr
 *   0x36C0 fg_array
 *   0x5A0  radiomsg
 *   0x70   cut
 *   0x900  fgstatus
 *   0xA1   species_table[i].load_flags
 *   0x198  camera block
 * -------------------------------------------------------------------------- */

static int write_raw_block(const void* src, size_t bytes, TieFile* fp) {
	const uint8_t* p = (const uint8_t*)src;
	size_t i;

	for (i = 0; i < bytes; ++i) {
		if (TieStorage_Putc(p[i], fp) == TIE_EOF)
			return 0;
	}
	return 1;
}

static int read_raw_block(void* dst, size_t bytes, TieFile* fp) {
	uint8_t* p = (uint8_t*)dst;
	size_t i;

	for (i = 0; i < bytes; ++i) {
		int c = TieStorage_Getc(fp);
		if (c == TIE_EOF)
			return 0;
		p[i] = (uint8_t)c;
	}
	return 1;
}

/* PORT: retail checkpoints contain absolute addresses into fixed-image
 * globals. Rebuild those aliases after PIE/ASLR relocation. */
static void replayio_port_rebind_checkpoint_pointers(void) {
	size_t i;

	for (i = 0; i < NUM_OBJECTS; ++i) {
		FlightObject* object = &objects[i];
		if (!object->ship_idx) {
			object->craft_ptr = NULL;
		} else if (i < NUM_CRAFTS) {
			object->craft_ptr = &crafts[i];
		} else if (i < WARHEAD_SLOT_END) {
			object->craft_ptr = (CraftData*)&warheads[i - NUM_CRAFTS];
		} else {
			object->craft_ptr = NULL;
		}
	}

	pstate.player = &objects[pstate.object_idx];
	pstate.player_craft = pstate.player->craft_ptr;
}

// FUNCTION: TIE95 0x478A0
int16_t replayio_copytosave(const char* fname) {
	size_t i;
#ifdef TIE_MODERN
	uint8_t fg_save_buf[48 * EFGSTRUCT_DISK_SIZE];
#endif

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, fname, "wb", 0))
		return 0;

	/* Refresh the slot-[6] sidecar from the live mission_file_header so
	 * the generic raw-byte loop emits the canonical 456-byte LE image. */
#ifdef TIE_MODERN
	MissionFile_encode(TieReplayMissionHeaderImage, &mission_file_header);
#endif

	for (i = 0; savearrayptrs[i]; ++i) {
		uint32_t sz = savearraysizes[i];
		int16_t n = fediskio_writefileblock(savearrayptrs[i], 1, (int)sz, fileptr);
		if ((uint32_t)n != sz) {
			TieStorage_Close(fileptr);
			return 0;
		}
	}

	/* The save format expects fg_array as the on-disk 48 x 292-byte
	 * little-endian image; native builds encode the word fields through
	 * a buffer for host-endian independence. */

#ifdef TIE_MODERN
	for (i = 0; i < 48; ++i)
		EFGStruct_encode(fg_save_buf + i * EFGSTRUCT_DISK_SIZE, &fg_array[i]);
	if (!write_raw_block(fg_save_buf, sizeof fg_save_buf, fileptr))
#else
	if (!write_raw_block(fg_array, 48 * EFGSTRUCT_DISK_SIZE, fileptr))
#endif
		goto fail;
	if (!write_raw_block(radiomsg, 0x5A0, fileptr))
		goto fail;
	if (!write_raw_block(cut, 0x70, fileptr))
		goto fail;
	if (!write_raw_block(fgstatus, 0x900, fileptr))
		goto fail;

	for (i = 0; i < NUM_SPECIES; ++i) {
		if (TieStorage_Putc(species_table[i].load_flags, fileptr) == TIE_EOF)
			goto fail;
	}

	if (fediskio_writefileblock(&camera, sizeof(camera), 1, fileptr) != 1 ||
		!TieFlightCheckpoint_Write(fileptr))
		goto fail;

	TieStorage_Close(fileptr);
	msg_clearmessagequeue();
	return 1;

fail:
	TieStorage_Close(fileptr);
	return 0;
}

// FUNCTION: TIE95 0x47A08
int16_t replayio_copyfromsave(const char* fname) {
	size_t i;
#ifdef TIE_MODERN
	uint8_t fg_load_buf[48 * EFGSTRUCT_DISK_SIZE];
#endif

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, fname, "rb", 1))
		return 0;

	for (i = 0; savearrayptrs[i]; ++i) {
		uint32_t sz = savearraysizes[i];
		int16_t n = fediskio_readfileblock(savearrayptrs[i], 1, sz, fileptr);
		if ((uint32_t)n != sz) {
			TieStorage_Close(fileptr);
			return 0;
		}
	}

	/* Slot [6] just landed in TieReplayMissionHeaderImage; decode
	 * back into the live struct. */
#ifdef TIE_MODERN
	MissionFile_decode(&mission_file_header, TieReplayMissionHeaderImage);
#endif

	/* Read 48 x 292-byte fg records as a contiguous on-disk image,
	 * then decode each into the runtime fg_array (whose element width
	 * may differ from the disk size due to natural alignment). */

#ifdef TIE_MODERN
	if (!read_raw_block(fg_load_buf, sizeof fg_load_buf, fileptr)) {
#else
	if (!read_raw_block(fg_array, 48 * EFGSTRUCT_DISK_SIZE, fileptr)) {
#endif
		TieStorage_Close(fileptr);
		return 0;
	}
#ifdef TIE_MODERN
	for (i = 0; i < 48; ++i)
		EFGStruct_decode(&fg_array[i], fg_load_buf + i * EFGSTRUCT_DISK_SIZE);
#endif
	if (!read_raw_block(radiomsg, 0x5A0, fileptr)) {
		TieStorage_Close(fileptr);
		return 0;
	}
	if (!read_raw_block(cut, 0x70, fileptr)) {
		TieStorage_Close(fileptr);
		return 0;
	}
	if (!read_raw_block(fgstatus, 0x900, fileptr)) {
		TieStorage_Close(fileptr);
		return 0;
	}

	for (i = 0; i < NUM_SPECIES; ++i) {
		int c = TieStorage_Getc(fileptr);
		if (c == TIE_EOF) {
			TieStorage_Close(fileptr);
			return 0;
		}
		species_table[i].load_flags = (uint8_t)c;
	}

	if (fediskio_readfileblock(&camera, sizeof(camera), 1, fileptr) != 1 ||
		!TieFlightCheckpoint_Read(fileptr)) {
		TieStorage_Close(fileptr);
		return 0;
	}
	TieInput_ResetThrottle();
	inputthrottle = UINT32_MAX;
	replayio_port_rebind_checkpoint_pointers();
	msg_clearmessagequeue();
	TieStorage_Close(fileptr);
	return 1;
}

/* --------------------------------------------------------------------------
 * Input-buffer spooling
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x47B2C
int replayio_openreplayinputfile(void) {
	/* Drop any stale .spl, then create a fresh one prefixed with the
	 * current versioned header. Subsequent replayio_spoolreplayinput calls
	 * open with "ab" and append fixed-size records after the header. */
	TieFile* fp;
	int ok;

	TieStorage_Remove(TIE_FILE_ROOT_TEMP, inputspoolfile);
	fp = TieStorage_Open(TIE_FILE_ROOT_TEMP, inputspoolfile, "wb");
	if (!fp)
		return 0;
	ok = TieReplayFormat_WriteHeader(fp);
	if (TieStorage_Close(fp) != 0)
		return 0;
	return ok;
}

/* replayio_spoolreplayinput — append the valid records in this chunk. */
// FUNCTION: TIE95 0x47B3C
int16_t replayio_spoolreplayinput(void) {
	TieFile* saved;
	const uint8_t* bufp;
	uint16_t frame;

	if (!replayspoolflag)
		return 1;
	if (!replaybuffercnt)
		return 1;

	if (!endgamereplayflag) {
		msg_messageprintf(MSG_CAMERA_SAVING);
	}

	saved = fileptr;
	bufp = (const uint8_t*)replaybufferstart;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "ab", 0)) {
		fileptr = saved;
		return 0;
	}

	for (frame = 0; frame < replaybuffercnt; ++frame) {
		if (TieStorage_Write(bufp, 1, REPLAYINPUTFRAME_DISK_SIZE, fileptr) != REPLAYINPUTFRAME_DISK_SIZE) {
			TieStorage_Close(fileptr);
			fileptr = saved;
			return 0;
		}
		bufp += REPLAYINPUTFRAME_DISK_SIZE;
	}
	if (TieStorage_Close(fileptr) != 0) {
		fileptr = saved;
		return 0;
	}
	if (!endgamereplayflag) {
		msg_messageprintf(MSG_CAMERA_SAVED);
	}
	fileptr = saved;
	return 1;
}

// FUNCTION: TIE95 0x47C44
int16_t replayio_savereplaybuffer(void) {
	uint8_t header[REPLAY_BUFFER_TEMP_HEADER_SIZE] = { 'R', 'B', 'U', 'F' };
	size_t valid_bytes;

	if (replaybuffercnt > REPLAY_INPUT_CHUNK_FRAMES)
		return 0;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, kBufferTempFile, "wb", 0))
		return 0;
	header[4] = REPLAY_BUFFER_TEMP_VERSION;
	header[6] = (uint8_t)replaybuffercnt;
	header[7] = (uint8_t)(replaybuffercnt >> 8);
	valid_bytes = (size_t)replaybuffercnt * REPLAYINPUTFRAME_DISK_SIZE;
	if (TieStorage_Write(header, 1, sizeof header, fileptr) != sizeof header ||
		TieStorage_Write(replaybufferstart, 1, valid_bytes, fileptr) != valid_bytes) {
		TieStorage_Close(fileptr);
		return 0;
	}
	return (TieStorage_Close(fileptr) == 0);
}

// FUNCTION: TIE95 0x47CA4
int replayio_restorereplaybuffer(void) {
	uint8_t header[REPLAY_BUFFER_TEMP_HEADER_SIZE];
	uint16_t frame_count;
	size_t valid_bytes;

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, kBufferTempFile, "rb", 1))
		return 0;

	if (TieStorage_Read(header, 1, sizeof header, fileptr) != sizeof header ||
		memcmp(header, "RBUF", 4) != 0 || header[4] != REPLAY_BUFFER_TEMP_VERSION || header[5] != 0) {
		TieStorage_Close(fileptr);
		return 0;
	}
	frame_count = (uint16_t)(header[6] | (uint16_t)header[7] << 8);
	if (frame_count > REPLAY_INPUT_CHUNK_FRAMES) {
		TieStorage_Close(fileptr);
		return 0;
	}
	valid_bytes = (size_t)frame_count * REPLAYINPUTFRAME_DISK_SIZE;
	memset(replaybufferstart, 0, REPLAY_INPUT_BUFFER_BYTES);
	if (TieStorage_Read(replaybufferstart, 1, valid_bytes, fileptr) != valid_bytes) {
		TieStorage_Close(fileptr);
		return 0;
	}
	if (TieStorage_Close(fileptr) != 0)
		return 0;
	replaybuffercnt = frame_count;
	replayptr = (uint8_t*)replaybufferstart + valid_bytes;
	return 1;
}

/* --------------------------------------------------------------------------
 * Viewer entry point
 * -------------------------------------------------------------------------- */

/* Build panelname = cockpitdir + infix + ".PNL". */

/* Load the camera-viewer panel.
 *   panel_name is "CAMERA" (cockpit) or "FILM" (stand-alone viewer).
 *   panel_x / panel_y / panel_depth / panel_width drive the viewport.
 * The displaycorner value is derived directly via rtsvga2_calcpositionVGA
 * (= panel_y * screenMemWidth + panel_x). */

/* Resolution-change detection on exit: retail saves flightResolution at
 * viewer entry; on return to sim, if the user changed it (via the
 * viewer's OPTION row hook), re-init graphics + reload scaled fonts. */

/* --------------------------------------------------------------------------
 * replayio_setreturnview -- restore the cockpit when re-entering the live
 * simulator. Unchanged between demo and retail.
 * -------------------------------------------------------------------------- */
// FUNCTION: TIE95 0x483E4
void replayio_setreturnview(void) {
	farbufferptr = (uint8_t*)panelpartsptr;
	{
		size_t n = 0;
		size_t s;
		const char* pnl;

		while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
			panelname[n] = cockpitdir[n];
			++n;
		}
		s = 0;
		while (n + 1 < sizeof(panelname) && parts[s]) {
			panelname[n++] = parts[s++];
		}
		pnl = ".PNL";
		s = 0;
		while (n + 1 < sizeof(panelname) && pnl[s]) {
			panelname[n++] = pnl[s++];
		}
		panelname[n] = '\0';
	}
	fediskio_loadbufferdata(panelname, 0, parts[9] + (uint8_t)parts[10], 0);

	if (camera.view_zoom_flag) {
		lastpilotpaneldraw = 0xFFFFu;
		camera.pilotview = 0xFFu;
		panelrts_setnewpilotview(0x12);
	} else if (camera.view_target_obj == pstate.object_idx) {
		uint16_t v = (uint16_t)camera.pilotview |
					 (uint16_t)((uint8_t)(pstate.object_idx >> 8) ^ (uint8_t)(camera.view_target_obj >> 8))
						 << 8;
		lastpilotpaneldraw = 0xFFFFu;
		camera.pilotview = 0xFFu;
		panelrts_setnewpilotview(v);
	} else {
		camera.pilotview = 0xFFu;
		lastpilotpaneldraw = 0xFFFFu;
		panelrts_setnewpilotview(0x12);
	}
	msg_messageinit();
}

// FUNCTION: TIE95 0x47CF4
// FUNCTION: TIE98 0x475350
void replayio_replayscreen(void) {
	int16_t saved_res;
#ifdef TIE_MODERN
	ReplayioTask* continuation = landru_task_top();
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	const bool tie98_logic = TieProfile_UsesTie98Logic();
	saved_res = continuation->saved_res;
	if (continuation->phase == REPLAYIO_PHASE_PREPARE)
#elif defined(TIE98)
	const bool tie98_display = true;
	const bool tie98_logic = true;
#else
	const bool tie98_display = false;
	const bool tie98_logic = false;
#endif
	{
		saved_res = (int16_t)flightResolution;
		if (tie98_logic) {
			uint8_t saved_mapflag = mapflag;
			mapflag = 1;
			FSFX_UpdatePlayerEngineSound();
			mapflag = saved_mapflag;
		}
		replayvolume = (int16_t)imuse_get_master_vol(im);
		imuse_set_master_vol(im, 0);
		imuse_pause(im);
		replaymusic = 0;
#ifdef TIE_MODERN
		continuation->saved_res = saved_res;
		continuation->phase = REPLAYIO_PHASE_ENTER;
		return;
#endif
	}
#ifdef TIE_MODERN
	if (continuation->phase == REPLAYIO_PHASE_ENTER)
#endif
	{
		if (maingameflag && !replayio_copytosave(replaysavegamefile)) {
#ifdef TIE_MODERN
			continuation->finished = true;
#endif
			return;
		}
		recordingreplay = 0;
		replayviewmode = 1;
		if (tie98_display)
			FlightSurface_Lock();
		{
			if (maingameflag) {
				memset(replayclipname, 0, sizeof(replayclipname));
				strncpy(replayclipname, "UNTITLED", sizeof(replayclipname) - 1);
				if (flightResolution == TIE_FLIGHT_RES_VGA) {
					{
						uint32_t dc;

						farbufferptr = (uint8_t*)panelpartsptr;
						{
							size_t n = 0;
							size_t s;
							const char* pnl;

							while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
								panelname[n] = cockpitdir[n];
								++n;
							}
							s = 0;
							while (n + 1 < sizeof(panelname) && "camerap"[s]) {
								panelname[n++] = "camerap"[s++];
							}
							pnl = ".PNL";
							s = 0;
							while (n + 1 < sizeof(panelname) && pnl[s]) {
								panelname[n++] = pnl[s++];
							}
							panelname[n] = '\0';
						}
						fediskio_loadbufferdata(panelname, 0xE3, 38, 0);
						temppanelptr = newbuf;
						panel_loadcontrolpanel((char*)"CAMERA", section_ptrs, 3);

						buildpalette((uint8_t*)section_ptrs[2], 0, 64);
						if (tie98_logic) {
							festring_setbackcolor(deepspacecolor);
							festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
							clearwindow();
						}
						drawshape(section_ptrs[0], 0, 0, 253, 0);

						dc = rtsvga2_calcpositionVGA(0, 17);
						logbuf2_setbufferdimensions(320, 135, dc);

						panel_copymaskdata((char*)section_ptrs[1], pixelswide, pixelsdeep, 0);
						transfm2_screenyoffset = 0;
					}
				} else {
					{
						uint32_t dc;

						farbufferptr = (uint8_t*)panelpartsptr;
						{
							size_t n = 0;
							size_t s;
							const char* pnl;

							while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
								panelname[n] = cockpitdir[n];
								++n;
							}
							s = 0;
							while (n + 1 < sizeof(panelname) && "camerap"[s]) {
								panelname[n++] = "camerap"[s++];
							}
							pnl = ".PNL";
							s = 0;
							while (n + 1 < sizeof(panelname) && pnl[s]) {
								panelname[n++] = pnl[s++];
							}
							panelname[n] = '\0';
						}
						fediskio_loadbufferdata(panelname, 0xE3, 38, 0);
						temppanelptr = newbuf;
						panel_loadcontrolpanel((char*)"CAMERA", section_ptrs, 3);

						buildpalette((uint8_t*)section_ptrs[2], 0, 64);
						if (tie98_logic) {
							festring_setbackcolor(deepspacecolor);
							festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
							clearwindow();
						}
						drawshape(section_ptrs[0], 0, 0, 253, 0);

						dc = rtsvga2_calcpositionVGA(0, 40);
						logbuf2_setbufferdimensions(640, 325, dc);

						panel_copymaskdata((char*)section_ptrs[1], pixelswide, pixelsdeep, 0);
						transfm2_screenyoffset = 0;
					}
				}
				msg_messageinit();
			} else {
				/* Stand-alone viewer uses "FILM" (retail) instead of demo's
				 * "XFILM1". TIE98 adds a separate SVGA layout. */
				replay_loadreplay();
				replayio_copyfromsave(replaystartfile);
				fediskio_loadspecies();
				panel_loadpaneldata();
				/* Per-mission voice .LFD load. Retail calls FSFX_loadvoicelfd
				 * here in the stand-alone viewer path (not in the in-flight
				 * replay branch — the live mission's voice cues are still
				 * resident in soundhandles from create_createmission). */
				fsfx_loadvoicelfd();
				msg_messageinit();
				{
					if (tie_is_high_resolution_flight()) {
						uint32_t dc;

						farbufferptr = (uint8_t*)panelpartsptr;
						{
							size_t n = 0;
							size_t s;
							const char* pnl;

							while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
								panelname[n] = cockpitdir[n];
								++n;
							}
							s = 0;
							while (n + 1 < sizeof(panelname) && "camerap"[s]) {
								panelname[n++] = "camerap"[s++];
							}
							pnl = ".PNL";
							s = 0;
							while (n + 1 < sizeof(panelname) && pnl[s]) {
								panelname[n++] = pnl[s++];
							}
							panelname[n] = '\0';
						}
						fediskio_loadbufferdata(panelname, 0xE3, 38, 0);
						temppanelptr = newbuf;
						panel_loadcontrolpanel((char*)"FILM", section_ptrs, 3);

						buildpalette((uint8_t*)section_ptrs[2], 0, 64);
						if (tie98_logic) {
							festring_setbackcolor(deepspacecolor);
							festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
							clearwindow();
						}
						drawshape(section_ptrs[0], 0, 0, 253, 0);

						dc = rtsvga2_calcpositionVGA(28, 16);
						logbuf2_setbufferdimensions(584, 298, dc);

						panel_copymaskdata((char*)section_ptrs[1], pixelswide, pixelsdeep, 0);
						transfm2_screenyoffset = 0;
					} else {
						uint32_t dc;

						farbufferptr = (uint8_t*)panelpartsptr;
						{
							size_t n = 0;
							size_t s;
							const char* pnl;

							while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
								panelname[n] = cockpitdir[n];
								++n;
							}
							s = 0;
							while (n + 1 < sizeof(panelname) && "camerap"[s]) {
								panelname[n++] = "camerap"[s++];
							}
							pnl = ".PNL";
							s = 0;
							while (n + 1 < sizeof(panelname) && pnl[s]) {
								panelname[n++] = pnl[s++];
							}
							panelname[n] = '\0';
						}
						fediskio_loadbufferdata(panelname, 0xE3, 38, 0);
						temppanelptr = newbuf;
						panel_loadcontrolpanel((char*)"FILM", section_ptrs, 3);

						buildpalette((uint8_t*)section_ptrs[2], 0, 64);
						if (tie98_logic) {
							festring_setbackcolor(deepspacecolor);
							festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
							clearwindow();
						}
						drawshape(section_ptrs[0], 0, 0, 253, 0);

						dc = rtsvga2_calcpositionVGA(0, 8);
						logbuf2_setbufferdimensions(320, 123, dc);

						panel_copymaskdata((char*)section_ptrs[1], pixelswide, pixelsdeep, 0);
						transfm2_screenyoffset = 0;
					}
				}
			}
		}
		replay_rewindreplay();
		if (tie98_display)
			FlightSurface_Unlock();
#ifdef TIE_MODERN
		continuation->phase = REPLAYIO_PHASE_VIEW;
#endif
	}
	for (;;) {
#ifdef TIE_MODERN
		if (continuation->phase == REPLAYIO_PHASE_VIEW)
#endif
		{
			reentersimflag = 0;
			festring_showscreen();
			if (tie98_display) {
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
				FrontendDisplay_BlitOffscreenToRenderSurface();
			}
#ifdef TIE_MODERN
			continuation->phase = REPLAYIO_PHASE_AFTER_VIEWER;
			TieReplayViewer_Begin();
			return;
#else
			replay_doreplayscreen();
#endif
		}
#ifdef TIE_MODERN
		if (continuation->phase == REPLAYIO_PHASE_AFTER_VIEWER)
#endif
		{
			if (replaymusic) {
				replayvolume = (int16_t)imuse_get_master_vol(im);
				imuse_set_master_vol(im, 0);
				imuse_pause(im);
				replaymusic = 0;
			}
			replayviewmode = 0;
			if (maingameflag) {
				if (flightResolution != saved_res) {
					flightResolution = saved_res;
#ifdef TIE_MODERN
					if (!TieClassicDisplay_ActivateFlight()) {
						xerror_Set_Landru_Error(12);
						continuation->finished = true;
						return;
					}
#endif
					tie_initflightresolution();
					rtsvga2_initgraphVGA();
					feinput_setupgraphics((uint8_t)detaillevel);
					fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "xtiny64.fnt", fontptrtiny);
					fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "dmicro64.fnt", fontptrmicro);
					festring_setfontsize(1);
				}
				replayio_copyfromsave(replaysavegamefile);
				replayio_setreturnview();
				if (!replaymusic) {
					imuse_set_master_vol(im, (uint16_t)replayvolume);
					imuse_resume(im);
					replaymusic = 1;
				}
#ifdef TIE_MODERN
				continuation->finished = true;
#endif
				return;
			}
			if (!reentersimflag) {
				if (tie98_display)
					FlightSurface_Lock();
				if (tie_is_high_resolution_flight())
					festring_setbound(28, 16, 611, 315);
				else
					festring_setbound(14, 8, 306, 131);
				festring_setbackcolor(0x40);
				clearwindow();
				if (tie98_display)
					FlightSurface_Unlock();
				if (!replaymusic) {
					imuse_set_master_vol(im, (uint16_t)replayvolume);
					imuse_resume(im);
					replaymusic = 1;
				}
#ifdef TIE_MODERN
				continuation->finished = true;
#endif
				return;
			}
			blank();
			recordingreplay = 0;
			numhistorymsgs = 0;
			camera.view_zoom_flag = 0;
			camera.up_angle = 0;
			mission.end_flag = 0;
			camera.view_target_obj = pstate.object_idx;
			camera.pilotview = 0;
			camera.side_angle = 0;
			camera.view_dir_dirty = 0;
			blastcount = 0;
			msg_clearmessagequeue();
			replayio_setreturnview();
			if (!replaymusic) {
				imuse_set_master_vol(im, (uint16_t)replayvolume);
				imuse_resume(im);
				replaymusic = 1;
			}
#ifdef TIE_MODERN
			continuation->phase = REPLAYIO_PHASE_AFTER_REENTERSIM;
			TieFlightTask_BeginMission();
			return;
#else
			while (!mission.end_flag)
				tie_doframe();
#endif
		}
		replayvolume = (int16_t)imuse_get_master_vol(im);
		imuse_set_master_vol(im, 0);
		imuse_pause(im);
		replaymusic = 0;
		blank();
		recordingreplay = 0;
		replayviewmode = 1;
		if (tie98_display)
			FlightSurface_Lock();
		replay_loadreplay();
		replayio_copyfromsave(replaystartfile);
		msg_messageinit();
		{
			if (tie_is_high_resolution_flight()) {
				uint32_t dc;

				farbufferptr = (uint8_t*)panelpartsptr;
				{
					size_t n = 0;
					size_t s;
					const char* pnl;

					while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
						panelname[n] = cockpitdir[n];
						++n;
					}
					s = 0;
					while (n + 1 < sizeof(panelname) && "camerap"[s]) {
						panelname[n++] = "camerap"[s++];
					}
					pnl = ".PNL";
					s = 0;
					while (n + 1 < sizeof(panelname) && pnl[s]) {
						panelname[n++] = pnl[s++];
					}
					panelname[n] = '\0';
				}
				fediskio_loadbufferdata(panelname, 0xE3, 38, 0);
				temppanelptr = newbuf;
				panel_loadcontrolpanel((char*)"FILM", section_ptrs, 3);

				buildpalette((uint8_t*)section_ptrs[2], 0, 64);
				if (tie98_logic) {
					festring_setbackcolor(deepspacecolor);
					festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
					clearwindow();
				}
				drawshape(section_ptrs[0], 0, 0, 253, 0);

				dc = rtsvga2_calcpositionVGA(28, 16);
				logbuf2_setbufferdimensions(584, 298, dc);

				panel_copymaskdata((char*)section_ptrs[1], pixelswide, pixelsdeep, 0);
				transfm2_screenyoffset = 0;
			} else {
				uint32_t dc;

				farbufferptr = (uint8_t*)panelpartsptr;
				{
					size_t n = 0;
					size_t s;
					const char* pnl;

					while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
						panelname[n] = cockpitdir[n];
						++n;
					}
					s = 0;
					while (n + 1 < sizeof(panelname) && "camerap"[s]) {
						panelname[n++] = "camerap"[s++];
					}
					pnl = ".PNL";
					s = 0;
					while (n + 1 < sizeof(panelname) && pnl[s]) {
						panelname[n++] = pnl[s++];
					}
					panelname[n] = '\0';
				}
				fediskio_loadbufferdata(panelname, 0xE3, 38, 0);
				temppanelptr = newbuf;
				panel_loadcontrolpanel((char*)"FILM", section_ptrs, 3);

				buildpalette((uint8_t*)section_ptrs[2], 0, 64);
				if (tie98_logic) {
					festring_setbackcolor(deepspacecolor);
					festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
					clearwindow();
				}
				drawshape(section_ptrs[0], 0, 0, 253, 0);

				dc = rtsvga2_calcpositionVGA(0, 8);
				logbuf2_setbufferdimensions(320, 123, dc);

				panel_copymaskdata((char*)section_ptrs[1], pixelswide, pixelsdeep, 0);
				transfm2_screenyoffset = 0;
			}
		}
		replay_rewindreplay();
		if (tie98_display)
			FlightSurface_Unlock();
		if (!reentersimflag) {
#ifdef TIE_MODERN
			continuation->finished = true;
#endif
			return;
		}
#ifdef TIE_MODERN
		continuation->phase = REPLAYIO_PHASE_VIEW;
#endif
	}
}
