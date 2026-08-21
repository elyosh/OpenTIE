#ifndef __USER_H__
#define __USER_H__

#include "tie/tie.h" /* CraftData typedef */
#include "tie_runtime/snapshot/snapshot.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Active craft-slot count used by in-flight target and enemy scans. */
#define NUM_ACTIVE_CRAFT_SLOTS 32

/*
 * UserKey -- codes returned by feinput_getrawinput in `inputkey` and
 * dispatched on by user_userinterface, user_inputforplane, and other
 * in-flight callers (replay.c, maproom.c). Every entry names a
 * keystroke, never an action — the action lives at the call site.
 *
 * Three families:
 *   1. ASCII printable: case-sensitive identifier matches the literal
 *      character. KEY_A (65) is Shift+a, KEY_a (97) is bare 'a'.
 *   2. Arrow keys: feinput_getrawinput translates BIOS extended scans
 *      0x4B/0x4D/0x48/0x50 (and Ctrl+arrow variants) to engine codes
 *      1..4.
 *   3. Other extended scan codes (Alt+letter, Alt+digit, F1..F10,
 *      Shift+F5..F10): the engine returns `scan | 0x80`. Joystick
 *      chord-release synthesis in user_userinterface reuses Shift+F9
 *      and Shift+F10 codes so the dispatcher handles keyboard and
 *      joystick alternates uniformly.
 */
typedef enum {
	KEY_NONE = 0,
	KEY_LEFT_ARROW = 1,
	KEY_RIGHT_ARROW = 2,
	KEY_UP_ARROW = 3,
	KEY_DOWN_ARROW = 4,
	KEY_BACKSPACE = 8,
	KEY_ENTER = 13,
	KEY_ESCAPE = 27,
	KEY_SPACE = 32,
	KEY_APOSTROPHE = 39, /* ' */
	KEY_ASTERISK = 42,   /* * */
	KEY_PLUS = 43,
	KEY_COMMA = 44,
	KEY_MINUS = 45,
	KEY_PERIOD = 46,
	KEY_SLASH = 47,
	KEY_0 = 48,
	KEY_1 = 49,
	KEY_2 = 50,
	KEY_3 = 51,
	KEY_4 = 52,
	KEY_5 = 53,
	KEY_6 = 54,
	KEY_7 = 55,
	KEY_8 = 56,
	KEY_9 = 57,
	KEY_SEMICOLON = 59,
	KEY_LESS = 60, /* < */
	KEY_EQUALS = 61,
	KEY_A = 65, /* Shift+a */
	KEY_B = 66,
	KEY_C = 67,
	KEY_D = 68,
	KEY_E = 69,
	KEY_F = 70,
	KEY_G = 71,
	KEY_H = 72,
	KEY_I = 73,
	KEY_J = 74,
	KEY_K = 75,
	KEY_L = 76,
	KEY_M = 77,
	KEY_N = 78,
	KEY_O = 79,
	KEY_P = 80,
	KEY_Q = 81,
	KEY_R = 82,
	KEY_S = 83,
	KEY_T = 84,
	KEY_U = 85,
	KEY_V = 86,
	KEY_W = 87,
	KEY_X = 88,
	KEY_Y = 89,
	KEY_Z = 90,
	KEY_LBRACKET = 91, /* [ */
	KEY_BACKSLASH = 92,
	KEY_RBRACKET = 93,
	KEY_a = 97,
	KEY_b = 98,
	KEY_c = 99,
	KEY_d = 100,
	KEY_e = 101,
	KEY_f = 102,
	KEY_g = 103,
	KEY_h = 104,
	KEY_i = 105,
	KEY_j = 106,
	KEY_k = 107,
	KEY_l = 108,
	KEY_m = 109,
	KEY_n = 110,
	KEY_o = 111,
	KEY_p = 112,
	KEY_q = 113,
	KEY_r = 114,
	KEY_s = 115,
	KEY_t = 116,
	KEY_u = 117,
	KEY_v = 118,
	KEY_w = 119,
	KEY_x = 120,
	KEY_y = 121,
	KEY_z = 122,
	KEY_ALT_E = 146, /* scan 0x12 | 0x80 */
	KEY_ALT_T = 148,
	KEY_ALT_O = 152,
	KEY_ALT_P = 153,
	KEY_ALT_S = 159,
	KEY_ALT_D = 160,
	KEY_ALT_C = 174,
	KEY_ALT_V = 175,
	KEY_ALT_B = 176,
	KEY_ALT_M = 178,
	KEY_F1 = 187, /* scan 0x3B | 0x80 */
	KEY_F2 = 188,
	KEY_F3 = 189,
	KEY_F4 = 190,
	KEY_F5 = 191,
	KEY_F6 = 192,
	KEY_F7 = 193,
	KEY_F8 = 194,
	KEY_F9 = 195,
	KEY_F10 = 196,
	KEY_SHIFT_F5 = 216, /* scan 0x58 | 0x80 */
	KEY_SHIFT_F6 = 217,
	KEY_SHIFT_F7 = 218,
	KEY_SHIFT_F9 = 220, /* also synth from joystick chord-11 release */
	KEY_SHIFT_F10 = 221,
	KEY_ALT_1 = 248, /* scan 0x78 | 0x80 */
} UserKey;

