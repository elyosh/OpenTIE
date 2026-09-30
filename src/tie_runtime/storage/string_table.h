#ifndef TIE_RUNTIME_STORAGE_STRING_TABLE_H
#define TIE_RUNTIME_STORAGE_STRING_TABLE_H

#include "tie_runtime/storage/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host-thread storage. Returned strings remain valid until the next successful
 * read or Clear; failed reads retain the previous table. */
/* Read consumes a file positioned at its beginning. */
char** TieStringTable_Read(TieFile* file, size_t minimum_entries);
char** TieStringTable_Current(void);
void TieStringTable_Clear(void);
const char* TieStringTable_Cell(int cell);
int TieStringTable_Count(void);
const char* TieStringTable_SpeciesName(unsigned int species);

#ifdef __cplusplus
}
#endif

#endif
