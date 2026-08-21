#include <landru/actor.h>
#include <landru/canvas.h>
#include <landru/memptr.h>
#include <landru/rect.h>
#include <landru/render.h>
#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

#include "render_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: TIE 0xFBB80
static ActorType* actor_type_gbl;
// GLOBAL: TIE 0xD2C1C
static Actor* actor_list_gbl;
// GLOBAL: TIE 0xD2C22
static bool actor_module_gbl;
// GLOBAL: TIE 0xFBB84
static bool refresh_actors_gbl;
// GLOBAL: TIE 0xD2C20
static bool zplane_build_gbl;

/* --- Module lifecycle --- */

void lactor_Create_Actor_Module(void) {
	actor_type_gbl = NULL;
	actor_list_gbl = NULL;
	actor_module_gbl = true;
}

void lactor_Destroy_Actor_Module(void) { actor_module_gbl = false; }

/* --- Actor type registry --- */

bool lactor_Create_Actor_Type(uint32_t type, lactorFrameFunc frameFunc, lactorStateFunc stateFunc) {
	ActorType* t = lmemptr_Alloc_Clear_System_Pointer(sizeof(ActorType));
	if (t) {
		t->type = type;
		t->frameFunc = frameFunc;
		t->stateFunc = stateFunc;
		t->next = actor_type_gbl;
		actor_type_gbl = t;
	}
	return t != NULL;
}

void lactor_Destroy_Actor_Type(uint32_t type) {
	ActorType* cur = actor_type_gbl;
	ActorType* prev = NULL;
	while (cur && cur->type != type) {
		prev = cur;
		cur = cur->next;
	}
	if (cur) {
		if (prev)
			prev->next = cur->next;
		else
			actor_type_gbl = actor_type_gbl->next;
		lmemptr_Free_System_Pointer(cur);
	}
}

ActorType* lactor_Find_Actor_Type(uint32_t type) {
	for (ActorType* t = actor_type_gbl; t; t = t->next)
		if (t->type == type)
			return t;
	return NULL;
}

/* --- Actor list management --- */

Actor* lactor_Ask_Actor_List(void) { return actor_list_gbl; }

void lactor_Add_Actor_To_System(Actor* actor) {
	Actor* cur = actor_list_gbl;
	Actor* prev = NULL;
	while (cur) {
		if (actor->zplane >= cur->zplane)
			break;
		prev = cur;
		cur = cur->next;
	}
	actor->next = cur;
	if (prev)
		prev->next = actor;
	else
		actor_list_gbl = actor;
	actor->id = 0;
}

void lactor_Free_Actor_From_System(Actor* actor) {
	Actor* cur = actor_list_gbl;
	Actor* prev = NULL;
	while (cur && cur != actor) {
		prev = cur;
		cur = cur->next;
	}
	if (cur) {
		if (prev)
			prev->next = cur->next;
		else
			actor_list_gbl = cur->next;
		actor->next = NULL;
	}
}

/* --- Alloc / Free --- */

Actor* lactor_Alloc_Actor(int16_t extend) {
	Actor* actor = lmemptr_Alloc_Clear_System_Pointer(sizeof(Actor) + extend);
	if (actor) {
		actor->start = 0;
		actor->stop = -1;
		actor->flags = AF_DIRTY | AF_REFRESHABLE | AF_REFRESH;
		actor->foreColor = 15;
		actor->backColor = 0;
		actor->xscale = 256;
		actor->yscale = 256;
		actor->prev_xscale = 256;
		actor->prev_yscale = 256;
		actor->next = NULL;
		actor->film_entry_index = -1;
	}
	return actor;
}

void lactor_Free_Actor(Actor* actor) {
	lactor_Free_Actor_Data(actor);
	if (actor->varptr)
		lmemptr_Free_System_Pointer(actor->varptr);
	if (actor->varhdl)
		free(actor->varhdl);
	lmemptr_Free_System_Pointer(actor);
}

