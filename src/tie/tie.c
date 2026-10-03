/* Flight-engine globals and per-frame driver. */

#include "tie/tie.h"
#include "tie_runtime/audio/imuse_api.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/flight_requests.h"
#include "tie_runtime/runtime/flight_task.h"
#include "tie_runtime/runtime/inflight_info_task.h"
#include "tie_runtime/runtime/replay_session_task.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/timing/sim_clock.h"
#endif
#include "tie/anim.h"
#include "tie/backdrp2.h"
#include "tie/cdaudio_tie98.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/dynamix.h"
#include "tie/edition.h"
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
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/diagnostics/flight_trace.h"
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
#include "tie_runtime/runtime/wide_arithmetic.h"
#include "tie_runtime/snapshot/snapshot_billboards.h" /* SNAPSHOT-ONLY billboard capture drain */
#include "tie_runtime/snapshot/snapshot_flight.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/ai_lead.h"
#include "tie_runtime/timing/chase_camera.h"
#include "tie_runtime/timing/flight_cadence.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/replay_timing.h"
#include "util/binio.h"

#ifndef TIE_MODERN
#include <conio.h>
#else
#include <landru/task.h>
#endif
#include <imuse/filelist.h>
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <landru/error.h>
#include <landru/vesa.h>
#include <stdlib.h>

/* --- Struct arrays --- */

/* species_table owned by species.c now (it has the static initializer
 * extracted from the binary's _species table). */
// GLOBAL: TIE95 0xE6394
CraftData crafts[NUM_CRAFTS];
// GLOBAL: TIE95 0xE38BC
FlightObject objects[NUM_OBJECTS];
// GLOBAL: TIE95 0xEA6D4
// GLOBAL: TIE98 0x596700
StaticObject staticobjects[NUM_STATIC_OBJECTS];
// GLOBAL: TIE95 0xEB70C
// GLOBAL: TIE98 0x5926A6
uint16_t framerate = 20; /* populated by the frame-pacer; fallback value avoids divide-by-zero before init */
// GLOBAL: TIE95 0xEB712
uint16_t frameticks;

/* Rendering pipeline scratch (set by pai/fview each frame). */
// GLOBAL: TIE95 0xEB0E4
int32_t rotatedx;
// GLOBAL: TIE95 0xEB0E0
int32_t rotatedy;
// GLOBAL: TIE95 0xEB0E8
int32_t rotatedz;
// GLOBAL: TIE95 0xEAC38
int32_t craftmoveX;
// GLOBAL: TIE95 0xEAC3C
int32_t craftmoveY;
// GLOBAL: TIE95 0xEAC34
int32_t craftmoveZ;

/* spec_data[] lives in spec.c (its watdbg owning module). */
// GLOBAL: TIE95 0xEB29C
// GLOBAL: TIE98 0x592240
RUNTIME_MissionState mission;

// GLOBAL: TIE95 0xEB72E
int16_t fileerror;

/* --- Display --- */

// GLOBAL: TIE95 0xEB148
// GLOBAL: TIE98 0x59190C
uint32_t screenXRes;
// GLOBAL: TIE95 0xEB14C
// GLOBAL: TIE98 0x591E34
uint32_t screenYRes;
/* Required before the first same-mode framebuffer allocation. */
// GLOBAL: TIE95 0xCD17C
int32_t bytesPerPixel = 1;
// GLOBAL: TIE95 0xCD180
int16_t flightResolution;

/* The linear framebuffer disables bank switching with an unbounded page. */
// GLOBAL: TIE95 0xCD170
uint32_t vesa_page_size = 0xFFFFFFFFu;
// GLOBAL: TIE95 0xCD178
uint8_t vesa_window;
// GLOBAL: TIE95 0xEB150
int32_t screenMemWidth;

/* --- Ship-render context (set by DRAW_Lockshipfileptrs). watdbg owner: tie.c. --- */
// GLOBAL: TIE95 0xEB284
struct ShipModelData* shipimageptr;
// GLOBAL: TIE95 0xEB27C
struct ShipModelData* objectblockptr;
// GLOBAL: TIE95 0xEB280
struct ShipModelMesh* componentblockptr;
// GLOBAL: TIE95 0xEB28C
CraftData* craftptr;
// GLOBAL: TIE95 0xEB730
int16_t shipdetailvalue;
// GLOBAL: TIE95 0xEB734
uint16_t shipdetailpolycnt;

/* --- World-to-eye rotation matrices. watdbg owner: tie.c. --- */
// GLOBAL: TIE95 0xEAC0C
int32_t rotworldeyeA1;
// GLOBAL: TIE95 0xEAC14
int32_t rotworldeyeA2;
// GLOBAL: TIE95 0xEAC10
int32_t rotworldeyeA3;
// GLOBAL: TIE95 0xEAC18
int32_t rotworldeyeB1;
// GLOBAL: TIE95 0xEABCC
int32_t rotworldeyeB2;
// GLOBAL: TIE95 0xEABC8
int32_t rotworldeyeB3;
// GLOBAL: TIE95 0xEABB8
int32_t rotworldeyeC1;
// GLOBAL: TIE95 0xEABC4
int32_t rotworldeyeC2;
// GLOBAL: TIE95 0xEABBC
int32_t rotworldeyeC3;

/* Perspective-projection constants (set by TIE_InitFlightResolution per
 * selected flight resolution). */
// GLOBAL: TIE95 0xEB771
// GLOBAL: TIE98 0x5926D4
uint8_t perspShift;
// GLOBAL: TIE95 0xEB13C
// GLOBAL: TIE98 0x5A272C
int32_t perspFactor;
// GLOBAL: TIE95 0xEB140
// GLOBAL: TIE98 0x590AF8
int32_t halfPerspFactor;

/* Master enable for the skybox backdrop renderer. */
// GLOBAL: TIE95 0xEB775
uint8_t drawbackdropflag;

/* --- calc-frame rotation rows. tie.c. --- */
// GLOBAL: TIE95 0xEAC1C
int32_t calcS1;
// GLOBAL: TIE95 0xEAC20
int32_t calcS2;
// GLOBAL: TIE95 0xEAC24
int32_t calcS3;
// GLOBAL: TIE95 0xEABF4
int32_t calcf1;
// GLOBAL: TIE95 0xEABFC
int32_t calcf2;
// GLOBAL: TIE95 0xEABF8
int32_t calcf3;
// GLOBAL: TIE95 0xEABDC
int32_t calcU1;
// GLOBAL: TIE95 0xEABE4
int32_t calcU2;
// GLOBAL: TIE95 0xEABE0
int32_t calcU3;

/* --- Current-craft orientation rows. tie.c. --- */
// GLOBAL: TIE95 0xEAC00
int32_t craftS1;
// GLOBAL: TIE95 0xEAC04
int32_t craftS2;
// GLOBAL: TIE95 0xEAC08
int32_t craftS3;
// GLOBAL: TIE95 0xEAC28
int32_t craftf1;
// GLOBAL: TIE95 0xEAC2C
int32_t craftf2;
// GLOBAL: TIE95 0xEAC30
int32_t craftf3;
// GLOBAL: TIE95 0xEABE8
int32_t craftU1;
// GLOBAL: TIE95 0xEABEC
int32_t craftU2;
// GLOBAL: TIE95 0xEABF0
int32_t craftU3;

/* --- World/camera state. The 408-byte _camera struct from the binary
 * (see Camera typedef in tie.h) is owned here. replaycam lives in
 * replay.c. --- */
// GLOBAL: TIE95 0xE2D8C
// GLOBAL: TIE98 0x590960
Camera camera;

// GLOBAL: TIE95 0xEB0C4
int32_t worldlocy;
// GLOBAL: TIE95 0xEB0C8
int32_t worldlocx;
// GLOBAL: TIE95 0xEB0D0
int32_t worldlocz;
// GLOBAL: TIE95 0xEAC48
int32_t worldx;
// GLOBAL: TIE95 0xEAC44
int32_t worldy;
// GLOBAL: TIE95 0xEAC40
int32_t worldz; /* watdbg-owned by tie.c */
// GLOBAL: TIE95 0xEB72C
uint16_t yAspect; /* watdbg-owned by tie.c; 0 = square pixels */
// GLOBAL: TIE95 0xEB6AE
int16_t objectsize;
// GLOBAL: TIE95 0xEB75F
// GLOBAL: TIE98 0x5A2748
uint8_t gouraudflag;
// GLOBAL: TIE95 0xEAC54
int32_t objecteyey;
// GLOBAL: TIE95 0xEAC58
int32_t objecteyex;
// GLOBAL: TIE95 0xEAC5C
int32_t objecteyez;

/* --- Swept-segment globals for collision pipeline. tie.c. --- */
// GLOBAL: TIE95 0xEAB94
int32_t laserx;
// GLOBAL: TIE95 0xEAB98
int32_t lasery;
// GLOBAL: TIE95 0xEAB90
int32_t laserz;
// GLOBAL: TIE95 0xEABD4
int32_t laserxold;
// GLOBAL: TIE95 0xEABD8
int32_t laseryold;
// GLOBAL: TIE95 0xEABD0
int32_t laserzold;
// GLOBAL: TIE95 0xEABA8
int32_t craftx;
// GLOBAL: TIE95 0xEABAC
int32_t crafty;
// GLOBAL: TIE95 0xEAB80
int32_t craftz;
// GLOBAL: TIE95 0xEABB0
int32_t craftxold;
// GLOBAL: TIE95 0xEABB4
int32_t craftyold;
// GLOBAL: TIE95 0xEABC0
int32_t craftzold;
// GLOBAL: TIE95 0xEAB58
int32_t gatex1;
// GLOBAL: TIE95 0xEAB60
int32_t gatey1;
// GLOBAL: TIE95 0xEAB68
int32_t gatez1;
// GLOBAL: TIE95 0xEAB5C
int32_t gatex2;
// GLOBAL: TIE95 0xEAB64
int32_t gatey2;
// GLOBAL: TIE95 0xEAB6C
int32_t gatez2;
// GLOBAL: TIE95 0xEAB7C
// GLOBAL: TIE98 0x592694
int32_t collidexoff;
// GLOBAL: TIE95 0xEAB78
// GLOBAL: TIE98 0x592238
int32_t collideyoff;
// GLOBAL: TIE95 0xEAB74
// GLOBAL: TIE98 0x592200
int32_t collidezoff;

/* --- Targeting / damage state. tie.c. --- */
// GLOBAL: TIE95 0xEB724
uint16_t bluetarget;
// GLOBAL: TIE95 0xEB71E
uint16_t currenttarget;
// GLOBAL: TIE95 0xEB718
uint16_t currenttargetcomp;
// GLOBAL: TIE95 0xEB732
// GLOBAL: TIE98 0x5A2744
int16_t drawmarkingsflag;

/* --- Sound/input flags --- */

// GLOBAL: TIE95 0xEB769
// GLOBAL: TIE98 0x59271D
uint8_t musicenabled;
// GLOBAL: TIE95 0xEB76D
uint8_t voiceenabled;
// GLOBAL: TIE95 0xEB773
uint8_t sfxenabled;

/* --- Flight engine state --- */

// GLOBAL: TIE95 0xEB763
// GLOBAL: TIE98 0x596218
uint16_t maingameflag;
// GLOBAL: TIE95 0xCD16E
// GLOBAL: TIE98 0x58A264
uint8_t cheatingflag;

/* --- Mission data --- */

// GLOBAL: TIE95 0xDEDCC
FGStatus fgstatus[48];
// GLOBAL: TIE95 0xDF6CC
EFGStruct fg_array[48];

/* Authoritative player state serialized by replay slot 8. Its native
 * pointers make host replay files pointer-width dependent. */
// GLOBAL: TIE95 0xEB158
PlayerInFlightState pstate;
#ifdef TIE_MODERN
/* The port appends axis_roll_accum after the original 0x124 bytes. */
typedef char CheckPlayerInFlightStateSize[sizeof(pstate) == 294 + 2 * (sizeof(void*) - 4) ? 1 : -1];
#else
typedef char CheckPlayerInFlightStateSize[sizeof(pstate) == 292 + 2 * (sizeof(void*) - 4) ? 1 : -1];
#endif

// GLOBAL: TIE95 0xE3534
MissionFile mission_file_header;

