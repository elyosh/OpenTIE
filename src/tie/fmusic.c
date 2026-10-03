#include "tie/fmusic.h"
#include "tie/edition.h"
#include "tie/fediskio.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include "landru/memhdl.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	FMUSIC_MAX_TRACKS = 150,
	FMUSIC_NUM_SLOTS = 2,
	FMUSIC_CHUNK_SIZE = 64,
	FMUSIC_ID_BASE = 500,
};

/* 16-byte GMD record: master header (size = directory data size) or
 * track record (big-endian tag, name, size = track data size). */
typedef struct FmusicRecord {
	int32_t tag;
	uint8_t name[8];
	int32_t size;
} FmusicRecord;

/* Paging slot offsets into music_buffer (initialized data in the binary) */
// GLOBAL: TIE95 0xC1F48
// GLOBAL: TIE98 0x4E03FC
static const uint16_t music_slot_offsets[FMUSIC_NUM_SLOTS] = { 0x0000, 0x2000 };

/* Paging state */
// GLOBAL: TIE95 0xD41A8
// GLOBAL: TIE98 0x55F900
static int32_t music_page_state[FMUSIC_NUM_SLOTS]; /* track index in each slot, -1 = empty */

/* Module state */
// GLOBAL: TIE95 0xD41B0
// GLOBAL: TIE98 0x626770
void* music_buffer; /* paging buffer (allocated externally) */

/* Per-track data */
// GLOBAL: TIE95 0xD41B4
// GLOBAL: TIE98 0x55F908
static LandruHandle music_handle[FMUSIC_MAX_TRACKS]; /* track data handle */
// GLOBAL: TIE95 0xD42E0
// GLOBAL: TIE98 0x55FA38
static uint16_t music_size[FMUSIC_MAX_TRACKS]; /* data size per track */
// GLOBAL: TIE95 0xD440C
// GLOBAL: TIE98 0x55FB68
static char music_name[FMUSIC_MAX_TRACKS * 9]; /* 8-char name + NUL per track */
// GLOBAL: TIE95 0xD4952
// GLOBAL: TIE98 0x55F8F8
int16_t num_music; /* number of loaded tracks, -1 = not initialized */
// GLOBAL: TIE95 0xD4954
// GLOBAL: TIE98 0x55FB64
static uint8_t music_age[FMUSIC_NUM_SLOTS]; /* LRU age bit: 1 = recently used */

/*
 * Ensure a track is paged into the music buffer.
 * Three-pass search: (1) already paged, (2) empty slot, (3) LRU eviction.
 */
static void fmusic_pagemusic(int track_idx, uint16_t slot);
/*
 * Look up a music track by name.
 * Returns FMUSIC_ID_BASE + track_index on match, 0 if not found.
 */
// FUNCTION: TIE95 0x239B0
// FUNCTION: TIE98 0x41EDF0
int fmusic_fmLoadSound(const char* name) {
	uint16_t idx;
	uint16_t i;

	for (idx = 0; idx < (uint16_t)num_music; idx++) {
		i = 0;
		while (name[i]) {
			if (name[i] != music_name[idx * 9 + i])
				break;
			i++;
		}
		if (!name[i] && !music_name[idx * 9 + i])
			return idx + FMUSIC_ID_BASE;
	}
	return 0;
}

/*
 * Unload stub — always returns 1.
 * Track data stays resident until freemusic.
 */
// FUNCTION: TIE95 0x23A28
// FUNCTION: TIE98 0x41EE80
int16_t fmusic_fmUnloadSound(void) { return 1; }

/*
 * Return a pointer to the paged data for a track, or NULL if not paged in.
 */
// FUNCTION: TIE95 0x23A30
// FUNCTION: TIE98 0x41EE90
void* fmusic_GetPagedSound(unsigned int track_idx) {
	uint16_t i;

	if (track_idx >= (uint16_t)num_music)
		return NULL;

	for (i = 0; i < 2; i++) {
		if (music_page_state[i] == track_idx)
			return (uint8_t*)music_buffer + music_slot_offsets[i];
	}
	return NULL;
}

// FUNCTION: TIE95 0x23A7C
// FUNCTION: TIE98 0x41EEE0
void fmusic_PageSound(unsigned int track_idx) {
	uint16_t i;

	if (track_idx >= (uint16_t)num_music)
		return;

	/* Pass 1: already paged? */
	for (i = 0; i < 2; i++) {
		if (track_idx == music_page_state[i])
			return;
	}

	/* Pass 2: empty slot? */
	for (i = 0; i < 2; i++) {
		if (music_page_state[i] == -1) {
			fmusic_pagemusic(track_idx, i);
			return;
		}
	}

	/* Pass 3: evict LRU (age == 0) */
	for (i = 0; i < 2; i++) {
		if (!music_age[i]) {
			fmusic_pagemusic(track_idx, i);
			return;
		}
	}
}

/*
 * Copy track data into a paging slot. Reset all ages, mark this slot as used.
 */
// FUNCTION: TIE95 0x23AF0
// FUNCTION: TIE98 0x41EF60
static void fmusic_pagemusic(int track_idx, uint16_t slot) {
	uint16_t i;
	void* data;

	if (music_buffer) {
		data = xmemhdl_Lock_Handle(music_handle[track_idx]);
		memmove((uint8_t*)music_buffer + music_slot_offsets[slot], data, music_size[track_idx]);
		xmemhdl_Unlock_Handle(music_handle[track_idx]);

		music_page_state[slot] = track_idx;
		for (i = 0; i < 2; i++)
			music_age[i] = 0;
		music_age[slot] = 1;
	}
}

/*
 * Initialize paging state. Does NOT allocate music_buffer — that must be
 * set externally before calling loadmusic.
 */
