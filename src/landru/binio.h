#ifndef LANDRU_BINIO_H
#define LANDRU_BINIO_H

#include <stdint.h>

static inline uint16_t br_u16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static inline int16_t br_i16le(const uint8_t* p) { return (int16_t)br_u16le(p); }

static inline uint32_t br_u32le(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#endif