// GLOBAL: TIE95 0xEB71A
// GLOBAL: TIE98 0x591E4C
uint16_t idnumber; /* monotonic per-craft id */
// GLOBAL: TIE95 0xEB744
// GLOBAL: TIE98 0x591E10
uint16_t currentdebrisslot = DEBRIS_FIRST_SLOT; /* cycled 112..119 (retail) by checkdebris */
// GLOBAL: TIE95 0xEB740
uint16_t missionversion; /* .TIE file version (0 = legacy) */
// GLOBAL: TIE95 0xEB710
uint16_t baseframerate = 20; /* mission base framerate (seeded by xtimer) */
// GLOBAL: TIE95 0xEB71C
// GLOBAL: TIE98 0x591E40
uint16_t tickcounter;
// GLOBAL: TIE95 0xEB720
// GLOBAL: TIE98 0x592678
uint16_t targetblinkstate;
// GLOBAL: TIE95 0xEB726
// GLOBAL: TIE98 0x5A26DA
int16_t targetblinkflag;
// GLOBAL: TIE95 0xEB6B2
uint16_t fullupdateflag;
// GLOBAL: TIE95 0xEB75D
uint8_t hyperspaceflag;
// GLOBAL: TIE95 0xEB765
// GLOBAL: TIE98 0x596644
uint8_t hyperabortflag;

/* Hyperspace timing + state -- driven by anim_dohyperspace.
 *   hyperticks       -- cumulative frameticks since the warp started
 *   hyperstarlength  -- scratch used during the streak stretch/shrink math
 *   hypertemp1/2     -- saved drawbackdropflag / drawdebrisflag (restored
 *                       at the end of phase 5 so the post-warp scene comes
 *                       back with the same backdrop config) */
// GLOBAL: TIE95 0xEB722
// GLOBAL: TIE98 0x592212
uint16_t hyperticks;
// GLOBAL: TIE95 0xEB716
uint16_t hyperstarlength;
// GLOBAL: TIE95 0xEB72A
uint16_t hypertemp1;
// GLOBAL: TIE95 0xEB728
uint16_t hypertemp2;

/* drawdebrisflag -- enables the parallax-debris layer in BACKDRP2/CREATE.
 * Cleared to 0 during the hyperspace warp; restored from hypertemp2 in
 * phase 5. Owned by tie.c per watdbg. */
// GLOBAL: TIE95 0xEB776
// GLOBAL: TIE98 0x5918F5
uint8_t drawdebrisflag;

/* timers[20] -- the global cooldown bank. tie_updatetime decrements every
 * non-zero slot by frameticks and clamps to zero. Slots are named via the
 * TimerSlot enum in tie.h; consumers reset their slot to a tick count
 * (e.g. timers[TIMER_ANIM_UPDATE] = 29). */
// GLOBAL: TIE95 0xEB75C
// GLOBAL: TIE98 0x5973A0
uint8_t calcframerate;
// GLOBAL: TIE95 0xEB75E
uint8_t entercombatflag;
// GLOBAL: TIE95 0xE3894
// GLOBAL: TIE98 0x595E00
int16_t timers[20];

/* REPLAY module globals -- watdbg-owned by tie.c. PANEL_updatereplaystuff
 * consumes recordingreplay / replaypercent / replaytotalcnt / replaymaxcnt
 * for the cockpit REC LED + %-remaining readout. replay.c / replayio.c
 * consume the rest. */
// GLOBAL: TIE95 0xEB6A6
uint16_t replaypercent;
// GLOBAL: TIE95 0xEB6CC
// GLOBAL: TIE98 0x5A2734
int16_t recordingreplay;
// GLOBAL: TIE95 0xEAC50
// GLOBAL: TIE98 0x592684
int32_t replaytotalcnt;
// GLOBAL: TIE95 0xEAC60
int32_t replaymaxcnt;
// GLOBAL: TIE95 0xCD1F0
// GLOBAL: TIE98 0x4F2B70
char replayclipname[14];
// GLOBAL: TIE95 0xCD1DC
// GLOBAL: TIE98 0x4F2B50
char replaystartfile[10] = "start.rpy";
// GLOBAL: TIE95 0xCD1CF
char replaysavegamefile[13] = "savegame.rpy";
// GLOBAL: TIE95 0xCD1E6
char inputspoolfile[10] = "input.spl";
// GLOBAL: TIE95 0xEAC68
// GLOBAL: TIE98 0x591FE4
void* replayptr; /* write/read cursor into replaybuffer. */
// GLOBAL: TIE95 0xEB6AA
uint16_t replaybuffercnt; /* frames in the current 3071-slot page. */
// GLOBAL: TIE95 0xEAC4C
uint32_t replaytotalcntdown; /* playback counter (counts up toward replaytotalcnt). */
// GLOBAL: TIE95 0xEB6AC
uint16_t replayrandomseed;
// GLOBAL: TIE95 0xEB6C4
int16_t replayviewmode;
// GLOBAL: TIE95 0xEB74C
uint8_t replayspoolflag; /* 1 = auto-spool to disk when buffer fills. */
// GLOBAL: TIE95 0xEB74F
uint8_t endgamereplayflag;
// GLOBAL: TIE95 0xEB751
// GLOBAL: TIE98 0x591FD8
uint8_t updateactionflag; /* 1 = replay is actively advancing */
// GLOBAL: TIE95 0xEB6B4
// GLOBAL: TIE98 0x592210
uint16_t replayavailable; /* 1 if a saved film is loadable. */

/* lasttargetnum is owned by panel.c per watdbg; declared extern in panel.h. */

/* Binary is u8 in both demo and retail. Readers only test for nonzero
 * and decrement, so the narrower type matches. */
// GLOBAL: TIE95 0xEB770
// GLOBAL: TIE98 0x5A26A8
uint8_t acceleratedtimectr;
// GLOBAL: TIE95 0xEB736
int16_t hyperspacedetail;

/* Shield LED flash toggle (set by COLLIDE_damagecraft on hit; read by
 * PANEL_updateshields). 0 = fwd flash, 1 = rear flash. Owned by tie.c.
 * Single byte in the binary (byte_EB75B); paired with timers[TIMER_SHIELD_FLASH]. */
// GLOBAL: TIE95 0xEB75B
uint8_t shieldblink;

/* MissionClock — the single 8-byte wall-clock storage. See the typedef
 * comment in tie.h for the layout and tick semantics. */
// GLOBAL: TIE95 0xE6384
// GLOBAL: TIE98 0x596210
MissionClock date;

/* Mission time-limit countdown (watdbg _timeleft[8], owned by tie.c).
 * Captured wholesale by the replay state-dump. */
// GLOBAL: TIE95 0xE638C
// GLOBAL: TIE98 0x5926C8
MissionClock timeleft;

/* TieStorage_Open(3) modes embedded in the binary as const char arrays; owned by tie.c
 * per watdbg. C stdlib fopen treats the first two chars the same way here. */
// GLOBAL: TIE95 0xCD1C6
const char readmode[3] = { 'r', 'b', '\0' };
// GLOBAL: TIE95 0xCD1C9
const char _writemode[3] = { 'w', 'b', '\0' };
// GLOBAL: TIE95 0xCD1CC
const char _appendmode[3] = { 'a', 'b', '\0' };

/* Rendering scratch (owned by tie.c per watdbg). maxPixelsDeep is set by
 * tie_InitFlightResolution per video mode; numbitmaps and lightflag are
 * written each frame by the 3D pipeline (anim.c / draw.c / xtrans2.c). */
// GLOBAL: TIE95 0xEB154
int32_t maxPixelsDeep;
// GLOBAL: TIE95 0xEB6A8
// GLOBAL: TIE98 0x591C24
int16_t numbitmaps;
// GLOBAL: TIE95 0xEB74A
uint8_t lightflag;

/* Squared distance scratch written by pai_roughdistancebetween and
 * consumed by PANEL_addbliptoradar for the distance-fade color step.
 * Owned by tie.c per watdbg. */
// GLOBAL: TIE95 0xEB0EC
int32_t roughdistance;
// GLOBAL: TIE95 0xEB70E
uint16_t messageside; /* sampled by MSG_*message writers */
// GLOBAL: TIE95 0xDED54
// GLOBAL: TIE98 0x5A2688
uint16_t argtable[4]; /* 0xED560 -- '*' and '&N' substitution slots */
// GLOBAL: TIE95 0xEB6BE
// GLOBAL: TIE98 0x595FD8
uint16_t messageloghandle; /* 32000-byte message-history handle */
/* (pstate.space_confirm_action: 1=laser-warn ack, 2=abort mission,
 * 3=accept penalty; driven by timers[TIMER_SPACE_CONFIRM] decay in
 * msg_messageupdate.) */
// GLOBAL: TIE95 0xE2F24
uint8_t radiomsg[1440];
// GLOBAL: TIE95 0xE34C4
EMissionGoal cut[4];

/* Approximate distance scratch -- last collide_roughdistance3d result.
 * Owned by tie.c per watdbg (segment 2 offset 0x2899C). */
// GLOBAL: TIE95 0xEAB70
int32_t approxdist;

/* (mission elapsed hr/min/sec previously lived as standalone globals here;
 * they are now `date.hour` / `date.minute` / `date.second`, fields of
 * the single MissionClock storage above.) */

/* (pstate.target_obj_idx: currently-targeted object slot; written by
 * USER_inputforplane and PANEL_*, read by collide_collisions for the
 * friendly-tag check.) */

/* --- Input state (read/written by FEINPUT, consumed by screen modules) --- */

// GLOBAL: TIE95 0xEB6DE
// GLOBAL: TIE98 0x595F64
int16_t inputbuttons;
// GLOBAL: TIE95 0xEB6E8
// GLOBAL: TIE98 0x59223E
int16_t inputkey;
// GLOBAL: TIE95 0xEB6D2
int16_t inputdeltax;
// GLOBAL: TIE95 0xEB6CE
// GLOBAL: TIE98 0x595FE4
int16_t inputdeltay;
// GLOBAL: TIE95 0xEB6DC
int16_t mouseflag;
// GLOBAL: TIE95 0xEB708
int16_t joystickflag;
// GLOBAL: TIE95 0xEB6F8
int16_t joystickx;
// GLOBAL: TIE95 0xEB6F4
int16_t joysticky;
// GLOBAL: TIE95 0xEB6FC
int16_t joybuttons;
// GLOBAL: TIE95 0xEB6D8
int16_t mousebuttons;
// GLOBAL: TIE95 0xEB6FA
// GLOBAL: TIE98 0x591E2E
int16_t keypress;
// GLOBAL: TIE95 0xEB6E2
int16_t deltamx;
// GLOBAL: TIE95 0xEB6E0
int16_t deltamy;
// GLOBAL: TIE95 0xEB6D6
int16_t mousex;
// GLOBAL: TIE95 0xEB6D4
int16_t mousey;
// GLOBAL: TIE95 0xEB6DA
int16_t joystickcount;
// GLOBAL: TIE95 0xEB772
uint8_t graphicsmode;
#ifdef TIE_MODERN
int16_t detaillevel; /* widened for the host replay format */
#else
// GLOBAL: TIE95 0xCD16C
uint8_t detaillevel;
#endif

// GLOBAL: TIE95 0xEAB54
// GLOBAL: TIE98 0x595F70
void* viewfilmstr;

/* --- FESTRING text output globals --- */

// GLOBAL: TIE95 0xEB702
// GLOBAL: TIE98 0x5A26D8
int16_t cursorx;
// GLOBAL: TIE95 0xEB6FE
// GLOBAL: TIE98 0x5A26D0
int16_t cursory;
// GLOBAL: TIE95 0xEB704
// GLOBAL: TIE98 0x5A26EA
int16_t topmargin;
// GLOBAL: TIE95 0xEB706
// GLOBAL: TIE98 0x5A26CC
int16_t bottommargin;
// GLOBAL: TIE95 0xEB70A
// GLOBAL: TIE98 0x5918E0
int16_t leftmargin;
// GLOBAL: TIE95 0xEB700
// GLOBAL: TIE98 0x5926D0
int16_t rightmargin;
// GLOBAL: TIE95 0xEB754
// GLOBAL: TIE98 0x591D8A
uint8_t textcolor;
// GLOBAL: TIE95 0xEB756
// GLOBAL: TIE98 0x5A26CF
uint8_t backcolor;
// GLOBAL: TIE95 0xEB757
// GLOBAL: TIE98 0x595DE2
uint8_t dropcolor;
// GLOBAL: TIE95 0xEB758
// GLOBAL: TIE98 0x595F5F
uint8_t dropflag;
// GLOBAL: TIE95 0xEB6F2
// GLOBAL: TIE98 0x592206
int16_t lwrapflag;
// GLOBAL: TIE95 0xEB6F0
// GLOBAL: TIE98 0x596BA4
int16_t autofillflag;
// GLOBAL: TIE95 0xEB6EE
// GLOBAL: TIE98 0x5A269C
int16_t flight_text_reserved_flag;
// GLOBAL: TIE95 0xEB75A
// GLOBAL: TIE98 0x5926A0
uint8_t fontflag;
// GLOBAL: TIE95 0xEB755
// GLOBAL: TIE98 0x590E6D
uint8_t fontheight;
// GLOBAL: TIE95 0xEB6F6
int16_t fontcharsize;
// GLOBAL: TIE95 0xEB759
uint8_t fontlowercase;
// GLOBAL: TIE95 0xEAC70
void* curfontptr;
// GLOBAL: TIE95 0xDED7C
char tempstring[40];
// GLOBAL: TIE95 0xDEDA4
char temp2string[40];

