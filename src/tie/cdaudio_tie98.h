#ifndef TIE_CDAUDIO_TIE98_H
#define TIE_CDAUDIO_TIE98_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int CDAUDIO_Open_Device(void);
int CDAUDIO_Play_Track(int track, int start_minute, int start_second);
void CDAUDIO_Stop_Track(void);
void CDAUDIO_Close_Device(void);
int32_t CDAUDIO_Track_Length_Ms(int track);
void CDAUDIO_Set_Volume(uint32_t volume);

#ifdef __cplusplus
}
#endif

#endif
