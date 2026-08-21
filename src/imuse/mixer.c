#include "internal/mixer.h"
#include "internal/state.h"

#include "internal/utils.h"
#include "internal/wave_renderer.h"
#include <imuse/commands.h>

#include <string.h>

#include "internal/mixer_tables.h"

/* im->mixer.normalization is a
 * midpoint pointer into a ±128*waveMixCount-byte buffer.
 * Positive mix values index forward; negative values index
 * backward. Clamps saturate to 0/255.
 */

/* ===== Module constants ===== */

/* Max supported waveMixCount — matches the driver's hard cap
 * (ImTracks_Init clamps config.waveMixCount to [1, 16]). */
#define IM_MIXER_MAX_MIX_COUNT 16

/* Companding buffer — 2 * 128 * maxMix bytes, indexed via the
 * im->mixer.normalization midpoint pointer. */

/* Per-frame state (valid between prepareDigitalOutput and
 * audioWriteToDriver). audioProcessFrame / AddSoundFrame read
 * these; WriteToDriver clears them so a double-flush no-ops. */

/* ===== Forward decls for the mix kernels ===== */

static int16_t mixer_mixMono1x(int16_t* dst, const uint8_t* src, const int8_t* volTable, int count);
static int16_t mixer_mixMono2x(int16_t* dst, const uint8_t* src, const int8_t* volTable, int count);
static int16_t mixer_mixStereo1x(int16_t* dst, const uint8_t* src, const int8_t* lTable, const int8_t* rTable,
								 int count);
static int16_t mixer_mixStereo2x(int16_t* dst, const uint8_t* src, const int8_t* lTable, const int8_t* rTable,
								 int count);
static void mixer_compandToPcm8(uint8_t* dst, const int16_t* src, const int8_t* table, int count);

/* ===== Lifecycle ===== */

int ImMixer_Init(imuse_t* im) {
	/* waveMixCount is read after ImTracks_Init has clamped it
	 * (and pushed the clamped value back into config). */
	int waveMixCount = im->commands.config.waveMixCount;

	/* Position im->mixer.normalization at the midpoint of the
	 * companding buffer so negative mix values index backward. */
	im->mixer.normalization = &im->mixer.normBuf[sizeof(im->mixer.normBuf) / 2];

	/* Companding curve: soft-limits summed voices so N sounds
	 * at full volume never clip the 8-bit DAC.
	 *
	 *   q = (i * 127 * M) / (127 * M + i * (M - 1))
	 *   table[+i] = 128 + q       (positive side, 128..255)
	 *   table[-i-1] = 127 - q     (negative side,   0..127)
	 *
	 * where M = waveMixCount and i walks [0, 128*M).
	 *
	 * q -> 127 as i saturates, so the endpoints flatten at
	 * 0xFF and 0x00. Result is offset-binary u8 PCM centred
	 * on 128. */
	int span = waveMixCount * 128;
	if (span > IM_MIXER_MAX_MIX_COUNT * 128)
		span = IM_MIXER_MAX_MIX_COUNT * 128;

	for (int i = 0; i < span; ++i) {
		int numer = i * 127 * waveMixCount;
		int denom = 127 * waveMixCount + i * (waveMixCount - 1);
		int q = ((numer << 8) / denom + 128) >> 8;
		im->mixer.normalization[i] = (int8_t)(128 + q);
		im->mixer.normalization[-i - 1] = (int8_t)(128 - q - 1);
	}
	return 0;
}

int ImMixer_Deinit(imuse_t* im) {
	/* Static LUT + per-frame pointers are the only state; no
	 * teardown needed. Kept as an API slot for symmetry. */
	return 0;
}

/* ===== Per-frame entry ===== */

int ImMixer_prepareDigitalOutput(imuse_t* im, ImDigitalOutBuf* outDesc, int16_t* mixBuf) {
	if (!mixBuf || !outDesc || !outDesc->buffer)
		return -1;

	im->mixer.driverOutPtr = outDesc;
	im->mixer.outBuf = mixBuf;

	/* Zero the int16 mix accumulator — AddSoundFrame / the per-track
	 * dispatch loop add into it. writeCount is in output samples
	 * (int16 slots). */
	memset(mixBuf, 0, sizeof(int16_t) * (size_t)outDesc->writeCount);
	return 0;
}

int ImMixer_audioWriteToDriver(imuse_t* im) {
	if (!im->mixer.outBuf || !im->mixer.driverOutPtr || !im->mixer.driverOutPtr->buffer)
		return -1;
	/* Compand the int16 mix scratch into the renderer's u8 half
	 * (outDesc->buffer aliases the renderer's halves[produceIdx]). */
	mixer_compandToPcm8(im->mixer.driverOutPtr->buffer, im->mixer.outBuf, im->mixer.normalization,
						im->mixer.driverOutPtr->writeCount);
	/* Publish the freshly-companded half to the consumer thread. */
	ImWaveRenderer_CommitProduceFrame(im);
	/* Clear the frame pointers so a spurious second flush cleanly
	 * returns -1 instead of re-shipping stale samples. */
	im->mixer.driverOutPtr = 0;
	im->mixer.outBuf = 0;
	return 0;
}

/* ===== Mix dispatcher ===== */