/* Graphics function pointers (assigned by FEINPUT_SetGraphicsPtrs) */
// GLOBAL: TIE95 0xEB0D4
// GLOBAL: TIE98 0x59267C
void* initgraph;
// GLOBAL: TIE95 0xEB0FC
// GLOBAL: TIE98 0x591E50
void (*blank)(void);
// GLOBAL: TIE95 0xEB0F0
// GLOBAL: TIE98 0x591E3C
void (*unblank)(void);
// GLOBAL: TIE95 0xEB0CC
// GLOBAL: TIE98 0x59268C
void (*buildpalette)(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count);
// GLOBAL: TIE95 0xEB0D8
// GLOBAL: TIE98 0x5A26DC
void* savepalette;
// GLOBAL: TIE95 0xEB0F8
// GLOBAL: TIE98 0x596BA0
void* restorepalette;
// GLOBAL: TIE95 0xEB0C0
// GLOBAL: TIE98 0x591E44
uint32_t (*calcposition)(uint16_t, uint16_t);
// GLOBAL: TIE95 0xEB0DC
// GLOBAL: TIE98 0x59269C
void (*drawshape)(const void*, int16_t, int16_t, int16_t, uint16_t);
// GLOBAL: TIE95 0xEB0F4
// GLOBAL: TIE98 0x5918E4
void (*outchar)(int ch);
// GLOBAL: TIE95 0xEB100
// GLOBAL: TIE98 0x59222C
void (*clearwindow)(void);
// GLOBAL: TIE95 0xEB0B4
// GLOBAL: TIE98 0x595FE0
void (*fillbox)(uint16_t, uint16_t, uint16_t, uint16_t);
// GLOBAL: TIE95 0xEB0BC
// GLOBAL: TIE98 0x5926C4
void* savebox;
// GLOBAL: TIE95 0xEB0B8
// GLOBAL: TIE98 0x5A26C8
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
// GLOBAL: TIE95 0xC5810
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

// GLOBAL: TIE95 0xEAC74
// GLOBAL: TIE98 0x5A2750
uint8_t* farbufferptr;
/* Shape pointer table shared with maproom_swap_buffer_ptrs. */
// GLOBAL: TIE95 0xEAC80
uint8_t* farbufferptrs[265];
// GLOBAL: TIE95 0xEB0A4
// GLOBAL: TIE98 0x5918F8
void* fontptrtiny;
// GLOBAL: TIE95 0xEB0AC
// GLOBAL: TIE98 0x5A26AC
void* fontptrmicro;
// GLOBAL: TIE95 0xEAC7C
// GLOBAL: TIE98 0x591E48
void* newbuf;
// GLOBAL: TIE95 0xEAC64
void* xtransdataptr;
// GLOBAL: TIE95 0xEB0A8
void* loadbuffer;
// GLOBAL: TIE95 0xEAC6C
void* replaybufferstart;

/* --- Starfield source data (watdbg owner: tie.c). ---
 * stars[] packs 256 (index, palette-delta) pairs at stride 2: even bytes
 * are star indices (0..124 into stareye{x,y,z}), odd bytes are the
 * color-slot delta added to starcol1 to pick one of palette entries
 * 0xFC..0xFF via the clamp(starcol1 + delta, 0..3) - 4 formula
 * in rtsvga2_drawstars. Initialized once at the start of each flight. */
// GLOBAL: TIE95 0xDE7C4
// GLOBAL: TIE98 0x592000
uint8_t stars[512];
// GLOBAL: TIE95 0xEB74B
// GLOBAL: TIE98 0x596648
uint8_t starcol1;

/* Palette-cycling state (watdbg owner: tie.c).
 *   colorcycleflag -- 0 while the engine is mid-palette-swap, 1 when
 *                     cycling is active (checked by the cycle timer
 *                     and by blankVGA/unblankVGA).
 *   blankcondition -- bit field. Bit 0 = fade-to-black active. */
// GLOBAL: TIE95 0xEB760
// GLOBAL: TIE98 0x596208
uint8_t colorcycleflag;
// GLOBAL: TIE95 0xEB762
// GLOBAL: TIE98 0x5A2736
uint8_t blankcondition;

/* Accelerated-game-clock gear shift (fediskio persists; xtimer uses it). */
// GLOBAL: TIE95 0xCD184
// GLOBAL: TIE98 0x4F2AD8
uint8_t acceleratedtimesetting;

/* Bitmap-draw queue populated by anim_add_bitmap_draw, consumed by
 * anim_sort_and_draw_bitmaps. numbitmaps is defined above; the array
 * itself is declared in anim.h. */
// GLOBAL: TIE95 0xDEB54
BitmapDrawEntry drawitems[ANIM_DRAWITEMS_MAX];

/* In-flight SFX scheduler (watdbg owner: tie.c). blastqueue is a FIFO of
 * pending blast/voice SFX slots processed by fsfx_updatesfx; blastflag is
 * set on enqueue and cleared by the scheduler; blastcount tracks the
 * number of outstanding entries. */
// GLOBAL: TIE95 0xEB764
uint8_t blastflag;
// GLOBAL: TIE95 0xEB761
// GLOBAL: TIE98 0x590E68
uint8_t blastcount;
// GLOBAL: TIE95 0xDED5C
// GLOBAL: TIE98 0x5A2700
uint8_t blastqueue[FSFX_BLAST_QUEUE_SIZE];

/* Warhead state table: one record per concurrent missile/torpedo. */
// GLOBAL: TIE95 0xE61FC
WarheadRecord warheads[NUM_WARHEAD_SLOTS];

/* VESA paging state. */
// GLOBAL: TIE95 0xCD174
uint32_t vesa_grains_per_page;

/* Starship LOD / explosion-LOD thresholds (written by user_updateflight). */
// GLOBAL: TIE95 0xEB73A
// GLOBAL: TIE98 0x596202
uint16_t starshipdetail;
// GLOBAL: TIE95 0xEB738
// GLOBAL: TIE98 0x595DE0
uint16_t starshipexplodetail;

/* Directional-light vector rotated into the current craft's local frame. */
// GLOBAL: TIE95 0xEAB9C
int32_t rotlightX;
// GLOBAL: TIE95 0xEABA0
int32_t rotlightY;
// GLOBAL: TIE95 0xEABA4
int32_t rotlightZ;
// GLOBAL: TIE98 0x4F2A70
int32_t g_localLightsEnabled = 1;
// GLOBAL: TIE95 0xEB144
int16_t thicknessMultiple;

/* Draw color used for training-gate silhouettes. */
// GLOBAL: TIE95 0xEB714
uint16_t gatecolor;

/* .TIE file/path scratch. The binary sizes the buffer at 64 bytes to hold
 * a full DOS directory + filename. */
// GLOBAL: TIE95 0xCD185
// GLOBAL: TIE98 0x4F2AE0
char missionfilename[64];

/* Front-end vs flight resolution selectors. */
// GLOBAL: TIE95 0xCD182
int16_t frontResolution;

/* --- TIE module per-frame engine driver state (defined here because
 * the binary places these in tie.c per watdbg). --- */

/* Map-room visibility latch. Set by USER_userinterface when the player
 * opens the in-flight map; cleared at the top of each TIE_doframe before
 * USER_userinterface runs. The post-userinterface check `!end_flag &&
 * !mapflag` is what suspends the world-render block while the map is up. */
// GLOBAL: TIE95 0xEB767
uint8_t mapflag;

/* Replay fast-forward UI state. fastforwardtimer (initialized to 236)
 * counts the ticks left before the next render frame; tie_doframe drains
 * it by frameticks each call. Reloaded with +236 when a render fires. */
// GLOBAL: TIE95 0xEB750
// GLOBAL: TIE98 0x591FD7
uint8_t fastforwardflag;
// GLOBAL: TIE95 0xEB6BC
// GLOBAL: TIE98 0x5926A2
int16_t fastforwardtimer;

/* Hyperspace-cinematic + reload-mission gate. Was the binary's
 * byte_D354C — set non-zero by SHELLEXT_loadprefs when "transitions" is
 * enabled in the player's preferences. tie_simulator only triggers
 * CREATE_createhyperin when this is set. */
// GLOBAL: TIE95 0xD354C
uint8_t transitions_on;

/* Retail-only special-features dword (binary's dword_D3548). Gate for
 * the training-filename auto-detect (set train_craft_type_src to 5 when the
 * mission filename starts with 't'). 0 in normal builds. */
// GLOBAL: TIE95 0xD3548
// GLOBAL: TIE98 0x58CA58
uint32_t special_features_flag;

/* (Mission elapsed clock `date` is defined above as a single
 * MissionClock storage; subsec is `date.subsec`.) */

/* Snapshot of XTIMER tickcounter taken at the top of tie_doframe (live
 * branch). Used to seed framerate / frameticks for this frame. */
// GLOBAL: TIE95 0xEB6A2
int16_t lastcounter;

/* HUD target-blink tick countdown. Decremented by frameticks per call to
 * tie_updatetime; on expiry toggles the targetblinkstate 0x400 bit and
 * picks a new tick interval (118 normal, 14 micro-flicker for too-small
 * targets). Owned by tie.c per watdbg. */
// GLOBAL: TIE95 0xEB73E
// GLOBAL: TIE98 0x591D82
int16_t blinkticks;

/* Engine-init/teardown latches set/cleared at mission boundaries by
 * tie_simulator. Each is a byte in the binary's data segment, owned by
 * tie.c per watdbg. Read by other modules to gate optional subsystems. */
// GLOBAL: TIE95 0xEB777
// GLOBAL: TIE98 0x596B9C
uint8_t musicflag; /* iMUSE script live? */
/* iMUSE per-frame evaluation state saved by the replay state dump.
 * tie_updatemusic reads/updates them each frame so
 * they persist across evaluations and are captured by the replay-state
 * dump. */
// GLOBAL: TIE95 0xEB73C
// GLOBAL: TIE98 0x596BB0
int16_t music_state = 1;
// GLOBAL: TIE95 0xEB742
// GLOBAL: TIE98 0x596B82
uint16_t music_intensity;
// GLOBAL: TIE95 0xEB768
uint8_t graphicsinit; /* video mode set + buffers allocated */
// GLOBAL: TIE95 0xEB748
// GLOBAL: TIE98 0x59266C
uint8_t colorcycleuserflag; /* user-side palette-cycling enable (transient) */
// GLOBAL: TIE95 0xEB746
uint8_t panelflag; /* cockpit-panel rendering enabled */

/* "Map icons loaded" latch (binary's byte_CD1C5 in retail, byte_DC409 in
 * demo). Cleared by tie_simulator between fediskio_Init_Buffers_and_Fonts
 * and fediskio_readfiletofarmemory so the first map-room open on a fresh
 * mission re-uploads the icon atlas. Other modules (MAP, PANEL) set it
 * after a successful load. Owned by tie.c per watdbg. */
// GLOBAL: TIE95 0xCD1C5
// GLOBAL: TIE98 0x58A284
uint8_t mapiconsloaded;

/* User-toggleable "Color Cycling" option (binary's byte_EB766). Persists
 * across missions, edited via OPTION_optionsroom (row 6 in the in-flight
 * options menu). GAMESND_Host_Int gates color cycling on
 *   (palette_cycle_user & colorcycleflag) || colorcycleuserflag
 * so this is the persistent user setting; colorcycleuserflag is the
 * transient runtime toggle. tie_simulator sets it to 1 at startup. */
// GLOBAL: TIE95 0xEB766
uint8_t palette_cycle_user;

/* Write-only simulator initialization flags. */
// GLOBAL: TIE95 0xEB76C
// GLOBAL: TIE98 0x59716C
uint8_t deadflag_EB76C;
// GLOBAL: TIE95 0xEB774
// GLOBAL: TIE98 0x5A269E
uint8_t deadflag_EB774;

/* Last iMUSE music state pushed by tie_updatemusic. Latched here so the
 * UI / debug overlays can read what's currently playing. */
// GLOBAL: TIE95 0xEB747
uint8_t lastmusicstate;

