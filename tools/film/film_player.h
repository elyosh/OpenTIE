/* Stateful FILM playback using per-cel Landru record semantics. Palette fades
 * and sound are not simulated; CUST actors use a placeholder. Composition is
 * clipped to each actor frame and draws descending zplanes, with lower values
 * on top. */
#ifndef FILM_FILM_PLAYER_H
#define FILM_FILM_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "film.h"
#include "imgbake/anim.h"
#include "imgbake/delt.h"
#include "imgbake/palette.h"
#include "lfd_file.h"
#include "stream_player.h"

#include <stdbool.h>
#include <stdint.h>

/* Default framebuffer dimensions — VGA 320×200, the dominant target.
 * Use PLAYER_SVGA_FB_W / _H for the 640×480 SVGA build of the game
 * (same data formats; assets authored at the higher resolution).
 * Players carry their dim per instance — see TieFilmPlayer.fb_w/h. */
#define PLAYER_FB_W 320
#define PLAYER_FB_H 200
#define PLAYER_SVGA_FB_W 640
#define PLAYER_SVGA_FB_H 480

typedef struct {
	/* Identity (from FilmObject array). */
	uint32_t res_type; /* FCC_DELT/ANIM/RAW/CUST/PLTT/VIEW/VOIC/GMID */
	char res_name[9];
	uint16_t entry_index;
	uint16_t type_code; /* FILM_TC_VIEW/ACTOR/PALETTE/SOUND */

	/* Record stream (points into the FilmObject; valid for player lifetime). */
	const uint8_t* records;
	uint16_t records_size;
	uint16_t offset;

	/* ACTOR state. */
	int16_t x, y, xf, yf;
	int16_t xv, yv, xvf, yvf;
	int16_t state, state_f, state_v, state_vf;
	int16_t var1, var2;
	int16_t zplane;
	/* Clip rect (FCMD_ACTOR_CLIP). After rewind: canvas (0,0,320,200). */
	int16_t frame_l, frame_t, frame_r, frame_b;
	int16_t frame_vl, frame_vt, frame_vr, frame_vb;
	bool show;
	bool hflip, vflip;

	/* Pre-decoded sprite. For DELT/RAW: sprite.pixels != NULL.
	   For ANIM: anim.count > 0. For other types: both empty. */
	Image8 sprite;
	AnimImage anim;
	/* Natural bounds (DELT/RAW = sprite bbox; ANIM = union of all frame
	   bboxes). Used as the flip-mirror axis. */
	int16_t bounds_l, bounds_t, bounds_r, bounds_b;
	/* Current sprite dimensions for the displayed frame. Updated
	   when state changes. */
	int16_t w, h;

	/* PALETTE FilmObject only: copy of the PLTT resource bytes. Empty
	   (pltt_size == 0) for non-PALETTE objects or when the PLTT is
	   missing in the LFD chain. */
	uint8_t* pltt_data;
	uint32_t pltt_size;

	/* True iff the resource named by (res_type, res_name) was found in
	   the LFD chain at load time. False means the actor will silently
	   composite to nothing or the PLTT swap will be a no-op — surface
	   this in the UI so users notice missing --extra LFDs. */
	bool resource_loaded;

	/* CUST stream actor (engine path: play1_film_Callback installs a
	 * decode callback when var1 == 123). Detected post-rewind. The
	 * TieFilmStreamSession is owned by TieFilmPlayer (see streams[]), this is a
	 * non-owning back-pointer or NULL when no .WRK was bound. */
	bool is_stream;
	TieFilmStreamSession* stream;

	/* GUI state — caller-set, ignored by playback. */
	bool gui_hidden;
	/* Debug override: when true, the compositor renders this actor
	 * even if its FILM-script `show` flag is false. Used by filmview
	 * to inspect engine-driven actors that have no ACTOR_SHOW record
	 * in the FILM script (e.g. reg-dora in REGISTER.LFD, whose
	 * visibility is flipped imperatively by register.c::user_Door).
	 * `gui_hidden` still wins — checking that lets the user hide an
	 * individual actor regardless. */
	bool force_show;
} TieFilmPlayerObject;

