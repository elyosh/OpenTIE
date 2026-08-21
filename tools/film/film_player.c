#include "film_player.h"

#include "fourcc.h"
#include "imgbake/byteio.h"

#include <stdlib.h>
#include <string.h>

static void TieFilmPlayer_MarkStreamActors(TieFilmPlayer* p);

/* ---------- helpers ---------- */

static int16_t TieFilmPlayer_Clampi16(int v) {
	if (v < INT16_MIN)
		return INT16_MIN;
	if (v > INT16_MAX)
		return INT16_MAX;
	return (int16_t)v;
}

/* Get the bbox of an ANIM frame (or zero rect for empty frames). */
static void TieFilmPlayer_AnimFrameBounds(const AnimImage* a, int idx, int16_t* l, int16_t* t, int16_t* r,
										  int16_t* b) {
	*l = *t = *r = *b = 0;
	if (idx < 0 || idx >= a->count)
		return;
	const Image8* img = &a->frames[idx];
	if (!img->pixels)
		return;
	*l = (int16_t)img->left;
	*t = (int16_t)img->top;
	*r = (int16_t)(img->left + img->width);
	*b = (int16_t)(img->top + img->height);
}

/* Union all ANIM frame bboxes into the natural bounds. */
static void TieFilmPlayer_AnimTotalBounds(const AnimImage* a, int16_t* l, int16_t* t, int16_t* r,
										  int16_t* b) {
	bool any = false;
	int16_t L = 0, T = 0, R = 0, B = 0;
	for (int i = 0; i < a->count; i++) {
		int16_t fl, ft, fr, fb;
		TieFilmPlayer_AnimFrameBounds(a, i, &fl, &ft, &fr, &fb);
		if (fl == fr || ft == fb)
			continue;
		if (!any) {
			L = fl;
			T = ft;
			R = fr;
			B = fb;
			any = true;
		} else {
			if (fl < L)
				L = fl;
			if (ft < T)
				T = ft;
			if (fr > R)
				R = fr;
			if (fb > B)
				B = fb;
		}
	}
	*l = L;
	*t = T;
	*r = R;
	*b = B;
}

static void TieFilmPlayer_ObjectUpdateSize(TieFilmPlayerObject* o) {
	int16_t l, t, r, b;
	if (o->res_type == FCC_ANIM) {
		int n = o->anim.count;
		int s = o->state;
		if (n > 0) {
			while (s < 0)
				s += n;
			while (s >= n)
				s -= n;
		} else {
			s = 0;
		}
		TieFilmPlayer_AnimFrameBounds(&o->anim, s, &l, &t, &r, &b);
	} else if (o->sprite.pixels) {
		l = (int16_t)o->sprite.left;
		t = (int16_t)o->sprite.top;
		r = (int16_t)(o->sprite.left + o->sprite.width);
		b = (int16_t)(o->sprite.top + o->sprite.height);
	} else {
		o->w = o->h = 0;
		return;
	}
	o->w = (int16_t)(r - l);
	o->h = (int16_t)(b - t);
}

/* ---------- record application ---------- */

/* Apply a single record to its FilmObject (and to the screen palette
   for PALETTE/VIEW commands). */
