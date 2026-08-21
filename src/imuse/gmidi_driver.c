#include "internal/gmidi_driver.h"

#include <string.h>

enum { GMIDI_INVALID_VOICE = -1 };

static const uint8_t g_roland_init[] = {
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, 0x10, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x17, 0xf7,
};

static int valid_channel(int channel) { return (unsigned int)channel < IM_GMIDI_CHANNEL_COUNT; }

static int send_message(ImGmidiDriver* driver, const uint8_t* bytes, size_t size) {
	return driver->write_message ? driver->write_message(driver->write_user, bytes, size) : -1;
}

static int send_control(ImGmidiDriver* driver, int channel, int controller, int value) {
	const uint8_t message[3] = {
		(uint8_t)(0xb0 | channel),
		(uint8_t)controller,
		(uint8_t)value,
	};
	return send_message(driver, message, sizeof message);
}

static void list_remove(ImGmidiDriver* driver, int8_t* head, int8_t index) {
	ImGmidiVoice* voice = &driver->voices[index];
	if (voice->previous != GMIDI_INVALID_VOICE)
		driver->voices[voice->previous].next = voice->next;
	else
		*head = voice->next;
	if (voice->next != GMIDI_INVALID_VOICE)
		driver->voices[voice->next].previous = voice->previous;
	voice->previous = GMIDI_INVALID_VOICE;
	voice->next = GMIDI_INVALID_VOICE;
}

static void list_add(ImGmidiDriver* driver, int8_t* head, int8_t index) {
	ImGmidiVoice* voice = &driver->voices[index];
	voice->previous = GMIDI_INVALID_VOICE;
	voice->next = *head;
	if (*head != GMIDI_INVALID_VOICE)
		driver->voices[*head].previous = index;
	*head = index;
}

static void release_voice(ImGmidiDriver* driver, int8_t voice_index) {
	ImGmidiVoice* voice = &driver->voices[voice_index];
	ImGmidiChannelState* channel = &driver->channels[voice->channel & 0x7f];
	--channel->voice_count;
	channel->overflow = channel->voice_count > channel->note_request;
	list_remove(driver, &driver->active_head, voice_index);
	list_add(driver, &driver->free_head, voice_index);
}

void im_gmidi_driver_init(ImGmidiDriver* driver, ImGmidiWriteMessageFunc write_message, void* write_user) {
	if (!driver)
		return;
	memset(driver, 0, sizeof *driver);
	driver->write_message = write_message;
	driver->write_user = write_user;
	driver->active_head = GMIDI_INVALID_VOICE;
	driver->free_head = GMIDI_INVALID_VOICE;
	(void)send_message(driver, g_roland_init, sizeof g_roland_init);
	for (int channel = 0; channel < IM_GMIDI_CHANNEL_COUNT; ++channel) {
		(void)send_control(driver, channel, 0, 0);
		(void)im_gmidi_driver_program_change(driver, channel, 0);
		(void)send_control(driver, channel, 100, 0);
		(void)send_control(driver, channel, 101, 0);
		(void)send_control(driver, channel, 6, 16);
		(void)send_control(driver, channel, 64, 0);
		(void)send_control(driver, channel, 123, 0);
		(void)send_control(driver, channel, 91, 64);
		(void)send_control(driver, channel, 93, 0);
		driver->channels[channel].priority = 0;
		driver->channels[channel].note_request = 1;
	}
	for (int8_t voice = 0; voice < IM_GMIDI_VOICE_COUNT; ++voice) {
		driver->voices[voice].previous = GMIDI_INVALID_VOICE;
		driver->voices[voice].next = GMIDI_INVALID_VOICE;
		list_add(driver, &driver->free_head, voice);
	}
}

void im_gmidi_driver_deinit(ImGmidiDriver* driver) {
	if (!driver)
		return;
	for (int channel = 0; channel < IM_GMIDI_CHANNEL_COUNT; ++channel) {
		(void)send_control(driver, channel, 64, 0);
		(void)send_control(driver, channel, 123, 0);
	}
}

int im_gmidi_driver_program_change(ImGmidiDriver* driver, int channel, int program) {
	if (!driver || !valid_channel(channel))
		return -1;
	const uint8_t message[2] = { (uint8_t)(0xc0 | channel), (uint8_t)program };
	return send_message(driver, message, sizeof message);
}

