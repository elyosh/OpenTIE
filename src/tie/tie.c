/* Flight-engine globals and per-frame driver. */

#include "tie/tie.h"
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
#include "tie_runtime/audio/imuse_session.h"
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
StaticObject staticobjects[NUM_STATIC_OBJECTS];
// GLOBAL: TIE95 0xEB70C
uint16_t framerate = 20; /* populated by the frame-pacer; fallback value avoids divide-by-zero before init */
// GLOBAL: TIE95 0xEB712
uint16_t frameticks;

/* Rendering pipeline scratch (set by pai/fview each frame). */
// GLOBAL: TIE95 0xEB0E0
int32_t rotatedx;
// GLOBAL: TIE95 0xEB0E4
int32_t rotatedy;
// GLOBAL: TIE95 0xEB0E8
int32_t rotatedz;
// GLOBAL: TIE95 0xEAC34
int32_t craftmoveX;
// GLOBAL: TIE95 0xEAC38
int32_t craftmoveY;
// GLOBAL: TIE95 0xEAC3C
int32_t craftmoveZ;

/* spec_data[] lives in spec.c (its watdbg owning module). */
// GLOBAL: TIE95 0xEB29C
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
// GLOBAL: TIE95 0xEAC10
int32_t rotworldeyeA2;
// GLOBAL: TIE95 0xEAC14
int32_t rotworldeyeA3;
// GLOBAL: TIE95 0xEABC8
int32_t rotworldeyeB1;
// GLOBAL: TIE95 0xEABCC
int32_t rotworldeyeB2;
// GLOBAL: TIE95 0xEAC18
int32_t rotworldeyeB3;
// GLOBAL: TIE95 0xEABB8
int32_t rotworldeyeC1;
// GLOBAL: TIE95 0xEABBC
int32_t rotworldeyeC2;
// GLOBAL: TIE95 0xEABC4
int32_t rotworldeyeC3;

/* Perspective-projection constants (set by TIE_InitFlightResolution per
 * selected flight resolution). */
// GLOBAL: TIE95 0xEB771
uint8_t perspShift;
// GLOBAL: TIE95 0xEB13C
int32_t perspFactor;
// GLOBAL: TIE95 0xEB140
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
// GLOBAL: TIE95 0xEABF8
int32_t calcf2;
// GLOBAL: TIE95 0xEABFC
int32_t calcf3;
// GLOBAL: TIE95 0xEABDC
int32_t calcU1;
// GLOBAL: TIE95 0xEABE0
int32_t calcU2;
// GLOBAL: TIE95 0xEABE4
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
Camera camera;

// GLOBAL: TIE95 0xEB0C4
int32_t worldlocx;
// GLOBAL: TIE95 0xEB0C8
int32_t worldlocy;
// GLOBAL: TIE95 0xEB0D0
int32_t worldlocz;
// GLOBAL: TIE95 0xEAC40
int32_t worldx;
// GLOBAL: TIE95 0xEAC44
int32_t worldy;
// GLOBAL: TIE95 0xEAC48
int32_t worldz; /* watdbg-owned by tie.c */
// GLOBAL: TIE95 0xEB72C
uint16_t yAspect; /* watdbg-owned by tie.c; 0 = square pixels */
// GLOBAL: TIE95 0xEB6AE
int16_t objectsize;
// GLOBAL: TIE95 0xEB75F
// GLOBAL: TIE98 0x5A2748
uint8_t gouraudflag;
// GLOBAL: TIE95 0xEAC54
int32_t objecteyex;
// GLOBAL: TIE95 0xEAC58
int32_t objecteyey;
// GLOBAL: TIE95 0xEAC5C
int32_t objecteyez;

/* --- Swept-segment globals for collision pipeline. tie.c. --- */
// GLOBAL: TIE95 0xEAB90
int32_t laserx;
// GLOBAL: TIE95 0xEAB94
int32_t lasery;
// GLOBAL: TIE95 0xEAB98
int32_t laserz;
// GLOBAL: TIE95 0xEABD0
int32_t laserxold;
// GLOBAL: TIE95 0xEABD4
int32_t laseryold;
// GLOBAL: TIE95 0xEABD8
int32_t laserzold;
// GLOBAL: TIE95 0xEAB80
int32_t craftx;
// GLOBAL: TIE95 0xEABA8
int32_t crafty;
// GLOBAL: TIE95 0xEABAC
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
// GLOBAL: TIE95 0xEAB74
int32_t collidexoff;
// GLOBAL: TIE95 0xEAB78
int32_t collideyoff;
// GLOBAL: TIE95 0xEAB7C
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
uint8_t drawmarkingsflag;

/* --- Sound/input flags --- */

