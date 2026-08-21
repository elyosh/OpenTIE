#include <stdlib.h>
#include <string.h>

#include <landru/actanim.h>
#include <landru/actcust.h>
#include <landru/actdelt.h>
#include <landru/actor.h>
#include <landru/actraw.h>
#include <landru/canvas.h>
#include <landru/fade.h>
#include <landru/file.h>
#include <landru/film.h>
#include <landru/fourcc.h>
#include <landru/memptr.h>
#include <landru/pal.h>
#include <landru/render.h>
#include <landru/res.h>
#include <landru/sound.h>
#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

#include "render_internal.h"

// GLOBAL: TIE 0xD2FFC
Film* first_film_gbl;
int16_t film_zplane_gbl;
// GLOBAL: TIE 0xD3002
int16_t film_color_fade_gbl;
// GLOBAL: TIE 0xD3000
int16_t film_fade_gbl;

Film* lfilm_Ask_Film_List(void) { return first_film_gbl; }

void lfilm_Add_Film_To_System(Film* film) {
	Film* curFilm = first_film_gbl;
	Film* lastFilm = NULL;

	while (curFilm && (film->zplane < curFilm->zplane)) {
		lastFilm = curFilm;
		curFilm = curFilm->next;
	}

	film->next = curFilm;
	if (lastFilm)
		lastFilm->next = film;
	else
		first_film_gbl = film;
}

void lfilm_Free_Film_From_System(Film* film) {
	Film* curFilm = first_film_gbl;
	Film* lastFilm = NULL;

	while (curFilm && (curFilm != film)) {
		lastFilm = curFilm;
		curFilm = curFilm->next;
	}

	if (curFilm) {
		if (!lastFilm) {
			first_film_gbl = curFilm->next;
		} else {
			lastFilm->next = curFilm->next;
		}
		film->next = NULL;
	}
}

Film* lfilm_Alloc_Film(int16_t extend) {
	Film* film = lmemptr_Alloc_Clear_System_Pointer(sizeof(Film) + extend);
	if (film) {
		film->def_palette = NULL;
		film->start = 0;
		film->stop = -1;
		film->flags = FF_REFRESH;
		film->next = NULL;
	}
	return film;
}

void lfilm_Free_Film(Film* film) {
	if (film->varptr) {
		lmemptr_Free_System_Pointer(film->varptr);
	}
	if (film->varhdl) {
		free(film->varhdl);
	}
	if (lfilm_Is_Discard_Film_Data(film)) {
		lfilm_Free_Film_Data(film);
	}
	lmemptr_Free_System_Pointer(film);
}

void lfilm_Free_Film_Data(Film* film) {
	if (film->array) {
		for (int i = 0; i < film->array_size; i++) {
			lfilm_Free_Film_Data_Object(film, i);
		}
		free(film->array);
		film->array = NULL;
	}
}

void lfilm_Free_Film_Data_Object(Film* film, int16_t objId) {
	FilmObject* obj = (FilmObject*)film->array[objId];
	if (!obj)
		return;

	switch (obj->id) {
		case FTC_ACTOR: {
			Actor* actor = (Actor*)obj->object;
			lactor_Free_Actor_From_System(actor);
			lactor_Free_Actor(actor);
		} break;
		case FTC_PALETTE: {
			Palette* pal = (Palette*)obj->object;
			lpal_Free_Palette_From_System(pal);
			lpal_Free_Palette(pal);
		} break;
		case FTC_SOUND:
			lsound_Free_Sound((Sound*)obj->object);
			break;
	}
	free(obj);
	film->array[objId] = NULL;
}

void lfilm_Free_Films(Film* film) {
	while (film) {
		Film* next = film->next;
		lfilm_Free_Film_From_System(film);
		lfilm_Free_Film(film);
		film = next;
	}
}

Film* lfilm_Res_Film(const char* name, Rect* frame, int16_t x, int16_t y, int16_t zPlane) {
	return lfilm_Res_Callback_Film(name, frame, x, y, zPlane, NULL);
}