void lactor_Free_Actor_Data(Actor* actor) {
	if (lactor_Is_Discard_Actor_Data(actor)) {
		if (actor->data)
			free(actor->data);
		if (actor->array) {
			for (int16_t i = 0; i < actor->arraySize; i++) {
				if (actor->array[i])
					free(actor->array[i]);
			}
			free(actor->array);
		}
	}
	actor->array = NULL;
	actor->arraySize = 0;
	actor->data = NULL;
}

void lactor_Free_Actors(Actor* actor) {
	while (actor) {
		Actor* next = actor->next;
		lactor_Free_Actor_From_System(actor);
		lactor_Free_Actor(actor);
		actor = next;
	}
}

/* --- Draw / Update / User loops --- */

void lactor_Refresh_Actors(void) { refresh_actors_gbl = true; }

void lactor_Draw_Actors(int16_t refresh) {
	int16_t do_refresh = refresh | refresh_actors_gbl;
	refresh_actors_gbl = false;

	Actor* head = actor_list_gbl;
	for (int16_t vi = 0; vi < 4; vi++) {
		Rect r;
		lview_Get_View_Frame(vi, &r);
		if (lrect_Empty_Rect(&r))
			continue;

		for (Actor* cur = head; cur; cur = cur->next) {
			ltimer_Often();
			if (!cur->draw)
				continue;
			if (!lactor_Is_Actor_Visible(cur))
				continue;

			Rect clip;
			if (!lviewadd_Clip_Object_To_View(vi, cur->zplane, &cur->frame, &r, &clip))
				continue;

			int16_t needs_draw = do_refresh | lactor_Is_Actor_Refresh(cur) | lactor_Is_Actor_Refreshable(cur);

			lcanvas_Set_Drawing_Canvas_Clip(&clip);
			int16_t rel_x, rel_y;
			lactor_Get_Actor_Relative_XY(cur, &r, &rel_x, &rel_y);
			cur->draw(cur, &r, &clip, rel_x, rel_y, needs_draw);
		}
	}

	for (Actor* it = head; it; it = it->next)
		lactor_Non_Refresh_Actor(it);
}

void lactor_Update_Actors(int32_t time) {
	for (Actor* cur = actor_list_gbl; cur; cur = cur->next) {
		if (time == cur->start) {
			lactor_Start_Actor(cur);
			if (cur->start == cur->stop)
				lactor_Stop_Actor(cur);
			continue;
		}
		if (time == cur->stop) {
			lactor_Stop_Actor(cur);
		} else if (cur->update && lactor_Is_Actor_Active(cur)) {
			cur->update(cur);
		}
	}
}

void lactor_User_Actors(int32_t time) {
	for (Actor* cur = actor_list_gbl; cur; cur = cur->next)
		if (cur->user)
			cur->user(cur, time);
}

/* --- Find --- */

Actor* lactor_Find_Actor(uint32_t type, const char* name) {
	for (Actor* cur = actor_list_gbl; cur; cur = cur->next) {
		if (cur->res_type != type)
			continue;
		bool match = true;
		int i;
		for (i = 0; i < 8 && match && name[i]; i++) {
			if (tolower((unsigned char)name[i]) != (unsigned char)cur->res_name[i])
				match = false;
		}
		if (match && (i >= 8 || !cur->res_name[i]))
			return cur;
	}
	return NULL;
}

/* --- Flag accessors --- */

