#include "tie_formats/cockpit.h"
#include "tie_formats/internal.h"

#include <stdlib.h>
#include <string.h>

static uint8_t TieCockpit_ExpandDac6(uint8_t value) {
	value &= 0x3f;
	return (uint8_t)((value << 2) | (value >> 4));
}

bool TieCockpitPalette_Build(const void* vga_data, size_t vga_size, const void* pltt_data, size_t pltt_size,
							 uint8_t palette[768], TieFormatError* error) {
	const uint8_t *vga = vga_data, *pltt = pltt_data;
	if (!vga || vga_size < 576 || !pltt || pltt_size < 194 || !palette)
		return TieFormat_SetError(error, 50, "cockpit palette input is truncated");
	memset(palette, 0, 768);
	for (int index = 0; index < 192; ++index)
		for (int channel = 0; channel < 3; ++channel)
			palette[(64 + index) * 3 + channel] = TieCockpit_ExpandDac6(vga[index * 3 + channel]);
	for (int index = 0; index < 64; ++index)
		for (int channel = 0; channel < 3; ++channel)
			palette[index * 3 + channel] =
				TieCockpit_ExpandDac6((uint8_t)(pltt[2 + index * 3 + channel] >> 2));
	return true;
}

static int32_t TieCockpit_ReadMaskDelta(const uint8_t** cursor, const uint8_t* end, int extension) {
	if (*cursor == end)
		return -1;
	uint32_t value = *(*cursor)++;
	if (!value) {
		if (*cursor == end)
			return -1;
		value = *(*cursor)++;
		if (!value) {
			if (*cursor == end)
				return -1;
			value = *(*cursor)++ + (uint32_t)(extension * 2);
		} else
			value += (uint32_t)extension;
	}
	return (int32_t)value;
}

bool TieCockpitMask_Apply(const void* data, size_t size, int viewport_x, int viewport_y, int viewport_width,
						  int viewport_height, int image_width, int image_height, uint8_t* rgba,
						  int* out_rows, TieFormatError* error) {
	if (out_rows)
		*out_rows = 0;
	if (!data || !rgba || viewport_width <= 0 || viewport_height <= 0 || image_width <= 0 ||
		image_height <= 0 || viewport_x < 0 || viewport_y < 0 || viewport_width > image_width ||
		viewport_height > image_height || viewport_x > image_width - viewport_width ||
		viewport_y > image_height - viewport_height)
		return TieFormat_SetError(error, 51, "invalid cockpit mask viewport");
	const uint8_t *cursor = data, *end = cursor + size;
	for (int y = 0; y < viewport_height; ++y) {
		if (cursor == end) {
			if (out_rows)
				*out_rows = y;
			return true;
		}
		int8_t state = (int8_t)*cursor++;
		int x = 0;
		while (x < viewport_width) {
			const int32_t delta = TieCockpit_ReadMaskDelta(&cursor, end, 256);
			/* Short source masks are valid; remaining rows stay opaque. */
			if (delta <= 0)
				return true;
			const int visible = delta < viewport_width - x ? delta : viewport_width - x;
			if (state >= 0) {
				for (int offset = 0; offset < visible; ++offset)
					rgba[((size_t)(viewport_y + y) * image_width + (viewport_x + x + offset)) * 4 + 3] = 0;
			}
			/* Source rows may cross the right edge; the classic renderer clips. */
			x += visible;
			state = (int8_t)-state;
		}
		if (out_rows)
			*out_rows = y + 1;
	}
	return true;
}

bool TieCockpitBase_Build(const void* panl, size_t panl_size, const void* mask, size_t mask_size,
						  const void* pltt, size_t pltt_size, const void* vga_pac, size_t vga_pac_size,
						  int viewport_x, int viewport_y, int viewport_width, int viewport_height,
						  TieRgbaFrame* out, uint8_t out_palette[768], int* out_complete_rows,
						  TieFormatError* error) {
	if (out)
		memset(out, 0, sizeof *out);
	if (!out || !out_palette)
		return TieFormat_SetError(error, 59, "invalid cockpit base output");
	int width = 0, height = 0;
	if (!TieCockpitPalette_Build(vga_pac, vga_pac_size, pltt, pltt_size, out_palette, error) ||
		!TieShape_Measure(panl, panl_size, &width, &height, NULL, error))
		return false;
	if ((size_t)width > SIZE_MAX / (size_t)height || (size_t)width * (size_t)height > SIZE_MAX / 4)
		return TieFormat_SetError(error, 59, "cockpit base allocation overflows");
	uint8_t* rgba = calloc((size_t)width * (size_t)height, 4);
	if (!rgba)
		return TieFormat_SetError(error, 59, "cockpit base allocation failed");
	if (!TieShape_RasterizeRgba8(panl, panl_size, width, height, out_palette, 253, 0x100, rgba, error) ||
		!TieCockpitMask_Apply(mask, mask_size, viewport_x, viewport_y, viewport_width, viewport_height, width,
							  height, rgba, out_complete_rows, error)) {
		free(rgba);
		return false;
	}
	out->rgba = rgba;
	out->width = (uint16_t)width;
	out->height = (uint16_t)height;
	return true;
}

