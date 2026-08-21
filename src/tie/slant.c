#include "tie/slant.h"

#include "landru/bitmap.h"
#include "landru/canvas.h"

// FUNCTION: TIE 0x877E0
void slant_Scale_Line(void* bitmap_data, int16_t src_x, int16_t src_y, int16_t skip, int16_t skipf,
					  int16_t dst_x, int16_t dst_y, int16_t width, uint8_t color) {
	BitmapStruct* canvas_bm = lcanvas_Get_Current_Canvas_Bitmap();
	uint8_t* canvas_pixels = (uint8_t*)lbitmap_Lock_Bitmap(canvas_bm);

	uint8_t* dst = canvas_pixels + dst_y * 320 + dst_x;
	uint8_t* src = (uint8_t*)bitmap_data + src_y * 320 + src_x;

	int32_t frac = skip; /* integer part doubles as initial accumulator */

	if (width > 0) {
		do {
			/* Copy non-transparent pixels as the given color */
			if (*src)
				*dst = color;

			src++;                   /* always advance source by 1 */
			frac += (uint16_t)skipf; /* add fractional step */
			dst++;                   /* advance destination by 1 */

			if (frac > 0xFFFF) {
				src++; /* extra source advance on overflow */
				frac &= 0xFFFF;
			}

			src += skip; /* advance source by integer skip */
			width--;
		} while (width > 0);
	}

	lbitmap_Unlock_Bitmap(canvas_bm);
}
