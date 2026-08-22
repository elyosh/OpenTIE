/* Flight-engine globals and per-frame driver. */

#include "tie/tie.h"
#include "anim.h"
#include "tie/backdrp2.h"
#include "tie/cdaudio_tie98.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/dynamix.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_composite_tie98.h"
#include "tie/flight_surface_tie98.h"
#include "tie/fmusic.h"
#include "tie/frontend_display_tie98.h"
#include "tie/frontend_sound_tie98.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/gamesnd.h"
#include "tie/gate.h"
#include "tie/laser.h"
#include "tie/logbuf2.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/move.h"
#include "tie/msg.h"
#include "tie/msgroom.h"
#include "tie/option.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/rand.h"
#include "tie/render_scene_tie98.h"
#include "tie/render_texture_tie98.h"
#include "tie/replay.h"
#include "tie/replayio.h"
#include "tie/rotscale.h"
#include "tie/rtsvga2.h"
#include "tie/score.h"
#include "tie/spec.h"
#include "tie/species.h" /* hyperstardata — for hyperspace emit */
#include "tie/starship.h"
#include "tie/static.h"
#include "tie/tie_render_tie98.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h"
#include "tie/xtimer.h"
#include "tie/xtrans2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/display/tie98_renderer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_billboards.h" /* SNAPSHOT-ONLY billboard capture drain */
#include "tie_runtime/snapshot/snapshot_flight.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/ai_lead.h"
#include "tie_runtime/timing/chase_camera.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"
#include "tie_runtime/timing/replay_timing.h"
#include "tie_runtime/timing/sim_clock.h" /* TieSimClock_NowUs — loading-screen minimum hold */
#include "util/binio.h"
#include <imuse/filelist.h>
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <landru/error.h>
#include <landru/task.h>
#include <landru/vesa.h>

/* --- Struct arrays --- */

/* species_table owned by species.c now (it has the static initializer
 * extracted from the binary's _species table). */
CraftData crafts[NUM_CRAFTS];
// GLOBAL: TIE 0xE38BC
FlightObject objects[NUM_OBJECTS];
// GLOBAL: TIE 0xEA6D6
StaticObject staticobjects[NUM_STATIC_OBJECTS];
// GLOBAL: TIE 0xEB70C
uint16_t framerate = 20; /* populated by the frame-pacer; fallback value avoids divide-by-zero before init */
// GLOBAL: TIE 0xEB712
uint16_t frameticks;

/* Rendering pipeline scratch (set by pai/fview each frame). */
// GLOBAL: TIE 0xEB0E0
// GLOBAL: TIE 0xEB0E4
// GLOBAL: TIE 0xEB0E8
int32_t rotatedx, rotatedy, rotatedz;
// GLOBAL: TIE 0xEAC34
// GLOBAL: TIE 0xEAC38
// GLOBAL: TIE 0xEAC3C
int32_t craftmoveX, craftmoveY, craftmoveZ;

/* spec_data[] lives in spec.c (its watdbg owning module). */
// GLOBAL: TIE 0xEB29C
RUNTIME_MissionState mission;

int16_t fileerror;

/* --- Display --- */

// GLOBAL: TIE 0xEB148, TIE98 0x59190C
int32_t screenXRes;
// GLOBAL: TIE 0xEB14C, TIE98 0x591E34
int32_t screenYRes;
/* Required before the first same-mode framebuffer allocation. */
// GLOBAL: TIE 0xCD17C
int32_t bytesPerPixel = 1;
// GLOBAL: TIE 0xCD180
int16_t flightResolution;

/* The linear framebuffer disables bank switching with an unbounded page. */
// GLOBAL: TIE 0xCD170
uint32_t vesa_page_size = 0xFFFFFFFFu;
// GLOBAL: TIE 0xCD178
uint8_t vesa_window;
// GLOBAL: TIE 0xEB150
int32_t screenMemWidth;

/* --- Ship-render context (set by DRAW_Lockshipfileptrs). watdbg owner: tie.c. --- */
struct ShipModelData* shipimageptr;
// GLOBAL: TIE 0xEB27C
struct ShipModelData* objectblockptr;
// GLOBAL: TIE 0xEB280
struct ShipModelMesh* componentblockptr;
// GLOBAL: TIE 0xEB28C
CraftData* craftptr;
// GLOBAL: TIE 0xEB730
int16_t shipdetailvalue;
// GLOBAL: TIE 0xEB734
uint16_t shipdetailpolycnt;

/* --- World-to-eye rotation matrices. watdbg owner: tie.c. --- */
// GLOBAL: TIE 0xEAC0C
// GLOBAL: TIE 0xEAC10
// GLOBAL: TIE 0xEAC14
int32_t rotworldeyeA1, rotworldeyeA2, rotworldeyeA3;
// GLOBAL: TIE 0xEABC8
// GLOBAL: TIE 0xEABCC
// GLOBAL: TIE 0xEAC18
int32_t rotworldeyeB1, rotworldeyeB2, rotworldeyeB3;
// GLOBAL: TIE 0xEABB8
// GLOBAL: TIE 0xEABBC
// GLOBAL: TIE 0xEABC4
int32_t rotworldeyeC1, rotworldeyeC2, rotworldeyeC3;

/* Perspective-projection constants (set by TIE_InitFlightResolution per
 * selected flight resolution). */
// GLOBAL: TIE 0xEB771
uint8_t perspShift;
// GLOBAL: TIE 0xEB13C
int32_t perspFactor;
// GLOBAL: TIE 0xEB140
int32_t halfPerspFactor;

/* Master enable for the skybox backdrop renderer. */
// GLOBAL: TIE 0xEB775
uint8_t drawbackdropflag;

/* --- calc-frame rotation rows. tie.c. --- */
// GLOBAL: TIE 0xEAC1C
// GLOBAL: TIE 0xEAC20
// GLOBAL: TIE 0xEAC24
int32_t calcS1, calcS2, calcS3;
// GLOBAL: TIE 0xEABF4
// GLOBAL: TIE 0xEABF8
// GLOBAL: TIE 0xEABFC
int32_t calcf1, calcf2, calcf3;
// GLOBAL: TIE 0xEABDC
// GLOBAL: TIE 0xEABE0
// GLOBAL: TIE 0xEABE4
int32_t calcU1, calcU2, calcU3;

/* --- Current-craft orientation rows. tie.c. --- */
// GLOBAL: TIE 0xEAC00
// GLOBAL: TIE 0xEAC04
// GLOBAL: TIE 0xEAC08
int32_t craftS1, craftS2, craftS3;
// GLOBAL: TIE 0xEAC28
// GLOBAL: TIE 0xEAC2C
// GLOBAL: TIE 0xEAC30
int32_t craftf1, craftf2, craftf3;
// GLOBAL: TIE 0xEABE8
// GLOBAL: TIE 0xEABEC
// GLOBAL: TIE 0xEABF0
int32_t craftU1, craftU2, craftU3;

/* --- World/camera state. The 408-byte _camera struct from the binary
 * (see Camera typedef in tie.h) is owned here. replaycam lives in
 * replay.c. --- */
// GLOBAL: TIE 0xE2D8C
Camera camera;

// GLOBAL: TIE 0xEB0C4
// GLOBAL: TIE 0xEB0C8
// GLOBAL: TIE 0xEB0D0
int32_t worldlocx, worldlocy, worldlocz;
// GLOBAL: TIE 0xEAC40
// GLOBAL: TIE 0xEAC44
// GLOBAL: TIE 0xEAC48
int32_t worldx, worldy, worldz; /* watdbg-owned by tie.c */
// GLOBAL: TIE 0xEB72C
uint16_t yAspect; /* watdbg-owned by tie.c; 0 = square pixels */
int16_t objectsize;
uint8_t gouraudflag;
// GLOBAL: TIE 0xEAC54
// GLOBAL: TIE 0xEAC58
// GLOBAL: TIE 0xEAC5C
int32_t objecteyex, objecteyey, objecteyez;

/* --- Swept-segment globals for collision pipeline. tie.c. --- */
// GLOBAL: TIE 0xEAB90
// GLOBAL: TIE 0xEAB94
// GLOBAL: TIE 0xEAB98
int32_t laserx, lasery, laserz;
// GLOBAL: TIE 0xEABD0
// GLOBAL: TIE 0xEABD4
// GLOBAL: TIE 0xEABD8
int32_t laserxold, laseryold, laserzold;
// GLOBAL: TIE 0xEAB80
// GLOBAL: TIE 0xEABA8
// GLOBAL: TIE 0xEABAC
int32_t craftx, crafty, craftz;
// GLOBAL: TIE 0xEABB0
// GLOBAL: TIE 0xEABB4
// GLOBAL: TIE 0xEABC0
int32_t craftxold, craftyold, craftzold;
int32_t gatex1, gatey1, gatez1;
int32_t gatex2, gatey2, gatez2;
int32_t gatenx, gateny, gatenz;
// GLOBAL: TIE 0xEAB74
// GLOBAL: TIE 0xEAB78
// GLOBAL: TIE 0xEAB7C
int32_t collidexoff, collideyoff, collidezoff;

/* --- Targeting / damage state. tie.c. --- */
// GLOBAL: TIE 0xEB724
uint16_t bluetarget;
// GLOBAL: TIE 0xEB71E
uint16_t currenttarget;
// GLOBAL: TIE 0xEB718
uint16_t currenttargetcomp;
uint8_t drawmarkingsflag;

/* --- Sound/input flags --- */

// GLOBAL: TIE 0xEB769
uint8_t musicenabled;
// GLOBAL: TIE 0xEB76D
uint8_t voiceenabled;
// GLOBAL: TIE 0xEB773
uint8_t sfxenabled;

/* --- Flight engine state --- */

// GLOBAL: TIE 0xEB763, TIE98 0x596218
uint16_t maingameflag;
uint8_t cheatingflag;

/* --- Mission data --- */

FGStatus fgstatus[48];
EFGStruct fg_array[48];

/* Authoritative player state serialized by replay slot 8. Its native
 * pointers make host replay files pointer-width dependent. */
PlayerInFlightState pstate;
_Static_assert(sizeof(pstate) == 294 + 2 * (sizeof(void*) - 4), "PlayerInFlightState layout mismatch");

MissionFile mission_file_header;

uint16_t idnumber;                              /* monotonic per-craft id */
uint16_t currentdebrisslot = DEBRIS_FIRST_SLOT; /* cycled 112..119 (retail) by checkdebris */
uint16_t missionversion;                        /* .TIE file version (0 = legacy) */
uint16_t baseframerate = 20;                    /* mission base framerate (seeded by xtimer) */
// GLOBAL: TIE 0xEB71C
uint16_t tickcounter;
uint16_t targetblinkstate;
int16_t targetblinkflag;
uint16_t fullupdateflag;
// GLOBAL: TIE 0xEB75D
uint8_t hyperspaceflag;
uint8_t hyperabortflag;

/* Hyperspace timing + state -- driven by anim_dohyperspace.
 *   hyperticks       -- cumulative frameticks since the warp started
 *   hyperstarlength  -- scratch used during the streak stretch/shrink math
 *   hypertemp1/2     -- saved drawbackdropflag / drawdebrisflag (restored
 *                       at the end of phase 5 so the post-warp scene comes
 *                       back with the same backdrop config) */
uint16_t hyperticks;
uint16_t hyperstarlength;
uint16_t hypertemp1;
uint16_t hypertemp2;

/* drawdebrisflag -- enables the parallax-debris layer in BACKDRP2/CREATE.
 * Cleared to 0 during the hyperspace warp; restored from hypertemp2 in
 * phase 5. Owned by tie.c per watdbg. */
// GLOBAL: TIE 0xEB776
uint8_t drawdebrisflag;

/* timers[20] -- the global cooldown bank. tie_updatetime decrements every
 * non-zero slot by frameticks and clamps to zero. Slots are named via the
 * TimerSlot enum in tie.h; consumers reset their slot to a tick count
 * (e.g. timers[TIMER_ANIM_UPDATE] = 29). */
// GLOBAL: TIE 0xEB75C
uint8_t calcframerate;
// GLOBAL: TIE 0xEB75E
uint8_t entercombatflag;
/* Set by user_ejectcamera; gates beam/laser firing in laser_weaponsfire and
 * suppresses normal HUD/input updates after the player ejects. Cleared at
 * mission init. Mirrors retail byte_EB163. */
uint8_t player_ejected;
int16_t timers[20];

/* REPLAY module globals -- watdbg-owned by tie.c. PANEL_updatereplaystuff
 * consumes recordingreplay / replaypercent / replaytotalcnt / replaymaxcnt
 * for the cockpit REC LED + %-remaining readout. replay.c / replayio.c
 * consume the rest. */
// GLOBAL: TIE 0xEB6A6
int16_t replaypercent;
// GLOBAL: TIE 0xEB6CC
int16_t recordingreplay;
// GLOBAL: TIE 0xEAC50
int32_t replaytotalcnt;
// GLOBAL: TIE 0xEAC60
int32_t replaymaxcnt;
char replayclipname[14];
char replaystartfile[10] = "start.rpy";
char replaysavegamefile[13] = "savegame.rpy";
char inputspoolfile[10] = "input.spl";
// GLOBAL: TIE 0xEAC68
void* replayptr; /* write/read cursor into replaybuffer. */
// GLOBAL: TIE 0xEB6AA
uint16_t replaybuffercnt; /* frames in the current 3071-slot page. */
int16_t replaybuffercntdown;
// GLOBAL: TIE 0xEAC4C
uint32_t replaytotalcntdown; /* playback counter (counts up toward replaytotalcnt). */
// GLOBAL: TIE 0xEB6AC
uint16_t replayrandomseed;
int16_t replayviewtype;
int16_t lastreplayviewtype;
int16_t replayobjectnum;
int16_t replaydebounce;
// GLOBAL: TIE 0xEB6C4
uint8_t replayviewmode;
// GLOBAL: TIE 0xEB74C
uint8_t replayspoolflag; /* 1 = auto-spool to disk when buffer fills. */
uint8_t endgamereplayflag;
uint8_t replayescapeflag;
uint8_t replayfpctr;
uint8_t lastreplayname;
uint8_t replayfg;
// GLOBAL: TIE 0xEB751
uint8_t updateactionflag; /* 1 = replay is actively advancing */
uint16_t replayavailable; /* 1 if a saved film is loadable. */

/* --- Mission-file flag: bit 0 forces eject-pod rescue (story gate). --- */
uint8_t rescue_override_flag;

/* lasttargetnum is owned by panel.c per watdbg; declared extern in panel.h. */

/* Binary is u8 in both demo and retail. Readers only test for nonzero
 * and decrement, so the narrower type matches. */
// GLOBAL: TIE 0xEB770
uint8_t acceleratedtimectr;
// GLOBAL: TIE 0xEB736
int16_t hyperspacedetail;

/* --- View-angle lookup table used by the 0..9 numpad view keys. Populated
 * at startup from a trig-derived formula; 512 entries in the binary. --- */
// GLOBAL: TIE 0xCDB08
int16_t squarerootable[512];

/* Shield LED flash toggle (set by COLLIDE_damagecraft on hit; read by
 * PANEL_updateshields). 0 = fwd flash, 1 = rear flash. Owned by tie.c.
 * Single byte in the binary (byte_EB75B); paired with timers[TIMER_SHIELD_FLASH]. */
