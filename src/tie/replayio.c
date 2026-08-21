#include "tie/replayio.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/replay_format.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/flight_checkpoint.h"
#include <landru/error.h>
#include <landru/task.h>

#include <stdint.h>
#include <string.h>

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

/* --------------------------------------------------------------------------
 * Module-owned globals (watdbg: replayio.c's OBJ).
 * -------------------------------------------------------------------------- */

/* On-disk sidecar buffer for slot [6]. The save loop dumps raw bytes,
 * but mission_file_header's runtime layout is naturally aligned and
 * wider than the 456-byte disk record; copytosave encodes into this
 * buffer before the loop, copyfromsave decodes out of it after. */
static uint8_t mission_file_header_disk_image[MISSIONFILE_DISK_SIZE];

/* Static (pointer, size) table of dynamic-state regions serialized by
 * replayio_copytosave / copyfromsave and (retail only) replay_save-
 * replay / loadreplay. 67 entries + a NULL terminator. The ordering
 * matches the retail binary's Z_TIE__.EXE data section at 0xC7344 /
 * 0xC7454 — replay clip files are only compatible across builds that
 * preserve this order. */
void* savearrayptrs[68] = {
	objects,                        /* [ 0] FlightObject[NUM_OBJECTS]    */
	staticobjects,                  /* [ 1] StaticObject[...]            */
	crafts,                         /* [ 2] CraftData[NUM_CRAFTS]        */
	warheads,                       /* [ 3] WarheadRecord[...]           */
	&_date,                         /* [ 4] mission clock (8 bytes)      */
	timeleft,                       /* [ 5] mission time-left (8 bytes)  */
	mission_file_header_disk_image, /* [ 6] mission header (encoded sidecar) */
	&mission,                       /* [ 7] RUNTIME_MissionState         */
	&pstate,                        /* [ 8] 292-byte player-state block  */
	&music_state,                   /* [ 9]                              */
	&music_intensity,               /* [10]                              */
	&musicflag,                     /* [11]                              */
	&currenttarget,                 /* [12]                              */
	&currenttargetcomp,             /* [13]                              */
	&bluetarget,                    /* [14]                              */
	&targetblinkstate,              /* [15]                              */
	&targetblinkflag,               /* [16]                              */
	&blinkticks,                    /* [17]                              */
	timers,                         /* [18] timers[20] (40 bytes)        */
	&currentdebrisslot,             /* [19]                              */
	backdropposition,               /* [20] backdrop skybox slot map     */
	backdropspecies,                /* [21] skybox-species table         */
	&backdropfrontcnt,              /* [22]                              */
	&backdropbackcnt,               /* [23]                              */
	&backdroptopcnt,                /* [24]                              */
	&backdropbottomcnt,             /* [25]                              */
	&backdropleftcnt,               /* [26]                              */
	&backdroprightcnt,              /* [27]                              */
	&stardetaillevel,               /* [28]                              */
	&drawbackdropflag,              /* [29]                              */
	&drawdebrisflag,                /* [30]                              */
	&starshipexplodetail,           /* [31]                              */
	&starshipdetail,                /* [32]                              */
	&hyperspacedetail,              /* [33]                              */
	&shipdetailvalue,               /* [34]                              */
	&shipdetailpolycnt,             /* [35]                              */
	&drawmarkingsflag,              /* [36]                              */
	&detaillevel,                   /* [37]                              */
	&cheatingflag,                  /* [38]                              */
	&inflight_music_vol,            /* [39]                              */
	&inflight_sound_vol,            /* [40]                              */
	&inflight_speech_vol,           /* [41]                              */
	&soundvolflag,                  /* [42]                              */
	&musicvolflag,                  /* [43]                              */
	&inflight_unlimited,            /* [44]                              */
	&inflight_invulnerable,         /* [45]                              */
	&inflight_collision,            /* [46]                              */
	&acceleratedtimectr,            /* [47]                              */
	&acceleratedtimesetting,        /* [48]                              */
	&musicenabled,                  /* [49]                              */
	&sfxenabled,                    /* [50]                              */
	&voiceenabled,                  /* [51]                              */
	&palette_cycle_user,            /* [52] = demo's colorcycleuserflag  */
	&gouraudflag,                   /* [53]                              */
	&blastcount,                    /* [54]                              */
	blastqueue,                     /* [55] blastqueue (32 bytes)        */
	&idnumber,                      /* [56]                              */
	&lasthistorymsg,                /* [57]                              */
	&numhistorymsgs,                /* [58]                              */
	gatepreviousx,                  /* [59] gatepreviousx[4]             */
	gatepreviousy,                  /* [60] gatepreviousy[4]             */
	gatepreviousz,                  /* [61] gatepreviousz[4]             */
	gatepreviousroll,               /* [62] gatepreviousroll[4]          */
	gatepreviouspitch,              /* [63] gatepreviouspitch[4]         */
	gatepreviousheading,            /* [64] gatepreviousheading[4]       */
	&currentgate,                   /* [65]                              */
	gatetimer,                      /* [66] gatetimer[3]                 */
	NULL                            /* [67] terminator                   */
};

