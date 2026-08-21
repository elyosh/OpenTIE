#ifndef LANDRU_ACTOR_H
#define LANDRU_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>
#include <landru/render.h>

typedef struct Actor Actor;

typedef int (*lactorDrawFunc)(Actor*, Rect*, Rect*, int16_t, int16_t, int16_t);
typedef void (*lactorUpdateFunc)(Actor*);
typedef void (*lactorCallback)(Actor*, int32_t);
typedef void (*lactorFrameFunc)(Actor*, Rect*);
typedef void (*lactorStateFunc)(Actor*, int16_t, int16_t);

enum ActorFlags {
	AF_VISIBLE = 0x0001,
	AF_ACTIVE = 0x0002,
	AF_DISCARD = 0x0004,
	AF_DIRTY = 0x0008,
	AF_REFRESHABLE = 0x0010,
	AF_REFRESH = 0x0020,
	/* The custom draw callback does not render this actor's standard pose.
	 * Such actors are omitted from the render sink, while framebuffer
	 * rendering continues to call actor->draw. */
	AF_NO_RENDER_CAPTURE = 0x0040,
	AF_HFLIP = 0x0100,
	AF_VFLIP = 0x0200,
	AF_REMAP_COLOR = 0x0400,
	AF_FLAG1 = 0x4000,
	AF_FLAG2 = 0x8000,
};

typedef struct {
	uint32_t type;
	void* next;
	lactorFrameFunc frameFunc;
	lactorStateFunc stateFunc;
} ActorType;

struct Actor {
	uint32_t res_type;
	char res_name[8];
	Actor* next;
	int32_t start;
	int32_t stop;
	Rect frame;
	Rect frame_v;
	Rect bounds;
	int16_t x;
	int16_t y;
	int16_t xf;
	int16_t yf;
	int16_t xv;
	int16_t yv;
	int16_t xvf;
	int16_t yvf;
	/* Position from BEFORE the most recent lactor_Move_Actor call —
	 * used by render consumers to interpolate between cel commits. Captured
	 * at the start of Move_Actor so it always reflects the previous
	 * cel's commit position; reseeded to (x,y) by Set_Actor_Pos so
	 * that initial placement / ACTOR_POS teleports don't render as
	 * a slide from the prior cel.
	 *
	 * prev_xv/prev_yv hold the velocity actually integrated this cel
	 * — captured at the same moment as prev_x/prev_y. The post-Move
	 * FILM script may overwrite actor->xv via FCMD_ACTOR_VEL for the
	 * NEXT cel; consumers comparing dx against velocity for teleport
	 * detection must use the captured prev_xv to avoid false positives
	 * on velocity-transition cels. */
	int16_t prev_x;
	int16_t prev_y;
	int16_t prev_xv;
	int16_t prev_yv;
	/* Scale (Watcom Q8, 256 = identity) at the start of the most
	 * recent lactor_Move_Actor — captured the same moment as
	 * prev_x/prev_y. Ships through the render state so a consumer can
	 * lerp scale alongside position when the actor
	 * has interpolation enabled. Initialized to 256 in
	 * Alloc_Actor (matches xscale/yscale defaults) so first-cel
	 * actors render at identity instead of sliding from 0.
	 * Set_Actor_Scale does NOT reseed (unlike Set_Actor_Pos's
	 * teleport-friendly reseed) — every Set_Actor_Scale caller
	 * today is animating, not teleporting. */
	int16_t prev_xscale;
	int16_t prev_yscale;
	int16_t w;
	int16_t h;
	int16_t zplane;
	uint16_t flags;
	int16_t id;
	/* Index of the FilmObject in film->array[] that owns this Actor.
	 * -1 when the Actor wasn't instantiated by the FILM loader (e.g.
	 * standalone DELT/ANIM allocations). Set once at film load and
	 * carried through to LandruActorRenderState so consumers can
	 * disambiguate multiple instances of the same resource (the FILM
	 * format keys actor entries by linked-resource name, not by
	 * unique id — e.g. brdg1b_f has six "stars" entries). */
	int16_t film_entry_index;
	int16_t state;
	int16_t state_f;
	int16_t state_v;
	int16_t state_vf;
	int16_t foreColor;
	int16_t backColor;
	int16_t xscale;
	int16_t yscale;
	int16_t var1;
	int16_t var2;
	void* varptr;
	void* varhdl;
	void* data;
	void** array;
	int16_t arraySize;
	lactorDrawFunc draw;
	lactorUpdateFunc update;
	lactorCallback user;
};

