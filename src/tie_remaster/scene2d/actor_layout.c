/* Resolves manifest layout and records actor geometry. */

#include "tie_remaster/scene2d/actor_layout.h"
#include "tie_remaster/scene2d/manifest_expr.h"

#include <math.h>
#include <string.h>

/* Authoring constants shared by actor resolution and recording. */
#define CUTSCENE_AUTHORED_SCALE_X 9.0f
#define CUTSCENE_AUTHORED_SCALE_Y 10.8f

static void TieScene2dActors_ApplyAnchor(TieScene2dAnchor anchor, float ref_x, float ref_y, float ref_w,
										 float ref_h, float dst_w, float dst_h, float* dst_x, float* dst_y) {
	float ax = 0.5f, ay = 0.5f;
	switch (anchor) {
		case ANCHOR_TOP_LEFT:
			ax = 0.0f;
			ay = 0.0f;
			break;
		case ANCHOR_TOP_CENTER:
			ax = 0.5f;
			ay = 0.0f;
			break;
		case ANCHOR_TOP_RIGHT:
			ax = 1.0f;
			ay = 0.0f;
			break;
		case ANCHOR_CENTER_LEFT:
			ax = 0.0f;
			ay = 0.5f;
			break;
		case ANCHOR_CENTER:
			ax = 0.5f;
			ay = 0.5f;
			break;
		case ANCHOR_CENTER_RIGHT:
			ax = 1.0f;
			ay = 0.5f;
			break;
		case ANCHOR_BOTTOM_LEFT:
			ax = 0.0f;
			ay = 1.0f;
			break;
		case ANCHOR_BOTTOM_CENTER:
			ax = 0.5f;
			ay = 1.0f;
			break;
		case ANCHOR_BOTTOM_RIGHT:
			ax = 1.0f;
			ay = 1.0f;
			break;
	}
	*dst_x = ref_x + (ref_w - dst_w) * ax;
	*dst_y = ref_y + (ref_h - dst_h) * ay;
}

/* Manifest-defined fade between two cel values. Inactive returns
 * identity tint + zero bias. The shader does
 *   out.rgb = sample.rgb * tint.rgb + bias.rgb * sample.a
 *   out.a   = sample.a   * tint.a
 * For a fade by progress `t in [0,1]`:
 *   tint.rgb = 1 - t,   tint.a = 1
 *   bias.rgb = target * t   (PMA for the alpha-weighted bias). */
static void TieScene2dActors_ComputeFade(const TieScene2dActorEntry* e, int cur_cel, TieScene2dRgba* out_tint,
										 TieScene2dRgba* out_bias) {
	*out_tint = (TieScene2dRgba) { 1.0f, 1.0f, 1.0f, 1.0f };
	*out_bias = (TieScene2dRgba) { 0.0f, 0.0f, 0.0f, 0.0f };
	if (!e->fade_active)
		return;
	int span = e->fade_to_cel - e->fade_from_cel;
	if (span <= 0)
		return;
	float t;
	if (cur_cel <= e->fade_from_cel)
		t = 0.0f;
	else if (cur_cel >= e->fade_to_cel)
		t = 1.0f;
	else
		t = (float)(cur_cel - e->fade_from_cel) / (float)span;
	out_tint->r = out_tint->g = out_tint->b = 1.0f - t;
	out_bias->r = ((float)e->fade_to_r / 255.0f) * t;
	out_bias->g = ((float)e->fade_to_g / 255.0f) * t;
	out_bias->b = ((float)e->fade_to_b / 255.0f) * t;
}

static inline TieScene2dUvRect TieScene2dActors_RectToUv(float x, float y, float w, float h, float tex_w,
														 float tex_h) {
	return (TieScene2dUvRect) {
		x / tex_w,
		y / tex_h,
		(x + w) / tex_w,
		(y + h) / tex_h,
	};
}