static void TieFilmPlayer_ApplyRecord(TieFilmPlayer* p, TieFilmPlayerObject* o, uint16_t cmd,
									  const uint8_t* payload, uint16_t payload_size) {
	(void)payload_size;
	switch (cmd) {
		/* --- ACTOR --- */
		case FILM_CMD_ACTOR_POS:
			o->x = rd_i16(payload + 0);
			o->y = rd_i16(payload + 2);
			o->xf = rd_i16(payload + 4);
			o->yf = rd_i16(payload + 6);
			break;
		case FILM_CMD_ACTOR_VEL:
			o->xv = rd_i16(payload + 0);
			o->yv = rd_i16(payload + 2);
			o->xvf = rd_i16(payload + 4);
			o->yvf = rd_i16(payload + 6);
			break;
		case FILM_CMD_ACTOR_Z:
			o->zplane = rd_i16(payload + 0);
			break;
		case FILM_CMD_ACTOR_STATE:
			o->state = rd_i16(payload + 0);
			o->state_f = rd_i16(payload + 2);
			TieFilmPlayer_ObjectUpdateSize(o);
			break;
		case FILM_CMD_ACTOR_STATEV:
			o->state_v = rd_i16(payload + 0);
			o->state_vf = rd_i16(payload + 2);
			break;
		case FILM_CMD_ACTOR_VAR1:
			o->var1 = rd_i16(payload + 0);
			break;
		case FILM_CMD_ACTOR_VAR2:
			o->var2 = rd_i16(payload + 0);
			break;
		case FILM_CMD_ACTOR_CLIP:
			o->frame_l = rd_i16(payload + 0);
			o->frame_t = rd_i16(payload + 2);
			o->frame_r = rd_i16(payload + 4);
			o->frame_b = rd_i16(payload + 6);
			break;
		case FILM_CMD_ACTOR_CLIPV:
			o->frame_vl = rd_i16(payload + 0);
			o->frame_vt = rd_i16(payload + 2);
			o->frame_vr = rd_i16(payload + 4);
			o->frame_vb = rd_i16(payload + 6);
			break;
		case FILM_CMD_ACTOR_SHOW:
			/* Engine reads the payload as int16; only bit 0 matters in
			   the show/hide branch. */
			o->show = (rd_i16(payload + 0) != 0);
			break;
		case FILM_CMD_ACTOR_FLIP:
			o->hflip = (rd_i16(payload + 0) != 0);
			o->vflip = (rd_i16(payload + 2) != 0);
			break;

		/* --- PALETTE --- */
		case FILM_CMD_PALETTE_SET:
			if (o->pltt_data && o->pltt_size > 0)
				palette_overlay(&p->palette, o->pltt_data, o->pltt_size);
			break;

		/* --- VIEW --- */
		case FILM_CMD_VIEW_SETRGB: {
			/* Engine reads as bytes: payload[0..4] = start, end, r, g, b. */
			uint8_t start = payload[0];
			uint8_t end = payload[1];
			uint8_t r = payload[2], g = payload[3], b = payload[4];
			for (int i = start; i <= end && i < 256; i++) {
				p->palette.rgba[i][0] = r;
				p->palette.rgba[i][1] = g;
				p->palette.rgba[i][2] = b;
				p->palette.rgba[i][3] = (i == 0) ? 0 : 255;
			}
			break;
		}
		case FILM_CMD_VIEW_DEFPAL:
			palette_black(&p->palette);
			if (p->def_pltt_data && p->def_pltt_size > 0)
				palette_overlay(&p->palette, p->def_pltt_data, p->def_pltt_size);
			break;
		case FILM_CMD_VIEW_FADE:
			/* Fade is a multi-frame interpolation effect; for a static
			   stepper the post-fade state is what each cel-boundary
			   already shows via PALETTE_SET / VIEW_DEFPAL above. */
			break;

		/* --- SOUND / others — ignored for the static viewer. --- */
		default:
			break;
	}
}

/* ---------- per-cel velocity tick (engine: Move_Actor + Move_Actor_Frame
   + Move_Actor_State for ANIM). Called BEFORE the cel's records run, on
   every cel, regardless of whether a TIMESTAMP fires. */

static void TieFilmPlayer_VelocityTick(TieFilmPlayerObject* o) {
	if (o->type_code != FILM_TC_ACTOR)
		return;

	o->xf = TieFilmPlayer_Clampi16(o->xf + o->xvf);
	o->yf = TieFilmPlayer_Clampi16(o->yf + o->yvf);
	o->x = TieFilmPlayer_Clampi16(o->x + o->xv + o->xf / 256);
	o->y = TieFilmPlayer_Clampi16(o->y + o->yv + o->yf / 256);
	while (o->xf >= 256)
		o->xf -= 256;
	while (o->yf >= 256)
		o->yf -= 256;
	while (o->xf <= -256)
		o->xf += 256;
	while (o->yf <= -256)
		o->yf += 256;

	o->frame_l = TieFilmPlayer_Clampi16(o->frame_l + o->frame_vl);
	o->frame_t = TieFilmPlayer_Clampi16(o->frame_t + o->frame_vt);
	o->frame_r = TieFilmPlayer_Clampi16(o->frame_r + o->frame_vr);
	o->frame_b = TieFilmPlayer_Clampi16(o->frame_b + o->frame_vb);

	if (o->res_type == FCC_ANIM) {
		int16_t prev_state = o->state;
		o->state_f = TieFilmPlayer_Clampi16(o->state_f + o->state_vf);
		o->state = TieFilmPlayer_Clampi16(o->state + o->state_v + o->state_f / 256);
		while (o->state_f >= 256)
			o->state_f -= 256;
		while (o->state_f <= -256)
			o->state_f += 256;

		int n = o->anim.count;
		if (n == 0)
			o->state = 0;
		else {
			while (o->state < 0)
				o->state += n;
			while (o->state >= n)
				o->state -= n;
		}
		if (prev_state != o->state)
			TieFilmPlayer_ObjectUpdateSize(o);
	}
}

