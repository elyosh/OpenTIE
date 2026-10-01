#ifndef TIE_CDAUDIO_TIE98_H
#define TIE_CDAUDIO_TIE98_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int cdaudio_Open_Device(void);
int cdaudio_Play_Track(int track, int start_minute, int start_second);
void cdaudio_Stop_Track(void);
void cdaudio_Close_Device(void);
int32_t cdaudio_Track_Length_Ms(int track);
void cdaudio_Set_Volume(uint32_t volume);

#ifdef __cplusplus
}
#endif

#endif