// GLOBAL: TIE95 0xEB769
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
typedef char CheckPlayerInFlightStateSize[sizeof(pstate) == 294 + 2 * (sizeof(void*) - 4) ? 1 : -1];

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
uint8_t hyperabortflag;

/* Hyperspace timing + state -- driven by anim_dohyperspace.
 *   hyperticks       -- cumulative frameticks since the warp started
 *   hyperstarlength  -- scratch used during the streak stretch/shrink math
 *   hypertemp1/2     -- saved drawbackdropflag / drawdebrisflag (restored
 *                       at the end of phase 5 so the post-warp scene comes
 *                       back with the same backdrop config) */
// GLOBAL: TIE95 0xEB722
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
uint8_t drawdebrisflag;

/* timers[20] -- the global cooldown bank. tie_updatetime decrements every
 * non-zero slot by frameticks and clamps to zero. Slots are named via the
 * TimerSlot enum in tie.h; consumers reset their slot to a tick count
 * (e.g. timers[TIMER_ANIM_UPDATE] = 29). */
// GLOBAL: TIE95 0xEB75C
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
int16_t replaypercent;
// GLOBAL: TIE95 0xEB6CC
int16_t recordingreplay;
// GLOBAL: TIE95 0xEAC50
int32_t replaytotalcnt;
// GLOBAL: TIE95 0xEAC60
int32_t replaymaxcnt;
// GLOBAL: TIE95 0xCD1F0
char replayclipname[14];
// GLOBAL: TIE95 0xCD1DC
char replaystartfile[10] = "start.rpy";
// GLOBAL: TIE95 0xCD1CF
char replaysavegamefile[13] = "savegame.rpy";
// GLOBAL: TIE95 0xCD1E6
char inputspoolfile[10] = "input.spl";
// GLOBAL: TIE95 0xEAC68
void* replayptr; /* write/read cursor into replaybuffer. */
// GLOBAL: TIE95 0xEB6AA
uint16_t replaybuffercnt; /* frames in the current 3071-slot page. */
// GLOBAL: TIE95 0xEAC4C
uint32_t replaytotalcntdown; /* playback counter (counts up toward replaytotalcnt). */
// GLOBAL: TIE95 0xEB6AC
uint16_t replayrandomseed;
// GLOBAL: TIE95 0xEB6C4
uint8_t replayviewmode;
// GLOBAL: TIE95 0xEB74C
uint8_t replayspoolflag; /* 1 = auto-spool to disk when buffer fills. */
// GLOBAL: TIE95 0xEB74F
uint8_t endgamereplayflag;
// GLOBAL: TIE95 0xEB751
uint8_t updateactionflag; /* 1 = replay is actively advancing */
// GLOBAL: TIE95 0xEB6B4
// GLOBAL: TIE98 0x592210
uint16_t replayavailable; /* 1 if a saved film is loadable. */

/* lasttargetnum is owned by panel.c per watdbg; declared extern in panel.h. */

/* Binary is u8 in both demo and retail. Readers only test for nonzero
 * and decrement, so the narrower type matches. */
// GLOBAL: TIE95 0xEB770
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
MissionClock _date;

/* Mission time-limit countdown (watdbg _timeleft[8], owned by tie.c).
 * Captured wholesale by the replay state-dump. */
// GLOBAL: TIE95 0xE638C
// GLOBAL: TIE98 0x5926C8
MissionClock timeleft;

/* TieStorage_Open(3) modes embedded in the binary as const char arrays; owned by tie.c
 * per watdbg. C stdlib fopen treats the first two chars the same way here. */
// GLOBAL: TIE95 0xCD1C6
const char _readmode[3] = { 'r', 'b', '\0' };
// GLOBAL: TIE95 0xCD1C9
const char _writemode[3] = { 'w', 'b', '\0' };
// GLOBAL: TIE95 0xCD1CC
const char _appendmode[3] = { 'a', 'b', '\0' };

/* Rendering scratch (owned by tie.c per watdbg). maxPixelsDeep is set by
 * tie_initflightresolution per video mode; numbitmaps and lightflag are
 * written each frame by the 3D pipeline (anim.c / draw.c / xtrans2.c). */
// GLOBAL: TIE95 0xEB154
int32_t maxPixelsDeep;
// GLOBAL: TIE95 0xEB6A8
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
 * they are now `_date.hour` / `_date.minute` / `_date.second`, fields of
 * the single MissionClock storage above.) */

/* (pstate.target_obj_idx: currently-targeted object slot; written by
 * USER_inputforplane and PANEL_*, read by collide_collisions for the
 * friendly-tag check.) */

/* --- Input state (read/written by FEINPUT, consumed by screen modules) --- */

