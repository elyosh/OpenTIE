#include "tie_runtime/audio/direct_sound.h"
#include "tie/dsound_wave_tie98.h"

#include <stddef.h>

int TieDirectSound_Init(void* window) {
	DSBufferDesc desc = { 0 };
	void* device;

	if (direct_sound)
		return 1;
	device = NULL;
	if (DirectSoundCreate(NULL, &device, NULL) != 0)
		return 0;
	direct_sound = (IDirectSound*)device;
	if (direct_sound->lpVtbl->SetCooperativeLevel(direct_sound, window, DSSCL_PRIORITY) != 0) {
		TieDirectSound_Shutdown();
		return 0;
	}
	desc.dwSize = 20;
	desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
	if (direct_sound->lpVtbl->CreateSoundBuffer(direct_sound, &desc, &primary_buffer, NULL) != 0) {
		TieDirectSound_Shutdown();
		return 0;
	}
	return 1;
}

void TieDirectSound_Shutdown(void) {
	DirectSound_ReleaseBuffer(&primary_buffer);
	if (direct_sound) {
		direct_sound->lpVtbl->Release(direct_sound);
		direct_sound = NULL;
	}
}