void lactor_Show_Actor(Actor* actor) { actor->flags |= AF_VISIBLE; }
void lactor_Hide_Actor(Actor* actor) { actor->flags &= ~AF_VISIBLE; }
bool lactor_Is_Actor_Visible(Actor* actor) { return actor->flags & AF_VISIBLE; }
void lactor_Activate_Actor(Actor* actor) { actor->flags |= AF_ACTIVE; }
void lactor_Deactivate_Actor(Actor* actor) { actor->flags &= ~AF_ACTIVE; }
bool lactor_Is_Actor_Active(Actor* actor) { return actor->flags & AF_ACTIVE; }
void lactor_Start_Actor(Actor* actor) { actor->flags |= (AF_VISIBLE | AF_ACTIVE); }
void lactor_Stop_Actor(Actor* actor) { actor->flags &= ~(AF_VISIBLE | AF_ACTIVE); }
void lactor_Dirty_Actor(Actor* actor) { actor->flags |= AF_DIRTY; }
void lactor_Non_Dirty_Actor(Actor* actor) { actor->flags &= ~AF_DIRTY; }
bool lactor_Is_Actor_Dirty(Actor* actor) { return actor->flags & AF_DIRTY; }
void lactor_Refresh_Actor(Actor* actor) { actor->flags |= AF_REFRESH; }
void lactor_Non_Refresh_Actor(Actor* actor) { actor->flags &= ~AF_REFRESH; }
bool lactor_Is_Actor_Refresh(Actor* actor) { return actor->flags & AF_REFRESH; }
void lactor_Refreshable_Actor(Actor* actor) { actor->flags |= AF_REFRESHABLE; }
void lactor_Non_Refreshable_Actor(Actor* actor) { actor->flags &= ~AF_REFRESHABLE; }
bool lactor_Is_Actor_Refreshable(Actor* actor) { return actor->flags & AF_REFRESHABLE; }
void lactor_Discard_Actor_Data(Actor* actor) { actor->flags |= AF_DISCARD; }
void lactor_Non_Discard_Actor_Data(Actor* actor) { actor->flags &= ~AF_DISCARD; }
bool lactor_Is_Discard_Actor_Data(Actor* actor) { return actor->flags & AF_DISCARD; }
void lactor_Set_Actor_Flag1(Actor* actor) { actor->flags |= AF_FLAG1; }
void lactor_Clear_Actor_Flag1(Actor* actor) { actor->flags &= ~AF_FLAG1; }
bool lactor_Is_Actor_Flag1(Actor* actor) { return actor->flags & AF_FLAG1; }
void lactor_Set_Actor_Flag2(Actor* actor) { actor->flags |= AF_FLAG2; }
void lactor_Clear_Actor_Flag2(Actor* actor) { actor->flags &= ~AF_FLAG2; }
bool lactor_Is_Actor_Flag2(Actor* actor) { return actor->flags & AF_FLAG2; }

/* --- Movement --- */

void lactor_Move_Actor(Actor* actor) {
	/* Capture the pre-integration position and the velocity being integrated.
	 * Render state ships (prev_x, x) per cel for consumer interpolation.
	 * prev_xv/yv let the consumer's teleport check compare against the
	 * vel that produced dx — the FILM script may rewrite actor->xv
	 * for the NEXT cel after Move_Actor returns (FCMD_ACTOR_VEL is
	 * processed inside lfilm_Step_Actor_Film AFTER the actor->update
	 * call), and using the post-script xv would misflag legitimate
	 * velocity-change cels (e.g. logoluke 15→12 at cel 10) as
	 * teleports. */
	actor->prev_x = actor->x;
	actor->prev_y = actor->y;
	actor->prev_xv = actor->xv;
	actor->prev_yv = actor->yv;
	/* Scale has no engine-driven velocity — user callbacks (e.g.
	 * title.c's Star-Wars-logo zoom) write actor->xscale/yscale
	 * directly. Capture the cel-start scale here so the render state
	 * ships (prev → current) for the compositor to lerp. */
	actor->prev_xscale = actor->xscale;
	actor->prev_yscale = actor->yscale;
	actor->xf += actor->xvf;
	actor->yf += actor->yvf;
	actor->x += actor->xv + actor->xf / 256;
	actor->y += actor->yv + actor->yf / 256;
	while (actor->xf >= 256)
		actor->xf -= 256;
	while (actor->yf >= 256)
		actor->yf -= 256;
	while (actor->xf <= -256)
		actor->xf += 256;
	while (actor->yf <= -256)
		actor->yf += 256;
}

void lactor_Move_Actor_Frame(Actor* actor) {
	actor->frame.left += actor->frame_v.left;
	actor->frame.top += actor->frame_v.top;
	actor->frame.right += actor->frame_v.right;
	actor->frame.bottom += actor->frame_v.bottom;
}

void lactor_Move_Actor_State(Actor* actor) {
	actor->state_f += actor->state_vf;
	actor->state += actor->state_v + actor->state_f / 256;
	while (actor->state_f >= 256)
		actor->state_f -= 256;
	while (actor->state_f <= -256)
		actor->state_f += 256;
}

