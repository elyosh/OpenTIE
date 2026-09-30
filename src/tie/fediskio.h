#ifndef TIE_FEDISKIO_H
#define TIE_FEDISKIO_H

/* Compiler annotations are advisory when GNU attributes are unavailable. */
#if defined(__WATCOMC__) || (defined(_MSC_VER) && !defined(__clang__))
#define __attribute__(x)
#endif

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/pilot_storage.h"
#include "tie_runtime/storage/storage.h"

#include "tie/shipext.h"
#include "tie/string_table_ids.h"
#include "tie/tie.h"

#include <landru/memhdl.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Pilot record I/O --- */

void fediskio_initpilotrecord(int16_t clear_name);
void fediskio_createpilotrecord(void);
int16_t fediskio_readpilotrecord(const char* name);
int16_t fediskio_writepilotrecord(const char* name);
int16_t fediskio_updatepilotrecord(int16_t exit_status, int16_t ejected);

/* --- File I/O wrappers --- */

int16_t fediskio_tryopenfile(TieFileRoot root, const char* name, const char* mode, int16_t fatal);
int16_t fediskio_tryclosefile(int16_t delete_on_error);
#ifndef TIE_MODERN
/* Original installation-relative deletion entry. */
int32_t fediskio_delfile(const char* name);
#endif
int16_t fediskio_readfileblock(void* buf, unsigned int size, unsigned int count, TieFile* fp);
int16_t fediskio_writefileblock(void* buf, unsigned int size, int count, TieFile* fp);
int8_t fediskio_displayerror(void);

/* --- Buffer/resource loading --- */

void fediskio_loadbufferdata(const char* filename, uint16_t buf_index, int16_t num_entries,
							 uint16_t skip_count);
int fediskio_readfiletofarmemory(TieFileRoot root, const char* filename, void* dest);

/* --- Flight engine buffer management --- */

void fediskio_Init_Buffers_and_Fonts(void);
void fediskio_UnlockGlobals(void);
void fediskio_RelockGlobals(void);
void fediskio_FreeFlightHandles(void);
/* Nonzero reads STRINGS.DAT; zero rebinds the existing relocated table. */
void fediskio_loadstringdata(int read_file);
void fediskio_loadspecies(void);
void fediskio_fillinspec(void* data, uint8_t lfd_idx, uint8_t species_idx);
void fediskio_fillinspec_tie98(uint8_t spec_index, uint8_t model_type);

/* --- Fatal error --- */

void fediskio_fatalerror(FatalErrId error_code) __attribute__((noreturn));

/* --- FEDISKIO globals --- */

extern char pilotname[TIE_PILOT_FILENAME_CAPACITY];
extern char openfilename[256];
extern TieFile* fileptr;
extern uint8_t currentmission;
extern uint8_t currentbattle;
extern char resourcedir[10];
extern char fatalmemorystr[27];
extern char fatalfilemissingstr[55];
extern char* fatalerrstr[2];
extern char** fatalerrstrings;
/* acceleratedtimesetting is tie.c-owned per watdbg; declared in tie.h. */

extern uint32_t species_model_handle_sizes[NUM_SPECIES];

extern uint32_t rankscores[5];
extern uint32_t secretscores[12];
extern uint8_t secretcompletioncnts[12];
extern uint8_t battlemask[8];
extern char specieslfds[3][9];
extern uint8_t weaponsystype[33];

/* Flight memory handles allocated by fediskio_Init_Buffers_and_Fonts and
 * released by fediskio_FreeFlightHandles. The map-room icon handle holds a
 * 265-entry pointer table (1060 bytes) followed by icon shape data. The
 * TRACE2 edge pools are locked by xtrans2_initxtrans; BPFLIGHT allocates
 * them itself for frontend previews when they are not allocated. */
extern LandruHandle log2handle;
extern LandruHandle flightbuf_big_handle;
extern LandruHandle flightbuf_small_handle;
extern LandruHandle panelpartshandle;
extern LandruHandle rundiffhandle;
extern LandruHandle replaybufferhandle;
extern LandruHandle maproomiconshandle;

#ifdef __cplusplus
}
#endif

#endif
