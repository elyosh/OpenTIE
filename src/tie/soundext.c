#include "tie/soundext.h"
#include "tie/rand.h"
#include "tie/shellext.h"
#include "tie/wavestream_tie98.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include "landru/error.h"
#include "landru/file.h"
#include "landru/fourcc.h"
#include "landru/res.h"
#include "landru/sound.h"

/* --- External modules --- */

#include "tie/mfscript.h"
#include "tie/overlay.h"

#include "landru/memcom.h"

#include "tie/shipext.h"

#include <ctype.h>
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <stdlib.h>
#include <string.h>
#ifdef AUDIO_TRACE
#include <stdio.h>
#endif

/* XFILE */
/* XFILE — included via landru/file.h below */

/* --- Globals --- */

/* Retail scene cues as [scene, type, time, arg], terminated by {-1,-1,-1,-1}.
 * Types 0, 1, and 2 select cue point, state, and sequence respectively;
 * time -1 runs during scene setup. */
// GLOBAL: TIE95 0xCDDF8
// GLOBAL: TIE98 0x4EBC90
static int16_t sound_scene_list_gbl[644] = {
	6,   2, 0,   1,  6,   0, 0,   0,  6,   1, 0,   1,  6,   0, 46,  1,  7,   0,  -1,  2,  8,   0, -1,  3,
	8,   0, 40,  4,  10,  0, -1,  5,  20,  0, -1,  6,  30,  0, -1,  7,  30,  0,  71,  8,  40,  0, -1,  9,
	50,  0, -1,  10, 50,  0, 159, 11, 60,  0, -1,  12, 70,  0, -1,  13, 71,  0,  -1,  14, 80,  0, -1,  15,
	80,  0, 60,  16, 80,  0, 140, 17, 80,  0, 190, 18, 90,  0, -1,  19, 91,  2,  -1,  1,  91,  0, 0,   19,
	100, 1, 0,   1,  101, 1, -1,  1,  110, 1, -1,  2,  120, 2, -1,  2,  120, 1,  -1,  5,  121, 1, -1,  5,
	122, 1, -1,  6,  123, 1, -1,  7,  130, 2, -1,  3,  130, 1, -1,  8,  131, 1,  -1,  8,  132, 1, -1,  9,
	133, 1, -1,  10, 134, 1, -1,  11, 140, 1, -1,  4,  150, 1, -1,  3,  160, 1,  -1,  12, 180, 1, -1,  13,
	181, 1, -1,  16, 182, 1, -1,  14, 183, 1, -1,  15, 190, 1, -1,  17, 191, 1,  -1,  18, 192, 1, -1,  19,
	270, 2, -1,  5,  500, 2, -1,  6,  510, 2, -1,  7,  520, 2, -1,  8,  530, 2,  -1,  9,  531, 0, -1,  1,
	540, 2, -1,  10, 540, 0, 209, 1,  550, 2, -1,  11, 25,  2, -1,  12, 700, 0,  -1,  1,  700, 0, 50,  2,
	700, 0, 398, 5,  700, 0, 435, 6,  710, 0, -1,  1,  710, 0, 50,  3,  710, 0,  372, 5,  710, 0, 409, 6,
	720, 0, -1,  1,  720, 0, 50,  4,  720, 0, 331, 5,  720, 0, 368, 6,  280, 2,  -1,  13, 281, 2, -1,  14,
	282, 2, -1,  15, 283, 2, -1,  16, 284, 2, -1,  17, 285, 2, -1,  18, 231, 0,  -1,  1,  231, 0, 30,  2,
	210, 2, -1,  19, 240, 2, -1,  20, 560, 0, -1,  1,  560, 0, 43,  2,  250, 0,  -1,  1,  251, 0, -1,  3,
	251, 0, 79,  4,  252, 0, -1,  3,  252, 0, 79,  4,  253, 0, -1,  3,  253, 0,  79,  4,  254, 0, -1,  3,
	254, 0, 79,  4,  255, 0, -1,  3,  255, 0, 79,  4,  256, 0, -1,  3,  256, 0,  79,  4,  257, 0, -1,  3,
	257, 0, 79,  4,  390, 0, -1,  5,  400, 0, -1,  6,  401, 0, -1,  6,  402, 0,  -1,  6,  403, 0, -1,  6,
	404, 0, -1,  6,  405, 0, -1,  6,  420, 0, -1,  7,  420, 0, 49,  8,  570, 2,  -1,  21, 570, 0, -1,  0,
	571, 0, -1,  1,  572, 0, -1,  2,  573, 0, -1,  3,  573, 0, 195, 4,  573, 0,  344, 5,  580, 2, -1,  22,
	580, 0, 50,  0,  580, 0, 75,  1,  580, 0, 119, 2,  581, 0, 1,   3,  581, 0,  205, 4,  590, 2, -1,  23,
	590, 0, 80,  0,  591, 0, 5,   1,  591, 0, 55,  2,  591, 0, 170, 3,  591, 0,  235, 4,  407, 0, -1,  6,
	408, 0, -1,  6,  409, 0, -1,  6,  730, 0, -1,  1,  730, 0, 50,  2,  730, 0,  352, 5,  730, 0, 389, 6,
	600, 2, -1,  24, 601, 0, 0,   1,  603, 0, 0,   2,  603, 0, 454, 3,  610, 2,  -1,  25, 610, 0, 235, 1,
	610, 0, 400, 2,  620, 2, -1,  26, 621, 0, 0,   1,  622, 0, 83,  2,  622, 0,  220, 3,  623, 0, 95,  4,
	740, 2, -1,  27, 740, 0, 0,   1,  740, 0, 446, 2,  740, 0, 542, 3,  258, 0,  -1,  3,  258, 0, 79,  4,
	259, 0, -1,  3,  259, 0, 79,  4,  260, 0, -1,  3,  260, 0, 79,  4,  261, 0,  -1,  3,  261, 0, 79,  4,
	262, 0, -1,  3,  262, 0, 79,  4,  263, 0, -1,  3,  263, 0, 79,  4,  -1,  -1, -1,  -1,
};
// GLOBAL: TIE95 0xCE300
// GLOBAL: TIE98 0x4EC198
static int16_t sound_start_gbl = -1;
// GLOBAL: TIE95 0xCE302
// GLOBAL: TIE98 0x4EC19C
static int16_t sound_stop_gbl = -1;