uint32_t savearraysizes[68] = {
	sizeof(objects),                        /* [ 0] */
	sizeof(staticobjects),                  /* [ 1] */
	sizeof(crafts),                         /* [ 2] */
	sizeof(warheads),                       /* [ 3] */
	sizeof(_date),                          /* [ 4] = 8 */
	sizeof(timeleft),                       /* [ 5] = 8 */
	sizeof(mission_file_header_disk_image), /* [ 6] = 456 */
	sizeof(mission),                        /* [ 7] */
	sizeof(pstate),                         /* [ 8] = 292 (300 on 64-bit) */
	sizeof(music_state),                    /* [ 9] = 2 */
	sizeof(music_intensity),                /* [10] = 2 */
	sizeof(musicflag),                      /* [11] = 1 */
	sizeof(currenttarget),                  /* [12] = 2 */
	sizeof(currenttargetcomp),              /* [13] = 2 */
	sizeof(bluetarget),                     /* [14] = 2 */
	sizeof(targetblinkstate),               /* [15] = 2 */
	sizeof(targetblinkflag),                /* [16] = 2 */
	sizeof(blinkticks),                     /* [17] = 2 */
	sizeof(timers),                         /* [18] = 40 */
	sizeof(currentdebrisslot),              /* [19] = 2 */
	sizeof(backdropposition),               /* [20] = 64 */
	sizeof(backdropspecies),                /* [21] = 64 */
	sizeof(backdropfrontcnt),               /* [22] = 2 */
	sizeof(backdropbackcnt),                /* [23] = 2 */
	sizeof(backdroptopcnt),                 /* [24] = 2 */
	sizeof(backdropbottomcnt),              /* [25] = 2 */
	sizeof(backdropleftcnt),                /* [26] = 2 */
	sizeof(backdroprightcnt),               /* [27] = 2 */
	sizeof(stardetaillevel),                /* [28] = 2 */
	sizeof(drawbackdropflag),               /* [29] = 1 */
	sizeof(drawdebrisflag),                 /* [30] = 1 */
	sizeof(starshipexplodetail),            /* [31] = 2 */
	sizeof(starshipdetail),                 /* [32] = 2 */
	sizeof(hyperspacedetail),               /* [33] = 2 */
	sizeof(shipdetailvalue),                /* [34] = 2 */
	sizeof(shipdetailpolycnt),              /* [35] = 2 */
	sizeof(drawmarkingsflag),               /* [36] = 1 */
	sizeof(detaillevel),                    /* [37] = 2; host replay format */
	sizeof(cheatingflag),                   /* [38] = 1 */
	sizeof(inflight_music_vol),             /* [39] = 1 */
	sizeof(inflight_sound_vol),             /* [40] = 1 */
	sizeof(inflight_speech_vol),            /* [41] = 1 */
	sizeof(soundvolflag),                   /* [42] = 1 */
	sizeof(musicvolflag),                   /* [43] = 1 */
	sizeof(inflight_unlimited),             /* [44] = 1 */
	sizeof(inflight_invulnerable),          /* [45] = 1 */
	sizeof(inflight_collision),             /* [46] = 1 */
	sizeof(acceleratedtimectr),             /* [47] = 1 */
	sizeof(acceleratedtimesetting),         /* [48] = 1 */
	sizeof(musicenabled),                   /* [49] = 1 */
	sizeof(sfxenabled),                     /* [50] = 1 */
	sizeof(voiceenabled),                   /* [51] = 1 */
	sizeof(palette_cycle_user),             /* [52] = 1 */
	sizeof(gouraudflag),                    /* [53] = 1 */
	sizeof(blastcount),                     /* [54] = 1 */
	sizeof(blastqueue),                     /* [55] = 32 */
	sizeof(idnumber),                       /* [56] = 2 */
	sizeof(lasthistorymsg),                 /* [57] = 2 */
	sizeof(numhistorymsgs),                 /* [58] = 2 */
	sizeof(gatepreviousx),                  /* [59] = 16 */
	sizeof(gatepreviousy),                  /* [60] = 16 */
	sizeof(gatepreviousz),                  /* [61] = 16 */
	sizeof(gatepreviousroll),               /* [62] = 8 */
	sizeof(gatepreviouspitch),              /* [63] = 8 */
	sizeof(gatepreviousheading),            /* [64] = 8 */
	sizeof(currentgate),                    /* [65] = 2 */
	sizeof(gatetimer),                      /* [66] = 6 */
	0                                       /* [67] terminator */
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
	REPLAY_BUFFER_TEMP_VERSION = 1,
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
	for (size_t i = 0; i < bytes; ++i) {
		if (TieStorage_Putc(p[i], fp) == TIE_EOF)
			return 0;
	}
	return 1;
}