/* --- Data copy --- */

void lactor_Copy_Actor_Data(Actor* dst, Actor* src) {
	lactor_Free_Actor_Data(dst);
	lactor_Non_Discard_Actor_Data(dst);
	lactor_Set_Actor_Name(dst, src->res_type, src->res_name);
	dst->data = src->data;
	dst->array = src->array;
	dst->arraySize = src->arraySize;
	lrect_Copy_Rect(&dst->bounds, &src->bounds);
	dst->w = src->w;
	dst->h = src->h;
}

bool lactor_Clip_Actor_To_Actor(Actor* actor, Actor* clip_to) {
	return lrect_Clip_Rect(&actor->frame, &clip_to->frame);
}

/* --- Z-plane sorting --- */

static void sort_actor_list(void) {
	Actor* sorted = NULL;
	Actor* cur = actor_list_gbl;
	while (cur) {
		Actor* node = cur;
		cur = cur->next;
		node->next = NULL;
		Actor *pos = sorted, *prev = NULL;
		while (pos && pos->zplane >= node->zplane) {
			prev = pos;
			pos = pos->next;
		}
		if (prev)
			prev->next = node;
		else
			sorted = node;
		if (pos)
			node->next = pos;
	}
	actor_list_gbl = sorted;
}

void lactor_Check_Actor_ZPlanes(void) {
	if (!zplane_build_gbl)
		return;
	zplane_build_gbl = false;
	sort_actor_list();
}

void lactor_Sort_Actor_ZPlanes(void) { sort_actor_list(); }

/* --- Position / coordinate helpers --- */

void lactor_Get_Actor_Relative_XY(Actor* actor, Rect* rect, int16_t* outX, int16_t* outY) {
	*outX = rect->left + actor->x - actor->frame.left;
	*outY = rect->top + actor->y - actor->frame.top;
}

void lactor_Get_Actor_Offset(Actor* actor, int16_t* outX, int16_t* outY) {
	ActorType* t = lactor_Find_Actor_Type(actor->res_type);
	if (t && t->frameFunc) {
		Rect frame;
		t->frameFunc(actor, &frame);
		*outX = frame.left;
		*outY = frame.top;
	} else {
		*outX = 0;
		*outY = 0;
	}
}

void lactor_Set_Actor_Name(Actor* actor, uint32_t res_type, const char* name) {
	actor->res_type = res_type;
	int i;
	for (i = 0; i < 8 && name[i]; i++)
		actor->res_name[i] = tolower((unsigned char)name[i]);
	for (; i < 8; i++)
		actor->res_name[i] = 0;
}

/* --- Getters / Setters --- */

void lactor_Set_Actor_Frame(Actor* actor, Rect* frame) { lrect_Copy_Rect(&actor->frame, frame); }
void lactor_Get_Actor_Frame(Actor* actor, Rect* out) { lrect_Copy_Rect(out, &actor->frame); }
void lactor_Set_Actor_Bounds(Actor* actor, Rect* b) { lrect_Copy_Rect(&actor->bounds, b); }
void lactor_Get_Actor_Bounds(Actor* actor, Rect* out) { lrect_Copy_Rect(out, &actor->bounds); }

void lactor_Set_Actor_Pos(Actor* actor, int16_t x, int16_t y, int16_t xf, int16_t yf) {
	actor->x = x;
	actor->y = y;
	actor->xf = xf;
	actor->yf = yf;
	/* Reseed prev_x/y so a freshly-placed actor (or one that's just
	 * teleported via FCMD_ACTOR_POS / Rewind_Actor_Film) doesn't
	 * render as a slide from its previous-cel commit position. Zero
	 * prev_xv/yv so the teleport-detection tolerance matches the
	 * (zero) integration that just "happened" — i.e. the lerp
	 * collapses to a no-op at the snap target. */
	actor->prev_x = x;
	actor->prev_y = y;
	actor->prev_xv = 0;
	actor->prev_yv = 0;
}

