#include "internal/wave_renderer.h"
#include "internal/state.h"

#include "internal/debug.h"
#include <imuse/commands.h>

#include <math.h>
#include <stdatomic.h>
#include <string.h>

/* The producer writes native-rate u8 offset-binary stereo; the audio thread
 * converts and mixes it. The int16-to-u8 round trip preserves the mixer's
 * soft-clipping companding curve. */

#define IM_WAVERENDERER_PI 3.14159265358979323846

static void wave_renderer_init_sb16_filter(ImWaveRendererState* r) {
	const int center = IM_WAVERENDERER_FIR_PAIRS;
	const double cutoff = 0.5 / (double)r->upsampleRatio;
	double coefficients[IM_WAVERENDERER_FIR_PAIRS];
	double sum = 2.0 * cutoff;

	/* Window an ideal low-pass impulse with a Blackman window. The
	 * filter operates after the sample hold, so unity DC gain is the
	 * correct normalization rather than the interpolation ratio. */
	for (int tap = 0; tap < center; ++tap) {
		const int offset = tap - center;
		const double phase = 2.0 * IM_WAVERENDERER_PI * cutoff * (double)offset;
		const double ideal = sin(phase) / (IM_WAVERENDERER_PI * (double)offset);
		const double windowPhase =
			2.0 * IM_WAVERENDERER_PI * (double)tap / (double)(IM_WAVERENDERER_FIR_TAPS - 1);
		const double window = 0.42 - 0.5 * cos(windowPhase) + 0.08 * cos(2.0 * windowPhase);
		coefficients[tap] = ideal * window;
		sum += 2.0 * coefficients[tap];
	}

	r->firPairCount = 0;
	for (int tap = 0; tap < center; ++tap) {
		const double normalized = coefficients[tap] / sum;
		if (fabs(normalized) <= 1.0e-10)
			continue;
		const int index = r->firPairCount++;
		r->firPairTaps[index] = (uint8_t)tap;
		r->firPairCoefficients[index] = (float)normalized;
	}
	r->firCenterCoefficient = (float)((2.0 * cutoff) / sum);
	r->firHistoryPos = 0;
	memset(r->firHistory, 0, sizeof r->firHistory);
}

static int wave_renderer_read_source_frame(ImWaveRendererState* r, float* left, float* right) {
	if (r->consumeOffset == 0 && atomic_load_explicit(&r->filledHalves, memory_order_acquire) <= 0) {
		return 0;
	}

	const uint8_t* src = &r->halves[r->consumeIdx][r->consumeOffset];
	*left = (float)((int)src[0] - 128) * (1.0f / 128.0f);
	*right = (float)((int)src[1] - 128) * (1.0f / 128.0f);
	r->consumeOffset += 2;
	if (r->consumeOffset >= IM_WAVERENDERER_HALF_BYTES) {
		r->consumeOffset = 0;
		r->consumeIdx = (r->consumeIdx + 1) % IM_WAVERENDERER_NUM_HALVES;
		atomic_fetch_sub_explicit(&r->filledHalves, 1, memory_order_release);
	}
	return 1;
}

static void wave_renderer_filter_frame(ImWaveRendererState* r, float left, float right, float* filteredLeft,
									   float* filteredRight) {
	const int position = r->firHistoryPos;
	const int center = IM_WAVERENDERER_FIR_PAIRS;
	r->firHistory[position][0] = left;
	r->firHistory[position][1] = right;

	int centerIndex = position - center;
	if (centerIndex < 0)
		centerIndex += IM_WAVERENDERER_FIR_TAPS;
	float sumLeft = r->firCenterCoefficient * r->firHistory[centerIndex][0];
	float sumRight = r->firCenterCoefficient * r->firHistory[centerIndex][1];

	for (int pair = 0; pair < r->firPairCount; ++pair) {
		const int tap = r->firPairTaps[pair];
		int recentIndex = position - tap;
		int oldIndex = position - (IM_WAVERENDERER_FIR_TAPS - 1 - tap);
		if (recentIndex < 0)
			recentIndex += IM_WAVERENDERER_FIR_TAPS;
		if (oldIndex < 0)
			oldIndex += IM_WAVERENDERER_FIR_TAPS;
		const float coefficient = r->firPairCoefficients[pair];
		sumLeft += coefficient * (r->firHistory[recentIndex][0] + r->firHistory[oldIndex][0]);
		sumRight += coefficient * (r->firHistory[recentIndex][1] + r->firHistory[oldIndex][1]);
	}

	r->firHistoryPos = (position + 1) % IM_WAVERENDERER_FIR_TAPS;
	*filteredLeft = sumLeft;
	*filteredRight = sumRight;
}