bool TieScene2dActors_ResolveInSourceAspect(const TieScene2dActorView* a, const TieScene2dActorEntry* e,
											const TieScene2dActorTexture* tex, int viewport_w, int viewport_h,
											int source_w, int source_h, uint8_t source_pixel_aspect,
											int cur_cel, TieScene2dResolvedActorDraw* out) {
	if (!a || !e || !tex || !tex->texture || !out)
		return false;
	if (e->hide)
		return false;
	if (tex->tex_w <= 0 || tex->tex_h <= 0)
		return false;

	memset(out, 0, sizeof *out);

	/* Compute the FRAME rect — region of the texture to sample.
	 * For sprite/frames-dir the texture IS the frame; for atlas, look
	 * up the per-state sub-rect and rescale to the loaded (possibly
	 * downsampled) texture's dims. */
	float frame_x, frame_y, frame_w, frame_h;
	if (tex->use_atlas) {
		frame_x = tex->atlas_frame_x;
		frame_y = tex->atlas_frame_y;
		frame_w = tex->atlas_frame_w;
		frame_h = tex->atlas_frame_h;
		if (tex->atlas_w_authored > 0 && tex->atlas_h_authored > 0 &&
			(tex->tex_w != tex->atlas_w_authored || tex->tex_h != tex->atlas_h_authored)) {
			float sx = (float)tex->tex_w / (float)tex->atlas_w_authored;
			float sy = (float)tex->tex_h / (float)tex->atlas_h_authored;
			frame_x *= sx;
			frame_w *= sx;
			frame_y *= sy;
			frame_h *= sy;
		}
	} else {
		frame_x = 0.0f;
		frame_y = 0.0f;
		frame_w = (float)tex->tex_w;
		frame_h = (float)tex->tex_h;
	}

	/* Interpolate from the cel-start snapshot. A zero frame_progress
	 * preserves continuity at cel boundaries; position interpolation is
	 * rejected when the delta exceeds the integrated velocity. */
	float p = 0.0f;
	bool do_lerp = e->interpolate ? true : false;
	if (do_lerp) {
		p = a->frame_progress;
		if (p > 1.0f)
			p = 1.0f;
		if (p < 0.0f)
			p = 0.0f;
		/* A positive rate quantizes interpolation independently of host refresh. */
		if (e->interp_rate_hz > 0 && a->cel_period_us > 0) {
			float n = (float)a->cel_period_us * (float)e->interp_rate_hz / 1.0e6f;
			float buckets = ceilf(n);
			if (buckets < 1.0f)
				buckets = 1.0f;
			p = floorf(p * buckets) / buckets;
		}
	}

	/* Position lerp + teleport guard. */
	float src_x_classic = (float)a->x;
	float src_y_classic = (float)a->y;
	if (do_lerp) {
		int dx = (int)a->x - (int)a->prev_x;
		int dy = (int)a->y - (int)a->prev_y;
		int abs_dx = dx < 0 ? -dx : dx;
		int abs_dy = dy < 0 ? -dy : dy;
		int abs_xv = a->prev_xv < 0 ? -a->prev_xv : a->prev_xv;
		int abs_yv = a->prev_yv < 0 ? -a->prev_yv : a->prev_yv;
		/* +1 absorbs Q8.8 fractional-velocity carry (xvf >= 256
		 * triggers a +1 increment in x beyond the integer xv). */
		int tol_x = abs_xv + 1;
		int tol_y = abs_yv + 1;
		if (abs_dx <= tol_x && abs_dy <= tol_y) {
			src_x_classic = (float)a->prev_x + (float)dx * p;
			src_y_classic = (float)a->prev_y + (float)dy * p;
		}
		/* else: teleport (delta >> velocity) — keep current commit. */
	}

	/* Scale is Q8 and has no velocity with which to detect teleports. */
	float xs_q8 = (a->xscale > 0 ? (float)a->xscale : 256.0f);
	float ys_q8 = (a->yscale > 0 ? (float)a->yscale : 256.0f);
	if (do_lerp) {
		float xs_prev = (a->prev_xscale > 0 ? (float)a->prev_xscale : 256.0f);
		float ys_prev = (a->prev_yscale > 0 ? (float)a->prev_yscale : 256.0f);
		xs_q8 = xs_prev + (xs_q8 - xs_prev) * p;
		ys_q8 = ys_prev + (ys_q8 - ys_prev) * p;
	}
	float xs = xs_q8 / 256.0f;
	float ys = ys_q8 / 256.0f;
	float sw_classic = (float)a->w * xs;
	float sh_classic = (float)a->h * ys;
	float sx_classic = src_x_classic + ((float)a->w - sw_classic) * 0.5f;
	float sy_classic = src_y_classic + ((float)a->h - sh_classic) * 0.5f;

	TieScene2dViewportTransform vx;
	if (!TieScene2dViewport_ComputeXformForSourceAspect(viewport_w, viewport_h, source_w, source_h,
														source_pixel_aspect, &vx))
		return false;

	TieScene2dManifestEvaluationContext ectx = {
		.classic_x = sx_classic,
		.classic_y = sy_classic,
		.classic_w = sw_classic,
		.classic_h = sh_classic,
		.sprite_w = (int)frame_w,
		.sprite_h = (int)frame_h,
		.entry_index = a->film_entry_index,
		.viewport_w = vx.viewport_w,
		.viewport_h = vx.viewport_h,
		.region_x = vx.region_x,
		.region_y = vx.region_y,
		.region_w = vx.region_w,
		.region_h = vx.region_h,
		.scale_x = vx.scale_x,
		.scale_y = vx.scale_y,
	};

	/* Default viewport dst rect: scaled bbox times the 4:3 letterbox
	 * scale, anchored at region_x/y. */
	float dst_x = (float)vx.region_x + sx_classic * vx.scale_x;
	float dst_y = (float)vx.region_y + sy_classic * vx.scale_y;
	float dst_w = sw_classic * vx.scale_x;
	float dst_h = sh_classic * vx.scale_y;
	bool overridden = false;

	if (e->fit == FIT_EXTEND && frame_w > 0.0f && frame_h > 0.0f) {
		/* Texture-driven dst dims (canonical 4K rate), anchored on
		 * the engine-driven classic bbox. Re-applies engine scale so
		 * dynamic scale animations shrink the wider asset uniformly. */
		float ref_x = dst_x, ref_y = dst_y;
		float ref_w = dst_w, ref_h = dst_h;
		dst_w = frame_w * vx.scale_x * xs / CUTSCENE_AUTHORED_SCALE_X;
		dst_h = frame_h * vx.scale_y * ys / CUTSCENE_AUTHORED_SCALE_Y;
		TieScene2dActors_ApplyAnchor(e->anchor, ref_x, ref_y, ref_w, ref_h, dst_w, dst_h, &dst_x, &dst_y);
		overridden = true;
	}

	/* Manifest dst:{x,y,w,h} expression — always wins when present. */
	if (TieScene2dExpression_Rect(e->dst_x_expr, e->dst_y_expr, e->dst_w_expr, e->dst_h_expr, &ectx, &dst_x,
								  &dst_y, &dst_w, &dst_h))
		overridden = true;

	/* Source rect (in texture pixels). For non-overridden default
	 * actors, narrow by the engine's classic clip-rect intersection
	 * using INT recenter math (mirrors lactdelt_Draw_Scaled_Clipped_
	 * Delta — keeps HD pixel-locked to the classic FB on scaled
	 * clipped actors). */
	float src_x = frame_x, src_y = frame_y;
	float src_w = frame_w, src_h = frame_h;
	bool src_set = false;

	if (!overridden) {
		/* Float source and destination rectangles preserve sub-pixel interpolation. */
		float aw_f = (float)a->w, ah_f = (float)a->h;
		float sw_f = sw_classic, sh_f = sh_classic;
		/* Recenter the scaled bbox on the unscaled rect's center.
		 * Always-on (was conditional on a->xscale != 256) because
		 * when scale-lerp is active the effective sw_f can differ
		 * from aw_f even on cels where a->xscale == 256 (the lerp
		 * pulls toward prev_xscale ≠ 256). When scale really IS
		 * identity throughout, sw_f == aw_f and the offset is 0,
		 * so the always-on branch costs nothing. */
		float sx_lf = src_x_classic - (sw_f - aw_f) * 0.5f;
		float sy_tf = src_y_classic - (sh_f - ah_f) * 0.5f;
		float sx_rf = sx_lf + sw_f;
		float sy_bf = sy_tf + sh_f;
		float cx_l = (float)a->clip_left, cx_r = (float)a->clip_right;
		float cy_t = (float)a->clip_top, cy_b = (float)a->clip_bottom;
		float vis_l = sx_lf > cx_l ? sx_lf : cx_l;
		float vis_r = sx_rf < cx_r ? sx_rf : cx_r;
		float vis_t = sy_tf > cy_t ? sy_tf : cy_t;
		float vis_b = sy_bf < cy_b ? sy_bf : cy_b;
		if (vis_r <= vis_l || vis_b <= vis_t)
			return false;
		src_x = frame_x + (vis_l - sx_lf) * frame_w / sw_f;
		src_y = frame_y + (vis_t - sy_tf) * frame_h / sh_f;
		src_w = (vis_r - vis_l) * frame_w / sw_f;
		src_h = (vis_b - vis_t) * frame_h / sh_f;
		src_set = true;
		dst_x = (float)vx.region_x + vis_l * vx.scale_x;
		dst_y = (float)vx.region_y + vis_t * vx.scale_y;
		dst_w = (vis_r - vis_l) * vx.scale_x;
		dst_h = (vis_b - vis_t) * vx.scale_y;
	} else if (tex->use_atlas) {
		src_set = true;
	}

	/* Tile mode. The renderer is told the base sample (full atlas
	 * frame for atlas, full texture otherwise) plus the (tw, th)
	 * stride and (ox, oy) wrap offset; SDL3 expands to N×M quads
	 * with a scissor clip, retained-mode backends use a tiling
	 * material. The classic-clip-narrowed src_uv from above is
	 * IGNORED in tile mode (matches the pre-split behavior). */
	bool tile_x = e->tile_x_expr[0] != '\0';
	bool tile_y = e->tile_y_expr[0] != '\0';
	float tw = 0.0f, th = 0.0f, ox = 0.0f, oy = 0.0f;
	if (tile_x || tile_y) {
		tw = e->tile_w_expr[0] ? TieScene2dExpression_Evaluate(e->tile_w_expr, &ectx) : frame_w;
		th = e->tile_h_expr[0] ? TieScene2dExpression_Evaluate(e->tile_h_expr, &ectx) : frame_h;
		if (tw < 1.0f)
			tw = frame_w;
		if (th < 1.0f)
			th = frame_h;
		if (tile_x) {
			ox = fmodf(TieScene2dExpression_Evaluate(e->tile_x_expr, &ectx), tw);
			if (ox < 0.0f)
				ox += tw;
		}
		if (tile_y) {
			oy = fmodf(TieScene2dExpression_Evaluate(e->tile_y_expr, &ectx), th);
			if (oy < 0.0f)
				oy += th;
		}
	}

	/* Final src UV — tile mode uses the full base rect; otherwise
	 * the (possibly clip-narrowed) src_x/y/w/h. */
	TieScene2dUvRect src_uv;
	if (tile_x || tile_y) {
		src_uv = tex->use_atlas ? TieScene2dActors_RectToUv(frame_x, frame_y, frame_w, frame_h,
															(float)tex->tex_w, (float)tex->tex_h)
								: (TieScene2dUvRect) { 0.0f, 0.0f, 1.0f, 1.0f };
	} else if (src_set) {
		src_uv = TieScene2dActors_RectToUv(src_x, src_y, src_w, src_h, (float)tex->tex_w, (float)tex->tex_h);
	} else {
		src_uv = (TieScene2dUvRect) { 0.0f, 0.0f, 1.0f, 1.0f };
	}

	/* Identity for retained-mode backends. SDL3 ignores it. */
	memcpy(out->res_name, a->res_name, 8);
	out->film_entry_index = a->film_entry_index;

	out->texture = tex->texture;
	out->tex_w = tex->tex_w;
	out->tex_h = tex->tex_h;
	out->dst = (TieScene2dRect) { dst_x, dst_y, dst_w, dst_h };
	out->src = src_uv;

	TieScene2dActors_ComputeFade(e, cur_cel, &out->tint, &out->bias);

	/* AF_REMAP_COLOR equivalent — render the cel as a silhouette in
	 * the engine-supplied fore_color (pre-resolved to remap_rgba by
	 * the application). Mirrors lactanim's AF_REMAP_COLOR + foreColor pair
	 * used by player_Draw_Display_Ship's reticle silhouette quartet
	 * on iconsgrn. The fade tint is overridden — REMAP_COLOR fully
	 * replaces the cel's natural color, fade can't compose with it
	 * meaningfully. */
	if (a->flags & ACTOR2D_REMAP_COLOR) {
		out->tint = (TieScene2dRgba) { 0.0f, 0.0f, 0.0f, 1.0f };
		out->bias = a->remap_rgba;
		out->bias.a = 0.0f; /* shader uses bias.rgb only */
	}

	out->flip_flags = 0;
	if (a->flags & ACTOR2D_HFLIP)
		out->flip_flags |= TIE_SCENE2D_FLIP_H;
	if (a->flags & ACTOR2D_VFLIP)
		out->flip_flags |= TIE_SCENE2D_FLIP_V;
	out->filter = e->filter_linear ? AERON_BLIT2D_FILTER_LINEAR : AERON_BLIT2D_FILTER_NEAREST;
	out->blend = AERON_BLIT2D_BLEND_PMA;

	out->tile_x = tile_x;
	out->tile_y = tile_y;
	out->tile_w = tw;
	out->tile_h = th;
	out->tile_offset_x = ox;
	out->tile_offset_y = oy;

	return true;
}