/* ---------- record-stream walk ----------
 *
 * Engine pattern (lfilm_Step_*_Film):
 *   if (current record is TIMESTAMP for cur_cel) {
 *       loop: step (advance offset by cur record size);
 *             if next record is TIMESTAMP/END → break (don't apply);
 *             apply current record; continue.
 *   }
 */

static bool TieFilmPlayer_ObjectAtTimestamp(const TieFilmPlayerObject* o, uint16_t cel) {
	if ((uint32_t)o->offset + 6 > o->records_size)
		return false;
	uint16_t cmd = rd_u16(o->records + o->offset + 2);
	if (cmd != FILM_CMD_TIMESTAMP)
		return false;
	return rd_u16(o->records + o->offset + 4) == cel;
}

static bool TieFilmPlayer_ObjectAtBoundary(const TieFilmPlayerObject* o) {
	if ((uint32_t)o->offset + 4 > o->records_size)
		return true; /* end of stream behaves like END */
	uint16_t cmd = rd_u16(o->records + o->offset + 2);
	return cmd == FILM_CMD_TIMESTAMP || cmd == FILM_CMD_END;
}

static void TieFilmPlayer_ObjectRunCel(TieFilmPlayer* p, TieFilmPlayerObject* o, uint16_t cel) {
	TieFilmPlayer_VelocityTick(o);

	if (!TieFilmPlayer_ObjectAtTimestamp(o, cel))
		return;

	while (true) {
		/* Step past current record (TIMESTAMP on first iter, then the
		   most recently applied record). */
		if ((uint32_t)o->offset + 4 > o->records_size)
			break;
		uint16_t sz = rd_u16(o->records + o->offset);
		if (sz == 0)
			break;
		o->offset = (uint16_t)(o->offset + sz);

		if (TieFilmPlayer_ObjectAtBoundary(o))
			break;

		/* Apply the new current record. */
		uint16_t r_sz = rd_u16(o->records + o->offset);
		uint16_t r_cmd = rd_u16(o->records + o->offset + 2);
		uint16_t r_pl = (r_sz > FILM_REC_HDR_SIZE) ? (uint16_t)(r_sz - FILM_REC_HDR_SIZE) : 0;
		const uint8_t* r_payload = o->records + o->offset + FILM_REC_HDR_SIZE;
		TieFilmPlayer_ApplyRecord(p, o, r_cmd, r_payload, r_pl);
	}
}

/* ---------- rewind one object (engine: Rewind_*_Film) ---------- */

static void TieFilmPlayer_RewindObjectState(TieFilmPlayer* p, TieFilmPlayerObject* o) {
	o->offset = 0;

	if (o->type_code == FILM_TC_ACTOR) {
		/* lfilm_Rewind_Actor_Film: clip rect = canvas bounds, pos/speed/
		   state/state_speed/flip/var1/var2 = 0, hidden. */
		o->frame_l = 0;
		o->frame_t = 0;
		o->frame_r = (int16_t)p->fb_w;
		o->frame_b = (int16_t)p->fb_h;
		o->frame_vl = o->frame_vt = o->frame_vr = o->frame_vb = 0;
		o->x = o->y = o->xf = o->yf = 0;
		o->xv = o->yv = o->xvf = o->yvf = 0;
		o->state = o->state_f = o->state_v = o->state_vf = 0;
		o->hflip = o->vflip = false;
		o->var1 = o->var2 = 0;
		o->show = false;
		o->zplane = 0;
		TieFilmPlayer_ObjectUpdateSize(o);
	}
	/* PALETTE/VIEW/SOUND objects keep no extra rewind state — only
	   their record cursor. */

	/* Engine pattern: if the first record is TIMESTAMP for cel 0,
	   immediately step it so cel 0 gets its records. */
	if (TieFilmPlayer_ObjectAtTimestamp(o, 0))
		TieFilmPlayer_ObjectRunCel(p, o, 0);
}

