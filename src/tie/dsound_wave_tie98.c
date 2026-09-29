#include "tie/dsound_wave_tie98.h"

#include "tie/filestream_tie98.h"
#include "tie_runtime/storage/storage.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { DIRECTSOUND_WAVE_HEADER_BYTES = 90 };

static IDirectSound* direct_sound;
static IDirectSoundBuffer* primary_buffer;

typedef struct WaveFormat {
	DSWaveFormat pcm;
	uint32_t data_offset;
	uint32_t data_size;
} WaveFormat;

// GLOBAL: TIE98 0x4EBA90
static const int32_t g_directSoundVolumeTable[128] = {
	-10000, -6000, -5415, -5000, -4678, -4415, -4192, -4000, -3830, -3678, -3540, -3415, -3299, -3192, -3093,
	-3000,  -2912, -2830, -2752, -2678, -2607, -2540, -2476, -2415, -2356, -2299, -2245, -2192, -2142, -2093,
	-2045,  -2000, -1955, -1912, -1870, -1830, -1790, -1752, -1714, -1678, -1642, -1607, -1573, -1540, -1508,
	-1476,  -1445, -1415, -1385, -1356, -1327, -1299, -1272, -1245, -1218, -1192, -1167, -1142, -1117, -1093,
	-1069,  -1045, -1022, -1000, -977,  -955,  -933,  -912,  -891,  -870,  -850,  -830,  -810,  -790,  -771,
	-752,   -733,  -714,  -696,  -678,  -660,  -642,  -624,  -607,  -590,  -573,  -557,  -540,  -524,  -508,
	-492,   -476,  -460,  -445,  -430,  -415,  -400,  -385,  -370,  -356,  -341,  -327,  -313,  -299,  -285,
	-272,   -258,  -245,  -231,  -218,  -205,  -192,  -179,  -167,  -154,  -142,  -129,  -117,  -105,  -93,
	-81,    -69,   -57,   -45,   -34,   -22,   -11,   0,
};

static uint16_t read_u16(const uint8_t* p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }

static uint32_t read_u32(const uint8_t* p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Parses a bounded RIFF prefix; sample data may extend beyond the supplied bytes. */
static int parse_wave_prefix(const uint8_t* bytes, size_t size, WaveFormat* format) {
	size_t offset = 12;
	int have_format = 0;
	if (!bytes || !format || size < 12 || memcmp(bytes, "RIFF", 4) || memcmp(bytes + 8, "WAVE", 4))
		return 0;
	uint64_t riff_end = (uint64_t)read_u32(bytes + 4) + 8;
	if (riff_end < 12)
		return 0;
	memset(format, 0, sizeof *format);
	while (offset <= size && size - offset >= 8) {
		uint32_t chunk_size = read_u32(bytes + offset + 4);
		size_t payload = offset + 8;
		if (payload > riff_end || chunk_size > riff_end - payload)
			return 0;
		if (!memcmp(bytes + offset, "fmt ", 4)) {
			if (chunk_size < 16 || chunk_size > size - payload || read_u16(bytes + payload) != 1)
				return 0;
			DSWaveFormat* pcm = &format->pcm;
			pcm->wFormatTag = 1;
			pcm->nChannels = read_u16(bytes + payload + 2);
			pcm->nSamplesPerSec = read_u32(bytes + payload + 4);
			pcm->nAvgBytesPerSec = read_u32(bytes + payload + 8);
			pcm->nBlockAlign = read_u16(bytes + payload + 12);
			pcm->wBitsPerSample = read_u16(bytes + payload + 14);
			if ((pcm->nChannels != 1 && pcm->nChannels != 2) ||
				(pcm->wBitsPerSample != 8 && pcm->wBitsPerSample != 16) || !pcm->nSamplesPerSec ||
				pcm->nSamplesPerSec > INT_MAX ||
				pcm->nBlockAlign != pcm->nChannels * (pcm->wBitsPerSample / 8))
				return 0;
			have_format = 1;
		} else if (!memcmp(bytes + offset, "data", 4)) {
			if (!have_format || payload > UINT32_MAX)
				return 0;
			format->data_offset = (uint32_t)payload;
			format->data_size = chunk_size;
			return 1;
		}
		if (chunk_size > size - payload)
			return 0;
		uint64_t next = (uint64_t)payload + chunk_size + (chunk_size & 1u);
		if (next > size)
			return 0;
		offset = (size_t)next;
	}
	return 0;
}

int TieDirectSound_Init(void* window) {
	if (direct_sound)
		return 1;
	void* device = NULL;
	if (DirectSoundCreate(NULL, &device, NULL) != 0)
		return 0;
	direct_sound = (IDirectSound*)device;
	if (direct_sound->lpVtbl->SetCooperativeLevel(direct_sound, window, DSSCL_PRIORITY) != 0) {
		TieDirectSound_Shutdown();
		return 0;
	}
	DSBufferDesc desc = { 0 };
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

// FUNCTION: TIE98 0x419310
int DirectSound_CreateWaveBuffer(IDirectSoundBuffer** out_buffer, uint32_t buffer_bytes, DSWaveFormat* format,
								 int alternate_capabilities) {
	if (!out_buffer)
		return -1;
	*out_buffer = NULL;
	if (!direct_sound)
		return -1;
	DSBufferDesc desc = { 0 };
	DSWaveFormat default_format = { 1, 1, 22050, 22050, 1, 8, 0 };
	desc.dwSize = 20;
	desc.dwFlags = alternate_capabilities ? 194u : 234u;
	desc.dwBufferBytes = buffer_bytes;
	desc.lpwfxFormat = format ? format : &default_format;
	int result = direct_sound->lpVtbl->CreateSoundBuffer(direct_sound, &desc, out_buffer, NULL);
	if (result < 0)
		*out_buffer = NULL;
	return result;
}

// FUNCTION: TIE98 0x418E30
int DirectSound_CopyWaveDataToBuffer(IDirectSoundBuffer* buffer, const void* samples, uint32_t bytes) {
	void* first;
	void* second;
	uint32_t first_bytes;
	uint32_t second_bytes;
	if (!buffer || !samples || !bytes)
		return 0;
	if (buffer->lpVtbl->Lock(buffer, 0, bytes, &first, &first_bytes, &second, &second_bytes, 0) < 0)
		return 0;
	memcpy(first, samples, first_bytes);
	if (second_bytes)
		memcpy(second, (const uint8_t*)samples + first_bytes, second_bytes);
	buffer->lpVtbl->Unlock(buffer, first, first_bytes, second, second_bytes);
	return 1;
}

// FUNCTION: TIE98 0x4189D0
IDirectSoundBuffer* DirectSound_LoadWaveBuffer(IDirectSound* device, const char* path,
											   int alternate_capabilities) {
	if (!device || !path)
		return NULL;
	TieFile* file = TieStorage_Open(TIE_FILE_ROOT_TIE98_MEDIA, path, "rb");
	if (!file)
		return NULL;
	IDirectSoundBuffer* buffer = NULL;
	uint8_t* bytes = NULL;
	long file_size;
	WaveFormat format;
	if (TieStorage_Seek(file, 0, TIE_SEEK_END) != 0 || (file_size = TieStorage_Tell(file)) < 12 ||
		(uint64_t)file_size > UINT32_MAX || TieStorage_Seek(file, 0, TIE_SEEK_SET) != 0)
		goto done;
	bytes = (uint8_t*)malloc((size_t)file_size);
	if (!bytes || TieStorage_Read(bytes, 1, (size_t)file_size, file) != (size_t)file_size ||
		!parse_wave_prefix(bytes, (size_t)file_size, &format) ||
		(uint64_t)read_u32(bytes + 4) + 8 > (uint64_t)file_size ||
		format.data_size > (size_t)file_size - format.data_offset)
		goto done;
	DSBufferDesc desc = { 0 };
	desc.dwSize = 20;
	desc.dwFlags = alternate_capabilities ? 194u : 234u;
	desc.dwBufferBytes = format.data_size;
	desc.lpwfxFormat = &format.pcm;
	if (device->lpVtbl->CreateSoundBuffer(device, &desc, &buffer, NULL) < 0) {
		buffer = NULL;
	} else if (!DirectSound_CopyWaveDataToBuffer(buffer, bytes + format.data_offset, format.data_size)) {
		DirectSound_ReleaseBuffer(&buffer);
	}
done:
	free(bytes);
	TieStorage_Close(file);
	return buffer;
}

// FUNCTION: TIE98 0x419800
IDirectSoundBuffer* DirectSound_LoadWaveBufferIntoPtr(IDirectSoundBuffer** out_buffer, const char* path,
													  int alternate_capabilities) {
	if (!out_buffer)
		return NULL;
	*out_buffer = DirectSound_LoadWaveBuffer(direct_sound, path, alternate_capabilities);
	return *out_buffer;
}

// FUNCTION: TIE98 0x419690
int DirectSound_CreateStreamingWaveBuffer(IDirectSoundBuffer** out_buffer, uint32_t buffer_bytes,
										  uint32_t* data_offset, int file_stream_channel) {
	if (!out_buffer)
		return -1;
	*out_buffer = NULL;
	uint8_t header[DIRECTSOUND_WAVE_HEADER_BYTES];
	WaveFormat format;
	int got;
	do {
		got = FrontendFileStream_ReadBytes(file_stream_channel, header, 0, sizeof header, 1);
	} while (got == -1);
	if (got != (int)sizeof header || !parse_wave_prefix(header, sizeof header, &format))
		return -1;
	DirectSound_CreateWaveBuffer(out_buffer, buffer_bytes, &format.pcm, 0);
	if (!*out_buffer)
		return -1;
	if (data_offset)
		*data_offset = format.data_offset;
	uint32_t initial_bytes = (uint32_t)sizeof header - format.data_offset;
	if (!initial_bytes)
		return 0;
	void* first;
	void* second;
	uint32_t first_bytes;
	uint32_t second_bytes;
	if ((*out_buffer)
			->lpVtbl->Lock(*out_buffer, 0, initial_bytes, &first, &first_bytes, &second, &second_bytes, 0) <
		0)
		return -1;
	uint32_t copy_bytes = first_bytes < initial_bytes ? first_bytes : initial_bytes;
	/* The original copies the locked region into the temporary WAV prefix. */
	memcpy(header + format.data_offset, first, copy_bytes);
	(*out_buffer)->lpVtbl->Unlock(*out_buffer, first, copy_bytes, second, 0);
	return (int)initial_bytes;
}

// FUNCTION: TIE98 0x419450
int DirectSound_LockBuffer(IDirectSoundBuffer* buffer, uint32_t offset, uint32_t bytes, void** first,
						   uint32_t* first_bytes, void** second, uint32_t* second_bytes) {
	return buffer &&
		   buffer->lpVtbl->Lock(buffer, offset, bytes, first, first_bytes, second, second_bytes, 0) >= 0;
}

// FUNCTION: TIE98 0x419490
int DirectSound_UnlockBuffer(IDirectSoundBuffer* buffer, void* first, uint32_t first_bytes, void* second,
							 uint32_t second_bytes) {
	return buffer ? buffer->lpVtbl->Unlock(buffer, first, first_bytes, second, second_bytes) : 0;
}

// FUNCTION: TIE98 0x485550
static int DirectSound_VolumeToMillibels(int volume) {
	if (volume > 127)
		volume = 127;
	if (volume < 0)
		volume = 0;
	return g_directSoundVolumeTable[volume];
}

// FUNCTION: TIE98 0x4193F0
void DirectSound_PlayBuffer(IDirectSoundBuffer* buffer, uint32_t cursor, int looping, int volume) {
	if (!buffer)
		return;
	buffer->lpVtbl->SetCurrentPosition(buffer, cursor);
	buffer->lpVtbl->SetVolume(buffer, DirectSound_VolumeToMillibels(volume));
	buffer->lpVtbl->SetPan(buffer, 0);
	buffer->lpVtbl->Play(buffer, 0, 0, looping ? DSBPLAY_LOOPING : 0);
}

// FUNCTION: TIE98 0x419440
int DirectSound_StopBuffer(IDirectSoundBuffer* buffer) { return buffer ? buffer->lpVtbl->Stop(buffer) : 0; }

// FUNCTION: TIE98 0x4194C0
uint32_t DirectSound_GetPlayCursor(IDirectSoundBuffer* buffer) {
	uint32_t play = 0;
	uint32_t write;
	if (buffer)
		buffer->lpVtbl->GetCurrentPosition(buffer, &play, &write);
	return play;
}

// FUNCTION: TIE98 0x4193D0
void DirectSound_ReleaseBuffer(IDirectSoundBuffer** buffer) {
	if (buffer && *buffer) {
		(*buffer)->lpVtbl->Release(*buffer);
		*buffer = NULL;
	}
}