// GLOBAL: TIE95 0xEB6DE
int16_t inputbuttons;
// GLOBAL: TIE95 0xEB6E8
int16_t inputkey;
// GLOBAL: TIE95 0xEB6D2
int16_t inputdeltax;
// GLOBAL: TIE95 0xEB6CE
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
// GLOBAL: TIE95 0xCD16C
int16_t detaillevel;

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
void* initgraph;
// GLOBAL: TIE95 0xEB0FC
void (*blank)(void);
// GLOBAL: TIE95 0xEB0F0
void (*unblank)(void);
// GLOBAL: TIE95 0xEB0CC
void (*buildpalette)(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count);
// GLOBAL: TIE95 0xEB0D8
void* savepalette;
// GLOBAL: TIE95 0xEB0F8
void* restorepalette;
// GLOBAL: TIE95 0xEB0C0
uint32_t (*calcposition)(uint16_t, uint16_t);
// GLOBAL: TIE95 0xEB0DC
void (*drawshape)(const void*, int16_t, int16_t, int16_t, uint16_t);
// GLOBAL: TIE95 0xEB0F4
void (*outchar)(int ch);
// GLOBAL: TIE95 0xEB100
// GLOBAL: TIE98 0x59222C
void (*clearwindow)(void);
// GLOBAL: TIE95 0xEB0B4
void (*fillbox)(uint16_t, uint16_t, uint16_t, uint16_t);
// GLOBAL: TIE95 0xEB0BC
void* savebox;
// GLOBAL: TIE95 0xEB0B8
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
uint8_t* farbufferptr;
/* Shape pointer table shared with maproom_swap_buffer_ptrs. */
// GLOBAL: TIE95 0xEAC80
uint8_t* farbufferptrs[265];
// GLOBAL: TIE95 0xEB0A4
void* fontptrtiny;
// GLOBAL: TIE95 0xEB0AC
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
uint8_t fastforwardflag;
// GLOBAL: TIE95 0xEB6BC
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

/* (Mission elapsed clock `_date` is defined above as a single
 * MissionClock storage; subsec is `_date.subsec`.) */

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
uint8_t deadflag_EB76C;
// GLOBAL: TIE95 0xEB774
uint8_t deadflag_EB774;

/* Last iMUSE music state pushed by tie_updatemusic. Latched here so the
 * UI / debug overlays can read what's currently playing. */
// GLOBAL: TIE95 0xEB747
uint8_t lastmusicstate;

/* Forward decls for static helpers. */
static bool tie_doframe_tie98(void);

/* Configure flight geometry for VGA or the 640x480 modes and select the
 * corresponding CP320/CP640 cockpit asset directory. */
