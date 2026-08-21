#include "tie/stub.h"
#include "tie/rotpoly.h"

#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/rect.h"

/*
 * Both Copy_From and Copy_To work the same way:
 * 1. Get the drawing canvas clip rect
 * 2. Read the source rect dimensions from *src_rect
 * 3. Clip the operation against the canvas bounds
 * 4. Build a temporary BitmapStruct wrapping the flat buffer
 * 5. Call the appropriate canvas↔bitmap copy function
 *
 * The source rect (src_rect) is in buffer-local coordinates:
 *   left/top = offset into the buffer, right-left = width, bottom-top = height.
 * (screen_x, screen_y) is the canvas position.
 * (buf_w, buf_h) is the full buffer size (for the bitmap wrapper).
 */

// FUNCTION: TIE 0x870F0
int stub_Copy_From_Clipped_Buffer(void* buffer, Rect* src_rect, int16_t screen_x, int16_t screen_y,
								  int16_t buf_w, int16_t buf_h) {
	BitmapStruct bm;
	Rect canvas_clip;
	int16_t clip_w, clip_h;
	int16_t buf_left, buf_top;
	int16_t copy_w, copy_h;
	int16_t dest_x, dest_y;

	lcanvas_Get_Drawing_Canvas_Clip(&canvas_clip);
	clip_w = canvas_clip.right - canvas_clip.left;
	clip_h = canvas_clip.bottom - canvas_clip.top;

	buf_left = src_rect->left;
	buf_top = src_rect->top;
	copy_w = src_rect->right - buf_left;
	copy_h = src_rect->bottom - buf_top;
	dest_x = screen_x;

	/* Clip left edge */
	if (screen_x < canvas_clip.left) {
		int16_t overshoot = canvas_clip.left - screen_x;
		buf_left += overshoot;
		dest_x = canvas_clip.left;
		copy_w -= overshoot;
	}

	/* Clip right edge */
	if (dest_x + copy_w > clip_w + canvas_clip.left)
		copy_w = clip_w + canvas_clip.left - dest_x;

	/* Clip top edge */
	dest_y = screen_y;
	if (screen_y < canvas_clip.top) {
		dest_y = canvas_clip.top;
		copy_h -= canvas_clip.top - screen_y;
		buf_top += canvas_clip.top - screen_y;
	}

	/* Clip bottom edge */
	if (dest_y + copy_h > canvas_clip.top + clip_h)
		copy_h = clip_h + canvas_clip.top - dest_y;

	if (copy_w <= 0 || copy_h <= 0)
		return 0;

	/* Build temporary bitmap wrapping the buffer */
	bm.w = buf_w;
	bm.h = buf_h;
	lrect_Set_Rect(&bm.clip, 0, 0, buf_w, buf_h);
	bm.flags = 0;
	bm.type = 0;
	bm.offset = 0;
	bm.data = buffer;

	Rect copy_rect;
	lrect_Set_Rect(&copy_rect, buf_left, buf_top, copy_w + buf_left, buf_top + copy_h);
	lcanvas_Copy_Bitmap_Portion_To_Canvas(&bm, &copy_rect, dest_x, dest_y);
	return 1;
}

// FUNCTION: TIE 0x872C8
int stub_Copy_To_Clipped_Buffer(void* buffer, Rect* src_rect, int16_t screen_x, int16_t screen_y,
								int16_t buf_w, int16_t buf_h) {
	BitmapStruct bm;
	Rect canvas_clip;
	int16_t clip_w, clip_h;
	int16_t buf_left, buf_top;
	int16_t copy_w, copy_h;
	int16_t src_x, src_y;

	lcanvas_Get_Drawing_Canvas_Clip(&canvas_clip);
	clip_w = canvas_clip.right - canvas_clip.left;
	clip_h = canvas_clip.bottom - canvas_clip.top;

	buf_left = src_rect->left;
	buf_top = src_rect->top;
	copy_w = src_rect->right - buf_left;
	copy_h = src_rect->bottom - buf_top;
	src_x = screen_x;

	/* Clip left edge */
	if (screen_x < canvas_clip.left) {
		int16_t overshoot = canvas_clip.left - screen_x;
		buf_left += overshoot;
		src_x = canvas_clip.left;
		copy_w -= overshoot;
	}

	/* Clip right edge */
	if (src_x + copy_w > clip_w + canvas_clip.left)
		copy_w = clip_w + canvas_clip.left - src_x;

	/* Clip top edge */
	src_y = screen_y;
	if (screen_y < canvas_clip.top) {
		src_y = canvas_clip.top;
		copy_h -= canvas_clip.top - screen_y;
		buf_top += canvas_clip.top - screen_y;
	}

	/* Clip bottom edge */
	if (src_y + copy_h > canvas_clip.top + clip_h)
		copy_h = clip_h + canvas_clip.top - src_y;

	if (copy_w <= 0 || copy_h <= 0)
		return 0;

	/* Build temporary bitmap wrapping the buffer */
	bm.w = buf_w;
	bm.h = buf_h;
	lrect_Set_Rect(&bm.clip, 0, 0, buf_w, buf_h);
	bm.flags = 0;
	bm.type = 0;
	bm.offset = 0;
	bm.data = buffer;

	Rect copy_rect;
	lrect_Set_Rect(&copy_rect, buf_left, buf_top, copy_w + buf_left, buf_top + copy_h);
	lcanvas_Copy_Canvas_Portion_To_Bitmap(&bm, &copy_rect, src_x, src_y);
	return 1;
}

