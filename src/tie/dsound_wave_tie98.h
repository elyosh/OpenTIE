#ifndef DSOUND_WAVE_TIE98_H
#define DSOUND_WAVE_TIE98_H

#include <stddef.h>
#include <stdint.h>

typedef struct DirectSoundWaveBuffer DirectSoundWaveBuffer;

int DirectSound_CreateStreamingWaveBuffer(DirectSoundWaveBuffer** out_buffer, size_t buffer_bytes,
										  size_t* data_offset, int file_stream_channel);
DirectSoundWaveBuffer* DirectSound_CreateStaticBufferFromWaveFile(const char* path);
int DirectSound_LockBuffer(DirectSoundWaveBuffer* buffer, size_t offset, size_t bytes, void** first,
						   size_t* first_bytes, void** second, size_t* second_bytes);
int DirectSound_UnlockBuffer(DirectSoundWaveBuffer* buffer, void* first, size_t first_bytes, void* second,
							 size_t second_bytes);
void DirectSound_PlayBuffer(DirectSoundWaveBuffer* buffer, size_t cursor, int looping, int volume);
void DirectSound_StopBuffer(DirectSoundWaveBuffer* buffer);
size_t DirectSound_GetPlayCursor(DirectSoundWaveBuffer* buffer);
int DirectSound_IsBufferPlaying(DirectSoundWaveBuffer* buffer);
void DirectSound_ReleaseBuffer(DirectSoundWaveBuffer* buffer);
size_t DirectSound_BufferSize(const DirectSoundWaveBuffer* buffer);

#endif
