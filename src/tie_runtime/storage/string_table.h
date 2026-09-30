#ifndef TIE_RUNTIME_STORAGE_STRING_TABLE_H
#define TIE_RUNTIME_STORAGE_STRING_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Host storage and pointer-width adaptation for the recovered string loader.
 * The game owns file reads and assigns its string groups after resolution. */
void* TieStringTable_Allocate(void);
void* TieStringTable_Data(void);
char** TieStringTable_Resolve(void);
char** TieStringTable_Current(void);
void TieStringTable_Clear(void);
const char* TieStringTable_Cell(int cell);
int TieStringTable_Count(void);

extern const char* spec_name_ptrs[69];

#ifdef __cplusplus
}
#endif

#endif