/* ---------- public API ---------- */

int TieFilmPlayer_DisplayedCel(const TieFilmPlayer* p) { return (int)p->cur_cel - 1; }

int TieFilmPlayer_TotalCels(const TieFilmPlayer* p) { return (int)p->header.cels; }

void TieFilmPlayer_Rewind(TieFilmPlayer* p) {
	/* Engine: Rewind_Film_Objects sets cur_cel=0, resets palette to
	   def_palette, calls Rewind_*_Film for each object (which apply
	   cel 0 records), then cur_cel++. After reset all velocity
	   fields are 0, so object_run_cel's velocity_tick is a no-op
	   and the cel-0 path matches the engine's Rewind. */
	p->cur_cel = 0;
	palette_black(&p->palette);
	if (p->def_pltt_data && p->def_pltt_size > 0)
		palette_overlay(&p->palette, p->def_pltt_data, p->def_pltt_size);

	for (int i = 0; i < p->object_count; i++)
		TieFilmPlayer_RewindObjectState(p, &p->objects[i]);

	/* var1 is now observable on each TieFilmPlayerObject — re-mark the
	 * is_stream flag so a host that re-rewinds doesn't lose the
	 * binding. The pointer in o->stream is preserved by the rewind
	 * (it's not part of the engine-mirrored state), so this is a
	 * pure consistency refresh. */
	TieFilmPlayer_MarkStreamActors(p);

	/* Rewind any bound stream sessions in lockstep with the FILM
	 * cursor. Engine equivalent: lstream_Use_Stream_File at scene
	 * setup re-positions the CD reader; for our static viewer, fseek
	 * to byte 0 and re-read the header. */
	for (int i = 0; i < p->stream_count; i++)
		TieFilmStreamPlayer_StreamRewind(&p->streams[i]);

	p->cur_cel = 1;
}

void TieFilmPlayer_Step(TieFilmPlayer* p) {
	if (p->header.cels == 0)
		return;
	if (p->cur_cel >= p->header.cels)
		return;
	for (int i = 0; i < p->object_count; i++)
		TieFilmPlayer_ObjectRunCel(p, &p->objects[i], p->cur_cel);

	/* Advance every visible stream actor by one frame after the cel
	 * records have been applied. Mirrors play1_Update_Stream_Actor:
	 * runs only when actor is visible and not in a film fade (we
	 * don't model fade as a multi-tick effect; cel boundary
	 * snapshots are post-fade so this is consistent). EOS leaves the
	 * last decoded frame on screen, matching lactor_Deactivate_Actor. */
	for (int i = 0; i < p->object_count; i++) {
		TieFilmPlayerObject* o = &p->objects[i];
		if (!o->is_stream || !o->stream)
			continue;
		if (!o->show && !o->force_show)
			continue;
		(void)TieFilmStreamPlayer_StreamAdvanceOneFrame(o->stream);
	}

	p->cur_cel++;
}

void TieFilmPlayer_Seek(TieFilmPlayer* p, int target) {
	if (target < 0)
		target = 0;
	if (target >= (int)p->header.cels)
		target = (int)p->header.cels - 1;

	int cur_displayed = (int)p->cur_cel - 1;
	if (target < cur_displayed) {
		TieFilmPlayer_Rewind(p);
		cur_displayed = (int)p->cur_cel - 1;
	}
	while (cur_displayed < target) {
		TieFilmPlayer_Step(p);
		cur_displayed = (int)p->cur_cel - 1;
	}
}

/* ---------- composition ---------- */

bool TieFilmPlayer_ActorScreenRect(const TieFilmPlayerObject* o, int* l, int* t, int* r, int* b) {
	if (o->type_code != FILM_TC_ACTOR)
		return false;
	if (o->w <= 0 || o->h <= 0)
		return false;

	int16_t fl, ft, fr, fb;
	if (o->res_type == FCC_ANIM) {
		TieFilmPlayer_AnimFrameBounds(&o->anim, o->state, &fl, &ft, &fr, &fb);
	} else if (o->sprite.pixels) {
		fl = (int16_t)o->sprite.left;
		ft = (int16_t)o->sprite.top;
		fr = (int16_t)(o->sprite.left + o->sprite.width);
		fb = (int16_t)(o->sprite.top + o->sprite.height);
	} else {
		return false;
	}

	int sx, sy;
	if (o->hflip)
		sx = (int)(o->bounds_l + o->bounds_r - fr) + o->x;
	else
		sx = fl + o->x;
	if (o->vflip)
		sy = (int)(o->bounds_t + o->bounds_b - fb) + o->y;
	else
		sy = ft + o->y;

	*l = sx;
	*t = sy;
	*r = sx + (fr - fl);
	*b = sy + (fb - ft);
	return true;
}

