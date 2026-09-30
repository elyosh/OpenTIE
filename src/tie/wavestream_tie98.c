#include "tie/wavestream_tie98.h"
#include "tie/dsound_wave_tie98.h"
#include "tie/filestream_tie98.h"
#include "tie/shellext.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdlib.h>
#include <string.h>

enum {
	WAVE_STREAM_STATIC_LIMIT = 251000,
	WAVE_STREAM_BUFFER_BYTES = 500000,
	WAVE_STREAM_PREFILL_BYTES = 250000,
	WAVE_STREAM_MAX_REFILL_BYTES = 64000,
	WAVE_STREAM_MIN_REFILL_BYTES = 1000,
};

// GLOBAL: TIE98 0x584D2C
static uint32_t g_waveStreamDataOffset;
// GLOBAL: TIE98 0x584D30
static int g_waveStreamFillingSaved;
// GLOBAL: TIE98 0x584D34
static uint32_t g_waveStreamPrevFreeBytes;
// GLOBAL: TIE98 0x584D38
static uint32_t g_waveStreamBytesPlayed;
// GLOBAL: TIE98 0x584D3C
static int g_waveStreamFilling;
// GLOBAL: TIE98 0x584D40
static int g_waveStreamEnd;
// GLOBAL: TIE98 0x584D44
static uint32_t g_waveStreamWriteCursor;
// GLOBAL: TIE98 0x584D48
static int g_waveStreamLoop;
// GLOBAL: TIE98 0x584D4C
static uint32_t g_waveStreamRefillThreshold;
// GLOBAL: TIE98 0x584D50
static int g_waveStreamPlaying;
// GLOBAL: TIE98 0x584D54
static uint32_t g_waveStreamPrevPlayCursor;
// GLOBAL: TIE98 0x584D58
static int g_waveStreamIsStreaming;
// GLOBAL: TIE98 0x584D5C
static uint32_t g_waveStreamLastPlayCursor;
// GLOBAL: TIE98 0x584D60
static IDirectSoundBuffer* g_waveStreamBuffer;
// GLOBAL: TIE98 0x584D64
static uint8_t* g_waveStreamStaging;
// GLOBAL: TIE98 0x584D68
static int g_waveStreamPauseDepth;

// FUNCTION: TIE98 0x458250
static int FrontendWaveStream_EnsureBuffer(void) {
	int initial = -1;
	if (!g_waveStreamBuffer) {
		initial = DirectSound_CreateStreamingWaveBuffer(&g_waveStreamBuffer, WAVE_STREAM_BUFFER_BYTES,
														&g_waveStreamDataOffset, 1);
		g_waveStreamPlaying = 0;
		g_waveStreamFilling = 0;
		g_waveStreamFillingSaved = 0;
		g_waveStreamWriteCursor = 0;
	}
	if (!g_waveStreamStaging)
		g_waveStreamStaging = (uint8_t*)malloc(WAVE_STREAM_MAX_REFILL_BYTES);
	if (!g_waveStreamStaging)
		return -1;
	return initial;
}

// FUNCTION: TIE98 0x457B70
static int FrontendWaveStream_StartFile(const char* path) {
	int initial;
	int volume;

	if (!FrontendFileStream_QueueFile(1, path))
		return 0;
	if (g_waveStreamLoop && !FrontendFileStream_QueueFile(1, path)) {
		FrontendFileStream_PopHead(1);
		return 0;
	}
	if (!FrontendFileStream_StartNamedFile(1, path)) {
		if (g_waveStreamLoop)
			FrontendFileStream_PopHead(1);
		FrontendFileStream_PopHead(1);
		return 0;
	}
	initial = FrontendWaveStream_EnsureBuffer();
	g_waveStreamWriteCursor = initial >= 0 ? (uint32_t)initial : 0;
	g_waveStreamBytesPlayed = 0;
	g_waveStreamPrevPlayCursor = 0;
	if (initial < 0) {
		FrontendWaveStream_Shutdown();
		return 0;
	}
	{
		void* first;
		void* second;
		uint32_t first_bytes;
		uint32_t second_bytes;
		if (!DirectSound_LockBuffer(g_waveStreamBuffer, initial, WAVE_STREAM_BUFFER_BYTES - initial, &first,
									&first_bytes, &second, &second_bytes)) {
			FrontendWaveStream_Shutdown();
			return 0;
		}
		memset(first, 0x80, first_bytes);
		if (second_bytes)
			memset(second, 0x80, second_bytes);
		DirectSound_UnlockBuffer(g_waveStreamBuffer, first, first_bytes, second, second_bytes);
	}
	g_waveStreamFilling = 1;
	g_waveStreamEnd = 0;
	while (g_waveStreamWriteCursor < WAVE_STREAM_PREFILL_BYTES) {
		uint32_t bytes = WAVE_STREAM_PREFILL_BYTES - g_waveStreamWriteCursor;
		int got;
		void* first;
		void* second;
		uint32_t first_bytes;
		uint32_t second_bytes;
		if (bytes > WAVE_STREAM_MAX_REFILL_BYTES)
			bytes = WAVE_STREAM_MAX_REFILL_BYTES;
		got = FrontendFileStream_ReadBytes(1, g_waveStreamStaging, 0, bytes, 1);
		if (got == -1)
			continue;
		if (got == 0) {
			g_waveStreamFilling = 0;
			break;
		}
		if (DirectSound_LockBuffer(g_waveStreamBuffer, g_waveStreamWriteCursor, (uint32_t)got, &first,
								   &first_bytes, &second, &second_bytes)) {
			if (first && g_waveStreamStaging) {
				memcpy(first, g_waveStreamStaging, first_bytes);
				g_waveStreamWriteCursor += first_bytes;
				if (g_waveStreamWriteCursor >= WAVE_STREAM_BUFFER_BYTES)
					g_waveStreamWriteCursor -= WAVE_STREAM_BUFFER_BYTES;
			}
			if (second && g_waveStreamStaging) {
				memcpy(second, g_waveStreamStaging + first_bytes, second_bytes);
				g_waveStreamWriteCursor = second_bytes;
			}
			DirectSound_UnlockBuffer(g_waveStreamBuffer, first, first_bytes, second, second_bytes);
		}
	}
	if (g_waveStreamLoop)
		volume = options_gbl.sound_active && options_gbl.sound_volume ? 8 * options_gbl.sound_volume - 1 : 0;
	else
		volume = options_gbl.music_active && options_gbl.music_volume ? 8 * options_gbl.music_volume - 1 : 0;
	DirectSound_PlayBuffer(g_waveStreamBuffer, 0, 1, volume);
	g_waveStreamPlaying = 1;
	g_waveStreamPrevFreeBytes = WAVE_STREAM_PREFILL_BYTES;
	return 1;
}

