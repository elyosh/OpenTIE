#include "tie_runtime/storage/media_stream.h"
#include "tie_runtime/storage/storage.h"

#include <string.h>

typedef struct TieMediaStreamState {
	char paths[2][TIE_MEDIA_STREAM_PATH_MAX];
	int count;
	TieFile* file;
} TieMediaStreamState;

static TieMediaStreamState s_wave_files;

int TieMediaStream_Queue(const char* path) {
	if (s_wave_files.count >= 2 || strlen(path) >= TIE_MEDIA_STREAM_PATH_MAX)
		return 0;
	strcpy(s_wave_files.paths[s_wave_files.count++], path);
	return 1;
}

void TieMediaStream_PopHead(void) {
	if (s_wave_files.file)
		TieStorage_Close(s_wave_files.file);
	s_wave_files.file = NULL;
	if (s_wave_files.count > 0) {
		if (s_wave_files.count > 1)
			memcpy(s_wave_files.paths[0], s_wave_files.paths[1], sizeof s_wave_files.paths[0]);
		memset(s_wave_files.paths[s_wave_files.count - 1], 0, sizeof s_wave_files.paths[0]);
		--s_wave_files.count;
	}
}

int TieMediaStream_Count(void) { return s_wave_files.count; }

const char* TieMediaStream_Path(int index) { return s_wave_files.paths[index]; }

int TieMediaStream_OpenHead(void) {
	if (s_wave_files.file || s_wave_files.count == 0)
		return s_wave_files.file != NULL;
	s_wave_files.file = TieStorage_Open(TIE_FILE_ROOT_TIE98_MEDIA, s_wave_files.paths[0], "rb");
	return s_wave_files.file != NULL;
}

size_t TieMediaStream_Read(void* destination, size_t bytes) {
	return TieStorage_Read(destination, 1, bytes, s_wave_files.file);
}
