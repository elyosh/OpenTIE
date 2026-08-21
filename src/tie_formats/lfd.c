#include "tie_formats/lfd.h"
#include "tie_formats/internal.h"

#include <stdlib.h>
#include <string.h>

#define TIE_LFD_MAX_ARCHIVE_SIZE (256u * 1024u * 1024u)
#define TIE_LFD_MAX_ENTRIES 4096u

void TieLfdIndex_Free(TieLfdIndex* index) {
	if (!index)
		return;
	free(index->entries);
	memset(index, 0, sizeof *index);
}

bool TieLfdIndex_Parse(const void* header_and_directory, size_t available_size, uint64_t declared_file_size,
					   TieLfdIndex* out, TieFormatError* error) {
	const uint8_t* bytes = header_and_directory;
	if (out)
		memset(out, 0, sizeof *out);
	if (error)
		memset(error, 0, sizeof *error);
	if (!bytes || !out || available_size < 16 || declared_file_size < 16 ||
		declared_file_size > TIE_LFD_MAX_ARCHIVE_SIZE)
		return TieFormat_SetError(error, 1, "invalid LFD archive size");
	if (TieFormat_ReadFourcc(bytes) != TIE_FOURCC('R', 'M', 'A', 'P'))
		return TieFormat_SetError(error, 2, "archive does not start with RMAP");
	const uint32_t directory_size = TieFormat_ReadU32Le(bytes + 12);
	if (directory_size % 16)
		return TieFormat_SetError(error, 3, "RMAP directory size is not a multiple of 16");
	if (directory_size > available_size - 16)
		return TieFormat_SetError(error, 4, "RMAP directory is truncated");
	const uint32_t count = directory_size / 16;
	if (count > TIE_LFD_MAX_ENTRIES)
		return TieFormat_SetError(error, 5, "RMAP contains too many entries");
	TieLfdEntry* entries = calloc(count ? count : 1, sizeof *entries);
	if (!entries)
		return TieFormat_SetError(error, 6, "RMAP allocation failed");
	uint64_t chunk_offset = 16u + directory_size;
	for (uint32_t index = 0; index < count; ++index) {
		const uint8_t* record = bytes + 16 + (size_t)index * 16;
		TieLfdEntry* entry = &entries[index];
		entry->type = TieFormat_ReadFourcc(record);
		memcpy(entry->name, record + 4, 8);
		entry->name[8] = '\0';
		entry->size = TieFormat_ReadU32Le(record + 12);
		if (entry->size > TIE_FORMAT_MAX_ENTRY_SIZE) {
			free(entries);
			return TieFormat_SetError(error, 7, "RMAP entry %u exceeds 64 MiB", index);
		}
		if (chunk_offset > UINT64_MAX - 16 || chunk_offset + 16 > UINT64_MAX - entry->size ||
			chunk_offset + 16 + entry->size > declared_file_size) {
			free(entries);
			return TieFormat_SetError(error, 8, "RMAP entry %u extends beyond archive", index);
		}
		entry->payload_offset = chunk_offset + 16;
		chunk_offset = entry->payload_offset + entry->size;
	}
	out->entries = entries;
	out->count = count;
	out->file_size = declared_file_size;
	return true;
}
