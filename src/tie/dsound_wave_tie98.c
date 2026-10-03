#include "tie/dsound_wave_tie98.h"
#include "tie/filestream_tie98.h"
#include "tie_runtime/storage/storage.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { DIRECTSOUND_WAVE_HEADER_BYTES = 90 };

// GLOBAL: TIE98 0x5A2784
IDirectSound* direct_sound;
// GLOBAL: TIE98 0x5A2788
IDirectSoundBuffer* primary_buffer;

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

static int DirectSound_VolumeToMillibels(int volume);

// FUNCTION: TIE98 0x4189D0
IDirectSoundBuffer* DirectSound_LoadWaveFile(IDirectSound* device, const char* path,
											 int alternate_capabilities) {
	DSBufferDesc desc = { 0 };
	TieFile* file;
	IDirectSoundBuffer* buffer;
	uint8_t* bytes;
	long file_size;
	DSWaveFormat* format;
	uint8_t* data;
	uint32_t data_size;

	if (!device || !path)
		return NULL;
	file = TieStorage_Open(TIE_FILE_ROOT_TIE98_MEDIA, path, "rb");
	if (!file)
		return NULL;
	buffer = NULL;
	bytes = NULL;

	if (TieStorage_Seek(file, 0, TIE_SEEK_END) == 0 && (file_size = TieStorage_Tell(file)) >= 12 &&
		(uint64_t)file_size <= UINT32_MAX && TieStorage_Seek(file, 0, TIE_SEEK_SET) == 0) {
		bytes = (uint8_t*)malloc((size_t)file_size);
		if (bytes && TieStorage_Read(bytes, 1, (size_t)file_size, file) == (size_t)file_size &&
			(uint64_t)((const uint32_t*)bytes)[1] + 8 <= (uint64_t)file_size &&
			DirectSound_ParseWaveHeader(bytes, &format, &data, &data_size) &&
			data_size <= (size_t)(bytes + file_size - data)) {
			desc.dwSize = 20;
			desc.dwFlags = alternate_capabilities ? 194u : 234u;
			desc.dwBufferBytes = data_size;
			desc.lpwfxFormat = format;
			if (device->lpVtbl->CreateSoundBuffer(device, &desc, &buffer, NULL) < 0) {
				buffer = NULL;
			} else if (!DirectSound_CopyWaveDataToBuffer(buffer, data, data_size)) {
				DirectSound_ReleaseBuffer(&buffer);
			}
		}
	}
	free(bytes);
	TieStorage_Close(file);
	return buffer;
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

// FUNCTION: TIE98 0x418EF0
int DirectSound_ParseWaveHeader(const void* riff_data, DSWaveFormat** out_format, uint8_t** out_data,
								uint32_t* out_data_size) {
	const uint8_t* chunk;
	const uint8_t* riff_end;
	uint32_t chunk_id;
	uint32_t chunk_size;
	uint8_t* payload;

	if (out_format)
		*out_format = NULL;
	if (out_data)
		*out_data = NULL;
	if (out_data_size)
		*out_data_size = 0;
	chunk = (const uint8_t*)riff_data + 12;
	if (((const uint32_t*)riff_data)[0] != 0x46464952u || ((const uint32_t*)riff_data)[2] != 0x45564157u)
		return 0;
	riff_end = chunk + ((const uint32_t*)riff_data)[1] - 4;
	if (chunk >= riff_end)
		return 0;
	for (;;) {
		chunk_id = ((const uint32_t*)chunk)[0];
		chunk_size = ((const uint32_t*)chunk)[1];
		payload = (uint8_t*)chunk + 8;
		if (chunk_id == 0x20746D66u) {
			if (out_format && !*out_format) {
				if (chunk_size < 14)
					return 0;
				*out_format = (DSWaveFormat*)payload;
				if ((!out_data || *out_data) && (!out_data_size || *out_data_size))
					return 1;
			}
		} else if (chunk_id == 0x61746164u &&
				   ((out_data && !*out_data) || (out_data_size && !*out_data_size))) {
			if (out_data)
				*out_data = payload;
			if (out_data_size)
				*out_data_size = chunk_size;
			if (!out_format || *out_format)
				return 1;
		}
		chunk = payload + ((chunk_size + 1) & ~1u);
		if (chunk >= riff_end)
			return 0;
	}
}

// FUNCTION: TIE98 0x419310
int DirectSound_CreateWaveBuffer(IDirectSoundBuffer** out_buffer, uint32_t buffer_bytes, DSWaveFormat* format,
								 int alternate_capabilities) {
	DSBufferDesc desc = { 0 };
	DSWaveFormat default_format = { 1, 1, 22050, 22050, 1, 8, 0 };
	int result;

	if (!out_buffer)
		return -1;
	*out_buffer = NULL;
	if (!direct_sound)
		return -1;
	desc.dwSize = 20;
	desc.dwFlags = alternate_capabilities ? 194u : 234u;
	desc.dwBufferBytes = buffer_bytes;
	desc.lpwfxFormat = format ? format : &default_format;
	result = direct_sound->lpVtbl->CreateSoundBuffer(direct_sound, &desc, out_buffer, NULL);
	if (result < 0)
		*out_buffer = NULL;
	return result;
}

// FUNCTION: TIE98 0x4193D0
void DirectSound_ReleaseBuffer(IDirectSoundBuffer** buffer) {
	if (buffer && *buffer) {
		(*buffer)->lpVtbl->Release(*buffer);
		*buffer = NULL;
	}
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

// FUNCTION: TIE98 0x4194C0
uint32_t DirectSound_GetPlayCursor(IDirectSoundBuffer* buffer) {
	uint32_t play = 0;
	uint32_t write;
	if (buffer)
		buffer->lpVtbl->GetCurrentPosition(buffer, &play, &write);
	return play;
}

// FUNCTION: TIE98 0x419690
int DirectSound_CreateStreamingWaveBuffer(IDirectSoundBuffer** out_buffer, uint32_t buffer_bytes,
										  uint32_t* data_offset, int file_stream_channel) {
	uint8_t header[DIRECTSOUND_WAVE_HEADER_BYTES];
	DSWaveFormat* format;
	uint8_t* data;
	uint32_t header_data_offset;
	int got;
	uint32_t initial_bytes;
	void* first;
	void* second;
	uint32_t first_bytes;
	uint32_t second_bytes;
	uint32_t copy_bytes;

	if (!out_buffer)
		return -1;
	*out_buffer = NULL;
	do {
		got = FrontendFileStream_ReadBytes(file_stream_channel, header, 0, sizeof header, 1);
	} while (got == -1);
	if (got != (int)sizeof header || !DirectSound_ParseWaveHeader(header, &format, &data, NULL) ||
		data > header + sizeof header)
		return -1;
	header_data_offset = (uint32_t)(data - header);
	DirectSound_CreateWaveBuffer(out_buffer, buffer_bytes, format, 0);
	if (!*out_buffer)
		return -1;
	if (data_offset)
		*data_offset = header_data_offset;
	initial_bytes = (uint32_t)sizeof header - header_data_offset;
	if (!initial_bytes)
		return 0;

	if ((*out_buffer)
			->lpVtbl->Lock(*out_buffer, 0, initial_bytes, &first, &first_bytes, &second, &second_bytes, 0) <
		0)
		return -1;
	copy_bytes = first_bytes < initial_bytes ? first_bytes : initial_bytes;
	/* The original copies the locked region into the temporary WAV prefix. */
	memcpy(data, first, copy_bytes);
	(*out_buffer)->lpVtbl->Unlock(*out_buffer, first, copy_bytes, second, 0);
	return (int)initial_bytes;
}

// FUNCTION: TIE98 0x419800
IDirectSoundBuffer* DirectSound_CreateStaticBufferFromWaveFile(IDirectSoundBuffer** out_buffer,
															   const char* path, int alternate_capabilities) {
	if (!out_buffer)
		return NULL;
	*out_buffer = DirectSound_LoadWaveFile(direct_sound, path, alternate_capabilities);
	return *out_buffer;
}

// FUNCTION: TIE98 0x485550
static int DirectSound_VolumeToMillibels(int volume) {
	if (volume > 127)
		volume = 127;
	if (volume < 0)
		volume = 0;
	return g_directSoundVolumeTable[volume];
}
