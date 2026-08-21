#ifndef __IMUSE_MIXER_H__
#define __IMUSE_MIXER_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Owns the int16 mix buffer and the 8-to-16-bit companding table. */

/* 24-byte driver-owned output descriptor. mixSampleCount is the
 * number of source samples the mixer should consume per frame;
 * sampleStride is the stride between consecutive output samples
 * (1 for mono, 2 for stereo). stereoMode selects the panning
 * model used by audioProcessFrame. */
typedef struct ImDigitalOutBuf {
	uint8_t* buffer;
	int32_t writeCount;
	int32_t mixSampleCount;
	int32_t sampleStride;
	int32_t field_10;
	int32_t stereoMode;
} ImDigitalOutBuf;

/* Per-source-frame staging struct. Populated by
 * ImDispatch_PlaySoundFrame and ImMixer_AddSoundFrame before
 * each audioProcessFrame call; the mixer consumes `buffer` for
 * `count` input samples and upsamples 2x when sourceIsHighRate
 * == 0. */
typedef struct ImSoundFrameState {
	uint8_t* buffer;
	int32_t count;
	int32_t sourceIsHighRate;
} ImSoundFrameState;

int ImMixer_Init(imuse_t* im);
int ImMixer_Deinit(imuse_t* im);

/* Seed the int16 mix scratch buffer for a new frame. `outDesc`
 * describes the output region; `mixBufferOut` is the scratch
 * int16 buffer the driver will read back. Passed NULL in silent
 * mode. */
int ImMixer_prepareDigitalOutput(imuse_t* im, ImDigitalOutBuf* outDesc, int16_t* mixBufferOut);

/* Compand the scratch int16 buffer back to 8-bit and release
 * the frame to the driver. No-op in silent mode. */
int ImMixer_audioWriteToDriver(imuse_t* im);

/* Add one source's frame to the int16 mix accumulator at the
 * given sample offset. volume is the post-group effective
 * volume; pan is 0..127 centre 64. */
int16_t ImMixer_audioProcessFrame(imuse_t* im, ImSoundFrameState* state, int offset, int volume, int pan);

/* Wave-driver callback installed by prepareDigitalOutput.
 * Invoked once per active voice with a decoded u8 PCM slice;
 * wraps the args and forwards to audioProcessFrame. pan is
 * signed 8.8 bipolar; vol is 0..127. */
int16_t ImMixer_AddSoundFrame(imuse_t* im, uint8_t* buf, int offset, int srcConsumedCount, int sampleCount,
							  int unused, int16_t vol, int16_t pan);

#endif /* __IMUSE_MIXER_H__ */