uint8_t shieldblink;

/* MissionClock — the single 8-byte wall-clock storage. See the typedef
 * comment in tie.h for the layout and tick semantics. */
MissionClock _date;

/* 8-byte "time left" strip at watdbg _timeleft[8] (owned by tie.c).
 * Captured wholesale by the replay state-dump; individual bytes index
 * into mission-timer display state. */
uint8_t timeleft[8];

/* TieStorage_Open(3) modes embedded in the binary as const char arrays; owned by tie.c
 * per watdbg. C stdlib fopen treats the first two chars the same way here. */
const char _readmode[3] = { 'r', 'b', '\0' };
const char _writemode[3] = { 'w', 'b', '\0' };
const char _appendmode[3] = { 'a', 'b', '\0' };

/* Rendering scratch (owned by tie.c per watdbg). maxPixelsDeep is set by
 * tie_initflightresolution per video mode; numbitmaps and lightflag are
 * written each frame by the 3D pipeline (anim.c / draw.c / xtrans2.c). */
// GLOBAL: TIE 0xEB154
int32_t maxPixelsDeep;
int16_t numbitmaps;
// GLOBAL: TIE 0xEB74A
uint8_t lightflag;

/* Squared distance scratch written by pai_roughdistancebetween and
 * consumed by PANEL_addbliptoradar for the distance-fade color step.
 * Owned by tie.c per watdbg. */
// GLOBAL: TIE 0xEB0EC
int32_t roughdistance;
uint16_t messageside; /* 0xF955A -- sampled by MSG_*message writers */
// GLOBAL: TIE 0xDED54
uint16_t argtable[4];      /* 0xED560 -- '*' and '&N' substitution slots */
uint16_t messageloghandle; /* 0xF94F8 -- unused handle-shaped state */
/* (pstate.space_confirm_action: 1=laser-warn ack, 2=abort mission,
 * 3=accept penalty; driven by timers[TIMER_SPACE_CONFIRM] decay in
 * msg_messageupdate.) */
uint8_t mtimer_state, mtimer_min, mtimer_sec;
uint8_t mfile_time_min, mfile_time_sec;
int16_t mfile_rnd_seed;
// GLOBAL: TIE 0xE2F24
uint8_t radiomsg[1440];
// GLOBAL: TIE 0xE34C4
EMissionGoal cut[4];

/* Cockpit instrument knockout flag set by panel_updatecockpitdamage
 * (byte 0xF8FAB). */
uint8_t byte_F8FAB;

/* Per-subsystem damage state inside _player.
 *   player_system_damage_hash  -- xor-of-craftptr-bits 'invalidate' marker
 *                                 (the misnamed _rank_pilot_score in IDA)
 *   player_system_repair_timer -- countdown ticks until the system comes
 *                                 back online (the misnamed _rank_pilot_kills) */
uint16_t player_system_damage_hash[10];
uint16_t player_system_repair_timer[10];

/* Approximate distance scratch -- last collide_roughdistance3d result.
 * Owned by tie.c per watdbg (segment 2 offset 0x2899C). */
int32_t approxdist;

/* (mission elapsed hr/min/sec previously lived as standalone globals here;
 * they are now `_date.hour` / `_date.minute` / `_date.second`, fields of
 * the single MissionClock storage above.) */

/* (pstate.target_obj_idx: currently-targeted object slot; written by
 * USER_inputforplane and PANEL_*, read by collide_collisions for the
 * friendly-tag check.) */

/* --- Input state (read/written by FEINPUT, consumed by screen modules) --- */

// GLOBAL: TIE 0xEB6DE
int16_t inputbuttons;
// GLOBAL: TIE 0xEB6E8
int16_t inputkey;
// GLOBAL: TIE 0xEB6D2
int16_t inputdeltax;
// GLOBAL: TIE 0xEB6CE
int16_t inputdeltay;
int16_t inputdeltaroll;
// GLOBAL: TIE 0xEB6DC
int16_t mouseflag;
// GLOBAL: TIE 0xEB708
int16_t joystickflag;
// GLOBAL: TIE 0xEB6F8
int16_t joystickx;
// GLOBAL: TIE 0xEB6F4
int16_t joysticky;
int16_t joystickroll;
int16_t joystickthrottle;
// GLOBAL: TIE 0xEB6FC
int16_t joybuttons;
// GLOBAL: TIE 0xEB6D8
int16_t mousebuttons;
// GLOBAL: TIE 0xEB6FA
int16_t keypress;
// GLOBAL: TIE 0xEB6E2
int16_t deltamx;
// GLOBAL: TIE 0xEB6E0
int16_t deltamy;
int16_t mousex;
int16_t mousey;
int16_t joystickcount;
// GLOBAL: TIE 0xEB772
uint8_t graphicsmode;
// GLOBAL: TIE 0xCD16C
int16_t detaillevel;

void* viewfilmstr;

/* --- FESTRING text output globals --- */

// GLOBAL: TIE 0xEB702, TIE98 0x5A26D8
int16_t cursorx;
// GLOBAL: TIE 0xEB6FE, TIE98 0x5A26D0
int16_t cursory;
// GLOBAL: TIE 0xEB704, TIE98 0x5A26EA
int16_t topmargin;
// GLOBAL: TIE 0xEB706, TIE98 0x5A26CC
int16_t bottommargin;
// GLOBAL: TIE 0xEB70A, TIE98 0x5918E0
int16_t leftmargin;
// GLOBAL: TIE 0xEB700, TIE98 0x5926D0
int16_t rightmargin;
// GLOBAL: TIE 0xEB754, TIE98 0x591D8A
uint8_t textcolor;
// GLOBAL: TIE 0xEB756, TIE98 0x5A26CF
uint8_t backcolor;
// GLOBAL: TIE 0xEB757, TIE98 0x595DE2
uint8_t dropcolor;
// GLOBAL: TIE 0xEB758, TIE98 0x595F5F
uint8_t dropflag;
// GLOBAL: TIE 0xEB6F2, TIE98 0x592206
int16_t lwrapflag;
// GLOBAL: TIE 0xEB6F0, TIE98 0x596BA4
int16_t autofillflag;
// GLOBAL: TIE 0xEB6EE, TIE98 0x5A269C
int16_t flight_text_reserved_flag;
// GLOBAL: TIE 0xEB75A, TIE98 0x5926A0
uint8_t fontflag;
// GLOBAL: TIE 0xEB755, TIE98 0x590E6D
uint8_t fontheight;
// GLOBAL: TIE 0xEB6F6
int16_t fontcharsize;
// GLOBAL: TIE 0xEB759
uint8_t fontlowercase;
// GLOBAL: TIE 0xEAC70
void* curfontptr;
// GLOBAL: TIE 0xDED7C
char tempstring[40];
char temp2string[40];

/* Graphics function pointers (assigned by FEINPUT_SetGraphicsPtrs) */
void* initgraph;
// GLOBAL: TIE 0xEB0FC
void (*blank)(void);
// GLOBAL: TIE 0xEB0F0
void (*unblank)(void);
// GLOBAL: TIE 0xEB0CC
void (*buildpalette)(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count);
void* savepalette;
void* restorepalette;
uint32_t (*calcposition)(uint16_t, uint16_t);
void (*drawshape)(const void*, int16_t, int16_t, int16_t, uint16_t);
// GLOBAL: TIE 0xEB0F4
void (*outchar)(int ch);
// GLOBAL: TIE 0xEB100, TIE98 0x59222C
void (*clearwindow)(void);
void (*fillbox)(uint16_t, uint16_t, uint16_t, uint16_t);
void* savebox;
void* restorebox;

/* Color remap table. Indexed by FESTRING_set{text,back,drop}color for any
 * color >= 0x40 -- the logical HUD palette IDs used by panel.c / msg.c /
 * festring etc. are translated through this table to the actual physical
 * palette indices loaded by buildpalette.
 *
 * The retail Z_TIE__.EXE ships this as initialised data at 0xC5810. Copy
 * the exact 256 bytes here; entries 0x00..0x3F and 0x74.. overlap other
 * globals in the retail data segment and are never indexed by festring
 * (which gates on `>= 0x40`) -- we still preserve the retail values so
 * anyone else who happened to read them sees the same bytes. Without
 * this table the HUD clock, shield readouts, CMD target strings, radar
 * labels, and message log all rendered in palette index 0 (black) on a
 * black background, producing the "empty cockpit" look.
 */
// GLOBAL: TIE 0xC5810
uint8_t color_remap_table[256] = {
	/* 0x00 */ 0xC8,
	0x00,
	0x2C,
	0x01,
	0x90,
	0x01,
	0xF4,
	0x01,
	0x00,
	0x00,
	0x19,
	0x00,
	0x32,
	0x00,
	0x64,
	0x00,
	/* 0x10 */ 0x96,
	0x00,
	0xC8,
	0x00,
	0xFA,
	0x00,
	0x00,
	0x00,
	0x64,
	0x00,
	0xC8,
	0x00,
	0x90,
	0x01,
	0x58,
	0x02,
	/* 0x20 */ 0x20,
	0x03,
	0xE8,
	0x03,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	/* 0x30 */ 0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	/* 0x40 */ 0x2C,
	0x2D,
	0x2E,
	0x2F,
	0x30,
	0x31,
	0x32,
	0x33,
	0x34,
	0x35,
	0x36,
	0x37,
	0x38,
	0x39,
	0x3A,
	0x3B,
	/* 0x50 */ 0x3C,
	0x3D,
	0x3E,
	0x3F,
	0xD5,
	0xD5,
	0xD4,
	0xD3,
	0x2C,
	0x2D,
	0x2E,
	0x2F,
	0x2C,
	0x2D,
	0x2E,
	0x2F,
	/* 0x60 */ 0x42,
	0x4A,
	0x46,
	0x4E,
	0x52,
	0x45,
	0x42,
	0x52,
	0x4A,
	0x52,
	0x46,
	0x56,
	0x4A,
	0x56,
	0x52,
	0x4A,
	/* 0x70 */ 0x46,
	0x56,
	0x4A,
	0x56,
	0x00,
	0x00,
	0x00,
	0x00,
	0xFF,
	0xFF,
	0x00,
	0x00,
	0x50,
	0x50,
	0x50,
	0x50,
	/* 0x80 */ 0x48,
	0x48,
	0x48,
	0x48,
	0x50,
	0x50,
	0x50,
	0x48,
	0x48,
	0x48,
	0x6F,
	0x70,
	0x74,
	0x69,
	0x6F,
	0x6E,
	/* 0x90 */ 0x73,
	0x2E,
	0x63,
	0x66,
	0x67,
	0x00,
	0x00,
	0x00,
	0x00,
	0x60,
	0x00,
	0x00,
	0x00,
	0x80,
	0x00,
	0x00,
	/* 0xA0 */ 0x00,
	0xA0,
	0x00,
	0x00,
	0x03,
	0x04,
	0x05,
	0x00,
	0x09,
	0x00,
	0x06,
	0x00,
	0x03,
	0x00,
	0x00,
	0xF4,
	/* 0xB0 */ 0x00,
	0x00,
	0x00,
	0x0C,
	0x00,
	0xF4,
	0x00,
	0x00,
	0x00,
	0x0C,
	0x00,
	0xF4,
	0x00,
	0x00,
	0x00,
	0x0C,
	/* 0xC0 */ 0x00,
	0xF4,
	0x00,
	0x00,
	0x00,
	0x0C,
	0x00,
	0xF4,
	0x00,
	0x00,
	0x00,
	0x0C,
	0x00,
	0xF4,
	0x00,
	0x00,
	/* 0xD0 */ 0x00,
	0x0C,
	0x00,
	0xF4,
	0x00,
	0x00,
	0x00,
	0x0C,
	0x00,
	0xF4,
	0x00,
	0x00,
	0x00,
	0x0C,
	0x00,
	0xF4,
	/* 0xE0 */ 0x00,
	0x00,
	0x00,
	0x0C,
	0x00,
	0x0C,
	0x00,
	0x0C,
	0x00,
	0x0C,
	0x00,
	0x0C,
	0x00,
	0x0C,
	0x00,
	0x0C,
	/* 0xF0 */ 0x00,
	0x0C,
	0x00,
	0x0C,
	0x00,
	0x0C,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
};

/* --- Buffer pointers --- */

// GLOBAL: TIE 0xEAC74
uint8_t* farbufferptr;
/* Shape pointer table shared with maproom_swap_buffer_ptrs. */
uint8_t* farbufferptrs[265];
// GLOBAL: TIE 0xEB0A4
void* fontptrtiny;
// GLOBAL: TIE 0xEB0AC
void* fontptrmicro;
// GLOBAL: TIE 0xEAC7C, TIE98 0x591E48
void* newbuf;
// GLOBAL: TIE 0xEAC64
void* xtransdataptr;
// GLOBAL: TIE 0xEB0A8
void* loadbuffer;
// GLOBAL: TIE 0xEAC6C
void* replaybufferstart;

/* --- Starfield source data (watdbg owner: tie.c). ---
 * stars[] packs 256 (index, palette-delta) pairs at stride 2: even bytes
 * are star indices (0..124 into stareye{x,y,z}), odd bytes are the
 * color-slot delta added to starcol1 to pick one of palette entries
 * 0xFC..0xFF via the clamp(starcol1 + delta, 0..3) - 4 formula
 * in rtsvga2_drawstars. Initialized once at the start of each flight. */
uint8_t stars[512];
uint8_t starcol1;

/* Palette-cycling state (watdbg owner: tie.c).
 *   colorcycleflag -- 0 while the engine is mid-palette-swap, 1 when
 *                     cycling is active (checked by the cycle timer
 *                     and by blankVGA/unblankVGA).
 *   blankcondition -- bit field. Bit 0 = fade-to-black active. */
// GLOBAL: TIE 0xEB760, TIE98 0x596208
uint8_t colorcycleflag;
// GLOBAL: TIE 0xEB762, TIE98 0x5A2736
uint8_t blankcondition;

/* Accelerated-game-clock gear shift (fediskio persists; xtimer uses it). */
// GLOBAL: TIE 0xCD184
uint8_t acceleratedtimesetting;

/* Bitmap-draw queue populated by anim_add_bitmap_draw, consumed by
 * anim_sort_and_draw_bitmaps. numbitmaps is defined above; the array
 * itself is declared in anim.h. */
BitmapDrawEntry drawitems[ANIM_DRAWITEMS_MAX];

/* In-flight SFX scheduler (watdbg owner: tie.c). blastqueue is a FIFO of
 * pending blast/voice SFX slots processed by fsfx_updatesfx; blastflag is
 * set on enqueue and cleared by the scheduler; blastcount tracks the
 * number of outstanding entries. */
// GLOBAL: TIE 0xEB764
uint8_t blastflag;
// GLOBAL: TIE 0xEB761
uint8_t blastcount;
uint8_t blastqueue[FSFX_BLAST_QUEUE_SIZE];

/* Warhead state table: one record per concurrent missile/torpedo. */
WarheadRecord warheads[NUM_WARHEAD_SLOTS];

/* VESA paging state. */
// GLOBAL: TIE 0xCD174
uint32_t vesa_grains_per_page;

/* Starship LOD / explosion-LOD thresholds (written by user_updateflight). */
uint16_t starshipdetail;
uint16_t starshipexplodetail;

