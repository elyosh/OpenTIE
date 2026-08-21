#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <landru/fourcc.h>
#include <landru/memptr.h>
#include <landru/res.h>
#include <landru/sound.h>

// GLOBAL: TIE 0xD2C28
static Sound* first_sound_gbl = NULL;
// GLOBAL: TIE 0xD2C2C
static SoundActionFunc sound_func = NULL;

Sound* lsound_Ask_Sound_List(void) { return first_sound_gbl; }

void lsound_Open_Sound(void) {
	sound_func = NULL;
	first_sound_gbl = NULL;
}

void lsound_Close_Sound(void) {
	Sound* s = first_sound_gbl;
	while (s) {
		lsound_Clear_Sound_Keep(s);
		lsound_Clear_Sound_Keepable(s);
		s = s->next;
	}
	lsound_Free_Sounds(first_sound_gbl);
	first_sound_gbl = NULL;
}

void lsound_Purge_All_Sounds(void) {
	Sound* s = first_sound_gbl;
	while (s) {
		s->flags &= ~0x3000;
		s = s->next;
	}
	s = first_sound_gbl;
	while (s) {
		Sound* next = s->next;
		lsound_Free_Sound(s);
		s = next;
	}
	first_sound_gbl = NULL;
}

Sound* lsound_Alloc_Sound(int16_t extend) {
	Sound* sound = lmemptr_Alloc_System_Pointer(sizeof(Sound) + extend);
	if (!sound)
		return NULL;

	lsound_Init_Sound(sound);
	sound->next = first_sound_gbl;
	first_sound_gbl = sound;
	return sound;
}

void lsound_Init_Sound(Sound* sound) {
	sound->type = noSound;
	sound->id = 0;
	sound->flags = 0;
	sound->volume = 128;
	sound->var1 = 0;
	sound->var2 = 0;
	sound->varptr = NULL;
	sound->varhdl = NULL;
	sound->user = NULL;
	sound->data = NULL;
	sound->size = 0;
	sound->next = NULL;
}

Sound* lsound_Res_Music(const char* name) {
	ResFile* rf = lres_Find_File_With_Resource(FOURCC_GMID, name);
	if (!rf)
		return NULL;

	int res_offset;
	uint32_t res_size;
	if (!lres_Get_Resource_Offset(rf, FOURCC_GMID, name, &res_offset, &res_size))
		return NULL;

	Sound* sound = lsound_Alloc_Sound(0);
	if (!sound)
		return NULL;

	lsound_Set_Sound_Name(sound, FOURCC_GMID, name);
	lsound_Discard_Sound_Data(sound);
	sound->type = gmidiSound;
	sound->size = res_size;
	sound->data = lres_Load_Resource_Data(FOURCC_GMID, name);
	if (!sound->data) {
		lsound_Free_Playing_Sound(sound, 0);
		return NULL;
	}
	return sound;
}

Sound* lsound_Res_Digital_Sound(const char* name) {
	ResFile* rf = lres_Find_File_With_Resource(FOURCC_VOIC, name);
	if (!rf)
		return NULL;

	int res_offset;
	uint32_t res_size;
	if (!lres_Get_Resource_Offset(rf, FOURCC_VOIC, name, &res_offset, &res_size))
		return NULL;

	Sound* sound = lsound_Alloc_Sound(0);
	if (!sound)
		return NULL;

	lsound_Set_Sound_Name(sound, FOURCC_VOIC, name);
	lsound_Discard_Sound_Data(sound);
	sound->type = digitalSound;
	sound->size = res_size;
	sound->data = lres_Load_Resource_Data(FOURCC_VOIC, name);
	if (!sound->data) {
		lsound_Free_Playing_Sound(sound, 0);
		return NULL;
	}
	return sound;
}

Sound* lsound_Res_Digital_Voice(const char* name) { return lsound_Res_Digital_Sound(name); }