/*
 * USER -- in-flight player input + view + replay-camera dispatcher.
 *
 * Public entry points consumed by the rest of the engine. USER owns the
 * per-frame cockpit loop (user_userinterface -> user_inputforplane) plus
 * the target-bracket reticle, view matrix reset, replay tape I/O, and
 * the in-flight info/options room.
 */

/* --- Top-level frame entry points --------------------------------- */

/* Called once per frame from TIE_doframe. Gates replay/pause/options
 * meta-keys, records the input tuple to the replay tape, then dispatches
 * into user_inputforplane for flight controls. */
void user_userinterface(void);

int user_is_paused(void);

/* Full in-flight key/joystick dispatcher (~150 keybinds). Called from
 * user_userinterface after the hyperspace-freeze / replay-view / pause
 * guards clear. */
void user_inputforplane(void);

/* Replay-playback tick (advance counters, page/stop buffer). */
void user_nextreplaycount(void);

/* Replay-record tick (spool / abort on full buffer). */
void user_nextreplaystore(void);

/* --- Math + flight-control helpers -------------------------------- */

/* Scale a per-frame increment by the elapsed PIT ticks. 236 is the
 * nominal tick count for one frame at the target frame rate. */
int16_t user_framerateadjust(int16_t per_236);

/* Player-throttle adjust (saturating u16). */
void user_increasepower(uint16_t delta);
void user_decreasepower(uint16_t delta);

/* Pour shield energy from src_idx (0=front, 1=rear) to dst_idx. */
void user_adjustshields(uint16_t dst_idx, uint16_t src_idx);

/* Recenter the pilot view: clears camera.view_pitch_offset when not zoomed,
 * priming cam-chase history when zoomed. */
void user_resetview(void);

/* Rotate craft's forward vector by (dheading, dpitch), decompose back
 * to Euler (heading, pitch, roll) and write into objects[obj_idx]. */
void user_calcdeltapitch(int16_t dheading, int16_t dpitch, uint16_t obj_idx, CraftData* cp);

/* Install the detail preset tables into the runtime flags. */
void user_setdetaillevel(uint16_t level);

/* --- Targeting ---------------------------------------------------- */

/* Double-beam auto-target: scan 116 craft + 64 static slots and return
 * the best-scored hostile target, or 0xFFFF if none. */
uint16_t user_picktarget(void);

/* Step the target cursor by +1/-1 through the target index space,
 * skipping dead / friendly / non-targetable slots. Updates the global
 * _craftptr to the new target's CraftData. */
uint16_t user_picknexttarget(uint16_t start, int32_t step);

/* Reticle hit test: does obj_idx project inside the gunsight this frame?
 * strict=1 uses the pixel-accurate reticle; strict=0 triples the
 * tolerance for the auto-target scanner. Side effect: writes screendist. */
int16_t user_targetincross(uint16_t obj_idx, int32_t strict);

/* Paint the target bracket around the currently-targeted object. Called
 * from anim_sort_and_draw_bitmaps each frame. Returns the
 * rotatescaleimage result (or 0 if off-screen / invalid). */
int16_t user_targetonscreen(uint16_t obj_or_kind);
void user_targetonscreen_tie98(uint16_t object_reference, int16_t mesh_index, uint8_t color_index);

/* Lock player_craft's target on new_obj. Plays the target-acquired beep,
 * primes radar_target1 on the first MainHull/Engines mesh, and emits a
 * radio report when the target viewer is active. */
void user_setnewtarget(uint16_t obj_idx);

/* Validate that target_obj_idx is a live, radio-reachable craft; stores
 * its craft_ptr into the global _craftptr for the caller. */
int16_t user_checkradio(void);

/* Give a wingman order. Walks objects[0..27] picking every friendly FG
 * member with a non-dead order and points its pending_radio_command at the
 * new target; emits a radio ack from the last tasked craft. */
void user_assigntarget(uint16_t new_target_obj, uint16_t msg_template_id);

/* Find closest enemy currently attacking obj_idx. */
uint16_t user_findclosestattacker(uint16_t obj_idx);

