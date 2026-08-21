#include "internal/fm4_driver.h"

#include <limits.h>
#include <string.h>

enum {
	FM4_BANK_0 = 0,
	FM4_BANK_1 = 1,
	FM4_CONTROLLER_VOLUME = 7,
	FM4_CONTROLLER_PAN = 10,
	FM4_CONTROLLER_NOTE_REQUEST = 17,
	FM4_CONTROLLER_PRIORITY = 18,
	FM4_PERCUSSION_CHANNEL = 9,
};

typedef struct ImFm4BankLevels {
	int carrierBase;
	int modulatorBase;
	int carrier;
	int modulator;
	int algorithm;
	int duration;
} ImFm4BankLevels;

static int clamp_int(int value, int minimum, int maximum) {
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

static bool valid_channel(int channel) { return channel >= 0 && channel < IM_FM4_CHANNEL_COUNT; }

/* Arithmetic right shift without relying on implementation-defined negative
 * signed shifts. Recovered pitch arithmetic only supplies small values. */
static int32_t shift_right_signed(int32_t value, unsigned int shift) {
	if (value >= 0)
		return value >> shift;
	return -(int32_t)(((uint32_t)(-value) + ((1u << shift) - 1u)) >> shift);
}

static void list_add(ImFm4Driver* driver, uint8_t* head, uint8_t index) {
	ImFm4VoiceNode* node = &driver->voiceNodes[index];
	node->prev = IM_FM4_INVALID_VOICE;
	node->next = *head;
	if (*head != IM_FM4_INVALID_VOICE)
		driver->voiceNodes[*head].prev = index;
	*head = index;
}

static void list_remove(ImFm4Driver* driver, uint8_t* head, uint8_t index) {
	ImFm4VoiceNode* node = &driver->voiceNodes[index];
	if (node->next != IM_FM4_INVALID_VOICE)
		driver->voiceNodes[node->next].prev = node->prev;
	if (node->prev != IM_FM4_INVALID_VOICE)
		driver->voiceNodes[node->prev].next = node->next;
	else
		*head = node->next;
	node->prev = IM_FM4_INVALID_VOICE;
	node->next = IM_FM4_INVALID_VOICE;
}

static uint8_t list_tail(const ImFm4Driver* driver, uint8_t head) {
	uint8_t index = head;
	while (index != IM_FM4_INVALID_VOICE && driver->voiceNodes[index].next != IM_FM4_INVALID_VOICE)
		index = driver->voiceNodes[index].next;
	return index;
}

static void write_raw(ImFm4Driver* driver, int bank, unsigned int reg, unsigned int value) {
	uint8_t byte = (uint8_t)value;
	driver->registers[bank][reg & 0xffu] = byte;
	if (driver->writeReg)
		driver->writeReg(driver->writeUser, (uint16_t)(bank * 0x100 + (reg & 0xffu)), byte);
}

static void write_cached(ImFm4Driver* driver, int bank, unsigned int reg, unsigned int value) {
	uint8_t byte = (uint8_t)value;
	reg &= 0xffu;
	if (driver->registers[bank][reg] != byte)
		write_raw(driver, bank, reg, byte);
}

static const ImFm4ToneRecord* get_tone(const ImFm4Driver* driver, int bank, int tone) {
	static const ImFm4ToneRecord silentTone;
	if (tone < 0 || tone >= IM_FM4_TONE_COUNT)
		return &silentTone;
	return &driver->tones[bank][tone];
}

static unsigned int get_tone_field(const ImFm4Driver* driver, int bank, int tone, ImFm4ToneField field) {
	if (tone < 0 || tone >= IM_FM4_TONE_COUNT)
		return 0;
	if ((unsigned int)field < IM_FM4_TONE_FIELD_COUNT) {
		const ImFm4ToneFieldDesc* desc = &im_fm4_tone_fields[field];
		return (driver->tones[bank][tone].packed[desc->byteOffset] & desc->mask) >> desc->shift;
	}
	if (field == IM_FM4_TONE_DURATION)
		return driver->tones[bank][tone].duration;
	return 0;
}

static void clear_hardware(ImFm4Driver* driver) {
	for (unsigned int reg = 0; reg < 256; ++reg) {
		write_raw(driver, FM4_BANK_0, reg, 0);
		write_raw(driver, FM4_BANK_1, reg, 0);
	}
}

static void init_hardware(ImFm4Driver* driver) {
	/* FM4_TonesInitHardware, 0x108fe. */
	clear_hardware(driver);
	write_cached(driver, FM4_BANK_0, 0x08, 0x40);
	write_cached(driver, FM4_BANK_0, 0xbd, 0x00);
	write_cached(driver, FM4_BANK_1, 0x05, 0x01);
}

static void program_bank_instrument(ImFm4Driver* driver, int bank, int voiceIndex,
									const ImFm4ToneRecord* tone) {
	unsigned int modulator = im_fm4_modulator_offsets[voiceIndex];
	unsigned int carrier = im_fm4_carrier_offsets[voiceIndex];
	unsigned int route = bank == FM4_BANK_0 ? 0x20u : 0x10u;

	write_cached(driver, bank, modulator + 0x40, 0x3f);
	write_cached(driver, bank, carrier + 0x40, 0x3f);
	write_cached(driver, bank, modulator + 0x20, tone->packed[0]);
	write_cached(driver, bank, modulator + 0x40, tone->packed[1] | 0x3f);
	write_cached(driver, bank, modulator + 0x60, (uint8_t)~tone->packed[2]);
	write_cached(driver, bank, modulator + 0x80, (uint8_t)~tone->packed[3]);
	write_cached(driver, bank, modulator + 0xe0, tone->packed[4]);
	write_cached(driver, bank, carrier + 0x20, tone->packed[5]);
	write_cached(driver, bank, carrier + 0x40, tone->packed[6] | 0x3f);
	write_cached(driver, bank, carrier + 0x60, (uint8_t)~tone->packed[7]);
	write_cached(driver, bank, carrier + 0x80, (uint8_t)~tone->packed[8]);
	write_cached(driver, bank, carrier + 0xe0, tone->packed[9]);
	write_cached(driver, bank, (unsigned int)voiceIndex + 0xc0, tone->packed[10] | route);
}

static void set_operator_level(ImFm4Driver* driver, int bank, int voiceIndex, ImFm4ToneField field,
							   int attenuation) {
	unsigned int offset;
	if (field == IM_FM4_TONE_CARRIER_LEVEL)
		offset = im_fm4_carrier_offsets[voiceIndex];
	else if (field == IM_FM4_TONE_MODULATOR_LEVEL)
		offset = im_fm4_modulator_offsets[voiceIndex];
	else
		return;
	unsigned int reg = offset + 0x40;
	write_cached(driver, bank, reg, (driver->registers[bank][reg] | 0x3f) - attenuation);
}

static void program_pitch(ImFm4Driver* driver, int voiceIndex, int note, int bendDelta) {
	/* FM4_TonesProgramPitch, 0x10cba. */
	int noteIndex = shift_right_signed(bendDelta, 8) + note - 7;
	noteIndex = clamp_int(noteIndex, 0, IM_FM4_NOTE_TABLE_COUNT - 1);
	unsigned int fine = (unsigned int)shift_right_signed(bendDelta, 4) & 0x0fu;
	unsigned int fIndex = im_fm4_note_fnumber_rows[noteIndex] + fine;
	if (fIndex >= IM_FM4_FNUMBER_TABLE_COUNT)
		fIndex = IM_FM4_FNUMBER_TABLE_COUNT - 1;
	unsigned int fNumber = im_fm4_f_numbers[fIndex];
	unsigned int b0 = (fNumber >> 8) + ((unsigned int)im_fm4_note_octaves[noteIndex] << 2) +
					  (driver->registers[FM4_BANK_0][voiceIndex + 0xb0] & 0x20u);

	write_cached(driver, FM4_BANK_0, voiceIndex + 0xa0, fNumber);
	write_cached(driver, FM4_BANK_0, voiceIndex + 0xb0, b0);
	write_cached(driver, FM4_BANK_1, voiceIndex + 0xa0, fNumber);
	write_cached(driver, FM4_BANK_1, voiceIndex + 0xb0, b0);
}

static void key_off_voice(ImFm4Driver* driver, int voiceIndex) {
	unsigned int reg = (unsigned int)voiceIndex + 0xb0;
	write_cached(driver, FM4_BANK_0, reg, driver->registers[FM4_BANK_0][reg] & 0xdfu);
	write_cached(driver, FM4_BANK_1, reg, driver->registers[FM4_BANK_1][reg] & 0xdfu);
}

static ImFm4BankLevels calculate_bank_levels(const ImFm4Driver* driver, int bank, int tone,
											 int velocityAttenuation, int volume) {
	ImFm4BankLevels levels;
	levels.algorithm = (int)get_tone_field(driver, bank, tone, IM_FM4_TONE_CONNECTION);
	levels.duration = 4 * (int)get_tone_field(driver, bank, tone, IM_FM4_TONE_DURATION) - 1;

	int carrierVelocity =
		((int)get_tone_field(driver, bank, tone, IM_FM4_TONE_CARRIER_VELOCITY_SENSITIVITY) + 1) *
			velocityAttenuation >>
		6;
	levels.carrierBase = clamp_int(
		carrierVelocity + (int)get_tone_field(driver, bank, tone, IM_FM4_TONE_CARRIER_LEVEL), 0, 63);

	int modulatorVelocity =
		((int)get_tone_field(driver, bank, tone, IM_FM4_TONE_MODULATOR_VELOCITY_SENSITIVITY) + 1) *
			velocityAttenuation >>
		6;
	levels.modulatorBase = clamp_int(
		modulatorVelocity + (int)get_tone_field(driver, bank, tone, IM_FM4_TONE_MODULATOR_LEVEL), 0, 63);

	levels.carrier = im_fm4_attenuation[(volume * (levels.carrierBase + 1)) >> 7];
	levels.modulator = levels.modulatorBase;
	if (levels.algorithm == 1)
		levels.modulator = im_fm4_attenuation[(volume * (levels.modulatorBase + 1)) >> 7];
	return levels;
}

static void start_voice(ImFm4Driver* driver, int voiceIndex, int channel, int note, int velocity, int tone,
						int volume, int pitchBend) {
	/* FM4_TonesStartVoice, 0x1040f. */
	ImFm4VoiceState* voice = &driver->voices[voiceIndex];
	int velocityAttenuation = 2 * im_fm4_attenuation[(unsigned int)velocity >> 1];
	ImFm4BankLevels bank0 = calculate_bank_levels(driver, FM4_BANK_0, tone, velocityAttenuation, volume);
	ImFm4BankLevels bank1 = calculate_bank_levels(driver, FM4_BANK_1, tone, velocityAttenuation, volume);

	voice->midiChannel = channel;
	voice->note = note;
	voice->algorithm[FM4_BANK_0] = bank0.algorithm;
	voice->algorithm[FM4_BANK_1] = bank1.algorithm;
	voice->carrierBase[FM4_BANK_0] = bank0.carrierBase;
	voice->carrierBase[FM4_BANK_1] = bank1.carrierBase;
	voice->modulatorBase[FM4_BANK_0] = bank0.modulatorBase;
	voice->modulatorBase[FM4_BANK_1] = bank1.modulatorBase;
	voice->duration[FM4_BANK_0] = bank0.duration;
	voice->duration[FM4_BANK_1] = bank1.duration;

	program_bank_instrument(driver, FM4_BANK_0, voiceIndex, get_tone(driver, FM4_BANK_0, tone));
	program_bank_instrument(driver, FM4_BANK_1, voiceIndex, get_tone(driver, FM4_BANK_1, tone));
	program_pitch(driver, voiceIndex, note, shift_right_signed(pitchBend - 0x2000, 1));
	if (tone < 0 || tone >= IM_FM4_TONE_COUNT)
		return;

	set_operator_level(driver, FM4_BANK_0, voiceIndex, IM_FM4_TONE_MODULATOR_LEVEL, bank0.modulator);
	set_operator_level(driver, FM4_BANK_0, voiceIndex, IM_FM4_TONE_CARRIER_LEVEL, bank0.carrier);
	set_operator_level(driver, FM4_BANK_1, voiceIndex, IM_FM4_TONE_MODULATOR_LEVEL, bank1.modulator);
	set_operator_level(driver, FM4_BANK_1, voiceIndex, IM_FM4_TONE_CARRIER_LEVEL, bank1.carrier);
	write_cached(driver, FM4_BANK_0, voiceIndex + 0xb0,
				 driver->registers[FM4_BANK_0][voiceIndex + 0xb0] | 0x20u);
	write_cached(driver, FM4_BANK_1, voiceIndex + 0xb0,
				 driver->registers[FM4_BANK_1][voiceIndex + 0xb0] | 0x20u);
}

static void apply_volume(ImFm4Driver* driver, int voiceIndex, int volume) {
	/* FM4_TonesApplyVolume, 0x10688. */
	ImFm4VoiceState* voice = &driver->voices[voiceIndex];
	for (int bank = 0; bank < 2; ++bank) {
		int carrier = im_fm4_attenuation[(volume * (voice->carrierBase[bank] + 1)) >> 7];
		set_operator_level(driver, bank, voiceIndex, IM_FM4_TONE_CARRIER_LEVEL, carrier);
		if (voice->algorithm[bank] == 1) {
			int modulator = im_fm4_attenuation[(volume * (voice->modulatorBase[bank] + 1)) >> 7];
			set_operator_level(driver, bank, voiceIndex, IM_FM4_TONE_MODULATOR_LEVEL, modulator);
		}
	}
}

void im_fm4_driver_init(ImFm4Driver* driver, ImFm4WriteRegFunc writeReg, void* writeUser) {
	/* FM4_OpInit, 0x10005. */
	if (!driver)
		return;
	memset(driver, 0, sizeof *driver);
	driver->writeReg = writeReg;
	driver->writeUser = writeUser;
	memcpy(driver->tones, im_fm4_default_tones, sizeof driver->tones);
	init_hardware(driver);

	for (int channel = 0; channel < IM_FM4_CHANNEL_COUNT; ++channel) {
		driver->channels[channel].noteRequest = 1;
		driver->channels[channel].volume = 127;
		driver->channels[channel].pan = 64;
		driver->channels[channel].pitchBend = 0x2000;
	}

	driver->freeHead = IM_FM4_INVALID_VOICE;
	driver->activeHead = IM_FM4_INVALID_VOICE;
	for (int voice = 0; voice < IM_FM4_VOICE_COUNT; ++voice) {
		driver->voiceNodes[voice].prev = IM_FM4_INVALID_VOICE;
		driver->voiceNodes[voice].next = IM_FM4_INVALID_VOICE;
		driver->voiceNodes[voice].voiceIndex = (uint8_t)voice;
		list_add(driver, &driver->freeHead, (uint8_t)voice);
	}
}

void im_fm4_driver_deinit(ImFm4Driver* driver) {
	/* FM4_TonesClearHardware, 0x1095a. */
	if (driver)
		clear_hardware(driver);
}

void im_fm4_driver_program_change(ImFm4Driver* driver, int channel, int program) {
	/* FM4_OpProgramChange, 0x100e7. */
	if (!driver || !valid_channel(channel))
		return;
	driver->channels[channel].program = clamp_int(program, 0, 127);
}

int im_fm4_driver_note_on(ImFm4Driver* driver, int channel, int note, int velocity) {
	/* FM4_OpNoteOn, 0x100fb. */
	if (!driver || !valid_channel(channel))
		return -1;
	note = clamp_int(note, 0, 127);
	velocity = clamp_int(velocity, 0, 127);
	ImFm4ChannelState* channelState = &driver->channels[channel];
	if (channel == FM4_PERCUSSION_CHANNEL) {
		channelState->program = im_fm4_percussion_tones[note] + 128;
		channelState->pitchBend = im_fm4_percussion_pitch[note] + 0x2000;
	}

	uint8_t voiceIndex = list_tail(driver, driver->freeHead);
	if (voiceIndex != IM_FM4_INVALID_VOICE) {
		list_remove(driver, &driver->freeHead, voiceIndex);
	} else {
		uint8_t selected = IM_FM4_INVALID_VOICE;
		const ImFm4ChannelState* selectedChannel = channelState;
		for (uint8_t candidate = driver->activeHead; candidate != IM_FM4_INVALID_VOICE;
			 candidate = driver->voiceNodes[candidate].next) {
			const ImFm4ChannelState* candidateChannel =
				&driver->channels[driver->voiceNodes[candidate].midiChannel];
			bool replace = false;
			if (candidateChannel->overflow == selectedChannel->overflow)
				replace = candidateChannel->priority <= selectedChannel->priority;
			else
				replace = candidateChannel->overflow != 0;
			if (replace) {
				selected = candidate;
				selectedChannel = candidateChannel;
			}
		}
		if (selected == IM_FM4_INVALID_VOICE)
			return -1;

		voiceIndex = selected;
		key_off_voice(driver, voiceIndex);
		ImFm4ChannelState* stolen = &driver->channels[driver->voiceNodes[voiceIndex].midiChannel];
		--stolen->voiceCount;
		stolen->overflow = stolen->voiceCount > stolen->noteRequest;
		list_remove(driver, &driver->activeHead, voiceIndex);
	}

	ImFm4VoiceNode* node = &driver->voiceNodes[voiceIndex];
	node->midiChannel = (uint8_t)channel;
	node->note = (uint8_t)note;
	list_add(driver, &driver->activeHead, voiceIndex);
	++channelState->voiceCount;
	channelState->overflow = channelState->voiceCount > channelState->noteRequest;
	start_voice(driver, voiceIndex, channel, note, velocity, channelState->program, channelState->volume,
				channelState->pitchBend);
	return voiceIndex;
}

bool im_fm4_driver_note_off(ImFm4Driver* driver, int channel, int note) {
	/* FM4_OpNoteOff, 0x10239. */
	if (!driver || !valid_channel(channel))
		return false;
	note = clamp_int(note, 0, 127);
	for (uint8_t voice = driver->activeHead; voice != IM_FM4_INVALID_VOICE;
		 voice = driver->voiceNodes[voice].next) {
		ImFm4VoiceNode* node = &driver->voiceNodes[voice];
		if (node->midiChannel != channel || node->note != note)
			continue;
		key_off_voice(driver, voice);
		ImFm4ChannelState* state = &driver->channels[channel];
		--state->voiceCount;
		state->overflow = state->voiceCount > state->noteRequest;
		list_remove(driver, &driver->activeHead, voice);
		list_add(driver, &driver->freeHead, voice);
		return true;
	}
	return false;
}

void im_fm4_driver_control_change(ImFm4Driver* driver, int channel, unsigned int controller, int value) {
	/* FM4_OpControlChange, 0x102a8. */
	if (!driver || !valid_channel(channel))
		return;
	value = clamp_int(value, 0, 127);
	ImFm4ChannelState* state = &driver->channels[channel];
	if (controller == FM4_CONTROLLER_VOLUME) {
		state->volume = value;
		for (uint8_t voice = driver->activeHead; voice != IM_FM4_INVALID_VOICE;
			 voice = driver->voiceNodes[voice].next) {
			if (driver->voiceNodes[voice].midiChannel == channel)
				apply_volume(driver, voice, value);
		}
	} else if (controller == FM4_CONTROLLER_PAN) {
		state->pan = value;
	} else if (controller == FM4_CONTROLLER_NOTE_REQUEST) {
		state->noteRequest = value;
	} else if (controller == FM4_CONTROLLER_PRIORITY) {
		state->priority = value;
	}
}

void im_fm4_driver_pitch_bend(ImFm4Driver* driver, int channel, int pitchBend14) {
	/* FM4_OpPitchBend, 0x10321. */
	if (!driver || !valid_channel(channel) || channel == FM4_PERCUSSION_CHANNEL)
		return;
	pitchBend14 = clamp_int(pitchBend14, 0, 0x3fff);
	driver->channels[channel].pitchBend = pitchBend14;
	for (uint8_t voice = driver->activeHead; voice != IM_FM4_INVALID_VOICE;
		 voice = driver->voiceNodes[voice].next) {
		if (driver->voiceNodes[voice].midiChannel == channel)
			program_pitch(driver, voice, driver->voiceNodes[voice].note,
						  shift_right_signed(pitchBend14 - 0x2000, 1));
	}
}

bool im_fm4_driver_apply_patch_sysex(ImFm4Driver* driver, const uint8_t* message, size_t size) {
	/* FM4_OpSysEx, 0x1035e. The decoded duration is payload byte 29. */
	if (!driver || !message || size < 6 || message[0] != 0xf0 || message[1] != 0x7d || message[2] != 0x02 ||
		message[size - 1] != 0xf7)
		return false;
	size_t nibbleCount = size - 6;
	if ((nibbleCount & 1u) != 0 || nibbleCount / 2 < 30 || nibbleCount / 2 > 128)
		return false;

	uint8_t decoded[128];
	size_t decodedCount = nibbleCount / 2;
	for (size_t i = 0; i < decodedCount; ++i) {
		uint8_t high = message[5 + 2 * i];
		uint8_t low = message[6 + 2 * i];
		if (high > 0x0f || low > 0x0f)
			return false;
		decoded[i] = (uint8_t)((high << 4) | low);
	}

	unsigned int flags = message[3];
	unsigned int tone = message[4];
	if ((flags & 1u) != 0) {
		if (tone >= IM_FM4_PERCUSSION_NOTE_COUNT)
			return false;
		tone = im_fm4_percussion_tones[tone] + 128u;
	}
	if (tone >= IM_FM4_TONE_COUNT)
		return false;
	int bank = (flags & 2u) != 0 ? FM4_BANK_1 : FM4_BANK_0;
	memcpy(driver->tones[bank][tone].packed, decoded, 11);
	driver->tones[bank][tone].duration = decoded[29];
	return true;
}

void im_fm4_driver_tick_voice(ImFm4Driver* driver, int voiceIndex) {
	/* FM4_TonesTickVoice, 0x1063c. Intentionally not scheduled. */
	if (!driver || voiceIndex < 0 || voiceIndex >= IM_FM4_VOICE_COUNT)
		return;
	ImFm4VoiceState* voice = &driver->voices[voiceIndex];
	if (voice->duration[FM4_BANK_0] != 0)
		--voice->duration[FM4_BANK_0];
	if (voice->duration[FM4_BANK_1] != 0)
		--voice->duration[FM4_BANK_1];
	if (voice->duration[FM4_BANK_0] == 0 && voice->duration[FM4_BANK_1] == 0)
		im_fm4_driver_note_off(driver, voice->midiChannel, voice->note);
}
