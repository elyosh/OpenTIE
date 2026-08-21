#ifndef LIBIMUSE_INTERNAL_FM4_DRIVER_H
#define LIBIMUSE_INTERNAL_FM4_DRIVER_H

#include "fm4_data.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IM_FM4_INVALID_VOICE UINT8_MAX

typedef void (*ImFm4WriteRegFunc)(void* user, uint16_t reg, uint8_t value);

typedef enum ImFm4ToneField {
	IM_FM4_TONE_CARRIER_LEVEL = 0,
	IM_FM4_TONE_CARRIER_VELOCITY_SENSITIVITY = 1,
	IM_FM4_TONE_MODULATOR_LEVEL = 13,
	IM_FM4_TONE_MODULATOR_VELOCITY_SENSITIVITY = 14,
	IM_FM4_TONE_CONNECTION = 26,
	IM_FM4_TONE_FEEDBACK = 27,
	IM_FM4_TONE_DURATION = 68,
} ImFm4ToneField;

typedef struct ImFm4ChannelState {
	int32_t priority;
	int32_t noteRequest;
	int32_t voiceCount;
	int32_t overflow;
	int32_t program;
	int32_t volume;
	int32_t pan;
	int32_t pitchBend;
} ImFm4ChannelState;

typedef struct ImFm4VoiceState {
	int32_t midiChannel;
	int32_t note;
	int32_t algorithm[2];
	int32_t carrierBase[2];
	int32_t modulatorBase[2];
	int32_t duration[2];
} ImFm4VoiceState;

typedef struct ImFm4VoiceNode {
	uint8_t prev;
	uint8_t next;
	uint8_t midiChannel;
	uint8_t note;
	uint8_t voiceIndex;
} ImFm4VoiceNode;

typedef struct ImFm4Driver {
	ImFm4ChannelState channels[IM_FM4_CHANNEL_COUNT];
	ImFm4VoiceState voices[IM_FM4_VOICE_COUNT];
	ImFm4VoiceNode voiceNodes[IM_FM4_VOICE_COUNT];
	uint8_t freeHead;
	uint8_t activeHead;
	ImFm4ToneRecord tones[2][IM_FM4_TONE_COUNT];
	uint8_t registers[2][256];
	ImFm4WriteRegFunc writeReg;
	void* writeUser;
} ImFm4Driver;

void im_fm4_driver_init(ImFm4Driver* driver, ImFm4WriteRegFunc writeReg, void* writeUser);
void im_fm4_driver_deinit(ImFm4Driver* driver);
void im_fm4_driver_program_change(ImFm4Driver* driver, int channel, int program);
int im_fm4_driver_note_on(ImFm4Driver* driver, int channel, int note, int velocity);
bool im_fm4_driver_note_off(ImFm4Driver* driver, int channel, int note);
void im_fm4_driver_control_change(ImFm4Driver* driver, int channel, unsigned int controller, int value);
void im_fm4_driver_pitch_bend(ImFm4Driver* driver, int channel, int pitchBend14);
bool im_fm4_driver_apply_patch_sysex(ImFm4Driver* driver, const uint8_t* message, size_t size);
void im_fm4_driver_tick_voice(ImFm4Driver* driver, int voiceIndex);

#endif /* LIBIMUSE_INTERNAL_FM4_DRIVER_H */