static int read_raw_block(void* dst, size_t bytes, TieFile* fp) {
	uint8_t* p = (uint8_t*)dst;
	for (size_t i = 0; i < bytes; ++i) {
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
	for (size_t i = 0; i < NUM_OBJECTS; ++i) {
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

// FUNCTION: TIE 0x478A0
int16_t replayio_copytosave(const char* fname) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, fname, "wb", 0))
		return 0;

	/* Refresh the slot-[6] sidecar from the live mission_file_header so
	 * the generic raw-byte loop emits the canonical 456-byte LE image. */
	MissionFile_encode(mission_file_header_disk_image, &mission_file_header);

	for (size_t i = 0; savearrayptrs[i]; ++i) {
		uint32_t sz = savearraysizes[i];
		int16_t n = fediskio_writefileblock(savearrayptrs[i], 1, (int)sz, fileptr);
		if ((uint32_t)n != sz) {
			TieStorage_Close(fileptr);
			return 0;
		}
	}

	/* The save format expects fg_array as the on-disk 48 x 292-byte
	 * little-endian image; encode through a buffer because the runtime
	 * EFGStruct layout is naturally aligned and wider on most hosts. */
	uint8_t fg_save_buf[48 * EFGSTRUCT_DISK_SIZE];
	for (size_t i = 0; i < 48; ++i)
		EFGStruct_encode(fg_save_buf + i * EFGSTRUCT_DISK_SIZE, &fg_array[i]);
	if (!write_raw_block(fg_save_buf, sizeof fg_save_buf, fileptr))
		goto fail;
	if (!write_raw_block(radiomsg, 0x5A0, fileptr))
		goto fail;
	if (!write_raw_block(cut, 0x70, fileptr))
		goto fail;
	if (!write_raw_block(fgstatus, 0x900, fileptr))
		goto fail;

	for (size_t i = 0; i < NUM_SPECIES; ++i) {
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

// FUNCTION: TIE 0x47A08
int16_t replayio_copyfromsave(const char* fname) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, fname, "rb", 1))
		return 0;

	for (size_t i = 0; savearrayptrs[i]; ++i) {
		uint32_t sz = savearraysizes[i];
		int16_t n = fediskio_readfileblock(savearrayptrs[i], 1, sz, fileptr);
		if ((uint32_t)n != sz) {
			TieStorage_Close(fileptr);
			return 0;
		}
	}

	/* Slot [6] just landed in mission_file_header_disk_image; decode
	 * back into the live struct. */
	MissionFile_decode(&mission_file_header, mission_file_header_disk_image);

	/* Read 48 x 292-byte fg records as a contiguous on-disk image,
	 * then decode each into the runtime fg_array (whose element width
	 * may differ from the disk size due to natural alignment). */
	uint8_t fg_load_buf[48 * EFGSTRUCT_DISK_SIZE];
	if (!read_raw_block(fg_load_buf, sizeof fg_load_buf, fileptr)) {
		TieStorage_Close(fileptr);
		return 0;
	}
	for (size_t i = 0; i < 48; ++i)
		EFGStruct_decode(&fg_array[i], fg_load_buf + i * EFGSTRUCT_DISK_SIZE);
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

	for (size_t i = 0; i < NUM_SPECIES; ++i) {
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
	replayio_port_rebind_checkpoint_pointers();
	msg_clearmessagequeue();
	TieStorage_Close(fileptr);
	return 1;
}

