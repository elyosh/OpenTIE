#include <imuse/midi_nuked_sc55.h>

#include "internal/gmidi_driver.h"
#include "internal/midi_backend.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#ifdef HAVE_NUKED_SC55

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include <speex/speex_resampler.h>

#include "emu.h"
#include "pcm.h"
#include "rom_io.h"
#include "rom_loader.h"

namespace {

constexpr unsigned kEventCapacity = 4096;
constexpr unsigned kEventMask = kEventCapacity - 1;
constexpr size_t kNativeChunkFrames = 512;
constexpr size_t kNativeCapacityFrames = 1024;

enum class MidiEventType : uint8_t {
	Program,
	NoteOn,
	NoteOff,
	Control,
	PitchBend,
};

struct MidiEvent {
	uint16_t value;
	uint8_t type;
	uint8_t channel;
	uint8_t data1;
	uint8_t data2;
};

static_assert(sizeof(MidiEvent) == 6);

constexpr std::string_view kPreferredRomsetNames[] = {
	"mk2-v1.01", "mk1-v1.21", "mk1-v1.20", "mk1-v1.10", "mk1-v1.00", "mk1-v2.00",
};

int clamp_midi(int value, int maximum) { return std::clamp(value, 0, maximum); }

void set_error(char* error, size_t capacity, const char* format, ...) {
	if (!error || capacity == 0)
		return;
	va_list args;
	va_start(args, format);
	std::vsnprintf(error, capacity, format, args);
	va_end(args);
}

} // namespace

struct ImuseNukedSc55Romset {
	common::LoadRomsetResult loaded;
};