bool TieScene2dActors_ResolveInSource(const TieScene2dActorView* a, const TieScene2dActorEntry* e,
									  const TieScene2dActorTexture* tex, int viewport_w, int viewport_h,
									  int source_w, int source_h, int cur_cel,
									  TieScene2dResolvedActorDraw* out) {
	const uint8_t aspect = source_w == CLASSIC_FB_W && source_h == CLASSIC_FB_H
							   ? TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_VGA_4_3
							   : TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_SQUARE;
	return TieScene2dActors_ResolveInSourceAspect(a, e, tex, viewport_w, viewport_h, source_w, source_h,
												  aspect, cur_cel, out);
}

bool TieScene2dActors_Resolve(const TieScene2dActorView* a, const TieScene2dActorEntry* e,
							  const TieScene2dActorTexture* tex, int viewport_w, int viewport_h, int cur_cel,
							  TieScene2dResolvedActorDraw* out) {
	return TieScene2dActors_ResolveInSourceAspect(a, e, tex, viewport_w, viewport_h, CLASSIC_FB_W,
												  CLASSIC_FB_H, TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_VGA_4_3,
												  cur_cel, out);
}

void TieScene2dActors_Record(TieScene2dCanvas* canvas, const TieScene2dActorView* a,
							 const TieScene2dActorEntry* e, const TieScene2dActorTexture* tex, int cur_cel) {
	if (!canvas)
		return;
	TieScene2dResolvedActorDraw d;
	if (!TieScene2dActors_ResolveInSourceAspect(a, e, tex, canvas->viewport_w, canvas->viewport_h,
												canvas->source_w, canvas->source_h,
												canvas->source_pixel_aspect, cur_cel, &d))
		return;
	AeronDrawList2DSprite sprite = {
		.texture = d.texture,
		.src_u0 = (d.flip_flags & TIE_SCENE2D_FLIP_H) ? d.src.u1 : d.src.u0,
		.src_u1 = (d.flip_flags & TIE_SCENE2D_FLIP_H) ? d.src.u0 : d.src.u1,
		.src_v0 = (d.flip_flags & TIE_SCENE2D_FLIP_V) ? d.src.v1 : d.src.v0,
		.src_v1 = (d.flip_flags & TIE_SCENE2D_FLIP_V) ? d.src.v0 : d.src.v1,
		.tint = { d.tint.r, d.tint.g, d.tint.b, d.tint.a },
		.bias = { d.bias.r, d.bias.g, d.bias.b, d.bias.a },
		.filter = d.filter,
		.blend = d.blend,
	};

	if (!d.tile_x && !d.tile_y) {
		TieScene2dCanvas_SetScissor(canvas, (AeronRectI) { 0 });
		sprite.dst_x = d.dst.x;
		sprite.dst_y = d.dst.y;
		sprite.dst_w = d.dst.w;
		sprite.dst_h = d.dst.h;
		TieScene2dCanvas_AddSprite(canvas, &sprite);
		return;
	}

	/* Tile-mode expansion. Mirrors the descriptor's tile_x/y/w/h/
	 * offsets, scissor-clipped to the dst rect. */
	int nx = d.tile_x ? (int)ceilf((d.dst.w + d.tile_offset_x) / d.tile_w) : 1;
	int ny = d.tile_y ? (int)ceilf((d.dst.h + d.tile_offset_y) / d.tile_h) : 1;
	if (nx < 1)
		nx = 1;
	if (ny < 1)
		ny = 1;

	TieScene2dCanvas_SetScissor(
		canvas, (AeronRectI) { (int32_t)d.dst.x, (int32_t)d.dst.y, (int32_t)d.dst.w, (int32_t)d.dst.h });

	for (int ty = 0; ty < ny; ty++) {
		for (int tx = 0; tx < nx; tx++) {
			float td_x = d.tile_x ? (d.dst.x - d.tile_offset_x + (float)tx * d.tile_w) : d.dst.x;
			float td_w = d.tile_x ? d.tile_w : d.dst.w;
			float td_y = d.tile_y ? (d.dst.y - d.tile_offset_y + (float)ty * d.tile_h) : d.dst.y;
			float td_h = d.tile_y ? d.tile_h : d.dst.h;
			sprite.dst_x = td_x;
			sprite.dst_y = td_y;
			sprite.dst_w = td_w;
			sprite.dst_h = td_h;
			TieScene2dCanvas_AddSprite(canvas, &sprite);
		}
	}
}