// GLOBAL: TIE98 0x596B80
static int cdmusic_kind;
// GLOBAL: TIE98 0x597180
static int cdmusic_switch_latched;
// GLOBAL: TIE98 0x595F60
static int32_t cdmusic_ms_remaining;
// GLOBAL: TIE98 0x591C20
static uint32_t cdmusic_last_ms;
// GLOBAL: TIE98 0x4F2B88
static const uint8_t cdmusic_start_min[4] = { 0, 4, 8, 12 };
// GLOBAL: TIE98 0x4F2B8C
static const uint8_t cdmusic_start_sec[4] = { 0, 1, 40, 52 };
// FUNCTION: TIE95 0x55A60
// FUNCTION: TIE98 0x48CF10
void tie_simulator(int replay_mode) {
	uint8_t saved_drawbackdrop;
	uint8_t saved_drawdebris;
	uint16_t saved_master_vol;
	bool post_mission_ui;
	int16_t i;
#ifdef TIE_MODERN
	TieSimulatorTask* continuation = landru_task_top();
	saved_drawbackdrop = continuation->saved_drawbackdrop;
	saved_drawdebris = continuation->saved_drawdebris;
	saved_master_vol = continuation->saved_master_vol;
	post_mission_ui = continuation->post_mission_ui;
	if (continuation->phase == TIE_SIM_PHASE_INIT)
#endif
	{
#ifdef TIE_MODERN
		if (!TieFlightRuntime_PrepareSimulator(replay_mode)) {
			continuation->next_step = LANDRU_TASK_STEP_DONE;
			return;
		}
#endif
		deadflag_EB76C = 0;
		deadflag_EB774 = 1;
		maingameflag = 0;
#ifdef TIE_MODERN
		replaymaxcnt = (int32_t)TieFlightTiming_RecordFrameLimit();
#else
		replaymaxcnt = 0x20000;
#endif
		panelsloadedflag = 0;
		blastcount = 0;
#ifdef TIE_MODERN
		g_engineSoundPreviousPlayerSpecies = -1;
#endif
		rotscale_linedata_built = 0;
		replayspoolflag = 1;

		tie_InitFlightResolution();
		rtsvga2_blankVGA();
		if (TIE_DISPLAY_DX5)
			FlightSurface_Lock();
		rtsvga2_initgraphVGA();
		if (TIE_DISPLAY_DX5) {
			FlightSurface_Unlock();
		}
		feinput_setupgraphics(3u);

		graphicsinit = 1;
		colorcycleflag = 1;
		palette_cycle_user = 1;
		colorcycleuserflag = 1;

		if (TIE_DISPLAY_DX5) {
			maingameflag = 1;
			FlightSurface_Lock();
		}
		fediskio_Init_Buffers_and_Fonts();
		if (TIE_DISPLAY_DX5) {
			FlightSurface_Unlock();
			maingameflag = 0;
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		mapiconsloaded = 0;
		if (!TIE_FLIGHT_TIE98) {
			fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "VGA.PAC", xtransdataptr);
			buildpalette(xtransdataptr, 64, 192);
		}
		if (TIE_DISPLAY_DX5)
			FlightSurface_Lock();
		feinput_setupinputdevices();
		if (TIE_DISPLAY_DX5)
			FlightSurface_Unlock();

		if (special_features_flag) {
			if (missionfilename[0] == 't')
				mission.train_craft_type_src = 5;
			else
				mission.train_craft_type_src = 0;
		}

		for (i = 0; i < 511; i += 2) {

			do {
				stars[i] = (uint8_t)(rand() & 0x7F);
			} while (stars[i] > 0x7C);
			stars[i + 1] = (uint8_t)(rand() & 3);
		}

		starcol1 = 0;
		lightX = 18000;
		lightY = -18000;

		lightZ = TIE_FLIGHT_EDITION(-18000, 18000);
		colorcycleflag = 1;
#ifdef TIE_MODERN
		TieInflightOptions_Apply();
#else
		option_optionsroom(1);
#endif
#ifdef TIE_MODERN
		shipdetailvalue = -1;
		shipdetailpolycnt = 16;
#endif

		if (replay_mode) {
			gamesnd_game_Open_iMuse();
			hyperspaceflag = 0;
			entercombatflag = 0;
			colorcycleuserflag = 0;
			if (TIE_DISPLAY_DX5) {
#ifdef TIE_MODERN
				Tie98StarColors_Invalidate();
#endif
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
#ifdef TIE_MODERN
			TieReplaySession_Begin();
			continuation->phase = TIE_SIM_PHASE_AFTER_REPLAY_VIEWER;
			return;
#else
			replayio_replayscreen();
#endif
		} else {
			maingameflag = 1;
			fediskio_createpilotrecord();

			lasthistorymsg = -1;
			numhistorymsgs = 0;
			panelflag = 0;
			replayavailable = 0;

			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			create_loadmission(missionfilename);
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
			fediskio_loadspecies();
			gamesnd_game_Open_iMuse();
			if (TIE_DISPLAY_DX5) {
#ifdef TIE_MODERN
				Tie98StarColors_Invalidate();
#endif
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

#ifdef TIE_MODERN
			continuation->loadscreen_shown_us = TieSimClock_NowUs();
			continuation->phase = TIE_SIM_PHASE_LOADSCREEN_HOLD;
			continuation->next_step = LANDRU_TASK_STEP_YIELD;
			return;
#endif
		}
	}
#ifdef TIE_MODERN
	if (continuation->phase == TIE_SIM_PHASE_AFTER_REPLAY_VIEWER) {
		continuation->phase = TIE_SIM_PHASE_TEARDOWN;
		return;
	}
#endif
	if (!replay_mode) {
#ifdef TIE_MODERN
		if (continuation->phase == TIE_SIM_PHASE_LOADSCREEN_HOLD ||
			continuation->phase == TIE_SIM_PHASE_AFTER_HYPER)
#endif
		{
#ifdef TIE_MODERN
			if (continuation->phase == TIE_SIM_PHASE_LOADSCREEN_HOLD) {
				if (TieSimClock_NowUs() - continuation->loadscreen_shown_us < TIE_SIM_LOADSCREEN_MIN_US) {
					continuation->next_step = LANDRU_TASK_STEP_YIELD;
					return;
				}
#endif
				if (pstate.player_fg_idx < 48 && !fg_array[pstate.player_fg_idx].start_fg_used &&
					transitions_on && spec_data[pstate.player_spec_num].has_hyperdrive) {
					saved_drawbackdrop = drawbackdropflag;
					saved_drawdebris = drawdebrisflag;
					if (TIE_DISPLAY_DX5)
						FlightSurface_Lock();
					create_createhyperin();
					if (TIE_DISPLAY_DX5)
						FlightSurface_Unlock();
					hyperspaceflag = 2;
					colorcycleuserflag = 0;
					drawbackdropflag = 0;
					drawdebrisflag = 0;
					anim_dohyperspace();
#ifdef TIE_MODERN
					continuation->saved_drawbackdrop = saved_drawbackdrop;
					continuation->saved_drawdebris = saved_drawdebris;
					continuation->phase = TIE_SIM_PHASE_AFTER_HYPER;
					TieFlightRuntime_BeginHyperspace();
					return;
#else
				while (hyperspaceflag)
					tie_doframe();
				drawbackdropflag = saved_drawbackdrop;
				drawdebrisflag = saved_drawdebris;
				species_table[114].load_flags &= ~0x10u;
				species_table[115].load_flags &= ~0x10u;
				species_table[116].load_flags &= ~0x10u;
				hyperspaceflag = 0;
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
				create_loadmission(missionfilename);
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
#endif
				}
#ifdef TIE_MODERN
			}
			if (continuation->phase == TIE_SIM_PHASE_AFTER_HYPER) {
				drawbackdropflag = saved_drawbackdrop;
				drawdebrisflag = saved_drawdebris;
				species_table[114].load_flags &= ~0x10u;
				species_table[115].load_flags &= ~0x10u;
				species_table[116].load_flags &= ~0x10u;
				hyperspaceflag = 0;
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
				create_loadmission(missionfilename);
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
			}
#endif

			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
#ifdef TIE_MODERN
			TIE_FLIGHT_TRACE_BEGIN_MISSION(missionfilename);
#endif
			create_createmission();
#ifdef TIE_MODERN
			TieFlightRuntime_ResetTiming();
#endif
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
				FrontendDisplay_BlitOffscreenToRenderSurface();
			}

			colorcycleuserflag = 0;
			if (mission.train_craft_type) {
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
				gate_createtraininggates();
				gate_settraininglevel(mission.train_level);
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
			}
#ifdef TIE_MODERN
			TIE_FLIGHT_TRACE_MISSION_CREATED();
#endif

			if (pstate.player_fg_idx < 48 && !fg_array[pstate.player_fg_idx].start_fg_used &&
				spec_data[pstate.player_spec_num].has_hyperdrive)
				msg_messageprintf(MSG_HYPER_COMPLETED);

			if (mission.train_craft_type && mission.train_level > 1u) {
				argtable[0] = (uint16_t)(mission.train_level - 1);
				msg_messageprintf(MSG_BONUS_PRIOR_LEVELS);

				mission.mission_score = 10000 * ((int)mission.train_level - 1);
			}

#if defined(TIE_MODERN) || defined(TIE98)
#ifdef TIE_MODERN
			if (TieMusicPolicy_UsesTie98())
#endif
			{
				cdmusic_switch_latched = 0;
				if (cdaudio_Open_Device()) {
					const int start = math2_getrandomalt() & 3;
					cdaudio_Set_Volume((uint16_t)(0xFFFF * inflight_music_vol / 16));
					cdaudio_Play_Track(2, cdmusic_start_min[start], cdmusic_start_sec[start]);
					cdmusic_ms_remaining = cdaudio_Track_Length_Ms(2) -
										   1000 * (cdmusic_start_sec[start] + 60 * cdmusic_start_min[start]);
					cdmusic_last_ms = TieMusicPolicy_NowMs();
					cdmusic_kind = 2;
				} else {
					cdmusic_ms_remaining = INT32_MAX;
					cdmusic_kind = 0;
				}
			}
#endif
#ifdef TIE_MODERN
			continuation->phase = TIE_SIM_PHASE_AFTER_MISSION;
			TieFlightTask_BeginMission();
			return;
#else
			do {
				tickcounter += xtimer_Time_Elapsed();
			} while (!tickcounter);
			while (!mission.end_flag)
				tie_doframe();
#endif
		}
#ifdef TIE_MODERN
		if (continuation->phase == TIE_SIM_PHASE_AFTER_MISSION) {
			TIE_FLIGHT_TRACE_END_MISSION();
			if (TieMusicPolicy_UsesTie98())
				cdaudio_Close_Device();
#else
#ifdef TIE98
		cdaudio_Close_Device();
#endif
		{
#endif
			post_mission_ui = mission.player_status < 10u || mission.end_flag == 2;
#ifdef TIE_MODERN
			continuation->post_mission_ui = post_mission_ui;
#endif
			if (post_mission_ui) {
				pstate.post_mission_shield_q4 =
					(uint8_t)((int)math2_percentage(pstate.player_craft->hull_damage,
													pstate.player_craft->hull_max) >>
							  14);
				if (mission.end_flag == 2)
					mission.player_status = 3;
				blank();
#ifdef TIE_MODERN
				if (replayavailable) {
					continuation->saved_master_vol = (uint16_t)hilevel_ImGetMasterVol();
					continuation->previous_screen =
						TieFlightScreen_SetActive(TIE_FLIGHT_SCREEN_REPLAY_PROMPT);
					continuation->phase = TIE_SIM_PHASE_PROMPT_RENDER;
					return;
				}
#endif
			} else {
				blank();
#ifdef TIE_MODERN
				continuation->phase = TIE_SIM_PHASE_TEARDOWN;
				return;
#endif
			}
		}
		if (post_mission_ui) {
#ifdef TIE_MODERN
			if (continuation->phase == TIE_SIM_PHASE_PROMPT_RENDER ||
				continuation->phase == TIE_SIM_PHASE_PROMPT_POLL ||
				continuation->phase == TIE_SIM_PHASE_PROMPT_AFTER_VIEWER)
#else
			if (replayavailable)
#endif
			{
#ifdef TIE_MODERN
				if (continuation->phase == TIE_SIM_PHASE_PROMPT_RENDER)
#else
				saved_master_vol = (uint16_t)hilevel_ImGetMasterVol();
#endif
				{
					int32_t margin = screenXRes / 10;
					int32_t right = screenXRes - margin;
					int32_t top;
					int32_t bottom;
					hilevel_ImSetMasterVol(0);
					lolevel_ImPause();
					if (TIE_DISPLAY_DX5)
						FlightSurface_Lock();
					festring_setfontsize(1);
					top = (screenYRes >> 1) - (int32_t)fontheight - (screenYRes >> 3);
					bottom = top + 2 * (int32_t)fontheight;
					festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
					festring_setbackcolor(0x40);
					clearwindow();
					festring_setbound((int16_t)(margin - 1), (int16_t)(top - 1), (int16_t)(right + 1),
									  (int16_t)(bottom + 1));
					festring_setbackcolor(0x4A);
					clearwindow();
					festring_setbound((int16_t)margin, (int16_t)top, (int16_t)right, (int16_t)bottom);
					festring_setbackcolor(0x40);
					clearwindow();
					festring_settextcolor(0x43);
					festring_setdropcolor(0x41);
					festring_setcursor((int16_t)(margin + 1), (int16_t)(top + (int32_t)fontheight / 2));
					festring_outstringcenter((const uint8_t*)viewfilmstr);
					unblank();
					if (TIE_DISPLAY_DX5) {
						FlightSurface_Unlock();
						FrontendDisplay_BlitOffscreenToRenderSurface();
						FrontendDisplay_PresentFrame();
					}
#ifdef TIE_MODERN
					continuation->phase = TIE_SIM_PHASE_PROMPT_POLL;
					return;
#endif
				}
#ifdef TIE_MODERN
				if (continuation->phase == TIE_SIM_PHASE_PROMPT_POLL)
#endif
				{
					int ch;
					for (;;) {
#ifdef TIE_MODERN
						if (!TieInput_KeyPending()) {
							continuation->next_step = LANDRU_TASK_STEP_YIELD;
							return;
						}
						ch = TieInput_ReadKey();
#elif defined(TIE98)
						ch = FlightInput_GetChar();
#else
					ch = (char)getch();
#endif
						if (ch == 'y' || ch == 'Y') {
							blank();
							hilevel_ImSetMasterVol(saved_master_vol);
							lolevel_ImResume();
#ifdef TIE_MODERN
							TieReplaySession_Begin();
							continuation->phase = TIE_SIM_PHASE_PROMPT_AFTER_VIEWER;
							return;
#else
#ifdef TIE98
							lolevel_ImStopAllSounds();
#endif
							replayio_replayscreen();
							blank();
							break;
#endif
						}
						if (ch == 'n' || ch == 'N') {
							hilevel_ImSetMasterVol(saved_master_vol);
							lolevel_ImResume();
							lolevel_ImStopAllSounds();
							break;
						}
#ifdef TIE_MODERN
						return;
#endif
					}
#ifdef TIE_MODERN
					TieFlightScreen_SetActive(continuation->previous_screen);
					continuation->phase = TIE_SIM_PHASE_AFTER_REPLAY_PROMPT;
					return;
#endif
				}
#ifdef TIE_MODERN
				if (continuation->phase == TIE_SIM_PHASE_PROMPT_AFTER_VIEWER) {
					blank();
					TieFlightScreen_SetActive(continuation->previous_screen);
					continuation->phase = TIE_SIM_PHASE_AFTER_REPLAY_PROMPT;
					return;
				}
#endif
			}
#ifdef TIE_MODERN
			if (continuation->phase == TIE_SIM_PHASE_AFTER_MISSION ||
				continuation->phase == TIE_SIM_PHASE_AFTER_REPLAY_PROMPT)
#endif
			{
				if (!mission.train_craft_type) {
#ifdef TIE_MODERN
					TieInflightInfo_Begin(0);
					continuation->phase = TIE_SIM_PHASE_AFTER_INFOROOM;
					return;
#else
					user_inflightinfo(0);
					blank();
#endif
				}
				if (mission.player_status == 3)
					fediskio_updatepilotrecord(0, 0);
#ifdef TIE_MODERN
				continuation->phase = TIE_SIM_PHASE_TEARDOWN;
				return;
#endif
			}
#ifdef TIE_MODERN
			if (continuation->phase == TIE_SIM_PHASE_AFTER_INFOROOM) {
				blank();
				if (mission.player_status == 3)
					fediskio_updatepilotrecord(0, 0);
				continuation->phase = TIE_SIM_PHASE_TEARDOWN;
				return;
			}
#endif
		}
	}
#ifdef TIE_MODERN
	TieFlightTiming_EndSession();
#endif
	lolevel_ImStopAllSounds();
	filelist_ImUnloadAll();
	if (TIE_FLIGHT_TIE98) {
		colorcycleflag = 0;
		fediskio_FreeFlightHandles();
#ifdef TIE_MODERN
		TieFlightRuntime_ReleaseRecoveredResources();
#endif
		maingameflag = 0;
		gamesnd_Transition_Sound();
#ifdef TIE_MODERN
		if (!TieClassicDisplay_ActivateFrontend()) {
			xerror_Set_Landru_Error(12);
			continuation->next_step = LANDRU_TASK_STEP_DONE;
			return;
		}
#else
		if (flightResolution != frontResolution)
			xvesa_Enter_VESA_Mode((uint16_t)frontResolution);
#endif
		rtsvga2_clearflightdisplay();
	} else {
		gamesnd_Transition_Sound();
		colorcycleflag = 0;
		fediskio_FreeFlightHandles();
#ifdef TIE_MODERN
		TieFlightRuntime_ReleaseRecoveredResources();
		if (TIE_FRONTEND_TIE98)
			maingameflag = 0;
		if (!TieClassicDisplay_ActivateFrontend())
			xerror_Set_Landru_Error(12);
#else
		if (flightResolution != frontResolution)
			xvesa_Enter_VESA_Mode((uint16_t)frontResolution);
#endif
	}
#ifdef TIE_MODERN
	continuation->next_step = LANDRU_TASK_STEP_DONE;
#endif
}

/* Configure flight geometry for VGA or the 640x480 modes and select the
 * corresponding CP320/CP640 cockpit asset directory. */
// FUNCTION: TIE95 0x56048
// FUNCTION: TIE98 0x48D850
void tie_InitFlightResolution(void) {
	/* PORT: display ownership and mode selection happen at the simulator,
	 * replay or frontend-preview boundary before this recovered geometry setup. */
	if (flightResolution == TIE_FLIGHT_RES_SVGA_16 || flightResolution == TIE_FLIGHT_RES_SVGA_D3D)
		bytesPerPixel = 2;
	else
		bytesPerPixel = 1;

	/* Disable VESA bank wrapping for the host's linear framebuffer. */
	{
		uint16_t* modeinfo = (uint16_t*)xvesa_Get_Vesa_Mode_Struct();
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

	if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
		flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
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
		rtsvga2_SetVESAScanLineLength(0x400u); /* retained for parity */
		if (TIE_DISPLAY_DX5)
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

#ifdef TIE_MODERN
// FUNCTION: TIE95 0x56270
// FUNCTION: TIE98 0x48D9B0
bool tie_doframe(void) {
	TieFlightCadence ai_cadence;
	TieFlightCadence animation_cadence;
	bool rendered;

	/* PORT: apply a renderer switch requested by the host between frames. */
	if (TIE_FLIGHT_TIE98 && !Tie98Renderer_ApplyPending())
		return false;
#else
// FUNCTION: TIE95 0x56270
// FUNCTION: TIE98 0x48D9B0
void tie_doframe(void) {
#ifdef TIE98
	int rendered;
#endif
#endif

	/* Step 1 — refresh frameticks/framerate. */
	if (!replayviewmode) {
#ifdef TIE_MODERN
		/* PORT: xtimer advances between runtime ticks; return until a
		 * complete simulation period has accumulated instead of spinning. */
		tickcounter += (uint16_t)xtimer_Time_Elapsed();
		if (tickcounter < TieFlightTiming_StepTicks())
			return false;
#else
		do {
			tickcounter += xtimer_Time_Elapsed();
		} while (tickcounter < TIE_FLIGHT_EDITION(4, 7));
#endif

		lastcounter = (int16_t)tickcounter;
		tickcounter = 0;

		if (calcframerate) {
			framerate = (uint16_t)(236 / lastcounter);
			frameticks = (uint16_t)lastcounter;
			if (framerate == 0) {
				framerate = 1;
				frameticks = 236;
			}
		}
		calcframerate = 1;
	} else {
#ifdef TIE_MODERN
		ReplayInputFrame replay_frame;
		if (!TieReplayTiming_DecodeCurrentInputFrame(&replay_frame)) {
			replay_stopreplay();
			return true;
		}
		frameticks = replay_frame.frameticks;
#else
		uint16_t* record = (uint16_t*)replayptr;
		frameticks = record[3] >> 8;
#endif
		framerate = (uint16_t)(236 / frameticks);
		if (framerate == 0)
			framerate = 1;
	}

	/* Step 2 — UI input pass (skipped on fast-time skip frames). */
	mapflag = 0;
	if (acceleratedtimesetting <= 1 || acceleratedtimectr == 0)
		user_userinterface();

#ifdef TIE_MODERN
	/* The tick budget was consumed even when world work is skipped. */
	if (TieFlightPause_IsActive())
		return true;
#endif
	if (mission.end_flag != 0 || mapflag != 0)
#ifdef TIE_MODERN
		return true;
#else
		return;
#endif

#ifdef TIE_MODERN
	TieFlightTiming_BeginAdvance(frameticks);
	TIE_FLIGHT_TRACE_BEGIN_FRAME(frameticks, framerate);
	TieAiLead_Advance(frameticks);
	ai_cadence = TieFlightTiming_AdvanceAi(frameticks);
	animation_cadence = TieFlightTiming_AdvanceAnimation(frameticks);
	TieFlightCadence_SetAiTimerTicks(ai_cadence);
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_TIME);
#endif
	tie_updatetime();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	if (mission.train_craft_type == 0) {
		TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_FG_STATUS);
		create_updatefgstatus();
		TIE_FLIGHT_TRACE_OBSERVE_STATE();
		TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_AI);
#ifdef TIE_MODERN
		TieFlightCadence_RunPlaneAi(ai_cadence);
#else
		pai_updateplaneai();
#endif
		TIE_FLIGHT_TRACE_OBSERVE_STATE();
	}
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_WEAPONS);
	laser_weaponsfire();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_DYNAMICS);
	dynamix_planedynamics();

	/* Render gate (with accelerated-time skip): renders once every
	 * acceleratedtimesetting frames when set > 1. The live branch also
	 * redraws the cockpit panel. */
#if defined(TIE98) || defined(TIE_MODERN)
	rendered = 0;
#endif
	if (!replayviewmode) {
		if (acceleratedtimesetting > 1) {
			if (acceleratedtimectr == 0) {
				tie_updatescreen();
				/* TIE98 locks the flight surface only for the panel. */
				if (TIE_FLIGHT_TIE98)
					FlightSurface_Lock();
				panel_updatepanel();
				if (TIE_FLIGHT_TIE98)
					FlightSurface_Unlock();
#if defined(TIE98) || defined(TIE_MODERN)
				rendered = 1;
#endif
				acceleratedtimectr = acceleratedtimesetting;
			} else {
				/* Skip the render — let the timer catch up. */
				tickcounter += xtimer_Time_Elapsed();
				tickcounter += frameticks;
			}
			--acceleratedtimectr;
		} else {
			tie_updatescreen();
			/* TIE98 locks the flight surface only for the panel. */
			if (TIE_FLIGHT_TIE98)
				FlightSurface_Lock();
			panel_updatepanel();
			if (TIE_FLIGHT_TIE98)
				FlightSurface_Unlock();
#if defined(TIE98) || defined(TIE_MODERN)
			rendered = 1;
#endif
		}
	} else if (fastforwardflag) {
		/* Fast-forward stalls rendering until fastforwardtimer drains
		 * one mission-second (236 ticks) worth of frame time. */
		if (frameticks > (uint16_t)fastforwardtimer) {
			fastforwardtimer += 236;
			if (acceleratedtimesetting > 1) {
				if (acceleratedtimectr == 0) {
					tie_updatescreen();
#if defined(TIE98) || defined(TIE_MODERN)
					rendered = 1;
#endif
					acceleratedtimectr = acceleratedtimesetting;
				} else {
					/* Skip the render — let the timer catch up. */
					tickcounter += xtimer_Time_Elapsed();
					tickcounter += frameticks;
				}
				--acceleratedtimectr;
			} else {
				tie_updatescreen();
#if defined(TIE98) || defined(TIE_MODERN)
				rendered = 1;
#endif
			}
		}
		fastforwardtimer -= (int16_t)frameticks;
	} else {
		if (acceleratedtimesetting > 1) {
			if (acceleratedtimectr == 0) {
				tie_updatescreen();
#if defined(TIE98) || defined(TIE_MODERN)
				rendered = 1;
#endif
				acceleratedtimectr = acceleratedtimesetting;
			} else {
				/* Skip the render — let the timer catch up. */
				tickcounter += xtimer_Time_Elapsed();
				tickcounter += frameticks;
			}
			--acceleratedtimectr;
		} else {
			tie_updatescreen();
#if defined(TIE98) || defined(TIE_MODERN)
			rendered = 1;
#endif
		}
	}

	/* Post-render world updates. */
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_RENDER);
#ifdef TIE_MODERN
	if (drawdebrisflag && mission.train_craft_type == 0 && TieFlightTiming_LegacyDue())
