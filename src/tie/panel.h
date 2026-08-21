#ifndef __PANEL_H__
#define __PANEL_H__

#include "tie/tie.h"
#include <stdint.h>

/* Cockpit panel and HUD state. panel_updatepanel is the per-frame entry point. */

/* ------------------------------------------------------------------ */
/* Panel-owned types                                                  */
/* ------------------------------------------------------------------ */

/*
 * HudInstrument -- one HUD widget slot. 95 entries loaded from .INT file.
 *   x, y           : screen position (resolution-dependent)
 *   param1, param2 : per-widget payload, interpreted by the widget
 *                    updater that consumes the row. The two bytes are
 *                    deliberately polysemic in the data file:
 *
 *     panel_updatelever / panel_updatemonolever (any idx)
 *         param1 = farbufferptrs[] base index (shape)
 *         param2 = drawshape skip_color (transparent colorkey)
 *
 *     panel_updatevalue (any idx)
 *         param1 = digit count
 *         param2 = default text color (overridden in critical/warning)
 *
 *     panel_updatesetting (slider)
 *         param1 = farbufferptrs[] base for unlit/lit pair
 *         param2 = unused (constant 253 passed instead)
 *
 *     panel_updatelasers (idx = weapon_group + 3)
 *         param1 = farbufferptrs[] base for the LED row
 *         param2 = mirror flag (nonzero => negate step + flip = 1)
 *
 *     panel_updatebeam (idx = 35)
 *         param1 = farbufferptrs[] base for the LED row
 *         param2 = drawmonoshape skip_color
 *
 *     panel_update3Dcrt / panel_pointcamera (idx = 2)
 *         param1 = CRT viewport width  (LOGBUF2_startPIP width)
 *         param2 = CRT viewport depth  (LOGBUF2_startPIP depth, and
 *                  pix divisor for HUD framing in panel_pointcamera)
 *
 * Naturally aligned in memory (every field already lands on its natural
 * boundary, so dropping the original pragma pack(2) doesn't change
 * sizeof). On-disk layout is the fixed 6-byte little-endian record
 * produced/consumed by the codec helpers below.
 */
typedef struct {
	uint16_t x;
	uint16_t y;
	uint8_t param1;
	uint8_t param2;
} HudInstrument;

#define HUDINSTRUMENT_DISK_SIZE 6u

void HudInstrument_decode(HudInstrument* dst, const uint8_t* src);

/*
 * PanelViewDef -- cockpit view slot. 28 entries loaded from .INT file.
 *   flags    : bit 7 (0x80) = inherit another view's panel data
 *              bit 6+7 (0xC0) = MIRRORED copy of view (flags - 0xC0)
 *   name     : LFD filename base (no extension)
 *   pos_x/y  : world-to-screen position for the view's logbuf
 *   width    : logbuf width (instrument coord origin)
 *   depth    : logbuf depth
 *   yoffset  : screen Y offset when this view is active
 *   title    : text label shown by view 17
 *
 * Naturally aligned in memory (the 1-byte flags + 9-byte name end at
 * offset 0x0A which is already 2-aligned, so dropping the original
 * pragma pack(2) doesn't change sizeof). On-disk layout is the fixed
 * 36-byte little-endian record produced/consumed by the codec helpers
 * below.
 */
typedef struct {
	uint8_t flags;
	char name[9];
	uint16_t pos_x;
	uint16_t pos_y;
	uint16_t width;
	uint16_t depth;
	int16_t yoffset; /* screen Y offset (signed — cockpit views can
						have negative yoffsets, e.g. main view = -19
						to push the 3D viewport up into the cockpit
						frame). Zero-extending as u16 into the i32
						transfm2_screenyoffset was breaking stars
						and every TRANSFM2_getscreen* caller. */
	char title[16];
} PanelViewDef;

#define PANELVIEWDEF_DISK_SIZE 36u

void PanelViewDef_decode(PanelViewDef* dst, const uint8_t* src);