void lactor_Create_Actor_Module(void);
void lactor_Destroy_Actor_Module(void);
bool lactor_Create_Actor_Type(uint32_t type, lactorFrameFunc frameFunc, lactorStateFunc stateFunc);
void lactor_Destroy_Actor_Type(uint32_t type);
ActorType* lactor_Find_Actor_Type(uint32_t type);
Actor* lactor_Ask_Actor_List(void);
void lactor_Add_Actor_To_System(Actor* actor);
void lactor_Free_Actor_From_System(Actor* actor);
Actor* lactor_Alloc_Actor(int16_t extend);
void lactor_Free_Actor(Actor* actor);
void lactor_Free_Actor_Data(Actor* actor);
void lactor_Free_Actors(Actor* actor);
void lactor_Refresh_Actors(void);
void lactor_Draw_Actors(int16_t refresh);
void lactor_Update_Actors(int32_t time);
void lactor_User_Actors(int32_t time);
Actor* lactor_Find_Actor(uint32_t type, const char* name);
void lactor_Show_Actor(Actor* actor);
void lactor_Hide_Actor(Actor* actor);
bool lactor_Is_Actor_Visible(Actor* actor);
void lactor_Activate_Actor(Actor* actor);
void lactor_Deactivate_Actor(Actor* actor);
bool lactor_Is_Actor_Active(Actor* actor);
void lactor_Start_Actor(Actor* actor);
void lactor_Stop_Actor(Actor* actor);
void lactor_Dirty_Actor(Actor* actor);
void lactor_Non_Dirty_Actor(Actor* actor);
bool lactor_Is_Actor_Dirty(Actor* actor);
void lactor_Refresh_Actor(Actor* actor);
void lactor_Non_Refresh_Actor(Actor* actor);
bool lactor_Is_Actor_Refresh(Actor* actor);
void lactor_Refreshable_Actor(Actor* actor);
void lactor_Non_Refreshable_Actor(Actor* actor);
bool lactor_Is_Actor_Refreshable(Actor* actor);
void lactor_Discard_Actor_Data(Actor* actor);
void lactor_Non_Discard_Actor_Data(Actor* actor);
bool lactor_Is_Discard_Actor_Data(Actor* actor);
void lactor_Set_Actor_Flag1(Actor* actor);
void lactor_Clear_Actor_Flag1(Actor* actor);
bool lactor_Is_Actor_Flag1(Actor* actor);
void lactor_Set_Actor_Flag2(Actor* actor);
void lactor_Clear_Actor_Flag2(Actor* actor);
bool lactor_Is_Actor_Flag2(Actor* actor);
void lactor_Move_Actor(Actor* actor);
void lactor_Move_Actor_Frame(Actor* actor);
void lactor_Move_Actor_State(Actor* actor);
void lactor_Copy_Actor_Data(Actor* dst, Actor* src);
bool lactor_Clip_Actor_To_Actor(Actor* actor, Actor* clip_to);
void lactor_Check_Actor_ZPlanes(void);
void lactor_Sort_Actor_ZPlanes(void);
void lactor_Get_Actor_Relative_XY(Actor* actor, Rect* rect, int16_t* outX, int16_t* outY);
void lactor_Get_Actor_Offset(Actor* actor, int16_t* outX, int16_t* outY);
void lactor_Set_Actor_Name(Actor* actor, uint32_t res_type, const char* name);
void lactor_Set_Actor_Frame(Actor* actor, Rect* frame);
void lactor_Get_Actor_Frame(Actor* actor, Rect* outFrame);
void lactor_Set_Actor_Bounds(Actor* actor, Rect* bounds);
void lactor_Get_Actor_Bounds(Actor* actor, Rect* outBounds);
void lactor_Set_Actor_Pos(Actor* actor, int16_t x, int16_t y, int16_t xf, int16_t yf);
void lactor_Get_Actor_Pos(Actor* actor, int16_t* outX, int16_t* outY, int16_t* outXf, int16_t* outYf);
void lactor_Get_Actor_Center(Actor* actor, int16_t* outCX, int16_t* outCY);
void lactor_Set_Actor_Size(Actor* actor, int16_t w, int16_t h);
void lactor_Get_Actor_Size(Actor* actor, int16_t* outW, int16_t* outH);
void lactor_Get_Actor_Rect(Actor* actor, Rect* outRect);
void lactor_Get_Actor_Base_Rect(Actor* actor, Rect* outRect);
void lactor_Set_Actor_ZPlane(Actor* actor, int16_t zplane);
int16_t lactor_Get_Actor_ZPlane(Actor* actor);
void lactor_Set_Actor_Time(Actor* actor, int32_t start, int32_t stop);
void lactor_Get_Actor_Time(Actor* actor, int32_t* outStart, int32_t* outStop);
void lactor_Set_Actor_State(Actor* actor, int16_t state, int16_t stateFract);
void lactor_Get_Actor_State(Actor* actor, int16_t* outState, int16_t* outStateFract);
void lactor_Set_Actor_State_Speed(Actor* actor, int16_t stateV, int16_t stateVf);
void lactor_Get_Actor_State_Speed(Actor* actor, int16_t* outV, int16_t* outVf);
void lactor_Set_Actor_Color(Actor* actor, int16_t fore, int16_t back);
void lactor_Get_Actor_Color(Actor* actor, int16_t* outFore, int16_t* outBack);
void lactor_Set_Actor_Remap_Color(Actor* actor, int16_t color);
int16_t lactor_Get_Actor_Remap_Color(Actor* actor);
void lactor_Clear_Actor_Remap_Color(Actor* actor);
void lactor_Set_Actor_Scale(Actor* actor, int16_t xScale, int16_t yScale);
void lactor_Get_Actor_Scale(Actor* actor, int16_t* outXScale, int16_t* outYScale);
void lactor_Set_Actor_Flip(Actor* actor, int16_t hFlip, int16_t vFlip);
void lactor_Get_Actor_Flip(Actor* actor, int16_t* outHFlip, int16_t* outVFlip);
void lactor_Set_Actor_Speed(Actor* actor, int16_t xv, int16_t yv, int16_t xvf, int16_t yvf);
void lactor_Get_Actor_Speed(Actor* actor, int16_t* outXv, int16_t* outYv, int16_t* outXvf, int16_t* outYvf);
void lactor_Set_Actor_Frame_Speed(Actor* actor, int16_t left, int16_t top, int16_t right, int16_t bottom);
void lactor_Get_Actor_Frame_Speed(Actor* actor, int16_t* outLeft, int16_t* outTop, int16_t* outRight,
								  int16_t* outBottom);