/* Blit a sprite at screen origin (sx, sy), with optional H/V flip,
   clipped to [cl, cr) x [ct, cb) and to the framebuffer. Index 0 is
   transparent (matches engine convention). */
static void TieFilmPlayer_BlitIndexed(uint8_t* fb, int fb_w, int fb_h, const Image8* src, int sx, int sy,
									  bool hflip, bool vflip, int cl, int ct, int cr, int cb) {
	if (!src->pixels)
		return;
	int w = src->width, h = src->height;
	if (w <= 0 || h <= 0)
		return;

	if (cl < 0)
		cl = 0;
	if (ct < 0)
		ct = 0;
	if (cr > fb_w)
		cr = fb_w;
	if (cb > fb_h)
		cb = fb_h;
	if (cl >= cr || ct >= cb)
		return;

	for (int row = 0; row < h; row++) {
		int dy = sy + row;
		if (dy < ct || dy >= cb)
			continue;
		int srow = vflip ? (h - 1 - row) : row;
		const uint8_t* srcrow = src->pixels + (size_t)srow * (size_t)w;
		uint8_t* dstrow = fb + (size_t)dy * (size_t)fb_w;
		for (int col = 0; col < w; col++) {
			int dx = sx + col;
			if (dx < cl || dx >= cr)
				continue;
			int scol = hflip ? (w - 1 - col) : col;
			uint8_t v = srcrow[scol];
			if (v) /* index 0 = transparent */
				dstrow[dx] = v;
		}
	}
}

void TieFilmPlayer_Composite(TieFilmPlayer* p) {
	if (!p->indexed)
		return;
	memset(p->indexed, 0, (size_t)p->fb_w * (size_t)p->fb_h);

	/* Stream actors paint first, full-frame, opaque (engine path:
	 * lcanvas_Copy_Bitmap_To_Canvas — no transparency mask). z=12700
	 * in the engine puts them at the back of the actor list; subsequent
	 * non-stream actors then draw on top. We composite the stream's
	 * 320×200 buffer; if the player's framebuffer is wider (SVGA) the
	 * stream still lands at (0,0) — there's no engine path for SVGA
	 * stream actors, so this just preserves the layout the user can
	 * eyeball. */
	for (int i = 0; i < p->object_count; i++) {
		TieFilmPlayerObject* o = &p->objects[i];
		if (!o->is_stream || !o->stream)
			continue;
		if (o->gui_hidden)
			continue;
		if (!o->show && !o->force_show)
			continue;
		const uint8_t* src = o->stream->cur;
		int copy_w = (STREAM_FB_W < p->fb_w) ? STREAM_FB_W : p->fb_w;
		int copy_h = (STREAM_FB_H < p->fb_h) ? STREAM_FB_H : p->fb_h;
		for (int row = 0; row < copy_h; row++) {
			memcpy(p->indexed + (size_t)row * (size_t)p->fb_w, src + (size_t)row * STREAM_FB_W,
				   (size_t)copy_w);
		}
	}

	/* Gather visible non-CUST actors, then insertion-sort by
	   (zplane DESC, entry_index ASC). Engine equivalent: actor list
	   ordered descending by zplane (Add_Actor_To_System inserts before
	   the first node with zplane <= ours), and iteration head→tail
	   draws highest z first. Insertion sort is fine — typical films
	   have <50 actors and we only resort once per cel boundary. */
	if (p->object_count <= 0)
		return;
	TieFilmPlayerObject** list =
		(TieFilmPlayerObject**)malloc(sizeof(TieFilmPlayerObject*) * (size_t)p->object_count);
	if (!list)
		return;
	int n = 0;
	for (int i = 0; i < p->object_count; i++) {
		TieFilmPlayerObject* o = &p->objects[i];
		if (o->type_code != FILM_TC_ACTOR)
			continue;
		if (o->gui_hidden)
			continue;
		if (!o->show && !o->force_show)
			continue;
		if (o->res_type == FCC_CUST)
			continue;
		list[n++] = o;
	}
	for (int i = 1; i < n; i++) {
		TieFilmPlayerObject* v = list[i];
		int j = i - 1;
		while (j >= 0) {
			TieFilmPlayerObject* u = list[j];
			bool greater =
				(u->zplane < v->zplane) || (u->zplane == v->zplane && u->entry_index > v->entry_index);
			if (!greater)
				break;
			list[j + 1] = u;
			j--;
		}
		list[j + 1] = v;
	}

	/* Iterate descending z (= drawing order back-to-front: lower z lands
	   on top, matching engine). */
	for (int i = 0; i < n; i++) {
		TieFilmPlayerObject* o = list[i];
		const Image8* src = NULL;
		int16_t fl, ft, fr, fb;

		if (o->res_type == FCC_ANIM) {
			int s = o->state;
			int cnt = o->anim.count;
			if (cnt <= 0)
				continue;
			while (s < 0)
				s += cnt;
			while (s >= cnt)
				s -= cnt;
			if (!o->anim.frames[s].pixels)
				continue;
			src = &o->anim.frames[s];
			TieFilmPlayer_AnimFrameBounds(&o->anim, s, &fl, &ft, &fr, &fb);
		} else if (o->sprite.pixels) {
			src = &o->sprite;
			fl = (int16_t)o->sprite.left;
			ft = (int16_t)o->sprite.top;
			fr = (int16_t)(o->sprite.left + o->sprite.width);
			fb = (int16_t)(o->sprite.top + o->sprite.height);
		} else {
			continue;
		}

		int sx = o->hflip ? (o->bounds_l + o->bounds_r - fr) + o->x : fl + o->x;
		int sy = o->vflip ? (o->bounds_t + o->bounds_b - fb) + o->y : ft + o->y;

		TieFilmPlayer_BlitIndexed(p->indexed, p->fb_w, p->fb_h, src, sx, sy, o->hflip, o->vflip, o->frame_l,
								  o->frame_t, o->frame_r, o->frame_b);
	}
	free(list);
}