/*
 * PanelViewPtrs -- loaded-data pointers for one PanelViewDef slot.
 * Filled by panel_tryEMSforpanels (preload) or panel_loadcontrolpanel
 * (lazy load in panel_dosetnewpilotview).
 *   handle   : nonzero if loaded (binary stores the XMEMHDL handle id)
 *   image    : panel bitmap (drawshape source)
 *   mask     : occlusion mask (panel_copymaskdata source)
 *   palette  : 64-entry palette for this view
 */
#pragma pack(push, 2)
typedef struct {
	uint16_t handle;
	void* image;
	void* mask;
	void* palette;
} PanelViewPtrs; /* 14 bytes */
#pragma pack(pop)

/*
 * RadarBlip -- one entry in the radar display buffers.
 * 3 words each (6 bytes). 48 per hemisphere list.
 */
#pragma pack(push, 2)
typedef struct RadarBlip {
	uint16_t x;
	uint16_t y;
	uint16_t color;
} RadarBlip; /* 6 bytes */
#pragma pack(pop)

/* ------------------------------------------------------------------ */
/* Panel-owned globals (defined in panel.c)                           */
/* ------------------------------------------------------------------ */

#define PANEL_NUM_VIEWS 28
#define PANEL_NUM_INSTRUMENTS 95
#define PANEL_NUM_BLIPS 48

/* Cockpit base directory + panel parts filename (appended to panelname). */
extern char cockpitdir[7];
extern char parts[11];

/* Palette-record FOURCC tag ("PALT" in the LFD). */
extern char xpal_id[5];

/* Shield LED color ramp (11 indices 0..10, used by shieldcolor[]). */
extern char shieldcolor[11];

/* Beam LED color ramp (4 entries: dim / mid / bright / dark-background). */
extern char beamcolors[4];

/* Working buffers. */
extern char panelfilename[32];
extern char panelname[32];
extern PanelViewDef panelviewdefs[PANEL_NUM_VIEWS];
extern PanelViewPtrs panelviewptrs[PANEL_NUM_VIEWS];
extern void* temppanelptr;       /* write cursor into the LFD payload buffer */
extern int32_t panelsloadedflag; /* 1 after panel_tryEMSforpanels succeeded */

/* Radar blip ring buffers. */
extern RadarBlip rightbliplist1[PANEL_NUM_BLIPS];
extern RadarBlip rightbliplist2[PANEL_NUM_BLIPS];
extern RadarBlip leftbliplist2[PANEL_NUM_BLIPS];
extern RadarBlip leftbliplist1[PANEL_NUM_BLIPS];
extern RadarBlip* oldrightbliplist;
extern RadarBlip* oldleftbliplist;
extern RadarBlip* newrightbliplist;
extern RadarBlip* newleftbliplist;

/* HUD instrument tables. */
extern HudInstrument instruments[PANEL_NUM_INSTRUMENTS];
extern int16_t oldinstruments[PANEL_NUM_INSTRUMENTS];

/* Transient radar/bracket state. */
extern int16_t oldbracketx, oldbrackety;
extern int16_t radary, radarx;
extern int16_t bracketx, brackety;
extern int16_t blipboxx, blipboxy;
extern int16_t oldblipboxx, oldblipboxy;
extern int16_t oldleftlistsize, newrightlistsize;
extern int16_t blipcolor;
extern int16_t oldrightlistsize, newleftlistsize;
extern int16_t lasttargetnum;
extern int16_t lastpilotpaneldraw;

/* Flag bytes. */
extern uint8_t lockflag;
extern uint8_t bracketflag;
extern uint8_t blipboxflag;
extern uint8_t blipptrflag;
extern uint8_t initpanelflag;
extern uint8_t searchpartsflag;
extern uint8_t panelpartsflag;
extern uint8_t panelmirrorflag;

/* Panel bitmap storage. */
extern void* panelpartsptr;

/* ------------------------------------------------------------------ */
/* PANEL API (44 functions)                                           */
/* ------------------------------------------------------------------ */

/* -- Top-level dispatchers -- */
void panel_initpanel(void);
void panel_updatepanel(void);
void panel_updateforwardpanel(void);
void panel_updatefullforward(void);
void panel_updatethreatdisplay(void);