#else
	if (drawdebrisflag && mission.train_craft_type == 0)
#endif
		create_checkdebris();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_COLLISION);
	collide_collisions();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_MOVE);
	move_moveobjects();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_ANIMATION);
#ifdef TIE_MODERN
	TieFlightCadence_RunAnimation(animation_cadence);
#else
	anim_updateanimation();
#endif
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_OBJECTIVES);
	score_checkobjective();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	msg_messageupdate();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
#ifdef TIE_MODERN
	TieMusicPolicy_UpdateFlightMusic();
#else
	if (TIE_FLIGHT_TIE98)
		tie_updatemusic_tie98();
	else
		tie_updatemusic();
#endif

	if (TIE_FLIGHT_TIE98)
		FrontendSound_FlushQueuedSounds();
	if (blastflag) {
		if (blastcount)
			fsfx_checkblastqueue();
		fsfx_checktieflyby();
		if (TIE_FLIGHT_TIE98)
			fsfx_UpdatePlayerEngineSound();
	}
#if defined(TIE98) || defined(TIE_MODERN)
	if (TIE_FLIGHT_TIE98 && rendered) {
		FrontendDisplay_PresentFrame();
		if (g_useHardware3D)
			RenderScene_ClearFrameBuffers();
		else
			FrontendDisplay_BlitOffscreenToRenderSurface();
	}