/*
 * Texture-map a source image onto a rotated quadrilateral.
 *
 * dst_poly points to a Poly struct (x[4], y[4]) defining the 4 screen vertices.
 * src_rect defines the source region in the texture.
 * src_stride is the source image width in bytes.
 *
 * Algorithm:
 * 1. Build a source-space Poly from src_rect corners
 * 2. Find the top (min Y) and bottom (max Y) vertices
 * 3. Walk edges from top to bottom in both directions (CW + CCW),
 *    building interpolation tables for x, u, v via Build_Ratio
 * 4. Detect winding order (which side is left vs right)
 * 5. Call Map_Image to render the textured scanlines
 */
// FUNCTION: TIE 0x874A0
void stub_Map_Clipped_Image(void* src_data, int16_t* dst_poly, Rect* src_rect, int16_t src_stride,
							int16_t map_mode) {
	Poly src_poly;
	int16_t min_y_idx, max_y_idx;
	int16_t num_scanlines, start_y;
	int16_t table_a[600]; /* 3 arrays × 200 entries */
	int16_t table_b[600];
	int16_t *left_table, *right_table;

	(void)map_mode; /* passed through to Map_Image but never read there */

	/* Access dst_poly as a Poly: x[0..3] at offsets 0..3, y[0..3] at offsets 4..7 */
	int16_t* poly_x = dst_poly;
	int16_t* poly_y = dst_poly + 4;

	/* Build source-space polygon from src_rect corners (TL, TR, BR, BL) */
	lrect_Set_Poly(&src_poly, src_rect->left, src_rect->top, src_rect->right - 1, src_rect->top,
				   src_rect->right - 1, src_rect->bottom - 1, src_rect->left, src_rect->bottom - 1);

	/* Find min/max Y vertices */
	min_y_idx = 0;
	max_y_idx = 0;
	for (int16_t v = 1; v < 4; v++) {
		if (poly_y[v] < poly_y[min_y_idx])
			min_y_idx = v;
		if (poly_y[v] > poly_y[max_y_idx])
			max_y_idx = v;
	}

	/* Degenerate: all vertices on the same Y line */
	if (poly_y[min_y_idx] == poly_y[max_y_idx])
		return;

	/*
	 * Build edge tables for both traversal directions.
	 * Pass 1 (step=+1, CW): writes to table_b (tentative right)
	 * Pass 2 (step=-1, CCW): writes to table_a (tentative left)
	 */
	int16_t step = 3; /* starts at 3, decremented by 2 each pass → 1, -1 */
	while (step > -1) {
		step -= 2;
		int16_t* dest;
		if (step == 1)
			dest = table_b;
		else
			dest = table_a;

		int16_t cur = min_y_idx;
		while (cur != max_y_idx) {
			/* Advance to the next vertex with a different Y */
			int16_t next;
			for (next = (cur + step) & 3; poly_y[cur] == poly_y[next] && cur != max_y_idx;
				 next = (next + step) & 3) {
				cur = next;
			}

			if (cur == max_y_idx)
				break;

			int16_t edge_h = poly_y[next] - poly_y[cur];
			/* x: screen x along this edge */
			rotpoly_Build_Ratio(dest, edge_h, poly_x[cur], poly_x[next]);
			/* u: source x along this edge */
			rotpoly_Build_Ratio(dest + 200, edge_h, src_poly.x[cur], src_poly.x[next]);
			/* v: source y along this edge */
			rotpoly_Build_Ratio(dest + 400, edge_h, src_poly.y[cur], src_poly.y[next]);

			cur = next;
			dest += edge_h;
		}

		/* Final vertex: single-entry constant for the bottom point */
		rotpoly_Build_Ratio(dest, 1, poly_x[cur], poly_x[cur]);
		rotpoly_Build_Ratio(dest + 200, 1, src_poly.x[cur], src_poly.x[cur]);
		rotpoly_Build_Ratio(dest + 400, 1, src_poly.y[cur], src_poly.y[cur]);
	}

	/* Determine winding: find first scanline where table_a and table_b differ */
	num_scanlines = poly_y[max_y_idx] - poly_y[min_y_idx] + 1;
	int16_t diff = 0;
	for (int16_t s = 0; s < num_scanlines; s++) {
		diff = table_a[s] - table_b[s];
		if (diff)
			break;
	}

	start_y = poly_y[min_y_idx];

	/* If table_a[x] > table_b[x], table_a is the right side */
	if (diff <= 0) {
		left_table = table_a;
		right_table = table_b;
	} else {
		left_table = table_b;
		right_table = table_a;
	}

	rotpoly_Map_Image(src_data, left_table, src_stride, right_table, num_scanlines, start_y);
}
