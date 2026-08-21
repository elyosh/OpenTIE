#include "tie/model_texture_tie98.h"

#include "tie/rtsvga2.h"

#include <string.h>

// FUNCTION: TIE98 0x4330E0
void ModelTexture_BuildPalettedShadeTable(uint8_t* dst, const uint8_t* rgb24, int width, int height) {
	uint8_t rgb555_palette[256][3];
	const int pixel_count = width * height;
	unsigned int palette_count = 1;

	// PORT: the binary reads unused stack palette slots whose values cannot affect referenced texels.
	memset(rgb555_palette, 0, sizeof rgb555_palette);
	rgb555_palette[0][0] = rgb24[0] >> 3;
	rgb555_palette[0][1] = rgb24[1] >> 3;
	rgb555_palette[0][2] = rgb24[2] >> 3;
	dst[0] = 0;
	for (int pixel = 1; pixel < pixel_count; ++pixel) {
		uint8_t rgb555[3] = {
			rgb24[3 * pixel] >> 3,
			rgb24[3 * pixel + 1] >> 3,
			rgb24[3 * pixel + 2] >> 3,
		};
		uint8_t index = (uint8_t)rtsvga2_findNearestColor(rgb555, &rgb555_palette[0][0], 0, palette_count);
		if ((rgb555_palette[index][0] != rgb555[0] || rgb555_palette[index][1] != rgb555[1] ||
			 rgb555_palette[index][2] != rgb555[2]) &&
			palette_count < 256) {
			index = (uint8_t)palette_count++;
			rgb555_palette[index][0] = rgb555[0];
			rgb555_palette[index][1] = rgb555[1];
			rgb555_palette[index][2] = rgb555[2];
		}
		dst[pixel] = index;
	}

	uint8_t* software_shades = dst + pixel_count;
	uint16_t* hardware_shades = (uint16_t*)(software_shades + 4096);
	for (int color = 0; color < 256; ++color) {
		for (int shade = 0; shade < 16; ++shade) {
			uint8_t rgb5[3];
			for (int component = 0; component < 3; ++component) {
				const unsigned int value = rgb555_palette[color][component];
				if (shade < 8)
					rgb5[component] = (uint8_t)(((value << 7) + ((shade * (value << 8)) >> 4)) >> 8);
				else
					rgb5[component] = (uint8_t)(((value << 8) + 32 * (shade - 8) * (31 - value)) >> 8);
			}
			hardware_shades[shade * 256 + color] = (uint16_t)(rgb5[2] + ((rgb5[1] + 32 * rgb5[0]) << 6));
			uint8_t rgb6[3] = {
				(uint8_t)(2 * rgb5[0]),
				(uint8_t)(2 * rgb5[1]),
				(uint8_t)(2 * rgb5[2]),
			};
			software_shades[shade * 256 + color] =
				(uint8_t)rtsvga2_findNearestColor(rgb6, rtsvga2_vgapalette, 0x40, 0x100);
		}
	}
}
