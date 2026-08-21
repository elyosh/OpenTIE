#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <landru/cursor.h>
#include <landru/filedir.h>
#include <landru/timer.h>

#include "host_internal.h"

/* Directory enumeration through LandruHost. Drive operations are no-ops. */

/* Globals */
static bool dir_module_gbl;
static int file_drive_gbl;
static int def_drive_gbl;
static int num_drives_gbl;
static char def_dir_gbl[64];
static char file_dir_gbl[64];
static char file_name_gbl[16];
static char file_path_gbl[76];
static LandruFileRoot file_root_gbl;

/* --- Helpers --- */

static void str_upper(char* s) {
	while (*s) {
		*s = toupper((unsigned char)*s);
		s++;
	}
}

static const char* get_extension(const char* name) {
	const char* dot = strrchr(name, '.');
	return dot ? dot : "";
}

static int match_extension(const char* file_ext, const char* pattern, int ext_count) {
	(void)ext_count;
	/* Walk pattern's dot-separated extensions, compare each against file_ext.
	   '*' after a dot matches any extension. */
	const char* p = pattern;
	while (*p) {
		if (*p == '.')
			p++;
		/* Extract this pattern segment */
		char seg[34];
		int len = 0;
		seg[0] = '.';
		len = 1;
		while (*p && *p != '.') {
			seg[len++] = *p++;
		}
		seg[len] = '\0';

		/* Wildcard */
		if (seg[1] == '*')
			return 2; /* match, but wildcard */

		/* Compare */
		char upper_ext[34];
		strncpy(upper_ext, file_ext, sizeof(upper_ext) - 1);
		upper_ext[sizeof(upper_ext) - 1] = '\0';
		str_upper(upper_ext);

		char upper_seg[34];
		strncpy(upper_seg, seg, sizeof(upper_seg) - 1);
		upper_seg[sizeof(upper_seg) - 1] = '\0';
		str_upper(upper_seg);

		if (strcmp(upper_ext, upper_seg) == 0)
			return 1; /* exact match */
	}
	return 0;
}

/* --- Module lifecycle --- */

void lfiledir_Create_Directory_Module(void) {
	def_drive_gbl = 0;
	num_drives_gbl = 1;
	file_root_gbl = LANDRU_FILE_ROOT_USER;
	def_dir_gbl[0] = '\0';
	strcpy(file_dir_gbl, def_dir_gbl);
	file_drive_gbl = def_drive_gbl;
	dir_module_gbl = true;
}

void lfiledir_Destroy_Directory_Module(void) { dir_module_gbl = false; }

/* --- Directory init/free --- */

void lfiledir_Init_Directory(Directory* dir, const char* pattern, int16_t flags) {
	dir->entries = (DirEntry*)calloc(FILEDIR_MAX_ENTRIES, sizeof(DirEntry));
	dir->count = 0;
	dir->ext_count = 0;
	dir->flags = flags;
	// PORT: runtime-only extension for TIE98 registration names.
	dir->max_name_length = FILEDIR_DEFAULT_NAME_LENGTH;

	/* Count dots in pattern */
	for (const char* p = pattern; *p; p++) {
		if (*p == '.')
			dir->ext_count++;
	}

	/* Copy and uppercase pattern */
	strncpy(dir->pattern, pattern, sizeof(dir->pattern) - 1);
	dir->pattern[sizeof(dir->pattern) - 1] = '\0';
	str_upper(dir->pattern);
}

// PORT: configures the widened runtime entry used by TIE98 registration.
void lfiledir_Set_Name_Length(Directory* dir, uint8_t max_name_length) {
	if (!dir)
		return;
	if (max_name_length > FILEDIR_MAX_NAME_LENGTH)
		max_name_length = FILEDIR_MAX_NAME_LENGTH;
	dir->max_name_length = max_name_length;
}

void lfiledir_Free_Directory(Directory* dir) {
	if (dir && dir->entries) {
		free(dir->entries);
		dir->entries = NULL;
	}
}

/* --- Read directory --- */