/* -- Per-widget updaters -- */
void panel_updatelever(uint16_t idx, uint16_t value);
void panel_updatemonolever(uint16_t idx, uint16_t value);
void panel_updatevalue(uint16_t idx, uint16_t value, uint16_t flags);
void panel_updatesetting(uint16_t value, uint16_t idx, uint16_t count, int16_t step);
void panel_updatecovers(void);
void panel_updatecockpitdamage(void);
void panel_updatereplaystuff(void);

/* -- Flight-state indicators -- */
void panel_updatespeed(void);
void panel_updatethrottle(void);
void panel_updateclock(void);
void panel_updatepower(void);

/* -- Weapons -- */
void panel_updatelasers(void);
void panel_updateweapons(void);
void panel_updatehardpoint(uint16_t slot, uint16_t hp_idx, uint16_t flags);
void panel_updateshields(void);
void panel_updatebeam(void);
void panel_updateweaponwarnings(void);

/* -- Targeting / CMD -- */
void panel_updategunsight(void);
void panel_updateradar(void);
void panel_addbliptoradar(uint16_t obj_idx);
void panel_updatecmd(void);
void panel_buildobjectname(uint16_t obj_idx, uint8_t flags);
uint16_t panel_getcraftstatus(uint16_t obj_idx);
void panel_outputdistance(int32_t polar_dist);
void panel_updatethreatname(void);
void panel_updatethreatweapons(void);

/* -- Resource / view loader -- */
void panel_loadpaneldata(void);
void panel_forcenewviewdir(uint16_t view_idx);
void panel_dosetnewpilotview(uint16_t view_idx);
void panel_loadcontrolpanel(char* name, void** section_ptrs, uint16_t count);
void panel_tryEMSforpanels(void);
void panel_freeviewbufs(void);
void panel_resetpilotview(void);
void panel_loadpanelviewdefs(char* base_name);

/* -- Mask / 3D CRT / camera -- */
void panel_copymaskdata(char* mask_src, uint16_t width, uint16_t height, uint8_t mirror);
void panel_clearmaskdata(uint16_t width, uint16_t height);
void panel_update3Dcrt(uint16_t x, uint16_t y, uint16_t width, uint16_t depth, int16_t clear_runs);
void panel_drawboxinxtrans(int16_t left_x, int16_t top_y, uint16_t width, uint16_t height, uint8_t color);
void panel_pointcamera(uint16_t obj_idx, int16_t use_hud_size);
void panel_update3Dcrt_tie98(int x, int y, uint16_t width, uint16_t depth, int clear_runs);
void PANEL_Update3DCrtIfVisible(void);
int16_t panel_drawboxinxtrans_tie98(int x, int y, int width, int height, uint8_t color);
void panel_pointcamera_tie98(uint16_t obj_idx, int16_t use_hud_size);
uint16_t panel_AdjustXForRes(uint16_t x);

/* PANELRTS support routines (in panelrts.c). */
#include "tie/panelrts.h"

/* String tables (populated by fediskio_loadstringdata). */
extern char** waypointstrings;
extern void* diststring;
extern void* shieldstring;
extern void* hullstring;
extern void* sysstring;
extern void* targetstring;
extern void* nonestring;
extern void* ourstring;
extern void* currentorderstring;
extern void* notargetstring;
extern void* curtargetstring;
extern void* curdeststring;
extern void* distfromtargetstring;
extern void* disttodeststring;
extern void* componentnames;
extern void* timeremstring;
extern void* timetotargetstring;
extern void* timetodeststring;

/* Separator glyph strings (owned by tie.c in binary; sys2_calclength input).
 * Retail keeps a 3-space and a 2-space variant at distinct addresses;
 * the former is the '%' offset, the latter is the dist '.' offset. */
extern char separator_3_spaces[4]; /* "   \0"   — engine 0xC057C */
extern char separator_2_spaces[4]; /* "  \0\x1c" — engine 0xC0580 */
extern char separator_colon[4];    /* "00:\0"   — engine 0xC0584 */
extern char separator_period[4];   /* "00.\0"   — engine 0xC0588 */

#endif