// FUNCTION: TIE95 0x56048
// FUNCTION: TIE98 0x48D850
void tie_initflightresolution(void) {
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
		rtsvga2_setvesascanlinelength(0x400u); /* retained for parity */
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
// FUNCTION: TIE95 0x576E4
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
// FUNCTION: TIE95 0x57158
int tie_makelocallights(int obj_idx) {
	uint32_t max_distance_sq;

	FlightObject* src_obj = &objects[obj_idx];
	int model_scale_shift;
	int src_world_x;
	int src_world_y;
	int src_world_z;
	int light_idx;
	int light_count;
	uint16_t scan_idx;

	draw_lockshipfileptrs(src_obj->ship_idx);
	model_scale_shift = objectblockptr->model_scale_shift;

	/* Light reach scales with source ship size:
	 *   model_scale_shift == 0 -> 0x4000  (small craft, short reach).
	 *   model_scale_shift > 0  -> 0x8000 << (model_scale_shift - 1). */
	if (model_scale_shift)
		max_distance_sq = 0x8000u << (model_scale_shift - 1);
	else
		max_distance_sq = 0x4000u;

	src_world_x = src_obj->world_x;
	src_world_y = src_obj->world_y;
	src_world_z = src_obj->world_z;
	light_idx = 0;
	light_count = 0;

	for (scan_idx = 0; scan_idx < NUM_OBJECTS; ++scan_idx) {
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
		int side_proj;
		int fwd_proj;
		int up_proj;
		uint8_t ship_idx;

		if (expl->ship_idx == 0 || expl->genus != GENUS_EXPLOSION)
			continue;

		dx = expl->world_x - src_world_x;
		dy = expl->world_y - src_world_y;
		dz = expl->world_z - src_world_z;
		if ((uint32_t)collide_roughdistance3d(dx, dy, dz) >= max_distance_sq)
			continue;

		out = &localLights[light_idx];
		side_proj =
			(int32_t)src_obj->side_x * dx + (int32_t)src_obj->side_y * dy + (int32_t)src_obj->side_z * dz;
		if (side_proj >= 0x40000000)
			side_proj = 0x3FFFFFFF;
		if (side_proj <= -0x40000000)
			side_proj = -0x3FFF0000;
		out->x = side_proj >> 15;

		fwd_proj = (int32_t)src_obj->fwd_x * dx + (int32_t)src_obj->fwd_y * dy + (int32_t)src_obj->fwd_z * dz;
		if (fwd_proj >= 0x40000000)
			fwd_proj = 0x3FFFFFFF;
		if (fwd_proj <= -0x40000000)
			fwd_proj = -0x3FFF0000;
		out->y = -(fwd_proj >> 15);

		up_proj = (int32_t)src_obj->up_x * dx + (int32_t)src_obj->up_y * dy + (int32_t)src_obj->up_z * dz;
		if (up_proj >= 0x40000000)
			up_proj = 0x3FFFFFFF;
		if (up_proj <= -0x40000000)
			up_proj = -0x3FFF0000;
		out->z = up_proj >> 15;

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

/* Advance global and craft timers, target blinking, mission clock and warning,
 * pilot damage bookkeeping, object ages, and message ages. */
// FUNCTION: TIE95 0x577F4
void tie_updatetime(void) {
	/* systemmask[10] / damagemsg[10] declared in collide.h. */
	/* Polar distance scratch: trig2_polardistance is set by the binary's
	 * pai_distancebetween call inside the blink branch and re-read here. */

	/* 1. Per-slot timer decrement. */
	uint16_t i;
	uint16_t ai_timer_ticks;

#ifdef TIE_MODERN
	/* PORT: high-rate flight advances per-craft AI timers on the AI cadence. */
	ai_timer_ticks = TieFlightCadence_AiTimerTicks();
#else
	ai_timer_ticks = frameticks;
#endif

	for (i = 0; i < 20; ++i) {
		if (timers[i] != 0) {
			int16_t v = (int16_t)(timers[i] - frameticks);
			if (v < 0)
				v = 0;
			timers[i] = v;
		}
	}

	/* 2. Per-craft AI/plan timer decrement. RETAIL: NUM_CRAFTS = 32. */
	for (i = 0; i < NUM_CRAFTS; ++i) {
		FlightObject* obj = &objects[i];
		CraftData* cp;
		if (obj->ship_idx == 0)
			continue;
		cp = obj->craft_ptr;

		if (ai_timer_ticks && cp->ai_update_rate_copy)
			cp->ai_update_rate_copy = (uint16_t)(cp->ai_update_rate_copy - ai_timer_ticks);

		if (ai_timer_ticks && cp->maneuver_timer) {
			int v = cp->maneuver_timer - ai_timer_ticks;
			cp->maneuver_timer = (v < 0) ? 0 : v;
		}
		if (ai_timer_ticks && cp->ai_plan_state) {
			int16_t v = (int16_t)(cp->ai_plan_state - ai_timer_ticks);
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
				if ((int)bound_hwidth > trig2_polardistance)
					blinkticks = 118; /* big enough to hold steady */
				else
					blinkticks = 14; /* small/distant target: flicker */
			} else if ((int)bound_hwidth <= trig2_polardistance) {
				blinkticks = 118;
			} else {
				blinkticks = 14;
			}
		}
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
		return;

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
	--timeleft.second;
	if (timeleft.second == 255u) { /* sec underflowed */
		timeleft.second = 59;
		if ((--timeleft.minute) == 255u) { /* min underflowed -> done */
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
	if (mission.train_craft_type && timeleft.minute == 0 && timeleft.second < 15u)
		fsfx_triggersfx(0x20u, 0xFFFFu);

	/* 5. Repair the highest-priority offline subsystem. Zero health marks
	 * a system under repair; its timer counts down once per mission second.
	 * At zero, restore full health and re-enable the subsystem. */
	{
		uint16_t best_priority = 0xFFFFu;
		int16_t repair_slot = -1;

		uint16_t scan;
		uint16_t slot;

		for (scan = 0; scan < 10; ++scan) {
			if (pstate.subsystem_health_percent[scan] == 0) {
				int priority = pstate.subsystem_repair_priority[scan];
				if ((uint16_t)priority < best_priority) {
					repair_slot = (int16_t)scan;
					best_priority = (uint8_t)priority;
				}
			}
		}

		for (slot = 0; slot < 10; ++slot) {
			int16_t health_percent = (int16_t)pstate.subsystem_health_percent[slot];
			if (health_percent == 0 && (int16_t)slot == repair_slot) {
				int16_t repair_seconds = (int16_t)pstate.subsystem_repair_seconds[slot];
				if (repair_seconds) {
					pstate.subsystem_repair_seconds[slot] = (uint16_t)(repair_seconds - 1);
				} else {
					pstate.subsystem_health_percent[slot] = 100;
					/* Apply the per-system damage bit and emit the message.
					 * Binary dereferences player_craft unconditionally; if
					 * the pointer is NULL here we have a bigger problem. */
					pstate.player_craft->status_flags |= systemmask[slot];
					argtable[0] = damagemsg[slot];
					argtable[1] = 26; /* "repaired" suffix template */
					msg_messageprintf(MSG_SYSTEM_STATUS);
				}
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
	uint16_t ships_per_side[6] = { 0 };
	/* music_state / music_intensity are file-scope globals (see top of
	 * tie.c); we seed them fresh at entry to match the binary's
	 * per-frame reset semantics, but they persist in .bss so the replay
	 * state-dump can capture them. */
	uint32_t min_distance;
	int closest_enemy_obj;
	uint32_t combat_thresh;
	int has_missile_lock_on_player;
	uint16_t primary_kill_count;
	uint16_t secondary_total;
	uint16_t secondary_kill_count;
	uint16_t hostile_score;
	int hostile_pct;
	uint16_t obj_iter;
	uint16_t slot;

	music_state = 1;
	music_intensity = 0;
	min_distance = 0xFFFFFFFFu;
	closest_enemy_obj = 0xFFFF;
	combat_thresh = 0;
	has_missile_lock_on_player = 0;
	primary_kill_count = 0;
	secondary_total = 0;
	secondary_kill_count = 0;
	hostile_score = 0;
	hostile_pct = 0;

	/* Bail if music disabled, no buffer, or cooldown active. */
	if (!musicenabled || !music_buffer || timers[TIMER_MUSIC_CHANGE] != 0)
		return;
	timers[TIMER_MUSIC_CHANGE] = 59;

	/* --- Training mission paths ---------------------------------------- */
	if (mission.train_craft_type != 0) {
		if (timeleft.minute || timeleft.second >= 20u) {
			if ((uint16_t)mission.train_gates_remaining < 2u)
				music_state = 9;
			else if ((uint16_t)mission.train_gates_remaining < 3u)
				music_state = 7;
			else
				music_state = 6;
		} else {
			music_state = 8;
		}
	} else if (mission.primary_complete == 2) { /* won */
		/* --- Combat mission state ----------------------------------- */
		music_state = 10;
	} else if (timers[TIMER_PRI_COMPLETE] != 0 || timers[TIMER_SEC_COMPLETE] != 0) {
		music_state = 11; /* objective hold */
	} else {
		/* Tally ships per side weighted by genus + find the closest hostile. */
		/* RETAIL: scan first NUM_CRAFTS (32) slots. */
		for (obj_iter = 0; obj_iter < NUM_CRAFTS; ++obj_iter) {
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

		if ((uint16_t)closest_enemy_obj == 0xFFFFu) {
			/* No hostile in range. */
			if (mission.primary_complete == 1)
				music_state = 11;
			else if (entercombatflag)
				music_state = 2;
			else
				music_state = 1;
		} else {
			/* Hostile in range — pick combat-near vs combat-far threshold. */
			combat_thresh = entercombatflag ? 0x40000u : 0x20000u;
			if (min_distance > combat_thresh) {
				/* Far away: ramp intensity 5 -> 0 as we go further. */
				music_intensity = (uint16_t)(5 - ((min_distance - combat_thresh) >> 15));
				music_state = 1;
				if (music_intensity >= 0x8000u)
					music_intensity = 0;
			} else {
				/* Within attack range: scan AI fighter slots for missile lock
				 * on player. RETAIL: slots 48..79 (32 wide). Demo was 44..75. */
				entercombatflag = 1;
				for (slot = 48; slot < 80; ++slot) {
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
				} else if (min_distance <= 0x10000u) {
					/* Mid-to-close-range: walk the FG kill counts to detect
					 * "almost wiped". */
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

					if ((total_primary > 3u && primary_kill_count + 1 == total_primary) ||
						(secondary_total > 3u && secondary_kill_count + 1 == secondary_total)) {
						/* Big primary or secondary FG with all but 1 dead. */
						music_state = 9;
					} else {
						music_state = 8; /* outnumbered */
						/* Compare hostile vs ally weighted score (sides 0/4
						 * always count; sides 2/3/5 only count if their tag
						 * string starts with '1'). */
						if (min_distance >= 0x8000u ||
							(objects[(uint16_t)closest_enemy_obj].genus != GENUS_STARSHIP &&
							 objects[(uint16_t)closest_enemy_obj].genus != GENUS_PLATFORM)) {
							hostile_score = (uint16_t)(ships_per_side[4] + ships_per_side[0]);
							/* Sides 2/3/5 only count as hostile when their .TIE-file
							 * IFF tag string starts with '1' (mission_file_header.
							 * mission.neutral_name[side-2][0]). */
							if (mission_file_header.mission.neutral_name[0][0] == '1')
								hostile_score += ships_per_side[2];
							if (mission_file_header.mission.neutral_name[1][0] == '1')
								hostile_score += ships_per_side[3];
							if (mission_file_header.mission.neutral_name[3][0] == '1')
								hostile_score += ships_per_side[5];

							if (hostile_score <= ships_per_side[1]) {
								music_state = 7; /* winning */
							} else {
								hostile_pct = math2_percentage(ships_per_side[1], hostile_score);
								if (hostile_pct >= 57344) /* >= 87.5% */
									music_state = 7;
								else if (hostile_pct >= 0x8000) /* >= 50% */
									music_state = 6;
							}
						}
					}
				} else if (ships_per_side[0]) {
					/* Mid-range default: pick a music state based on which
					 * sides are alive. */
					music_state = 3;
				} else if (ships_per_side[4]) {
					music_state = 5;
				} else {
					music_state = 4;
				}
			}
		}
	}

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
// GLOBAL: TIE98 0x4F2B88
static const uint8_t cdmusic_start_min[4] = { 0, 4, 8, 12 };
// GLOBAL: TIE98 0x4F2B8C
static const uint8_t cdmusic_start_sec[4] = { 0, 1, 40, 52 };

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

/* PORT: returns false until one flight period has accumulated, in place of
 * the original busy-wait, so the flight task can yield to the host. */
// FUNCTION: TIE98 0x48D9B0
static bool tie_doframe_tie98(void) {
	TieFlightCadence ai_cadence;
	TieFlightCadence animation_cadence;
	int rendered;

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
#ifdef TIE_MODERN
	/* PORT: TIE98's pause loop is represented by the host task state. */
	if (TieFlightPause_IsActive())
		return true;
#endif
	if (mission.end_flag != 0 || mapflag != 0)
		return true;

	TieFlightTiming_BeginAdvance(frameticks);
	TIE_FLIGHT_TRACE_BEGIN_FRAME(frameticks, framerate);
	TieAiLead_Advance(frameticks);
	ai_cadence = TieFlightTiming_AdvanceAi(frameticks);
	animation_cadence = TieFlightTiming_AdvanceAnimation(frameticks);
	TieFlightCadence_SetAiTimerTicks(ai_cadence);
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_TIME);
	tie_updatetime();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	if (mission.train_craft_type == 0) {
		TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_FG_STATUS);
		create_updatefgstatus();
		TIE_FLIGHT_TRACE_OBSERVE_STATE();
	}
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_AI);
	TieFlightCadence_RunPlaneAi(ai_cadence);
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_WEAPONS);
	laser_weaponsfire();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_DYNAMICS);
	dynamix_planedynamics();

	rendered = 0;
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

	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_RENDER);
	if (drawdebrisflag && mission.train_craft_type == 0 && TieFlightTiming_LegacyDue())
		create_checkdebris();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_COLLISION);
	collide_collisions();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_MOVE);
	move_moveobjects();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_ANIMATION);
	TieFlightCadence_RunAnimation(animation_cadence);
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_OBJECTIVES);
	score_checkobjective();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	msg_messageupdate();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
#ifdef TIE_MODERN
	TieMusicPolicy_UpdateFlightMusic();
#else
	tie_updatemusic_tie98();
#endif
	if (blastflag) {
		FrontendSound_FlushQueuedSounds();
		if (blastcount)
			fsfx_checkblastqueue();
		fsfx_checktieflyby();
		fsfx_UpdatePlayerEngineSound();
	}

	if (rendered) {
		FrontendDisplay_PresentFrame();
		if (g_useHardware3D)
			RenderScene_ClearFrameBuffers();
		else
			FrontendDisplay_BlitOffscreenToRenderSurface();
	}
	TIE_FLIGHT_TRACE_END_FRAME();
	return true;
}