// FUNCTION: TIE95 0x23B90
// FUNCTION: TIE98 0x41EFE0
void fmusic_allocmusicbuffer(void) {
	int16_t i;

	num_music = 0;
	for (i = 0; i < 2; i++) {
		music_page_state[i] = -1;
		music_age[i] = 0;
	}
}

/*
 * Register a new track: store its size and allocate a memory handle for it.
 * Returns the new handle, or 0 on allocation failure.
 */
// FUNCTION: TIE95 0x23BC8
// FUNCTION: TIE98 0x41F000
static LandruHandle fmusic_allocmusic(uint16_t size) {
	if (!music_buffer)
		return LANDRU_NULL_HANDLE;

	music_size[(uint16_t)num_music] = size;
	fediskio_UnlockGlobals();
	music_handle[(uint16_t)num_music++] = xmemhdl_Alloc_Handle(size, LANDRU_MEMORY_DEFAULT);
	fediskio_RelockGlobals();

	return music_handle[(uint16_t)num_music - 1];
}

/*
 * Free all track handles and set num_music to -1 (not initialized).
 */
// FUNCTION: TIE95 0x23C30
// FUNCTION: TIE98 0x41F070
void fmusic_freemusic(void) {
	while (num_music--) {
		if (music_handle[(uint16_t)num_music])
			xmemhdl_Free_Handle(music_handle[(uint16_t)num_music]);
	}
}

/*
 * Byte-swap a 32-bit value (big-endian <-> little-endian).
 */
// FUNCTION: TIE95 0x23C7C
// FUNCTION: TIE98 0x41F0C0
uint32_t fmusic_swapdword(int32_t val) {
	uint32_t result;

	result = (uint32_t)(val & 0x000000FF) << 24;
	result |= (val & 0x0000FF00) << 8;
	result |= (val & 0x00FF0000) >> 8;
	result |= (val & 0xFF000000) >> 24;
	return result;
}

/*
 * Load a GMD-format music file.
 *
 * File layout:
 *   [16-byte master header]  — bytes 12-13 = directory data size
 *   [directory data]         — skipped (size from header)
 *   [track records × N]      — N = directory_size / 16
 *     per record: [4B big-endian tag] [8B name] [2B data size] [2B unused]
 *   [track data × N]         — each track's data follows its record
 *
 * Returns the number of tracks loaded, or 0 on failure.
 */
// FUNCTION: TIE95 0x23CB0
// FUNCTION: TIE98 0x41F0F0
int16_t fmusic_loadmusic(const char* filename) {
	FmusicRecord record;
	TieFile* fp;
	uint16_t track_count;
	uint16_t loaded;

	if (!music_buffer)
		return 0;

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb", 0)) {
		music_buffer = NULL;
		return 0;
	}
	fp = fileptr;

	/* Master header: size is the directory data size; fseek uses the full
	 * 32-bit value, the track count only the low word. */
	fediskio_readfileblock(&record, 1, 16, fp);
	TieStorage_Seek(fp, record.size, TIE_SEEK_CUR);
	track_count = (uint16_t)record.size / 16;

	for (loaded = 0; loaded < track_count; loaded++) {
		uint16_t c;
		LandruHandle handle;
		uint8_t* data;

		fediskio_readfileblock(&record, 1, 16, fp);

		/* The tag is byte-swapped in place but never consumed. TIE95 expands
		 * the swap inline; TIE98 calls the helper. */
		if (TIE_FLIGHT_TIE98) {
			record.tag = fmusic_swapdword(record.tag);
		} else {
			uint32_t swapped;

			swapped = (uint32_t)(record.tag & 0x000000FF) << 24;
			swapped |= (record.tag & 0x0000FF00) << 8;
			swapped |= (record.tag & 0x00FF0000) >> 8;
			swapped |= (record.tag & 0xFF000000) >> 24;
			record.tag = swapped;
		}

		/* Copy 8-byte name, lowercasing A-Z */
		for (c = 0; c < 8; c++) {
			if (record.name[c] >= 'A' && record.name[c] <= 'Z')
				record.name[c] += 32;
			music_name[c + (uint16_t)num_music * 9] = (char)record.name[c];
		}
		music_name[9 * (uint16_t)num_music + 8] = '\0';

		handle = fmusic_allocmusic(record.size);
		if (!handle) {
			TieStorage_Close(fp);
			music_buffer = NULL;
			return 0;
		}

		/* Read track data directly into the locked handle */
		data = xmemhdl_Lock_Handle(handle);
		fmusic_readfiledata(fp, data, record.size);
		xmemhdl_Unlock_Handle(handle);
	}

	TieStorage_Close(fp);
	return track_count;
}

/*
 * Read 'total' bytes from file into 'dest' via a 64-byte stack buffer.
 * Returns 1 on success, 0 if any read returned fewer bytes than requested.
 */
// FUNCTION: TIE95 0x23E44
// FUNCTION: TIE98 0x41F260
int16_t fmusic_readfiledata(TieFile* fp, uint8_t* dest, uint16_t total) {
	uint8_t chunk[FMUSIC_CHUNK_SIZE];
	uint16_t had_error = 0;
	uint16_t remaining = total;
	uint16_t dest_offset = 0;

	while (remaining) {
		uint16_t chunk_size = remaining;
		uint16_t i;

		if (chunk_size > FMUSIC_CHUNK_SIZE)
			chunk_size = FMUSIC_CHUNK_SIZE;
		had_error |= fediskio_readfileblock(chunk, 1, chunk_size, fp) != (int16_t)chunk_size;
		for (i = 0; i < chunk_size; i++)
			dest[dest_offset++] = chunk[i];
		remaining -= chunk_size;
	}

	return had_error == 0;
}
