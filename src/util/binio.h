#ifndef TIE_UTIL_BINIO_H
#define TIE_UTIL_BINIO_H

/*
 * binio.h -- endian-safe little-endian integer accessors for on-disk and
 * in-memory binary buffers.
 *
 * The DOS binary stores every multi-byte integer little-endian, both on
 * disk and in the buffers it streams to disk verbatim (replay tape, save
 * games, .TFR pilot saves, .TIE missions, etc.). To preserve that format
 * on big-endian hosts -- and to avoid undefined behaviour from unaligned
 * `*(int16_t *)p` casts on strict-alignment hosts -- every read/write of
 * a multi-byte field in a file-loaded struct goes through these helpers.
 *
 * The br_* and bw_* helpers read and write directly to a raw byte buffer at a
 * caller-supplied offset. File access remains the responsibility of the Aeron
 * VFS layer.
 *
 * On little-endian hosts the byte-shift expressions fold to a single
 * load/store; on big-endian or strict-align hosts they generate a
 * portable byte-by-byte sequence.
 */

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Pointer-based readers (buffer -> integer)
 * -------------------------------------------------------------------------- */

static inline uint8_t br_u8(const uint8_t* p) { return p[0]; }

static inline uint16_t br_u16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static inline int16_t br_i16le(const uint8_t* p) { return (int16_t)br_u16le(p); }

static inline uint32_t br_u32le(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int32_t br_i32le(const uint8_t* p) { return (int32_t)br_u32le(p); }

/* --------------------------------------------------------------------------
 * Pointer-based writers (integer -> buffer)
 * -------------------------------------------------------------------------- */

static inline void bw_u8(uint8_t* p, uint8_t v) { p[0] = v; }

static inline void bw_u16le(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)(v >> 8);
}

static inline void bw_i16le(uint8_t* p, int16_t v) { bw_u16le(p, (uint16_t)v); }

static inline void bw_u32le(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline void bw_i32le(uint8_t* p, int32_t v) { bw_u32le(p, (uint32_t)v); }

#endif /* TIE_UTIL_BINIO_H */