// GLOBAL: TIE95 0xCE306
// GLOBAL: TIE98 0x4EC1A0
static char Sound_Speech_Name[1][10] = {
	"reg1",
};

// GLOBAL: TIE95 0xCE310
// GLOBAL: TIE98 0x4EC1B0
static char Sound_SFX_Name[19][10] = {
	"door-1",  "door-1a",  "door-1b",  "door-1c",  "dr-cls-1", "door-1",   "dr-cls-1",
	"guncock", "log-on-c", "visor-1b", "button-1", "button-2", "target-4", "target-5",
	"door-6",  "vis-clk2", "text-5",   "door-5",   "cannon-1",
};

// GLOBAL: TIE95 0xF5008
// GLOBAL: TIE98 0x589C4C
static ResFile* music_file;
// GLOBAL: TIE95 0xF500C
// GLOBAL: TIE98 0x589C54
static ResFile* music2_file;
// GLOBAL: TIE95 0xF5010
// GLOBAL: TIE98 0x589C44
static ResFile* sfx2_file;
// GLOBAL: TIE95 0xF5014
// GLOBAL: TIE98 0x589C60
static ResFile* speech_file;
// GLOBAL: TIE95 0xF5018
// GLOBAL: TIE98 0x589C48
static ResFile* speech2_file;
// GLOBAL: TIE95 0xF501C
// GLOBAL: TIE98 0x589C64
static ResFile* sfx_file;
// GLOBAL: TIE95 0xF5020
// GLOBAL: TIE98 0x589C5C
static Sound* music_sound;
// GLOBAL: TIE95 0xF5024
// GLOBAL: TIE98 0x589C50
static int16_t script_active_gbl;
// GLOBAL: TIE95 0xF5026
// GLOBAL: TIE98 0x589C58
static int16_t group_vol_gbl;

/* --- Internal helpers --- */

static void soundext_isnd_iMuse(Sound* the_sound, int32_t time);
static void soundext_Find_Sound_Range(int16_t scene, int16_t* pstart, int16_t* pstop);

/* --- Functions --- */