void lactor_Get_Actor_Pos(Actor* actor, int16_t* x, int16_t* y, int16_t* xf, int16_t* yf) {
	*x = actor->x;
	*y = actor->y;
	*xf = actor->xf;
	*yf = actor->yf;
}

void lactor_Get_Actor_Center(Actor* actor, int16_t* cx, int16_t* cy) {
	*cx = actor->x + actor->w / 2;
	*cy = actor->y + actor->h / 2;
}

void lactor_Set_Actor_Size(Actor* actor, int16_t w, int16_t h) {
	actor->w = w;
	actor->h = h;
}
void lactor_Get_Actor_Size(Actor* actor, int16_t* w, int16_t* h) {
	*w = actor->w;
	*h = actor->h;
}

void lactor_Get_Actor_Rect(Actor* actor, Rect* out) {
	lactor_Get_Actor_Base_Rect(actor, out);
	lrect_Offset_Rect(out, actor->x, actor->y);
}

void lactor_Get_Actor_Base_Rect(Actor* actor, Rect* out) {
	ActorType* t = lactor_Find_Actor_Type(actor->res_type);
	if (t && t->frameFunc)
		t->frameFunc(actor, out);
	else
		lrect_Set_Rect(out, 0, 0, actor->w, actor->h);
}

void lactor_Set_Actor_ZPlane(Actor* actor, int16_t z) {
	actor->zplane = z;
	zplane_build_gbl = true;
}
int16_t lactor_Get_Actor_ZPlane(Actor* actor) { return actor->zplane; }
void lactor_Set_Actor_Time(Actor* actor, int32_t s, int32_t e) {
	actor->start = s;
	actor->stop = e;
}
void lactor_Get_Actor_Time(Actor* actor, int32_t* s, int32_t* e) {
	*s = actor->start;
	*e = actor->stop;
}

void lactor_Set_Actor_State(Actor* actor, int16_t state, int16_t fract) {
	ActorType* t = lactor_Find_Actor_Type(actor->res_type);
	if (t && t->stateFunc)
		t->stateFunc(actor, state, fract);
	else {
		actor->state = state;
		actor->state_f = fract;
	}
}

void lactor_Get_Actor_State(Actor* actor, int16_t* s, int16_t* f) {
	*s = actor->state;
	*f = actor->state_f;
}
void lactor_Set_Actor_State_Speed(Actor* actor, int16_t v, int16_t f) {
	actor->state_v = v;
	actor->state_vf = f;
}
void lactor_Get_Actor_State_Speed(Actor* actor, int16_t* v, int16_t* f) {
	*v = actor->state_v;
	*f = actor->state_vf;
}
void lactor_Set_Actor_Color(Actor* actor, int16_t fg, int16_t bg) {
	actor->foreColor = fg;
	actor->backColor = bg;
}
void lactor_Get_Actor_Color(Actor* actor, int16_t* fg, int16_t* bg) {
	*fg = actor->foreColor;
	*bg = actor->backColor;
}

void lactor_Set_Actor_Remap_Color(Actor* actor, int16_t color) {
	actor->foreColor = color;
	actor->flags |= AF_REMAP_COLOR;
}
int16_t lactor_Get_Actor_Remap_Color(Actor* actor) { return actor->foreColor; }
void lactor_Clear_Actor_Remap_Color(Actor* actor) { actor->flags &= ~AF_REMAP_COLOR; }

void lactor_Set_Actor_Scale(Actor* actor, int16_t xs, int16_t ys) {
	actor->xscale = xs;
	actor->yscale = ys;
	/* No prev_xscale/yscale reseed — unlike Set_Actor_Pos (whose
	 * caller is the FILM script's ACTOR_POS teleport opcode),
	 * Set_Actor_Scale is invoked by user callbacks running INSIDE
	 * each cel as the per-frame step of a smooth scale animation
	 * (title.c's Star-Wars-logo zoom is the canonical case).
	 * Reseeding here would set prev = current, killing the lerp;
	 * letting it ride keeps prev = cel-start scale captured at
	 * Move_Actor, so a consumer can interpolate prev to current. */
}
void lactor_Get_Actor_Scale(Actor* actor, int16_t* xs, int16_t* ys) {
	*xs = actor->xscale;
	*ys = actor->yscale;
}