/* Directional-light vector rotated into the current craft's local frame. */
// GLOBAL: TIE 0xEAB9C
// GLOBAL: TIE 0xEABA0
// GLOBAL: TIE 0xEABA4
int32_t rotlightX, rotlightY, rotlightZ;
// GLOBAL: TIE98 0x4F2A70
int32_t g_localLightsEnabled = 1;
// GLOBAL: TIE98 0x4F3C68
int32_t g_explosionLightBase = 256;
// GLOBAL: TIE 0xEB144
int16_t thicknessMultiple;

/* Draw color used for training-gate silhouettes. */
uint16_t gatecolor;

/* .TIE file/path scratch. The binary sizes the buffer at 64 bytes to hold
 * a full DOS directory + filename. */
// GLOBAL: TIE 0xCD185
char missionfilename[64];

/* Front-end vs flight resolution selectors. */
// GLOBAL: TIE 0xCD182
int16_t frontResolution;

/* --- TIE module per-frame engine driver state (defined here because
 * the binary places these in tie.c per watdbg). --- */

/* Map-room visibility latch. Set by USER_userinterface when the player
 * opens the in-flight map; cleared at the top of each TIE_doframe before
 * USER_userinterface runs. The post-userinterface check `!end_flag &&
 * !mapflag` is what suspends the world-render block while the map is up. */
// GLOBAL: TIE 0xEB767
uint8_t mapflag;

/* Replay fast-forward UI state. fastforwardtimer (initialized to 236)
 * counts the ticks left before the next render frame; tie_doframe drains
 * it by frameticks each call. Reloaded with +236 when a render fires. */
// GLOBAL: TIE 0xEB750
uint8_t fastforwardflag;
// GLOBAL: TIE 0xEB6BC
int16_t fastforwardtimer;

/* Hyperspace-cinematic + reload-mission gate. Was the binary's
 * byte_D354C — set non-zero by SHELLEXT_loadprefs when "transitions" is
 * enabled in the player's preferences. tie_simulator only triggers
 * CREATE_createhyperin when this is set. */
// GLOBAL: TIE 0xD354C
uint8_t transitions_on;

/* Retail-only special-features dword (binary's dword_D3548). Gate for
 * the training-filename auto-detect (set train_craft_type_src to 5 when the
 * mission filename starts with 't'). 0 in normal builds. */
uint32_t special_features_flag;

/* (Mission elapsed clock `_date` is defined above as a single
 * MissionClock storage; subsec is `_date.subsec`.) */

/* Snapshot of XTIMER tickcounter taken at the top of tie_doframe (live
 * branch). Used to seed framerate / frameticks for this frame. */
// GLOBAL: TIE 0xEB6A2
int16_t lastcounter;

/* HUD target-blink tick countdown. Decremented by frameticks per call to
 * tie_updatetime; on expiry toggles the targetblinkstate 0x400 bit and
 * picks a new tick interval (118 normal, 14 micro-flicker for too-small
 * targets). Owned by tie.c per watdbg. */
int16_t blinkticks;

/* Engine-init/teardown latches set/cleared at mission boundaries by
 * tie_simulator. Each is a byte in the binary's data segment, owned by
 * tie.c per watdbg. Read by other modules to gate optional subsystems
 * (mute audio if !soundinit, suppress prints if !outputflag, etc.). */
uint8_t musicflag; /* iMUSE script live? */
/* iMUSE per-frame evaluation state (two 16-bit globals in the binary at
 * 0xF9578 / 0xF9582). tie_updatemusic reads/updates them each frame so
 * they persist across evaluations and are captured by the replay-state
 * dump. */
int16_t music_state = 1;
uint16_t music_intensity;
uint8_t debugnum; /* on-screen debug overlay enabled */
// GLOBAL: TIE 0xEB768
uint8_t graphicsinit; /* video mode set + buffers allocated */
uint8_t soundinit;    /* iMUSE init complete */
uint8_t outputflag;   /* msg / festring writes enabled */
// GLOBAL: TIE 0xEB748, TIE98 0x59266C
uint8_t colorcycleuserflag; /* user-side palette-cycling enable (transient) */
// GLOBAL: TIE 0xEB746
uint8_t panelflag; /* cockpit-panel rendering enabled */

/* "Map icons loaded" latch (binary's byte_CD1C5 in retail, byte_DC409 in
 * demo). Cleared by tie_simulator between fediskio_Init_Buffers_and_Fonts
 * and fediskio_readfiletofarmemory so the first map-room open on a fresh
 * mission re-uploads the icon atlas. Other modules (MAP, PANEL) set it
 * after a successful load. Owned by tie.c per watdbg. */
uint8_t mapiconsloaded;

/* User-toggleable "Color Cycling" option (binary's byte_EB766). Persists
 * across missions, edited via OPTION_optionsroom (row 6 in the in-flight
 * options menu). GAMESND_Host_Int gates color cycling on
 *   (palette_cycle_user & colorcycleflag) || colorcycleuserflag
 * so this is the persistent user setting; colorcycleuserflag is the
 * transient runtime toggle. tie_simulator sets it to 1 at startup. */
// GLOBAL: TIE 0xEB766
uint8_t palette_cycle_user;

/* Always zero because the host does not use DOS expanded memory. */
// GLOBAL: TIE 0xD5B0C
int panels_in_ems;

/* Write-only simulator initialization flags. */
// GLOBAL: TIE 0xEB76C
uint8_t deadflag_EB76C;
// GLOBAL: TIE 0xEB774
uint8_t deadflag_EB774;

/* Last iMUSE music state pushed by tie_updatemusic. Latched here so the
 * UI / debug overlays can read what's currently playing. */
uint8_t lastmusicstate;

/* Objective-just-completed grace timers. SCORE_checkobjective sets them
 * to a small non-zero on the frame an objective transitions to "done";
 * tie_updatemusic checks them as a "hold the post-objective music state
 * for a few seconds" gate. Bytes in the binary, not real timers[] slots. */
uint8_t pri_complete_cooldown;
uint8_t sec_complete_cooldown;

/* Forward decls for static helpers. */
static int shield_quartile(uint16_t balance, uint16_t maxv);
static bool render_world_or_skip(int has_panel);
static bool tie_doframe_tie98(void);
static void tie_updatescreen_tie95(void);
static void view_replay_prompt_Push_Task(uint16_t saved_master_vol);

/* Configure flight geometry for VGA or the 640x480 modes and select the
 * corresponding CP320/CP640 cockpit asset directory. */
// FUNCTION: TIE 0x56048, TIE98 0x48D850
void tie_initflightresolution(void) {
	const bool dx5_display = TieClassicDisplay_UsesDx5();
	/* PORT: display ownership and mode selection happen at the simulator,
	 * replay or frontend-preview boundary before this recovered geometry setup. */
	if (flightResolution == TIE_FLIGHT_RES_SVGA_16 || flightResolution == TIE_FLIGHT_RES_SVGA_D3D)
		bytesPerPixel = 2;
	else
		bytesPerPixel = 1;

	/* Disable VESA bank wrapping for the host's linear framebuffer. */
	{
		uint16_t* modeinfo = (uint16_t*)lvesa_Get_Vesa_Mode_Struct();
		(void)modeinfo;
		vesa_page_size = 0xFFFFFFFFu;
		vesa_grains_per_page = 1u;
	}

	if ((uint16_t)flightResolution == TIE_FLIGHT_RES_VGA) {
		/* 320x200 VGA path. Linear framebuffer -- vesa_page_size stays
		 * unbounded (see rationale above). */
		vesa_grains_per_page = 1u;
		screenXRes = 320;
		screenYRes = 200;
		maxPixelsDeep = 189;
		screenMemWidth = 320;
		perspFactor = 256;
		thicknessMultiple = 1;
		halfPerspFactor = 128;
		perspShift = 8;
		yAspect = (uint16_t)-5958; /* 0xE8BA — non-square pixel correction */
		cockpitdir[2] = '3';       /* 0x33 */
		cockpitdir[3] = '2';       /* 0x32  -> "CP32" + "0\" */
		return;
	}

	if (tie_is_high_resolution_flight()) {
		/* TIE95 mode 0x101 and the three TIE98 640x480 modes share geometry. */
		screenXRes = 640;
		screenYRes = 480;
		maxPixelsDeep = 455; /* RETAIL: was 454 in demo */
		/* Retail CRTC reg 0x13 = 0x80 widens the logical scan line to
		 * 1024 bytes so VESA window-banking can hand out aligned pages.
		 * A linear host framebuffer has no such constraint -- we use a
		 * tight 640-byte stride (matching vesa_bpsl_gbl) so that rtsvga2
		 * writes via vgapointer/lineaddressVGA stay inside vesa_buff_gbl.
		 * Leaving screenMemWidth at 1024 would overrun the 640x480 buffer
		 * by ~180 KB/frame and clobber whatever follows it on the heap. */
		rtsvga2_setvesascanlinelength(0x400u); /* retained for parity */
		if (dx5_display)
			screenMemWidth = (int32_t)g_surfacePitch;
		else
			screenMemWidth = 640;
		perspFactor = 512;
		halfPerspFactor = 256;
		thicknessMultiple = 2;
		cockpitdir[2] = '6'; /* 0x36 */
		perspShift = 9;
		yAspect = 0;
		cockpitdir[3] = '4'; /* 0x34  -> "CP64" + "0\" */
		return;
	}

	/* Default: same as 0x13 (some other mode somehow selected). RETAIL
	 * also re-asserts vesa_page_size in this branch. */
	vesa_page_size = 0x10000u;
	vesa_grains_per_page = 1u;
	screenXRes = 320;
	screenYRes = 200;
	maxPixelsDeep = 189;
	screenMemWidth = 320;
	perspFactor = 256;
	thicknessMultiple = 1;
	halfPerspFactor = 128;
	perspShift = 8;
	cockpitdir[2] = '3';
	yAspect = (uint16_t)-5958;
	cockpitdir[3] = '2';
}

/* ----------------------------------------------------------------------------
 * tie_getobjecteyexyz                                            retail 0x57518
 * ----------------------------------------------------------------------------
 * Cache the camera-relative + rotated eye-space coords of objects[obj_idx]
 * into the globals (worldx/y/z, objecteyex/y/z) AND into the craft's
 * eye_{x,y,z}_cache slots. Identical to the demo version (byte-for-byte
 * match after absolute-address normalization). */
// FUNCTION: TIE 0x57518
void tie_getobjecteyexyz(uint16_t obj_idx) {
	FlightObject* obj = &objects[obj_idx];

	craftptr = obj->craft_ptr;
	worldx = obj->world_x - camera.x;
	worldy = obj->world_y - camera.y;
	worldz = obj->world_z - camera.z;

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	craftptr->eye_x_cache = objecteyex;

	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	craftptr->eye_y_cache = objecteyey;

	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	craftptr->eye_z_cache = objecteyez;
}

/* ----------------------------------------------------------------------------
 * tie_checkobjecteyexyz                                          retail 0x575E4
 * ----------------------------------------------------------------------------
 * Eye-space cull test for objects[obj_idx] within a +/-bound box.
 * Returns 1 when visible, 0 when culled. Side-effect: the worldx/y/z and
 * objecteyex/y/z globals are written even when culled (so the caller can
 * still read them after a "not visible" return).
 *
 * Cull conditions:
 *   eye_z + bound          >= 0           (in front of the camera)
 *   (eye_z + bound) >> 8   <= bound       (perspective near-depth limit;
 *                                          ~256*bound max range)
 *   |eye_x| - bound        <= eye_z+bound (within view cone slope 1)
 *   |eye_y| - bound        <= eye_z+bound (within view cone slope 1)
 *
 * Identical to demo. */
// FUNCTION: TIE 0x575E4
int16_t tie_checkobjecteyexyz(uint16_t obj_idx, uint16_t bound) {
	FlightObject* obj = &objects[obj_idx];
	int near_far_extent;
	int abs_x, abs_y;

	worldx = obj->world_x - camera.x;
	worldy = obj->world_y - camera.y;
	worldz = obj->world_z - camera.z;

	/* Compute eye_z first because the cheapest reject is the depth-cone. */
	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	near_far_extent = (int)objecteyez + (int)bound;
	if (near_far_extent < 0)
		return 0; /* fully behind camera.x */
	if ((near_far_extent >> 8) > (int)bound)
		return 0; /* past the depth limit */

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	abs_x = (objecteyex < 0) ? -objecteyex : objecteyex;
	if (abs_x - (int)bound > near_far_extent)
		return 0; /* outside left/right cone */

	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	abs_y = (objecteyey < 0) ? -objecteyey : objecteyey;
	return (abs_y - (int)bound <= near_far_extent) ? 1 : 0;
}

/* ----------------------------------------------------------------------------
 * tie_checkstaticobjecteyexyz                                    retail 0x576E4
 * ----------------------------------------------------------------------------
 * Same cull as tie_checkobjecteyexyz, but for a static-object whose 16-bit
 * world coords are passed directly (each is shifted left by 8 to convert
 * to the engine's 24.8 fixed-point space before subtracting camera).
 * Used by the hyperstar render and the planet/mine static-object loop.
 * The 'bound = 0xFFFF' callers (hyperstars) effectively disable the
 * depth/view-cone tests so the function only computes the eye coords.
 *
 * Identical to demo. */
// FUNCTION: TIE 0x576E4
int16_t tie_checkstaticobjecteyexyz(int16_t wx, int16_t wy, int16_t wz, uint16_t bound) {
	int near_far_extent;
	int abs_x, abs_y;

	/* * 256 instead of << 8: same as the binary's `shl 8` but
	 * well-defined for negative int16 coords. */
	worldx = (int32_t)wx * 256 - camera.x;
	worldy = (int32_t)wy * 256 - camera.y;
	worldz = (int32_t)wz * 256 - camera.z;

	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	near_far_extent = (int)objecteyez + (int)bound;
	if (near_far_extent < 0)
		return 0;
	if ((near_far_extent >> 8) > (int)bound)
		return 0;

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	abs_x = (objecteyex < 0) ? -objecteyex : objecteyex;
	if (abs_x - (int)bound > near_far_extent)
		return 0;

	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	abs_y = (objecteyey < 0) ? -objecteyey : objecteyey;
	return (abs_y - (int)bound <= near_far_extent) ? 1 : 0;
}

/* Build up to eight explosion lights in the source craft's reflected local
 * basis (side, -forward, up). Returns and stores the emitted count. */