// FUNCTION: TIE95 0x651E4
void soundext_Open_Post_iMuse(int16_t use_script) {
	void* fp;

	script_active_gbl = use_script && TieMusicPolicy_UsesImuse();
	xmemcom_Add_Memory_Callback(soundext_compact_Sound, 0x1000);
	/* Adapt the recovered usercall callback to Landru's function type. */
	xsound_Set_Sound_Action_Function((SoundActionFunc)(void*)soundext_Action_iMuse);

	music_file = shellext_Open_Empire_Resource("tiemusic.lfd");
	sfx_file = shellext_Open_Empire_Resource("tiesfx.lfd");
	sfx2_file = shellext_Open_Empire_Resource("tiesfx2.lfd");
	speech_file = shellext_Open_Empire_Resource("tiespch.lfd");

	/* tiespch2.lfd and tiemus2.lfd are optional (CD version extras) */
	fp = shellext_Open_Empire_File("tiespch2.lfd", "r");
	if (fp) {
		xfile_Close_File(fp);
		speech2_file = shellext_Open_Empire_Resource("tiespch2.lfd");
	} else {
		speech2_file = NULL;
	}

	fp = shellext_Open_Empire_File("tiemus2.lfd", "r");
	if (fp) {
		xfile_Close_File(fp);
		music2_file = shellext_Open_Empire_Resource("tiemus2.lfd");
	} else {
		music2_file = NULL;
	}

	if (script_active_gbl) {
		mfscript_MfStartScript(initData);
		music_sound = xsound_Alloc_Sound(LANDRU_NULL_HANDLE, 0, 0);
		xsound_Set_Sound_Keepable(music_sound);
		xsound_Set_Sound_User_Keep(music_sound);
		xsound_Set_Sound_User_Function(music_sound, soundext_isnd_iMuse);
	}
}

// FUNCTION: TIE95 0x6531A
void soundext_Close_Post_iMuse(void) {
	xres_Close_Resource(speech_file);
	xres_Close_Resource(sfx_file);
	xres_Close_Resource(sfx2_file);
	xres_Close_Resource(music_file);
	if (speech2_file)
		xres_Close_Resource(speech2_file);
	if (music2_file)
		xres_Close_Resource(music2_file);

	if (script_active_gbl) {
		xsound_Clear_Sound_Keepable(music_sound);
		xsound_Free_Sound(music_sound);
		mfscript_MfStopScript();
	}

	xsound_Set_Sound_Action_Function(NULL);
	xmemcom_Free_Memory_Callback(soundext_compact_Sound);
}

// FUNCTION: TIE95 0x653BB
void soundext_Open_Sound_Scene(int16_t scene) {
	int16_t start, stop;

	if (script_active_gbl) {
		soundext_Find_Sound_Range(scene, &start, &stop);
		sound_start_gbl = start;
		sound_stop_gbl = stop;
	}
}

// FUNCTION: TIE95 0x6540A
void soundext_Close_Sound_Scene(int16_t scene, int16_t next_scene) {
	Sound* snd;
	int16_t flight;

	(void)scene;
	flight = (uint16_t)next_scene >= 2 && (uint16_t)next_scene <= 4;

	if (shellext_Is_Sudden_Scene_End() || flight) {
		for (snd = xsound_Ask_Sound_List(); snd; snd = snd->next) {
			if (snd->type == digitalSound)
				xsound_Stop_Sound(snd);
		}
	}

	soundext_Prep_Sound_Scene(next_scene);
}

/*
 * Pre-load sound state for the next scene.
 * Walks the sound scene list for entries matching next_scene, processing
 * only time==-1 entries to extract cue, state, and sequence values.
 */
// FUNCTION: TIE95 0x6549B
void soundext_Prep_Sound_Scene(int16_t next_scene) {
	int16_t start, stop;
	int16_t type, time_val, arg;
	int16_t state, seq, cue;
	int16_t index, i;

	if (!script_active_gbl)
		return;

	soundext_Find_Sound_Range(next_scene, &start, &stop);
	state = 0;
	seq = 0;
	cue = -1;

	for (i = start, index = start * 4; i < stop; i++, index += 4) {
		type = sound_scene_list_gbl[index + 1];
		time_val = sound_scene_list_gbl[index + 2];

		if (time_val != -1)
			continue;

		arg = sound_scene_list_gbl[index + 3];
		switch (type) {
			case 0:
				cue = arg;
				break;
			case 1:
				state = arg;
				break;
			case 2:
				seq = arg;
				break;
		}
	}

	if (seq)
		mfscript_MfSetSequence(seq);

	if (state) {
		/* Remap victory states to defeat variants if mission failed */
		if (state == 17 && !shipext_Is_Mission_Success())
			state = 20;
		else if (state == 18 && !shipext_Is_Mission_Success())
			state = 21;
		else if (state == 19 && !shipext_Is_Mission_Success())
			state = 22;

		mfscript_MfSetState(state);
		if (!seq && cue == -1)
			mfscript_MfSetSequence(0);
	}

	if (cue != -1)
		mfscript_MfSetCuePoint(cue);
}

