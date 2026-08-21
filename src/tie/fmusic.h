#ifndef TIE_FMUSIC_H
#define TIE_FMUSIC_H

#include <stdint.h>

extern void* music_buffer;
extern int16_t num_music;

void fmusic_allocmusicbuffer(void);
void fmusic_freemusic(void);
int16_t fmusic_loadmusic(const char* filename);

int16_t fmusic_fmLoadSound(const char* name);
int16_t fmusic_fmUnloadSound(void);
void* fmusic_GetPagedSound(uint16_t track_idx);
int16_t fmusic_PageSound(uint16_t track_idx);

#endif