Film* lfilm_Res_Callback_Film(const char* name, Rect* frame, int16_t x, int16_t y, int16_t zPlane,
							  lfilmCallbackFunc cbFunc) {
	Film* film = lfilm_Alloc_Film(0);
	if (!film)
		return NULL;

	ResFile* filmRes = lres_Open_Resource_Data(FOURCC_FILM, name);
	if (!filmRes) {
		lfilm_Free_Film(film);
		return NULL;
	}

	uint16_t magic = lres_Read_Resource_Word(filmRes);
	uint16_t numCels = lres_Read_Resource_Word(filmRes);
	uint16_t numObjects = lres_Read_Resource_Word(filmRes);
	if (magic != 4) {
		lres_Close_Resource_Data(filmRes);
		lfilm_Free_Film(film);
		return NULL;
	}
	film->array = malloc(sizeof(uint8_t*) * numObjects);
	film->cur_cel = 0;
	film->array_size = (int16_t)numObjects;
	film->cels = numCels;
	for (int i = 0; i < numObjects; i++) {
		film->array[i] = NULL;
	}
	for (int i = 0; i < film->array_size; i++) {
		char objName[8];
		uint32_t objType = lres_Read_Resource_Long(filmRes);
		objType = lfile_Swap_DWord(objType);
		lres_Read_Resource_Buffer_Data(filmRes, objName, 8);
		lres_Read_Resource_Long(filmRes); // total length of block
		uint16_t objTypeId = lres_Read_Resource_Word(filmRes);
		lres_Read_Resource_Word(filmRes); // number of chunks
		uint16_t objLength = lres_Read_Resource_Word(filmRes);
		FilmObject* filmObj = malloc(objLength + sizeof(FilmObject));
		filmObj->res_type = objType;
		memcpy(filmObj->res_name, objName, 8);
		filmObj->offset = 0;
		filmObj->object = NULL;
		filmObj->id = objTypeId;
		lres_Read_Resource_Buffer_Data(filmRes, (uint8_t*)(filmObj + 1), objLength);
		film->array[i] = (uint8_t*)filmObj;
	}

	lres_Close_Resource_Data(filmRes);
	for (int16_t i = 0; i < film->array_size; i++) {
		if (!film->array[i])
			break;
		FilmObject* filmObj = (FilmObject*)film->array[i];
		if (!lfilm_Res_Film_Object(filmObj->res_type, filmObj->res_name, &filmObj->object))
			break;
		/* Stamp the FilmObject array index onto actor instances so the
		 * render state can carry a per-instance discriminator. The same
		 * (res_type, res_name) appears in multiple FilmObject entries
		 * (e.g. brdg1b_f's six "stars" actors); the array position is
		 * the only unique identity in the FILM format. */
		if (filmObj->object && filmObj->id == FTC_ACTOR) {
			((Actor*)filmObj->object)->film_entry_index = i;
		}
		if (cbFunc && cbFunc(film, filmObj)) {
			lfilm_Free_Film_Data_Object(film, i);
		}
	}

	lfilm_Init_Film(film, film->array, frame, x, y, zPlane);
	lfilm_Set_Film_Name(film, FOURCC_FILM, name);

	return film;
}

bool lfilm_Res_Film_Object(uint32_t objType, const char* objName, void** outObj) {
	Rect canvasBounds;
	Actor* actor = NULL;
	void* object = NULL;

	/* VIEW objects are no-ops — the film system doesn't instantiate them.
	 * Return true so the loading loop continues to subsequent objects. */
	if (objType == FOURCC_VIEW)
		return true;

	lcanvas_Get_Drawing_Canvas_Bounds(&canvasBounds);
	if (objType == FOURCC_CUST) {
		actor = lactcust_Alloc_Custom_Actor(0, &canvasBounds, 0, 0, 0);
		if (actor) {
			lactor_Set_Actor_Draw_Function(actor, 0);
		}
	} else if (objType == FOURCC_DELT) {
		actor = lfilm_Clone_Film_Actor(objType, objName);
		if (!actor) {
			actor = lactdelt_Res_Delta_Actor(objName, &canvasBounds, 0, 0, 0);
		}
	} else if (objType == 'RAW ') {
		actor = lfilm_Clone_Film_Actor(objType, objName);
		if (!actor) {
			actor = lactraw_Res_Raw_Actor(objName, &canvasBounds, 0, 0, 0);
		}
	} else if (objType == FOURCC_ANIM) {
		actor = lfilm_Clone_Film_Actor(objType, objName);
		if (!actor) {
			actor = lactanim_Res_Anim_Actor(objName, &canvasBounds, 0, 0, 0);
		}
	} else if (objType == FOURCC_PLTT) {
		object = lpal_Res_Palette(objName);
	} else if (objType == FOURCC_GMID) {
		object = lsound_Res_Music(objName);
	} else if (objType == FOURCC_VOIC) {
		object = lfilm_Clone_Film_Sound(digitalSound, objName);
		if (!object) {
			object = lsound_Res_Digital_Sound(objName);
		}
	}

	if (actor) {
		lactor_Set_Actor_Time(actor, -1, -1);
		*outObj = actor;
		return true;
	} else if (object) {
		*outObj = object;
		return true;
	}

	return false;
}