/* Per-frame iMUSE callback on music_sound */
// FUNCTION: TIE95 0x65649
static void soundext_isnd_iMuse(Sound* the_sound, int32_t time) {
	int16_t state, seq, cue;
	int16_t sound_type, sound_arg;
	int16_t index, i;

	(void)the_sound;
	state = 0;
	seq = 0;
	cue = -1;

	for (i = sound_start_gbl, index = sound_start_gbl * 4; i < sound_stop_gbl; i++, index += 4) {
		if (sound_scene_list_gbl[index + 2] != (int16_t)time)
			continue;

		sound_type = sound_scene_list_gbl[index + 1];
		sound_arg = sound_scene_list_gbl[index + 3];

		switch (sound_type) {
			case 0:
				mfscript_MfSetCuePoint(sound_arg);
				cue = sound_arg;
				break;
			case 1:
				mfscript_MfSetState(sound_arg);
				state = sound_arg;
				break;
			case 2:
				mfscript_MfSetSequence(sound_arg);
				seq = sound_arg;
				break;
		}
	}

	if (state && !seq && cue == -1)
		mfscript_MfSetSequence(0);

	mfscript_MfRefreshScript();
}

/* Search sound_scene_list_gbl for all entries matching scene.
   Returns entry indices (not WORD indices) via pstart/pstop. */
// FUNCTION: TIE95 0x65770
static void soundext_Find_Sound_Range(int16_t scene, int16_t* pstart, int16_t* pstop) {
	int16_t index, start, stop;

	index = 0;
	start = -1;
	stop = -1;

	/* Search for first entry matching scene (sentinel = scene -1).
	   Extend stop inside the match block so the no-match path
	   doesn't trip over the original binary's OOB int16 read at
	   &sound_scene_list_gbl[-1] (benign on x86 because that slot
	   happened not to equal any queried scene id, but UB in C). */
	while (sound_scene_list_gbl[index] != -1) {
		if (sound_scene_list_gbl[index] == scene) {
			start = index;
			stop = index + 4;
			while (sound_scene_list_gbl[stop] == scene)
				stop += 4;
			break;
		}
		index += 4;
	}

	/* Convert WORD indices to entry indices */
	start /= 4;
	stop /= 4;

	if (start == stop) {
		start = -1;
		stop = -1;
	}

	*pstart = start;
	*pstop = stop;
}

// FUNCTION: TIE95 0x65837
void soundext_Play_SFX(uint8_t sound_index, int16_t volume) {
	Sound* snd;
	uint8_t idx = sound_index;

	if (idx == sfxSmallDoorOpen)
		idx = (rand_rand() & 3) + 1;

	snd = xsound_Find_Sound_Type(Sound_SFX_Name[idx - 1], FOURCC_VOIC);
	if (!snd)
		snd = xsound_Res_Digital_Sound(Sound_SFX_Name[idx - 1]);
	if (snd) {
		xsound_Start_SFX(snd);
		xsound_Set_Sound_Keep(snd);
		if (volume)
			xsound_Set_Sound_Volume(snd, volume);
	}
}

// FUNCTION: TIE95 0x658D0
void soundext_Fade_SFX(uint8_t sound_index, int16_t volume, int16_t time) {
	Sound* snd;

	snd = xsound_Find_Sound_Type(Sound_SFX_Name[sound_index - 1], FOURCC_VOIC);
	if (snd)
		xsound_Set_Sound_Fade(snd, volume, time);
}

// FUNCTION: TIE95 0x65929
void soundext_Stop_SFX(uint8_t sound_index) {
	Sound* snd;

	snd = xsound_Find_Sound_Type(Sound_SFX_Name[sound_index - 1], FOURCC_VOIC);
	if (snd)
		xsound_Stop_Sound(snd);
}