// FUNCTION: TIE 0x57158
int tie_makelocallights(int obj_idx) {
	uint32_t max_distance_sq;

	FlightObject* src_obj = &objects[obj_idx];
	draw_lockshipfileptrs(src_obj->ship_idx);
	int model_scale_shift = objectblockptr->model_scale_shift;

	/* Light reach scales with source ship size:
	 *   model_scale_shift == 0 -> 0x4000  (small craft, short reach).
	 *   model_scale_shift > 0  -> 0x8000 << (model_scale_shift - 1). */
	if (model_scale_shift)
		max_distance_sq = 0x8000u << (model_scale_shift - 1);
	else
		max_distance_sq = 0x4000u;

	int src_world_x = src_obj->world_x;
	int src_world_y = src_obj->world_y;
	int src_world_z = src_obj->world_z;
	int light_idx = 0;
	int light_count = 0;

	for (uint16_t scan_idx = 0; scan_idx < NUM_OBJECTS; ++scan_idx) {
		FlightObject* expl = &objects[scan_idx];
		int dx, dy, dz;
		int side_proj, fwd_proj, up_proj;
		DRAWPOL_LocalLight* out;
		uint8_t ship_idx;

		if (expl->ship_idx == 0)
			continue; /* dead slot */
		if (expl->genus != GENUS_EXPLOSION)
			continue;

		dx = expl->world_x - src_world_x;
		dy = expl->world_y - src_world_y;
		dz = expl->world_z - src_world_z;
		if ((uint32_t)collide_roughdistance3d(dx, dy, dz) >= max_distance_sq)
			continue;

		out = &localLights[light_idx];

		/* dot products: source craft's local basis × world delta.
		 * Coefficients are int16; cast each to int32 first to keep the
		 * sign during the multiply before summing. */
		side_proj =
			(int32_t)src_obj->side_x * dx + (int32_t)src_obj->side_y * dy + (int32_t)src_obj->side_z * dz;
		if (side_proj >= 0x40000000)
			side_proj = 0x3FFE0000;
		if (side_proj <= -0x40000000)
			side_proj = -0x3FFE0000;
		out->x = side_proj >> 15;

		fwd_proj = (int32_t)src_obj->fwd_x * dx + (int32_t)src_obj->fwd_y * dy + (int32_t)src_obj->fwd_z * dz;
		if (fwd_proj >= 0x40000000)
			fwd_proj = 0x3FFE0000;
		if (fwd_proj <= -0x40000000)
			fwd_proj = -0x3FFE0000;
		/* y axis is FLIPPED: we store -(fwd >> 15) so the local frame
		 * matches the right-handed eye-space DRAWPOL expects. */
		out->y = -(fwd_proj >> 15);

		up_proj = (int32_t)src_obj->up_x * dx + (int32_t)src_obj->up_y * dy + (int32_t)src_obj->up_z * dz;
		if (up_proj >= 0x40000000)
			up_proj = 0x3FFE0000;
		if (up_proj <= -0x40000000)
			up_proj = -0x3FFE0000;
		out->z = up_proj >> 15;

		/* Distance scale: small ships (model_scale_shift>0) divide by 2^(model_scale_shift-1);
		 * large ships (model_scale_shift==0) double the position. */
		if (model_scale_shift) {
			int8_t shift = (int8_t)(model_scale_shift - 1);
			out->x >>= shift;
			out->y >>= shift;
			out->z >>= shift;
		} else {
			out->x *= 2;
			out->y *= 2;
			out->z *= 2;
		}

		out->range = 16;
		ship_idx = expl->ship_idx;

		if (ship_idx >= 0x7Fu && ship_idx <= 0x82u) {
			switch (expl->anim_frame) {
				case 2:
				case 9:
					out->range = 192;
					break;
				case 3:
				case 5:
				case 6:
				case 7:
				case 8:
					out->range = 320;
					break;
				case 4:
					out->range = 480;
					break;
				case 10:
					out->range = 96;
					break;
				case 11:
					out->range = 48;
					break;
				default:
					break;
			}
			if (expl->damage_state >= 4)
				out->range *= ((int)expl->damage_state + 4) >> 2;
		} else if (ship_idx == 0x83u || ship_idx == 0x84u) {
			switch (expl->anim_frame) {
				case 2:
					out->range = 24;
					break;
				case 3:
					out->range = 48;
					break;
				case 4:
					out->range = 32;
					break;
				case 5:
					out->range = 16;
					break;
				default:
					break;
			}
		}
		++light_idx;
		++light_count;
		if (light_idx == 8)
			break; /* localLights[] is 8 entries */
	}

	localLightCnt = light_count;
	return light_count;
}

// FUNCTION: TIE98 0x48EC60 TIE_MakeLocalLights
int tie_makelocallights_tie98(FlightObject* src_obj) {
	localLightCnt = 0;
	if (!g_localLightsEnabled)
		return 0;

	uint32_t max_distance_sq = (uint32_t)species_table[src_obj->ship_idx].bound_hwidth + 0x4000u;
	int src_world_x = src_obj->world_x;
	int src_world_y = src_obj->world_y;
	int src_world_z = src_obj->world_z;
	int light_idx = 0;
	int light_count = 0;

	/* TIE98 0x48EC60 scans every object slot (loop bound 0x4F2A7C = 120 =
	 * NUM_OBJECTS), not DRAWPOL's per-frame poly-object counter. */
	for (uint16_t scan_idx = 0; scan_idx < NUM_OBJECTS; ++scan_idx) {
		FlightObject* expl = &objects[scan_idx];
		if (expl->ship_idx == 0 || expl->genus != GENUS_EXPLOSION)
			continue;

		int dx = expl->world_x - src_world_x;
		int dy = expl->world_y - src_world_y;
		int dz = expl->world_z - src_world_z;
		if ((uint32_t)collide_roughdistance3d(dx, dy, dz) >= max_distance_sq)
			continue;

		DRAWPOL_LocalLight* out = &localLights[light_idx];
		int side_proj =
			(int32_t)src_obj->side_x * dx + (int32_t)src_obj->side_y * dy + (int32_t)src_obj->side_z * dz;
		if (side_proj >= 0x40000000)
			side_proj = 0x3FFFFFFF;
		if (side_proj <= -0x40000000)
			side_proj = -0x3FFF0000;
		out->x = side_proj >> 15;

		int fwd_proj =
			(int32_t)src_obj->fwd_x * dx + (int32_t)src_obj->fwd_y * dy + (int32_t)src_obj->fwd_z * dz;
		if (fwd_proj >= 0x40000000)
			fwd_proj = 0x3FFFFFFF;
		if (fwd_proj <= -0x40000000)
			fwd_proj = -0x3FFF0000;
		out->y = -(fwd_proj >> 15);

		int up_proj = (int32_t)src_obj->up_x * dx + (int32_t)src_obj->up_y * dy + (int32_t)src_obj->up_z * dz;
		if (up_proj >= 0x40000000)
			up_proj = 0x3FFFFFFF;
		if (up_proj <= -0x40000000)
			up_proj = -0x3FFF0000;
		out->z = up_proj >> 15;

		out->range = 16;
		uint8_t ship_idx = expl->ship_idx;
		if (ship_idx >= 0x7Fu && ship_idx <= 0x82u) {
			switch (expl->anim_frame) {
				case 2:
				case 9:
					out->range = 192;
					break;
				case 3:
				case 5:
				case 6:
				case 7:
				case 8:
					out->range = 320;
					break;
				case 4:
					out->range = 480;
					break;
				case 10:
					out->range = 96;
					break;
				case 11:
					out->range = 48;
					break;
				default:
					break;
			}
			if (mission.train_craft_type)
				out->range /= 8;
		} else if (ship_idx == 0x83u || ship_idx == 0x84u) {
			switch (expl->anim_frame) {
				case 2:
					out->range = 48;
					break;
				case 3:
					out->range = 96;
					break;
				case 4:
					out->range = 64;
					break;
				case 5:
					out->range = 32;
					break;
				default:
					break;
			}
			if (mission.train_craft_type)
				out->range /= 8;
		} else {
			out->range = g_explosionLightBase - 256;
		}
		out->range *= 8;

		++light_idx;
		++light_count;
		if (light_idx == 8)
			break;
	}

	localLightCnt = light_count;
	return light_count;
}

/* Advance global and craft timers, target blinking, mission clock and warning,
 * pilot damage bookkeeping, object ages, and message ages. */
// FUNCTION: TIE 0x577F4
static uint16_t s_ai_timer_elapsed_ticks;

void tie_updatetime(void) {
	/* systemmask[10] / damagemsg[10] declared in collide.h. */
	/* Polar distance scratch: trig2_polardistance is set by the binary's
	 * pai_distancebetween call inside the blink branch and re-read here. */

	/* 1. Per-slot timer decrement. */
	for (uint16_t i = 0; i < 20; ++i) {
		if (timers[i] != 0) {
			int16_t v = (int16_t)(timers[i] - frameticks);
			if (v < 0)
				v = 0;
			timers[i] = v;
		}
	}

	/* 2. Per-craft AI/plan timer decrement. RETAIL: NUM_CRAFTS = 32. */
	for (uint16_t i = 0; i < NUM_CRAFTS; ++i) {
		FlightObject* obj = &objects[i];
		CraftData* cp;
		if (obj->ship_idx == 0)
			continue;
		cp = obj->craft_ptr;

		if (s_ai_timer_elapsed_ticks && cp->ai_update_rate_copy)
			cp->ai_update_rate_copy = (uint16_t)(cp->ai_update_rate_copy - s_ai_timer_elapsed_ticks);

		if (s_ai_timer_elapsed_ticks && cp->maneuver_timer) {
			int v = cp->maneuver_timer - s_ai_timer_elapsed_ticks;
			cp->maneuver_timer = (v < 0) ? 0 : v;
		}
		if (s_ai_timer_elapsed_ticks && cp->ai_plan_state) {
			int16_t v = (int16_t)(cp->ai_plan_state - s_ai_timer_elapsed_ticks);
			cp->ai_plan_state = (uint16_t)((v < 0) ? 0 : v);
		}
		if (cp->ion_drain_timer) {
			uint16_t v = (uint16_t)(cp->ion_drain_timer - frameticks);
			/* Sign-bit reload pattern: if the subtraction borrowed past 0
			 * (0x8000 set in the 16-bit result), reset to 0. */
			cp->ion_drain_timer = (v & 0x8000u) ? 0 : v;
		}
	}

	/* 3. HUD target-blink ticker. blinkticks is its own global, not a
	 * timers[] slot. */
	{

		blinkticks = (int16_t)(blinkticks - frameticks);
		if (blinkticks < 0) {
			uint16_t bound_hwidth = 0; /* see HUD-blink branch below */
			uint8_t tgt_species = 0;

			/* Toggle the 0x400 bit (= 4 in HIBYTE). */
			targetblinkstate ^= 0x0400u;

			if (pstate.target_obj_idx != 0xFFFFu) {
				pai_distancebetween(pstate.target_obj_idx, pstate.object_idx);
				if (pstate.target_obj_idx >= OBJ_REF_STATIC_BASE)
					tgt_species = staticobjects[pstate.target_obj_idx - OBJ_REF_STATIC_BASE].species;
				else
					tgt_species = objects[pstate.target_obj_idx].ship_idx;
				trig2_polardistance >>= 5;
				bound_hwidth = species_table[tgt_species].bound_hwidth;
			}
			/* When (targetblinkstate & 0x400) is set we're in the "blink
			 * on" phase; otherwise "blink off". The size-vs-distance test
			 * picks 118 (normal cycle) vs 14 (micro-flicker for small targets). */
			if ((targetblinkstate & 0x0400u) != 0) {
				if ((int)bound_hwidth > trig2_polardistance) {
					blinkticks = 118; /* big enough to hold steady */
					goto blink_done;
				}
			} else if ((int)bound_hwidth <= trig2_polardistance) {
				blinkticks = 118;
				goto blink_done;
			}
			blinkticks = 14; /* small/distant target: flicker */
		}
	blink_done:;
	}

	/* 4. Publish target HUD state and tick the mission clock. */
	currenttarget = (uint16_t)((uint16_t)targetblinkflag | targetblinkstate | pstate.target_obj_idx);
	currenttargetcomp = (uint16_t)pstate.radar_target1;

	if (hyperspaceflag != 0)
		return; /* hyperspace freezes the mission clock */

	/* Per-frame sub-second decrement. On underflow, reload with one
	 * mission-second worth of ticks (236) and cascade into seconds /
	 * minutes / hours. The Watcom `xor reg, reg+1` clear trick on the
	 * 24-hour wrap is preserved literally so replay re-runs match the
	 * original byte-for-byte. */
	_date.subsec = (int16_t)(_date.subsec - frameticks);
	if (_date.subsec > 0)
		goto skip_clock_tick;

	_date.subsec += 236;

	if ((++_date.second) >= 60u) {
		_date.second = 0;
		if ((++_date.minute) >= 60u) {
			uint8_t pre_inc = (uint8_t)(_date.hour + 1);
			_date.minute = 0;
			if ((++_date.hour) >= 24u)
				_date.hour ^= pre_inc;
		}
	}

	/* Mission time-limit countdown. */
	--mtimer_sec;
	if (mtimer_sec == 255u) { /* sec underflowed */
		mtimer_sec = 59;
		if ((--mtimer_min) == 255u) { /* min underflowed -> done */
			mtimer_sec = 0;
			mtimer_min = 0;
			if (mission.train_craft_type) { /* training mission */
				user_checkreplaycamera();
				mission.end_flag = 1;
				mission.player_status = 3;
			}
		}
	}
	/* Last-15-second timer warning beep. */
	if (mission.train_craft_type && mtimer_min == 0 && mtimer_sec < 15u)
		fsfx_triggersfx(0x20u, 0xFFFFu);

	/* 5. Pilot-rank demotion: find the slot with score==0 + smallest
	 * pilot idx; decrement its kills counter or, if that's already 0,
	 * "destroy" the next subsystem (set score=100, apply systemmask
	 * damage, queue a MSG_SYSTEM_STATUS message). */
	{
		uint16_t min_pilot_idx = 0xFFFFu;
		int16_t demote_slot = -1;

		for (uint16_t scan = 0; scan < 10; ++scan) {
			if (pstate.rank_pilot_score[scan] == 0) {
				int cur_idx = pstate.rank_pilot_idx[scan];
				if ((uint16_t)cur_idx < min_pilot_idx) {
					demote_slot = (int16_t)scan;
					min_pilot_idx = (uint8_t)cur_idx;
				}
			}
		}

		for (uint16_t slot = 0; slot < 10; ++slot) {
			int16_t score = (int16_t)pstate.rank_pilot_score[slot];
			if (score == 0 && (int16_t)slot == demote_slot) {
				int16_t kills = (int16_t)pstate.rank_pilot_kills[slot];
				if (kills) {
					pstate.rank_pilot_kills[slot] = (uint16_t)(kills - 1);
				} else {
					pstate.rank_pilot_score[slot] = 100;
					/* Apply the per-system damage bit and emit the message.
					 * Binary dereferences player_craft unconditionally; if
					 * the pointer is NULL here we have a bigger problem. */
					pstate.player_craft->status_flags |= systemmask[slot];
					argtable[0] = damagemsg[slot];
					argtable[1] = 26; /* "destroyed" suffix template */
					msg_messageprintf(MSG_SYSTEM_STATUS);
				}
			}
		}
	}

	/* 6. Age every live object. RETAIL: NUM_OBJECTS = 120. */
	for (uint16_t i = 0; i < NUM_OBJECTS; ++i)
		if (objects[i].ship_idx)
			++objects[i].age_ticks;

	/* 7. Tick all queued cockpit messages forward. */
	msg_updatemessageage();

skip_clock_tick:
	return;
}

static void tie_run_plane_ai(TieFlightCadence cadence) {
	if (!cadence.due || mission.train_craft_type != 0)
		return;
	if (!TieFlightTiming_IsHighRate()) {
		pai_updateplaneai();
		return;
	}

	const uint16_t real_frameticks = frameticks;
	const uint16_t real_framerate = framerate;
	frameticks = cadence.elapsed_ticks;
	framerate = (uint16_t)(236u / cadence.elapsed_ticks);
	if (!framerate)
		framerate = 1;
	pai_updateplaneai();
	TieAiLead_CommitBoundary();
	frameticks = real_frameticks;
	framerate = real_framerate;
}

