#ifndef __FEDISKIO_H__
#define __FEDISKIO_H__

/* Watcom C has no __attribute__; annotations here are advisory. */
#if defined(__WATCOMC__)
#define __attribute__(x)
#endif

#include <stdint.h>

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

/* --- Pilot record I/O --- */

void fediskio_initpilotrecord(int16_t clear_name);
void fediskio_createpilotrecord(void);
int16_t fediskio_readpilotrecord(const char* name);
int16_t fediskio_writepilotrecord(const char* name);
int16_t fediskio_updatepilotrecord(int16_t exit_status, int16_t ejected);

/* --- File I/O wrappers --- */

int16_t fediskio_tryopenfile(TieFileRoot root, const char* name, const char* mode, int16_t fatal);
int16_t fediskio_tryclosefile(int16_t delete_on_error);
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
void fediskio_loadstringdata(void);
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
/* acceleratedtimesetting is tie.c-owned per watdbg; declared in tie.h. */

extern uint32_t species_model_handle_sizes[NUM_SPECIES];

extern uint32_t rankscores[5];
extern uint32_t secretscores[12];
extern uint8_t secretcompletioncnts[12];
extern uint8_t battlemask[8];
extern char specieslfds[3][9];
extern uint8_t weaponsystype[33];

/* Map-room icon buffer (31060 bytes). The first 1060 bytes are a 265-entry
 * `void *` lookup table (mapfarbufferptrs), the remainder holds the icon
 * shape byte-stream loaded from RESOURCE\\icons{320,640}.ico by
 * fediskio_loadbufferdata. Owned/allocated by fediskio.c; consumed by
 * maproom.c at room entry/exit. NULL until fediskio_Init_Buffers_and_Fonts
 * runs, freed by fediskio_FreeFlightHandles. */
extern void* maproomicons_buf;

/* Flight-engine edge pools (retail word_D4188 / word_D4186). Allocated
 * by fediskio_Init_Buffers_and_Fonts and consumed by xtrans2_initxtrans,
 * which casts them to trace2_edgeinfos / trace2_edgeheaders and derives
 * the TRACE2 overflow clamps. NULL before init / after FreeFlightHandles. */
extern void* flightbuf_small; /* 0x3C000 B */
extern void* flightbuf_big;   /* 0xCB200 B */

#endif