/* --------------------------------------------------------------------------
 * Input-buffer spooling
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE 0x47B2C
int replayio_openreplayinputfile(void) {
	/* Drop any stale .spl, then create a fresh one prefixed with the
	 * current versioned header. Subsequent replayio_spoolreplayinput calls
	 * open with "ab" and append fixed-size records after the header. */
	TieStorage_Remove(TIE_FILE_ROOT_TEMP, inputspoolfile);
	TieFile* fp = TieStorage_Open(TIE_FILE_ROOT_TEMP, inputspoolfile, "wb");
	if (!fp)
		return 0;
	int ok = TieReplayFormat_WriteHeader(fp);
	if (TieStorage_Close(fp) != 0)
		return 0;
	return ok;
}

/* replayio_spoolreplayinput — append the valid records in this chunk. */
// FUNCTION: TIE 0x47B3C
int16_t replayio_spoolreplayinput(void) {
	if (!replayspoolflag)
		return 1;
	if (!replaybuffercnt)
		return 1;

	if (!endgamereplayflag) {
		msg_messageprintf(MSG_CAMERA_SAVING);
	}

	TieFile* saved = fileptr;
	const uint8_t* bufp = (const uint8_t*)replaybufferstart;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, inputspoolfile, "ab", 0)) {
		fileptr = saved;
		return 0;
	}

	for (uint16_t frame = 0; frame < replaybuffercnt; ++frame) {
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

// FUNCTION: TIE 0x47C44
int16_t replayio_savereplaybuffer(void) {
	if (replaybuffercnt > REPLAY_INPUT_CHUNK_FRAMES)
		return 0;
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, kBufferTempFile, "wb", 0))
		return 0;
	uint8_t header[REPLAY_BUFFER_TEMP_HEADER_SIZE] = { 'R', 'B', 'U', 'F' };
	header[4] = REPLAY_BUFFER_TEMP_VERSION;
	header[6] = (uint8_t)replaybuffercnt;
	header[7] = (uint8_t)(replaybuffercnt >> 8);
	const size_t valid_bytes = (size_t)replaybuffercnt * REPLAYINPUTFRAME_DISK_SIZE;
	if (TieStorage_Write(header, 1, sizeof header, fileptr) != sizeof header ||
		TieStorage_Write(replaybufferstart, 1, valid_bytes, fileptr) != valid_bytes) {
		TieStorage_Close(fileptr);
		return 0;
	}
	return (TieStorage_Close(fileptr) == 0);
}

// FUNCTION: TIE 0x47CA4
int replayio_restorereplaybuffer(void) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_TEMP, kBufferTempFile, "rb", 1))
		return 0;
	uint8_t header[REPLAY_BUFFER_TEMP_HEADER_SIZE];
	if (TieStorage_Read(header, 1, sizeof header, fileptr) != sizeof header ||
		memcmp(header, "RBUF", 4) != 0 || header[4] != REPLAY_BUFFER_TEMP_VERSION || header[5] != 0) {
		TieStorage_Close(fileptr);
		return 0;
	}
	const uint16_t frame_count = (uint16_t)(header[6] | (uint16_t)header[7] << 8);
	if (frame_count > REPLAY_INPUT_CHUNK_FRAMES) {
		TieStorage_Close(fileptr);
		return 0;
	}
	const size_t valid_bytes = (size_t)frame_count * REPLAYINPUTFRAME_DISK_SIZE;
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
static void build_panel_path(const char* infix) {
	size_t n = 0;
	while (n + 1 < sizeof(panelname) && cockpitdir[n]) {
		panelname[n] = cockpitdir[n];
		++n;
	}
	size_t s = 0;
	while (n + 1 < sizeof(panelname) && infix[s]) {
		panelname[n++] = infix[s++];
	}
	const char* pnl = ".PNL";
	s = 0;
	while (n + 1 < sizeof(panelname) && pnl[s]) {
		panelname[n++] = pnl[s++];
	}
	panelname[n] = '\0';
}

/* Load the camera-viewer panel.
 *   panel_name is "CAMERA" (cockpit) or "FILM" (stand-alone viewer).
 *   panel_x / panel_y / panel_depth / panel_width drive the viewport.
 * The displaycorner value is derived directly via rtsvga2_calcpositionVGA
 * (= panel_y * screenMemWidth + panel_x). */