Actor* lfilm_Clone_Film_Actor(uint32_t actType, const char* actName) {
	Actor* curActor = lactor_Ask_Actor_List();
	Actor* actor = NULL;
	while (curActor && !actor) {
		if (curActor->res_type == actType && memcmp(curActor->res_name, actName, 8) == 0) {
			actor = curActor;
		}
		curActor = curActor->next;
	}

	curActor = NULL;

	if (actor) {
		Rect canvasClip;
		lcanvas_Get_Drawing_Canvas_Clip(&canvasClip);
		if (actor->res_type == FOURCC_DELT) {
			curActor = lactdelt_Alloc_Delta_Actor(0, &canvasClip, 0, 0, 0);
		} else if (actor->res_type == 'RAW ') {
			curActor = lactraw_Alloc_Raw_Actor(0, &canvasClip, 0, 0, 0);
		} else if (actor->res_type == FOURCC_ANIM) {
			curActor = lactanim_Alloc_Anim_Actor(0, &canvasClip, 0, 0, 0);
		}

		if (curActor) {
			lactor_Copy_Actor_Data(curActor, actor);
			lactor_Set_Actor_Name(curActor, actor->res_type, actor->res_name);
		}
	}

	return curActor;
}

Sound* lfilm_Clone_Film_Sound(uint32_t sndType, const char* sndName) {
	Sound* curSound = lsound_Ask_Sound_List();
	Sound* sound = NULL;

	while (curSound && !sound) {
		if (curSound->res_type == sndType && memcmp(curSound->res_name, sndName, 8) == 0) {
			sound = curSound;
		}
		curSound = curSound->next;
	}

	curSound = NULL;
	if (sound) {
		if (sound->res_type == FOURCC_VOIC) {
			curSound = lsound_Alloc_Sound(0);
		}
		if (curSound) {
			lsound_Copy_Sound_Data(curSound, sound);
			lsound_Set_Sound_Name(curSound, sndType, sndName);
		}
	}

	return curSound;
}

bool lfilm_Res_Film_Data(ResFile* fp, void** array, int16_t count) {
	for (int16_t i = 0; i < count; i++) {
		char objName[8];
		uint32_t objType = lres_Read_Resource_Long(fp);
		objType = lfile_Swap_DWord(objType);
		lres_Read_Resource_Buffer_Data(fp, objName, 8);
		lres_Read_Resource_Long(fp);
		uint16_t objTypeId = lres_Read_Resource_Word(fp);
		lres_Read_Resource_Word(fp);
		uint16_t objLength = lres_Read_Resource_Word(fp);
		FilmObject* filmObj = malloc(sizeof(FilmObject) + objLength);
		if (!filmObj)
			return false;
		filmObj->res_type = objType;
		memcpy(filmObj->res_name, objName, 8);
		filmObj->id = objTypeId;
		filmObj->offset = 0;
		filmObj->object = NULL;
		lres_Read_Resource_Buffer_Data(fp, (uint8_t*)(filmObj + 1), objLength);
		array[i] = filmObj;
	}
	return true;
}

bool lfilm_Res_Film_Objects(Film* film, lfilmCallbackFunc callback) {
	for (int16_t i = 0; i < film->array_size; i++) {
		FilmObject* filmObj = (FilmObject*)film->array[i];
		if (!filmObj)
			return false;

		if (!lfilm_Res_Film_Object(filmObj->res_type, filmObj->res_name, &filmObj->object))
			return false;

		if (callback && callback(film, filmObj))
			lfilm_Free_Film_Data_Object(film, i);
	}
	return true;
}

void lfilm_Init_Film(Film* film, void** array, Rect* frame, int16_t x, int16_t y, int16_t z) {
	lrect_Copy_Rect(&film->frame, frame);
	film->x = x;
	film->y = y;
	film->zplane = z;
	lfilm_Discard_Film_Data(film);
	film->draw = NULL;
	film->array = array;
	film->update = lfilm_fupdate_Film;
	lfilm_Add_Film_To_System(film);
}

void lfilm_Update_Films(int32_t time) {
	Film* film = first_film_gbl;
	while (film) {
		if (time == film->start) {
			lfilm_Start_Film(film);
			if (time == film->stop) {
				lfilm_Stop_Film(film);
			}
		} else if (time == film->stop) {
			lfilm_Stop_Film(film);
		} else if (!film->cels) {
			if (lfilm_Is_Film_Active(film)) {
				lfilm_Stop_Film(film);
			}
		} else if (film->cur_cel < film->cels) {
			if (film->update && lfilm_Is_Film_Active(film)) {
				film->update(film);
			}
		} else {
			if (lfilm_Is_Repeat_Film(film)) {
				lfilm_Rewind_Film(film);
			} else {
				lfilm_Stop_Film(film);
			}
		}
		film = film->next;
	}
}

