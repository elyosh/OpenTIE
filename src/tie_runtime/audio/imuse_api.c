#include "tie_runtime/audio/imuse_api.h"
#include "tie_runtime/audio/imuse_session.h"

#include <imuse/commands.h>
#include <imuse/groups.h>
#include <imuse/lolevel.h>

#include <stdarg.h>
#include <string.h>

/* Each entry point returns -1 without a session, as the original stubs do
 * when no engine command function is installed. */

int lolevel_ImPause(void) { return im ? imuse_pause(im) : -1; }

int lolevel_ImResume(void) { return im ? imuse_resume(im) : -1; }

int lolevel_ImSetGroupVol(int group, int vol) { return im ? imuse_set_group_volume(im, group, vol) : -1; }

int lolevel_ImStartSound(intptr_t sound, int priority) {
	return im ? imuse_start_sound(im, sound, priority) : -1;
}

int lolevel_ImStopSound(intptr_t sound) { return im ? imuse_stop_sound(im, sound) : -1; }

int lolevel_ImStopAllSounds(void) { return im ? imuse_stop_all_sounds(im) : -1; }

intptr_t lolevel_ImGetNextSound(intptr_t sound) { return im ? imuse_next_sound(im, sound) : -1; }

int lolevel_ImSetParam(intptr_t sound, int param, int value) {
	return im ? imuse_set_param(im, sound, param, value) : -1;
}

int lolevel_ImGetParam(intptr_t sound, int param) { return im ? imuse_get_param(im, sound, param) : -1; }

int lolevel_ImFadeParam(intptr_t sound, int param, int target, int time) {
	return im ? imuse_fade_param(im, sound, param, target, time) : -1;
}

int lolevel_ImSetHook(intptr_t sound, int hook) {
	if (!im)
		return -1;
	imuse_set_hook(im, sound, (uint32_t)hook);
	return 0;
}

int lolevel_ImJumpMidi(intptr_t sound, int chunk, int measure, int beat, int tick, int sustain) {
	return im ? imuse_midi_jump(im, sound, chunk, measure, beat, tick, sustain) : -1;
}

int lolevel_ImScanMidi(intptr_t sound, int chunk, int measure, int beat, int tick) {
	return im ? imuse_midi_scan(im, sound, chunk, measure, beat, tick) : -1;
}

int lolevel_ImShareParts(intptr_t src, intptr_t dst) { return im ? imuse_share_parts(im, src, dst) : -1; }

int lolevel_ImCheckTrigger(intptr_t sound, int marker, intptr_t opcode) {
	return im ? imuse_check_trigger(im, sound, marker, opcode) : -1;
}

int lolevel_ImClearTrigger(intptr_t sound, int marker, intptr_t opcode) {
	return im ? imuse_clear_trigger(im, sound, marker, opcode) : -1;
}

/* Argument kinds of each command a trigger or deferral can carry, indexed by
 * engine opcode: 's' is a sound (intptr_t), 'i' an int. The original stubs
 * copy ten stack words whatever the command; reading only the command's own
 * arguments keeps the variadic calls defined. */
static const char* const command_arguments[IMUSE_CMD_PANIC_INTERNAL + 1] = {
	[IMUSE_CMD_PAUSE] = "",
	[IMUSE_CMD_RESUME] = "",
	[IMUSE_CMD_SET_GROUP_VOL] = "ii",
	[IMUSE_CMD_START_SOUND] = "si",
	[IMUSE_CMD_STOP_SOUND] = "s",
	[IMUSE_CMD_STOP_ALL_SOUNDS] = "",
	[IMUSE_CMD_GET_NEXT_SOUND] = "s",
	[IMUSE_CMD_SET_PARAM] = "sii",
	[IMUSE_CMD_GET_PARAM] = "si",
	[IMUSE_CMD_FADE_PARAM] = "siii",
	[IMUSE_CMD_SET_HOOK] = "si",
	[IMUSE_CMD_GET_HOOK] = "s",
	[IMUSE_CMD_CHECK_TRIGGER] = "sii",
	[IMUSE_CMD_CLEAR_TRIGGER] = "sii",
	[IMUSE_CMD_JUMP_MIDI] = "siiiii",
	[IMUSE_CMD_SCAN_MIDI] = "siiii",
	[IMUSE_CMD_SEND_MIDI_MSG] = "siii",
	[IMUSE_CMD_SHARE_PARTS] = "ss",
};

/* Fill a command from the caller's variadic arguments; false for an opcode
 * the table does not describe. */