void TieCockpitCoverage_Free(TieCockpitCoverage* coverage) {
	if (!coverage)
		return;
	free(coverage->rects);
	memset(coverage, 0, sizeof *coverage);
}

bool TieCockpitCoverage_Build(const uint8_t* rgba, int width, int height, TieCockpitCoverage* out,
							  TieFormatError* error) {
	if (out)
		memset(out, 0, sizeof *out);
	if (!rgba || !out || width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX)
		return TieFormat_SetError(error, 90, "invalid coverage bitmap");
	const int columns = (width + 3) / 4;
	const int rows = (height + 3) / 4;
	if ((size_t)columns > SIZE_MAX / (size_t)rows)
		return TieFormat_SetError(error, 91, "coverage grid overflows");
	const size_t cell_count = (size_t)columns * (size_t)rows;
	uint8_t* cells = calloc(cell_count, 1);
	TieCockpitCoverageRect* rects = calloc(cell_count, sizeof *rects);
	if (!cells || !rects) {
		free(cells);
		free(rects);
		return TieFormat_SetError(error, 92, "coverage allocation failed");
	}
	for (int cell_y = 0; cell_y < rows; ++cell_y)
		for (int cell_x = 0; cell_x < columns; ++cell_x)
			for (int y = cell_y * 4; y < height && y < cell_y * 4 + 4; ++y)
				for (int x = cell_x * 4; x < width && x < cell_x * 4 + 4; ++x)
					if (rgba[((size_t)y * (size_t)width + (size_t)x) * 4 + 3])
						cells[(size_t)cell_y * (size_t)columns + (size_t)cell_x] = 1;
	uint32_t count = 0;
	for (int y = 0; y < rows; ++y) {
		for (int x = 0; x < columns;) {
			if (!cells[(size_t)y * (size_t)columns + (size_t)x]) {
				++x;
				continue;
			}
			int run_width = 1;
			while (x + run_width < columns && cells[(size_t)y * (size_t)columns + (size_t)(x + run_width)])
				++run_width;
			int run_height = 1;
			for (; y + run_height < rows; ++run_height) {
				bool full = true;
				for (int column = 0; column < run_width; ++column)
					if (!cells[(size_t)(y + run_height) * (size_t)columns + (size_t)(x + column)])
						full = false;
				if (!full)
					break;
			}
			for (int row = 0; row < run_height; ++row)
				memset(cells + (size_t)(y + row) * (size_t)columns + (size_t)x, 0, (size_t)run_width);
			rects[count++] = (TieCockpitCoverageRect) {
				.x = (int16_t)(x * 4),
				.y = (int16_t)(y * 4),
				.w = (int16_t)((x * 4 + run_width * 4 > width) ? width - x * 4 : run_width * 4),
				.h = (int16_t)((y * 4 + run_height * 4 > height) ? height - y * 4 : run_height * 4),
			};
			x += run_width;
		}
	}
	free(cells);
	out->rects = rects;
	out->count = count;
	out->image_width = (uint16_t)width;
	out->image_height = (uint16_t)height;
	return true;
}

static void TieCockpit_MarkPartFrames(uint16_t* indices, uint32_t count, int start, int frame_count,
									  uint16_t value) {
	for (int frame = 0; frame < frame_count; ++frame)
		if (start + frame >= 0 && (uint32_t)(start + frame) < count)
			indices[start + frame] = value;
}

bool TieCockpitPartTransparency_Build(const TieCockpitPartInstrument* instruments, uint32_t instrument_count,
									  uint32_t frame_count, uint16_t* indices, TieFormatError* error) {
	if (!instruments || instrument_count < 92 || !frame_count || !indices)
		return TieFormat_SetError(error, 93, "invalid cockpit part transparency input");
	for (uint32_t index = 0; index < frame_count; ++index)
		indices[index] = 253;
	for (uint32_t index = 0; index < 92; ++index) {
		const TieCockpitPartInstrument* instrument = &instruments[index];
		if (!(instrument->x | instrument->y))
			continue;
		const int first = instrument->param1;
		const uint16_t skip = instrument->param2;
		if (index >= 3 && index <= 10)
			TieCockpit_MarkPartFrames(indices, frame_count, first, 3, 253);
		else if (index >= 26 && index <= 29)
			TieCockpit_MarkPartFrames(indices, frame_count, first, 2, 253);
		else if (index >= 45 && index <= 57) {
			TieCockpit_MarkPartFrames(indices, frame_count, first, 1, skip);
			TieCockpit_MarkPartFrames(indices, frame_count, first + 13, 1, skip);
		} else if (index == 35) {
			/* panel_updatebeam draws nine consecutive arc cels with this skip color. */
			TieCockpit_MarkPartFrames(indices, frame_count, first, 9, skip);
		} else if ((index >= 11 && index <= 14) || (index >= 19 && index <= 23) || index == 31 ||
				   index == 36 || (index >= 37 && index <= 44) || (index >= 66 && index <= 68) ||
				   (index >= 73 && index <= 76) || (index >= 83 && index <= 85) || index == 91)
			TieCockpit_MarkPartFrames(indices, frame_count, first, (index >= 19 && index <= 22) ? 1 : 5,
									  skip);
	}
	return true;
}