static void tie_run_animation(TieFlightCadence cadence) {
	if (!cadence.due) {
		/* PORT: Player movement still advances on unlocked ticks. Keep the
		 * gate-plane check at that cadence while mesh animation remains on
		 * the recovered compatibility cadence. */
		if (TieFlightTiming_IsHighRate() && mission.train_craft_type)
			gate_updatecourseprogress();
		return;
	}
	if (!TieFlightTiming_IsHighRate()) {
		anim_updateanimation();
		return;
	}

	const uint16_t real_frameticks = frameticks;
	const uint16_t real_framerate = framerate;
	frameticks = cadence.elapsed_ticks;
	framerate = (uint16_t)(236u / cadence.elapsed_ticks);
	if (!framerate)
		framerate = 1;
	anim_updateanimation();
	frameticks = real_frameticks;
	framerate = real_framerate;
}

/* Throttled iMUSE state evaluator. Training progress, objective state,
 * hostile proximity, missile locks, and force balance determine the music
 * state and intensity. */
// FUNCTION: TIE 0x57C7C
void tie_updatemusic(void) {
	/* music_state / music_intensity are file-scope globals (see top of
	 * tie.c); we seed them fresh at entry to match the binary's
	 * per-frame reset semantics, but they persist in .bss so the replay
	 * state-dump can capture them. */
	music_state = 1;
	music_intensity = 0;
	uint16_t ships_per_side[6] = { 0 };
	uint32_t min_distance = 0xFFFFFFFFu;
	int closest_enemy_obj = 0xFFFF;
	uint32_t combat_thresh = 0;
	int has_missile_lock_on_player = 0;
	uint16_t primary_kill_count = 0;
	uint16_t secondary_total = 0;
	uint16_t secondary_kill_count = 0;
	uint16_t hostile_score = 0;
	int hostile_pct = 0;

	/* Bail if music disabled, no buffer, or cooldown active. */
	if (!musicenabled || !music_buffer || timers[TIMER_MUSIC_CHANGE] != 0)
		return;
	timers[TIMER_MUSIC_CHANGE] = 59;

	/* --- Training mission paths ---------------------------------------- */
	if (mission.train_craft_type != 0) {
		if (mtimer_min || mtimer_sec >= 20u) {
			if ((uint16_t)mission.train_gates_remaining < 2u)
				music_state = 9;
			else if ((uint16_t)mission.train_gates_remaining < 3u)
				music_state = 7;
			else
				music_state = 6;
		} else {
			music_state = 8;
		}
		goto publish;
	}

	/* --- Combat mission state ----------------------------------------- */
	if (mission.primary_complete == 2) { /* won */
		music_state = 10;
		goto publish;
	}
	if (pri_complete_cooldown != 0 || sec_complete_cooldown != 0) {
		music_state = 11; /* objective hold */
		goto publish;
	}

	/* Tally ships per side weighted by genus + find the closest hostile. */
	/* RETAIL: scan first NUM_CRAFTS (32) slots. */
	for (uint16_t obj_iter = 0; obj_iter < NUM_CRAFTS; ++obj_iter) {
		FlightObject* obj = &objects[obj_iter];
		uint8_t g;

		if (obj->ship_idx == 0)
			continue;
		g = obj->genus;
		if (g == GENUS_STARSHIP || g == GENUS_PLATFORM)
			ships_per_side[obj->side] += 4;
		else if (g == GENUS_TRANSPORT || g == GENUS_FREIGHTER)
			ships_per_side[obj->side] += 2;
		else
			++ships_per_side[obj->side];

		if (obj->side != pstate.player->side && obj->craft_ptr->status_flags != 0) {
			uint32_t d;
			pai_roughdistancebetween(obj_iter, pstate.object_idx);
			d = (uint32_t)roughdistance;
			/* Cap-ship and freighter weight: count them as closer. */
			if (obj->genus == GENUS_STARSHIP)
				d >>= 2;
			if (obj->genus == GENUS_PLATFORM)
				d >>= 2;
			if (obj->genus == GENUS_FREIGHTER)
				d >>= 1;
			if (min_distance > d) {
				min_distance = d;
				closest_enemy_obj = obj_iter;
			}
		}
	}

	/* No hostile in range. */
	if ((uint16_t)closest_enemy_obj == 0xFFFFu) {
		if (mission.primary_complete == 1)
			music_state = 11;
		else if (entercombatflag)
			music_state = 2;
		else
			music_state = 1;
		goto publish;
	}

	/* Hostile in range — pick combat-near vs combat-far threshold. */
	combat_thresh = entercombatflag ? 0x40000u : 0x20000u;
	if (min_distance > combat_thresh) {
		/* Far away: ramp intensity 5 -> 0 as we go further. */
		music_intensity = (uint16_t)(5 - ((min_distance - combat_thresh) >> 15));
		music_state = 1;
		if (music_intensity >= 0x8000u)
			music_intensity = 0;
		goto publish;
	}

	/* Within attack range: scan AI fighter slots for missile lock on player.
	 * RETAIL: slots 48..79 (32 wide). Demo was 44..75. */
	entercombatflag = 1;
	for (uint16_t slot = 48; slot < 80; ++slot) {
		FlightObject* obj = &objects[slot];
		CraftData* cp;
		if (obj->ship_idx == 0)
			continue;
		cp = obj->craft_ptr;
		if (cp->species_idx == 0)
			continue;
		if (pstate.object_idx == cp->missile_target) {
			has_missile_lock_on_player = 1;
			break;
		}
	}
	if (has_missile_lock_on_player) {
		music_state = 8; /* urgent */
		goto publish;
	}

	/* Mid-to-close-range: walk the FG kill counts to detect "almost wiped". */
	if (min_distance <= 0x10000u) {
		uint16_t fg_iter = 0;
		uint16_t total_primary = 0;

		for (fg_iter = 0; fg_iter < (uint16_t)mission_file_header.num_fg; ++fg_iter) {
			if (mission.primary_fg[fg_iter])
				++total_primary;
			if (mission.primary_fg[fg_iter] == 1)
				++primary_kill_count;
			if (mission.secondary_fg[fg_iter])
				++secondary_total;
			if (mission.secondary_fg[fg_iter] == 1)
				++secondary_kill_count;
		}

		/* Big primary or secondary FG with all but 1 dead -> state 9. */
		if ((total_primary > 3u && primary_kill_count + 1 == total_primary) ||
			(secondary_total > 3u && secondary_kill_count + 1 == secondary_total)) {
			music_state = 9;
			goto publish;
		}

		/* Compare hostile vs ally weighted score (sides 0/4 always count;
		 * sides 2/3/5 only count if their tag string starts with '1'). */
		if (min_distance >= 0x8000u || (objects[(uint16_t)closest_enemy_obj].genus != GENUS_STARSHIP &&
										objects[(uint16_t)closest_enemy_obj].genus != GENUS_PLATFORM)) {
			hostile_score = (uint16_t)(ships_per_side[4] + ships_per_side[0]);
			/* Sides 2/3/5 only count as hostile when their .TIE-file IFF
			 * tag string starts with '1' (mission_file_header.mission.
			 * neutral_name[side-2][0]). Side 4 is unconditionally counted
			 * above; the binary's nearest-enemy filter likewise omits it. */
			if (mission_file_header.mission.neutral_name[0][0] == '1')
				hostile_score += ships_per_side[2];
			if (mission_file_header.mission.neutral_name[1][0] == '1')
				hostile_score += ships_per_side[3];
			if (mission_file_header.mission.neutral_name[3][0] == '1')
				hostile_score += ships_per_side[5];

			if (hostile_score <= ships_per_side[1]) {
				music_state = 7; /* winning */
				goto publish;
			}
			hostile_pct = math2_percentage(ships_per_side[1], hostile_score);
			if (hostile_pct >= 57344) { /* >= 87.5% */
				music_state = 7;
				goto publish;
			}
			if (hostile_pct >= 0x8000) { /* >= 50% */
				music_state = 6;
				goto publish;
			}
		}
		music_state = 8; /* outnumbered */
		goto publish;
	}

	/* Mid-range default: pick a music state based on which sides are alive. */
	if (ships_per_side[0])
		music_state = 3;
	else if (ships_per_side[4])
		music_state = 5;
	else
		music_state = 4;

publish:
	if (music_intensity > 5u)
		music_intensity = 5;
	lastmusicstate = (uint8_t)music_state;
	fscript_MsSetState(music_state);
	fscript_MsSetAttribute(0, (int16_t)music_intensity);
	fscript_MsRefreshScript();
}

// GLOBAL: TIE98 0x596B80
static int cdmusic_kind;
// GLOBAL: TIE98 0x597180
static int cdmusic_switch_latched;
// GLOBAL: TIE98 0x595F60
static int32_t cdmusic_ms_remaining;
// GLOBAL: TIE98 0x591C20
static uint32_t cdmusic_last_ms;
static const uint8_t cdmusic_start_min[4] = { 0, 4, 8, 12 };
static const uint8_t cdmusic_start_sec[4] = { 0, 1, 40, 52 };

// FUNCTION: TIE98 0x48F730
static void tie_updatemusic_tie98(void) {
	uint32_t now;
	if (inflight_music_vol == 0 || musicenabled == 0) {
		CDAUDIO_Stop_Track();
		cdmusic_ms_remaining = 0;
		return;
	}
	if (cdmusic_kind == 2 && !cdmusic_switch_latched) {
		int kind = 0;
		if (mission.primary_global == 2)
			kind = 4;
		else if (timers[TIMER_PRI_COMPLETE] || timers[TIMER_SEC_COMPLETE])
			kind = 3;
		if (kind) {
			CDAUDIO_Play_Track(kind, 0, 0);
			cdmusic_ms_remaining = CDAUDIO_Track_Length_Ms(kind);
			cdmusic_kind = kind;
			cdmusic_switch_latched = 1;
			return;
		}
	}
	now = TieMusicPolicy_NowMs();
	cdmusic_ms_remaining -= (int32_t)(now - cdmusic_last_ms);
	cdmusic_last_ms = now;
	if (cdmusic_ms_remaining <= 0) {
		CDAUDIO_Play_Track(2, 0, 0);
		cdmusic_ms_remaining = CDAUDIO_Track_Length_Ms(2);
		cdmusic_last_ms = TieMusicPolicy_NowMs();
		cdmusic_kind = 2;
	}
}

static void tie_update_selected_music(void) {
	if (TieMusicPolicy_UsesTie98())
		tie_updatemusic_tie98();
	else
		tie_updatemusic();
}

static void tie_start_tie98_mission_music(void) {
	cdmusic_switch_latched = 0;
	if (CDAUDIO_Open_Device()) {
		const int start = rand_rand() & 3;
		gamesnd_Set_CD_Volume(inflight_music_vol);
		CDAUDIO_Play_Track(2, cdmusic_start_min[start], cdmusic_start_sec[start]);
		cdmusic_ms_remaining =
			CDAUDIO_Track_Length_Ms(2) - 1000 * (cdmusic_start_sec[start] + 60 * cdmusic_start_min[start]);
		cdmusic_last_ms = TieMusicPolicy_NowMs();
		cdmusic_kind = 2;
	} else {
		cdmusic_ms_remaining = INT32_MAX;
		cdmusic_kind = 0;
	}
}

// FUNCTION: TIE98 0x48D9B0 (task-split recovery)
static bool tie_doframe_tie98(void) {
	if (!Tie98Renderer_ApplyPending())
		return false;
	if (replayviewmode) {
		ReplayInputFrame replay_frame;
		if (!TieReplayTiming_DecodeCurrentInputFrame(&replay_frame)) {
			replay_stopreplay();
			return true;
		}
		frameticks = replay_frame.frameticks;
		framerate = (uint16_t)(236 / frameticks);
		if (framerate == 0)
			framerate = 1;
	} else {
		/* PORT: the original busy-waits until one flight period has
		 * accumulated. Consume the sampled interval as one bounded frame;
		 * the task returns to the host before another logical frame runs. */
		const uint16_t minimum_ticks = TieFlightTiming_StepTicks();
		tickcounter += (uint16_t)xtimer_time_elapsed();
		if (tickcounter < minimum_ticks)
			return false;
		lastcounter = (int16_t)tickcounter;
		tickcounter = 0;
		if (calcframerate) {
			frameticks = (uint16_t)lastcounter;
			framerate = (uint16_t)(236 / frameticks);
			if (framerate == 0) {
				framerate = 1;
				frameticks = 236;
			}
		}
		calcframerate = 1;
	}

	mapflag = 0;
	if (acceleratedtimesetting <= 1u || acceleratedtimectr == 0)
		user_userinterface();
	/* PORT: TIE98's pause loop is represented by the host task state. */
	if (user_is_paused())
		return true;
	if (mission.end_flag != 0 || mapflag != 0)
		return true;

	TieFlightTiming_BeginAdvance(frameticks);
	TieAiLead_Advance(frameticks);
	const TieFlightCadence ai_cadence = TieFlightTiming_AdvanceAi(frameticks);
	const TieFlightCadence animation_cadence = TieFlightTiming_AdvanceAnimation(frameticks);
	s_ai_timer_elapsed_ticks = ai_cadence.due ? ai_cadence.elapsed_ticks : 0;
	tie_updatetime();
	if (mission.train_craft_type == 0) {
		create_updatefgstatus();
	}
	tie_run_plane_ai(ai_cadence);
	laser_weaponsfire();
	dynamix_planedynamics();

	int rendered = 0;
	if (replayviewmode) {
		if (fastforwardflag) {
			if (frameticks > (uint16_t)fastforwardtimer) {
				fastforwardtimer += 236;
				if (acceleratedtimesetting <= 1u) {
					tie_updatescreen();
					rendered = 1;
				} else if (acceleratedtimectr != 0) {
					tickcounter += (uint16_t)xtimer_time_elapsed();
					tickcounter += frameticks;
					--acceleratedtimectr;
				} else {
					tie_updatescreen();
					rendered = 1;
					acceleratedtimectr = acceleratedtimesetting - 1;
				}
			}
			fastforwardtimer -= (int16_t)frameticks;
		} else if (acceleratedtimesetting <= 1u) {
			tie_updatescreen();
			rendered = 1;
		} else if (acceleratedtimectr != 0) {
			tickcounter += (uint16_t)xtimer_time_elapsed();
			tickcounter += frameticks;
			--acceleratedtimectr;
		} else {
			tie_updatescreen();
			rendered = 1;
			acceleratedtimectr = acceleratedtimesetting - 1;
		}
	} else if (acceleratedtimesetting <= 1u) {
		tie_updatescreen();
		FlightSurface_Lock();
		panel_updatepanel();
		FlightSurface_Unlock();
		rendered = 1;
	} else if (acceleratedtimectr != 0) {
		tickcounter += (uint16_t)xtimer_time_elapsed();
		tickcounter += frameticks;
		--acceleratedtimectr;
	} else {
		tie_updatescreen();
		FlightSurface_Lock();
		panel_updatepanel();
		FlightSurface_Unlock();
		rendered = 1;
		acceleratedtimectr = acceleratedtimesetting - 1;
	}

	if (drawdebrisflag && mission.train_craft_type == 0 && TieFlightTiming_LegacyDue())
		create_checkdebris();
	collide_collisions();
	move_moveobjects();
	tie_run_animation(animation_cadence);
	score_checkobjective();
	msg_messageupdate();
	tie_update_selected_music();
	if (blastflag) {
		FrontendSound_FlushQueuedSounds();
		if (blastcount)
			fsfx_checkblastqueue();
		fsfx_checktieflyby();
		FSFX_UpdatePlayerEngineSound();
	}

	if (rendered) {
		FrontendDisplay_PresentFrame();
		if (g_useHardware3D)
			RenderScene_ClearFrameBuffers();
		else
			FrontendDisplay_BlitOffscreenToRenderSurface();
	}
	return true;
}

