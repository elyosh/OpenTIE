#ifndef FILESTREAM_TIE98_H
#define FILESTREAM_TIE98_H

#include <stddef.h>

int FrontendFileStream_QueueFile(int channel, const char* path);
int FrontendFileStream_PopHead(int channel);
int FrontendFileStream_StartNamedFile(int channel, const char* path);
int FrontendFileStream_RotateToNext(int channel);
int FrontendFileStream_ReadBytes(int channel, void* destination, size_t destination_offset, size_t bytes,
								 int initial_fill);

#endif
