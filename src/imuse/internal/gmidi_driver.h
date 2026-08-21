#ifndef IMUSE_INTERNAL_GMIDI_DRIVER_H
#define IMUSE_INTERNAL_GMIDI_DRIVER_H

#include <stddef.h>
#include <stdint.h>

enum {
	IM_GMIDI_CHANNEL_COUNT = 16,
	IM_GMIDI_VOICE_COUNT = 12,
};

typedef int (*ImGmidiWriteMessageFunc)(void* user, const uint8_t* bytes, size_t size);

typedef struct ImGmidiChannelState {
	int32_t priority;     /* +0x00, CC18 */
	int32_t note_request; /* +0x04, CC17 */
	int32_t voice_count;  /* +0x08 */
	int32_t overflow;     /* +0x0c */
} ImGmidiChannelState;

typedef struct ImGmidiVoice {
	int8_t previous;
	int8_t next;
	uint8_t channel;
	uint8_t note;
} ImGmidiVoice;

typedef struct ImGmidiDriver {
	ImGmidiChannelState channels[IM_GMIDI_CHANNEL_COUNT];
	ImGmidiVoice voices[IM_GMIDI_VOICE_COUNT];
	int8_t active_head;
	int8_t free_head;
	ImGmidiWriteMessageFunc write_message;
	void* write_user;
} ImGmidiDriver;

#ifdef __cplusplus
extern "C" {
#endif

void im_gmidi_driver_init(ImGmidiDriver* driver, ImGmidiWriteMessageFunc write_message, void* write_user);
void im_gmidi_driver_deinit(ImGmidiDriver* driver);
int im_gmidi_driver_program_change(ImGmidiDriver* driver, int channel, int program);
int im_gmidi_driver_note_on(ImGmidiDriver* driver, int channel, int note, int velocity);
int im_gmidi_driver_note_off(ImGmidiDriver* driver, int channel, int note);
int im_gmidi_driver_control_change(ImGmidiDriver* driver, int channel, unsigned int controller, int value);
int im_gmidi_driver_pitch_bend(ImGmidiDriver* driver, int channel, int bend14);

#ifdef __cplusplus
}
#endif

#endif