#endif
#ifdef TIE_MODERN
	TIE_FLIGHT_TRACE_END_FRAME();

	/* The application uploads vesa_buff_gbl at the end of the tick. */
	return true;
#endif
}

/* Per-frame world render: camera, flight objects, static objects, rasterizer,
 * bitmap queue, and starfield. */
// FUNCTION: TIE95 0x56574
void tie_updatescreen(void) {
	int16_t obj_iter;
	int16_t i;

#ifdef TIE_MODERN
	if (TIE_FLIGHT_TIE98) {
		/* PORT: keep host-only frame state outside recovered tie_updatescreen_tie98. */
		TieBillboardCapture_BeginTick();
		/* PORT: TIE98 rebuilds its hardware palette table when DirectDraw's
		 * palette changes. The shared framebuffer owns that palette here. */
		RenderTexture_SyncFlightPalette();
		tie_updatescreen_tie98();
		TieFlightSnapshot_RecordCameraBasis();
		return;
	}
#endif

	/* SNAPSHOT-ONLY: reset the per-tick HD billboard capture caches
	 * here, at the start of every tick that actually renders the 3D
	 * world. tie_doframe gates this call behind TieFlightPause_IsActive() and
	 * the accelerated-time skip counter, so paused / skipped frames
	 * leave the caches frozen on the last-rendered tick — which is
	 * exactly what the HD billboard pass needs to keep showing the
	 * frozen sprites while the engine is paused (classic just keeps
	 * the previous framebuffer; HD pulls a fresh snapshot every host
	 * tick and would otherwise see an empty billboard array). */
#ifdef TIE_MODERN
	TieBillboardCapture_BeginTick();
#endif

	/* --- Step 1: pick a camera --------------------------------------- */
	if (replayviewmode) {
		replay_calcreplayview();
	} else if (camera.view_target_obj == (uint16_t)-1) {
		/* No target: aim camera at player via TRIG2_ctop, roll=0. */
		if (!hyperspaceflag) {
			trig2_ctop(pstate.player->world_x - camera.x, pstate.player->world_y - camera.y,
					   pstate.player->world_z - camera.z);
			camera.roll = 0;
			camera.cam_pitch = trig2_zangle;
			camera.cam_heading = trig2_xyangle;
		}
		fview_newcalcview(camera.roll, camera.cam_pitch, camera.cam_heading, 0, (int16_t)camera.side_angle,
						  (int16_t)camera.up_angle, NULL);
#ifdef TIE_MODERN
		TieFlightSnapshot_RecordCameraBasis();
#endif
	} else if ((camera.view_zoom_flag && camera.view_target_tracking == 0) ||
			   (camera.view_target_tracking != 0 && camera.view_camera_control != 0)) {
#ifdef TIE_MODERN
		TieChaseCamera_Update();
#else
		{
			int16_t history_slot = camera.cam_chase_slot - 5;
			if (history_slot < 0)
				history_slot += 60;
			camera.roll = camera.cam_chase_roll_hist[history_slot];
			camera.cam_pitch = camera.cam_chase_pitch_hist[history_slot];
			camera.cam_heading = camera.cam_chase_heading_hist[history_slot];
		}
		if (camera.view_target_obj < (int)OBJ_REF_STATIC_BASE) {
			camera.cam_chase_roll_hist[camera.cam_chase_slot] = objects[camera.view_target_obj].roll;
			camera.cam_chase_pitch_hist[camera.cam_chase_slot] = objects[camera.view_target_obj].pitch;
			camera.cam_chase_heading_hist[camera.cam_chase_slot] = objects[camera.view_target_obj].heading;
		} else {
			camera.cam_chase_roll_hist[camera.cam_chase_slot] =
				staticobjects[camera.view_target_obj - OBJ_REF_STATIC_BASE].roll_byte << 8;
			camera.cam_chase_pitch_hist[camera.cam_chase_slot] =
				staticobjects[camera.view_target_obj - OBJ_REF_STATIC_BASE].pitch_byte << 8;
			camera.cam_chase_heading_hist[camera.cam_chase_slot] =
				staticobjects[camera.view_target_obj - OBJ_REF_STATIC_BASE].heading_byte << 8;
		}
		if (++camera.cam_chase_slot == 60)
			camera.cam_chase_slot = 0;
#endif

		fview_newcalcview(camera.roll, camera.cam_pitch, camera.cam_heading, 0, (int16_t)camera.side_angle,
						  (int16_t)camera.up_angle, NULL);
#ifdef TIE_MODERN
		TieFlightSnapshot_RecordCameraBasis();
#endif

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
		camera.x -= (camera.view_zoom * worldeyeA3) >> 15;
		camera.y -= (camera.view_zoom * worldeyeB3) >> 15;
		camera.z -= (camera.view_zoom * worldeyeC3) >> 15;

		{
			uint8_t species_idx;
			if (camera.view_target_obj < (int)OBJ_REF_STATIC_BASE)
				species_idx = objects[camera.view_target_obj].ship_idx;
			else
				species_idx = staticobjects[camera.view_target_obj - OBJ_REF_STATIC_BASE].species;
			objectsize = species_table[species_idx].bound_hwidth;
			objectsize = (uint16_t)objectsize >> 2;
			/* Same basis as above. Binary 0x5696F/0x56989/0x569A2. */
			camera.x -= 4 * (((uint16_t)objectsize * worldeyeA3) >> 15);
			camera.y -= 4 * (((uint16_t)objectsize * worldeyeB3) >> 15);
			camera.z -= 4 * (((uint16_t)objectsize * worldeyeC3) >> 15);
		}
	} else if (camera.view_target_tracking != 0) {
		panel_pointcamera(camera.view_target_obj, 0);
#ifdef TIE_MODERN
		TieFlightSnapshot_RecordCameraBasis();
#endif
	} else {
		/* Default: camera follows camera.view_target_obj's exact position+orient. */
		camera.roll = objects[camera.view_target_obj].roll;
		camera.cam_pitch = objects[camera.view_target_obj].pitch;
		camera.cam_heading = objects[camera.view_target_obj].heading;
		fview_newcalcview(camera.roll, camera.cam_pitch, camera.cam_heading, camera.yaw,
						  (int16_t)camera.side_angle, (int16_t)camera.up_angle,
						  &objects[camera.view_target_obj]);
#ifdef TIE_MODERN
		TieFlightSnapshot_RecordCameraBasis();
#endif
		camera.x = objects[camera.view_target_obj].world_x;
		camera.y = objects[camera.view_target_obj].world_y;
		camera.z = objects[camera.view_target_obj].world_z;
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

	/* --- Step 3: per-object render dispatch. RETAIL: 0..119; the debris
	 * slots DEBRIS_FIRST_SLOT (112) and up are drawn only when debris is
	 * enabled. ------------------------------------------------------- */
	for (obj_iter = 0; obj_iter < NUM_OBJECTS; ++obj_iter) {
		uint16_t species;
		uint16_t genus;

		if (obj_iter == DEBRIS_FIRST_SLOT &&
			(!drawdebrisflag || hyperspaceflag || mission.train_craft_type != 0))
			break;

		if (obj_iter == camera.view_target_obj && camera.view_zoom_flag == 0 && !replayviewmode)
			continue;

		species = objects[obj_iter].ship_idx;
		if (species == 0)
			continue;
		objectsize = species_table[species].bound_hwidth;
		genus = objects[obj_iter].genus;

		switch (genus) {
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
				int near_far;
				int abs_x;
				int abs_y;

				craftptr = objects[obj_iter].craft_ptr;
				tie_getobjecteyexyz(obj_iter);
				craftptr->eye_x_cache = objecteyex;
				craftptr->eye_y_cache = objecteyey;
				craftptr->eye_z_cache = objecteyez;

				if ((objecteyez >> 8) >= (uint16_t)objectsize)
					break;
				near_far = objecteyez + (uint16_t)objectsize;
				if (near_far <= 0)
					break;
				abs_x = objecteyex;
				if (abs_x < 0)
					abs_x = -abs_x;
				if (abs_x - (uint16_t)objectsize >= near_far)
					break;
				abs_y = objecteyey;
				if (abs_y < 0)
					abs_y = -abs_y;
				if (abs_y - (uint16_t)objectsize >= near_far)
					break;

				if (genus == GENUS_GATE)
					lightflag = 0;
				fview_newcalcrotate(objects[obj_iter].roll, objects[obj_iter].pitch,
									objects[obj_iter].heading, 0, &objects[obj_iter]);
				if (TIE_FLIGHT_TIE98) {
					/* PORT: native OPT craft are emitted through the snapshot. */
				} else if (genus == GENUS_GATE) {
					gate_drawtraininggate(obj_iter);
				} else {
					tie_MakeLocalLights(obj_iter);
					draw_drawcomplexobject((uint16_t)obj_iter);
					localLightCnt = 0;
				}
				lightflag = 1;
				break;
			}

			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC:
				if (tie_checkobjecteyexyz(obj_iter, objectsize)) {
					fview_newcalcrotate(objects[obj_iter].roll, objects[obj_iter].pitch,
										objects[obj_iter].heading, 0, &objects[obj_iter]);
					draw_drawlaser(obj_iter);
				}
				break;

			case GENUS_DEBRIS: /* 11 */
				if (tie_checkobjecteyexyz(obj_iter, objectsize)) {
					fview_newcalcrotate(objects[obj_iter].roll, objects[obj_iter].pitch,
										objects[obj_iter].heading, 0, &objects[obj_iter]);
					anim_drawverysimpleobject(obj_iter);
				}
				break;

			case GENUS_EXPLOSION: /* 13 */
				if (tie_checkobjecteyexyz(obj_iter, objectsize)) {
					fview_newcalcrotate(objects[obj_iter].roll, objects[obj_iter].pitch,
										objects[obj_iter].heading, 0, &objects[obj_iter]);
					anim_drawverysimpleobject(obj_iter);
				}
				break;
		}
	}

	/* --- Step 4: static objects + hyperstars ------------------------ */
	for (i = 0; i < 64; ++i) {
		if (hyperspaceflag == 3 || hyperspaceflag == 5) {
			/* Hyperstar render: 4 mirrored stars per slot. */
			if ((uint16_t)hyperspacedetail > i) {
				int16_t wx = staticobjects[i].world_x;
				int16_t wy = staticobjects[i].world_y;
				int16_t wz = staticobjects[i].world_z;

				objectsize = -1;
				tie_checkstaticobjecteyexyz(wx, wy, wz, 0xFFFFu);
				draw_drawhyperstar(i);
				wz = -wz;
				tie_checkstaticobjecteyexyz(wx, wy, wz, objectsize);
				draw_drawhyperstar(i);
				++flatobjnum;
				if ((uint16_t)hyperspacedetail / 2 > i) {
					wx = -wx >> 1;
					wz >>= 1;
					tie_checkstaticobjecteyexyz(wx, wy, wz, objectsize);
					wz = -wz;
					draw_drawhyperstar(i);
					wx >>= 1;
					wz >>= 1;
					tie_checkstaticobjecteyexyz(wx, wy, wz, objectsize);
					draw_drawhyperstar(i);
					++flatobjnum;
				}
			}
			continue;
		}

		/* Standard static-object render (mines, planets, asteroids,
		 * backdrops). */
		{
			uint16_t species = staticobjects[i].species;
			uint16_t ship_class;

			if (species == 0)
				continue;
			objectsize = species_table[species].bound_hwidth;
			ship_class = staticobjects[i].ship_class;
			if (ship_class < (uint16_t)8 || ship_class > (uint16_t)0xB)
				continue;
			if (!tie_checkstaticobjecteyexyz(staticobjects[i].world_x, staticobjects[i].world_y,
											 staticobjects[i].world_z, objectsize))
				continue;
			/* Asteroids (species 100..105) tumble per frame. */
#ifdef TIE_MODERN
			if (species >= 100 && species <= 105 &&
				(!TieFlightTiming_IsHighRate() || TieFlightTiming_LegacyDue())) {
				uint16_t f = TieFlightTiming_IsHighRate() ? TieFlightTiming_CompatibilityTicks() : frameticks;
				staticobjects[i].roll_byte += (i >> 4) * f / 16;
				staticobjects[i].pitch_byte += (i >> 3) * f / 32;
				staticobjects[i].heading_byte += (4 - (i >> 4)) * f / 16;
			}
#else
			if (species >= 100 && species <= 105) {
				staticobjects[i].roll_byte += (i >> 4) * frameticks / 16;
				staticobjects[i].pitch_byte += (i >> 3) * frameticks / 32;
				staticobjects[i].heading_byte += (4 - (i >> 4)) * frameticks / 16;
			}
#endif
			fview_newcalcrotate((uint16_t)(staticobjects[i].roll_byte << 8),
								(uint16_t)(staticobjects[i].pitch_byte << 8),
								(uint16_t)(staticobjects[i].heading_byte << 8), 0, NULL);
			static_drawstaticobject(i);
		}
	}

	/* --- Step 5: flush bitmap queue + XTRANS rasterizer ------------- */
	anim_sort_and_draw_bitmaps();
	dxtticks = 0;
	oxtticks = 0;
	tickcounter += (uint16_t)xtimer_Time_Elapsed();
	dxtticks = tickcounter;

	xtrans2_drawxtrans();
	tickcounter += (uint16_t)xtimer_Time_Elapsed();
	dxtticks = (uint16_t)(tickcounter - dxtticks);

	deepspacecolor = (uint8_t)-5;
	if (hyperspaceflag != 3 && hyperspaceflag != 5)
		rtsvga2_drawstars();

#ifdef TIE_MODERN
	/* Signal that the application must upload the classic framebuffer. */
	vesa_dirty_gbl = true;
#endif
}

