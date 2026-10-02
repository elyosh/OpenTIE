#include "tie/filestream_tie98.h"
#include "tie_runtime/storage/media_stream.h"

#include <stdint.h>
#include <string.h>

// FUNCTION: TIE98 0x4C05D0
int FrontendFileStream_QueueFile(int channel, const char* path) {
	if (channel != 1 || !path || !path[0])
		return 0;
	return TieMediaStream_Queue(path);
}

// FUNCTION: TIE98 0x4C06A0
int FrontendFileStream_PopHead(int channel) {
	if (channel != 1)
		return 0;
	TieMediaStream_PopHead();
	return 1;
}

// FUNCTION: TIE98 0x4C0710
int FrontendFileStream_RotateToNext(int channel) {
	char current[TIE_MEDIA_STREAM_PATH_MAX];
	char next[TIE_MEDIA_STREAM_PATH_MAX];
	if (channel != 1 || TieMediaStream_Count() == 0)
		return 0;
	strcpy(current, TieMediaStream_Path(0));
	strcpy(next, TieMediaStream_Count() > 1 ? TieMediaStream_Path(1) : TieMediaStream_Path(0));
	FrontendFileStream_PopHead(channel);
	if (!FrontendFileStream_QueueFile(channel, current))
		return 0;
	return FrontendFileStream_StartNamedFile(channel, next);
}

// FUNCTION: TIE98 0x4C0840
int FrontendFileStream_StartNamedFile(int channel, const char* path) {
	int match;
	int index;

	if (channel != 1 || !path)
		return 0;
	match = -1;
	for (index = 0; index < TieMediaStream_Count(); ++index) {
		if (strncmp(path, TieMediaStream_Path(index), strlen(path)) == 0) {
			match = index;
			break;
		}
	}
	if (match < 0)
		return 0;
	while (match-- > 0)
		FrontendFileStream_PopHead(channel);
	return TieMediaStream_OpenHead();
}

// FUNCTION: TIE98 0x4C0DE0
int FrontendFileStream_ReadBytes(int channel, void* destination, size_t destination_offset, size_t bytes,
								 int initial_fill) {
	(void)initial_fill; /* The port's VFS reads synchronously, so data is always ready. */
	if (channel != 1 || !destination || !bytes || bytes > INT32_MAX || !TieMediaStream_OpenHead())
		return 0;
	return (int)TieMediaStream_Read((uint8_t*)destination + destination_offset, bytes);
}
