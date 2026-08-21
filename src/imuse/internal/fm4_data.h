#ifndef LIBIMUSE_INTERNAL_FM4_DATA_H
#define LIBIMUSE_INTERNAL_FM4_DATA_H

#include <stdint.h>

enum {
	IM_FM4_CHANNEL_COUNT = 16,
	IM_FM4_VOICE_COUNT = 9,
	IM_FM4_TONE_COUNT = 167,
	IM_FM4_PERCUSSION_NOTE_COUNT = 128,
	IM_FM4_TONE_FIELD_COUNT = 28,
	IM_FM4_NOTE_TABLE_COUNT = 128,
	IM_FM4_FNUMBER_TABLE_COUNT = 192,
};

typedef struct ImFm4ToneRecord {
	uint8_t packed[11];
	uint8_t duration;
} ImFm4ToneRecord;

typedef struct ImFm4ToneFieldDesc {
	uint32_t byteOffset;
	uint32_t shift;
	uint32_t mask;
} ImFm4ToneFieldDesc;

_Static_assert(sizeof(ImFm4ToneRecord) == 12, "FM4 tone records must retain the recovered 12-byte layout");
_Static_assert(sizeof(ImFm4ToneFieldDesc) == 12, "FM4 field descriptors must retain the recovered layout");

extern const int32_t im_fm4_attenuation[64];
extern const ImFm4ToneRecord im_fm4_default_tones[2][IM_FM4_TONE_COUNT];
extern const uint8_t im_fm4_percussion_tones[IM_FM4_PERCUSSION_NOTE_COUNT];
extern const int32_t im_fm4_percussion_pitch[IM_FM4_PERCUSSION_NOTE_COUNT];
extern const ImFm4ToneFieldDesc im_fm4_tone_fields[IM_FM4_TONE_FIELD_COUNT];
extern const uint8_t im_fm4_modulator_offsets[IM_FM4_VOICE_COUNT];
extern const uint8_t im_fm4_carrier_offsets[IM_FM4_VOICE_COUNT];
extern const uint8_t im_fm4_note_octaves[IM_FM4_NOTE_TABLE_COUNT];
extern const uint32_t im_fm4_f_numbers[IM_FM4_FNUMBER_TABLE_COUNT];
extern const uint8_t im_fm4_note_fnumber_rows[IM_FM4_NOTE_TABLE_COUNT];

#endif /* LIBIMUSE_INTERNAL_FM4_DATA_H */
