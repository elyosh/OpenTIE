#ifndef TIE_SCENE2D_MANIFEST_EXPR_H
#define TIE_SCENE2D_MANIFEST_EXPR_H

/*
 * Minimal arithmetic-expression evaluator for manifest override
 * fields (dst.x/y/w/h, tile_x, tile_y, clip.x/y/w/h).
 *
 * Grammar:
 *   expr   := term ( ('+' | '-') term )*
 *   term   := factor ( ('*' | '/') factor )*
 *   factor := number | identifier | '(' expr ')' | '-' factor
 *
 * Variables are looked up by name in a TieScene2dManifestEvaluationContext — basic
 * snapshot fields plus viewport / scale constants.
 *
 * Errors: malformed expressions evaluate to 0.0 with a one-shot
 * warning through the application logger. Real-world manifests come
 * from asset authoring; bad syntax is something to flag, not crash on.
 */

#include <stdbool.h>

typedef struct {
	/* Per-actor snapshot values in classic-coord screen space.
	 * Float so the cutscene compositor's lerped sub-cel position
	 * propagates through dst expressions like
	 * `classic_y * scale_y`. With smoothing off these are integer-
	 * valued floats — manifest expressions that floor/truncate
	 * still see exactly the engine commit, so existing assets
	 * with `dst: classic_x * scale_x` paths render bit-stably. */
	float classic_x, classic_y, classic_w, classic_h;
	int sprite_w, sprite_h; /* PNG dimensions */

	/* FilmObject array index of the actor in its source FILM, or -1
	 * when the actor wasn't instantiated from a FILM. Useful for
	 * arithmetic discriminators in manifests where the same res_name
	 * appears multiple times. */
	int entry_index;

	/* Viewport / region constants — same for every actor on a frame. */
	int viewport_w, viewport_h;
	int region_x, region_y, region_w, region_h;
	float scale_x, scale_y;
} TieScene2dManifestEvaluationContext;

/* Evaluate `expr` against `ctx`. Returns the evaluated value, or 0.0
 * on parse failure after logging a warning. Empty string returns 0. */
float TieScene2dExpression_Evaluate(const char* expr, const TieScene2dManifestEvaluationContext* ctx);

/* Evaluate four optional dst-rect component expressions. Each may be
 * NULL or empty, in which case *out_<component> is left unchanged.
 * Returns true if at least one component was evaluated (so the caller
 * can detect "any override present" cheaply, without reparsing). */
bool TieScene2dExpression_Rect(const char* expr_x, const char* expr_y, const char* expr_w, const char* expr_h,
							   const TieScene2dManifestEvaluationContext* ctx, float* out_x, float* out_y,
							   float* out_w, float* out_h);

#endif
