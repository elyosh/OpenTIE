#include "tie_runtime/audio/flight_sound_bank.h"
#include "tie/fsfx.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TieFlightSoundLayout TieFlightSound_Layout(void) {
	static const TieFlightSoundLayout tie95 = { FSFX_TIE95_SOUND_TABLE_COUNT, FSFX_TIE95_MISSION_VOICE_BASE,
												FSFX_MISSION_VOICE_COUNT, 0 };
	static const TieFlightSoundLayout tie98 = { FSFX_TIE98_SOUND_TABLE_COUNT, FSFX_TIE98_MISSION_VOICE_BASE,
												FSFX_MISSION_VOICE_COUNT, 1 };
	return TieProfile_Flight()->version == TIE_GAME_VERSION_TIE98 ? tie98 : tie95;
}

uint16_t TieFlightSound_MissionVoiceId(uint16_t logical_index) {
	TieFlightSoundLayout layout = TieFlightSound_Layout();
	return logical_index < layout.mission_voice_count ? (uint16_t)(layout.mission_voice_base + logical_index)
													  : UINT16_MAX;
}

const char* TieFlightSound_Name(uint16_t sound_id) {
	TieFlightSoundLayout layout = TieFlightSound_Layout();
	if (sound_id >= layout.table_count || !soundnames[sound_id][0])
		return NULL;
	return soundnames[sound_id];
}

int TieFlightSound_FindId(const char* name) {
	TieFlightSoundLayout layout = TieFlightSound_Layout();
	uint16_t i;

	if (!name)
		return -1;
	for (i = 4; i < layout.table_count; ++i)
		if (soundnames[i][0] && strcmp(soundnames[i], name) == 0)
			return i;
	return -1;
}

void TieFlightSound_StoreName(uint16_t sound_id, const char* bank_path, const uint8_t record_name[8]) {
	const char* base;
	const char* p;
	size_t bank_len;
	int i;

	char bank[12];
	char record[9];

	if (sound_id >= FSFX_NUM_SOUND_HANDLES)
		return;
	base = bank_path;
	for (p = bank_path; *p; ++p)
		if (*p == '/' || *p == '\\')
			base = p + 1;

	bank_len = 0;
	while (base[bank_len] && base[bank_len] != '.' && bank_len + 1 < sizeof bank) {
		bank[bank_len] = base[bank_len];
		++bank_len;
	}
	bank[bank_len] = '\0';

	memcpy(record, record_name, 8);
	record[8] = '\0';
	for (i = 7; i >= 0 && (record[i] == ' ' || record[i] == '\0'); --i)
		record[i] = '\0';
	snprintf(soundnames[sound_id], sizeof soundnames[sound_id], "%s:%s", bank, record);
}

/* Byte-swap a big-endian u32 directory tag to host byte order. */
static uint32_t TieFlightSound_SwapDword(uint32_t v) {
	return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) | ((v & 0x0000FF00u) << 8) |
		   ((v & 0x000000FFu) << 24);
}

int TieFlightSound_LoadBank(const char* filename, int start_idx, int end_idx, int max_records) {
	TieFile* fp = TieStorage_Open(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb");
	uint32_t dir_size_dw;
	uint16_t dir_size;
	uint16_t num_records;
	uint8_t* dir_buf;
	int handle_idx;
	long file_skip;
	uint16_t i;

	uint8_t header[16];

	if (!fp) {
		TieDiagnostics_Log(TIE_LOG_WARN, "fsfx: fopen(\"%s\") failed (SFX bank not loaded)\n", filename);
		return 0;
	}

	/* 16-byte file header -- only the trailing dword 'dir_size' is
	 * consulted; bytes 0..11 are unused format/magic. */
	if (TieStorage_Read(header, 1, 16, fp) != 16) {
		TieStorage_Close(fp);
		return 0;
	}
	memcpy(&dir_size_dw, &header[12], 4);
	dir_size = (uint16_t)dir_size_dw;
	num_records = (uint16_t)(dir_size >> 4);
	if (max_records > 0 && num_records > max_records)
		num_records = (uint16_t)max_records;
	if (num_records > (uint16_t)(end_idx - start_idx))
		num_records = (uint16_t)(end_idx - start_idx);

	/* Read the directory block (num_records * 16 bytes of tag/name/size). */
	dir_buf = (uint8_t*)malloc(dir_size ? dir_size : 16);
	if (!dir_buf) {
		TieStorage_Close(fp);
		return 0;
	}
	if (TieStorage_Read(dir_buf, 1, dir_size, fp) != dir_size) {
		free(dir_buf);
		TieStorage_Close(fp);
		return 0;
	}

	handle_idx = start_idx;
	file_skip = 0;
	for (i = 0; i < num_records; i++) {
		uint16_t rec_off = (uint16_t)(i * 16);

		/* Each record's payload in the data section is preceded by a
		 * 16-byte per-file LFD entry header (a duplicate of the
		 * directory record). Skip it before reading, as the original
		 * loader does at the top of every iteration. */
		uint32_t tag;
		uint16_t sample_size;
		uint32_t sample_size_dw;
		void* handle;

		file_skip += 16;

		/* Directory records pack [tag(BE u32) | name(8) | size(u32)].
		 * Byte-swap the tag in place as the original does. */
		memcpy(&tag, &dir_buf[rec_off], 4);
		tag = TieFlightSound_SwapDword(tag);
		memcpy(&dir_buf[rec_off], &tag, 4);

		memcpy(&sample_size, &dir_buf[rec_off + 12], 2);
		memcpy(&sample_size_dw, &dir_buf[rec_off + 12], 4);

		/* Allocate the slot (matches XMEMHDL_Alloc_Handle(size, 0)). */
		handle = malloc(sample_size ? sample_size : 1);
		soundhandles[handle_idx] = handle;
		if (handle) {
			TieFlightSound_StoreName((uint16_t)handle_idx, filename, &dir_buf[rec_off + 4]);
			if (file_skip) {
				TieStorage_Seek(fp, file_skip, TIE_SEEK_CUR);
				file_skip = 0;
			}
			/* A short read leaves whatever was read; the original
			 * silently tolerates this too. */
			(void)TieStorage_Read(handle, 1, sample_size, fp);
		} else {
			file_skip += sample_size_dw;
		}
		handle_idx++;
		blastflag = 1;
	}

	free(dir_buf);
	TieStorage_Close(fp);
	return handle_idx - start_idx;
}
