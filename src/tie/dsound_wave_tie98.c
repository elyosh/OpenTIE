#include "tie/dsound_wave_tie98.h"

#include "tie/filestream_tie98.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include "aeron/audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { DIRECTSOUND_WAVE_HEADER_BYTES = 90 };

typedef struct WaveFormat {
	int sample_rate;
	int channels;
	int bits;
	size_t data_offset;
	size_t data_size;
} WaveFormat;

struct DirectSoundWaveBuffer {
	AeronRing ring;
	size_t size;
	void* locked_first;
	void* locked_second;
	size_t locked_first_bytes;
	size_t locked_second_bytes;
};

static uint16_t read_u16(const uint8_t* p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t read_u32(const uint8_t* p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// FUNCTION: TIE98 0x418EF0
static int DirectSound_ParseWaveHeader(const uint8_t* bytes, size_t size, WaveFormat* format) {
	size_t offset = 12;
	int have_format = 0;
	if (!bytes || !format || size < 12 || memcmp(bytes, "RIFF", 4) || memcmp(bytes + 8, "WAVE", 4))
		return 0;
	memset(format, 0, sizeof *format);
	while (offset + 8 <= size) {
		const uint32_t chunk_size = read_u32(bytes + offset + 4);
		const size_t payload = offset + 8;
		if (!memcmp(bytes + offset, "fmt ", 4)) {
			if (chunk_size < 16 || payload + 16 > size || read_u16(bytes + payload) != 1)
				return 0;
			format->channels = read_u16(bytes + payload + 2);
			format->sample_rate = (int)read_u32(bytes + payload + 4);
			format->bits = read_u16(bytes + payload + 14);
			have_format = 1;
		} else if (!memcmp(bytes + offset, "data", 4)) {
			if (!have_format)
				return 0;
			format->data_offset = payload;
			format->data_size = chunk_size;
			return (format->channels == 1 || format->channels == 2) &&
				   (format->bits == 8 || format->bits == 16) && format->sample_rate > 0;
		}
		if ((size_t)chunk_size > SIZE_MAX - payload)
			return 0;
		offset = payload + ((size_t)chunk_size + 1u & ~(size_t)1u);
	}
	return 0;
}

static DirectSoundWaveBuffer* DirectSound_CreateBuffer(const WaveFormat* format, size_t size) {
	DirectSoundWaveBuffer* buffer;
	if (!format || size == 0 || size % ((size_t)format->channels * (size_t)(format->bits / 8)) != 0)
		return NULL;
	buffer = (DirectSoundWaveBuffer*)calloc(1, sizeof *buffer);
	if (!buffer)
		return NULL;
	buffer->ring = Aeron_AudioRingOpen(format->sample_rate, format->channels, format->bits, size, 1.0f);
	if (!buffer->ring) {
		free(buffer);
		return NULL;
	}
	buffer->size = size;
	return buffer;
}

// FUNCTION: TIE98 0x419690
int DirectSound_CreateStreamingWaveBuffer(DirectSoundWaveBuffer** out_buffer, size_t buffer_bytes,
										  size_t* data_offset, int file_stream_channel) {
	uint8_t header[DIRECTSOUND_WAVE_HEADER_BYTES];
	WaveFormat format;
	DirectSoundWaveBuffer* buffer;
	int got;
	if (!out_buffer || !data_offset || buffer_bytes == 0)
		return -1;
	do {
		got = FrontendFileStream_ReadBytes(file_stream_channel, header, 0, sizeof header, 1);
	} while (got == -1);
	if (got != (int)sizeof header || !DirectSound_ParseWaveHeader(header, (size_t)got, &format))
		return -1;
	buffer = DirectSound_CreateBuffer(&format, buffer_bytes);
	if (!buffer)
		return -1;
	*data_offset = format.data_offset;
	size_t initial_data_bytes = (size_t)got > format.data_offset ? (size_t)got - format.data_offset : 0;
	if (initial_data_bytes > format.data_size)
		initial_data_bytes = format.data_size;
	if (initial_data_bytes)
		Aeron_AudioRingWrite(buffer->ring, 0, header + format.data_offset, initial_data_bytes);
	*out_buffer = buffer;
	return (int)initial_data_bytes;
}

// FUNCTION: TIE98 0x419800
DirectSoundWaveBuffer* DirectSound_CreateStaticBufferFromWaveFile(const char* path) {
	TieFile* file;
	uint8_t* bytes;
	long file_size;
	WaveFormat format;
	DirectSoundWaveBuffer* buffer = NULL;
	if (!path)
		return NULL;
	file = TieStorage_Open(TIE_FILE_ROOT_TIE98_MEDIA, path, "rb");
	if (!file)
		return NULL;
	if (TieStorage_Seek(file, 0, TIE_SEEK_END) != 0 || (file_size = TieStorage_Tell(file)) <= 0 ||
		TieStorage_Seek(file, 0, TIE_SEEK_SET) != 0) {
		TieStorage_Close(file);
		return NULL;
	}
	bytes = (uint8_t*)malloc((size_t)file_size);
	if (!bytes || TieStorage_Read(bytes, 1, (size_t)file_size, file) != (size_t)file_size)
		goto done;
	if (!DirectSound_ParseWaveHeader(bytes, (size_t)file_size, &format) ||
		format.data_offset > (size_t)file_size || format.data_size > (size_t)file_size - format.data_offset)
		goto done;
	buffer = DirectSound_CreateBuffer(&format, format.data_size);
	if (buffer)
		Aeron_AudioRingWrite(buffer->ring, 0, bytes + format.data_offset, format.data_size);
done:
	free(bytes);
	TieStorage_Close(file);
	return buffer;
}

// FUNCTION: TIE98 0x419450
int DirectSound_LockBuffer(DirectSoundWaveBuffer* buffer, size_t offset, size_t bytes, void** first,
						   size_t* first_bytes, void** second, size_t* second_bytes) {
	uint8_t* base;
	if (!buffer || !first || !first_bytes || !second || !second_bytes || offset >= buffer->size ||
		bytes > buffer->size)
		return 0;
	base = (uint8_t*)Aeron_AudioRingBase(buffer->ring);
	if (!base)
		return 0;
	*first = base + offset;
	*first_bytes = bytes < buffer->size - offset ? bytes : buffer->size - offset;
	*second = base;
	*second_bytes = bytes - *first_bytes;
	buffer->locked_first = *first;
	buffer->locked_second = *second;
	buffer->locked_first_bytes = *first_bytes;
	buffer->locked_second_bytes = *second_bytes;
	return 1;
}

// FUNCTION: TIE98 0x419490
int DirectSound_UnlockBuffer(DirectSoundWaveBuffer* buffer, void* first, size_t first_bytes, void* second,
							 size_t second_bytes) {
	if (!buffer || first != buffer->locked_first || second != buffer->locked_second ||
		first_bytes != buffer->locked_first_bytes || second_bytes != buffer->locked_second_bytes)
		return 0;
	buffer->locked_first = buffer->locked_second = NULL;
	buffer->locked_first_bytes = buffer->locked_second_bytes = 0;
	return 1;
}

// FUNCTION: TIE98 0x4193F0
void DirectSound_PlayBuffer(DirectSoundWaveBuffer* buffer, size_t cursor, int looping, int volume) {
	if (!buffer)
		return;
	if (volume < 0)
		volume = 0;
	if (volume > 127)
		volume = 127;
	if (!Aeron_AudioRingSetPlayCursorBytes(buffer->ring, cursor))
		return;
	const float millibels =
		volume == 0 ? -10000.0f : lroundf(1000.0f * log2f(((float)volume + 1.0f) / 128.0f));
	const float gain = millibels <= -10000.0f ? 0.0f : powf(10.0f, millibels / 2000.0f);
	Aeron_AudioRingSetGain(buffer->ring, gain);
	Aeron_AudioRingPlay(buffer->ring, looping);
}

// FUNCTION: TIE98 0x419440
void DirectSound_StopBuffer(DirectSoundWaveBuffer* buffer) {
	if (buffer)
		Aeron_AudioRingStop(buffer->ring);
}

// FUNCTION: TIE98 0x4194C0
size_t DirectSound_GetPlayCursor(DirectSoundWaveBuffer* buffer) {
	return buffer ? Aeron_AudioRingPlayCursorBytes(buffer->ring) : 0;
}

int DirectSound_IsBufferPlaying(DirectSoundWaveBuffer* buffer) {
	return buffer && Aeron_AudioRingIsPlaying(buffer->ring);
}

void DirectSound_ReleaseBuffer(DirectSoundWaveBuffer* buffer) {
	if (!buffer)
		return;
	Aeron_AudioRingClose(buffer->ring);
	free(buffer);
}

size_t DirectSound_BufferSize(const DirectSoundWaveBuffer* buffer) { return buffer ? buffer->size : 0; }
