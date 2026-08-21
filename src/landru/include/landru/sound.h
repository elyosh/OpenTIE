#ifndef LANDRU_SOUND_H
#define LANDRU_SOUND_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Sound Sound;

typedef void (*SoundActionFunc)(int action, Sound* snd, int arg3, int arg4);
typedef void (*SoundUserFunc)(Sound* snd, int32_t time);

typedef enum {
	noSound = 0,
	gmidiSound = 1,
	digitalSound = 2,
} SoundType;

struct Sound {
	uint32_t res_type;
	char res_name[8];
	Sound* next;
	SoundType type;
	int16_t id;
	int16_t flags;
	int16_t volume;
	int16_t var1;
	int16_t var2;
	void* varptr;
	void* varhdl;
	SoundUserFunc user;
	void* data;
	int32_t size;
};

Sound* lsound_Ask_Sound_List(void);
void lsound_Open_Sound(void);
void lsound_Close_Sound(void);
void lsound_Purge_All_Sounds(void);
Sound* lsound_Alloc_Sound(int16_t extend);
void lsound_Init_Sound(Sound* sound);
Sound* lsound_Res_Music(const char* name);
Sound* lsound_Res_Digital_Sound(const char* name);
Sound* lsound_Res_Digital_Voice(const char* name);
Sound* lsound_Res_Sound_Data(uint32_t res_type, const char* name, int16_t kind);
void lsound_Free_Sound(Sound* sound);
void lsound_Free_Playing_Sound(Sound* sound, int16_t notify);
void lsound_Free_Sounds(Sound* sound);
void lsound_Free_Sound_From_User(Sound* sound);
void lsound_Free_User_Sounds(Sound* sound);
void lsound_Set_Sound_Action_Function(SoundActionFunc func);
void lsound_Pause_Sounds(void);
void lsound_Resume_Sounds(void);
void lsound_Start_Music(Sound* sound);
void lsound_Start_SFX(Sound* sound);
void lsound_Start_Speech(Sound* sound);
void lsound_Stop_Sound(Sound* sound);
void lsound_Set_Sound_Volume(Sound* sound, int16_t volume);
void lsound_Set_Sound_Fade(Sound* sound, int16_t volume, int16_t time);
void lsound_Set_Sound_Pan(Sound* sound, int16_t pan);
void lsound_Set_Sound_Pan_Fade(Sound* sound, int16_t pan, int16_t time);
void lsound_User_Sounds(int32_t time);
void lsound_Copy_Sound_Data(Sound* dst, Sound* src);
Sound* lsound_Find_Sound_Type(const char* name, uint32_t type);
void lsound_Discard_Sound_Data(Sound* sound);
void lsound_Non_Discard_Sound_Data(Sound* sound);
bool lsound_Is_Discard_Sound_Data(Sound* sound);
void lsound_Set_Sound_User_Keep(Sound* sound);
void lsound_Clear_Sound_User_Keep(Sound* sound);
bool lsound_Is_Sound_User_Keep(Sound* sound);
void lsound_Set_Sound_Keep(Sound* sound);
void lsound_Clear_Sound_Keep(Sound* sound);
bool lsound_Is_Sound_Keep(Sound* sound);
void lsound_Set_Sound_Keepable(Sound* sound);
void lsound_Clear_Sound_Keepable(Sound* sound);
bool lsound_Is_Sound_Keepable(Sound* sound);
void lsound_Set_Sound_Flag1(Sound* sound);
void lsound_Clear_Sound_Flag1(Sound* sound);
bool lsound_Is_Sound_Flag1(Sound* sound);
void lsound_Set_Sound_Flag2(Sound* sound);
void lsound_Clear_Sound_Flag2(Sound* sound);
bool lsound_Is_Sound_Flag2(Sound* sound);
void lsound_Set_Sound_Name(Sound* sound, uint32_t type, const char* name);
void lsound_Set_Sound_User_Function(Sound* sound, SoundUserFunc func);
void lsound_Get_Sound_User_Function(Sound* sound, SoundUserFunc* out);
void lsound_Stop_Landru_Sound(void);

#endif
