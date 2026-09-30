#ifndef TIE_WAVESTREAM_TIE98_H
#define TIE_WAVESTREAM_TIE98_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int FrontendWaveStream_PlayWaveFile(const char* path, int loop);
uint32_t FrontendWaveStream_Update(void);
void FrontendWaveStream_Pause(void);
void FrontendWaveStream_Resume(void);
void FrontendWaveStream_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