static int read_command(ImuseCmd* cmd, intptr_t opcode, va_list args) {
	const char* kinds = "";
	int i;

	memset(cmd, 0, sizeof(*cmd));
	cmd->opcode = opcode;
	if (opcode >= 0 && opcode <= IMUSE_CMD_PANIC_INTERNAL) {
		kinds = command_arguments[opcode];
		if (!kinds)
			return 0;
	}
	for (i = 0; kinds[i]; ++i)
		cmd->args[i] = kinds[i] == 's' ? va_arg(args, intptr_t) : va_arg(args, int);
	return 1;
}

int lolevel_ImSetTrigger(intptr_t sound, int marker, intptr_t opcode, ...) {
	ImuseCmd cmd;
	va_list args;
	int ok;

	va_start(args, opcode);
	ok = read_command(&cmd, opcode, args);
	va_end(args);
	if (!im || !ok)
		return -1;
	return imuse_set_trigger(im, sound, marker, &cmd);
}

int lolevel_ImDeferCommand(int ticks, intptr_t opcode, ...) {
	ImuseCmd cmd;
	va_list args;
	int ok;

	va_start(args, opcode);
	ok = read_command(&cmd, opcode, args);
	va_end(args);
	if (!im || !ok)
		return -1;
	return imuse_defer_command(im, ticks, &cmd);
}

int filelist_ImInitFilelist(ImuseLoadSoundFunc load, ImuseUnloadSoundFunc unload, ImuseOpenSoundFunc open,
							ImuseCloseSoundFunc close) {
	return im ? imuse_filelist_init(im, load, unload, open, close) : -1;
}

intptr_t filelist_ImLoadSound(const char* name) { return im ? (intptr_t)imuse_filelist_load(im, name) : 0; }

void filelist_ImUnloadSound(intptr_t sound) {
	if (im)
		imuse_filelist_unload(im, (void*)sound);
}

void filelist_ImUnloadAll(void) {
	if (im)
		imuse_filelist_unload_all(im);
}

intptr_t filelist_ImFindSound(const char* name) { return im ? (intptr_t)imuse_filelist_find(im, name) : 0; }

void filelist_ImFlushSounds(void) {
	if (im)
		imuse_filelist_flush(im);
}

/* hilevel.c is built on the low-level entry points, as in the original:
 * the setters return 0, the getters query with a volume of -1, and the
 * starts assign the sound group after starting at the caller's priority. */

int hilevel_ImSetMasterVol(int vol) {
	lolevel_ImSetGroupVol(IMUSE_GROUP_MASTER, vol);
	return 0;
}

int hilevel_ImGetMasterVol(void) { return lolevel_ImSetGroupVol(IMUSE_GROUP_MASTER, -1); }

int hilevel_ImSetMusicVol(int vol) {
	lolevel_ImSetGroupVol(IMUSE_GROUP_MUSIC, vol);
	return 0;
}

int hilevel_ImGetMusicVol(void) { return lolevel_ImSetGroupVol(IMUSE_GROUP_MUSIC, -1); }

int hilevel_ImSetSfxVol(int vol) {
	lolevel_ImSetGroupVol(IMUSE_GROUP_SFX, vol);
	return 0;
}

int hilevel_ImGetSfxVol(void) { return lolevel_ImSetGroupVol(IMUSE_GROUP_SFX, -1); }

int hilevel_ImSetVoiceVol(int vol) {
	lolevel_ImSetGroupVol(IMUSE_GROUP_VOICE, vol);
	return 0;
}

int hilevel_ImGetVoiceVol(void) { return lolevel_ImSetGroupVol(IMUSE_GROUP_VOICE, -1); }

static int hilevel_StartInGroup(intptr_t sound, int priority, int group) {
	if (lolevel_ImStartSound(sound, priority) || lolevel_ImSetParam(sound, IMUSE_PARAM_SOUND_GROUP, group))
		return -1;
	return 0;
}

int hilevel_ImStartSfx(intptr_t sound, int priority) {
	return hilevel_StartInGroup(sound, priority, IMUSE_GROUP_SFX);
}

int hilevel_ImStartVoice(intptr_t sound, int priority) {
	return hilevel_StartInGroup(sound, priority, IMUSE_GROUP_VOICE);
}

int hilevel_ImStartMusic(intptr_t sound, int priority) {
	return hilevel_StartInGroup(sound, priority, IMUSE_GROUP_MUSIC);
}

int hilevel_ImStartDippedMusic(intptr_t sound, int priority) {
	return hilevel_StartInGroup(sound, priority, IMUSE_GROUP_DIPPED);
}
