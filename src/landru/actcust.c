#include <stddef.h>

#include <landru/actcust.h>
#include <landru/actor.h>
#include <landru/dirty.h>
#include <landru/fourcc.h>
#include <landru/paint.h>

// GLOBAL: TIE 0xD2F74
bool custom_actor_module_gbl = false;

void lactcust_Create_Custom_Actor_Module(void) {
	lactor_Create_Actor_Type(FOURCC_CUST, NULL, NULL);
	custom_actor_module_gbl = true;
}

void lactcust_Destroy_Custom_Actor_Module(void) {
	if (custom_actor_module_gbl) {
		lactor_Destroy_Actor_Type(FOURCC_CUST);
		custom_actor_module_gbl = false;
	}
}

Actor* lactcust_Alloc_Custom_Actor(void* data, Rect* frame, int16_t x, int16_t y, int16_t z) {
	Actor* actor = lactor_Alloc_Actor(0);
	if (!actor) {
		return NULL;
	}

	lactcust_Init_Custom_Actor(actor, frame, x, y, z);
	actor->data = data;
	lactor_Set_Actor_Name(actor, FOURCC_CUST, "CUSTOM");
	lactor_Non_Discard_Actor_Data(actor);
	/* A custom actor's callback, rather than its generic pose and resource
	 * fields, defines its output. Suppress the otherwise misleading standard
	 * actor record; the callback may publish its concrete operations. */
	lactor_Set_Actor_Render_Capture_Hidden(actor, true);
	return actor;
}

void lactcust_Init_Custom_Actor(Actor* actor, Rect* frame, int16_t x, int16_t y, int16_t z) {
	lrect_Set_Rect(&actor->frame, frame->left, frame->top, frame->right, frame->bottom);
	actor->x = x;
	actor->y = y;
	actor->zplane = z;
	lactor_Discard_Actor_Data(actor);
	actor->draw = lactcust_Draw_Custom_Actor;
	actor->update = lactcust_Update_Custom_Actor;
	lactor_Add_Actor_To_System(actor);
}

int lactcust_Draw_Custom_Actor(Actor* actor, Rect* rect, Rect* clipRect, int16_t x, int16_t y,
							   int16_t refresh) {
	(void)rect;
	(void)clipRect;
	if (!refresh)
		return 0;

	Rect pRect;
	lrect_Set_Rect(&pRect, x, y, x + actor->w, y + actor->h);
	lpaint_Paint_Clipped_Rect(&pRect, actor->foreColor);
	if (lactor_Is_Actor_Dirty(actor)) {
		ldirty_Dirty_Rect(&pRect);
	}
	return 1;
}

void lactcust_Update_Custom_Actor(Actor* actor) {
	lactor_Move_Actor(actor);
	lactor_Move_Actor_Frame(actor);
}