/* Eject-pod rescue test: true if nearest friendly is closer than half
 * the distance to the nearest hostile (or rescue_override_flag&1). */
int16_t user_isrescued(uint16_t player_obj_idx);

/* --- Replay camera ------------------------------------------------ */

/* Advance the replay-record buffer if recording; no-op otherwise.
 * Called on mission-ending events. */
void user_checkreplaycamera(void);

/* Swap to the eject-pod / fly-by camera immediately after the player
 * craft is destroyed. */
void user_ejectcamera(void);

/* --- Info/options room ------------------------------------------- */

/* Push the full-screen mid-mission info room as a tie_core task.
 * Seven tabs (goals/map/messages/damage/wingmen/help/options) reachable
 * via Up/Down navigation; ESC/Q/q dismisses. The task drives replay
 * serialize/deserialize of the selected state and writes the final
 * screen index (or 0xFFFF on retreat-cancel) into user_submodal_result
 * before popping, so its parent task (the flight task / tie_simulator
 * AFTER_MISSION phase) can consume the outcome. */
void user_Push_InflightInfo_Task(int32_t screen_id);

/* Sub-modal result handoff — single int32 channel shared by every leaf
 * sub-modal task (msgroom / goals / maproom / damage / wingman / help /
 * option / inflightinfo). The producer writes its return code on the
 * tick it returns LANDRU_TASK_STEP_DONE; the consumer (the parent task)
 * reads it once in its post-sub-modal phase. The values mirror the
 * legacy synchronous returns:
 *   -1 / 0xFFFF : navigate to previous tab
 *    0          : exit info room
 *   +1          : navigate to next tab
 *   other       : sub-modal-specific (e.g. option's exit codes) */
extern int32_t user_submodal_result;

/* Info-room request channel. user_userinterface keybinds set this to
 * the requested screen index (0..6); the flight task step picks it up
 * AFTER tie_doframe returns, pushes user_Push_InflightInfo_Task, and
 * resets to -1. -1 means "no request". This decouples the synchronous
 * keybind handler from the asynchronous task push. */
int32_t user_consume_info_room_request(void);

/* Replay-viewer request channel. The 'v' key handler in
 * user_userinterface sets a pending flag after running the immediate
 * pre-empt bookkeeping (spool flush, blank, recording stop). The
 * flight task step picks it up AFTER tie_doframe returns, pushes
 * replayio_Push_ReplayScreen_Task, and clears the flag. The RESUMED
 * banner is posted by the flight task once the viewer task pops.
 * Returns 1 if a request was pending (and consumes it), 0 otherwise. */
int user_consume_replay_viewer_request(void);

/* --- Miscellaneous ------------------------------------------------ */

/* Map a CraftData.warhead_type byte into the argtable[N] substitution
 * id used by the 'out of X' status banner. Unknown warhead types
 * return default_msg unchanged. */
int32_t user_mapmissiletomessage(uint8_t warhead_type, int32_t default_msg);

/* Filter out hidden/helper meshes when advancing radar_target1 past a
 * destroyed component. */
int16_t user_validcomponent(uint16_t comp_idx);
int16_t user_validcomponent_tie98(uint16_t model_type, uint16_t mesh_index);

/* --- Module data tables (defined in user.c) ---------------------- */

/* 69-byte AI-order -> display-message-index table. Loaded from .DAT
 * by fediskio_loadtext. */
extern uint8_t convertmessage[69];

/* Key -> pilotview-index translation tables. 10- and 20-byte lookup
 * tables driven by the view keys inside user_inputforplane. */
extern uint8_t viewtranslate[10];
extern uint8_t looktranslate[20];

/* Detail-level LOD tables (indexed 0..3 by user_setdetaillevel). */
extern const uint16_t starshipexplodtl[4];
extern const uint16_t starshipdtl[4];
extern const uint16_t stardtl[4];
extern const uint16_t backdtl[4];
extern const uint16_t debrisdtl[4];
extern const int16_t polydtl[4];
extern const uint16_t numpolydtl[4];
extern const uint16_t markdtl[4];
extern const uint16_t hyperdtl[4];
extern const uint8_t gourauddtl[6];

/* Per-frame scratch written by user_targetincross: Manhattan screen
 * distance of the last-tested target (or 0xFFFF if rough-reject). */
extern uint16_t screendist;

/* Sound / music volume bitflag latches. Serialized into the replay
 * tape by user_inflightinfo so re-watch matches the original audio
 * mix. */
extern uint8_t soundvolflag;
extern uint8_t musicvolflag;

/* AI-order -> display-message-index lookup table. Indexed by craftptr
 * order code; result indexes messagetable[]. */
extern uint8_t convertmessage[69];

#endif /* __USER_H__ */