void lfilm_User_Films(int32_t time) {
	Film* film = first_film_gbl;
	while (film) {
		if (film->user) {
			film->user(film, time);
		}
		film = film->next;
	}
}

void lfilm_Draw_Films(int16_t refresh) {
	int16_t x, y;
	for (int16_t i = 0; i < 4; i++) {
		Rect rect, clipRect;
		lview_Get_View_Frame(i, &rect);
		if (lrect_Empty_Rect(&rect))
			continue;

		Film* film = first_film_gbl;
		while (film) {
			if (film->draw && lfilm_Is_Film_Visible(film)) {
				if (lviewadd_Clip_Object_To_View(i, film->zplane, &film->frame, &rect, &clipRect)) {
					int16_t needs_draw =
						refresh | lfilm_Is_Film_Refresh(film) | lfilm_Is_Film_Refreshable(film);
					lcanvas_Set_Drawing_Canvas_Clip(&clipRect);
					lfilm_Get_Film_Relative_XY(film, &rect, &x, &y);
					film->draw(film, &rect, &clipRect, x, y, needs_draw);
				}
			}
			film = film->next;
		}
	}

	for (Film* film = first_film_gbl; film; film = film->next) {
		lfilm_Non_Refresh_Film(film);
	}
}

void lfilm_fupdate_Film(Film* film) { lfilm_Step_Film_Objects(film); }

void lfilm_Get_Film_Relative_XY(Film* film, Rect* rect, int16_t* outX, int16_t* outY) {
	*outX = (film->x - film->frame.left) + rect->left;
	*outY = (film->y - film->frame.top) + rect->top;
}

void lfilm_Set_Film_Name(Film* film, uint32_t res_type, const char* name) {
	film->res_type = res_type;
	int i;
	for (i = 0; i < 8 && name[i]; i++)
		film->res_name[i] = name[i];
	for (; i < 8; i++)
		film->res_name[i] = 0;
}

void lfilm_Set_Film_Def_Palette(Film* film, Palette* palette) { film->def_palette = palette; }

bool lfilm_Is_Film_Visible(Film* film) { return film->flags & FF_VISIBLE; }

bool lfilm_Is_Film_Active(Film* film) { return film->flags & FF_ACTIVE; }

void lfilm_Start_Film(Film* film) {
	lfilm_Rewind_Film(film);
	film->flags |= (FF_VISIBLE | FF_ACTIVE);
}

void lfilm_Stop_Film(Film* film) { film->flags &= ~(FF_VISIBLE | FF_ACTIVE); }

void lfilm_Rewind_Film(Film* film) { lfilm_Rewind_Film_Objects(film); }

void lfilm_Non_Refresh_Film(Film* film) { film->flags &= ~FF_REFRESH; }

bool lfilm_Is_Film_Refresh(Film* film) { return film->flags & FF_REFRESH; }

bool lfilm_Is_Film_Refreshable(Film* film) { return film->flags & FF_REFRESHABLE; }

void lfilm_Discard_Film_Data(Film* film) { film->flags |= FF_DISCARD; }

bool lfilm_Is_Discard_Film_Data(Film* film) { return film->flags & FF_DISCARD; }

bool lfilm_Is_Repeat_Film(Film* film) { return film->flags & FF_REPEAT; }

void lfilm_Check_Film_ZPlanes(void) {
	if (!film_zplane_gbl)
		return;

	film_zplane_gbl = 0;
	Film* filmSort = NULL;
	Film* filmBase = lfilm_Ask_Film_List();
	while (filmBase) {
		Film* curFilmSort = filmSort;
		Film* curFilmBase = filmBase;
		filmBase = filmBase->next;

		Film* lastFilmSort = NULL;
		curFilmBase->next = NULL;

		while (curFilmSort && curFilmSort->zplane >= curFilmBase->zplane) {
			lastFilmSort = curFilmSort;
			curFilmSort = curFilmSort->next;
		}

		if (!curFilmSort) {
			if (!lastFilmSort) {
				filmSort = curFilmBase;
			} else {
				lastFilmSort->next = curFilmBase;
			}
		} else if (!lastFilmSort) {
			filmSort = curFilmBase;
			curFilmBase->next = curFilmSort;
		} else {
			lastFilmSort->next = curFilmBase;
			curFilmBase->next = curFilmSort;
		}
	}

	first_film_gbl = filmSort;
}

void lfilm_Clear_Film_Fade(void) {
	film_color_fade_gbl = 0;
	film_fade_gbl = 0;
}

