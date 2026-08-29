#include "tie/cdaudio_tie98.h"

#include "tie/frontend_display_tie98.h"
#include "aeron/compat/mmsystem.h"

#include <limits.h>
#include <string.h>

// GLOBAL: TIE98 0x588E6C
static MCIDEVICEID cdaudio_device_id;
// GLOBAL: TIE98 0x588DE4
static int cdaudio_track_count;
// GLOBAL: TIE98 0x588DEC
static uint32_t cdaudio_track_lengths[31];
// GLOBAL: TIE98 0x588DE8
static int32_t cdaudio_saved_aux_volume = -1;
// GLOBAL: TIE98 0x588E68
static int cdaudio_playing_track;

static void CDAUDIO_Clear_State(void) {
	memset(&cdaudio_track_lengths[1], 0, 0x78);
	cdaudio_track_count = 0;
	cdaudio_playing_track = 0;
}

// FUNCTION: TIE98 0x46F5B0
int CDAUDIO_Open_Device(void) {
	MCI_OPEN_PARMSA open_params = { 0 };
	MCI_SET_PARMS set_params = { 0 };
	MCI_STATUS_PARMS status = { 0 };
	uint32_t aux_volume;

	if (!g_flightWindowHandle)
		return 0;
	if (cdaudio_device_id)
		CDAUDIO_Close_Device();
	CDAUDIO_Clear_State();
	for (uint32_t i = 0; i < auxGetNumDevs(); ++i) {
		AUXCAPSA caps;
		if (auxGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR &&
			caps.wTechnology == AUXCAPS_CDAUDIO && (caps.dwSupport & AUXCAPS_VOLUME) &&
			auxGetVolume(i, &aux_volume) == MMSYSERR_NOERROR) {
			cdaudio_saved_aux_volume = (int32_t)(aux_volume & 0xFFFFu);
			break;
		}
	}
	open_params.lpstrDeviceType = "cdaudio";
	if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE, (MciDwordPtr)(uintptr_t)&open_params) != MMSYSERR_NOERROR)
		return 0;
	cdaudio_device_id = open_params.wDeviceID;
	set_params.dwTimeFormat = MCI_FORMAT_TMSF;
	if (mciSendCommandA(cdaudio_device_id, MCI_SET, MCI_SET_TIME_FORMAT,
						(MciDwordPtr)(uintptr_t)&set_params) != MMSYSERR_NOERROR)
		goto fail;
	status.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
	if (mciSendCommandA(cdaudio_device_id, MCI_STATUS, MCI_STATUS_ITEM, (MciDwordPtr)(uintptr_t)&status) !=
		MMSYSERR_NOERROR)
		goto fail;
	cdaudio_track_count = status.dwReturn > 30 ? 30 : (int)status.dwReturn;
	for (int track = 1; track <= cdaudio_track_count; ++track) {
		status.dwItem = MCI_STATUS_LENGTH;
		status.dwTrack = (uint32_t)track;
		if (mciSendCommandA(cdaudio_device_id, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK,
							(MciDwordPtr)(uintptr_t)&status) != MMSYSERR_NOERROR)
			goto fail;
		cdaudio_track_lengths[track] = (uint32_t)status.dwReturn;
	}
	return 1;

fail:
	mciSendCommandA(cdaudio_device_id, MCI_CLOSE, MCI_WAIT, 0);
	cdaudio_device_id = 0;
	CDAUDIO_Clear_State();
	return 0;
}

// FUNCTION: TIE98 0x46F7A0
int CDAUDIO_Play_Track(int track, int start_minute, int start_second) {
	MCI_PLAY_PARMS play = { 0 };
	uint32_t length;
	if (!cdaudio_device_id || track < 1 || track > cdaudio_track_count)
		return 0;
	length = cdaudio_track_lengths[track];
	play.dwCallback = (MciDwordPtr)(uintptr_t)g_flightWindowHandle;
	play.dwFrom = MCI_MAKE_TMSF(track, start_minute, start_second, 0);
	play.dwTo = MCI_MAKE_TMSF(track, MCI_MSF_MINUTE(length), MCI_MSF_SECOND(length), MCI_MSF_FRAME(length));
	if (mciSendCommandA(cdaudio_device_id, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO,
						(MciDwordPtr)(uintptr_t)&play) != MMSYSERR_NOERROR)
		return 0;
	cdaudio_playing_track = track;
	return 1;
}

// FUNCTION: TIE98 0x46F870
void CDAUDIO_Stop_Track(void) {
	if (!cdaudio_device_id || !cdaudio_playing_track)
		return;
	mciSendCommandA(cdaudio_device_id, MCI_STOP, 0, 0);
	cdaudio_playing_track = 0;
}

// FUNCTION: TIE98 0x46F8C0
void CDAUDIO_Close_Device(void) {
	if (cdaudio_device_id) {
		CDAUDIO_Stop_Track();
		mciSendCommandA(cdaudio_device_id, MCI_CLOSE, MCI_WAIT, 0);
		cdaudio_device_id = 0;
	}
	CDAUDIO_Clear_State();
	if (cdaudio_saved_aux_volume >= 0) {
		const uint32_t volume = 0x10001u * (uint32_t)cdaudio_saved_aux_volume;
		for (uint32_t i = 0; i < auxGetNumDevs(); ++i) {
			AUXCAPSA caps;
			if (auxGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR &&
				caps.wTechnology == AUXCAPS_CDAUDIO && (caps.dwSupport & AUXCAPS_VOLUME))
				auxSetVolume(i, volume);
		}
		cdaudio_saved_aux_volume = -1;
	}
}

// FUNCTION: TIE98 0x46F9F0
int32_t CDAUDIO_Track_Length_Ms(int track) {
	uint32_t length;
	if (track < 1 || track > cdaudio_track_count)
		return 0;
	length = cdaudio_track_lengths[track];
	return 1000 * (60 * (int32_t)MCI_MSF_MINUTE(length) + (int32_t)MCI_MSF_SECOND(length)) +
		   1000 * (int32_t)MCI_MSF_FRAME(length) / 75;
}

// FUNCTION: TIE98 0x46FA70
void CDAUDIO_Set_Volume(uint32_t volume) {
	if (volume > UINT16_MAX)
		volume = UINT16_MAX;
	volume *= 0x10001u;
	for (uint32_t i = 0; i < auxGetNumDevs(); ++i) {
		AUXCAPSA caps;
		if (auxGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR &&
			caps.wTechnology == AUXCAPS_CDAUDIO && (caps.dwSupport & AUXCAPS_VOLUME))
			auxSetVolume(i, volume);
	}
}
