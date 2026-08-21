#include <imuse/commands.h>

#include "internal/midi_backend.h"
#include "internal/wave_renderer.h"

#include <stdint.h>
#include <string.h>

/* MIDI writes an interleaved stereo float bus and the wave renderer mixes into
 * it. The s16 entry point processes that bus in chunks and saturates on output.
 * Concurrency is handled by the wave SPSC ring and the selected MIDI backend. */

/* Bound audio-callback stack use independently of the requested frame count. */
#define IMUSE_MIX_S16_CHUNK_FRAMES 512

void imuse_mix_f32(imuse_t* im, float* buf, int frames) {
	if (!buf || frames <= 0)
		return;
	if (!im) {
		memset(buf, 0, (size_t)frames * 2u * sizeof(float));
		return;
	}
	ImMidi_RenderFloat(im, buf, frames);
	ImWaveRenderer_PullSamples(im, buf, frames);
}

void imuse_mix_s16(imuse_t* im, int16_t* buf, int frames) {
	if (!buf || frames <= 0)
		return;
	if (!im) {
		memset(buf, 0, (size_t)frames * 2u * sizeof(int16_t));
		return;
	}

	float scratch[IMUSE_MIX_S16_CHUNK_FRAMES * 2];
	int written = 0;
	while (written < frames) {
		int chunk = frames - written;
		if (chunk > IMUSE_MIX_S16_CHUNK_FRAMES)
			chunk = IMUSE_MIX_S16_CHUNK_FRAMES;

		/* Float bus: GMIDI overwrites, wave adds. Identical math to
		 * imuse_mix_f32 except into the local scratch. */
		ImMidi_RenderFloat(im, scratch, chunk);
		ImWaveRenderer_PullSamples(im, scratch, chunk);

		/* Saturating cast. Wave contribution is bounded ±1 by the
		 * companding LUT; the synthesized contribution can still make the sum
		 * exceed ±1 in dense passages — clamp before the integer cast or sign-flip
		 * wraparound produces a click. */
		int16_t* dst = &buf[2 * written];
		int n = chunk * 2;
		for (int i = 0; i < n; ++i) {
			float x = scratch[i];
			if (x > 1.0f)
				x = 1.0f;
			if (x < -1.0f)
				x = -1.0f;
			dst[i] = (int16_t)(x * 32767.0f);
		}

		written += chunk;
	}
}
