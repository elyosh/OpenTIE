#ifndef DSOUND_WAVE_TIE98_H
#define DSOUND_WAVE_TIE98_H

#include <aeron/compat/dsound.h>

/* Runtime ownership of the TIE98 media device and primary buffer. */
int TieDirectSound_Init(void* window);
void TieDirectSound_Shutdown(void);

int DirectSound_CreateWaveBuffer(IDirectSoundBuffer** out_buffer, uint32_t buffer_bytes, DSWaveFormat* format,
								 int alternate_capabilities);
IDirectSoundBuffer* DirectSound_LoadWaveBuffer(IDirectSound* device, const char* path,
											   int alternate_capabilities);
IDirectSoundBuffer* DirectSound_LoadWaveBufferIntoPtr(IDirectSoundBuffer** out_buffer, const char* path,
													  int alternate_capabilities);
int DirectSound_CopyWaveDataToBuffer(IDirectSoundBuffer* buffer, const void* samples, uint32_t bytes);
int DirectSound_CreateStreamingWaveBuffer(IDirectSoundBuffer** out_buffer, uint32_t buffer_bytes,
										  uint32_t* data_offset, int file_stream_channel);
int DirectSound_LockBuffer(IDirectSoundBuffer* buffer, uint32_t offset, uint32_t bytes, void** first,
						   uint32_t* first_bytes, void** second, uint32_t* second_bytes);
int DirectSound_UnlockBuffer(IDirectSoundBuffer* buffer, void* first, uint32_t first_bytes, void* second,
							 uint32_t second_bytes);
void DirectSound_PlayBuffer(IDirectSoundBuffer* buffer, uint32_t cursor, int looping, int volume);
int DirectSound_StopBuffer(IDirectSoundBuffer* buffer);
uint32_t DirectSound_GetPlayCursor(IDirectSoundBuffer* buffer);
void DirectSound_ReleaseBuffer(IDirectSoundBuffer** buffer);

#endif
