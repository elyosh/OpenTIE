#ifndef WAVESTREAM_TIE98_H
#define WAVESTREAM_TIE98_H

#include <stdint.h>

int FrontendWaveStream_PlayWaveFile(const char* path, int loop);
uint32_t FrontendWaveStream_Update(void);
void FrontendWaveStream_Pause(void);
void FrontendWaveStream_Resume(void);
void FrontendWaveStream_Shutdown(void);

#endif