/* Build up to eight explosion lights in the source craft's reflected local
 * basis (side, -forward, up). Returns and stores the emitted count. */
// FUNCTION: TIE95 0x57158
int tie_MakeLocalLights(int obj_idx) {
	FlightObject* src_obj = &objects[obj_idx];
	uint32_t max_distance_sq;
	int model_scale_shift;
	int src_world_x;
	int src_world_y;
	int src_world_z;
	int light_idx;
	int light_count;
	int scan_idx;
	FlightObject* expl;

	draw_Lockshipfileptrs(src_obj->ship_idx);
	model_scale_shift = (int8_t)objectblockptr->model_scale_shift;

	/* Light reach scales with source ship size:
	 *   model_scale_shift == 0 -> 0x4000  (small craft, short reach).
	 *   model_scale_shift > 0  -> 0x8000 << (model_scale_shift - 1). */
	if (model_scale_shift == 0)
		max_distance_sq = 0x4000u;
	else
		max_distance_sq = 0x8000u << (model_scale_shift - 1);

	src_world_x = src_obj->world_x;
	src_world_y = src_obj->world_y;
	src_world_z = src_obj->world_z;
	light_idx = 0;
	light_count = 0;

	for (scan_idx = 0, expl = objects; scan_idx < NUM_OBJECTS; ++scan_idx, ++expl) {
		int dx, dy, dz;

		if (expl->ship_idx == 0)
			continue; /* dead slot */
		if (expl->genus != GENUS_EXPLOSION)
			continue;

		dx = expl->world_x - src_world_x;
		dy = expl->world_y - src_world_y;
		dz = expl->world_z - src_world_z;
		if ((uint32_t)collide_roughdistance3d(dx, dy, dz) >= max_distance_sq)
			continue;

		/* dot products: source craft's local basis x world delta. */
		localLights[light_idx].x =
			math2_dot3_q15_clamped(dx, dy, dz, src_obj->side_x, src_obj->side_y, src_obj->side_z);

		/* y axis is FLIPPED: we store -(fwd >> 15) so the local frame
		 * matches the right-handed eye-space DRAWPOL expects. */
		localLights[light_idx].y =
			-math2_dot3_q15_clamped(dx, dy, dz, src_obj->fwd_x, src_obj->fwd_y, src_obj->fwd_z);

		localLights[light_idx].z =
			math2_dot3_q15_clamped(dx, dy, dz, src_obj->up_x, src_obj->up_y, src_obj->up_z);

		/* Distance scale: large ships (model_scale_shift==0) double the position;
		 * others divide by 2^(model_scale_shift-1). */
		if (model_scale_shift == 0) {
			localLights[light_idx].x *= 2;
			localLights[light_idx].y *= 2;
			localLights[light_idx].z *= 2;
		} else {
			localLights[light_idx].x >>= model_scale_shift - 1;
			localLights[light_idx].y >>= model_scale_shift - 1;
			localLights[light_idx].z >>= model_scale_shift - 1;
		}

		localLights[light_idx].range = 16;
		switch (expl->ship_idx) {
			case 0x83:
			case 0x84:
				switch (expl->anim_frame) {
					case 2:
						localLights[light_idx].range = 24;
						break;
					case 3:
						localLights[light_idx].range = 48;
						break;
					case 4:
						localLights[light_idx].range = 32;
						break;
					case 5:
						localLights[light_idx].range = 16;
						break;
				}
				break;
			case 0x7F:
			case 0x80:
			case 0x81:
			case 0x82:
				switch (expl->anim_frame) {
					case 2:
					case 9:
						localLights[light_idx].range = 192;
						break;
					case 3:
					case 5:
					case 6:
					case 7:
					case 8:
						localLights[light_idx].range = 320;
						break;
					case 4:
						localLights[light_idx].range = 480;
						break;
					case 10:
						localLights[light_idx].range = 96;
						break;
					case 11:
						localLights[light_idx].range = 48;
						break;
				}
				if (expl->damage_state >= 4)
					localLights[light_idx].range *= (expl->damage_state + 4) / 4;
				break;
			default:
				localLights[light_idx].range = 16;
				break;
		}
		++light_count;
		if (++light_idx == 8)
			break; /* localLights[] is 8 entries */
	}

	localLightCnt = light_count;
	return light_count;
}

