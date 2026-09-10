#include "tie_formats/xact.h"
#include "aeron/asset/act.h"
#include "tie_formats/internal.h"
#include <stdlib.h>
#include <string.h>

bool TieXact_DecodeRgba8(const void* bytes, size_t size, TieRgbaFrames* out, TieFormatError* error) {
	if (!out)
		return TieFormat_SetError(error, 69, "invalid XACT output");
	memset(out, 0, sizeof *out);
	AeronIndexedFrames decoded = { 0 };
	if (!AeronAct_Decode(bytes, size, &decoded, error))
		return false;
	out->frames = calloc(decoded.count, sizeof *out->frames);
	if (!out->frames)
		goto oom;
	out->count = decoded.count;
	for (uint16_t i = 0; i < decoded.count; ++i) {
		const AeronIndexedFrame* source = &decoded.frames[i];
		TieRgbaFrame* frame = &out->frames[i];
		const size_t pixels = (size_t)source->width * source->height;
		frame->rgba = calloc(pixels, 4);
		if (!frame->rgba)
			goto oom;
		frame->width = source->width;
		frame->height = source->height;
		frame->anchor_x = source->anchor_x;
		frame->anchor_y = source->anchor_y;
		frame->stable_id = source->frame_index;
		for (size_t p = 0; p < pixels; ++p) {
			if (!source->coverage[p])
				continue;
			memcpy(frame->rgba + p * 4, source->palette[source->indices[p]], 3);
			frame->rgba[p * 4 + 3] = source->coverage[p];
		}
	}
	AeronIndexedFrames_Free(&decoded);
	return true;
oom:
	AeronIndexedFrames_Free(&decoded);
	TieRgbaFrames_Free(out);
	return TieFormat_SetError(error, 71, "XACT RGBA allocation failed");
}
