#include "tie/filestream_tie98.h"

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdint.h>
#include <string.h>

enum { FRONTEND_FILE_STREAM_PATH_MAX = 512 };

typedef struct FrontendFileStreamState {
	char paths[2][FRONTEND_FILE_STREAM_PATH_MAX];
	int count;
	TieFile* file;
} FrontendFileStreamState;

static FrontendFileStreamState wave_files;

static int tie98_file_stream_open_head(void) {
	if (wave_files.file || wave_files.count == 0)
		return wave_files.file != NULL;
	wave_files.file = TieStorage_Open(TIE_FILE_ROOT_TIE98_MEDIA, wave_files.paths[0], "rb");
	return wave_files.file != NULL;
}

// FUNCTION: TIE98 0x4C05D0
int FrontendFileStream_QueueFile(int channel, const char* path) {
	if (channel != 1 || !path || !path[0] || wave_files.count >= 2 ||
		strlen(path) >= FRONTEND_FILE_STREAM_PATH_MAX)
		return 0;
	strcpy(wave_files.paths[wave_files.count++], path);
	return 1;
}

// FUNCTION: TIE98 0x4C06A0
int FrontendFileStream_PopHead(int channel) {
	if (channel != 1)
		return 0;
	if (wave_files.file)
		TieStorage_Close(wave_files.file);
	wave_files.file = NULL;
	if (wave_files.count > 0) {
		if (wave_files.count > 1)
			memcpy(wave_files.paths[0], wave_files.paths[1], sizeof wave_files.paths[0]);
		memset(wave_files.paths[wave_files.count - 1], 0, sizeof wave_files.paths[0]);
		--wave_files.count;
	}
	return 1;
}

// FUNCTION: TIE98 0x4C0840
int FrontendFileStream_StartNamedFile(int channel, const char* path) {
	if (channel != 1 || !path)
		return 0;
	int match = -1;
	for (int index = 0; index < wave_files.count; ++index) {
		if (strncmp(path, wave_files.paths[index], strlen(path)) == 0) {
			match = index;
			break;
		}
	}
	if (match < 0)
		return 0;
	while (match-- > 0)
		FrontendFileStream_PopHead(channel);
	return tie98_file_stream_open_head();
}

// FUNCTION: TIE98 0x4C0710
int FrontendFileStream_RotateToNext(int channel) {
	char current[FRONTEND_FILE_STREAM_PATH_MAX];
	char next[FRONTEND_FILE_STREAM_PATH_MAX];
	if (channel != 1 || wave_files.count == 0)
		return 0;
	strcpy(current, wave_files.paths[0]);
	strcpy(next, wave_files.count > 1 ? wave_files.paths[1] : wave_files.paths[0]);
	FrontendFileStream_PopHead(channel);
	if (!FrontendFileStream_QueueFile(channel, current))
		return 0;
	return FrontendFileStream_StartNamedFile(channel, next);
}

// FUNCTION: TIE98 0x4C0DE0
int FrontendFileStream_ReadBytes(int channel, void* destination, size_t destination_offset, size_t bytes,
								 int initial_fill) {
	(void)initial_fill; /* The port's VFS reads synchronously, so data is always ready. */
	if (channel != 1 || !destination || !bytes || bytes > INT32_MAX || !tie98_file_stream_open_head())
		return 0;
	return (int)TieStorage_Read((uint8_t*)destination + destination_offset, 1, bytes, wave_files.file);
}
