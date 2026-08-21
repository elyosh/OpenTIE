/*
 * Cross-channel z-merge dispatch — see snapshot_merge.h.
 *
 * Painter's-algorithm walk over the three snapshot record streams
 * (draws_2d via TieScene2dActorView, paint_cmds, ui_texts) plus an optional
 * brief-map quad slot. Records whose `target` tag doesn't match
 * accept_target are skipped — that's the routing mechanism between
 * the cutscene RT pass and the brief-map source RT pass.
 *
 * Same-channel runs are batched into one renderer call: the inner
 * loop walks one channel forward as long as its z_order stays below
 * the next other-channel slot AND the record's target matches. This
 * keeps draw-call counts low (one paint recording call for a row of bevels,
 * one TieScene2dText_Draw for a run of labels) without sacrificing cross-
 * channel layering.
 */

#include "tie_remaster/scene2d/snapshot_merge.h"

#include "tie_remaster/scene2d/cutscene.h"
#include "tie_remaster/scene2d/paint.h"
#include "tie_remaster/scene2d/text.h"

#define Z_SENTINEL 0x7FFF

void TieScene2dSnapshotDispatch_Run(AeronDrawList2D* list, int viewport_w, int viewport_h,
									const TieScene2dSnapshotMergeDispatch* cfg) {
	if (!list || !cfg)
		return;
	if (viewport_w <= 0 || viewport_h <= 0)
		return;

	const int n_v = cfg->views ? cfg->view_count : 0;
	const int n_t = cfg->ui_texts ? cfg->ui_text_count : 0;
	const int n_p = cfg->paint_cmds ? cfg->paint_cmd_count : 0;
	const uint8_t accept = cfg->accept_target;
	const int source_w = cfg->source_w > 0 ? cfg->source_w : 320;
	const int source_h = cfg->source_h > 0 ? cfg->source_h : 200;
	const uint8_t source_pixel_aspect = cfg->source_pixel_aspect;
	bool map_pending = cfg->emit_map_quad != NULL;
	const int z_map = map_pending ? cfg->map_quad_z : Z_SENTINEL;
	int i_v = 0, i_t = 0, i_p = 0;

/* Per-channel cursor advance past records that don't match
 * accept_target. */
#define SKIP_REJECT(arr, idx, n)                                                                             \
	while ((idx) < (n) && (arr)[idx].target != accept)                                                       \
	(idx)++

	while (i_v < n_v || i_t < n_t || i_p < n_p || map_pending) {
		SKIP_REJECT(cfg->views, i_v, n_v);
		SKIP_REJECT(cfg->ui_texts, i_t, n_t);
		SKIP_REJECT(cfg->paint_cmds, i_p, n_p);
		if (i_v >= n_v && i_t >= n_t && i_p >= n_p && !map_pending)
			break;

		int zv = (i_v < n_v) ? cfg->views[i_v].z_order : Z_SENTINEL;
		int zt = (i_t < n_t) ? cfg->ui_texts[i_t].z_order : Z_SENTINEL;
		int zp = (i_p < n_p) ? cfg->paint_cmds[i_p].z_order : Z_SENTINEL;
		int zm = map_pending ? z_map : Z_SENTINEL;

		int z_min = zv;
		if (zt < z_min)
			z_min = zt;
		if (zp < z_min)
			z_min = zp;
		if (zm < z_min)
			z_min = zm;

		if (zv == z_min && i_v < n_v) {
			int next_other = zt;
			if (zp < next_other)
				next_other = zp;
			if (zm < next_other)
				next_other = zm;
			int run_start = i_v;
			while (i_v < n_v && cfg->views[i_v].z_order <= next_other && cfg->views[i_v].target == accept)
				i_v++;
			if (cfg->cutscene && cfg->lfd && cfg->film) {
				TieScene2dCutscene_RecordActorsInSource(
					cfg->cutscene, list, viewport_w, viewport_h, source_w, source_h, source_pixel_aspect,
					cfg->lfd, cfg->film, cfg->cur_cel, &cfg->views[run_start], i_v - run_start);
			}
		} else if (zt == z_min && i_t < n_t) {
			int next_other = zv;
			if (zp < next_other)
				next_other = zp;
			if (zm < next_other)
				next_other = zm;
			int run_start = i_t;
			while (i_t < n_t && cfg->ui_texts[i_t].z_order <= next_other &&
				   cfg->ui_texts[i_t].target == accept)
				i_t++;
			if (cfg->text_renderer) {
				const TieScene2dTextSpace space = {
					.classic_w = source_w,
					.classic_h = source_h,
					.atlas_scale_x = 2880.0f / (float)source_w,
					.atlas_scale_y = 2160.0f / (float)source_h,
					.space_between_classic = 1.0f,
					.fit = TIE_SCENE2D_TEXT_FIT_LETTERBOX_4_3,
				};
				TieScene2dTextRenderer_RecordInSpace(cfg->text_renderer, list, viewport_w, viewport_h, &space,
													 &cfg->ui_texts[run_start], i_t - run_start,
													 cfg->palette);
			}
		} else if (zp == z_min && i_p < n_p) {
			int next_other = zv;
			if (zt < next_other)
				next_other = zt;
			if (zm < next_other)
				next_other = zm;
			int run_start = i_p;
			while (i_p < n_p && cfg->paint_cmds[i_p].z_order <= next_other &&
				   cfg->paint_cmds[i_p].target == accept)
				i_p++;
			TieScene2dPaint_RecordInSource(list, viewport_w, viewport_h, source_w, source_h,
										   source_pixel_aspect, &cfg->paint_cmds[run_start], i_p - run_start,
										   cfg->palette);
		} else {
			/* zm == z_min — emit the brief-map quad once. */
			cfg->emit_map_quad(cfg->map_quad_userdata, list, viewport_w, viewport_h);
			map_pending = false;
		}
	}
#undef SKIP_REJECT
}
