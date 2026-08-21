#ifndef LANDRU_SURFACE_H
#define LANDRU_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/bitmap.h>
#include <landru/rect.h>

typedef enum LandruSurfaceSet {
	LANDRU_SURFACE_VGA = 0,
	LANDRU_SURFACE_SVGA = 1,
} LandruSurfaceSet;

typedef struct LandruVideoTarget {
	uint8_t* pixels;
	int16_t width;
	int16_t height;
	int16_t stride;
	Rect bounds;
	uint32_t generation;
	bool dirty;
} LandruVideoTarget;

/* Creates the session-lifetime surface selector after canvas and VESA setup. */
bool lsurface_Create_Surface_Module(bool create_secondary_vga);
void lsurface_Destroy_Surface_Module(void);

bool lsurface_Select_Surface_Set(LandruSurfaceSet set);
LandruSurfaceSet lsurface_Get_Surface_Set(void);
bool lsurface_Has_Surface_Set(LandruSurfaceSet set);

BitmapStruct* lsurface_Get_Active_Render_Bitmap(void);
BitmapStruct* lsurface_Get_Active_Diff_Bitmap(void);
bool lsurface_Get_Active_Video_Target(LandruVideoTarget* out);
void lsurface_Get_Logical_Bounds(Rect* out);

void lsurface_Invalidate_Presentation(void);
void lsurface_Mark_Active_Video_Dirty(void);
bool lsurface_Take_Active_Video_Dirty(void);
uint32_t lsurface_Get_Presentation_Generation(void);

#endif
