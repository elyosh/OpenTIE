#ifndef LANDRU_REMAP_H
#define LANDRU_REMAP_H

#include <landru/pal.h>
#include <stdbool.h>
#include <stdint.h>

/* 37-entry color ramp: 7 grays + 5x6 hue ramps (red, green, blue, magenta, yellow) */
#define REMAP_COLOR_COUNT 37

typedef enum {
	REMAP_GRAY_0 = 0,
	REMAP_GRAY_1 = 1,
	REMAP_GRAY_2 = 2,
	REMAP_GRAY_3 = 3,
	REMAP_GRAY_4 = 4,
	REMAP_GRAY_5 = 5,
	REMAP_GRAY_6 = 6,
	REMAP_RED_1 = 7,
	REMAP_RED_2 = 8,
	REMAP_RED_3 = 9,
	REMAP_RED_4 = 10,
	REMAP_RED_5 = 11,
	REMAP_RED_6 = 12,
	REMAP_GREEN_1 = 13,
	REMAP_GREEN_2 = 14,
	REMAP_GREEN_3 = 15,
	REMAP_GREEN_4 = 16,
	REMAP_GREEN_5 = 17,
	REMAP_GREEN_6 = 18,
	REMAP_BLUE_1 = 19,
	REMAP_BLUE_2 = 20,
	REMAP_BLUE_3 = 21,
	REMAP_BLUE_4 = 22,
	REMAP_BLUE_5 = 23,
	REMAP_BLUE_6 = 24,
	REMAP_MAGENTA_1 = 25,
	REMAP_MAGENTA_2 = 26,
	REMAP_MAGENTA_3 = 27,
	REMAP_MAGENTA_4 = 28,
	REMAP_MAGENTA_5 = 29,
	REMAP_MAGENTA_6 = 30,
	REMAP_YELLOW_1 = 31,
	REMAP_YELLOW_2 = 32,
	REMAP_YELLOW_3 = 33,
	REMAP_YELLOW_4 = 34,
	REMAP_YELLOW_5 = 35,
	REMAP_YELLOW_6 = 36,
} RemapColor;

void lremap_Create_Remap_Module(void);
void lremap_Destroy_Remap_Module(void);
void lremap_Remap_Interface(void);
int16_t lremap_Get_Remap(RemapColor color);

extern int16_t remap_table[REMAP_COLOR_COUNT];
extern RGBStruct remap_colors[REMAP_COLOR_COUNT];

#endif
