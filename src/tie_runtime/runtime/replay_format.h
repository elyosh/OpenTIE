#ifndef TIE_REPLAY_FORMAT_H
#define TIE_REPLAY_FORMAT_H

#include <stdint.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

/*
 * Replay-file format header.
 *
 * Fronts both the per-mission input spool (`input.spl`, written by
 * `replayio_spoolreplayinput`) and the saved replay clip (`*.clp`,
 * written by `replay_savereplay_file`). The header rejects records created with
 * an incompatible payload or model index space.
 *
 * Layout (all little-endian, 16 bytes total):
 *
 *     offset  0:  magic[4]      = 'T','I','E','R'
 *     offset  4:  version       u16
 *     offset  6:  flags         u16   (bits 0-1: flight version)
 *     offset  8:  frame_size    u16   (per-tick payload size in bytes)
 *     offset 10:  header_size   u16   (= sizeof(this struct))
 *     offset 12:  step_ticks    u8
 *     offset 13:  compat_ticks  u8
 *     offset 14:  reserved[2]   = 0
 *
 * V5 wire format:
 *   - frame_size = 14 — every record in the stream is a fixed
 *     14-byte slot. The per-tick input frame is:
 *       offset  0:  u32 delta_us   (complete admitted interval)
 *       offset  4:  u16 key        (inputkey)
 *       offset  6:  i16 deltax     (joystick/mouse pitch delta)
 *       offset  8:  i16 deltay     (joystick/mouse roll delta)
 *       offset 10:  u8  buttons    (inputbuttons & 0xFF)
 *       offset 11:  u8  frameticks (engine-counted PIT ticks since
 *                                   previous frame)
 *       offset 12:  i16 deltaroll   (second-stick roll delta)
 *     The same 14-byte slot also carries `user_inflightinfo`'s side-
 *     payload chunks (4 slots emitted per info-room open/close pair),
 *     each padded out to the fixed record size.
 *   - .clp file: header → u32 totalcnt → u16 randomseed → state blob
 *     (savearrayptrs/sizes + fg/radiomsg/cut/fgstatus/species/camera +
 *     modern timing checkpoint) →
 *     N x 14-byte records
 *   - .spl file: header → N x 14-byte records
 *
 * `delta_us` is the complete synthetic interval represented by this admitted
 * frame. Playback feeds it to every synthetic-clock consumer instead of the
 * host delta. V4 used the last host slice and is rejected because its
 * checkpoint and timing semantics cannot reproduce V5 deterministically.
 *
 * Files without this header are rejected.
 */

#define REPLAY_FORMAT_MAGIC0 'T'
#define REPLAY_FORMAT_MAGIC1 'I'
#define REPLAY_FORMAT_MAGIC2 'E'
#define REPLAY_FORMAT_MAGIC3 'R'

#define REPLAY_FORMAT_VERSION 5u
#define REPLAY_FORMAT_MIN_READ_VERSION 5u
#define REPLAY_FORMAT_HEADER_SIZE 16u
#define REPLAY_FORMAT_FRAME_SIZE 14u

#define REPLAY_FORMAT_FLIGHT_VERSION_MASK 0x0003u
#define REPLAY_FORMAT_UPDATE_RATE_SHIFT 2u
#define REPLAY_FORMAT_UPDATE_RATE_MASK 0x000Cu

typedef struct TieReplayFormatMetadata {
	uint16_t version;
	TieGameVersion flight_version;
	TieFlightUpdateRate update_rate;
	uint8_t step_ticks;
	uint8_t compatibility_ticks;
} TieReplayFormatMetadata;

/* Write the current header to fp at the current file position. Returns 1 on
 * success, 0 on I/O failure. The fp is left positioned right after
 * the header on success; on failure the file position is undefined. */
int TieReplayFormat_WriteHeader(TieFile* fp);

/* Read a header from fp at the current file position and validate
 * magic + version + frame_size against what this build understands.
 * Returns 1 if the header is well-formed and matches; 0 on EOF, I/O
 * error, magic mismatch, or unsupported version / frame_size. */
int TieReplayFormat_ReadHeader(TieFile* fp, TieReplayFormatMetadata* metadata);

#endif /* TIE_REPLAY_FORMAT_H */