void lfiledir_Read_Directory(Directory* dir) {
	dir->count = 0;
	lcursor_Set_Cursor(1); /* waitCursor */

	LandruDir* dh = landru_host_dir_open(file_root_gbl, file_dir_gbl);
	if (!dh) {
		lcursor_Set_Cursor(0); /* mainCursor */
		return;
	}

	LandruDirEntry de;
	while (landru_host_dir_next(dh, &de)) {
		ltimer_Often();

		DirEntry entry;
		memset(&entry, 0, sizeof(entry));

		// PORT: the recovered TIE95 function uses a fixed 12-byte limit.
		const size_t name_length = dir->max_name_length;
		if (de.is_dir) {
			/* Retail only includes directories when the caller requests them. */
			if (!dir->flags || de.name[0] == '.')
				continue;
			strncpy(entry.name, de.name, name_length);
			entry.name[name_length] = '\0';
			entry.sort_key = 1;
			entry.size_kb = 0;
		} else {
			/* Check extension match */
			const char* ext = get_extension(de.name);
			int m = match_extension(ext, dir->pattern, dir->ext_count);
			if (!m)
				continue;

			strncpy(entry.name, de.name, name_length);
			entry.name[name_length] = '\0';

			/* If flags indicate single-extension mode and exact match, strip extension */
			if (dir->ext_count == 1 && m == 1) {
				char* dot = strchr(entry.name, '.');
				if (dot)
					*dot = '\0';
			}

			entry.sort_key = 0;
			entry.size_kb = (int16_t)((de.size + 1023u) / 1024u);
		}

		str_upper(entry.name);
		lfiledir_Add_File_To_Directory(dir, dir->entries, &entry);
	}

	landru_host_dir_close(dh);
	lcursor_Set_Cursor(0); /* mainCursor */
}

/* --- Sorted insertion --- */

void lfiledir_Add_File_To_Directory(Directory* dir, DirEntry* entries, DirEntry* entry) {
	if (dir->count >= FILEDIR_MAX_ENTRIES)
		return;

	/* Find insertion point: sort by sort_key (dirs first), then alphabetical */
	int16_t i = 0;
	while (i < dir->count && entries[i].sort_key > entry->sort_key)
		i++;
	while (i < dir->count && entries[i].sort_key == entry->sort_key &&
		   strcmp(entry->name, entries[i].name) > 0)
		i++;

	/* Shift entries down to make room */
	for (int16_t j = dir->count; j > i; j--)
		entries[j] = entries[j - 1];

	entries[i] = *entry;
	dir->count++;
}

/* --- Drive operations (no-op on non-DOS) --- */

void lfiledir_Change_Current_Drive(Directory* dir, int drive) {
	(void)drive;
	lfiledir_Read_Directory(dir);
}

void lfiledir_Change_System_Drive(int drive) { (void)drive; }

int lfiledir_Get_Current_Drive(void) { return file_drive_gbl; }

/* --- Directory navigation --- */

void lfiledir_Change_Current_Dir(Directory* dir, const char* path) {
	/* Keep the prior path when the requested directory is unavailable. */
	if (!path || !landru_host_path_is_dir(file_root_gbl, path))
		return;
	strncpy(file_dir_gbl, path, sizeof file_dir_gbl - 1);
	file_dir_gbl[sizeof file_dir_gbl - 1] = '\0';
	lfiledir_Read_Directory(dir);
}

char* lfiledir_Get_Current_Dir(void) { return file_dir_gbl; }

/* --- File selection --- */

void lfiledir_Set_Current_File(const char* filename) {
	strncpy(file_name_gbl, filename, 13);
	file_name_gbl[13] = '\0';
	file_name_gbl[14] = '\0';
	file_name_gbl[15] = '\0';
}

char* lfiledir_Get_Current_File(void) { return file_name_gbl; }

char* lfiledir_Get_Current_Path(void) {
	strcpy(file_path_gbl, lfiledir_Get_Current_Dir());

	int len = strlen(file_path_gbl);
	if (len > 0 && file_path_gbl[len - 1] != '/' && file_path_gbl[len - 1] != '\\')
		strcat(file_path_gbl, "/");

	strcat(file_path_gbl, lfiledir_Get_Current_File());
	return file_path_gbl;
}