// FUNCTION: TIE98 0x457F70
static void FrontendWaveStream_Refill(void) {
	void* first;
	void* second;
	uint32_t first_bytes;
	uint32_t second_bytes;
	const int got = FrontendFileStream_ReadBytes(1, g_waveStreamStaging, 0, g_waveStreamRefillThreshold, 0);
	if (got == -1)
		return;
	if (got > 0) {
		void* first;
		void* second;
		uint32_t first_bytes;
		uint32_t second_bytes;
		if (DirectSound_LockBuffer(g_waveStreamBuffer, g_waveStreamWriteCursor, (uint32_t)got, &first,
								   &first_bytes, &second, &second_bytes)) {
			if (first && g_waveStreamStaging) {
				memcpy(first, g_waveStreamStaging, first_bytes);
				g_waveStreamWriteCursor += first_bytes;
				if (g_waveStreamWriteCursor >= WAVE_STREAM_BUFFER_BYTES)
					g_waveStreamWriteCursor -= WAVE_STREAM_BUFFER_BYTES;
			}
			if (second && g_waveStreamStaging) {
				memcpy(second, g_waveStreamStaging + first_bytes, second_bytes);
				g_waveStreamWriteCursor = second_bytes;
			}
			DirectSound_UnlockBuffer(g_waveStreamBuffer, first, first_bytes, second, second_bytes);
		}
		return;
	}
	if (g_waveStreamLoop) {
		int skipped;
		if (!FrontendFileStream_RotateToNext(1))
			return;
		do {
			skipped = FrontendFileStream_ReadBytes(1, g_waveStreamStaging, 0, g_waveStreamDataOffset, 0);
		} while (skipped == -1);
		return;
	}

	if (DirectSound_LockBuffer(g_waveStreamBuffer, g_waveStreamWriteCursor, 0, &first, &first_bytes, &second,
							   &second_bytes)) {
		memset(first, 0x80, first_bytes);
		if (second_bytes)
			memset(second, 0x80, second_bytes);
		DirectSound_UnlockBuffer(g_waveStreamBuffer, first, first_bytes, second, second_bytes);
	}
	g_waveStreamEnd = 1;
	g_waveStreamFilling = 0;
}

// FUNCTION: TIE98 0x4579E0
int FrontendWaveStream_PlayWaveFile(const char* path, int loop) {
	TieFile* file;
	long file_size;
	int volume;

	FrontendWaveStream_Shutdown();
	g_waveStreamPauseDepth = 0;
	g_waveStreamLoop = loop != 0;
	file_size = -1;
	file = path ? TieStorage_Open(TIE_FILE_ROOT_TIE98_MEDIA, path, "rb") : NULL;
	if (file) {
		if (TieStorage_Seek(file, 0, TIE_SEEK_END) == 0)
			file_size = TieStorage_Tell(file);
		TieStorage_Close(file);
	}
	if (file_size <= 0) {
		TieDiagnostics_Log(TIE_LOG_WARN, "TIE98 wave music is missing or unreadable: %s\n",
						   path ? path : "(null)");
		return 0;
	}
	if (file_size <= WAVE_STREAM_STATIC_LIMIT) {
		DirectSound_LoadWaveBufferIntoPtr(&g_waveStreamBuffer, path, 0);
		g_waveStreamFilling = 0;
		g_waveStreamIsStreaming = 0;
		if (!g_waveStreamBuffer) {
			TieDiagnostics_Log(TIE_LOG_WARN, "TIE98 wave music has an unsupported RIFF format: %s\n", path);
			return 0;
		}
		if (g_waveStreamLoop)
			volume =
				options_gbl.sound_active && options_gbl.sound_volume ? 8 * options_gbl.sound_volume - 1 : 0;
		else
			volume =
				options_gbl.music_active && options_gbl.music_volume ? 8 * options_gbl.music_volume - 1 : 0;
		DirectSound_PlayBuffer(g_waveStreamBuffer, 0, g_waveStreamLoop, volume);
		g_waveStreamPlaying = 1;
		return 0;
	}
	g_waveStreamIsStreaming = 1;
	if (!FrontendWaveStream_StartFile(path)) {
		TieDiagnostics_Log(TIE_LOG_WARN, "TIE98 wave music could not start: %s\n", path);
		FrontendWaveStream_Shutdown();
		return 0;
	}
	return 1;
}

