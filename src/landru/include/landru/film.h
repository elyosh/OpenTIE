#ifndef LANDRU_FILM_H
#define LANDRU_FILM_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/actor.h>
#include <landru/pal.h>
#include <landru/rect.h>
#include <landru/res.h>
#include <landru/sound.h>

typedef struct Film Film;
typedef struct FilmObject FilmObject;

typedef void (*lfilmDrawFunc)(Film*, Rect*, Rect*, int16_t, int16_t, int16_t);
typedef void (*lfilmUpdateFunc)(Film*);
typedef void (*lfilmCallback)(Film*, int32_t);
typedef int16_t (*lfilmCallbackFunc)(Film*, FilmObject*);

enum FilmFlags {
	FF_VISIBLE = 0x0001,
	FF_ACTIVE = 0x0002,
	FF_DISCARD = 0x0004,
	FF_REFRESHABLE = 0x0008,
	FF_REFRESH = 0x0010,
	FF_REPEAT = 0x0020,
	FF_FLAG1 = 0x4000,
	FF_FLAG2 = 0x8000,
};

/* type_code values stored in FilmObject::id, set from the per-entry
 * header at load time. Determines which subsystem owns the linked
 * resource pointer (object). */
enum FilmTypeCode {
	FTC_VIEW = 2,
	FTC_ACTOR = 3,
	FTC_PALETTE = 4,
	FTC_SOUND = 5,
};

enum FilmCmd {
	FCMD_NONE = 0,
	FCMD_DELETE = 1,
	FCMD_END = 2,
	FCMD_TIMESTAMP = 3,
	FCMD_ACTOR_POS = 4,
	FCMD_ACTOR_VEL = 5,
	FCMD_ACTOR_Z = 6,
	FCMD_ACTOR_STATE = 7,
	FCMD_ACTOR_STATEV = 8,
	FCMD_ACTOR_VAR1 = 9,
	FCMD_ACTOR_VAR2 = 10,
	FCMD_ACTOR_CLIP = 11,
	FCMD_ACTOR_CLIPV = 12,
	FCMD_ACTOR_SHOW = 13,
	FCMD_ACTOR_FLIP = 14,
	FCMD_PALETTE_SET = 15,
	FCMD_VIEW_SETRGB = 16,
	FCMD_VIEW_DEFPAL = 17,
	FCMD_VIEW_FADE = 18,
	FCMD_SOUND_START = 19,
	FCMD_SOUND_STOP = 20,
	FCMD_SOUND_VOLUME = 21,
	FCMD_SOUND_FADE = 22,
	FCMD_SOUND_VAR1 = 23,
	FCMD_SOUND_VAR2 = 24,
	FCMD_SOUND_CMD = 25,
	FCMD_SOUND_PAN = 26,
	FCMD_SOUND_PAN_FADE = 27,
	FCMD_SOUND_CMD2 = 28,
};

struct FilmObject {
	uint32_t res_type;
	char res_name[8];
	uint16_t id;
	uint16_t offset;
	void* object;
};

struct Film {
	uint32_t res_type;
	char res_name[8];
	Film* next;
	Palette* def_palette;
	int32_t start;
	int32_t stop;
	Rect frame;
	uint16_t flags;
	int16_t zplane;
	int16_t x;
	int16_t y;
	uint16_t cels;
	uint16_t cur_cel;
	int16_t var1;
	int16_t var2;
	void* varptr;
	void* varhdl;
	void** array;
	int16_t array_size;
	lfilmDrawFunc draw;
	lfilmUpdateFunc update;
	lfilmCallback user;
};

Film* lfilm_Ask_Film_List(void);
void lfilm_Add_Film_To_System(Film* film);
void lfilm_Free_Film_From_System(Film* film);
Film* lfilm_Alloc_Film(int16_t extend);
void lfilm_Free_Film(Film* film);
void lfilm_Free_Film_Data(Film* film);
void lfilm_Free_Film_Data_Object(Film* film, int16_t objId);
void lfilm_Free_Films(Film* film);
Film* lfilm_Res_Film(const char* name, Rect* frame, int16_t x, int16_t y, int16_t zPlane);
Film* lfilm_Res_Callback_Film(const char* name, Rect* frame, int16_t x, int16_t y, int16_t zPlane,
							  lfilmCallbackFunc cbFunc);
