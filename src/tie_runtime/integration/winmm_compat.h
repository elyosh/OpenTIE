#ifndef TIE_RUNTIME_INTEGRATION_WINMM_COMPAT_H
#define TIE_RUNTIME_INTEGRATION_WINMM_COMPAT_H

#include "aeron/winmm/mmsystem.h"

#define mciSendCommandA AeronWinmm_MciSendCommandA
#define auxGetNumDevs AeronWinmm_AuxGetNumDevs
#define auxGetDevCapsA AeronWinmm_AuxGetDevCapsA
#define auxGetVolume AeronWinmm_AuxGetVolume
#define auxSetVolume AeronWinmm_AuxSetVolume

#endif