void TieFilmPlayer_RenderRgba(const TieFilmPlayer* p, uint8_t* rgba) {
	const size_t n = (size_t)p->fb_w * (size_t)p->fb_h;
	for (size_t i = 0; i < n; i++) {
		uint8_t idx = p->indexed[i];
		rgba[i * 4 + 0] = p->palette.rgba[idx][0];
		rgba[i * 4 + 1] = p->palette.rgba[idx][1];
		rgba[i * 4 + 2] = p->palette.rgba[idx][2];
		/* Force opaque output regardless of slot 0's transparent
		   marker — the framebuffer represents the final screen. */
		rgba[i * 4 + 3] = 255;
	}
}

/* ---------- init / free ---------- */

static uint8_t* TieFilmPlayer_DupBytes(const uint8_t* src, uint32_t n) {
	uint8_t* p = (uint8_t*)malloc(n);
	if (!p)
		return NULL;
	memcpy(p, src, n);
	return p;
}

static void TieFilmPlayer_LoadObjectResource(TieFilmPlayerObject* o, const TieLfdFileChain* chain,
											 const char* film_name) {
	const TieLfdFile* owner = NULL;
	const TieLfdFileEntry* e = NULL;
	bool needs_resource = true;

	switch (o->res_type) {
		case FCC_DELT:
			e = TieLfdFileChain_Find(chain, FCC_DELT, o->res_name, &owner);
			if (e)
				decode_delt(&o->sprite, TieLfdFile_Data(owner, e), e->size);
			break;
		case FCC_RAW:
			/* Engine path: lfilm_Res_Film_Object falls back DELT→RAW when
			   it can't find the resource as DELT. The DELT decoder works
			   on RAW too in the existing toolchain. */
			e = TieLfdFileChain_Find(chain, FCC_RAW, o->res_name, &owner);
			if (!e)
				e = TieLfdFileChain_Find(chain, FCC_DELT, o->res_name, &owner);
			if (e)
				decode_delt(&o->sprite, TieLfdFile_Data(owner, e), e->size);
			break;
		case FCC_ANIM:
			e = TieLfdFileChain_Find(chain, FCC_ANIM, o->res_name, &owner);
			if (e)
				decode_anim(&o->anim, TieLfdFile_Data(owner, e), e->size);
			break;
		case FCC_PLTT:
			e = TieLfdFileChain_Find(chain, FCC_PLTT, o->res_name, &owner);
			if (e) {
				o->pltt_data = TieFilmPlayer_DupBytes(TieLfdFile_Data(owner, e), e->size);
				o->pltt_size = e->size;
			}
			break;
		case FCC_CUST:
		case FCC_VIEW:
		case FCC_VOIC:
		case FCC_GMID:
			/* Not visualised by the static viewer — don't bother resolving. */
			needs_resource = false;
			break;
		default:
			needs_resource = false;
			break;
	}

	o->resource_loaded = (e != NULL) || !needs_resource;

	if (needs_resource && !e) {
		char tn[5];
		TieFilmFourcc_Str(o->res_type, tn);
		fprintf(stderr,
				"filmview: FILM '%s' references %s '%s' but no such resource "
				"in the LFD chain — actor will composite to nothing. "
				"Pass --extra <lfd> to add the LFD that owns it "
				"(typically EMPIRE.LFD for shared assets).\n",
				film_name, tn, o->res_name);
	}

	if (o->res_type == FCC_ANIM && o->anim.count > 0) {
		TieFilmPlayer_AnimTotalBounds(&o->anim, &o->bounds_l, &o->bounds_t, &o->bounds_r, &o->bounds_b);
	} else if (o->sprite.pixels) {
		o->bounds_l = (int16_t)o->sprite.left;
		o->bounds_t = (int16_t)o->sprite.top;
		o->bounds_r = (int16_t)(o->sprite.left + o->sprite.width);
		o->bounds_b = (int16_t)(o->sprite.top + o->sprite.height);
	}
	TieFilmPlayer_ObjectUpdateSize(o);
}