// FUNCTION: TIE 0x56270
bool tie_doframe(void) {
	if (TieProfile_UsesTie98Logic())
		return tie_doframe_tie98();

	/* Step 1 — refresh frameticks/framerate. */
	if (replayviewmode) {
		ReplayInputFrame replay_frame;
		if (!TieReplayTiming_DecodeCurrentInputFrame(&replay_frame)) {
			replay_stopreplay();
			return true;
		}
		frameticks = replay_frame.frameticks;
		framerate = (uint16_t)(236 / frameticks);
		if (framerate == 0)
			framerate = 1;
	} else {
		/* xtimer advances between runtime ticks; return until a complete
		 * simulation period has accumulated. */
		tickcounter += (uint16_t)xtimer_time_elapsed();
		if (tickcounter < TieFlightTiming_StepTicks())
			return false;

		lastcounter = (int16_t)tickcounter;
		tickcounter = 0;

		if (calcframerate) {
			framerate = (uint16_t)(236 / (uint16_t)lastcounter);
			frameticks = (uint16_t)lastcounter;
			if (framerate == 0) {
				framerate = 1;
				frameticks = 236;
			}
		}
		calcframerate = 1;
	}

	/* Step 2 — UI input pass (skipped on fast-time skip frames). */
	mapflag = 0;
	if (acceleratedtimesetting <= 1u || acceleratedtimectr == 0)
		user_userinterface();

	/* The tick budget was consumed even when world work is skipped. */
	if (user_is_paused())
		return true;
	if (mission.end_flag != 0 || mapflag != 0)
		return true;

	TieFlightTiming_BeginAdvance(frameticks);
	TieAiLead_Advance(frameticks);
	const TieFlightCadence ai_cadence = TieFlightTiming_AdvanceAi(frameticks);
	const TieFlightCadence animation_cadence = TieFlightTiming_AdvanceAnimation(frameticks);
	s_ai_timer_elapsed_ticks = ai_cadence.due ? ai_cadence.elapsed_ticks : 0;
	tie_updatetime();
	if (mission.train_craft_type == 0) {
		create_updatefgstatus();
	}
	tie_run_plane_ai(ai_cadence);
	laser_weaponsfire();
	dynamix_planedynamics();

	/* Render gate (with accelerated-time skip): renders once every
	 * acceleratedtimesetting frames when set > 1. */
	bool rendered = false;
	if (replayviewmode) {
		if (fastforwardflag) {
			/* Fast-forward stalls rendering until fastforwardtimer drains
			 * one mission-second (236 ticks) worth of frame time. */
			if (frameticks > (uint16_t)fastforwardtimer) {
				fastforwardtimer += 236;
				rendered = render_world_or_skip(/*has_panel=*/0);
			}
			fastforwardtimer -= (int16_t)frameticks;
		} else {
			rendered = render_world_or_skip(/*has_panel=*/0);
		}
	} else {
		rendered = render_world_or_skip(/*has_panel=*/1);
	}

	/* Post-render world updates. */
	if (drawdebrisflag && mission.train_craft_type == 0 && TieFlightTiming_LegacyDue())
		create_checkdebris();
	collide_collisions();
	move_moveobjects();
	tie_run_animation(animation_cadence);
	score_checkobjective();
	msg_messageupdate();
	tie_update_selected_music();

	if (blastflag) {
		if (blastcount)
			fsfx_checkblastqueue();
		fsfx_checktieflyby();
	}
	if (rendered && TieClassicDisplay_UsesDx5()) {
		FrontendDisplay_PresentFrame();
		FrontendDisplay_BlitOffscreenToRenderSurface();
	}

	/* The application uploads vesa_buff_gbl at the end of the tick. */
	return true;
}

/* Render-or-skip helper used by both branches of tie_doframe. Implements
 * the accelerated-time pattern:
 *   acceleratedtimesetting <= 1            -> always render.
 *   acceleratedtimesetting > 1, ctr == 0   -> render once, re-arm ctr.
 *   acceleratedtimesetting > 1, ctr != 0   -> skip render, accumulate
 *                                              elapsed ticks instead.
 * has_panel is 1 only for the live (non-replay) branch, which also runs
 * panel_updatepanel after the world render. */
static bool render_world_or_skip(int has_panel) {
	bool rendered = false;
	if (acceleratedtimesetting <= 1u) {
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Lock();
		tie_updatescreen();
		if (has_panel)
			panel_updatepanel();
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Unlock();
		rendered = true;
	} else if (acceleratedtimectr) {
		/* Skip the render — let the timer catch up. */
		tickcounter += (uint16_t)xtimer_time_elapsed();
		tickcounter += frameticks;
	} else {
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Lock();
		tie_updatescreen();
		if (has_panel)
			panel_updatepanel();
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Unlock();
		acceleratedtimectr = acceleratedtimesetting;
		rendered = true;
	}
	if (acceleratedtimesetting > 1u)
		--acceleratedtimectr;
	return rendered;
}

/* Per-frame world render: camera, flight objects, static objects, rasterizer,
 * bitmap queue, and starfield. */
void tie_updatescreen(void) {
	if (TieProfile_UsesTie98Logic()) {
		/* PORT: keep host-only frame state outside recovered TIE98 TIE_Update_Screen. */
		TieBillboardCapture_BeginTick();
		draw_sync_tie98_hyperstar_state();
		/* PORT: TIE98 rebuilds its hardware palette table when DirectDraw's
		 * palette changes. The shared framebuffer owns that palette here. */
		RenderTexture_SyncFlightPalette();
		tie_updatescreen_tie98();
		TieFlightSnapshot_RecordCameraBasis();
		return;
	}
	tie_updatescreen_tie95();
}

// FUNCTION: TIE 0x56574
static void tie_updatescreen_tie95(void) {
	/* SNAPSHOT-ONLY: reset the per-tick HD billboard capture caches
	 * here, at the start of every tick that actually renders the 3D
	 * world. tie_doframe gates this call behind user_is_paused() and
	 * the accelerated-time skip counter, so paused / skipped frames
	 * leave the caches frozen on the last-rendered tick — which is
	 * exactly what the HD billboard pass needs to keep showing the
	 * frozen sprites while the engine is paused (classic just keeps
	 * the previous framebuffer; HD pulls a fresh snapshot every host
	 * tick and would otherwise see an empty billboard array). */
	TieBillboardCapture_BeginTick();

	/* --- Step 1: pick a camera --------------------------------------- */
	if (replayviewmode) {
		replay_calcreplayview();
	} else if (camera.view_target_obj == 0xFFFFu) {
		/* No target: aim camera at player via TRIG2_ctop, roll=0. */
		if (!hyperspaceflag) {
			trig2_ctop(pstate.player->world_x - camera.x, pstate.player->world_y - camera.y,
					   pstate.player->world_z - camera.z);
			camera.roll = 0;
			camera.cam_heading = trig2_zangle;
			camera.cam_pitch = trig2_xyangle;
		}
		fview_newcalcview(camera.roll, camera.cam_heading, camera.cam_pitch, 0, (int16_t)camera.side_angle,
						  (int16_t)camera.up_angle, NULL);
		TieFlightSnapshot_RecordCameraBasis();
	} else if ((camera.view_zoom_flag && camera.view_heading_offset == 0) ||
			   (camera.view_heading_offset != 0 && camera.view_pitch_offset != 0)) {
		TieChaseCamera_Update();

		fview_newcalcview(camera.roll, camera.cam_heading, camera.cam_pitch, 0, (int16_t)camera.side_angle,
						  (int16_t)camera.up_angle, NULL);
		TieFlightSnapshot_RecordCameraBasis();

		/* Hyperspace transition phases 4 / 6 zero the camera. */
		if (hyperspaceflag == 4 || hyperspaceflag == 6) {
			camera.z = 0;
			camera.y = 0;
			camera.x = 0;
		} else {
			create_getworldposition(camera.view_target_obj, 0);
			camera.x = worldlocx;
			camera.y = worldlocy;
			camera.z = worldlocz;
		}

		/* Pull the camera back along the world's Z eye basis by the
		 * zoom factor (integer offset). Uses worldeye*3 (the global
		 * world-to-eye basis set per frame by FVIEW_newcalcview), NOT
		 * rotworldeye*3 (the per-component-rotated basis used during
		 * ship rendering). Binary TIE_updatescreen 0x568A7/0x568C3/0x568EB. */
		camera.x -= (worldeyeA3 * camera.view_zoom) >> 15;
		camera.y -= (worldeyeB3 * camera.view_zoom) >> 15;
		camera.z -= (worldeyeC3 * camera.view_zoom) >> 15;

		{
			uint8_t species_idx;
			uint16_t bound_hwidth;
			if (camera.view_target_obj >= OBJ_REF_STATIC_BASE)
				species_idx = staticobjects[camera.view_target_obj - OBJ_REF_STATIC_BASE].species;
			else
				species_idx = objects[camera.view_target_obj].ship_idx;
			bound_hwidth = species_table[species_idx].bound_hwidth;
			objectsize = (int16_t)(bound_hwidth >> 2);
			/* Same basis as above. Binary 0x5696F/0x56989/0x569A2. */
			camera.x -= 4 * ((worldeyeA3 * (uint16_t)objectsize) >> 15);
			camera.y -= 4 * ((worldeyeB3 * (uint16_t)objectsize) >> 15);
			camera.z -= 4 * ((worldeyeC3 * (uint16_t)objectsize) >> 15);
		}
	} else if (camera.view_heading_offset != 0) {
		panel_pointcamera(camera.view_target_obj, 0);
		TieFlightSnapshot_RecordCameraBasis();
	} else {
		/* Default: camera follows camera.view_target_obj's exact position+orient. */
		FlightObject* o = &objects[camera.view_target_obj];
		camera.roll = o->roll;
		camera.cam_heading = o->heading;
		camera.cam_pitch = o->pitch;
		fview_newcalcview(camera.roll, camera.cam_heading, camera.cam_pitch, camera.bank,
						  (int16_t)camera.side_angle, (int16_t)camera.up_angle, o);
		TieFlightSnapshot_RecordCameraBasis();
		camera.x = o->world_x;
		camera.y = o->world_y;
		camera.z = o->world_z;
		if (camera.view_target_obj == pstate.object_idx && hyperspaceflag != 3 && hyperspaceflag != 5) {
			camera.x += pstate.laser_origin_dx;
			camera.y += pstate.laser_origin_dy;
			camera.z += pstate.laser_origin_dz;
		}
	}

	/* --- Step 2: full-frame buffer reset ---------------------------- */
	if (fullupdateflag) {
		logbuf2_clearbuffer();
		xtrans2_clearruntable();
		fullupdateflag = 0;
	}
	xtrans2_initxtrans();

	parentobject = 12288; /* 0x3000 — sentinel meaning "no parent" */
	backdrp2_backdrop();
	numbitmaps = 0;

	/* --- Step 3: per-object render dispatch. RETAIL: 0..119; skip slot
	 * DEBRIS_FIRST_SLOT (112) unless we're rendering debris. ----------- */
	for (int16_t obj_iter = 0; obj_iter < (int16_t)NUM_OBJECTS; ++obj_iter) {
		FlightObject* obj;
		uint16_t bound;
		uint8_t genus_v;

		if (obj_iter == DEBRIS_FIRST_SLOT &&
			!(drawdebrisflag && !hyperspaceflag && mission.train_craft_type == 0))
			continue;

		if (obj_iter == (int16_t)camera.view_target_obj && camera.view_zoom_flag == 0 && !replayviewmode)
			continue;

		obj = &objects[obj_iter];
		if (obj->ship_idx == 0)
			continue;

		genus_v = obj->genus;
		bound = species_table[obj->ship_idx].bound_hwidth;
		objectsize = (int16_t)bound;

		if (genus_v > 0xEu)
			continue;

		switch (genus_v) {
			case GENUS_FIGHTER:
			case GENUS_TRANSPORT:
			case GENUS_UTILITY:
			case GENUS_FREIGHTER:
			case GENUS_STARSHIP:
			case GENUS_PLATFORM:
			case GENUS_GATE: { /* 14 */
				/* Inline cull. NOTE: this is NOT equivalent to
				 * tie_checkobjecteyexyz even though the math looks similar.
				 * Boundary comparisons differ:
				 *   helper    culls when `eye_z + bound <  0`   (strict);
				 *             passes when `|eye_x| - bound <= eye_z+bound`.
				 *   inline    gates on `eye_z >> 8 <  bound` first (no
				 *             near_far term), and culls with STRICT `>=`
				 *             on the view-cone tests.
				 * Both match the binary's two separate code paths byte-for-
				 * byte — keep them in sync if either ever needs updating. */
				int eye_z;
				int near_far;
				int abs_x, abs_y;

				craftptr = obj->craft_ptr;
				tie_getobjecteyexyz((uint16_t)obj_iter);
				/* tie_getobjecteyexyz already wrote eye_*_cache too. */
				eye_z = objecteyez;

				if ((eye_z >> 8) >= (int)bound)
					break;
				near_far = (int)bound + eye_z;
				if (near_far <= 0)
					break;
				abs_x = (objecteyex < 0) ? -objecteyex : objecteyex;
				if (abs_x - (int)bound >= near_far)
					break;
				abs_y = (objecteyey < 0) ? -objecteyey : objecteyey;
				if (abs_y - (int)bound >= near_far)
					break;

				if (genus_v == GENUS_GATE)
					lightflag = 0;
				fview_newcalcrotate(obj->roll, obj->heading, obj->pitch, 0, obj);
				if (TieProfile_UsesTie98Logic()) {
					/* PORT: native OPT craft are emitted through the snapshot. */
				} else if (genus_v == GENUS_GATE) {
					gate_drawtraininggate((uint16_t)obj_iter);
				} else {
					tie_makelocallights(obj_iter);
					draw_drawcomplexobject((uint16_t)obj_iter);
					localLightCnt = 0;
				}
				lightflag = 1;
				break;
			}

			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC:
				if (tie_checkobjecteyexyz((uint16_t)obj_iter, bound)) {
					fview_newcalcrotate(obj->roll, obj->heading, obj->pitch, 0, obj);
					draw_drawlaser((uint16_t)obj_iter);
				}
				break;

			case GENUS_MINE: /* 8 */
			case 9:
			case 10:
			case 12:
				/* Skipped / handled by the static-object loop or another
				 * render system. */
				break;

			case GENUS_DEBRIS:    /* 11 */
			case GENUS_EXPLOSION: /* 13 */
				if (tie_checkobjecteyexyz((uint16_t)obj_iter, bound)) {
					fview_newcalcrotate(obj->roll, obj->heading, obj->pitch, 0, obj);
					anim_drawverysimpleobject((uint16_t)obj_iter);
				}
				break;
		}
	}

	/* --- Step 4: static objects + hyperstars ------------------------ */
	for (int16_t i = 0; i < 64; ++i) {
		if (hyperspaceflag == 3 || hyperspaceflag == 5) {
			/* Hyperstar render: 4 mirrored stars per slot. */
			if (hyperspacedetail > i) {
				StaticObject* s = &staticobjects[i];
				int16_t wx = s->world_x;
				int16_t wy = s->world_y;
				int16_t wz = s->world_z;

				objectsize = -1;
				tie_checkstaticobjecteyexyz(wx, wy, wz, 0xFFFFu);
				draw_drawhyperstar(i);
				tie_checkstaticobjecteyexyz(wx, wy, -wz, (uint16_t)objectsize);
				draw_drawhyperstar(i);
				++flatobjnum;
				if (hyperspacedetail / 2 > i) {
					int16_t wx2 = (int16_t)((-wx) >> 1);
					int16_t wz2 = (int16_t)((-wz) >> 1);
					tie_checkstaticobjecteyexyz(wx2, wy, wz2, (uint16_t)objectsize);
					draw_drawhyperstar(i);
					tie_checkstaticobjecteyexyz((int16_t)(wx2 >> 1), wy, (int16_t)((-wz2) >> 1),
												(uint16_t)objectsize);
					draw_drawhyperstar(i);
					++flatobjnum;
				}
			}
			continue;
		}

		/* Standard static-object render (mines, planets, asteroids,
		 * backdrops). */
		if (staticobjects[i].species != 0) {
			StaticObject* s = &staticobjects[i];
			uint8_t spec_idx = s->species;
			uint16_t bound = species_table[spec_idx].bound_hwidth;
			uint8_t shipcl = s->ship_class;
			objectsize = (int16_t)bound;

			if (shipcl >= 8u && shipcl <= 0xBu &&
				tie_checkstaticobjecteyexyz(s->world_x, s->world_y, s->world_z, bound)) {
				/* Planets (species 100..105) self-rotate per frame. */
				if (spec_idx >= 100 && spec_idx <= 105 &&
					(!TieFlightTiming_IsHighRate() || TieFlightTiming_LegacyDue())) {
					uint16_t f =
						TieFlightTiming_IsHighRate() ? TieFlightTiming_CompatibilityTicks() : frameticks;
					s->roll_byte = (uint8_t)((int)s->roll_byte + (((int)f * (i >> 4)) >> 4));
					s->yaw_byte = (uint8_t)((int)s->yaw_byte + (((int)f * (i >> 3)) >> 5));
					s->pitch_byte = (uint8_t)((int)s->pitch_byte + (((int)f * (4 - (i >> 4))) >> 4));
				}
				fview_newcalcrotate((int16_t)((uint16_t)s->roll_byte << 8),
									(int16_t)((uint16_t)s->yaw_byte << 8),
									(int16_t)((uint16_t)s->pitch_byte << 8), 0, NULL);
				static_drawstaticobject((uint16_t)i);
			}
		}
	}

	/* --- Step 5: flush bitmap queue + XTRANS rasterizer ------------- */
	anim_sort_and_draw_bitmaps();
	dxtticks = 0;
	oxtticks = 0;
	tickcounter += (uint16_t)xtimer_time_elapsed();
	dxtticks = tickcounter;

	xtrans2_drawxtrans();
	tickcounter += (uint16_t)xtimer_time_elapsed();
	dxtticks = (uint16_t)(tickcounter - dxtticks);

	deepspacecolor = (uint8_t)-5;
	if (hyperspaceflag != 3 && hyperspaceflag != 5)
		rtsvga2_drawstars();

	/* Signal that the application must upload the classic framebuffer. */
	vesa_dirty_gbl = true;
}

