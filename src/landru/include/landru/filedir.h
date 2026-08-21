#ifndef LANDRU_FILEDIR_H
#define LANDRU_FILEDIR_H

#include <stdbool.h>
#include <stdint.h>

#define FILEDIR_MAX_ENTRIES 384
/* PORT: the recovered TIE95 directory entry stores 12 characters. The
 * runtime entry is widened so TIE98 registration can retain its names. */
#define FILEDIR_DEFAULT_NAME_LENGTH 12
#define FILEDIR_MAX_NAME_LENGTH 32

typedef struct {
	char name[FILEDIR_MAX_NAME_LENGTH + 1];
	uint8_t sort_key; /* 1 = directory, 0 = file */
	int16_t size_kb;
} DirEntry;

typedef struct {
	char pattern[34];
	DirEntry* entries;
	int16_t field_36;
	int16_t field_38;
	int16_t count;
	int16_t flags;
	int16_t ext_count;
	/* PORT: per-consumer limit; not a field in the recovered structure. */
	uint8_t max_name_length;
} Directory;

void lfiledir_Create_Directory_Module(void);
void lfiledir_Destroy_Directory_Module(void);
void lfiledir_Init_Directory(Directory* dir, const char* pattern, int16_t flags);
/* PORT: no corresponding recovered Landru function. */
void lfiledir_Set_Name_Length(Directory* dir, uint8_t max_name_length);
void lfiledir_Free_Directory(Directory* dir);
void lfiledir_Read_Directory(Directory* dir);
void lfiledir_Add_File_To_Directory(Directory* dir, DirEntry* entries, DirEntry* entry);
void lfiledir_Change_Current_Drive(Directory* dir, int drive);
void lfiledir_Change_System_Drive(int drive);
int lfiledir_Get_Current_Drive(void);
void lfiledir_Change_Current_Dir(Directory* dir, const char* path);
char* lfiledir_Get_Current_Dir(void);
void lfiledir_Set_Current_File(const char* filename);
char* lfiledir_Get_Current_File(void);
char* lfiledir_Get_Current_Path(void);

#endif