void lfilm_Start_Film_Fade(int16_t direction) {
	int16_t lock = 1;
	/* PAL_TO_PAL with a non-wipe (or SNAP) fade type runs as a
	 * single-shot palette interpolation rather than a sustained
	 * task — drop the lock so Push_Fade_To_Video_Screen_Task
	 * doesn't extend the end_time. */
	if (film_color_fade_gbl == FADE_COLOR_PAL_TO_PAL && film_fade_gbl < FADE_WIPE_RIGHT) {
		lock = 0;
	}

	lfade_Start_Full_Fade(film_fade_gbl, film_color_fade_gbl, direction, 0, lock);
	lfilm_Clear_Film_Fade();
}

bool lfilm_Is_Film_Fade(void) { return film_fade_gbl || film_color_fade_gbl; }

void lfilm_Rewind_Film_Objects(Film* film) {
	film->cur_cel = 0;
	lpal_Screen_To_Dest_Palette(0, 0, 255);
	lpal_Screen_To_Src_Palette(0, 0, 255);
	lpal_Set_Dest_Pal_Color(0, 255, 0, 0, 0);
	if (film->def_palette) {
		lpal_Set_Dest_Palette(film->def_palette);
	}

	for (int i = 0; i < film->array_size; i++) {
		FilmObject* obj = (FilmObject*)film->array[i];
		if (!obj)
			continue;
		switch (obj->id) {
			case FTC_VIEW:
				lfilm_Rewind_View_Film(film, obj, (uint8_t*)(obj + 1));
				break;
			case FTC_ACTOR:
				lfilm_Rewind_Actor_Film(film, obj, (uint8_t*)(obj + 1));
				break;
			case FTC_PALETTE:
				lfilm_Rewind_Palette_Film(film, obj, (uint8_t*)(obj + 1));
				break;
			case FTC_SOUND:
				lfilm_Rewind_Sound_Film(film, obj, (uint8_t*)(obj + 1));
				break;
		}
	}

	if (!lpal_Compare_Src_Dest_Palette()) {
		if (lfilm_Is_Film_Fade()) {
			lfilm_Start_Film_Fade(true);
		} else if (!lfade_Fade_Active()) {
			lfade_Start_Color_Fade(1, 1, 1, 0, 1);
		} else {
			lpal_Dest_To_Screen_Palette(0, 0, 255);
			lpal_Put_Screen_Palette();
		}
	} else if (lfilm_Is_Film_Fade()) {
		lfilm_Start_Film_Fade(false);
	}
	film->cur_cel++;
}

void lfilm_Rewind_View_Film(Film* film, FilmObject* obj, void* data) {
	obj->offset = 0;
	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		lfilm_Step_View_Film(film, obj, data);
	}
}

void lfilm_Rewind_Palette_Film(Film* film, FilmObject* obj, void* data) {
	obj->offset = 0;
	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		lfilm_Step_Palette_Film(film, obj, data);
	}
}

void lfilm_Rewind_Actor_Film(Film* film, FilmObject* obj, void* data) {
	Actor* actor = obj->object;
	lcanvas_Get_Drawing_Canvas_Clip(&actor->frame);
	lactor_Set_Actor_Pos(actor, 0, 0, 0, 0);
	lactor_Set_Actor_Speed(actor, 0, 0, 0, 0);
	lactor_Set_Actor_State(actor, 0, 0);
	lactor_Set_Actor_State_Speed(actor, 0, 0);
	lactor_Set_Actor_ZPlane(actor, 0);
	lactor_Set_Actor_Flip(actor, 0, 0);
	if (lactor_Is_Actor_Visible(actor)) {
		lactor_Hide_Actor(actor);
	}
	actor->var1 = 0;
	actor->var2 = 0;
	obj->offset = 0;
	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		lfilm_Step_Actor_Film(film, obj, data);
	}
}

void lfilm_Rewind_Sound_Film(Film* film, FilmObject* obj, void* data) {
	obj->offset = 0;
	Sound* snd = obj->object;
	lsound_Stop_Landru_Sound();
	snd->var1 = 0;
	snd->var2 = 0;
	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		lfilm_Step_Sound_Film(film, obj, data);
	}
}