/* Helper: shield-balance / shield-max as 0..3 quartile (used by
 * tie_simulator end-of-mission post_mission_shield_q4 latch). The binary
 * does this same `MATH2_percentage(...) >> 14` in-line; broken out here
 * for readability. */
static int shield_quartile(uint16_t balance, uint16_t maxv) {
	return (int)math2_percentage(balance, maxv) >> 14;
}

typedef enum {
	VIEW_REPLAY_PROMPT_PHASE_RENDER = 0,
	VIEW_REPLAY_PROMPT_PHASE_POLL,
	VIEW_REPLAY_PROMPT_PHASE_AFTER_VIEWER,
} ViewReplayPromptPhase;

typedef struct ViewReplayPromptTask {
	uint16_t saved_master_vol;
	TieFlightScreen previous_screen;
	ViewReplayPromptPhase phase;
} ViewReplayPromptTask;

static LandruTaskStepResult view_replay_prompt_task_step(void* self) {
	ViewReplayPromptTask* t = (ViewReplayPromptTask*)self;

	switch (t->phase) {

		case VIEW_REPLAY_PROMPT_PHASE_RENDER: {
			int32_t margin = screenXRes / 10;
			int32_t right = screenXRes - margin;

			imuse_set_master_vol(im, 0);
			imuse_pause(im);
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			festring_setfontsize(1);

			int32_t top = (screenYRes >> 1) - (int32_t)fontheight - (screenYRes >> 3);
			int32_t bottom = top + 2 * (int32_t)fontheight;

			festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
			festring_setbackcolor(0x40u);
			clearwindow();
			festring_setbound((int16_t)(margin - 1), (int16_t)(top - 1), (int16_t)(right + 1),
							  (int16_t)(bottom + 1));
			festring_setbackcolor(0x4Au);
			clearwindow();
			festring_setbound((int16_t)margin, (int16_t)top, (int16_t)right, (int16_t)bottom);
			festring_setbackcolor(0x40u);
			clearwindow();
			festring_settextcolor(0x43u);
			festring_setdropcolor(0x41u);
			festring_setcursor((int16_t)(margin + 1), (int16_t)(top + (int32_t)fontheight / 2));
			festring_outstringcenter((const uint8_t*)viewfilmstr);
			unblank();
			if (TieClassicDisplay_UsesDx5()) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
			t->phase = VIEW_REPLAY_PROMPT_PHASE_POLL;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case VIEW_REPLAY_PROMPT_PHASE_POLL: {
			/* Wait for input. YIELD so the next TieRuntime_Tick gets a chance
			 * to refresh the keyboard queue via the host's input pump
			 * (libretro pumps before TieRuntime_Tick; SDL3 same). Spinning
			 * within one TieRuntime_Tick would never see new keystrokes. */
			if (!TieInput_KeyPending())
				return LANDRU_TASK_STEP_YIELD;

			int ch = TieInput_ReadKey();
			if (ch == 'y' || ch == 'Y') {
				blank();
				imuse_set_master_vol(im, (int)t->saved_master_vol);
				imuse_resume(im);
				replayio_Push_ReplayScreen_Task();
				t->phase = VIEW_REPLAY_PROMPT_PHASE_AFTER_VIEWER;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			if (ch == 'n' || ch == 'N') {
				imuse_set_master_vol(im, (int)t->saved_master_vol);
				imuse_resume(im);
				imuse_stop_all_sounds(im);
				return LANDRU_TASK_STEP_DONE;
			}
			/* Any other key: keep polling. */
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case VIEW_REPLAY_PROMPT_PHASE_AFTER_VIEWER:
			/* Replay viewer popped — match the original synchronous flow's
			 * trailing blank() after replayio_replayscreen returned. */
			blank();
			return LANDRU_TASK_STEP_DONE;
	}

	return LANDRU_TASK_STEP_DONE;
}

static void view_replay_prompt_task_end(void* self) {
	ViewReplayPromptTask* t = (ViewReplayPromptTask*)self;
	TieFlightScreen_SetActive(t->previous_screen);
}

static const LandruTaskVtable view_replay_prompt_task_vt = {
	.step = view_replay_prompt_task_step,
	.end = view_replay_prompt_task_end,
};

static void view_replay_prompt_Push_Task(uint16_t saved_master_vol) {
	ViewReplayPromptTask* t = (ViewReplayPromptTask*)landru_task_push(&view_replay_prompt_task_vt);
	if (!t)
		return;
	t->previous_screen = TieFlightScreen_SetActive(TIE_FLIGHT_SCREEN_REPLAY_PROMPT);
	t->saved_master_vol = saved_master_vol;
	t->phase = VIEW_REPLAY_PROMPT_PHASE_RENDER;
}

typedef struct FlightTask {
	/* 1 = the previous step pushed replayio_Push_ReplayScreen_Task; on
	 * this step the viewer has popped and we owe the player a RESUMED
	 * banner (matches the original synchronous code which posted
	 * MSG_RESUMED right after replayio_replayscreen returned). */
	uint8_t resumed_banner_pending;
	uint8_t rebase_pending;
} FlightTask;

static LandruTaskStepResult flight_hyper_step(void* self) {
	(void)self;
	if (!hyperspaceflag)
		return LANDRU_TASK_STEP_DONE;
	/* tie_doframe returns false when its PIT-tick budget hasn't
	 * landed yet — yield so the next TieRuntime_Tick advances xtimer. */
	return tie_doframe() ? LANDRU_TASK_STEP_FRAME_COMPLETE : LANDRU_TASK_STEP_YIELD;
}

static LandruTaskStepResult flight_mission_step(void* self) {
	FlightTask* t = (FlightTask*)self;
	if (mission.end_flag != 0)
		return LANDRU_TASK_STEP_DONE;
	if (t->rebase_pending) {
		t->rebase_pending = 0;
		xtimer_rebase();
		tickcounter = 0;
	}

	/* If the previous step pushed the replay viewer, the viewer task
	 * has now popped (otherwise we wouldn't be running). Post the
	 * RESUMED banner the original synchronous flow emitted right
	 * after replayio_replayscreen returned. */
	if (t->resumed_banner_pending) {
		t->resumed_banner_pending = 0;
		msg_messageprintf(MSG_RESUMED);
	}

	/* Pending info-room request from a user_userinterface keybind?
	 * Push the in-flight info task on top so the host loop steps it
	 * next; this step yields, and the next tick advances the
	 * sub-task. When the info-room pops, control returns here and
	 * tie_doframe resumes its frame-by-frame cadence. */
	int32_t pending = user_consume_info_room_request();
	if (pending >= 0) {
		user_Push_InflightInfo_Task(pending);
		t->rebase_pending = 1;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* Pending replay-viewer request from the 'v' keybind? Push the
	 * viewer on top and arm the post-pop RESUMED banner. */
	if (user_consume_replay_viewer_request()) {
		replayio_Push_ReplayScreen_Task();
		t->resumed_banner_pending = 1;
		t->rebase_pending = 1;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* See flight_hyper_step — yield when tie_doframe couldn't
	 * consume its PIT-tick budget yet this TieRuntime_Tick. */
	return tie_doframe() ? LANDRU_TASK_STEP_FRAME_COMPLETE : LANDRU_TASK_STEP_YIELD;
}

static uint64_t flight_task_next_wake_delay_us(const void* self) {
	(void)self;
	if (replayviewmode)
		return UINT64_MAX;
	return xtimer_delay_until_ticks_us(tickcounter, TieFlightTiming_StepTicks());
}

static const LandruTaskVtable flight_hyper_task_vt = {
	.step = flight_hyper_step,
	.next_wake_delay_us = flight_task_next_wake_delay_us,
};
static const LandruTaskVtable flight_mission_task_vt = {
	.step = flight_mission_step,
	.next_wake_delay_us = flight_task_next_wake_delay_us,
};

/* Public push helper: replayio's "re-enter sim from viewer" path needs to
 * push the flight loop from outside this module. FlightTask is zero-initialized
 * by the task runner. */
void tie_Push_FlightMission_Task(void) {
	/* Tag the snapshot scene-kind for any modern renderer reading
	 * TieSnapshot_Current during this scene. Sticks across the
	 * flight task's lifetime; reset by whichever modal pushes next
	 * (debrief, replay viewer, etc.). */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
	(void)landru_task_push(&flight_mission_task_vt);
}

typedef enum {
	TIE_SIM_PHASE_INIT = 0,
	TIE_SIM_PHASE_LOADSCREEN_HOLD,
	TIE_SIM_PHASE_AFTER_HYPER,
	TIE_SIM_PHASE_AFTER_REPLAY_VIEWER,
	TIE_SIM_PHASE_AFTER_MISSION,
	TIE_SIM_PHASE_AFTER_REPLAY_PROMPT,
	TIE_SIM_PHASE_AFTER_INFOROOM,
	TIE_SIM_PHASE_TEARDOWN,
} TieSimPhase;

typedef struct TieSimulatorTask {
	int replay_mode;
	TieSimPhase phase;
	/* Saved across the hyperspace cinematic so AFTER_HYPER can restore
	 * the player's draw-backdrop / draw-debris preferences after the
	 * cinematic stomps them. Only valid between INIT and AFTER_HYPER. */
	uint8_t saved_drawbackdrop;
	uint8_t saved_drawdebris;
	/* Synthetic-clock stamp of the tick that first presented the
	 * "Initializing Combat Sequence..." banner. LOADSCREEN_HOLD yields
	 * until TIE_SIM_LOADSCREEN_MIN_US have elapsed past it. */
	uint64_t loadscreen_shown_us;
} TieSimulatorTask;

void TieFlightRuntime_ReleaseRecoveredResources(void) {
	RenderTexture_ReleaseMissionCaches();
	TieRuntime_RequestFlightResourceRelease();
}

/* Minimum on-screen time for the loading banner. Retail never needed
 * one — visibility came from multi-second CD/floppy I/O covering the
 * mission + species load. Modern hosts load in milliseconds, so the
 * banner frame would be overdrawn by the first flight frame within the
 * same tick without an explicit hold. */
#define TIE_SIM_LOADSCREEN_MIN_US (2u * 1000u * 1000u)

/* RECOVERY HELPER: removes the identical TIE98 replay/live star-surface
 * initialization sequences in TIE_simulator. */
static void tie_prepare_star_surface_tie98(void) {
	Tie98StarColors_Invalidate();
	FlightSurface_Lock();
	worldeyeA1 = 0;
	worldeyeA2 = 0;
	worldeyeA3 = 0;
	worldeyeB1 = 0;
	worldeyeB2 = 0;
	worldeyeB3 = 0;
	worldeyeC1 = 0;
	worldeyeC2 = 0;
	worldeyeC3 = 0;
	pixelswide = 0;
	pixelsdeep = 0;
	rtsvga2_drawstars_tie98();
	FlightSurface_Unlock();
}

/* Common live-path mission setup, shared between the hyperspace and
 * no-hyperspace INIT branches. Builds the mission, seeds the gates /
 * train-level UI, prints the HYPER_COMPLETED / BONUS_PRIOR_LEVELS
 * messages, waits for the first XTIMER tick, and pushes the main
 * flight task. */
static void tie_reset_flight_timing_session_state(void) {
	TieFlightTiming_BeginSession(TieProfile_Flight());
	TieFlightTimingState_Reset();
	TieAiLead_Reset();
	TieChaseCamera_Reset();
}

static void tie_simulator_setup_mission_and_push(void) {
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (tie98_display)
		FlightSurface_Lock();
	create_createmission();
	tie_reset_flight_timing_session_state();
	if (tie98_display) {
		FlightSurface_Unlock();
		FrontendDisplay_BlitOffscreenToRenderSurface();
		FrontendDisplay_PresentFrame();
		FrontendDisplay_BlitOffscreenToRenderSurface();
	}
	/* Retail clears colorcycleuserflag unconditionally after the
	 * live mission is built, not just inside the hyperspace branch.
	 * The hyperspace path already cleared it; do it again here so a
	 * mission that skips the hyper-in still starts with cycling
	 * disabled. */
	colorcycleuserflag = 0;
	if (mission.train_craft_type) {
		if (tie98_display)
			FlightSurface_Lock();
		gate_createtraininggates();
		gate_settraininglevel(mission.train_level);
		if (tie98_display)
			FlightSurface_Unlock();
	}

	if (pstate.player_fg_idx < 48 && !fg_array[pstate.player_fg_idx].start_fg_used &&
		spec_data[pstate.player_spec_num].has_hyperdrive)
		msg_messageprintf(MSG_HYPER_COMPLETED);

	if (mission.train_craft_type && mission.train_level > 1u) {
		argtable[0] = (uint16_t)(mission.train_level - 1);
		msg_messageprintf(MSG_BONUS_PRIOR_LEVELS);
		/* RETAIL: seed mission.mission_score with the prior-levels
		 * bonus (10000 per level). Demo only printed the message. */
		mission.mission_score = 10000 * ((int)mission.train_level - 1);
	}

	/* Main flight loop is a task. The outer `while (mission.end_flag
	 * == 0)` is gone; the task runs tie_doframe per step and pops
	 * when end_flag is set. tie_doframe handles the active admission floor
	 * (the host's TieRuntime_Tick advances sim_clock, which xtimer reads
	 * via TieSimClockCursor on the first call to seed the cursor) and
	 * pause sub-state. Route through the wrapper so the snapshot's
	 * scene_kind gets tagged TIE_SCENE_FLIGHT for HD renderers — the
	 * direct landru_task_push call here used to skip that tag. */
	if (TieMusicPolicy_UsesTie98())
		tie_start_tie98_mission_music();
	tie_Push_FlightMission_Task();
}

// FUNCTION: TIE 0x55A60, TIE98 0x48CF10 (task-split recovery)
static LandruTaskStepResult tie_simulator_task_step(void* self) {
	TieSimulatorTask* t = (TieSimulatorTask*)self;

	switch (t->phase) {
		case TIE_SIM_PHASE_INIT: {
			tie_reset_flight_timing_session_state();
			/* PORT: loading and replay setup both own the selected flight
			 * version's framebuffer without starting the HD flight view.
			 * Stand-alone replay mode was selected explicitly by SHELL_Shell. */
			TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT_LOADING);
			const bool display_active = t->replay_mode
											? TieClassicDisplay_ActivateFlightMode((uint16_t)flightResolution)
											: TieClassicDisplay_ActivateFlight();
			if (!display_active) {
				lerror_Set_Landru_Error(12);
				TieFlightTiming_EndSession();
				return LANDRU_TASK_STEP_DONE;
			}
			const bool tie98_display = TieClassicDisplay_UsesDx5();
			/* --- Common engine init ------------------------------------ */
			deadflag_EB76C = 0; /* RETAIL-only init; no live reader */
			deadflag_EB774 = 1; /* RETAIL-only init (LOBYTE write) */
			maingameflag = 0;   /* moved up vs demo to match retail order */
			replaymaxcnt = (int32_t)TieFlightTiming_RecordFrameLimit();
			panels_in_ems = 0; /* PANEL_tryEMSforpanels later sets to 1 */
			blastcount = 0;
			g_engineSoundPreviousPlayerSpecies = -1;
			rotscale_invalidate_linedata(); /* drop any stale ROTSCALE line cache */
			replayspoolflag = 1;
			/* RETAIL: math2_setrandomseed() NOT called here (binary calls nullsub_3,
			 * a one-byte ret). The RNG seed survives from whatever called us. */
			/* math2_setrandomseed();   // demo-only */

			tie_initflightresolution();
			rtsvga2_blankVGA(); /* RETAIL-only */
			if (tie98_display)
				FlightSurface_Lock();
			rtsvga2_initgraphVGA();
			if (tie98_display) {
				FlightSurface_Unlock();
			}
			feinput_setupgraphics(3u); /* RETAIL: was 2 in demo */

			graphicsinit = 1;
			colorcycleflag = 1;
			palette_cycle_user = 1; /* RETAIL user "Color Cycling" option ON */
			colorcycleuserflag = 1;
			/* NB: musicflag/debugnum/soundinit/outputflag/blankcondition are NOT
			 * touched here. The demo writes them (=0/=1) as part of the init;
			 * retail does not — they keep whatever value they had at engine boot. */

			if (tie98_display) {
				maingameflag = 1;
				FlightSurface_Lock();
			}
			fediskio_Init_Buffers_and_Fonts();
			if (tie98_display) {
				FlightSurface_Unlock();
				maingameflag = 0;
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
			mapiconsloaded = 0; /* retail byte_CD1C5 = 0 between buffers
								   init and palette load; binary parity. */
			if (!TieProfile_UsesTie98Logic()) {
				fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "VGA.PAC", xtransdataptr);
				buildpalette(xtransdataptr, 64, 192);
			}
			if (tie98_display)
				FlightSurface_Lock();
			feinput_setupinputdevices();
			if (tie98_display)
				FlightSurface_Unlock();

			/* RETAIL-only: training-filename auto-detect. */
			if (special_features_flag) {
				if (missionfilename[0] == 't')
					mission.train_craft_type_src = 5;
				else
					mission.train_craft_type_src = 0;
			}

			/* Init 256 (idx, palette-delta) star pairs. The loop runs i = 0, 2, 4, ...
			 * because each pair is 2 bytes. Re-pick 'pos' until it lands in the
			 * 0..0x7C range (so the 5x5x5 grid lookup never overflows). */
			for (int16_t i = 0; i < 511; i += 2) {
				/* Retail TIE_simulator uses RAND_rand (standard LCG), NOT
				 * MATH2_getrandom (mission-RNG LFSR). The starfield positions
				 * must be non-deterministic w.r.t. mission replay state — stars
				 * are purely cosmetic and don't feed into sim/replay streams. */
				do {
					stars[i] = (uint8_t)(rand_rand() & 0x7F);
				} while (stars[i] > 0x7C);
				stars[i + 1] = (uint8_t)(rand_rand() & 3);
			}

			starcol1 = 0;
			lightX = 18000;
			lightY = -18000;
			/* TIE95 retail (0x55A60) uses lightZ = -18000; TIE98
			 * TIE_simulator (0x48D0B9) flips it to +18000. */
			lightZ = TieProfile_UsesTie98Logic() ? 18000 : -18000;
			colorcycleflag = 1;
			option_apply_options_cfg();
			shipdetailvalue = -1;
			shipdetailpolycnt = 16;

			if (t->replay_mode) {
				/* --- Replay-only path -------------------------------- */
				gamesnd_game_Open_iMuse(); /* RETAIL: was End_Transition in demo */
				hyperspaceflag = 0;
				entercombatflag = 0;
				colorcycleuserflag = 0; /* RETAIL-only extra reset */
				if (tie98_display)
					tie_prepare_star_surface_tie98();
				replayio_Push_ReplayScreen_Task();
				t->phase = TIE_SIM_PHASE_AFTER_REPLAY_VIEWER;
				return LANDRU_TASK_STEP_CONTINUE;
			}

			/* --- Live mission path ----------------------------------- */
			maingameflag = 1;
			fediskio_createpilotrecord();
			/* Reset the message-room ring before the mission starts. Retail
			 * writes word_C5888 = -1 (lasthistorymsg) and word_C588A = 0
			 * (numhistorymsgs) here so the msg-room starts empty for each
			 * flight. Without these clears, stale messages from the previous
			 * flight persist. */
			lasthistorymsg = -1;
			numhistorymsgs = 0;
			panelflag = 0;
			replayavailable = 0;

			if (tie98_display)
				FlightSurface_Lock();
			create_loadmission(missionfilename);
			if (tie98_display) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
			fediskio_loadspecies();
			gamesnd_game_Open_iMuse(); /* RETAIL: was End_Transition in demo */
			if (tie98_display)
				tie_prepare_star_surface_tie98();

			/* Loading is done, but the "Initializing Combat Sequence..."
			 * banner painted by fediskio_Init_Buffers_and_Fonts has not
			 * reached the screen yet — the application presents after this tick's
			 * step chain settles. YIELD (don't block) into LOADSCREEN_HOLD
			 * so the banner frame is presented, then hold it for a minimum
			 * display time while ticks keep driving the 0xFA palette pulse.
			 * Retail got this pause implicitly from slow CD I/O. */
			t->loadscreen_shown_us = TieSimClock_NowUs();
			t->phase = TIE_SIM_PHASE_LOADSCREEN_HOLD;
			return LANDRU_TASK_STEP_YIELD;
		}

		case TIE_SIM_PHASE_LOADSCREEN_HOLD: {
			if (TieSimClock_NowUs() - t->loadscreen_shown_us < TIE_SIM_LOADSCREEN_MIN_US)
				return LANDRU_TASK_STEP_YIELD;

			/* Hyperspace-in cinematic gate: run the warp-in sequence when the
			 * player's FG hasn't already started AND transitions are on AND
			 * the player's craft has a hyperdrive. */
			if (pstate.player_fg_idx < 48 && !fg_array[pstate.player_fg_idx].start_fg_used &&
				transitions_on && spec_data[pstate.player_spec_num].has_hyperdrive) {
				t->saved_drawbackdrop = drawbackdropflag;
				t->saved_drawdebris = drawdebrisflag;

				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Lock();
				create_createhyperin();
				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Unlock();
				hyperspaceflag = 2;
				colorcycleuserflag = 0;
				drawbackdropflag = 0;
				drawdebrisflag = 0;
				anim_dohyperspace();
				TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
				(void)landru_task_push(&flight_hyper_task_vt);
				t->phase = TIE_SIM_PHASE_AFTER_HYPER;
				return LANDRU_TASK_STEP_CONTINUE;
			}

			/* No hyperspace cinematic: jump straight to mission setup. */
			tie_simulator_setup_mission_and_push();
			t->phase = TIE_SIM_PHASE_AFTER_MISSION;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case TIE_SIM_PHASE_AFTER_REPLAY_VIEWER:
			/* Replay viewer task popped. Replay branch skips the live
			 * end-of-mission UI block; jump straight to TEARDOWN. */
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_HYPER:
			/* Hyperspace cinematic finished; restore the player's draw
			 * preferences, clear the hyperspace-tunnel particle 'used'
			 * flags, reload the mission cleanly, then run mission setup
			 * and push the main flight task. */
			drawbackdropflag = t->saved_drawbackdrop;
			drawdebrisflag = t->saved_drawdebris;

			/* Clear the 0x10 'used' flag on the hyperspace-tunnel particle
			 * species (114/115/116) so the next mission starts clean. */
			species_table[114].load_flags &= ~0x10u;
			species_table[115].load_flags &= ~0x10u;
			species_table[116].load_flags &= ~0x10u;
			hyperspaceflag = 0;

			/* Reload the mission cleanly. RETAIL passes 4 args; the
			 * extras are init-overrides which the demo path ignores. */
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			create_loadmission(missionfilename);
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Unlock();

			tie_simulator_setup_mission_and_push();
			t->phase = TIE_SIM_PHASE_AFTER_MISSION;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_MISSION:
			/* End-of-mission state. */
			if (TieMusicPolicy_UsesTie98())
				CDAUDIO_Close_Device();
			if (mission.player_status < 10u || mission.end_flag == 2) {
				/* Binary dereferences player_craft unconditionally here. */
				pstate.post_mission_shield_q4 =
					(uint8_t)shield_quartile(pstate.player_craft->hull_damage, pstate.player_craft->hull_max);
				if (mission.end_flag == 2)
					mission.player_status = 3;
				blank();

				if (replayavailable) {
					/* Replay prompt is now a sub-task: push it and yield;
					 * AFTER_REPLAY_PROMPT runs the post-prompt info-room +
					 * pilot-record update once the prompt (and any chained
					 * replay viewer it pushed) pops. */
					uint16_t saved_vol = (uint16_t)imuse_get_master_vol(im);
					view_replay_prompt_Push_Task(saved_vol);
					t->phase = TIE_SIM_PHASE_AFTER_REPLAY_PROMPT;
					return LANDRU_TASK_STEP_CONTINUE;
				}

				if (mission.train_craft_type == 0) {
					/* Push the in-flight info task and yield; the
					 * AFTER_INFOROOM phase resumes once it pops to run
					 * the post-info blank + pilot-record update. */
					user_Push_InflightInfo_Task(0);
					t->phase = TIE_SIM_PHASE_AFTER_INFOROOM;
					return LANDRU_TASK_STEP_CONTINUE;
				}
				if (mission.player_status == 3)
					fediskio_updatepilotrecord(0, 0);
			} else {
				blank();
			}
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_REPLAY_PROMPT:
			/* Replay prompt task popped (and any replay viewer it chained
			 * has popped too — both are nested below us on the task
			 * stack). Run the post-prompt block from AFTER_MISSION's
			 * synchronous tail: optional info-room push, optional pilot-
			 * record update. */
			if (mission.train_craft_type == 0) {
				user_Push_InflightInfo_Task(0);
				t->phase = TIE_SIM_PHASE_AFTER_INFOROOM;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			if (mission.player_status == 3)
				fediskio_updatepilotrecord(0, 0);
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_INFOROOM:
			blank();
			if (mission.player_status == 3)
				fediskio_updatepilotrecord(0, 0);
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_TEARDOWN:
			/* --- Tear-down ----------------------------------------- */
			TieFlightTiming_EndSession();
			imuse_stop_all_sounds(im);
			imuse_filelist_unload_all(im);
			if (TieProfile_UsesTie98Logic()) {
				colorcycleflag = 0;
				fediskio_FreeFlightHandles();
				TieFlightRuntime_ReleaseRecoveredResources();
				maingameflag = 0;
				gamesnd_Transition_Sound();
				if (!TieClassicDisplay_ActivateFrontend()) {
					lerror_Set_Landru_Error(12);
					return LANDRU_TASK_STEP_DONE;
				}
				rtsvga2_clearflightdisplay();
			} else {
				gamesnd_Transition_Sound();
				colorcycleflag = 0;
				fediskio_FreeFlightHandles();
				TieFlightRuntime_ReleaseRecoveredResources();
				/* PORT: TIE95 has no display object selected by maingameflag.
				 * Clear it before restoring a TIE98 frontend's Landru surface. */
				if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
					maingameflag = 0;
				if (!TieClassicDisplay_ActivateFrontend()) {
					lerror_Set_Landru_Error(12);
					return LANDRU_TASK_STEP_DONE;
				}
			}
			return LANDRU_TASK_STEP_DONE;
	}

	/* Unreachable; keeps -Wreturn-type happy. */
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable tie_simulator_task_vt = {
	.step = tie_simulator_task_step,
};

void tie_Push_Simulator_Task(int replay_mode) {
	TieSimulatorTask* t = (TieSimulatorTask*)landru_task_push(&tie_simulator_task_vt);
	if (!t)
		return;
	t->replay_mode = replay_mode;
	t->phase = TIE_SIM_PHASE_INIT;
}