typedef struct {
	const TieLfdFileChain* chain;

	char film_name[9];
	TieFilmHeader header; /* points into chain memory */

	/* Default palette = the "standard" PLTT in the chain (engine's
	   def_palette). Loaded once at init; NULL pltt_data if absent. */
	uint8_t* def_pltt_data;
	uint32_t def_pltt_size;

	TieFilmPlayerObject* objects;
	int object_count;

	Palette palette;  /* current screen palette */
	uint16_t cur_cel; /* next cel to process; after rewind = 1 */

	/* Framebuffer dimensions, set at init time. 320×200 default;
	 * 640×480 for the SVGA build via player_init_with_dims. */
	int fb_w, fb_h;
	uint8_t* indexed; /* heap-allocated [fb_w * fb_h] */

	/* Stream sessions for CUST/var1=123 actors. Owned. The pool is
	 * fixed-size — real films carry one stream actor; the cap exists
	 * only to make storage trivially scoped. */
#define PLAYER_MAX_STREAMS 4
	TieFilmStreamSession streams[PLAYER_MAX_STREAMS];
	int stream_count;
} TieFilmPlayer;

/* Initialize at the default 320×200 dim (VGA). */
bool TieFilmPlayer_Init(TieFilmPlayer* p, const TieLfdFileChain* chain, const TieLfdFile* primary,
						const TieLfdFileEntry* film_entry);

/* Initialize at an explicit framebuffer dim. fb_w/fb_h must be > 0;
 * typical values are (320,200) for the VGA build and (640,480) for
 * the SVGA build. The chosen dim drives the canvas-default
 * ACTOR_CLIP rect (rewind sets `frame = (0,0,fb_w,fb_h)` per
 * lfilm_Rewind_Actor_Film) so larger sprites in the SVGA assets
 * actually have somewhere to land. */
bool TieFilmPlayer_InitWithDims(TieFilmPlayer* p, const TieLfdFileChain* chain, const TieLfdFile* primary,
								const TieLfdFileEntry* film_entry, int fb_w, int fb_h);

void TieFilmPlayer_Free(TieFilmPlayer* p);

/* "displayed cel" = cur_cel - 1: the most recent cel whose records have
   been applied. -1 before the first rewind. */
int TieFilmPlayer_DisplayedCel(const TieFilmPlayer* p);
int TieFilmPlayer_TotalCels(const TieFilmPlayer* p);

/* Re-init all state to cel 0 displayed (engine: Rewind_Film_Objects). */
void TieFilmPlayer_Rewind(TieFilmPlayer* p);

/* Apply one cel; clamps at total_cels - 1. */
void TieFilmPlayer_Step(TieFilmPlayer* p);

/* Display cel `target`. Rewinds if target < current. */
void TieFilmPlayer_Seek(TieFilmPlayer* p, int target);

/* Composite the visible actors into p->indexed (320x200, palette
   indices). Must be called before player_render_rgba to refresh the
   image. Respects each TieFilmPlayerObject's gui_hidden flag. */
void TieFilmPlayer_Composite(TieFilmPlayer* p);

/* Convert p->indexed to RGBA8 (320*200*4 bytes) using the current
   screen palette. */
void TieFilmPlayer_RenderRgba(const TieFilmPlayer* p, uint8_t* rgba);

/* Compute the on-screen rect (inclusive-exclusive) of an ACTOR's
   current sprite frame, accounting for HFLIP/VFLIP. Returns false for
   non-ACTOR objects or when the frame is empty. */
bool TieFilmPlayer_ActorScreenRect(const TieFilmPlayerObject* o, int* l, int* t, int* r, int* b);

/*
 * Bind a .WRK stream file to a CUST/var1=123 actor in this film.
 * Looked up by entry_index (index into objects[]); call after
 * player_init has applied cel-0 records so var1 is observable.
 *
 * Returns false if the entry isn't a stream actor, the player's
 * stream pool is full, or the file can't be opened. On success, the
 * actor renders the decoded frame each cel; cur_cel resets to 0 of
 * the stream so the player view stays in sync with the FILM cursor.
 *
 * After calling, the caller MUST invoke player_rewind() (or rely on
 * the player_step loop starting from cel 0) to align the stream
 * cursor with the FILM cursor. player_init already does the rewind;
 * later binds re-rewind via this function.
 */
bool TieFilmPlayer_BindStream(TieFilmPlayer* p, int entry_index, const char* wrk_path);

/* Detach and close the stream session bound to this actor (if any).
 * No-op when the actor isn't a stream actor or has no session. */
void TieFilmPlayer_UnbindStream(TieFilmPlayer* p, int entry_index);

/* Helper: find the first TieFilmPlayerObject that's a stream actor (CUST
 * with var1==123). Returns -1 if there is none. */
int TieFilmPlayer_FindStreamActor(const TieFilmPlayer* p);

#ifdef __cplusplus
}
#endif

#endif