// FUNCTION: TIE98 0x48EC60
// TIE_MakeLocalLights
int tie_makelocallights_tie98(FlightObject* src_obj) {
	uint32_t max_distance_sq;
	int src_world_x;
	int src_world_y;
	int src_world_z;
	int light_idx;
	int light_count;
	uint16_t scan_idx;

	localLightCnt = 0;
	if (!g_localLightsEnabled)
		return 0;

	max_distance_sq = (uint32_t)species_table[src_obj->ship_idx].bound_hwidth + 0x4000u;
	src_world_x = src_obj->world_x;
	src_world_y = src_obj->world_y;
	src_world_z = src_obj->world_z;
	light_idx = 0;
	light_count = 0;

	/* TIE98 0x48EC60 scans every object slot (loop bound 0x4F2A7C = 120 =
	 * NUM_OBJECTS), not DRAWPOL's per-frame poly-object counter. */
	for (scan_idx = 0; scan_idx < NUM_OBJECTS; ++scan_idx) {
		FlightObject* expl = &objects[scan_idx];
		int dx;
		int dy;
		int dz;
		DRAWPOL_LocalLight* out;
		uint8_t ship_idx;

		if (expl->ship_idx == 0 || expl->genus != GENUS_EXPLOSION)
			continue;

		dx = expl->world_x - src_world_x;
		dy = expl->world_y - src_world_y;
		dz = expl->world_z - src_world_z;
		if ((uint32_t)collide_roughdistance3d(dx, dy, dz) >= max_distance_sq)
			continue;

		out = &localLights[light_idx];
		out->x = math2_dot3_q15_clamped(dx, dy, dz, (int32_t)src_obj->side_x, (int32_t)src_obj->side_y,
										(int32_t)src_obj->side_z);

		out->y = -math2_dot3_q15_clamped(dx, dy, dz, (int32_t)src_obj->fwd_x, (int32_t)src_obj->fwd_y,
										 (int32_t)src_obj->fwd_z);

		out->z = math2_dot3_q15_clamped(dx, dy, dz, (int32_t)src_obj->up_x, (int32_t)src_obj->up_y,
										(int32_t)src_obj->up_z);

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
			out->range = (int32_t)brightness_setting - 256;
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

/* ----------------------------------------------------------------------------
 * tie_getobjecteyexyz                                            retail 0x57518
 * ----------------------------------------------------------------------------
 * Cache the camera-relative + rotated eye-space coords of objects[obj_idx]
 * into the globals (worldx/y/z, objecteyex/y/z) AND into the craft's
 * eye_{x,y,z}_cache slots. Identical to the demo version (byte-for-byte
 * match after absolute-address normalization). */
// FUNCTION: TIE95 0x57518
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
// FUNCTION: TIE95 0x575E4
int16_t tie_checkobjecteyexyz(uint16_t obj_idx, uint16_t bound) {
	int near_far_extent;
	int magnitude;

	worldx = objects[obj_idx].world_x - camera.x;
	worldy = objects[obj_idx].world_y - camera.y;
	worldz = objects[obj_idx].world_z - camera.z;

	/* Compute eye_z first because the cheapest reject is the depth-cone. */
	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	near_far_extent = (int)objecteyez + (int)bound;
	if (near_far_extent < 0)
		return 0; /* fully behind camera.x */
	if ((near_far_extent >> 8) > (int)bound)
		return 0; /* past the depth limit */

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	magnitude = objecteyex;
	if (magnitude < 0)
		magnitude = -magnitude;
	if (magnitude - (int)bound > near_far_extent)
		return 0; /* outside left/right cone */

	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	magnitude = objecteyey;
	if (magnitude < 0)
		magnitude = -magnitude;
	if (magnitude - (int)bound > near_far_extent)
		return 0; /* outside up/down cone */
	return 1;
}

// FUNCTION: TIE98 0x48F730
void tie_updatemusic_tie98(void) {
	uint32_t now;
	if (inflight_music_vol == 0 || musicenabled == 0) {
		cdaudio_Stop_Track();
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
			cdaudio_Play_Track(kind, 0, 0);
			cdmusic_ms_remaining = cdaudio_Track_Length_Ms(kind);
			cdmusic_kind = kind;
			cdmusic_switch_latched = 1;
			return;
		}
	}
	now = TieMusicPolicy_NowMs();
	cdmusic_ms_remaining -= (int32_t)(now - cdmusic_last_ms);
	cdmusic_last_ms = now;
	if (cdmusic_ms_remaining <= 0) {
		cdaudio_Play_Track(2, 0, 0);
		cdmusic_ms_remaining = cdaudio_Track_Length_Ms(2);
		cdmusic_last_ms = TieMusicPolicy_NowMs();
		cdmusic_kind = 2;
	}
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
// FUNCTION: TIE95 0x576E4
int16_t tie_checkstaticobjecteyexyz(int16_t wx, int16_t wy, int16_t wz, uint16_t bound) {
	int near_far_extent;
	int abs_x, abs_y;

	/* *= 256 instead of <<= 8: same as the binary's `shl 8` but
	 * well-defined for negative int16 coords. */
	worldx = wx;
	worldx *= 256;
	worldy = wy;
	worldy *= 256;
	worldz = wz;
	worldz *= 256;
	worldx -= camera.x;
	worldy -= camera.y;
	worldz -= camera.z;

	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	near_far_extent = (int)objecteyez + (int)bound;
	if (near_far_extent < 0)
		return 0;
	if ((near_far_extent >> 8) > (int)bound)
		return 0;

	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	abs_x = objecteyex;
	if (abs_x < 0)
		abs_x = -abs_x;
	if (abs_x - (int)bound > near_far_extent)
		return 0;

	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	abs_y = objecteyey;
	if (abs_y < 0)
		abs_y = -abs_y;
	if (abs_y - (int)bound > near_far_extent)
		return 0;
	return 1;
}

/* Advance global and craft timers, target blinking, mission clock and warning,
 * pilot damage bookkeeping, object ages, and message ages. */
// FUNCTION: TIE95 0x577F4
void tie_updatetime(void) {
	uint16_t i;
	uint16_t tgt_species;
	uint16_t bound_hwidth = 0;
	uint16_t best_priority;
	uint16_t repair_slot;
#ifdef TIE_MODERN
	/* PORT: high-rate flight advances per-craft AI timers on the AI cadence. */
	uint16_t ai_timer_ticks = TieFlightCadence_AiTimerTicks();
#endif

	/* 1. Per-slot timer decrement. */
	for (i = 0; i < 20; ++i) {
		if (timers[i] != 0) {
			timers[i] -= frameticks;
			if (timers[i] < 0)
				timers[i] = 0;
		}
	}

	/* 2. Per-craft AI/plan timer decrement. RETAIL: NUM_CRAFTS = 32. */
	for (i = 0; i < NUM_CRAFTS; ++i) {
		if (objects[i].ship_idx != 0) {
			craftptr = objects[i].craft_ptr;
#ifdef TIE_MODERN
			if (craftptr->ai_update_rate_copy != 0)
				craftptr->ai_update_rate_copy -= ai_timer_ticks;
			if (craftptr->maneuver_timer != 0) {
				craftptr->maneuver_timer -= ai_timer_ticks;
				if (craftptr->maneuver_timer < 0)
					craftptr->maneuver_timer = 0;
			}
			if (craftptr->ai_plan_state != 0) {
				craftptr->ai_plan_state -= ai_timer_ticks;
				if ((int16_t)craftptr->ai_plan_state < 0)
					craftptr->ai_plan_state = 0;
			}
#else
			if (craftptr->ai_update_rate_copy != 0)
				craftptr->ai_update_rate_copy -= frameticks;
			if (craftptr->maneuver_timer != 0) {
				craftptr->maneuver_timer -= frameticks;
				if (craftptr->maneuver_timer < 0)
					craftptr->maneuver_timer = 0;
			}
			if (craftptr->ai_plan_state != 0) {
				craftptr->ai_plan_state -= frameticks;
				if ((int16_t)craftptr->ai_plan_state < 0)
					craftptr->ai_plan_state = 0;
			}
#endif
			if (craftptr->ion_drain_timer != 0) {
				craftptr->ion_drain_timer -= frameticks;
				/* Sign bit set: the countdown borrowed past zero. */
				if (craftptr->ion_drain_timer & 0x8000)
					craftptr->ion_drain_timer = 0;
			}
		}
	}

	/* 3. HUD target-blink ticker. Toggle the 0x400 bit, then pick 118
	 * (normal cycle) or 14 (micro-flicker for small/distant targets). */
	blinkticks -= frameticks;
	if (blinkticks < 0) {
		targetblinkstate ^= 0x0400;
		if (pstate.target_obj_idx != (uint16_t)0xFFFF) {
			pai_distancebetween(pstate.target_obj_idx, pstate.object_idx);
			if (pstate.target_obj_idx < 0x3800)
				tgt_species = objects[pstate.target_obj_idx].ship_idx;
			else
				tgt_species = staticobjects[pstate.target_obj_idx - 0x3800].species;
			trig2_polardistance >>= 5;
			bound_hwidth = species_table[tgt_species].bound_hwidth;
		}
		if (targetblinkstate & 0x0400) {
			if ((int)bound_hwidth > trig2_polardistance)
				blinkticks = 118;
			else
				blinkticks = 14;
		} else {
			if ((int)bound_hwidth > trig2_polardistance)
				blinkticks = 14;
			else
				blinkticks = 118;
		}
	}

	/* 4. Publish target HUD state and tick the mission clock. */
	currenttarget = pstate.target_obj_idx | (targetblinkstate | targetblinkflag);
	currenttargetcomp = pstate.radar_target1;

	if (hyperspaceflag != 0)
		return; /* hyperspace freezes the mission clock */

	/* On sub-second underflow, reload with one mission-second worth of
	 * ticks (236) and cascade into seconds / minutes / hours. */
	date.subsec -= frameticks;
	if (date.subsec > 0)
		return;

	date.subsec += 236;
	if (++date.second >= 60) {
		date.second = 0;
		if (++date.minute >= 60) {
			date.minute = 0;
			if (++date.hour >= 24)
				date.hour = 0;
		}
	}

	/* Mission time-limit countdown. */
	if (--timeleft.second == 255) {
		timeleft.second = 59;
		if (--timeleft.minute == 255) {
			timeleft.second = 0;
			timeleft.minute = 0;
			if (mission.train_craft_type) { /* training mission */
				user_checkreplaycamera();
				mission.end_flag = 1;
				mission.player_status = 3;
			}
		}
	}
	/* Last-15-second timer warning beep. */
	if (mission.train_craft_type && timeleft.minute == 0 && timeleft.second < 15)
		fsfx_triggersfx(0x20, 0xFFFF);

	/* 5. Repair the highest-priority offline subsystem. Zero health marks
	 * a system under repair; its timer counts down once per mission second.
	 * At zero, restore full health and re-enable the subsystem. */
	best_priority = 0xFFFF;
	repair_slot = 0xFFFF;
	for (i = 0; i < 10; ++i) {
		if (pstate.subsystem_health_percent[i] == 0 && pstate.subsystem_repair_priority[i] < best_priority) {
			repair_slot = i;
			best_priority = pstate.subsystem_repair_priority[i];
		}
	}

	for (i = 0; i < 10; ++i) {
		if (pstate.subsystem_health_percent[i] == 0 && i == repair_slot) {
			if (pstate.subsystem_repair_seconds[i] == 0) {
				pstate.subsystem_health_percent[i] = 100;
				pstate.player_craft->status_flags |= systemmask[i];
				argtable[0] = damagemsg[i];
				argtable[1] = 26; /* "repaired" suffix template */
				msg_messageprintf(MSG_SYSTEM_STATUS);
			} else {
				--pstate.subsystem_repair_seconds[i];
			}
		}
	}

	/* 6. Age every live object. RETAIL: NUM_OBJECTS = 120. */
	for (i = 0; i < NUM_OBJECTS; ++i)
		if (objects[i].ship_idx)
			++objects[i].age_ticks;

	/* 7. Tick all queued cockpit messages forward. */
	msg_updatemessageage();
}

/* Throttled iMUSE state evaluator. Training progress, objective state,
 * hostile proximity, missile locks, and force balance determine the music
 * state and intensity. */
// FUNCTION: TIE95 0x57C7C
void tie_updatemusic(void) {
	uint16_t ships_per_side[6];
	uint16_t i;
	uint32_t min_distance;
	int16_t state;
	uint16_t secondary_killed;
	uint16_t secondary_total;
	uint16_t closest;
	uint16_t intensity;
	uint16_t missile_lock;
	uint16_t primary_killed;
	uint16_t primary_total;

	if (!musicenabled || !music_buffer || timers[TIMER_MUSIC_CHANGE] != 0)
		return;
	intensity = 0;
	timers[TIMER_MUSIC_CHANGE] = 59;

	if (mission.train_craft_type != 0) {
		/* Training: gate progress, or urgency once under 20 seconds. */
		if (timeleft.minute == 0 && timeleft.second < 20)
			state = 8;
		else if ((uint16_t)mission.train_gates_remaining < 2)
			state = 9;
		else if ((uint16_t)mission.train_gates_remaining < 3)
			state = 7;
		else
			state = 6;
	} else if (mission.primary_complete == 2) {
		state = 10; /* won */
	} else if (timers[TIMER_PRI_COMPLETE] != 0 || timers[TIMER_SEC_COMPLETE] != 0) {
		state = 11; /* objective hold */
	} else {
		closest = 0xFFFF;
		min_distance = 0xFFFFFFFFu;
		for (i = 0; i < 6; ++i)
			ships_per_side[i] = 0;

		/* Tally ships per side weighted by genus and find the closest
		 * hostile. RETAIL: scan first NUM_CRAFTS (32) slots. */
		for (i = 0; i < NUM_CRAFTS; ++i) {
			if (objects[i].ship_idx == 0)
				continue;
			if (objects[i].genus == GENUS_STARSHIP || objects[i].genus == GENUS_PLATFORM)
				ships_per_side[objects[i].side] += 4;
			else if (objects[i].genus == GENUS_TRANSPORT || objects[i].genus == GENUS_FREIGHTER)
				ships_per_side[objects[i].side] += 2;
			else
				++ships_per_side[objects[i].side];

			if (objects[i].side != pstate.player->side && objects[i].craft_ptr->status_flags != 0) {
				pai_roughdistancebetween(i, pstate.object_idx);
				/* Capital ships and freighters count as closer. */
				if (objects[i].genus == GENUS_STARSHIP)
					roughdistance >>= 2;
				if (objects[i].genus == GENUS_PLATFORM)
					roughdistance >>= 2;
				if (objects[i].genus == GENUS_FREIGHTER)
					roughdistance >>= 1;
				if (min_distance > (uint32_t)roughdistance) {
					min_distance = roughdistance;
					closest = i;
				}
			}
		}

		if (closest == 0xFFFF) {
			/* No hostile in range. */
			if (mission.primary_complete == 1)
				state = 11;
			else if (!entercombatflag)
				state = 1;
			else
				state = 2;
		} else {
			uint32_t combat_thresh;

			/* Hostile present: pick the combat-far vs combat-near threshold. */
			combat_thresh = !entercombatflag ? 0x20000u : 0x40000u;
			if (min_distance > combat_thresh) {
				/* Far away: ramp intensity 5 -> 0 as we go further. */
				intensity = (uint16_t)(5 - ((min_distance - combat_thresh) >> 15));
				state = 1;
				if (intensity >= 0x8000)
					intensity = 0;
			} else {
				/* Within attack range: scan AI fighter slots for a missile
				 * lock on the player. RETAIL: slots 48..79. */
				entercombatflag = 1;
				missile_lock = 0;
				for (i = 48; i < 80; ++i) {
					if (objects[i].ship_idx != 0 && objects[i].craft_ptr->species_idx != 0 &&
						objects[i].craft_ptr->missile_target == pstate.object_idx)
						missile_lock = 1;
				}
				if (missile_lock) {
					state = 8; /* urgent */
				} else if (min_distance > 0x10000u) {
					/* Mid-range default: pick by which sides are alive. */
					if (ships_per_side[0])
						state = 3;
					else if (ships_per_side[4])
						state = 5;
					else
						state = 4;
				} else {
					primary_total = 0;
					primary_killed = 0;
					secondary_total = 0;
					secondary_killed = 0;

					/* Detect a big primary or secondary goal with all but
					 * one flight group done. */
					for (i = 0; i < mission_file_header.num_fg; ++i) {
						if (mission.primary_fg[i])
							++primary_total;
						if (mission.primary_fg[i] == 1)
							++primary_killed;
						if (mission.secondary_fg[i])
							++secondary_total;
						if (mission.secondary_fg[i] == 1)
							++secondary_killed;
					}

					if ((primary_total > 3 && primary_killed + 1 == primary_total) ||
						(secondary_total > 3 && secondary_killed + 1 == secondary_total)) {
						state = 9;
					} else if (min_distance < 0x8000 && (objects[closest].genus == GENUS_STARSHIP ||
														 objects[closest].genus == GENUS_PLATFORM)) {
						state = 8; /* outnumbered */
					} else {
						uint16_t hostile_score;
						uint16_t hostile_pct;

						/* Compare hostile vs ally weighted score. Sides 0/4
						 * always count; sides 2/3/5 count when their IFF
						 * name starts with '1'. */
						hostile_score = ships_per_side[0] + ships_per_side[4];
						if ((int8_t)mission_file_header.mission.neutral_name[0][0] == '1')
							hostile_score += ships_per_side[2];
						if ((int8_t)mission_file_header.mission.neutral_name[1][0] == '1')
							hostile_score += ships_per_side[3];
						if ((int8_t)mission_file_header.mission.neutral_name[3][0] == '1')
							hostile_score += ships_per_side[5];

						if (hostile_score <= ships_per_side[1]) {
							state = 7; /* winning */
						} else {
							hostile_pct = math2_percentage(ships_per_side[1], hostile_score);
							if (hostile_pct >= 0xE000) /* >= 87.5% */
								state = 7;
							else if (hostile_pct >= 0x8000) /* >= 50% */
								state = 6;
							else
								state = 8;
						}
					}
				}
			}
		}
	}

	if (intensity > 5)
		intensity = 5;
	lastmusicstate = (uint8_t)state;
	fscript_MsSetState(state);
	fscript_MsSetAttribute(0, (int16_t)intensity);
	fscript_MsRefreshScript();
}
