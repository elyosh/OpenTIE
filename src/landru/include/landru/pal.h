#ifndef LANDRU_PAL_H
#define LANDRU_PAL_H

#include <stdbool.h>
#include <stdint.h>

#define LANDRU_VGA_RETRACE_PERIOD_US 14286u

typedef struct {
	int16_t dir;
	int16_t rate;
	uint8_t active;
	uint8_t current;
	uint8_t low;
	uint8_t high;
} CycleStruct;

typedef struct {
	uint8_t r, g, b;
} RGBStruct;

typedef struct Palette Palette;

struct Palette {
	uint32_t res_type;
	uint8_t res_name[8];
	Palette* next;
	RGBStruct* colors;
	CycleStruct cycles[4];
	uint8_t cycle_count;
	uint8_t cycle_active;
	int16_t start;
	int16_t end;
	int16_t len;
	uint16_t flags;
	void* varptr;
	int16_t varhdl;
};

void lpal_Set_VGA_Palette(RGBStruct* pal, int16_t start, int16_t len);
/* Delay until all changed VGA palette writes have crossed their original
 * retrace waits. UINT64_MAX means that no wait is pending. */
uint64_t lpal_Next_VGA_Delay_Us(void);
void lpal_Create_Palette_Module(void);
void lpal_Destroy_Palette_Module(void);
Palette* lpal_Ask_Palette_List(void);
void lpal_Add_Palette_To_System(Palette* pal);
void lpal_Free_Palette_From_System(Palette* pal);
Palette* lpal_Alloc_Palette(int16_t start, int16_t numColors);
void lpal_Free_Palette(Palette* pal);
void lpal_Free_Palettes(Palette* pal);
uint8_t* lpal_Alloc_RGB(int16_t numColors);
void lpal_Free_RGB(uint8_t* rgb);
Palette* lpal_Res_Palette(const char* palName);
void lpal_Set_Palette_Name(Palette* pal, uint32_t res_type, const char* pal_name);
void lpal_Clear_Palette(Palette* pal);
void lpal_Copy_Palette(Palette* dstPal, Palette* srcPal, int16_t start, int16_t length, int16_t offset);
void lpal_Set_Palette_RGB(Palette* pal, uint8_t r, uint8_t g, uint8_t b, int16_t start, int16_t length);
bool lpal_Compare_Palette(Palette* dstPal, Palette* srcPal, int16_t start, int16_t length, int16_t offset);
bool lpal_Compare_Palette_RGB(Palette* pal, uint8_t r, uint8_t g, uint8_t b, int16_t start, int16_t count);
bool lpal_Get_Palette_Index_RGB(Palette* pal, int16_t* r, int16_t* g, int16_t* b, int16_t color_idx);
void lpal_Set_Palette_Index_RGB(Palette* pal, uint8_t r, uint8_t g, uint8_t b, int16_t color_idx);
void lpal_Start_Cycle(Palette* pal);
void lpal_Stop_Cycle(Palette* pal);
void lpal_Cycle_Screen(void);
void lpal_Cycles_To_Start(void);
void lpal_Stop_All_Cycles(void);
void lpal_Set_Screen_Palette(Palette* pal);
bool lpal_Compare_Screen_Palette(Palette* pal);
void lpal_Set_Screen_RGB(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b);
bool lpal_Compare_Screen_RGB(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b);
void lpal_Put_Screen_Palette(void);
void lpal_Put_Screen_Pal_Range(int16_t start, int16_t len);
void lpal_Set_Video_Palette(Palette* pal, int16_t start, int16_t len, int16_t dstStart);
void lpal_Set_Cycle_Range(int16_t start, int16_t stop, int16_t offset);
void lpal_Restore_Screen_Palette(int16_t start, int16_t stop);
Palette* lpal_Get_Screen_Palette(void);
Palette* lpal_Get_Source_Palette(void);
Palette* lpal_Get_Dest_Palette(void);
Palette* lpal_Get_Work_Palette(void);
void lpal_Screen_To_Src_Palette(int16_t offset, int16_t start, int16_t stop);
void lpal_Src_To_Screen_Palette(int16_t offset, int16_t start, int16_t stop);
void lpal_Screen_To_Dest_Palette(int16_t offset, int16_t start, int16_t stop);
void lpal_Dest_To_Screen_Palette(int16_t offset, int16_t start, int16_t stop);
void lpal_Dest_To_Src_Palette(int16_t offset, int16_t start, int16_t stop);
void lpal_Src_To_Dest_Palette(int16_t offset, int16_t start, int16_t stop);
void lpal_Set_Source_Palette(Palette* pal);
bool lpal_Compare_Source_Palette(Palette* pal);
void lpal_Set_Src_Pal_Color(int16_t start, int16_t stop, uint8_t r, uint8_t g, uint8_t b);
bool lpal_Compare_Src_Pal_Color(int16_t start, int16_t stop, uint8_t r, uint8_t g, uint8_t b);
void lpal_Set_Dest_Palette(Palette* pal);
bool lpal_Compare_Dest_Palette(Palette* pal);
void lpal_Set_Dest_Pal_Color(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b);
bool lpal_Compare_Dest_Pal_Color(int16_t start, int16_t end, uint8_t r, uint8_t g, uint8_t b);
bool lpal_Compare_Src_Dest_Palette(void);
void lpal_Build_Fade_Palette(int16_t type, int16_t start, int16_t stop, int16_t step, int16_t fr, int16_t fg,
							 int16_t fb);
void lpal_Build_Palette_To_Palette(int16_t start, int16_t stop, int16_t step);
void lpal_Build_Palette_To_Mono(int16_t start, int16_t stop, int16_t step, int16_t fr, int16_t fg,
								int16_t fb);
void lpal_Build_Mono_To_Palette(int16_t start, int16_t stop, int16_t step, int16_t fr, int16_t fg,
								int16_t fb);

#endif