// FUNCTION: TIE95 0x56270
bool tie_doframe(void) {
	TieFlightCadence ai_cadence;
	TieFlightCadence animation_cadence;
	bool rendered;

	if (TIE_FLIGHT_TIE98)
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

#ifdef TIE_MODERN
	/* The tick budget was consumed even when world work is skipped. */
	if (TieFlightPause_IsActive())
		return true;
#endif
	if (mission.end_flag != 0 || mapflag != 0)
		return true;

	TieFlightTiming_BeginAdvance(frameticks);
	TIE_FLIGHT_TRACE_BEGIN_FRAME(frameticks, framerate);
	TieAiLead_Advance(frameticks);
	ai_cadence = TieFlightTiming_AdvanceAi(frameticks);
	animation_cadence = TieFlightTiming_AdvanceAnimation(frameticks);
	TieFlightCadence_SetAiTimerTicks(ai_cadence);
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_TIME);
	tie_updatetime();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	if (mission.train_craft_type == 0) {
		TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_FG_STATUS);
		create_updatefgstatus();
		TIE_FLIGHT_TRACE_OBSERVE_STATE();
	}
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_AI);
	TieFlightCadence_RunPlaneAi(ai_cadence);
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_WEAPONS);
	laser_weaponsfire();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_DYNAMICS);
	dynamix_planedynamics();

	/* Render gate (with accelerated-time skip): renders once every
	 * acceleratedtimesetting frames when set > 1. The live branch also
	 * redraws the cockpit panel. */
	rendered = false;
	if (replayviewmode) {
		if (fastforwardflag) {
			/* Fast-forward stalls rendering until fastforwardtimer drains
			 * one mission-second (236 ticks) worth of frame time. */
			if (frameticks > (uint16_t)fastforwardtimer) {
				fastforwardtimer += 236;
				if (acceleratedtimesetting <= 1u) {
					if (TIE_DISPLAY_DX5)
						FlightSurface_Lock();
					tie_updatescreen();
					if (TIE_DISPLAY_DX5)
						FlightSurface_Unlock();
					rendered = true;
				} else {
					if (acceleratedtimectr) {
						/* Skip the render — let the timer catch up. */
						tickcounter += (uint16_t)xtimer_time_elapsed();
						tickcounter += frameticks;
					} else {
						if (TIE_DISPLAY_DX5)
							FlightSurface_Lock();
						tie_updatescreen();
						if (TIE_DISPLAY_DX5)
							FlightSurface_Unlock();
						rendered = true;
						acceleratedtimectr = acceleratedtimesetting;
					}
					--acceleratedtimectr;
				}
			}
			fastforwardtimer -= (int16_t)frameticks;
		} else {
			if (acceleratedtimesetting <= 1u) {
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
				tie_updatescreen();
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
				rendered = true;
			} else {
				if (acceleratedtimectr) {
					/* Skip the render — let the timer catch up. */
					tickcounter += (uint16_t)xtimer_time_elapsed();
					tickcounter += frameticks;
				} else {
					if (TIE_DISPLAY_DX5)
						FlightSurface_Lock();
					tie_updatescreen();
					if (TIE_DISPLAY_DX5)
						FlightSurface_Unlock();
					rendered = true;
					acceleratedtimectr = acceleratedtimesetting;
				}
				--acceleratedtimectr;
			}
		}
	} else {
		if (acceleratedtimesetting <= 1u) {
			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			tie_updatescreen();
			panel_updatepanel();
			if (TIE_DISPLAY_DX5)
				FlightSurface_Unlock();
			rendered = true;
		} else {
			if (acceleratedtimectr) {
				/* Skip the render — let the timer catch up. */
				tickcounter += (uint16_t)xtimer_time_elapsed();
				tickcounter += frameticks;
			} else {
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
				tie_updatescreen();
				panel_updatepanel();
				if (TIE_DISPLAY_DX5)
					FlightSurface_Unlock();
				rendered = true;
				acceleratedtimectr = acceleratedtimesetting;
			}
			--acceleratedtimectr;
		}
	}

	/* Post-render world updates. */
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_RENDER);
	if (drawdebrisflag && mission.train_craft_type == 0 && TieFlightTiming_LegacyDue())
		create_checkdebris();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_COLLISION);
	collide_collisions();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_MOVE);
	move_moveobjects();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_ANIMATION);
	TieFlightCadence_RunAnimation(animation_cadence);
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	TIE_FLIGHT_TRACE_PHASE(TIE_TRACE_PHASE_OBJECTIVES);
	score_checkobjective();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
	msg_messageupdate();
	TIE_FLIGHT_TRACE_OBSERVE_STATE();
