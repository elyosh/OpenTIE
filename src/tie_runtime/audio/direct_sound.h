#ifndef TIE_RUNTIME_AUDIO_DIRECT_SOUND_H
#define TIE_RUNTIME_AUDIO_DIRECT_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime ownership of the TIE98 DirectSound device and primary buffer. The
 * original Sound_Init_Sound_Engine also resets the frontend sound tables,
 * which the port's frontend sound adapter owns. */
int TieDirectSound_Init(void* window);
void TieDirectSound_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
