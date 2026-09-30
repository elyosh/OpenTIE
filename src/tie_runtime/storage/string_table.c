#include "tie_runtime/storage/string_table.h"
#include "tie/fediskio.h"

#include <stdint.h>
#include <stdlib.h>

static void* s_data;
static char** s_cells;
static size_t s_capacity;
static size_t s_count;

const char* spec_name_ptrs[69];

void* TieStringTable_Allocate(void) {
	s_data = malloc(16000);
	return s_data;
}

void* TieStringTable_Data(void) { return s_data; }

char** TieStringTable_Resolve(void) {
	const uint32_t* offsets = (const uint32_t*)s_data;
	size_t count = 0;

	while (offsets[count])
		++count;
	if (count > s_capacity) {
		free(s_cells);
		s_cells = (char**)malloc(count * sizeof(char*));
		if (!s_cells)
			fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);
		s_capacity = count;
	}
	for (size_t i = 0; i < count; ++i)
		s_cells[i] = (char*)s_data + offsets[i];
	s_count = count;
	return s_cells;
}

char** TieStringTable_Current(void) { return s_cells; }

void TieStringTable_Clear(void) {
	s_count = 0;
	free(s_data);
	s_data = NULL;
}

const char* TieStringTable_Cell(int cell) {
	return cell >= 0 && (size_t)cell < s_count ? s_cells[cell] : NULL;
}

int TieStringTable_Count(void) { return (int)s_count; }
