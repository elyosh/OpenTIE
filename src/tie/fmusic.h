#ifndef TIE_FMUSIC_H
#define TIE_FMUSIC_H

#include "tie_runtime/storage/storage.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void* music_buffer;
extern int16_t num_music;

void fmusic_allocmusicbuffer(void);
void fmusic_freemusic(void);
int16_t fmusic_loadmusic(const char* filename);
uint32_t fmusic_swapdword(int32_t val);
int16_t fmusic_readfiledata(TieFile* fp, uint8_t* dest, uint16_t total);

int fmusic_fmLoadSound(const char* name);
int16_t fmusic_fmUnloadSound(void);
void* fmusic_GetPagedSound(unsigned int track_idx);
void fmusic_PageSound(unsigned int track_idx);

#ifdef __cplusplus
}
#endif

#endif