bool TieFilmPlayer_InitWithDims(TieFilmPlayer* p, const TieLfdFileChain* chain, const TieLfdFile* primary,
								const TieLfdFileEntry* film_entry, int fb_w, int fb_h) {
	memset(p, 0, sizeof *p);
	if (fb_w <= 0 || fb_h <= 0)
		return false;
	p->chain = chain;
	p->fb_w = fb_w;
	p->fb_h = fb_h;

	memcpy(p->film_name, film_entry->name, 8);
	p->film_name[8] = '\0';

	if (!TieFilm_Parse(&p->header, TieLfdFile_Data(primary, film_entry), film_entry->size))
		return false;
	if (p->header.version != 4)
		return false;

	/* Allocate the indexed framebuffer. Sized at fb_w × fb_h, zeroed
	   per-cel by player_composite; freed by player_free. */
	p->indexed = (uint8_t*)calloc((size_t)fb_w * (size_t)fb_h, 1);
	if (!p->indexed)
		return false;

	/* Load def_palette = "standard" PLTT (engine: Film->def_palette,
	   typically set by the caller from EMPIRE.LFD's `standard`). */
	const TieLfdFile* std_owner = NULL;
	const TieLfdFileEntry* std_e = TieLfdFileChain_Find(chain, FCC_PLTT, "standard", &std_owner);
	if (std_e) {
		p->def_pltt_data = TieFilmPlayer_DupBytes(TieLfdFile_Data(std_owner, std_e), std_e->size);
		p->def_pltt_size = std_e->size;
	}

	/* Walk FilmObject array. */
	p->objects = (TieFilmPlayerObject*)calloc((size_t)p->header.array_size, sizeof(TieFilmPlayerObject));
	if (!p->objects)
		return false;

	uint32_t iter = 0;
	for (uint16_t i = 0; i < p->header.array_size; i++) {
		TieFilmEntry e;
		if (!TieFilm_EntryNext(&p->header, &iter, &e))
			break;
		TieFilmPlayerObject* o = &p->objects[p->object_count++];
		o->res_type = e.res_type;
		memcpy(o->res_name, e.res_name, 9);
		o->entry_index = i;
		o->type_code = e.type_code;
		o->records = e.records;
		o->records_size = e.records_size;
		TieFilmPlayer_LoadObjectResource(o, chain, p->film_name);
	}

	p->cur_cel = 0;
	TieFilmPlayer_Rewind(p);
	TieFilmPlayer_Composite(p);
	return true;
}