void lactor_Set_Actor_Data(Actor* actor, void* data);
void lactor_Get_Actor_Data(Actor* actor, void** outData);
void lactor_Set_Actor_Array(Actor* actor, void** array);
void lactor_Get_Actor_Array(Actor* actor, void*** outArray);
void* lactor_Get_Actor_Array_Data(Actor* actor, int16_t index);
void lactor_Set_Actor_Draw_Function(Actor* actor, lactorDrawFunc drawFunc);
void lactor_Get_Actor_Draw_Function(Actor* actor, lactorDrawFunc* outDraw);
void lactor_Set_Actor_Update_Function(Actor* actor, lactorUpdateFunc updateFunc);
void lactor_Get_Actor_Update_Function(Actor* actor, lactorUpdateFunc* outUpdate);
void lactor_Set_Actor_User_Function(Actor* actor, lactorCallback userFunc);
void lactor_Get_Actor_User_Function(Actor* actor, lactorCallback* outUser);

/* Publish the live actor list to the optional render sink. */
void lactor_emit_render_state(void);

/* Populate one renderer-neutral state record from an Actor. The same per-actor
 * body lactor_emit_render_state uses, exposed so callers that need to
 * capture a frozen actor pose at a specific moment can reuse the position /
 * HFLIP / VFLIP / clip / scale / fore_color logic without
 * duplicating it. `out` is filled fully. */
void lactor_fill_render_state(const Actor* a, LandruActorRenderState* out);

/* Toggle AF_NO_RENDER_CAPTURE — see the flag's comment. Scenes call
 * this on orchestration actors whose custom draw callback renders
 * other state (or no pixels at all). */
void lactor_Set_Actor_Render_Capture_Hidden(Actor* actor, bool hidden);

/* Publish one imperative actor draw at the supplied classic coordinates.
 * Called from
 * inside lact{delt,anim,raw}_Draw_*_Actor at the moment of the
 * imperative draw — captures per-call state that the live actor list
 * cannot represent (dialog UI in
 * computer.c / register.c draws the same Actor* multiple times per
 * frame at different positions and states). */
void lactor_emit_draw(Actor* actor, int16_t xoff, int16_t yoff);

#endif