static void load_viewer_panel(const char* infix, const char* panel_name, uint16_t panel_x, uint16_t panel_y,
							  uint16_t panel_depth, uint16_t panel_width) {
	farbufferptr = (uint8_t*)panelpartsptr;
	build_panel_path(infix);
	fediskio_loadbufferdata(panelname, 0xE3, 38, 0);
	temppanelptr = newbuf;
	panel_loadcontrolpanel((char*)panel_name, section_ptrs, 3);

	buildpalette((uint8_t*)section_ptrs[2], 0, 64);
	if (TieProfile_UsesTie98Logic()) {
		festring_setbackcolor(deepspacecolor);
		festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
		clearwindow();
	}
	drawshape(section_ptrs[0], 0, 0, 253, 0);

	uint32_t dc = rtsvga2_calcpositionVGA(panel_x, panel_y);
	logbuf2_setbufferdimensions(panel_width, panel_depth, dc);

	panel_copymaskdata((char*)section_ptrs[1], pixelswide, pixelsdeep, 0);
	transfm2_screenyoffset = 0;
}

static void load_standalone_viewer_panel(void) {
	if (tie_is_high_resolution_flight())
		load_viewer_panel("camerap", "FILM", 28, 16, 298, 584);
	else
		load_viewer_panel("camerap", "FILM", 0, 8, 123, 320);
}

/* Resolution-change detection on exit: retail saves flightResolution at
 * viewer entry; on return to sim, if the user changed it (via the
 * viewer's OPTION row hook), re-init graphics + reload scaled fonts. */
static bool restore_graphics_if_changed(int16_t saved_res) {
	if (flightResolution == saved_res)
		return true;
	flightResolution = saved_res;
	if (!TieClassicDisplay_ActivateFlight())
		return false;
	tie_initflightresolution();
	rtsvga2_initgraphVGA();
	feinput_setupgraphics((uint8_t)detaillevel);
	fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "xtiny64.fnt", fontptrtiny);
	fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "dmicro64.fnt", fontptrmicro);
	festring_setfontsize(1);
	return true;
}

typedef enum {
	REPLAYIO_PHASE_ENTER = 0,
	REPLAYIO_PHASE_AFTER_VIEWER,
	REPLAYIO_PHASE_AFTER_REENTERSIM,
	REPLAYIO_PHASE_DONE,
} ReplayioPhase;

typedef struct ReplayioTask {
	int16_t saved_res;
	ReplayioPhase phase;
} ReplayioTask;

static void replayio_load_initial_panel(void) {
	if (maingameflag) {
		memset(replayclipname, 0, sizeof(replayclipname));
		strncpy(replayclipname, "UNTITLED", sizeof(replayclipname) - 1);
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			load_viewer_panel("camerap", "CAMERA", 0, 17, 135, 320);
		} else {
			load_viewer_panel("camerap", "CAMERA", 0, 40, 325, 640);
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
		load_standalone_viewer_panel();
	}
}

static void replayio_pause_imuse_save_volume(void) {
	replayvolume = (int16_t)imuse_get_master_vol(im);
	imuse_set_master_vol(im, 0);
	imuse_pause(im);
	replaymusic = 0;
}

static void replayio_resume_imuse_if_paused(void) {
	if (!replaymusic) {
		imuse_set_master_vol(im, (uint16_t)replayvolume);
		imuse_resume(im);
		replaymusic = 1;
	}
}