bool TieFilmPlayer_Init(TieFilmPlayer* p, const TieLfdFileChain* chain, const TieLfdFile* primary,
						const TieLfdFileEntry* film_entry) {
	return TieFilmPlayer_InitWithDims(p, chain, primary, film_entry, PLAYER_FB_W, PLAYER_FB_H);
}

void TieFilmPlayer_Free(TieFilmPlayer* p) {
	for (int i = 0; i < p->object_count; i++) {
		image_free(&p->objects[i].sprite);
		anim_free(&p->objects[i].anim);
		free(p->objects[i].pltt_data);
	}
	for (int i = 0; i < p->stream_count; i++)
		TieFilmStreamPlayer_StreamClose(&p->streams[i]);
	free(p->objects);
	free(p->def_pltt_data);
	free(p->indexed);
	memset(p, 0, sizeof *p);
}

/* ---------- stream actor binding ---------- */

/* Refresh per-object is_stream flags after rewind/step. Engine path:
 * play1_film_Callback fires once on FilmObject creation and inspects
 * var1 only. We mirror that timing — the cel-0 records run during
 * player_init/rewind, so calling this immediately after gives the
 * same set of stream actors. */
static void TieFilmPlayer_MarkStreamActors(TieFilmPlayer* p) {
	for (int i = 0; i < p->object_count; i++) {
		TieFilmPlayerObject* o = &p->objects[i];
		o->is_stream = (o->res_type == FCC_CUST && o->type_code == FILM_TC_ACTOR && o->var1 == 123);
	}
}

int TieFilmPlayer_FindStreamActor(const TieFilmPlayer* p) {
	for (int i = 0; i < p->object_count; i++) {
		const TieFilmPlayerObject* o = &p->objects[i];
		if (o->res_type == FCC_CUST && o->type_code == FILM_TC_ACTOR && o->var1 == 123)
			return i;
	}
	return -1;
}

/* Internal: drop the TieFilmStreamSession attached to `o`, compact the
 * pool, and reseat back-pointers in any other TieFilmPlayerObject that
 * referenced a slot above the removed one. */
static void TieFilmPlayer_DetachStream(TieFilmPlayer* p, TieFilmPlayerObject* o) {
	if (!o->stream)
		return;
	TieFilmStreamSession* removed = o->stream;
	TieFilmStreamPlayer_StreamClose(removed);

	int slot = (int)(removed - p->streams);
	/* Reseat other actors' back-pointers: anyone with a stream
	 * sitting above the removed slot moves down by one. */
	for (int j = 0; j < p->object_count; j++) {
		TieFilmPlayerObject* q = &p->objects[j];
		if (q == o)
			continue;
		if (!q->stream)
			continue;
		if (q->stream > removed)
			q->stream--;
	}
	for (int k = slot; k + 1 < p->stream_count; k++)
		p->streams[k] = p->streams[k + 1];
	memset(&p->streams[p->stream_count - 1], 0, sizeof p->streams[0]);
	p->stream_count--;
	o->stream = NULL;
}

bool TieFilmPlayer_BindStream(TieFilmPlayer* p, int entry_index, const char* wrk_path) {
	if (!p || !wrk_path || !*wrk_path)
		return false;
	if (entry_index < 0 || entry_index >= p->object_count)
		return false;
	TieFilmPlayerObject* o = &p->objects[entry_index];
	if (!(o->res_type == FCC_CUST && o->type_code == FILM_TC_ACTOR && o->var1 == 123)) {
		o->is_stream = false;
		return false;
	}

	/* If a session is already attached (re-bind), tear it down first
	 * — the host might be swapping a wrong path. */
	TieFilmPlayer_DetachStream(p, o);

	if (p->stream_count >= PLAYER_MAX_STREAMS)
		return false;

	TieFilmStreamSession* s = &p->streams[p->stream_count];
	if (!TieFilmStreamPlayer_StreamOpen(s, wrk_path)) {
		memset(s, 0, sizeof *s);
		return false;
	}
	p->stream_count++;
	o->stream = s;
	o->is_stream = true;
	return true;
}

void TieFilmPlayer_UnbindStream(TieFilmPlayer* p, int entry_index) {
	if (!p)
		return;
	if (entry_index < 0 || entry_index >= p->object_count)
		return;
	TieFilmPlayer_DetachStream(p, &p->objects[entry_index]);
}
