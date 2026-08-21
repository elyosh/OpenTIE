#include "imgbake/anim.h"

#include "imgbake/byteio.h"

#include <stdlib.h>
#include <string.h>

void anim_free(AnimImage* a) {
	if (a->frames) {
		for (int i = 0; i < a->count; i++)
			image_free(&a->frames[i]);
		free(a->frames);
	}
	memset(a, 0, sizeof(*a));
}

bool decode_anim(AnimImage* out, const uint8_t* data, uint32_t size) {
	memset(out, 0, sizeof(*out));
	if (size < 2)
		return false;
	int n = rd_u16(data);
	if (n <= 0 || n > 4096)
		return false;
	out->count = n;
	out->frames = (Image8*)calloc((size_t)n, sizeof(Image8));
	if (!out->frames)
		return false;

	size_t off = 2;
	for (int i = 0; i < n; i++) {
		if (off + 4 > size) {
			anim_free(out);
			return false;
		}
		uint32_t flen = rd_u32(data + off);
		off += 4;
		if (flen == 0)
			continue; /* empty frame */
		if (off + flen > size) {
			anim_free(out);
			return false;
		}
		if (!decode_delt(&out->frames[i], data + off, flen)) {
			anim_free(out);
			return false;
		}
		off += flen;
	}
	return true;
}
