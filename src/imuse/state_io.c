#include <imuse/state.h>
#include <imuse/version.h>

#include "internal/debug.h"
#include "internal/fades.h"
#include "internal/midi.h"
#include "internal/state.h"
#include "internal/triggers.h"
#include "internal/wave.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * libimuse — versioned engine-state snapshot (I7 public surface).
 *
 * Layered on top of the existing per-subsystem Save/Restore chain
 * (FADES → TRIGGERS → MIDI → WAVE) — adds a 16-byte versioned header
 * so a stale snapshot from a different build / config can be
 * rejected up front.
 *
 * The legacy public API ImCommands_Save / ImCommands_Restore is gone
 * from <imuse/commands.h>; the same internal chain is now reached via
 * imuse_state_save / imuse_state_load (versioned wire format) or
 * imuse_ImSave / imuse_ImRestore (legacy LOLEVEL ABI wrappers — same
 * format).
 *
 * Wire format: see docs/imuse-state-format.md.
 */

/* Header byte offsets — also documented in docs/imuse-state-format.md. */
#define IMUSE_STATE_HDR_MAGIC_OFF 0u
#define IMUSE_STATE_HDR_FORMAT_OFF 4u
#define IMUSE_STATE_HDR_ABI_OFF 8u
#define IMUSE_STATE_HDR_MIXCOUNT_OFF 12u

static void write_u32_le(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
	p[2] = (uint8_t)((v >> 16) & 0xFFu);
	p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t read_u32_le(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static size_t imuse_payload_size(imuse_t* im) {
	return (size_t)ImFades_GetSaveSize(im) + (size_t)ImTriggers_GetSaveSize(im) + sizeof(im->seq.data) +
		   sizeof(im->players.players) + sizeof(im->dispatch.pool) + sizeof(im->tracks.pool);
}

size_t imuse_state_query_size(imuse_t* im) {
	if (!im)
		return 0;
	return (size_t)IMUSE_STATE_HEADER_SIZE + imuse_payload_size(im);
}

size_t imuse_state_save(imuse_t* im, void* buf, size_t size) {
	if (!im || !buf)
		return 0;

	size_t needed = imuse_state_query_size(im);
	if (size < needed) {
		ImDebug_LogError(im, "state_save: buffer too small (need %zu, have %zu)", needed, size);
		return 0;
	}

	uint8_t* p = (uint8_t*)buf;
	write_u32_le(p + IMUSE_STATE_HDR_MAGIC_OFF, IMUSE_STATE_MAGIC);
	write_u32_le(p + IMUSE_STATE_HDR_FORMAT_OFF, IMUSE_STATE_FORMAT_VERSION);
	write_u32_le(p + IMUSE_STATE_HDR_ABI_OFF, (uint32_t)IMUSE_ABI_VERSION);
	write_u32_le(p + IMUSE_STATE_HDR_MIXCOUNT_OFF, (uint32_t)im->tracks.waveMixCount);

	/* Subsystem chain — each call writes its own slice and returns
	 * the byte count (negative on error, but with the buffer-size
	 * already verified at the top, only a config-mismatch could
	 * still trip them). */
	size_t off = IMUSE_STATE_HEADER_SIZE;
	int rc;

	rc = ImFades_Save(im, p + off, (int)(size - off));
	if (rc < 0)
		return 0;
	off += (size_t)rc;

	rc = ImTriggers_Save(im, p + off, (int)(size - off));
	if (rc < 0)
		return 0;
	off += (size_t)rc;

	rc = ImMidi_Save(im, p + off, (int)(size - off));
	if (rc < 0)
		return 0;
	off += (size_t)rc;

	rc = ImWave_Save(im, p + off, (int)(size - off));
	if (rc < 0)
		return 0;
	off += (size_t)rc;

	return off;
}

int imuse_state_load(imuse_t* im, void* buf, size_t size) {
	if (!im || !buf || size < IMUSE_STATE_HEADER_SIZE) {
		if (im)
			ImDebug_LogError(im, "state_load: NULL input or truncated header");
		return 0;
	}

	uint8_t* p = (uint8_t*)buf;
	uint32_t magic = read_u32_le(p + IMUSE_STATE_HDR_MAGIC_OFF);
	uint32_t fmt = read_u32_le(p + IMUSE_STATE_HDR_FORMAT_OFF);
	uint32_t abi = read_u32_le(p + IMUSE_STATE_HDR_ABI_OFF);
	uint32_t mix = read_u32_le(p + IMUSE_STATE_HDR_MIXCOUNT_OFF);

	if (magic != IMUSE_STATE_MAGIC) {
		ImDebug_LogError(im, "state_load: bad magic 0x%08x", magic);
		return 0;
	}
	if (fmt != IMUSE_STATE_FORMAT_VERSION) {
		ImDebug_LogError(im, "state_load: format-version mismatch (blob %u, library %u)", fmt,
						 IMUSE_STATE_FORMAT_VERSION);
		return 0;
	}
	if (abi != (uint32_t)IMUSE_ABI_VERSION) {
		ImDebug_LogError(im, "state_load: ABI mismatch (blob %u, library %u)", abi,
						 (uint32_t)IMUSE_ABI_VERSION);
		return 0;
	}
	if (mix != (uint32_t)im->tracks.waveMixCount) {
		ImDebug_LogError(im, "state_load: waveMixCount mismatch (blob %u, engine %d)", mix,
						 im->tracks.waveMixCount);
		return 0;
	}

	size_t expected = imuse_state_query_size(im);
	if (size < expected) {
		ImDebug_LogError(im, "state_load: payload truncated (need %zu, have %zu)", expected, size);
		return 0;
	}

	/* Drain live state before stamping the snapshot in. ImPlayers_Restore
	 * uses the load buffer as scratch during its sequencer cursor
	 * replay, hence the non-const buf parameter on the public API. */
	imuse_stop_all_sounds(im);

	size_t off = IMUSE_STATE_HEADER_SIZE;
	off += (size_t)ImFades_Restore(im, p + off);
	off += (size_t)ImTriggers_Restore(im, p + off);
	off += (size_t)ImMidi_Restore(im, p + off);
	off += (size_t)ImWave_Restore(im, p + off);

	return 1;
}