void lactor_Set_Actor_Flip(Actor* actor, int16_t hFlip, int16_t vFlip) {
	if (hFlip)
		actor->flags |= AF_HFLIP;
	else
		actor->flags &= ~AF_HFLIP;
	if (vFlip)
		actor->flags |= AF_VFLIP;
	else
		actor->flags &= ~AF_VFLIP;
}

void lactor_Get_Actor_Flip(Actor* actor, int16_t* hFlip, int16_t* vFlip) {
	*hFlip = actor->flags & AF_HFLIP;
	*vFlip = actor->flags & AF_VFLIP;
}

static uint16_t actor_render_flags(uint16_t flags) {
	uint16_t result = 0;
	if (flags & AF_VISIBLE)
		result |= LANDRU_ACTOR_RENDER_VISIBLE;
	if (flags & AF_ACTIVE)
		result |= LANDRU_ACTOR_RENDER_ACTIVE;
	if (flags & AF_HFLIP)
		result |= LANDRU_ACTOR_RENDER_HFLIP;
	if (flags & AF_VFLIP)
		result |= LANDRU_ACTOR_RENDER_VFLIP;
	if (flags & AF_REMAP_COLOR)
		result |= LANDRU_ACTOR_RENDER_REMAP_COLOR;
	return result;
}

void lactor_Set_Actor_Speed(Actor* actor, int16_t xv, int16_t yv, int16_t xvf, int16_t yvf) {
	actor->xv = xv;
	actor->yv = yv;
	actor->xvf = xvf;
	actor->yvf = yvf;
}

void lactor_Get_Actor_Speed(Actor* actor, int16_t* xv, int16_t* yv, int16_t* xvf, int16_t* yvf) {
	*xv = actor->xv;
	*yv = actor->yv;
	*xvf = actor->xvf;
	*yvf = actor->yvf;
}

void lactor_Set_Actor_Frame_Speed(Actor* actor, int16_t l, int16_t t, int16_t r, int16_t b) {
	lrect_Set_Rect(&actor->frame_v, l, t, r, b);
}

void lactor_Get_Actor_Frame_Speed(Actor* actor, int16_t* l, int16_t* t, int16_t* r, int16_t* b) {
	*l = actor->frame_v.left;
	*t = actor->frame_v.top;
	*r = actor->frame_v.right;
	*b = actor->frame_v.bottom;
}

void lactor_Set_Actor_Data(Actor* actor, void* data) { actor->data = data; }
void lactor_Get_Actor_Data(Actor* actor, void** out) { *out = actor->data; }
void lactor_Set_Actor_Array(Actor* actor, void** array) { actor->array = array; }
void lactor_Get_Actor_Array(Actor* actor, void*** out) { *out = actor->array; }

void* lactor_Get_Actor_Array_Data(Actor* actor, int16_t index) {
	if (!actor->array)
		return NULL;
	return actor->array[index];
}

void lactor_Set_Actor_Draw_Function(Actor* actor, lactorDrawFunc f) { actor->draw = f; }
void lactor_Get_Actor_Draw_Function(Actor* actor, lactorDrawFunc* f) { *f = actor->draw; }
void lactor_Set_Actor_Update_Function(Actor* actor, lactorUpdateFunc f) { actor->update = f; }
void lactor_Get_Actor_Update_Function(Actor* actor, lactorUpdateFunc* f) { *f = actor->update; }
void lactor_Set_Actor_User_Function(Actor* actor, lactorCallback f) { actor->user = f; }
void lactor_Get_Actor_User_Function(Actor* actor, lactorCallback* f) { *f = actor->user; }

/* --- Render capture --- */