namespace {

common::LoadRomsetError load_preferred_romset(const std::filesystem::path& directory,
											  common::LoadRomsetResult& result) {
	const common::RomOverrides overrides {};
	common::LoadRomsetError load_error =
		common::LoadRomset(directory, "mk2", common::RomLoader::Hashing, overrides, result);
	if (load_error == common::LoadRomsetError {})
		return load_error;
	if (load_error != common::LoadRomsetError::AmbiguousRomset &&
		load_error != common::LoadRomsetError::NoCompleteRomsets)
		return load_error;

	/* MKII detection populated the hash and definition registries. Reuse them
	 * to resolve ambiguity or fall back to Mk1 without rescanning the directory. */
	for (size_t index = 0; index < std::size(kPreferredRomsetNames); ++index) {
		RomsetInfo candidate;
		if (!GetRomsetInfo(result.registries.romsets, kPreferredRomsetNames[index], result.registries.hashes,
						   ROMLOCATION_ALL, candidate))
			continue;

		if (!result.registries.romsets.GetRomsetFamily(kPreferredRomsetNames[index], result.romset))
			continue;
		result.picked_name = kPreferredRomsetNames[index];
		result.romset_info = std::move(candidate);
		(void)IsCompleteRomset(result.romset_info, result.romset, &result.completion);
		if (!LoadRomset(result.romset_info, &result.loaded))
			return common::LoadRomsetError::RomLoadFailed;
		return common::LoadRomsetError {};
	}

	result.picked_name.clear();
	return common::LoadRomsetError::NoCompleteRomsets;
}

struct NukedSc55Backend {
	ImuseMidiBackend vtable {};
	MidiEvent events[kEventCapacity] {};
	alignas(64) std::atomic_uint write_index { 0 };
	alignas(64) std::atomic_uint read_index { 0 };
	std::atomic_bool reset_requested { false };
	ImGmidiDriver driver {};
	std::unique_ptr<Emulator> emulator;
	const ImuseNukedSc55Romset* romset = nullptr;
	SpeexResamplerState* resampler = nullptr;
	float native_frames[kNativeCapacityFrames * 2] {};
	size_t native_start = 0;
	size_t native_count = 0;
	ImuseLogFunc log_func = nullptr;
	void* log_user = nullptr;
	int output_rate = 0;
	uint32_t native_rate = 0;
	bool opened = false;
};

void log_message(NukedSc55Backend* backend, ImuseLogLevel level, const char* message) {
	if (backend->log_func)
		backend->log_func(backend->log_user, level, message);
}

void receive_sample(void* user, const AudioFrame<int32_t>& frame) {
	auto* backend = static_cast<NukedSc55Backend*>(user);
	if (backend->native_start + backend->native_count >= kNativeCapacityFrames)
		return;
	float* destination = backend->native_frames + (backend->native_start + backend->native_count) * 2;
	destination[0] = static_cast<float>(frame.left) * (1.0f / 536870912.0f);
	destination[1] = static_cast<float>(frame.right) * (1.0f / 536870912.0f);
	++backend->native_count;
}

int write_midi_message(void* user, const uint8_t* bytes, size_t size) {
	auto* backend = static_cast<NukedSc55Backend*>(user);
	if (!backend || !backend->emulator || !bytes || size == 0)
		return -1;
	backend->emulator->PostMIDI(std::span<const uint8_t>(bytes, size));
	return 0;
}

void enqueue_event(NukedSc55Backend* backend, MidiEvent event) {
	if (!backend->opened || backend->reset_requested.load(std::memory_order_acquire))
		return;
	const unsigned write = backend->write_index.load(std::memory_order_relaxed);
	const unsigned read = backend->read_index.load(std::memory_order_acquire);
	if (write - read >= kEventCapacity) {
		backend->reset_requested.store(true, std::memory_order_release);
		return;
	}
	backend->events[write & kEventMask] = event;
	backend->write_index.store(write + 1, std::memory_order_release);
}

void apply_event(NukedSc55Backend* backend, const MidiEvent& event) {
	switch (static_cast<MidiEventType>(event.type)) {
		case MidiEventType::Program:
			(void)im_gmidi_driver_program_change(&backend->driver, event.channel, event.data1);
			break;
		case MidiEventType::NoteOn:
			(void)im_gmidi_driver_note_on(&backend->driver, event.channel, event.data1, event.data2);
			break;
		case MidiEventType::NoteOff:
			(void)im_gmidi_driver_note_off(&backend->driver, event.channel, event.data1);
			break;
		case MidiEventType::Control:
			(void)im_gmidi_driver_control_change(&backend->driver, event.channel, event.data1, event.data2);
			break;
		case MidiEventType::PitchBend:
			(void)im_gmidi_driver_pitch_bend(&backend->driver, event.channel, event.value);
			break;
	}
}

void drain_events(NukedSc55Backend* backend) {
	if (backend->reset_requested.load(std::memory_order_acquire)) {
		const unsigned write = backend->write_index.load(std::memory_order_acquire);
		backend->read_index.store(write, std::memory_order_release);
		im_gmidi_driver_deinit(&backend->driver);
		im_gmidi_driver_init(&backend->driver, write_midi_message, backend);
		backend->reset_requested.store(false, std::memory_order_release);
		log_message(backend, IMUSE_LOG_WARN, "midi_nuked_sc55: event queue overflow; resetting MIDI state");
		return;
	}
	unsigned read = backend->read_index.load(std::memory_order_relaxed);
	const unsigned snapshot = backend->write_index.load(std::memory_order_acquire);
	while (read != snapshot) {
		apply_event(backend, backend->events[read & kEventMask]);
		++read;
	}
	backend->read_index.store(read, std::memory_order_release);
}

void generate_native_chunk(NukedSc55Backend* backend) {
	backend->native_start = 0;
	backend->native_count = 0;
	while (backend->native_count < kNativeChunkFrames)
		backend->emulator->Step();
}

int close_backend(NukedSc55Backend* backend) {
	if (!backend->opened)
		return 0;
	backend->opened = false;
	im_gmidi_driver_deinit(&backend->driver);
	if (backend->resampler)
		speex_resampler_destroy(backend->resampler);
	backend->resampler = nullptr;
	backend->emulator.reset();
	backend->native_start = 0;
	backend->native_count = 0;
	backend->write_index.store(0, std::memory_order_relaxed);
	backend->read_index.store(0, std::memory_order_relaxed);
	backend->reset_requested.store(false, std::memory_order_relaxed);
	backend->log_func = nullptr;
	backend->log_user = nullptr;
	return 0;
}

int vt_open(ImuseMidiBackend* self, int sample_rate, ImuseLogFunc log_func, void* log_user) {
	auto* backend = reinterpret_cast<NukedSc55Backend*>(self);
	if (backend->opened)
		return 0;
	if (sample_rate <= 0 || !backend->romset)
		return -1;
	backend->log_func = log_func;
	backend->log_user = log_user;
	backend->emulator.reset(new (std::nothrow) Emulator());
	if (!backend->emulator) {
		log_message(backend, IMUSE_LOG_ERROR, "midi_nuked_sc55: emulator allocation failed");
		return -1;
	}
	if (!backend->emulator->Init(EMU_Options {}) ||
		!backend->emulator->LoadRoms(backend->romset->loaded.romset, backend->romset->loaded.romset_info)) {
		log_message(backend, IMUSE_LOG_ERROR, "midi_nuked_sc55: emulator initialization failed");
		backend->emulator.reset();
		return -1;
	}
	backend->emulator->Reset();
	backend->emulator->GetPCM().enable_oversampling = true;
	if (backend->romset->loaded.romset == Romset::MK2)
		backend->emulator->PostSystemReset(EMU_SystemReset::GS_RESET);
	for (size_t step = 0; step < 24000000; ++step)
		backend->emulator->Step();
	backend->emulator->SetSampleCallback(receive_sample, backend);
	backend->native_rate = PCM_GetOutputFrequency(backend->emulator->GetPCM());
	backend->output_rate = sample_rate;
	if (backend->native_rate == 0) {
		log_message(backend, IMUSE_LOG_ERROR, "midi_nuked_sc55: emulator reported an invalid sample rate");
		backend->emulator.reset();
		return -1;
	}
	int resampler_error = RESAMPLER_ERR_SUCCESS;
	backend->resampler = speex_resampler_init(2, backend->native_rate, static_cast<spx_uint32_t>(sample_rate),
											  SPEEX_RESAMPLER_QUALITY_DESKTOP, &resampler_error);
	if (!backend->resampler || resampler_error != RESAMPLER_ERR_SUCCESS) {
		log_message(backend, IMUSE_LOG_ERROR, "midi_nuked_sc55: could not create sample-rate converter");
		if (backend->resampler)
			speex_resampler_destroy(backend->resampler);
		backend->resampler = nullptr;
		backend->emulator.reset();
		return -1;
	}
	speex_resampler_skip_zeros(backend->resampler);
	backend->write_index.store(0, std::memory_order_relaxed);
	backend->read_index.store(0, std::memory_order_relaxed);
	backend->reset_requested.store(false, std::memory_order_relaxed);
	backend->native_start = 0;
	backend->native_count = 0;
	im_gmidi_driver_init(&backend->driver, write_midi_message, backend);
	backend->opened = true;
	return 0;
}

int vt_close(ImuseMidiBackend* self) { return close_backend(reinterpret_cast<NukedSc55Backend*>(self)); }

void vt_release(ImuseMidiBackend* self) {
	if (!self)
		return;
	auto* backend = reinterpret_cast<NukedSc55Backend*>(self);
	(void)close_backend(backend);
	delete backend;
}

void vt_program_change(ImuseMidiBackend* self, int channel, int program) {
	if (static_cast<unsigned>(channel) >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(reinterpret_cast<NukedSc55Backend*>(self),
				  { 0, static_cast<uint8_t>(MidiEventType::Program), static_cast<uint8_t>(channel),
					static_cast<uint8_t>(clamp_midi(program, 127)), 0 });
}

void vt_note_on(ImuseMidiBackend* self, int channel, int note, int velocity) {
	if (static_cast<unsigned>(channel) >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(reinterpret_cast<NukedSc55Backend*>(self),
				  { 0, static_cast<uint8_t>(MidiEventType::NoteOn), static_cast<uint8_t>(channel),
					static_cast<uint8_t>(clamp_midi(note, 127)),
					static_cast<uint8_t>(clamp_midi(velocity, 127)) });
}

void vt_note_off(ImuseMidiBackend* self, int channel, int note) {
	if (static_cast<unsigned>(channel) >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(reinterpret_cast<NukedSc55Backend*>(self),
				  { 0, static_cast<uint8_t>(MidiEventType::NoteOff), static_cast<uint8_t>(channel),
					static_cast<uint8_t>(clamp_midi(note, 127)), 0 });
}

void vt_control_change(ImuseMidiBackend* self, int channel, unsigned int controller, int value) {
	if (static_cast<unsigned>(channel) >= IM_GMIDI_CHANNEL_COUNT || controller > 127)
		return;
	enqueue_event(reinterpret_cast<NukedSc55Backend*>(self),
				  { 0, static_cast<uint8_t>(MidiEventType::Control), static_cast<uint8_t>(channel),
					static_cast<uint8_t>(controller), static_cast<uint8_t>(clamp_midi(value, 127)) });
}

void vt_pitch_bend(ImuseMidiBackend* self, int channel, int bend14) {
	if (static_cast<unsigned>(channel) >= IM_GMIDI_CHANNEL_COUNT)
		return;
	enqueue_event(reinterpret_cast<NukedSc55Backend*>(self),
				  { static_cast<uint16_t>(clamp_midi(bend14, 0x3fff)),
					static_cast<uint8_t>(MidiEventType::PitchBend), static_cast<uint8_t>(channel), 0, 0 });
}

void vt_render_float(ImuseMidiBackend* self, float* buffer, int frames) {
	auto* backend = reinterpret_cast<NukedSc55Backend*>(self);
	if (!buffer || frames <= 0)
		return;
	if (!backend->opened) {
		std::memset(buffer, 0, static_cast<size_t>(frames) * 2 * sizeof(float));
		return;
	}
	drain_events(backend);
	spx_uint32_t written = 0;
	while (written < static_cast<spx_uint32_t>(frames)) {
		if (backend->native_count == 0)
			generate_native_chunk(backend);
		spx_uint32_t input = static_cast<spx_uint32_t>(backend->native_count);
		spx_uint32_t output = static_cast<spx_uint32_t>(frames) - written;
		const int result = speex_resampler_process_interleaved_float(
			backend->resampler, backend->native_frames + backend->native_start * 2, &input,
			buffer + static_cast<size_t>(written) * 2, &output);
		if (result != RESAMPLER_ERR_SUCCESS) {
			std::memset(buffer + static_cast<size_t>(written) * 2, 0,
						(static_cast<size_t>(frames) - written) * 2 * sizeof(float));
			log_message(backend, IMUSE_LOG_ERROR, "midi_nuked_sc55: sample-rate conversion failed");
			return;
		}
		backend->native_start += input;
		backend->native_count -= input;
		written += output;
		if (input == 0 && output == 0) {
			std::memset(buffer + static_cast<size_t>(written) * 2, 0,
						(static_cast<size_t>(frames) - written) * 2 * sizeof(float));
			log_message(backend, IMUSE_LOG_ERROR, "midi_nuked_sc55: sample-rate converter made no progress");
			return;
		}
	}
}

} // namespace

extern "C" int imuse_nuked_sc55_backend_available(void) { return 1; }

extern "C" ImuseNukedSc55Romset* imuse_nuked_sc55_romset_load(const char* directory, char* error,
															  size_t error_capacity) {
	if (!directory || directory[0] == '\0') {
		set_error(error, error_capacity, "SC-55 ROM directory is empty.");
		return nullptr;
	}
	auto* resource = new (std::nothrow) ImuseNukedSc55Romset;
	if (!resource) {
		set_error(error, error_capacity, "Could not allocate the SC-55 ROM resource.");
		return nullptr;
	}
	try {
		const common::LoadRomsetError load_error =
			load_preferred_romset(std::filesystem::path(directory), resource->loaded);
		if (load_error != common::LoadRomsetError {}) {
			if (load_error == common::LoadRomsetError::NoCompleteRomsets) {
				set_error(error, error_capacity,
						  "No complete supported SC-55 or SC-55mkII ROM set was found in '%s'.", directory);
			} else {
				set_error(error, error_capacity, "%s while detecting an SC-55 ROM set in '%s'.",
						  common::ToCString(load_error), directory);
			}
			delete resource;
			return nullptr;
		}
	} catch (const std::exception& exception) {
		set_error(error, error_capacity, "Could not load SC-55 ROMs from '%s': %s", directory,
				  exception.what());
		delete resource;
		return nullptr;
	}
	if (error && error_capacity)
		error[0] = '\0';
	return resource;
}

extern "C" const char* imuse_nuked_sc55_romset_name(const ImuseNukedSc55Romset* romset) {
	return romset && !romset->loaded.picked_name.empty() ? romset->loaded.picked_name.c_str() : nullptr;
}

extern "C" void imuse_nuked_sc55_romset_release(ImuseNukedSc55Romset* romset) { delete romset; }

extern "C" ImuseMidiBackend* imuse_nuked_sc55_backend_create(const ImuseNukedSc55Romset* romset) {
	if (!romset)
		return nullptr;
	auto* backend = new (std::nothrow) NukedSc55Backend;
	if (!backend)
		return nullptr;
	backend->romset = romset;
	backend->vtable.open = vt_open;
	backend->vtable.close = vt_close;
	backend->vtable.release = vt_release;
	backend->vtable.program_change = vt_program_change;
	backend->vtable.note_on = vt_note_on;
	backend->vtable.note_off = vt_note_off;
	backend->vtable.control_change = vt_control_change;
	backend->vtable.pitch_bend = vt_pitch_bend;
	backend->vtable.render_float = vt_render_float;
	return &backend->vtable;
}

#else

extern "C" int imuse_nuked_sc55_backend_available(void) { return 0; }

extern "C" ImuseNukedSc55Romset* imuse_nuked_sc55_romset_load(const char*, char* error,
															  size_t error_capacity) {
	if (error && error_capacity)
		std::snprintf(error, error_capacity, "Nuked SC-55 support is unavailable in this build.");
	return nullptr;
}

extern "C" const char* imuse_nuked_sc55_romset_name(const ImuseNukedSc55Romset*) { return nullptr; }

extern "C" void imuse_nuked_sc55_romset_release(ImuseNukedSc55Romset*) {}

extern "C" ImuseMidiBackend* imuse_nuked_sc55_backend_create(const ImuseNukedSc55Romset*) { return nullptr; }

#endif
