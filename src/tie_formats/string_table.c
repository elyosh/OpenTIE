#include "tie_formats/string_table.h"
#include "tie_formats/internal.h"

#include <string.h>

bool TieStringIndex_Parse(const void* data, size_t size, TieStringIndex* out, TieFormatError* error) {
	const uint8_t* bytes = data;
	size_t count = 0;
	size_t text_end;

	if (out)
		memset(out, 0, sizeof *out);
	if (error)
		memset(error, 0, sizeof *error);
	if (!bytes || !out || size < 4)
		return TieFormat_SetError(error, 1, "invalid string-table size");

	/* The zero word terminates the offset table, not the file's text. */
	while (count < size / 4 && TieFormat_ReadU32Le(bytes + 4 * count) != 0)
		++count;
	if (count == size / 4)
		return TieFormat_SetError(error, 2, "unterminated string-offset table");

	/* Any offset before the last NUL has a terminator within the file. */
	text_end = size;
	while (text_end != 0 && bytes[text_end - 1] != 0)
		--text_end;
	for (size_t i = 0; i < count; ++i) {
		uint32_t offset = TieFormat_ReadU32Le(bytes + 4 * i);
		if (offset >= text_end)
			return TieFormat_SetError(error, 3, "invalid or unterminated string at cell %zu", i);
	}
	out->data = bytes;
	out->count = count;
	return true;
}

const char* TieStringIndex_At(const TieStringIndex* index, size_t cell) {
	if (!index || !index->data || cell >= index->count)
		return NULL;
	return (const char*)index->data + TieFormat_ReadU32Le(index->data + 4 * cell);
}