void lactor_fill_render_state(const Actor* a, LandruActorRenderState* out) {
	if (!a || !out)
		return;
	out->res_type = a->res_type;
	/* res_name is fixed 8 bytes in both source and dest. */
	memcpy(out->res_name, a->res_name, sizeof out->res_name);
	out->id = a->id;
	out->film_entry_index = a->film_entry_index;

	/* Ship the SCREEN POSITION of the actor's current frame,
	 * not the FILM-driven offset. The engine's draw path lays
	 * the actor at `frame.left + actor->x` where frame is the
	 * current bbox via the actor type's frameFunc.
	 *
	 * AF_HFLIP / AF_VFLIP also relocate the bbox within the
	 * actor's enclosing bounds — see
	 * lactdelt_Draw_Flipped_Clipped_Delta:
	 *
	 *     br_x = bounds.right + bounds.left - 1
	 *     left = (br_x - hdr[2]) + xoff
	 *
	 * For ANIMs whose frames sit at different bbox positions
	 * (e.g. tie07 in city1_f, where frame 0 is right-side and
	 * frame N is left-side) HFLIP mirrors each frame across the
	 * enclosing-bounds center. Compute the same flipped-frame
	 * origin here so renderers see the engine's actual screen
	 * position. For DELT/RAW with a single frame the bounds
	 * equals the frame and the flipped origin reduces to
	 * frame.left — no behaviour change. */
	int16_t fx = a->bounds.left, fy = a->bounds.top;
	ActorType* t = lactor_Find_Actor_Type(a->res_type);
	if (t && t->frameFunc) {
		Rect frame;
		t->frameFunc((Actor*)a, &frame);
		if (a->flags & AF_HFLIP)
			fx = (int16_t)(a->bounds.left + a->bounds.right - frame.right);
		else
			fx = frame.left;
		if (a->flags & AF_VFLIP)
			fy = (int16_t)(a->bounds.top + a->bounds.bottom - frame.bottom);
		else
			fy = frame.top;
	}
	/* Emit pre-scale, pre-recentering position and dimensions.
	 * Renderers apply Watcom Q8 scale (256 = identity, set by
	 * lactor_Set_Actor_Scale) on their own — see LandruActorRenderState
	 * for the formula. Pre-applying integer recentering on the
	 * emit side introduced ±0.5-pixel jitter as xscale parity flipped;
	 * doing the math in floating point on the
	 * consumer side keeps the scaled bbox center stable to sub-
	 * pixel precision. The classic-FB renderer (lactdelt_Draw_
	 * Scaled_Clipped_Delta) still uses the same int recenter
	 * math, so framebuffer pixels are unaffected. */
	out->x = (int16_t)(fx + a->x);
	out->y = (int16_t)(fy + a->y);
	out->w = a->w;
	out->h = a->h;
	out->xscale = a->xscale;
	out->yscale = a->yscale;
	out->zplane = a->zplane;
	out->state = a->state;
	out->flags = actor_render_flags(a->flags);

	/* Sub-cel smoothing channel: ship the pre-Move_Actor
	 * position alongside the velocity that produced this cel's
	 * delta. The frame-origin offset (fx/fy) is the same for
	 * the pre and post positions because frameFunc is keyed on
	 * actor state which doesn't change inside Move_Actor — so
	 * we can apply the same offset to actor->prev_x/y as we
	 * did to actor->x/y. */
	out->prev_x = (int16_t)(fx + a->prev_x);
	out->prev_y = (int16_t)(fy + a->prev_y);
	out->prev_xv = a->prev_xv;
	out->prev_yv = a->prev_yv;
	out->xv = a->xv;
	out->yv = a->yv;
	out->xvf = a->xvf;
	out->yvf = a->yvf;
	out->prev_xscale = a->prev_xscale;
	out->prev_yscale = a->prev_yscale;

	/* ACTOR_CLIP target. Default = canvas bounds (0,0,320,200)
	 * which is a no-op for renderers; FILM-driven ACTOR_CLIP
	 * commands narrow it. */
	out->clip_left = a->frame.left;
	out->clip_top = a->frame.top;
	out->clip_right = a->frame.right;
	out->clip_bottom = a->frame.bottom;

	/* Palette index for AF_REMAP_COLOR. */
	out->fore_color = a->foreColor;
}

void lactor_emit_render_state(void) {
	for (Actor* a = actor_list_gbl; a; a = a->next) {
		/* Skip actors whose callback makes their standard pose an invalid
		 * description of the rendered output. */
		if (a->flags & AF_NO_RENDER_CAPTURE)
			continue;
		LandruActorRenderState state = { 0 };
		lactor_fill_render_state(a, &state);
		landru_render_actor(&state);
	}
}