int16_t ImMixer_audioProcessFrame(imuse_t* im, ImSoundFrameState* state, int offset, int volume, int pan) {
	/* Guard rails — silently drop frames when the pipeline
	 * isn't primed or the slice has no samples. */
	if (!im->mixer.outBuf || !im->mixer.driverOutPtr || !im->mixer.driverOutPtr->buffer || !state->count)
		return 0;

	/* Volume bucket: 17 steps (0..16). (volume >> 3) gives
	 * 0..15 at most; +1 on non-zero volume pushes volume==0 to
	 * bucket 0 and volume==127 to bucket 16. */
	int volBucket = volume >> 3;
	if (volume)
		++volBucket;
	if (volBucket >= 17)
		volBucket = 16;

	int stride = im->mixer.driverOutPtr->sampleStride;

	if (im->mixer.driverOutPtr->stereoMode) {
		/* Stereo output: compute per-channel pan weighting. */
		int panBucket = (pan >> 3) - 8;
		if (pan > 64)
			++panBucket;

		int lPanFactor = (int8_t)s_panTableBase[volBucket][8 - panBucket];
		int rPanFactor = (int8_t)s_panTableBase[volBucket][8 + panBucket];

		const int8_t* lTable;
		const int8_t* rTable;
		if (im->mixer.driverOutPtr->stereoMode == 1) {
			lTable = &s_volumeCurves256[lPanFactor][0];
			rTable = &s_volumeCurves256[rPanFactor][0];
		} else {
			/* stereoMode == 2: L/R physically swapped. */
			lTable = &s_volumeCurves256[rPanFactor][0];
			rTable = &s_volumeCurves256[lPanFactor][0];
		}

		int writeOffset = stride * offset;
		if (state->sourceIsHighRate == 1)
			return mixer_mixStereo1x(&im->mixer.outBuf[writeOffset], state->buffer, lTable, rTable,
									 state->count);
		return mixer_mixStereo2x(&im->mixer.outBuf[writeOffset], state->buffer, lTable, rTable, state->count);
	}

	/* Mono output: single volume table, no pan. */
	const int8_t* volTable = &s_volumeCurves256[volBucket][0];
	int writeOffset = stride * offset;
	if (state->sourceIsHighRate == 1)
		return mixer_mixMono1x(&im->mixer.outBuf[writeOffset], state->buffer, volTable, state->count);
	return mixer_mixMono2x(&im->mixer.outBuf[writeOffset], state->buffer, volTable, state->count);
}

/* ===== Driver callback ===== */

int16_t ImMixer_AddSoundFrame(imuse_t* im, uint8_t* buf, int offset, int srcConsumedCount, int sampleCount,
							  int unused, int16_t vol, int16_t pan) {
	(void)unused;

	/* Detect whether the driver decoded at native output rate
	 * or half-rate (2x sample-duplication needed in the kernel).
	 * The >>4 is a tolerance window: anything within a 16-sample
	 * bucket counts as "same". */
	im->mixer.addFrameState.sourceIsHighRate = (srcConsumedCount >> 4) == (sampleCount >> 4);

	im->mixer.addFrameState.buffer = buf;
	im->mixer.addFrameState.count = sampleCount;

	/* Pan in on-wire form is signed 8.8 bipolar; map to 0..127
	 * centre 64 via (pan >> 1) + 64 with saturation. */
	int panClamped = ImUtils_Clamp(im, (int)(int16_t)((pan >> 1) + 64), 0, 127);
	int volClamped = ImUtils_Clamp(im, (int)vol, 0, 127);
	return ImMixer_audioProcessFrame(im, &im->mixer.addFrameState, offset, volClamped, panClamped);
}

/* ===== Mix kernels =====
 *
 * Each kernel reads `count` source bytes and adds scaled int8
 * amplitudes into the int16 accumulator. volTable / lTable /
 * rTable are 256-entry rows selected from s_volumeCurves256
 * (volume-bucket or pan-weighted-bucket row). Source bytes
 * index the table as unsigned u8 and the stored signed int8
 * result is added (sign-extended) into the accumulator.
 */

static int16_t mixer_mixMono1x(int16_t* dst, const uint8_t* src, const int8_t* volTable, int count) {
	int16_t last = 0;
	do {
		int16_t mix = volTable[*src++];
		*dst = (int16_t)(*dst + mix);
		last = *dst++;
	} while (--count);
	return last;
}

static int16_t mixer_mixMono2x(int16_t* dst, const uint8_t* src, const int8_t* volTable, int count) {
	int16_t last = 0;
	do {
		int16_t mix = volTable[*src++];
		dst[0] = (int16_t)(dst[0] + mix);
		dst[1] = (int16_t)(dst[1] + mix);
		last = dst[0];
		dst += 2;
	} while (--count);
	return last;
}

static int16_t mixer_mixStereo1x(int16_t* dst, const uint8_t* src, const int8_t* lTable, const int8_t* rTable,
								 int count) {
	int16_t last = 0;
	do {
		uint8_t s = *src++;
		dst[0] = (int16_t)(dst[0] + lTable[s]);
		dst[1] = (int16_t)(dst[1] + rTable[s]);
		last = dst[1];
		dst += 2;
	} while (--count);
	return last;
}

static int16_t mixer_mixStereo2x(int16_t* dst, const uint8_t* src, const int8_t* lTable, const int8_t* rTable,
								 int count) {
	int16_t last = 0;
	do {
		uint8_t s = *src++;
		int16_t mixL = lTable[s];
		int16_t mixR = rTable[s];
		dst[0] = (int16_t)(dst[0] + mixL);
		dst[2] = (int16_t)(dst[2] + mixL);
		dst[1] = (int16_t)(dst[1] + mixR);
		dst[3] = (int16_t)(dst[3] + mixR);
		last = dst[3];
		dst += 4;
	} while (--count);
	return last;
}

static void mixer_compandToPcm8(uint8_t* dst, const int16_t* src, const int8_t* table, int count) {
	/* table is a midpoint pointer: negative mix values index
	 * backward, positive values index forward. The stored int8
	 * is actually an offset-binary u8 (see Init), so we cast
	 * through int8_t -> u8 preserving the bit pattern. */
	do {
		int16_t mixSample = *src++;
		*dst++ = (uint8_t)table[mixSample];
	} while (--count);
}