void lfilm_Step_Film_Objects(Film* film) {
	if (!lfade_Fade_Active()) {
		lpal_Screen_To_Dest_Palette(0, 0, 255);
		lpal_Screen_To_Src_Palette(0, 0, 255);
	}
	for (int i = 0; i < film->array_size; i++) {
		FilmObject* obj = (FilmObject*)film->array[i];
		if (!obj)
			continue;
		switch (obj->id) {
			case FTC_VIEW:
				lfilm_Step_View_Film(film, obj, (uint8_t*)(obj + 1));
				break;
			case FTC_ACTOR:
				lfilm_Step_Actor_Film(film, obj, (uint8_t*)(obj + 1));
				break;
			case FTC_PALETTE:
				lfilm_Step_Palette_Film(film, obj, (uint8_t*)(obj + 1));
				break;
			case FTC_SOUND:
				lfilm_Step_Sound_Film(film, obj, (uint8_t*)(obj + 1));
				break;
		}
	}

	if (!lfade_Fade_Active()) {
		if (!lpal_Compare_Src_Dest_Palette()) {
			if (lfilm_Is_Film_Fade()) {
				lfilm_Start_Film_Fade(1);
			} else if (!lfade_Fade_Active()) {
				lfade_Start_Color_Fade(1, 1, 1, 0, 1);
			} else {
				lpal_Dest_To_Screen_Palette(0, 0, 255);
				lpal_Put_Screen_Palette();
			}
		} else if (lfilm_Is_Film_Fade()) {
			lfilm_Start_Film_Fade(0);
		}
	}
	film->cur_cel++;
}

void lfilm_Step_View_Film(Film* film, FilmObject* obj, void* data) {
	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		while (true) {
			lfilm_Step_Film_Frame(obj, data);
			if (lfilm_Is_Film_Next_Frame(obj, data))
				break;
			lfilm_Set_View_To_Film(film, obj, data);
		}
	}
}

void lfilm_Set_View_To_Film(Film* film, FilmObject* obj, void* data) {
	int16_t* chunk = (int16_t*)((uint8_t*)data + obj->offset);
	uint8_t* bChunk = (uint8_t*)chunk;
	uint16_t type = chunk[1];

	switch (type) {
		case FCMD_VIEW_SETRGB: {
			uint16_t start = bChunk[4];
			uint16_t stop = bChunk[5];
			uint16_t r = bChunk[6];
			uint16_t g = bChunk[7];
			uint16_t b = bChunk[8];
			lpal_Set_Dest_Pal_Color(start, stop, r, g, b);
		} break;
		case FCMD_VIEW_DEFPAL:
			lpal_Set_Dest_Pal_Color(0, 255, 0, 0, 0);
			if (film->def_palette) {
				lpal_Set_Dest_Palette(film->def_palette);
			}
			break;
		case FCMD_VIEW_FADE:
			film_fade_gbl = chunk[2] & 0xff;
			film_color_fade_gbl = chunk[3] & 0xff;
			break;
	}
}

void lfilm_Step_Palette_Film(Film* film, FilmObject* obj, void* data) {
	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		while (true) {
			lfilm_Step_Film_Frame(obj, data);
			if (lfilm_Is_Film_Next_Frame(obj, data))
				break;
			lfilm_Set_Palette_To_Film(obj, data);
		}
	}
}

void lfilm_Set_Palette_To_Film(FilmObject* obj, void* data) {
	int16_t* chunk = (int16_t*)((uint8_t*)data + obj->offset);
	if (chunk[1] == FCMD_PALETTE_SET) {
		lpal_Set_Dest_Palette(obj->object);
	}
}

void lfilm_Step_Actor_Film(Film* film, FilmObject* obj, void* data) {
	Actor* actor = obj->object;
	if (actor && actor->update) {
		actor->update(actor);
	}

	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		while (true) {
			lfilm_Step_Film_Frame(obj, data);
			if (lfilm_Is_Film_Next_Frame(obj, data))
				break;
			lfilm_Set_Actor_To_Film(obj, data);
		}
	}
}

void lfilm_Set_Actor_To_Film(FilmObject* obj, void* data) {
	int16_t* chunk = (int16_t*)((uint8_t*)data + obj->offset);
	uint16_t type = chunk[1];
	Actor* actor = obj->object;
	chunk += 2;

	switch (type) {
		case FCMD_ACTOR_POS:
			lactor_Set_Actor_Pos(actor, chunk[0], chunk[1], chunk[2], chunk[3]);
			break;
		case FCMD_ACTOR_VEL:
			lactor_Set_Actor_Speed(actor, chunk[0], chunk[1], chunk[2], chunk[3]);
			break;
		case FCMD_ACTOR_Z:
			lactor_Set_Actor_ZPlane(actor, chunk[0]);
			break;
		case FCMD_ACTOR_STATE:
			lactor_Set_Actor_State(actor, chunk[0], chunk[1]);
			break;
		case FCMD_ACTOR_STATEV:
			lactor_Set_Actor_State_Speed(actor, chunk[0], chunk[1]);
			break;
		case FCMD_ACTOR_VAR1:
			actor->var1 = chunk[0];
			break;
		case FCMD_ACTOR_VAR2:
			actor->var2 = chunk[0];
			break;
		case FCMD_ACTOR_CLIP:
			lrect_Set_Rect(&actor->frame, chunk[0], chunk[1], chunk[2], chunk[3]);
			break;
		case FCMD_ACTOR_CLIPV:
			lrect_Set_Rect(&actor->frame_v, chunk[0], chunk[1], chunk[2], chunk[3]);
			break;
		case FCMD_ACTOR_SHOW:
			if (chunk[0]) {
				lactor_Show_Actor(actor);
			} else {
				lactor_Hide_Actor(actor);
			}
			break;
		case FCMD_ACTOR_FLIP:
			lactor_Set_Actor_Flip(actor, chunk[0], chunk[1]);
			break;
	}
}

