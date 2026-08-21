#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#define FMUSIC_MAX_TRACKS 150
#define FMUSIC_NUM_SLOTS 2
#define FMUSIC_CHUNK_SIZE 64
#define FMUSIC_ID_BASE 500

/* Paging slot offsets into music_buffer (initialized data in the binary) */
// GLOBAL: TIE 0xC1F48
static const uint16_t music_slot_offsets[FMUSIC_NUM_SLOTS] = { 0x0000, 0x2000 };

/* Per-track data */
static char music_name[FMUSIC_MAX_TRACKS * 9]; /* 8-char name + NUL per track */
static uint16_t music_size[FMUSIC_MAX_TRACKS]; /* data size per track */
static void* music_data[FMUSIC_MAX_TRACKS];    /* malloc'd track data (replaces HANDLE) */

/* Paging state */
// GLOBAL: TIE 0xD41A8
static int32_t music_page_state[FMUSIC_NUM_SLOTS]; /* track index in each slot, -1 = empty */
static uint8_t music_age[FMUSIC_NUM_SLOTS];        /* LRU age bit: 1 = recently used */

/* Module state */
// GLOBAL: TIE 0xD41B0
void* music_buffer; /* paging buffer (allocated externally) */
// GLOBAL: TIE 0xD4952
int16_t num_music; /* number of loaded tracks, -1 = not initialized */

/*
 * Look up a music track by name.
 * Returns FMUSIC_ID_BASE + track_index on match, 0 if not found.
 */
// FUNCTION: TIE 0x239B0
int16_t fmusic_fmLoadSound(const char* name) {
	if (!num_music)
		return 0;

	for (uint16_t idx = 0; idx < (uint16_t)num_music; idx++) {
		const char* entry = &music_name[9 * idx];
		uint16_t i;
		for (i = 0; name[i] && name[i] == entry[i]; i++)
			;
		if (!name[i] && !entry[i])
			return idx + FMUSIC_ID_BASE;
	}
	return 0;
}

/*
 * Unload stub — always returns 1.
 * Track data stays resident until freemusic.
 */
// FUNCTION: TIE 0x23A28
int16_t fmusic_fmUnloadSound(void) { return 1; }

/*
 * Return a pointer to the paged data for a track, or NULL if not paged in.
 */
// FUNCTION: TIE 0x23A30
void* fmusic_GetPagedSound(uint16_t track_idx) {
	if (track_idx >= (uint16_t)num_music)
		return NULL;

	for (uint16_t i = 0; i < FMUSIC_NUM_SLOTS; i++) {
		if ((uint32_t)track_idx == (uint32_t)music_page_state[i])
			return (uint8_t*)music_buffer + music_slot_offsets[i];
	}
	return NULL;
}

/*
 * Ensure a track is paged into the music buffer. Returns slot index.
 * Three-pass search: (1) already paged, (2) empty slot, (3) LRU eviction.
 */
static int16_t pagemusic(int track_idx, int slot);

// FUNCTION: TIE 0x23A7C
int16_t fmusic_PageSound(uint16_t track_idx) {
	if (track_idx >= (uint16_t)num_music)
		return -1;

	/* Pass 1: already paged? */
	for (uint16_t i = 0; i < FMUSIC_NUM_SLOTS; i++) {
		if ((uint32_t)track_idx == (uint32_t)music_page_state[i])
			return i;
	}

	/* Pass 2: empty slot? */
	for (uint16_t i = 0; i < FMUSIC_NUM_SLOTS; i++) {
		if (music_page_state[i] == -1)
			return pagemusic(track_idx, i);
	}

	/* Pass 3: evict LRU (age == 0) */
	for (uint16_t i = 0; i < FMUSIC_NUM_SLOTS; i++) {
		if (!music_age[i])
			return pagemusic(track_idx, i);
	}

	return -1;
}

/*
 * Copy track data into a paging slot. Reset all ages, mark this slot as used.
 */
static int16_t pagemusic(int track_idx, int slot) {
	if (!music_buffer)
		return -1;

	memcpy((uint8_t*)music_buffer + music_slot_offsets[slot], music_data[track_idx], music_size[track_idx]);

	music_page_state[slot] = track_idx;

	for (int i = 0; i < FMUSIC_NUM_SLOTS; i++)
		music_age[i] = 0;
	music_age[slot] = 1;

	return slot;
}

/*
 * Initialize paging state. Does NOT allocate music_buffer — that must be
 * set externally before calling loadmusic.
 */