Sound* lsound_Res_Sound_Data(uint32_t res_type, const char* name, int16_t kind) {
	ResFile* rf = lres_Find_File_With_Resource(res_type, name);
	if (!rf)
		return NULL;

	int res_offset;
	uint32_t res_size;
	if (!lres_Get_Resource_Offset(rf, res_type, name, &res_offset, &res_size))
		return NULL;

	Sound* sound = lmemptr_Alloc_System_Pointer(sizeof(Sound));
	if (!sound)
		return NULL;

	lsound_Init_Sound(sound);
	sound->next = first_sound_gbl;
	first_sound_gbl = sound;

	lsound_Set_Sound_Name(sound, res_type, name);
	lsound_Discard_Sound_Data(sound);
	sound->type = kind;
	sound->size = res_size;

	/* iMUS resources load as VOIC with no alloc flags */
	if (res_type == 'iMUS')
		sound->data = lres_Load_Resource_Data(FOURCC_VOIC, name);
	else
		sound->data = lres_Load_Resource_Data(res_type, name);

	if (!sound->data) {
		lsound_Free_Playing_Sound(sound, 0);
		return NULL;
	}
	return sound;
}

void lsound_Free_Sound(Sound* sound) {
	if (!sound)
		return;

	if (lsound_Is_Sound_Keep(sound) || lsound_Is_Sound_Keepable(sound)) {
		lsound_Clear_Sound_Keep(sound);
		if (!lsound_Is_Sound_User_Keep(sound))
			sound->user = NULL;
		return;
	}

	/* Unlink from list */
	Sound* cur = first_sound_gbl;
	Sound* prev = NULL;
	while (cur && cur != sound) {
		prev = cur;
		cur = cur->next;
	}
	if (cur == sound) {
		if (prev)
			prev->next = sound->next;
		else
			first_sound_gbl = sound->next;
		sound->next = NULL;
	}

	lsound_Stop_Sound(sound);

	if (lsound_Is_Discard_Sound_Data(sound)) {
		free(sound->data);
		sound->data = NULL;
	}
	if (sound->varptr) {
		lmemptr_Free_System_Pointer(sound->varptr);
		sound->varptr = NULL;
	}
	if (sound->varhdl) {
		free(sound->varhdl);
		sound->varhdl = NULL;
	}
	lmemptr_Free_System_Pointer(sound);
}

void lsound_Free_Playing_Sound(Sound* sound, int16_t notify) {
	if (!sound)
		return;

	if (lsound_Is_Sound_Keep(sound) || lsound_Is_Sound_Keepable(sound)) {
		lsound_Clear_Sound_Keep(sound);
		if (!lsound_Is_Sound_User_Keep(sound))
			sound->user = NULL;
		return;
	}

	/* Unlink from list */
	Sound* cur = first_sound_gbl;
	Sound* prev = NULL;
	while (cur && cur != sound) {
		prev = cur;
		cur = cur->next;
	}
	if (cur == sound) {
		if (prev)
			prev->next = sound->next;
		else
			first_sound_gbl = sound->next;
		sound->next = NULL;
	}

	if (notify && sound_func)
		sound_func(6, sound, 0, 0);

	if (lsound_Is_Discard_Sound_Data(sound)) {
		free(sound->data);
		sound->data = NULL;
	}
	if (sound->varptr) {
		lmemptr_Free_System_Pointer(sound->varptr);
		sound->varptr = NULL;
	}
	if (sound->varhdl) {
		free(sound->varhdl);
		sound->varhdl = NULL;
	}
	lmemptr_Free_System_Pointer(sound);
}

void lsound_Free_Sounds(Sound* sound) {
	while (sound) {
		Sound* next = sound->next;
		lsound_Free_Playing_Sound(sound, 1);
		sound = next;
	}
}

void lsound_Free_Sound_From_User(Sound* sound) { sound->flags |= 0x01; }

void lsound_Free_User_Sounds(Sound* sound) {
	while (sound) {
		Sound* next = sound->next;
		if (sound->flags & 0x01)
			lsound_Free_Playing_Sound(sound, 1);
		sound = next;
	}
}

void lsound_Set_Sound_Action_Function(SoundActionFunc func) { sound_func = func; }

void lsound_Pause_Sounds(void) {
	if (sound_func)
		sound_func(1, NULL, 0, 0);
}

void lsound_Resume_Sounds(void) {
	if (sound_func)
		sound_func(2, NULL, 0, 0);
}

void lsound_Start_Music(Sound* sound) {
	if (sound_func)
		sound_func(3, sound, 0, 0);
}

void lsound_Start_SFX(Sound* sound) {
	if (sound_func)
		sound_func(4, sound, 0, 0);
}

void lsound_Start_Speech(Sound* sound) {
	if (sound_func)
		sound_func(5, sound, 0, 0);
}

void lsound_Stop_Sound(Sound* sound) {
	if (sound_func)
		sound_func(6, sound, 0, 0);
}