void lfilm_Step_Sound_Film(Film* film, FilmObject* obj, void* data) {
	if (lfilm_Is_Film_Time_Stamp(film, obj, data)) {
		while (true) {
			lfilm_Step_Film_Frame(obj, data);
			if (lfilm_Is_Film_Next_Frame(obj, data))
				break;
			lfilm_Set_Sound_To_Film(obj, data);
		}
	}
}

void lfilm_Set_Sound_To_Film(FilmObject* obj, void* data) {
	int16_t* chunk = (int16_t*)((uint8_t*)data + obj->offset);
	uint16_t type = chunk[1];
	Sound* sound = obj->object;
	chunk += 2;

	switch (type) {
		case FCMD_SOUND_START:
			if (sound->var2 == 1) {
				lsound_Start_Speech(sound);
			} else {
				lsound_Start_SFX(sound);
			}
			break;
		case FCMD_SOUND_STOP:
			lsound_Stop_Sound(sound);
			break;
		case FCMD_SOUND_VOLUME:
			lsound_Set_Sound_Volume(sound, chunk[0]);
			break;
		case FCMD_SOUND_FADE:
			lsound_Set_Sound_Fade(sound, chunk[0], chunk[1]);
			break;
		case FCMD_SOUND_VAR1:
			sound->var1 = chunk[0];
			if (sound->var1 == 1) {
				lsound_Set_Sound_Keep(sound);
			}
			break;
		case FCMD_SOUND_VAR2:
			sound->var2 = chunk[0];
			break;
		case FCMD_SOUND_CMD:
			if (chunk[0]) {
				if (sound->var2 == 1) {
					lsound_Start_Speech(sound);
				} else {
					lsound_Start_SFX(sound);
				}
			}
			if (chunk[1]) {
				lsound_Set_Sound_Volume(sound, chunk[1]);
			}
			if (chunk[2] || chunk[3]) {
				lsound_Set_Sound_Fade(sound, chunk[2], chunk[3]);
			}
			break;
		case FCMD_SOUND_CMD2:
			if (chunk[0]) {
				if (sound->var2 == 1) {
					lsound_Start_Speech(sound);
				} else {
					lsound_Start_SFX(sound);
				}
			}
			if (chunk[1]) {
				lsound_Set_Sound_Volume(sound, chunk[1]);
			}
			if (chunk[2] || chunk[3]) {
				lsound_Set_Sound_Fade(sound, chunk[2], chunk[3]);
			}
			if (chunk[4]) {
				lsound_Set_Sound_Pan(sound, chunk[4]);
			}
			if (chunk[5] || chunk[6]) {
				lsound_Set_Sound_Pan_Fade(sound, chunk[5], chunk[6]);
			}
			break;
	}
}

void lfilm_Step_Film_Frame(FilmObject* obj, void* data) {
	int16_t* chunk = (int16_t*)((uint8_t*)data + obj->offset);
	obj->offset += chunk[0];
}

bool lfilm_Is_Film_Time_Stamp(Film* film, FilmObject* obj, void* data) {
	int16_t* chunk = (int16_t*)((uint8_t*)data + obj->offset);
	return (chunk[1] == FCMD_TIMESTAMP) && (chunk[2] == film->cur_cel);
}

bool lfilm_Is_Film_Next_Frame(FilmObject* obj, void* data) {
	int16_t* chunk = (int16_t*)((uint8_t*)data + obj->offset);
	return (chunk[1] == FCMD_TIMESTAMP) || (chunk[1] == FCMD_END);
}