// FUNCTION: TIE 0x47CF4, TIE98 0x475350 (task-split recovery)
static LandruTaskStepResult replayio_task_step(void* self) {
	ReplayioTask* t = (ReplayioTask*)self;

	switch (t->phase) {

		case REPLAYIO_PHASE_ENTER:
			/* Bail-out check: in-flight ('maingameflag') invocations
			 * checkpoint the live mission to disk first. If the disk
			 * save fails, the entire viewer is skipped — iMUSE is
			 * already silenced, so leave it that way and pop. */
			if (maingameflag && !replayio_copytosave(replaysavegamefile))
				return LANDRU_TASK_STEP_DONE;

			recordingreplay = 0;
			replayviewmode = 1;
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			replayio_load_initial_panel();
			replay_rewindreplay();
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Unlock();

			/* Fall through to the loop-top: clear reentersim, repaint
			 * once, push the modal viewer task. */
			reentersimflag = 0;
			festring_showscreen();
			if (TieClassicDisplay_UsesDx5()) {
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
				FrontendDisplay_BlitOffscreenToRenderSurface();
			}
			replay_Push_DoReplayScreen_Task();
			t->phase = REPLAYIO_PHASE_AFTER_VIEWER;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAYIO_PHASE_AFTER_VIEWER:
			/* DoReplayScreen task popped. Decide which branch fires. */
			if (replaymusic) {
				replayvolume = (int16_t)imuse_get_master_vol(im);
				imuse_set_master_vol(im, 0);
				imuse_pause(im);
				replaymusic = 0;
			}
			replayviewmode = 0;

			if (maingameflag) {
				/* Cockpit: return to in-flight. Retail restores the flight
				 * resolution (if the user changed it inside the viewer)
				 * before copyfromsave. */
				if (!restore_graphics_if_changed(t->saved_res)) {
					lerror_Set_Landru_Error(12);
					return LANDRU_TASK_STEP_DONE;
				}
				replayio_copyfromsave(replaysavegamefile);
				replayio_setreturnview();
				replayio_resume_imuse_if_paused();
				return LANDRU_TASK_STEP_DONE;
			}

			if (!reentersimflag) {
				/* Stand-alone viewer: normal exit. */
				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Lock();
				if (tie_is_high_resolution_flight())
					festring_setbound(28, 16, 611, 315);
				else
					festring_setbound(14, 8, 306, 131);
				festring_setbackcolor(0x40);
				clearwindow();
				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Unlock();
				replayio_resume_imuse_if_paused();
				return LANDRU_TASK_STEP_DONE;
			}

			/* Stand-alone viewer + user pressed 's': re-enter the sim
			 * for a second sortie. Retail adds msg_clearmessagequeue +
			 * blastcount reset. */
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
			replayio_resume_imuse_if_paused();

			tie_Push_FlightMission_Task();
			t->phase = REPLAYIO_PHASE_AFTER_REENTERSIM;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAYIO_PHASE_AFTER_REENTERSIM:
			/* Flight task popped (mission.end_flag was set). Pause iMUSE,
			 * reload the replay-start checkpoint, repaint the FILM panel,
			 * and push DoReplayScreen again. The fidelity check
			 * `if (!reentersimflag) return;` from the binary is preserved:
			 * inside the inner sim nothing clears reentersimflag, so the
			 * always-true branch loops back to AFTER_VIEWER which is what
			 * actually decides exit on the next viewer dismissal. */
			replayio_pause_imuse_save_volume();
			blank();
			recordingreplay = 0;
			replayviewmode = 1;
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			replay_loadreplay();
			replayio_copyfromsave(replaystartfile);
			msg_messageinit();
			load_standalone_viewer_panel();
			replay_rewindreplay();
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Unlock();

			if (!reentersimflag)
				return LANDRU_TASK_STEP_DONE;

			reentersimflag = 0;
			festring_showscreen();
			if (TieClassicDisplay_UsesDx5()) {
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
				FrontendDisplay_BlitOffscreenToRenderSurface();
			}
			replay_Push_DoReplayScreen_Task();
			t->phase = REPLAYIO_PHASE_AFTER_VIEWER;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAYIO_PHASE_DONE:
			/* Unreachable — DONE is returned directly from the producing
			 * phase, never set as a stored value. Kept exhaustive for the
			 * compiler's switch-coverage check. */
			return LANDRU_TASK_STEP_DONE;
	}

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable replayio_task_vt = {
	.step = replayio_task_step,
};

void replayio_Push_ReplayScreen_Task(void) {
	ReplayioTask* t = (ReplayioTask*)landru_task_push(&replayio_task_vt);
	if (!t)
		return;
	t->saved_res = (int16_t)flightResolution;
	t->phase = REPLAYIO_PHASE_ENTER;
	if (TieProfile_UsesTie98Logic()) {
		uint8_t saved_mapflag = mapflag;
		mapflag = 1;
		FSFX_UpdatePlayerEngineSound();
		mapflag = saved_mapflag;
	}

	/* Pause iMUSE up-front: retail does this before the disk-save
	 * decision so that even the early-fail path leaves the mixer in
	 * the silenced state the cockpit caller expects. */
	replayio_pause_imuse_save_volume();
}

/* --------------------------------------------------------------------------
 * replayio_setreturnview -- restore the cockpit when re-entering the live
 * simulator. Unchanged between demo and retail.
 * -------------------------------------------------------------------------- */
// FUNCTION: TIE 0x483E4
void replayio_setreturnview(void) {
	farbufferptr = (uint8_t*)panelpartsptr;
	build_panel_path(parts);
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