void lsound_Set_Sound_Volume(Sound* sound, int16_t volume) {
	if (sound_func)
		sound_func(7, sound, volume, 0);
}

void lsound_Set_Sound_Fade(Sound* sound, int16_t volume, int16_t time) {
	if (sound_func)
		sound_func(8, sound, volume, time);
}

void lsound_Set_Sound_Pan(Sound* sound, int16_t pan) {
	if (sound_func)
		sound_func(9, sound, pan, 0);
}

void lsound_Set_Sound_Pan_Fade(Sound* sound, int16_t pan, int16_t time) {
	if (sound_func)
		sound_func(10, sound, pan, time);
}

void lsound_User_Sounds(int32_t time) {
	Sound* sound = first_sound_gbl;
	while (sound) {
		if (sound->user)
			sound->user(sound, time);
		sound = sound->next;
	}
}

void lsound_Copy_Sound_Data(Sound* dst, Sound* src) {
	if (lsound_Is_Discard_Sound_Data(dst) && dst->data) {
		free(dst->data);
		dst->data = NULL;
		dst->size = 0;
	}

	lsound_Non_Discard_Sound_Data(dst);
	lsound_Set_Sound_Name(dst, src->res_type, src->res_name);
	dst->data = src->data;
	dst->size = src->size;
	dst->id = src->id;
	dst->type = src->type;
}

Sound* lsound_Find_Sound_Type(const char* name, uint32_t type) {
	Sound* sound = first_sound_gbl;
	while (sound) {
		if (sound->res_type == type) {
			int i;
			bool match = true;
			for (i = 0; i < 8 && match && name[i]; i++) {
				if (tolower(name[i]) != sound->res_name[i])
					match = false;
			}
			if (match && i > 0 && (i >= 8 || !sound->res_name[i]))
				return sound;
		}
		sound = sound->next;
	}
	return NULL;
}

void lsound_Discard_Sound_Data(Sound* sound) { sound->flags |= 0x0002; }
void lsound_Non_Discard_Sound_Data(Sound* sound) { sound->flags &= ~0x0002; }
bool lsound_Is_Discard_Sound_Data(Sound* sound) { return sound->flags & 0x0002; }

void lsound_Set_Sound_User_Keep(Sound* sound) { sound->flags |= 0x0004; }
void lsound_Clear_Sound_User_Keep(Sound* sound) { sound->flags &= ~0x0004; }
bool lsound_Is_Sound_User_Keep(Sound* sound) { return sound->flags & 0x0004; }

void lsound_Set_Sound_Keep(Sound* sound) { sound->flags |= 0x1000; }
void lsound_Clear_Sound_Keep(Sound* sound) { sound->flags &= ~0x1000; }
bool lsound_Is_Sound_Keep(Sound* sound) { return sound->flags & 0x1000; }

void lsound_Set_Sound_Keepable(Sound* sound) { sound->flags |= 0x2000; }
void lsound_Clear_Sound_Keepable(Sound* sound) { sound->flags &= ~0x2000; }
bool lsound_Is_Sound_Keepable(Sound* sound) { return sound->flags & 0x2000; }

void lsound_Set_Sound_Flag1(Sound* sound) { sound->flags |= 0x4000; }
void lsound_Clear_Sound_Flag1(Sound* sound) { sound->flags &= ~0x4000; }
bool lsound_Is_Sound_Flag1(Sound* sound) { return sound->flags & 0x4000; }

void lsound_Set_Sound_Flag2(Sound* sound) { sound->flags |= 0x8000; }
void lsound_Clear_Sound_Flag2(Sound* sound) { sound->flags &= ~0x8000; }
bool lsound_Is_Sound_Flag2(Sound* sound) { return sound->flags & 0x8000; }

void lsound_Set_Sound_Name(Sound* sound, uint32_t type, const char* name) {
	sound->res_type = type;
	int i;
	for (i = 0; i < 8 && name[i]; i++)
		sound->res_name[i] = tolower(name[i]);
	for (; i < 8; i++)
		sound->res_name[i] = 0;
}

void lsound_Set_Sound_User_Function(Sound* sound, SoundUserFunc func) { sound->user = func; }

void lsound_Get_Sound_User_Function(Sound* sound, SoundUserFunc* out) { *out = sound->user; }

/* Empty in the binary too. */
void lsound_Stop_Landru_Sound(void) {}
