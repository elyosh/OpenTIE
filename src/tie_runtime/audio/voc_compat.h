#ifndef TIE_RUNTIME_AUDIO_VOC_COMPAT_H
#define TIE_RUNTIME_AUDIO_VOC_COMPAT_H

#include <stddef.h>
#include <stdint.h>

typedef enum TieVocCompatResult {
	TIE_VOC_COMPAT_NOT_NEEDED = 0,
	TIE_VOC_COMPAT_CONVERTED = 1,
	TIE_VOC_COMPAT_INVALID = -1,
} TieVocCompatResult;

/* Convert the uncompressed type-9 VOC used by shipped TIE98 voices into the
 * type-1 container consumed by the recovered iMUSE dispatcher. The conversion
 * is in-place and only shrinks the buffer. source_rate_hz is non-zero only
 * when conversion occurred and must be applied to the iMUSE sound. */
TieVocCompatResult TieVocCompat_PrepareImuse(uint8_t* data, size_t* size, uint32_t* source_rate_hz);

#endif /* TIE_RUNTIME_AUDIO_VOC_COMPAT_H */