int ImWaveRenderer_Init(imuse_t* im) {
	ImWaveRendererState* r = &im->wave_renderer;
	memset(r->halves, 0x80, sizeof r->halves); /* offset-binary silence */
	r->produceIdx = 0;
	r->produceArmed = 0;
	r->consumeIdx = 0;
	r->consumeOffset = 0;
	r->heldLeft = 0.0f;
	r->heldRight = 0.0f;
	r->repeatRemaining = 0;
	r->waveFilterEnabled = 0;
	r->firPairCount = 0;
	r->firCenterCoefficient = 0.0f;
	r->firHistoryPos = 0;
	memset(r->firHistory, 0, sizeof r->firHistory);
	atomic_store_explicit(&r->filledHalves, 0, memory_order_relaxed);

	/* Engine status block latches sampleRateMode from waveSpeed; same
	 * convention as the DOS wave driver INIT path. The renderer is
	 * stereo-only — the mono branch in mixer_audioProcessFrame still
	 * works (it writes stride-1 samples), but every host today opens
	 * a stereo device so stereo is the only mode wired up here. */
	r->sampleRateMode = im->commands.config.waveSpeed ? 1 : 0;
	r->channels = 2;

	/* Engine-side production rate (matches the wave mixer kernels) and
	 * consumer-side output rate (matches the MIDI backend and audio device).
	 * The engine wave rate stays 22050 / 11025 so the existing voice / VOC /
	 * streamer pipeline is untouched. */
	r->waveSampleRate = r->sampleRateMode ? 22050 : 11025;
	r->outputSampleRate =
		im->commands.config.outputSampleRate > 0 ? im->commands.config.outputSampleRate : 44100;
	int ratio = r->outputSampleRate / r->waveSampleRate;
	if (ratio < 1)
		ratio = 1;
	if (r->outputSampleRate != ratio * r->waveSampleRate) {
		ImDebug_LogMsg(im,
					   "WR: outputSampleRate %d not an integer multiple "
					   "of wave rate %d -- rounding upsample ratio to %d "
					   "(expect mild pitch drift)",
					   r->outputSampleRate, r->waveSampleRate, ratio);
	}
	r->upsampleRatio = ratio;
	if (im->commands.config.waveOutputFilter == IMUSE_WAVE_OUTPUT_FILTER_SB16) {
		if (ratio > 1) {
			wave_renderer_init_sb16_filter(r);
			r->waveFilterEnabled = 1;
		} else {
			ImDebug_LogMsg(im, "WR: SB16 filter requires an output rate above the wave rate");
		}
	} else if (im->commands.config.waveOutputFilter != IMUSE_WAVE_OUTPUT_FILTER_NONE) {
		ImDebug_LogMsg(im, "WR: unknown wave output filter %d -- disabled",
					   im->commands.config.waveOutputFilter);
	}

	r->outDesc.buffer = r->halves[0];
	r->outDesc.writeCount = IM_WAVERENDERER_HALF_BYTES;
	r->outDesc.mixSampleCount = IM_WAVERENDERER_HALF_BYTES / 2; /* stereo */
	r->outDesc.sampleStride = 2;
	r->outDesc.field_10 = 0;
	r->outDesc.stereoMode = 1; /* normal L/R panning. The mixer
								* (ImMixer_audioProcessFrame) reads
								* mode 1 = normal, mode 2 = L/R
								* swapped -- the original SB16.WDR set
								* mode 2 only when the user enabled the
								* "Stereo Reverse" setup option; its
								* default (and ours) is mode 1. */

	r->initialized = 1;
	ImDebug_LogMsg(im, "wave-renderer module%s...", r->waveFilterEnabled ? " (SB16 filter)" : "");
	return 0;
}

int ImWaveRenderer_Deinit(imuse_t* im) {
	ImWaveRendererState* r = &im->wave_renderer;
	r->initialized = 0;
	/* Drop any pending samples — the consumer is going away. */
	atomic_store_explicit(&r->filledHalves, 0, memory_order_relaxed);
	r->produceArmed = 0;
	r->consumeOffset = 0;
	r->repeatRemaining = 0;
	return 0;
}

ImDigitalOutBuf* ImWaveRenderer_AcquireProduceFrame(imuse_t* im, int16_t** mixBufferOut) {
	ImWaveRendererState* r = &im->wave_renderer;
	if (!r->initialized)
		return NULL;

	/* Back-pressure: when every half is filled, return NULL so the
	 * engine skips this tick (matches the SDL driver's >=3-halves
	 * gate). The acquire load pairs with the consumer's release
	 * store after it drains a half. */
	int filled = atomic_load_explicit(&r->filledHalves, memory_order_acquire);
	if (filled >= IM_WAVERENDERER_NUM_HALVES)
		return NULL;

	/* Hand out halves[produceIdx]. produceArmed=1 marks the slot as
	 * pinned until Commit publishes it. */
	r->produceArmed = 1;
	r->outDesc.buffer = r->halves[r->produceIdx];
	if (mixBufferOut)
		*mixBufferOut = r->mixScratch;
	return &r->outDesc;
}

void ImWaveRenderer_CommitProduceFrame(imuse_t* im) {
	ImWaveRendererState* r = &im->wave_renderer;
	if (!r->initialized || !r->produceArmed)
		return;

	/* Advance produce head; publish the half to the consumer with a
	 * release store on filledHalves so the half's u8 contents are
	 * visible before the count change. */
	r->produceIdx = (r->produceIdx + 1) % IM_WAVERENDERER_NUM_HALVES;
	r->produceArmed = 0;
	atomic_fetch_add_explicit(&r->filledHalves, 1, memory_order_release);
}

void ImWaveRenderer_PullSamples(imuse_t* im, float* dst, int frames) {
	ImWaveRendererState* r = &im->wave_renderer;
	if (!r->initialized || !dst || frames <= 0)
		return;

	for (int frame = 0; frame < frames; ++frame) {
		if (r->repeatRemaining <= 0) {
			if (!wave_renderer_read_source_frame(r, &r->heldLeft, &r->heldRight)) {
				r->heldLeft = 0.0f;
				r->heldRight = 0.0f;
			}
			r->repeatRemaining = r->upsampleRatio;
		}

		float left = r->heldLeft;
		float right = r->heldRight;
		--r->repeatRemaining;
		if (r->waveFilterEnabled)
			wave_renderer_filter_frame(r, left, right, &left, &right);
		dst[2 * frame] += left;
		dst[2 * frame + 1] += right;
	}
}