// FUNCTION: TIE95 0x65978
void soundext_Play_Speech(uint8_t sound_index) {
	Sound* snd;

	snd = xsound_Find_Sound_Type(Sound_Speech_Name[sound_index - 1], FOURCC_VOIC);
	if (!snd)
		snd = xsound_Res_Digital_Sound(Sound_Speech_Name[sound_index - 1]);
	if (snd) {
		xsound_Start_Speech(snd);
		imuse_set_param(im, TieImuse_SoundId(snd), 0x500, 4);
	}
}

// FUNCTION: TIE95 0x659F7
void soundext_compact_Sound(int16_t post_compaction) {
	if (post_compaction)
		imuse_resume(im);
	else
		imuse_pause(im);
}

// FUNCTION: TIE95 0x65A2E
void soundext_Action_iMuse(int16_t state, Sound* the_sound, int16_t var1, int16_t var2) {

#ifdef AUDIO_TRACE
	/* state is really a dispatch opcode for the sound action. Names
	 * match the switch handlers below; keep in sync if cases change. */
	static const char* const s_opNames[] = {
		[1] = "PauseAll",  /* master=0, save, pause */
		[2] = "ResumeAll", /* restore master, resume */
		[3] = "StartMusic", [4] = "StartSfx", [5] = "StartVoice",
		[6] = "StopSound",  [7] = "SetVol", /* param 0x600 = soundVol */
		[8] = "FadeVol",                    /* param 0x600 */
		[9] = "SetPan",                     /* param 0x700 = soundPan */
		[10] = "FadePan",                   /* param 0x700 */
	};
	const char* name = ((unsigned)state < sizeof(s_opNames) / sizeof(s_opNames[0]) && s_opNames[state])
						   ? s_opNames[state]
						   : "?";
	TieDiagnostics_Log(TIE_LOG_INFO, "[audio-dbg] soundext_Action_iMuse op=%s(%d) sound=%p v1=%d v2=%d\n",
					   name, state, (void*)the_sound, var1, var2);
#endif

	switch (state) {
		case 1:
			if (TieMusicPolicy_UsesTie98())
				FrontendWaveStream_Pause();
			imuse_pause(im);
			group_vol_gbl = imuse_set_group_volume(im, IMUSE_GROUP_MASTER, 0);
			break;
		case 2:
			imuse_set_group_volume(im, IMUSE_GROUP_MASTER, group_vol_gbl);
			imuse_resume(im);
			if (TieMusicPolicy_UsesTie98())
				FrontendWaveStream_Resume();
			break;
		case 3:
			imuse_start_music(im, the_sound);
			break;
		case 4:
			imuse_start_sfx(im, the_sound);
			break;
		case 5:
			imuse_start_voice(im, the_sound);
			break;
		case 6:
			imuse_stop_sound(im, TieImuse_SoundId(the_sound));
			break;
		case 7:
			imuse_set_param(im, TieImuse_SoundId(the_sound), 0x600, var1);
			break;
		case 8:
			imuse_fade_param(im, TieImuse_SoundId(the_sound), 0x600, var1, var2);
			break;
		case 9:
			imuse_set_param(im, TieImuse_SoundId(the_sound), 0x700, var1);
			break;
		case 10:
			imuse_fade_param(im, TieImuse_SoundId(the_sound), 0x700, var1, var2);
			break;
	}
}

// FUNCTION: TIE95 0x65B5A
void* soundext_TIE_Load_Sound(const char* name) {
	char low_name[16];
	Sound* snd;
	int i;

	for (i = 0; name[i]; i++)
		low_name[i] = tolower((unsigned char)name[i]);
	low_name[i] = '\0';

	snd = xsound_Res_Music(low_name);
	xsound_Set_Sound_Keepable(snd);
	return snd;
}

// FUNCTION: TIE95 0x65BEE
void soundext_TIE_Unload_Sound(void* sound) {
	xsound_Clear_Sound_Keepable((Sound*)sound);
	xsound_Free_Sound((Sound*)sound);
}

/* Release-build debug stub: copies the message and discards it. */
// FUNCTION: TIE98 0x485DB0
void soundext_TIE_Print_Msg(const char* ptr) {
	char msg[80];

	strcpy(msg, ptr);
}