// FUNCTION: TIE 0x23B90
void fmusic_allocmusicbuffer(void) {
	num_music = 0;
	for (int i = 0; i < FMUSIC_NUM_SLOTS; i++) {
		music_page_state[i] = -1;
		music_age[i] = 0;
	}
}

/*
 * Register a new track: store its size, allocate memory for it.
 * Returns non-zero on success, 0 on allocation failure.
 */
static int16_t allocmusic(uint16_t size) {
	if (!music_buffer)
		return 0;

	int idx = (uint16_t)num_music;
	music_size[idx] = size;
	music_data[idx] = malloc(size);
	num_music++;

	return music_data[idx] ? 1 : 0;
}

/*
 * Free all track data and set num_music to -1 (not initialized).
 */
// FUNCTION: TIE 0x23C30
void fmusic_freemusic(void) {
	while (num_music > 0) {
		num_music--;
		free(music_data[(uint16_t)num_music]);
		music_data[(uint16_t)num_music] = NULL;
	}
	num_music = -1;
}

/*
 * Byte-swap a 32-bit value (big-endian <-> little-endian).
 */
static uint32_t swapdword(uint32_t val) {
	return ((val & 0xFF000000) >> 24) | ((val & 0x00FF0000) >> 8) | ((val & 0x0000FF00) << 8) |
		   ((val & 0x000000FF) << 24);
}

/*
 * Read 'total' bytes from file into 'dest' via a 64-byte stack buffer.
 * Returns 1 on success, 0 if any fread returned fewer bytes than requested.
 */
static int readfiledata(TieFile* fp, void* dest, uint16_t total) {
	uint8_t chunk[FMUSIC_CHUNK_SIZE];
	int had_error = 0;
	uint16_t remaining = total;
	uint16_t dest_offset = 0;

	while (remaining > 0) {
		uint16_t chunk_size = (remaining > FMUSIC_CHUNK_SIZE) ? FMUSIC_CHUNK_SIZE : remaining;
		size_t n = TieStorage_Read(chunk, 1, chunk_size, fp);
		if ((uint16_t)n != chunk_size)
			had_error = 1;

		memcpy((uint8_t*)dest + dest_offset, chunk, chunk_size);
		dest_offset += chunk_size;
		remaining -= chunk_size;
	}

	return !had_error;
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
// FUNCTION: TIE 0x23CB0
int16_t fmusic_loadmusic(const char* filename) {
	if (!music_buffer) {
		TieDiagnostics_Log(TIE_LOG_WARN, "fmusic_loadmusic: music_buffer not allocated\n");
		return 0;
	}

	TieFile* fp = TieStorage_Open(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb");
	if (!fp) {
		TieDiagnostics_Log(TIE_LOG_WARN, "fmusic_loadmusic: fopen(\"%s\") failed\n", filename);
		return 0;
	}

	/* Read 16-byte master header */
	uint8_t header[16];
	TieStorage_Read(header, 1, 16, fp);

	/* Bytes 12-15: directory data size (as 32-bit LE in memory after fread).
	 * fseek uses the full 32-bit value; track count uses only the low 16 bits. */
	int32_t data_size;
	memcpy(&data_size, &header[12], 4);
	TieStorage_Seek(fp, data_size, TIE_SEEK_CUR);

	uint16_t track_count = (uint16_t)data_size / 16;
	if (!track_count) {
		TieStorage_Close(fp);
		return track_count;
	}

	for (uint16_t loaded = 0; loaded < track_count; loaded++) {
		/* Read 16-byte track record */
		uint8_t rec[16];
		TieStorage_Read(rec, 1, 16, fp);

		/* Byte-swap first DWORD (big-endian tag) */
		uint32_t tag;
		memcpy(&tag, &rec[0], 4);
		tag = swapdword(tag);
		(void)tag; /* tag is not used after swap — stored in binary but unused */

		/* Copy 8-byte name, lowercasing A-Z */
		int idx = (uint16_t)num_music;
		for (int c = 0; c < 8; c++) {
			char ch = rec[4 + c];
			if (ch >= 'A' && ch <= 'Z')
				ch += 32;
			music_name[9 * idx + c] = ch;
		}
		music_name[9 * idx + 8] = '\0';

		/* Track data size from bytes 12-13 of the record */
		uint16_t track_size;
		memcpy(&track_size, &rec[12], 2);

		if (!allocmusic(track_size)) {
			TieStorage_Close(fp);
			music_buffer = NULL;
			return 0;
		}

		/* Read track data directly into allocated buffer */
		readfiledata(fp, music_data[(uint16_t)num_music - 1], track_size);
	}

	TieStorage_Close(fp);
	return track_count;
}