bool lfilm_Res_Film_Data(ResFile* fp, void** array, int16_t count);
bool lfilm_Res_Film_Objects(Film* film, lfilmCallbackFunc callback);
bool lfilm_Res_Film_Object(uint32_t objType, const char* objName, void** outObj);
Actor* lfilm_Clone_Film_Actor(uint32_t actType, const char* actName);
Sound* lfilm_Clone_Film_Sound(uint32_t sndType, const char* sndName);
void lfilm_Init_Film(Film* film, void** array, Rect* frame, int16_t x, int16_t y, int16_t z);
void lfilm_Update_Films(int32_t time);
void lfilm_User_Films(int32_t time);
void lfilm_Draw_Films(int16_t refresh);
void lfilm_fupdate_Film(Film* film);
void lfilm_Get_Film_Relative_XY(Film* film, Rect* rect, int16_t* outX, int16_t* outY);
void lfilm_Set_Film_Name(Film* film, uint32_t res_type, const char* name);
void lfilm_Set_Film_Frame(Film* film, Rect* frame);
void lfilm_Get_Film_Frame(Film* film, Rect* outFrame);
void lfilm_Set_Film_Pos(Film* film, int16_t x, int16_t y);
void lfilm_Get_Film_Pos(Film* film, int16_t* outX, int16_t* outY);
void lfilm_Set_Film_ZPlane(Film* film, int16_t z);
void lfilm_Get_Film_ZPlane(Film* film, int16_t* outZ);
void lfilm_Set_Film_Time(Film* film, int32_t start, int32_t stop);
void lfilm_Get_Film_Time(Film* film, int32_t* outStart, int32_t* outStop);
void lfilm_Set_Film_Array(Film* film, void** array);
void lfilm_Get_Film_Array(Film* film, void*** outArray);
void lfilm_Set_Film_Def_Palette(Film* film, Palette* pal);
Palette* lfilm_Get_Film_Def_Palette(Film* film);
void lfilm_Set_Film_Draw_Function(Film* film, lfilmDrawFunc draw);
void lfilm_Get_Film_Draw_Function(Film* film, lfilmDrawFunc* outDraw);
void lfilm_Set_Film_Update_Function(Film* film, lfilmUpdateFunc update);
void lfilm_Get_Film_Update_Function(Film* film, lfilmUpdateFunc* outUpdate);
void lfilm_Set_Film_User_Function(Film* film, lfilmCallback user);
void lfilm_Get_Film_User_Function(Film* film, lfilmCallback* outUser);
void lfilm_Show_Film(Film* film);
void lfilm_Hide_Film(Film* film);
bool lfilm_Is_Film_Visible(Film* film);
void lfilm_Activate_Film(Film* film);
void lfilm_Deactivate_Film(Film* film);
bool lfilm_Is_Film_Active(Film* film);
void lfilm_Start_Film(Film* film);
void lfilm_Stop_Film(Film* film);
void lfilm_Rewind_Film(Film* film);
void lfilm_Refresh_Film(Film* film);
void lfilm_Non_Refresh_Film(Film* film);
bool lfilm_Is_Film_Refresh(Film* film);
void lfilm_Refreshable_Film(Film* film);
void lfilm_Non_Refreshable_Film(Film* film);
bool lfilm_Is_Film_Refreshable(Film* film);
void lfilm_Discard_Film_Data(Film* film);
void lfilm_Non_Discard_Film_Data(Film* film);
bool lfilm_Is_Discard_Film_Data(Film* film);
void lfilm_Repeat_Film(Film* film);
void lfilm_Non_Repeat_Film(Film* film);
bool lfilm_Is_Repeat_Film(Film* film);
void lfilm_Set_Film_Flag1(Film* film);
void lfilm_Clear_Film_Flag1(Film* film);
bool lfilm_Is_Film_Flag1(Film* film);
void lfilm_Set_Film_Flag2(Film* film);
void lfilm_Clear_Film_Flag2(Film* film);
bool lfilm_Is_Film_Flag2(Film* film);
void lfilm_Check_Film_ZPlanes(void);
void lfilm_Sort_Film_ZPlanes(void);
void lfilm_Clear_Film_Fade(void);
void lfilm_Start_Film_Fade(int16_t direction);
bool lfilm_Is_Film_Fade(void);
void lfilm_Set_Film_Fade(uint8_t fade, uint8_t colorFade);
void lfilm_Rewind_Film_Objects(Film* film);
void lfilm_Rewind_View_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Rewind_Palette_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Rewind_Actor_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Rewind_Sound_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Step_Film_Objects(Film* film);
void lfilm_Step_View_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Set_View_To_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Step_Palette_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Set_Palette_To_Film(FilmObject* obj, void* data);
void lfilm_Step_Actor_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Set_Actor_To_Film(FilmObject* obj, void* data);
void lfilm_Step_Sound_Film(Film* film, FilmObject* obj, void* data);
void lfilm_Set_Sound_To_Film(FilmObject* obj, void* data);
void lfilm_Step_Film_Frame(FilmObject* obj, void* data);
bool lfilm_Is_Film_Time_Stamp(Film* film, FilmObject* obj, void* data);
bool lfilm_Is_Film_Next_Frame(FilmObject* obj, void* data);

/* Publish the live film list and scene clock to the optional render sink. */
void lfilm_emit_render_state(void);

#endif