static int8_t choose_voice_to_steal(ImGmidiDriver* driver, int channel) {
	int8_t selected = GMIDI_INVALID_VOICE;
	const ImGmidiChannelState* selected_state = &driver->channels[channel];
	for (int8_t current = driver->active_head; current != GMIDI_INVALID_VOICE;
		 current = driver->voices[current].next) {
		const ImGmidiChannelState* candidate = &driver->channels[driver->voices[current].channel & 0x7f];
		if (candidate->overflow == selected_state->overflow) {
			if (candidate->priority > selected_state->priority)
				continue;
		} else if (!candidate->overflow) {
			continue;
		}
		selected = current;
		selected_state = candidate;
	}
	return selected;
}

int im_gmidi_driver_note_on(ImGmidiDriver* driver, int channel, int note, int velocity) {
	if (!driver || !valid_channel(channel))
		return -1;
	int8_t voice_index = driver->free_head;
	if (voice_index != GMIDI_INVALID_VOICE) {
		list_remove(driver, &driver->free_head, voice_index);
	} else {
		voice_index = choose_voice_to_steal(driver, channel);
		if (voice_index == GMIDI_INVALID_VOICE)
			return -1;
		const ImGmidiVoice* victim = &driver->voices[voice_index];
		const uint8_t release[3] = {
			(uint8_t)(0x80 | (victim->channel & 0x7f)),
			victim->note,
			64,
		};
		int result = -1;
		for (int attempt = 0; attempt < 10 && result == -1; ++attempt)
			result = send_message(driver, release, sizeof release);
		if (result == -1)
			return -1;
		ImGmidiChannelState* victim_state = &driver->channels[victim->channel & 0x7f];
		--victim_state->voice_count;
		victim_state->overflow = victim_state->voice_count > victim_state->note_request;
		list_remove(driver, &driver->active_head, voice_index);
	}

	ImGmidiVoice* voice = &driver->voices[voice_index];
	voice->channel = (uint8_t)channel;
	voice->note = (uint8_t)note;
	list_add(driver, &driver->active_head, voice_index);
	ImGmidiChannelState* state = &driver->channels[channel];
	++state->voice_count;
	state->overflow = state->voice_count > state->note_request;
	const uint8_t message[3] = {
		(uint8_t)(0x90 | channel),
		(uint8_t)note,
		(uint8_t)(((110 * velocity) >> 7) + 17),
	};
	return send_message(driver, message, sizeof message);
}

int im_gmidi_driver_note_off(ImGmidiDriver* driver, int channel, int note) {
	if (!driver || !valid_channel(channel))
		return -1;
	int result = 0;
	int8_t current = driver->active_head;
	while (current != GMIDI_INVALID_VOICE) {
		ImGmidiVoice* voice = &driver->voices[current];
		if ((voice->channel & 0x80) == 0 && (voice->channel != channel || voice->note != note)) {
			current = voice->next;
			continue;
		}

		const uint8_t message[3] = {
			(uint8_t)(0x80 | (voice->channel & 0x7f)),
			voice->note,
			64,
		};
		result = -1;
		/* The original loop performs nine sends: counters 1 through 9. */
		for (int attempt = 1; attempt < 10 && result == -1; ++attempt)
			result = send_message(driver, message, sizeof message);
		if (result == -1) {
			voice->channel |= 0x80;
			return -1;
		}
		voice->channel &= 0x7f;
		release_voice(driver, current);
		/* Retail leaves the old active chain after moving this node to the
		 * free list, so one call retires one matching or pending voice. */
		return result;
	}
	return result;
}

int im_gmidi_driver_control_change(ImGmidiDriver* driver, int channel, unsigned int controller, int value) {
	if (!driver || !valid_channel(channel))
		return -1;
	switch (controller) {
		case 1:
		case 7:
		case 10:
		case 91:
		case 93:
			return send_control(driver, channel, (int)controller, value);
		case 17:
			driver->channels[channel].note_request = value;
			return value;
		case 18:
			driver->channels[channel].priority = value;
			return value;
		default:
			return (int)controller;
	}
}

int im_gmidi_driver_pitch_bend(ImGmidiDriver* driver, int channel, int bend14) {
	if (!driver || !valid_channel(channel))
		return -1;
	const uint8_t message[3] = {
		(uint8_t)(0xe0 | channel),
		(uint8_t)(bend14 & 0x7f),
		(uint8_t)((bend14 >> 7) & 0x7f),
	};
	return send_message(driver, message, sizeof message);
}
