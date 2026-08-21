#include <landru/pal.h>
#include <landru/remap.h>
#include <stdlib.h>

// GLOBAL: TIE 0xFBCB0
static int16_t remap_module_gbl;

// GLOBAL: TIE 0xD2EE8
int16_t remap_table[REMAP_COLOR_COUNT];

/* 37 fixed RGB colors (8-bit): 7 grays + 6 each of red/green/blue/magenta/yellow */
RGBStruct remap_colors[REMAP_COLOR_COUNT] = {
	{ 0, 0, 0 },       /* BLACK */
	{ 42, 42, 42 },    /* GRAY_1 */
	{ 84, 84, 84 },    /* GRAY_2 */
	{ 126, 126, 126 }, /* GRAY_3 */
	{ 168, 168, 168 }, /* GRAY_4 */
	{ 210, 210, 210 }, /* GRAY_5 */
	{ 255, 255, 255 }, /* WHITE */
	{ 42, 0, 0 },      /* RED_1 */
	{ 84, 0, 0 },      /* RED_2 */
	{ 126, 0, 0 },     /* RED_3 */
	{ 168, 0, 0 },     /* RED_4 */
	{ 210, 0, 0 },     /* RED_5 */
	{ 255, 0, 0 },     /* RED_6 */
	{ 0, 42, 0 },      /* GREEN_1 */
	{ 0, 84, 0 },      /* GREEN_2 */
	{ 0, 126, 0 },     /* GREEN_3 */
	{ 0, 168, 0 },     /* GREEN_4 */
	{ 0, 210, 0 },     /* GREEN_5 */
	{ 0, 255, 0 },     /* GREEN_6 */
	{ 0, 0, 42 },      /* BLUE_1 */
	{ 0, 0, 84 },      /* BLUE_2 */
	{ 0, 0, 126 },     /* BLUE_3 */
	{ 0, 0, 168 },     /* BLUE_4 */
	{ 0, 0, 210 },     /* BLUE_5 */
	{ 0, 0, 255 },     /* BLUE_6 */
	{ 42, 0, 42 },     /* MAGENTA_1 */
	{ 84, 0, 84 },     /* MAGENTA_2 */
	{ 126, 0, 126 },   /* MAGENTA_3 */
	{ 168, 0, 168 },   /* MAGENTA_4 */
	{ 210, 0, 210 },   /* MAGENTA_5 */
	{ 255, 0, 255 },   /* MAGENTA_6 */
	{ 42, 42, 0 },     /* YELLOW_1 */
	{ 84, 84, 0 },     /* YELLOW_2 */
	{ 126, 126, 0 },   /* YELLOW_3 */
	{ 168, 168, 0 },   /* YELLOW_4 */
	{ 210, 210, 0 },   /* YELLOW_5 */
	{ 255, 255, 0 },   /* YELLOW_6 */
};

void lremap_Create_Remap_Module(void) { remap_module_gbl = 1; }

void lremap_Destroy_Remap_Module(void) { remap_module_gbl = 0; }

void lremap_Remap_Interface(void) {
	Palette* pal;
	RGBStruct* pal_data;
	uint16_t sq_lut[256];
	int16_t i, j;
	int16_t lut_val;

	pal = lpal_Get_Screen_Palette();
	pal_data = pal->colors;
	if (!pal_data)
		return;

	/* Build squared-distance lookup: sq_lut[n] = n*n */
	sq_lut[0] = 0;
	lut_val = 0;
	for (i = 1; i < 256; i++) {
		lut_val += (2 * i - 1);
		sq_lut[i] = lut_val;
	}

	/* For each remap color, find the closest palette entry */
	for (i = 0; i < REMAP_COLOR_COUNT; i++) {
		uint8_t target_r = remap_colors[i].r >> 2;
		uint8_t target_g = remap_colors[i].g >> 2;
		uint8_t target_b = remap_colors[i].b >> 2;
		int16_t best_index = i;
		uint16_t best_dist = 4095;

		for (j = 0; j < 256; j++) {
			uint16_t dist;
			dist = sq_lut[abs(target_r - pal_data[j].r)] + sq_lut[abs(target_g - pal_data[j].g)] +
				   sq_lut[abs(target_b - pal_data[j].b)];
			if (dist < best_dist) {
				best_index = j;
				best_dist = dist;
			}
		}

		remap_table[i] = best_index;
	}
}

int16_t lremap_Get_Remap(RemapColor color) { return remap_table[color]; }