// FUNCTION: TIE98 0x457E90
uint32_t FrontendWaveStream_Update(void) {
	uint32_t cursor, free_bytes, played_delta;

	if (!g_waveStreamBuffer)
		return g_waveStreamBytesPlayed;
	cursor = DirectSound_GetPlayCursor(g_waveStreamBuffer);
	g_waveStreamLastPlayCursor = cursor;
	free_bytes = cursor > g_waveStreamWriteCursor
					 ? cursor - g_waveStreamWriteCursor
					 : WAVE_STREAM_BUFFER_BYTES - g_waveStreamWriteCursor + cursor;
	played_delta = cursor >= g_waveStreamPrevPlayCursor
					   ? cursor - g_waveStreamPrevPlayCursor
					   : WAVE_STREAM_BUFFER_BYTES - g_waveStreamPrevPlayCursor + cursor;
	g_waveStreamBytesPlayed += (uint32_t)played_delta;
	g_waveStreamPrevPlayCursor = cursor;
	if (g_waveStreamEnd == 1 && free_bytes < g_waveStreamPrevFreeBytes) {
		DirectSound_StopBuffer(g_waveStreamBuffer);
		g_waveStreamPlaying = 0;
		g_waveStreamEnd = 0;
	}
	if (g_waveStreamFilling == 1) {
		int64_t threshold = ((int64_t)free_bytes - WAVE_STREAM_PREFILL_BYTES) & ~(int64_t)3;
		if (threshold > WAVE_STREAM_MAX_REFILL_BYTES)
			threshold = WAVE_STREAM_MAX_REFILL_BYTES;
		if (threshold < WAVE_STREAM_MIN_REFILL_BYTES)
			threshold = WAVE_STREAM_MIN_REFILL_BYTES;
		g_waveStreamRefillThreshold = (uint32_t)threshold;
		if (free_bytes > g_waveStreamRefillThreshold)
			FrontendWaveStream_Refill();
	}
	g_waveStreamPrevFreeBytes = free_bytes;
	return g_waveStreamBytesPlayed;
}

// FUNCTION: TIE98 0x458170
void FrontendWaveStream_Pause(void) {
	if (g_waveStreamPauseDepth == 0) {
		if (g_waveStreamBuffer && g_waveStreamPlaying == 1) {
			DirectSound_StopBuffer(g_waveStreamBuffer);
			g_waveStreamLastPlayCursor = DirectSound_GetPlayCursor(g_waveStreamBuffer);
		}
		g_waveStreamFillingSaved = g_waveStreamFilling;
		g_waveStreamFilling = 0;
	}
	++g_waveStreamPauseDepth;
}

// FUNCTION: TIE98 0x4581D0
void FrontendWaveStream_Resume(void) {
	if (g_waveStreamPauseDepth <= 0 || --g_waveStreamPauseDepth != 0)
		return;
	if (g_waveStreamBuffer && g_waveStreamPlaying == 1)
		DirectSound_PlayBuffer(
			g_waveStreamBuffer, g_waveStreamLastPlayCursor, g_waveStreamLoop || g_waveStreamIsStreaming,
			options_gbl.music_active && options_gbl.music_volume ? 8 * options_gbl.music_volume - 1 : 0);
	g_waveStreamFilling = g_waveStreamFillingSaved;
}

// FUNCTION: TIE98 0x4582C0
void FrontendWaveStream_Shutdown(void) {
	g_waveStreamWriteCursor = 0;
	g_waveStreamFilling = 0;
	g_waveStreamLoop = 0;
	g_waveStreamEnd = 0;
	FrontendFileStream_PopHead(1);
	FrontendFileStream_PopHead(1);
	if (g_waveStreamBuffer) {
		DirectSound_StopBuffer(g_waveStreamBuffer);
		g_waveStreamPlaying = 0;
		DirectSound_ReleaseBuffer(&g_waveStreamBuffer);
	}
	free(g_waveStreamStaging);
	g_waveStreamStaging = NULL;
}
