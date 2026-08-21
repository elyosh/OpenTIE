#ifndef TIE_STORAGE_H
#define TIE_STORAGE_H

#include "aeron/vfs.h"

#include <stddef.h>
#include <stdint.h>

typedef void TieFile;
typedef void TieDir;

typedef enum TieFileRoot {
	TIE_FILE_ROOT_FRONTEND_ASSET,
	TIE_FILE_ROOT_FLIGHT_ASSET,
	TIE_FILE_ROOT_TIE98_MEDIA,
	TIE_FILE_ROOT_USER,
	TIE_FILE_ROOT_TEMP,
} TieFileRoot;

enum {
	TIE_SEEK_SET = 0,
	TIE_SEEK_CUR = 1,
	TIE_SEEK_END = 2,
};

#define TIE_EOF (-1)
#define TIE_DIR_NAME_MAX 256

typedef struct TieDirEntry {
	char name[TIE_DIR_NAME_MAX];
	int is_dir;
#if defined(__WATCOMC__)
	uint32_t size;
#else
	uint64_t size;
#endif
} TieDirEntry;

typedef struct TieStorageConfig {
	AeronVfs* application;
	AeronVfs* frontend;
	AeronVfs* flight;
	AeronVfs* tie95_flight;
	AeronVfs* tie98_flight;
	AeronVfs* tie98_media;
} TieStorageConfig;

void TieStorage_Init(const TieStorageConfig* config);
void TieStorage_Shutdown(void);
void TieStorage_SelectFlightVfs(AeronVfs* vfs);
AeronVfs* TieStorage_Tie95FlightVfs(void);
AeronVfs* TieStorage_Tie98FlightVfs(void);

TieFile* TieStorage_Open(TieFileRoot root, const char* path, const char* mode);
size_t TieStorage_Read(void* buffer, size_t size, size_t count, TieFile* file);
size_t TieStorage_Write(const void* buffer, size_t size, size_t count, TieFile* file);
int TieStorage_WriteAllAtomic(TieFileRoot root, const char* path, const void* data, size_t size);
int TieStorage_Seek(TieFile* file, long offset, int whence);
long TieStorage_Tell(TieFile* file);
int TieStorage_Close(TieFile* file);
int TieStorage_Remove(TieFileRoot root, const char* path);
TieDir* TieStorage_DirOpen(TieFileRoot root, const char* path);
int TieStorage_DirNext(TieDir* directory, TieDirEntry* entry);
void TieStorage_DirClose(TieDir* directory);
int TieStorage_IsDirectory(TieFileRoot root, const char* path);
int TieStorage_Getc(TieFile* file);
int TieStorage_Putc(int value, TieFile* file);

#endif