#ifdef TIE_MODERN
	TieMusicPolicy_UpdateFlightMusic();
#else
	tie_updatemusic();
#endif

	if (blastflag) {
		if (blastcount)
			fsfx_checkblastqueue();
		fsfx_checktieflyby();
	}
	if (rendered && TIE_DISPLAY_DX5) {
		FrontendDisplay_PresentFrame();
		FrontendDisplay_BlitOffscreenToRenderSurface();
	}
	TIE_FLIGHT_TRACE_END_FRAME();

	/* The application uploads vesa_buff_gbl at the end of the tick. */
	return true;
}

/* Per-frame world render: camera, flight objects, static objects, rasterizer,
 * bitmap queue, and starfield. */
// FUNCTION: TIE95 0x56574
void tie_updatescreen(void) {
	int16_t obj_iter;
	int16_t i;

#ifdef TIE_MODERN
	if (TIE_FLIGHT_TIE98) {
		/* PORT: keep host-only frame state outside recovered TIE98 TIE_Update_Screen. */
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
			camera.cam_pitch = trig2_zangle;
			camera.cam_heading = trig2_xyangle;
		}
		fview_newcalcview(camera.roll, camera.cam_pitch, camera.cam_heading, 0, (int16_t)camera.side_angle,
						  (int16_t)camera.up_angle, NULL);
		TieFlightSnapshot_RecordCameraBasis();
	} else if ((camera.view_zoom_flag && camera.view_target_tracking == 0) ||
			   (camera.view_target_tracking != 0 && camera.view_camera_control != 0)) {
		TieChaseCamera_Update();

		fview_newcalcview(camera.roll, camera.cam_pitch, camera.cam_heading, 0, (int16_t)camera.side_angle,
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
	} else if (camera.view_target_tracking != 0) {
		panel_pointcamera(camera.view_target_obj, 0);
		TieFlightSnapshot_RecordCameraBasis();
	} else {
		/* Default: camera follows camera.view_target_obj's exact position+orient. */
		FlightObject* o = &objects[camera.view_target_obj];
		camera.roll = o->roll;
		camera.cam_pitch = o->pitch;
		camera.cam_heading = o->heading;
		fview_newcalcview(camera.roll, camera.cam_pitch, camera.cam_heading, camera.yaw,
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
	for (obj_iter = 0; obj_iter < (int16_t)NUM_OBJECTS; ++obj_iter) {
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
				fview_newcalcrotate(obj->roll, obj->pitch, obj->heading, 0, obj);
				if (TIE_FLIGHT_TIE98) {
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
					fview_newcalcrotate(obj->roll, obj->pitch, obj->heading, 0, obj);
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
					fview_newcalcrotate(obj->roll, obj->pitch, obj->heading, 0, obj);
					anim_drawverysimpleobject((uint16_t)obj_iter);
				}
				break;
		}
	}

	/* --- Step 4: static objects + hyperstars ------------------------ */
	for (i = 0; i < 64; ++i) {
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
				/* Asteroids (species 100..105) tumble per frame. */
				if (spec_idx >= 100 && spec_idx <= 105 &&
					(!TieFlightTiming_IsHighRate() || TieFlightTiming_LegacyDue())) {
					uint16_t f =
						TieFlightTiming_IsHighRate() ? TieFlightTiming_CompatibilityTicks() : frameticks;
					s->roll_byte = (uint8_t)((int)s->roll_byte + (((int)f * (i >> 4)) >> 4));
					s->pitch_byte = (uint8_t)((int)s->pitch_byte + (((int)f * (i >> 3)) >> 5));
					s->heading_byte = (uint8_t)((int)s->heading_byte + (((int)f * (4 - (i >> 4))) >> 4));
				}
				fview_newcalcrotate((int16_t)((uint16_t)s->roll_byte << 8),
									(int16_t)((uint16_t)s->pitch_byte << 8),
									(int16_t)((uint16_t)s->heading_byte << 8), 0, NULL);
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

		tie_initflightresolution();
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
				tickcounter += xtimer_time_elapsed();
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
					continuation->saved_master_vol = (uint16_t)imuse_get_master_vol(im);
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
				saved_master_vol = (uint16_t)imuse_get_master_vol(im);
#endif
				{
					int32_t margin = screenXRes / 10;
					int32_t right = screenXRes - margin;
					int32_t top;
					int32_t bottom;
					imuse_set_master_vol(im, 0);
					imuse_pause(im);
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
							imuse_set_master_vol(im, saved_master_vol);
							imuse_resume(im);
#ifdef TIE_MODERN
							TieReplaySession_Begin();
							continuation->phase = TIE_SIM_PHASE_PROMPT_AFTER_VIEWER;
							return;
#else
#ifdef TIE98
							imuse_stop_all_sounds(im);
#endif
							replayio_replayscreen();
							blank();
							break;
#endif
						}
						if (ch == 'n' || ch == 'N') {
							imuse_set_master_vol(im, saved_master_vol);
							imuse_resume(im);
							imuse_stop_all_sounds(im);
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
	imuse_stop_all_sounds(im);
	imuse_filelist_unload_all(im);
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