bool TieCockpitShapeFrames_Build(const TieShapeList* shapes, const uint8_t palette[768],
								 const uint16_t* transparent_indices, uint16_t transparent_index_alt,
								 TieRgbaFrames* out, TieFormatError* error) {
	if (out)
		memset(out, 0, sizeof *out);
	if (!shapes || !shapes->shapes || !shapes->count || shapes->count > UINT16_MAX || !palette ||
		!transparent_indices || !out)
		return TieFormat_SetError(error, 94, "invalid shape frame input");
	out->frames = calloc(shapes->count, sizeof *out->frames);
	if (!out->frames)
		return TieFormat_SetError(error, 95, "shape frame allocation failed");
	out->count = (uint16_t)shapes->count;
	for (uint32_t index = 0; index < shapes->count; ++index) {
		int width = 0, height = 0;
		if (!TieShape_Measure(shapes->shapes[index].data, shapes->shapes[index].size, &width, &height, NULL,
							  error))
			goto failed;
		if ((size_t)width > SIZE_MAX / (size_t)height || (size_t)width * (size_t)height > SIZE_MAX / 4) {
			TieFormat_SetError(error, 96, "shape frame %u allocation overflows", index);
			goto failed;
		}
		TieRgbaFrame* frame = &out->frames[index];
		frame->rgba = calloc((size_t)width * (size_t)height, 4);
		if (!frame->rgba) {
			TieFormat_SetError(error, 97, "shape frame %u allocation failed", index);
			goto failed;
		}
		frame->width = (uint16_t)width;
		frame->height = (uint16_t)height;
		frame->stable_id = (uint16_t)index;
		if (!TieShape_RasterizeRgba8(shapes->shapes[index].data, shapes->shapes[index].size, width, height,
									 palette, transparent_indices[index], transparent_index_alt, frame->rgba,
									 error))
			goto failed;
	}
	return true;

failed:
	TieRgbaFrames_Free(out);
	return false;
}

bool TieCockpitCrtMask_Decode(const void* data, size_t size, uint8_t** out_rgba, int* out_width,
							  int* out_height, TieFormatError* error) {
	const uint8_t *bytes = data, *cursor = bytes, *end = bytes + size;
	if (out_rgba)
		*out_rgba = NULL;
	if (!bytes || !out_rgba || !out_width || !out_height)
		return TieFormat_SetError(error, 53, "invalid CRT mask input");
	int width = 0, height = 0;
	while (cursor < end && *cursor) {
		++cursor;
		int row_width = 0;
		while (row_width < (width ? width : 0x4000)) {
			int32_t delta = TieCockpit_ReadMaskDelta(&cursor, end, 255);
			if (delta <= 0)
				return TieFormat_SetError(error, 54, "truncated CRT mask");
			row_width += delta;
			if (!width) {
				for (int part = 0; part < 2; ++part) {
					delta = TieCockpit_ReadMaskDelta(&cursor, end, 255);
					if (delta <= 0)
						return TieFormat_SetError(error, 54, "truncated CRT mask");
					row_width += delta;
				}
				width = row_width;
			}
		}
		if (row_width != width || ++height > 4096)
			return TieFormat_SetError(error, 55, "inconsistent CRT mask geometry");
	}
	if (width <= 0 || width > 4096 || height <= 0)
		return TieFormat_SetError(error, 56, "empty CRT mask");
	uint8_t* rgba = calloc((size_t)width * height, 4);
	if (!rgba)
		return TieFormat_SetError(error, 57, "CRT mask allocation failed");
	cursor = bytes;
	for (int y = 0; y < height; ++y) {
		int8_t state = (int8_t)*cursor++;
		int x = 0;
		while (x < width) {
			const int32_t delta = TieCockpit_ReadMaskDelta(&cursor, end, 255);
			if (delta <= 0 || delta > width - x) {
				free(rgba);
				return TieFormat_SetError(error, 58, "invalid CRT mask run");
			}
			if (state >= 0)
				for (int offset = 0; offset < delta; ++offset)
					rgba[((size_t)y * width + x + offset) * 4 + 3] = 255;
			x += delta;
			state = (int8_t)-state;
		}
	}
	*out_rgba = rgba;
	*out_width = width;
	*out_height = height;
	return true;
}
