#ifndef TIE_RUNTIME_STORAGE_MEDIA_STREAM_H
#define TIE_RUNTIME_STORAGE_MEDIA_STREAM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { TIE_MEDIA_STREAM_PATH_MAX = 512 };

/* Port-owned replacement for the TIE98 FrontendFileStream page ring on the
 * wave channel: a two-entry path queue whose head file is read synchronously
 * through the TIE98 media VFS root. */
int TieMediaStream_Queue(const char* path);
void TieMediaStream_PopHead(void);
int TieMediaStream_Count(void);
const char* TieMediaStream_Path(int index);
int TieMediaStream_OpenHead(void);
size_t TieMediaStream_Read(void* destination, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
