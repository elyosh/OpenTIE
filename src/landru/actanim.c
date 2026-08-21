#include <stdlib.h>

#include <landru/actanim.h>
#include <landru/actdelt.h>
#include <landru/actor.h>
#include <landru/fourcc.h>
#include <landru/rect.h>
#include <landru/res.h>

// GLOBAL: TIE 0xD2E70
static bool anim_actor_module_gbl = false;

void lactanim_Create_Anim_Actor_Module(void) {
	lactor_Create_Actor_Type(FOURCC_ANIM, (lactorFrameFunc)lactanim_Get_Anim_Actor_Frame,
							 (lactorStateFunc)lactanim_Set_Anim_Actor_State);
	anim_actor_module_gbl = true;
}

void lactanim_Destroy_Anim_Actor_Module(void) {
	if (anim_actor_module_gbl) {
		lactor_Destroy_Actor_Type(FOURCC_ANIM);
		anim_actor_module_gbl = false;
	}
}

void lactanim_Get_Anim_Actor_Frame(Actor* actor, Rect* outFrame) {
	lrect_Set_Rect(outFrame, 0, 0, 0, 0);
	int16_t* frame = lactor_Get_Actor_Array_Data(actor, actor->state);
	if (frame && frame[0] != -1 && frame[1] != -1) {
		lrect_Set_Rect(outFrame, frame[0], frame[1], frame[2] + 1, frame[3] + 1);
	}
}

void lactanim_Set_Anim_Actor_State(Actor* actor, int16_t state, int16_t stateFract) {
	actor->state = state;
	actor->state_f = stateFract;
	Rect frame;
	lactanim_Get_Anim_Actor_Frame(actor, &frame);
	actor->w = frame.right - frame.left;
	actor->h = frame.bottom - frame.top;
}

Actor* lactanim_Alloc_Anim_Actor(void* frames, Rect* rect, int16_t x, int16_t y, int16_t z) {
	Actor* actor = lactor_Alloc_Actor(0);
	if (!actor)
		return NULL;

	lactanim_Init_Anim_Actor(actor, frames, rect, x, y, z);
	lactor_Set_Actor_Name(actor, FOURCC_ANIM, "");
	lactor_Non_Discard_Actor_Data(actor);
	return actor;
}

Actor* lactanim_Res_Anim_Actor(const char* resName, Rect* rect, int16_t x, int16_t y, int16_t z) {
	Actor* actor = lactor_Alloc_Actor(0);
	if (!actor)
		return NULL;

	ResFile* resFile = lres_Open_Resource_Data(FOURCC_ANIM, resName);
	if (!resFile) {
		lactor_Free_Actor(actor);
		return NULL;
	}

	int16_t frameCount = lres_Read_Resource_Word(resFile);
	void** frames = malloc(sizeof(void*) * frameCount);
	if (!frames) {
		lres_Close_Resource_Data(resFile);
		lactor_Free_Actor(actor);
		return NULL;
	}

	for (int i = 0; i < frameCount; i++) {
		uint32_t len = lres_Read_Resource_Long(resFile);
		if (len <= 0) {
			frames[i] = NULL;
		} else {
			frames[i] = lres_Read_Resource_Data(resFile, len);
			if (!frames[i]) {
				/* Cleanup on failure */
				for (int j = 0; j < i; j++)
					free(frames[j]);
				free(frames);
				lres_Close_Resource_Data(resFile);
				lactor_Free_Actor(actor);
				return NULL;
			}
		}
	}
	lres_Close_Resource_Data(resFile);

	lactanim_Init_Anim_Actor(actor, frames, rect, x, y, z);
	lactor_Set_Actor_Name(actor, FOURCC_ANIM, resName);
	actor->arraySize = frameCount;
	lactanim_Get_Anim_Actor_Bounds(actor, &actor->bounds);
	return actor;
}

void lactanim_Init_Anim_Actor(Actor* actor, void* frames, Rect* rect, int16_t x, int16_t y, int16_t z) {
	lrect_Set_Rect(&actor->frame, rect->left, rect->top, rect->right, rect->bottom);
	actor->x = x;
	actor->y = y;
	actor->zplane = z;
	lactor_Discard_Actor_Data(actor);
	actor->draw = (lactorDrawFunc)lactanim_Draw_Anim_Actor;
	actor->array = frames;
	actor->update = (lactorUpdateFunc)lactanim_Update_Anim_Actor;
	lactor_Add_Actor_To_System(actor);

	Rect frame;
	lactanim_Get_Anim_Actor_Frame(actor, &frame);
	actor->w = frame.right - frame.left;
	actor->h = frame.bottom - frame.top;
}

void lactanim_Get_Anim_Actor_Bounds(Actor* actor, Rect* outRect) {
	lrect_Clear_Rect(outRect);
	for (int i = 0; i < actor->arraySize; i++) {
		int16_t* frame = lactor_Get_Actor_Array_Data(actor, i);
		if (frame && frame[0] != -1 && frame[1] != -1) {
			Rect frameRect;
			lrect_Set_Rect(&frameRect, frame[0], frame[1], frame[2] + 1, frame[3] + 1);
			lrect_Enclose_Rect(outRect, &frameRect);
		}
	}
}

int lactanim_Draw_Anim_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
							 int16_t refresh) {
	(void)clip;
	(void)dest;
	if (!refresh)
		return 0;

	void* frame = lactor_Get_Actor_Array_Data(actor, actor->state);
	if (!frame)
		return 0;

	lactor_emit_draw(actor, xoff, yoff);

	int dirty = lactor_Is_Actor_Dirty(actor);

	if (actor->xscale == 256 && actor->yscale == 256) {
		if ((actor->flags & AF_HFLIP) || (actor->flags & AF_VFLIP)) {
			return lactdelt_Draw_Flipped_Clipped_Delta(actor, frame, xoff, yoff, dirty);
		}
		if (actor->flags & AF_REMAP_COLOR) {
			return lactdelt_Draw_Color_Clipped_Delta(frame, xoff, actor->foreColor, yoff, dirty);
		}
		return lactdelt_Draw_Clipped_Delta(frame, xoff, yoff, dirty);
	}
	return lactdelt_Draw_Scaled_Clipped_Delta(actor, frame, xoff, yoff, dirty);
}

void lactanim_Update_Anim_Actor(Actor* actor) {
	int16_t prev_state = actor->state;

	lactor_Move_Actor(actor);
	lactor_Move_Actor_Frame(actor);
	lactor_Move_Actor_State(actor);

	if (actor->arraySize == 0) {
		actor->state = 0;
	} else {
		while (actor->state < 0)
			actor->state += actor->arraySize;
		while (actor->state >= actor->arraySize)
			actor->state -= actor->arraySize;
	}

	if (prev_state != actor->state) {
		Rect frame;
		lactanim_Get_Anim_Actor_Frame(actor, &frame);
		actor->w = frame.right - frame.left;
		actor->h = frame.bottom - frame.top;
	}
}