void lactor_Set_Actor_Render_Capture_Hidden(Actor* actor, bool hidden) {
	if (!actor)
		return;
	if (hidden)
		actor->flags |= AF_NO_RENDER_CAPTURE;
	else
		actor->flags &= ~AF_NO_RENDER_CAPTURE;
}

void lactor_emit_draw(Actor* actor, int16_t xoff, int16_t yoff) {
	if (!actor)
		return;
	/* Non-screen coordinates are published only when the caller explicitly
	 * selects the auxiliary render target. */
	if (!lcanvas_Render_Emit_Allowed())
		return;
	LandruDrawCommand command = { 0 };
	LandruDrawCommand* out = &command;
	out->res_type = actor->res_type;
	memcpy(out->res_name, actor->res_name, sizeof out->res_name);
	out->film_entry_index = actor->film_entry_index;

	/* xoff/yoff are FRAME-RELATIVE, not screen-absolute. The standard
	 * draw pass passes `rect.left + actor.x - frame.left` (per
	 * lactor_Get_Actor_Relative_XY); imperative callers pass values that
	 * the rendering loop combines with
	 * the resource data's natural origin via `frame.left + xoff`.
	 * Recover screen-space position the same way lactor_emit_render_state
	 * does for actors_2d: query the actor type's frameFunc for the
	 * current frame's natural rect, account for HFLIP/VFLIP, then add
	 * xoff/yoff. This makes draws_2d positions directly comparable to
	 * actors_2d positions for the same actor at the same instant. */
	int16_t fx = actor->bounds.left, fy = actor->bounds.top;
	ActorType* t = lactor_Find_Actor_Type(actor->res_type);
	if (t && t->frameFunc) {
		Rect frame;
		t->frameFunc(actor, &frame);
		if (actor->flags & AF_HFLIP)
			fx = (int16_t)(actor->bounds.left + actor->bounds.right - frame.right);
		else
			fx = frame.left;
		if (actor->flags & AF_VFLIP)
			fy = (int16_t)(actor->bounds.top + actor->bounds.bottom - frame.bottom);
		else
			fy = frame.top;
	}
	out->x = (int16_t)(fx + xoff);
	out->y = (int16_t)(fy + yoff);
	out->w = actor->w;
	out->h = actor->h;
	out->state = actor->state;
	/* Effective clip = current drawing canvas clip ∩ actor->frame.
	 * The canvas clip is what classic actually rasters against (set by
	 * the actor pass to the actor's view-clipped rect, or by the input
	 * pass to the input's frame when an idraw_* delegates the actor
	 * draw — e.g. map.c idraw_Map calling lactanim_Draw_Anim_Actor on
	 * cmbticons inside a 21×32 button rect). actor->frame alone misses
	 * the input-pass narrowing and lets the cel bleed past the input's
	 * visible region (visible at the brfbutns/brfpnl seam). */
	Rect canvas_clip;
	lcanvas_Get_Drawing_Canvas_Clip(&canvas_clip);
	int16_t cl = canvas_clip.left > actor->frame.left ? canvas_clip.left : actor->frame.left;
	int16_t ct = canvas_clip.top > actor->frame.top ? canvas_clip.top : actor->frame.top;
	int16_t cr = canvas_clip.right < actor->frame.right ? canvas_clip.right : actor->frame.right;
	int16_t cb = canvas_clip.bottom < actor->frame.bottom ? canvas_clip.bottom : actor->frame.bottom;
	out->clip_left = cl;
	out->clip_top = ct;
	out->clip_right = cr;
	out->clip_bottom = cb;
	out->flags = actor_render_flags(actor->flags);
	/* Palette index for AF_REMAP_COLOR. Set by callers like
	 * player_Draw_Display_Ship which paints reticle silhouettes as a
	 * flat target_color band over the iconsgrn cel. */
	out->fore_color = actor->foreColor;
	out->target = lcanvas_Render_Emit_Target();
	landru_render_draw(out);
}