void lfilm_Set_Film_Frame(Film* film, Rect* frame) { lrect_Copy_Rect(&film->frame, frame); }
void lfilm_Get_Film_Frame(Film* film, Rect* out) { lrect_Copy_Rect(out, &film->frame); }
void lfilm_Set_Film_Pos(Film* film, int16_t x, int16_t y) {
	film->x = x;
	film->y = y;
}
void lfilm_Get_Film_Pos(Film* film, int16_t* x, int16_t* y) {
	*x = film->x;
	*y = film->y;
}
void lfilm_Set_Film_ZPlane(Film* film, int16_t z) {
	film->zplane = z;
	film_zplane_gbl = 1;
}
void lfilm_Get_Film_ZPlane(Film* film, int16_t* z) { *z = film->zplane; }
void lfilm_Set_Film_Time(Film* film, int32_t s, int32_t e) {
	film->start = s;
	film->stop = e;
}
void lfilm_Get_Film_Time(Film* film, int32_t* s, int32_t* e) {
	*s = film->start;
	*e = film->stop;
}
void lfilm_Set_Film_Array(Film* film, void** a) { film->array = a; }
void lfilm_Get_Film_Array(Film* film, void*** out) { *out = film->array; }
Palette* lfilm_Get_Film_Def_Palette(Film* film) { return film->def_palette; }
void lfilm_Set_Film_Draw_Function(Film* film, lfilmDrawFunc f) { film->draw = f; }
void lfilm_Get_Film_Draw_Function(Film* film, lfilmDrawFunc* f) { *f = film->draw; }
void lfilm_Set_Film_Update_Function(Film* film, lfilmUpdateFunc f) { film->update = f; }
void lfilm_Get_Film_Update_Function(Film* film, lfilmUpdateFunc* f) { *f = film->update; }
void lfilm_Set_Film_User_Function(Film* film, lfilmCallback f) { film->user = f; }
void lfilm_Get_Film_User_Function(Film* film, lfilmCallback* f) { *f = film->user; }
void lfilm_Show_Film(Film* film) { film->flags |= FF_VISIBLE; }
void lfilm_Hide_Film(Film* film) { film->flags &= ~FF_VISIBLE; }
void lfilm_Activate_Film(Film* film) { film->flags |= FF_ACTIVE; }
void lfilm_Deactivate_Film(Film* film) { film->flags &= ~FF_ACTIVE; }
void lfilm_Refresh_Film(Film* film) { film->flags |= FF_REFRESH; }
void lfilm_Refreshable_Film(Film* film) { film->flags |= FF_REFRESHABLE; }
void lfilm_Non_Refreshable_Film(Film* film) { film->flags &= ~FF_REFRESHABLE; }
void lfilm_Non_Discard_Film_Data(Film* film) { film->flags &= ~FF_DISCARD; }
void lfilm_Repeat_Film(Film* film) { film->flags |= FF_REPEAT; }
void lfilm_Non_Repeat_Film(Film* film) { film->flags &= ~FF_REPEAT; }
void lfilm_Set_Film_Flag1(Film* film) { film->flags |= FF_FLAG1; }
void lfilm_Clear_Film_Flag1(Film* film) { film->flags &= ~FF_FLAG1; }
bool lfilm_Is_Film_Flag1(Film* film) { return film->flags & FF_FLAG1; }
void lfilm_Set_Film_Flag2(Film* film) { film->flags |= FF_FLAG2; }
void lfilm_Clear_Film_Flag2(Film* film) { film->flags &= ~FF_FLAG2; }
bool lfilm_Is_Film_Flag2(Film* film) { return film->flags & FF_FLAG2; }

void lfilm_Set_Film_Fade(uint8_t fade, uint8_t colorFade) {
	film_fade_gbl = fade;
	film_color_fade_gbl = colorFade;
}

static void sort_film_list(void) {
	Film* sorted = NULL;
	Film* cur = first_film_gbl;
	while (cur) {
		Film* node = cur;
		cur = cur->next;
		node->next = NULL;
		Film *pos = sorted, *prev = NULL;
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
	first_film_gbl = sorted;
}

void lfilm_Sort_Film_ZPlanes(void) { sort_film_list(); }

/* --- Render capture --- */

void lfilm_emit_render_state(void) {
	bool any_film = false;
	for (Film* f = first_film_gbl; f; f = f->next) {
		any_film = true;
		LandruFilmRenderState state = { 0 };
		LandruFilmRenderState* out = &state;
		out->res_type = f->res_type;
		memcpy(out->res_name, f->res_name, sizeof out->res_name);
		out->x = f->x;
		out->y = f->y;
		out->zplane = f->zplane;
		out->cur_cel = f->cur_cel;
		out->cels = f->cels;
		out->flags = f->flags;
		landru_render_film(out);
	}

	/* Publish the current engine frame and sub-cel progress so consumers may
	 * interpolate presentation without retiming the simulation. */
	if (any_film)
		landru_render_scene_clock(lview_Get_View_Time(), ltimer_Frame_Progress(), ltimer_Frame_Period_Us());
}
