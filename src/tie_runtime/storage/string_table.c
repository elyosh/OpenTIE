#include "tie_runtime/storage/string_table.h"
#include "tie_formats/string_table.h"
#include "tie_runtime/diagnostics/diagnostics.h"

#include <stdlib.h>

static uint8_t* s_data;
static char** s_cells;
static size_t s_count;

char** TieStringTable_Read(TieFile* file, size_t minimum_entries) {
	int32_t file_size;
	uint8_t* data;
	char** cells;
	TieStringIndex index;
	TieFormatError error;
	size_t size;

	if (!file)
		return NULL;
	file_size = TieStorage_FileLength(file);
	if (file_size < 4)
		return NULL;
	size = (size_t)file_size;
	data = (uint8_t*)malloc(size);
	if (!data)
		return NULL;
	if (TieStorage_Read(data, 1, size, file) != size) {
		free(data);
		return NULL;
	}

	if (!TieStringIndex_Parse(data, size, &index, &error)) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "STRINGS.DAT: %s\n", error.message);
		free(data);
		return NULL;
	}
	if (index.count < minimum_entries || index.count > SIZE_MAX / sizeof(char*)) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "STRINGS.DAT has an invalid cell count: %zu\n", index.count);
		free(data);
		return NULL;
	}
	cells = (char**)malloc(index.count * sizeof(char*));
	if (!cells) {
		free(data);
		return NULL;
	}
	for (size_t i = 0; i < index.count; ++i)
		cells[i] = (char*)TieStringIndex_At(&index, i);

	/* Publish only a complete table; diagnostics may still need the old one. */
	TieStringTable_Clear();
	s_data = data;
	s_cells = cells;
	s_count = index.count;
	return s_cells;
}

char** TieStringTable_Current(void) { return s_cells; }

void TieStringTable_Clear(void) {
	s_count = 0;
	free(s_cells);
	free(s_data);
	s_cells = NULL;
	s_data = NULL;
}

const char* TieStringTable_Cell(int cell) {
	return cell >= 0 && (size_t)cell < s_count ? s_cells[cell] : NULL;
}

int TieStringTable_Count(void) { return (int)s_count; }

const char* TieStringTable_SpeciesName(unsigned int species) {
	return species < 69 ? TieStringTable_Cell((int)(615 + species)) : NULL;
}
