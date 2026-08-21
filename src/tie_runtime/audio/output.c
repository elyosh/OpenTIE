#include "tie_runtime/audio/output.h"

#include "aeron/audio.h"
#include "aeron/log.h"
#include "aeron/sync.h"

#include <stdatomic.h>
#include <stdint.h>

#define TIE_AUDIO_QUANTUM_FRAMES 256
#define TIE_AUDIO_QUEUE_FRAMES 2048
#define TIE_AUDIO_CHANNELS 2

typedef struct TieAudioOutput {
	AeronAudioStream stream;
	AeronThread* worker;
	TieAudioRenderFunc render;
	void* render_userdata;
	atomic_bool stopping;
	int16_t scratch[TIE_AUDIO_QUANTUM_FRAMES * TIE_AUDIO_CHANNELS];
} TieAudioOutput;

static TieAudioOutput audio_output;

static void TieAudioOutput_Clear(void) {
	audio_output.stream = 0;
	audio_output.worker = NULL;
	audio_output.render = NULL;
	audio_output.render_userdata = NULL;
	atomic_store_explicit(&audio_output.stopping, false, memory_order_relaxed);
}

static int TieAudioOutput_Worker(void* userdata) {
	TieAudioOutput* output = (TieAudioOutput*)userdata;

	while (!atomic_load_explicit(&output->stopping, memory_order_acquire)) {
		if (!Aeron_AudioStreamWaitWritable(output->stream, TIE_AUDIO_QUANTUM_FRAMES))
			break;
		if (atomic_load_explicit(&output->stopping, memory_order_acquire))
			break;

		output->render(output->render_userdata, output->scratch, TIE_AUDIO_QUANTUM_FRAMES);
		if (atomic_load_explicit(&output->stopping, memory_order_acquire))
			break;
		if (Aeron_AudioStreamWrite(output->stream, output->scratch, TIE_AUDIO_QUANTUM_FRAMES) !=
			TIE_AUDIO_QUANTUM_FRAMES)
			break;
	}

	return 0;
}

bool TieAudioOutput_Start(int sample_rate, int channels, TieAudioRenderFunc render, void* render_userdata) {
	if (sample_rate <= 0 || channels != TIE_AUDIO_CHANNELS || !render || audio_output.stream ||
		audio_output.worker) {
		return false;
	}

	audio_output.stream =
		Aeron_AudioStreamOpen(sample_rate, channels, AERON_PCM_S16, TIE_AUDIO_QUEUE_FRAMES, 1.0f);
	if (!audio_output.stream) {
		Aeron_LogWarn("tie.audio", "could not open queued PCM stream");
		return false;
	}

	audio_output.render = render;
	audio_output.render_userdata = render_userdata;
	atomic_store_explicit(&audio_output.stopping, false, memory_order_relaxed);

	for (size_t queued = 0; queued < TIE_AUDIO_QUEUE_FRAMES; queued += TIE_AUDIO_QUANTUM_FRAMES) {
		render(render_userdata, audio_output.scratch, TIE_AUDIO_QUANTUM_FRAMES);
		if (Aeron_AudioStreamWrite(audio_output.stream, audio_output.scratch, TIE_AUDIO_QUANTUM_FRAMES) !=
			TIE_AUDIO_QUANTUM_FRAMES) {
			Aeron_LogWarn("tie.audio", "could not prefill queued PCM stream");
			Aeron_AudioStreamClose(audio_output.stream);
			TieAudioOutput_Clear();
			return false;
		}
	}

	Aeron_AudioStreamPlay(audio_output.stream);
	audio_output.worker = Aeron_ThreadCreate("tie-pcm", TieAudioOutput_Worker, &audio_output);
	if (!audio_output.worker) {
		Aeron_AudioStreamPause(audio_output.stream);
		Aeron_AudioStreamClose(audio_output.stream);
		TieAudioOutput_Clear();
		return false;
	}
	return true;
}

void TieAudioOutput_Stop(void) {
	if (!audio_output.stream)
		return;

	atomic_store_explicit(&audio_output.stopping, true, memory_order_release);
	Aeron_AudioStreamPause(audio_output.stream);
	if (audio_output.worker)
		(void)Aeron_ThreadJoin(audio_output.worker);
	Aeron_AudioStreamClose(audio_output.stream);
	TieAudioOutput_Clear();
}
