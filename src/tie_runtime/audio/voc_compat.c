#include "tie_runtime/audio/voc_compat.h"

#include "util/binio.h"

#include <string.h>

enum {
	VOC_FILE_HEADER_SIZE = 26,
	VOC_TIE95_VERSION = 0x010A,
	VOC_TIE98_VERSION = 0x0114,
	VOC_TYPE1_HEADER_SIZE = 6,
	VOC_TYPE9_FORMAT_SIZE = 12,
	VOC_TYPE9_HEADER_SIZE = 4 + VOC_TYPE9_FORMAT_SIZE,
	VOC_MAX_BLOCK_SIZE = 0xFFFFFF,
};

static uint32_t voc_read_u24le(const uint8_t* data) {
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
}

static void voc_write_u24le(uint8_t* data, uint32_t value) {
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
}

static uint16_t voc_version_checksum(uint16_t version) { return (uint16_t)(~version + 0x1234u); }

TieVocCompatResult TieVocCompat_PrepareImuse(uint8_t* data, size_t* size, uint32_t* source_rate_hz) {
	static const char signature[] = "Creative Voice File\x1A";
	if (!data || !size || !source_rate_hz)
		return TIE_VOC_COMPAT_INVALID;
	*source_rate_hz = 0;
	if (*size < VOC_FILE_HEADER_SIZE || memcmp(data, signature, sizeof signature - 1) != 0)
		return TIE_VOC_COMPAT_NOT_NEEDED;

	const uint16_t version = br_u16le(data + 22);
	if (version != VOC_TIE98_VERSION)
		return TIE_VOC_COMPAT_NOT_NEEDED;
	if (br_u16le(data + 24) != voc_version_checksum(version))
		return TIE_VOC_COMPAT_INVALID;

	const size_t block_offset = br_u16le(data + 20);
	if (block_offset < VOC_FILE_HEADER_SIZE || block_offset > *size ||
		*size - block_offset < VOC_TYPE9_HEADER_SIZE + 1 || data[block_offset] != 9)
		return TIE_VOC_COMPAT_INVALID;

	const uint32_t block_size = voc_read_u24le(data + block_offset + 1);
	if (block_size < VOC_TYPE9_FORMAT_SIZE || block_size > *size - block_offset - 4)
		return TIE_VOC_COMPAT_INVALID;
	const size_t block_end = block_offset + 4 + block_size;
	if (block_end >= *size || data[block_end] != 0)
		return TIE_VOC_COMPAT_INVALID;

	const uint8_t* format = data + block_offset + 4;
	const uint32_t rate = br_u32le(format);
	const uint8_t bits_per_sample = format[4];
	const uint8_t channels = format[5];
	const uint16_t codec = br_u16le(format + 6);
	if (!rate || rate > INT32_MAX || bits_per_sample != 8 || channels != 1 || codec != 0)
		return TIE_VOC_COMPAT_INVALID;

	const uint32_t period = (1000000u + rate / 2u) / rate;
	const size_t pcm_size = block_size - VOC_TYPE9_FORMAT_SIZE;
	if (!period || period >= 256u || pcm_size > VOC_MAX_BLOCK_SIZE - 2u)
		return TIE_VOC_COMPAT_INVALID;

	const size_t pcm_offset = block_offset + VOC_TYPE9_HEADER_SIZE;
	const size_t output_pcm_offset = VOC_FILE_HEADER_SIZE + VOC_TYPE1_HEADER_SIZE;
	memmove(data + output_pcm_offset, data + pcm_offset, pcm_size);

	bw_u16le(data + 20, VOC_FILE_HEADER_SIZE);
	bw_u16le(data + 22, VOC_TIE95_VERSION);
	bw_u16le(data + 24, voc_version_checksum(VOC_TIE95_VERSION));
	data[VOC_FILE_HEADER_SIZE] = 1;
	voc_write_u24le(data + VOC_FILE_HEADER_SIZE + 1, (uint32_t)pcm_size + 2u);
	data[VOC_FILE_HEADER_SIZE + 4] = (uint8_t)(256u - period);
	data[VOC_FILE_HEADER_SIZE + 5] = 0;
	data[output_pcm_offset + pcm_size] = 0;

	*size = output_pcm_offset + pcm_size + 1;
	*source_rate_hz = rate;
	return TIE_VOC_COMPAT_CONVERTED;
}
