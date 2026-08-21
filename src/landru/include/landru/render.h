#ifndef LANDRU_RENDER_H
#define LANDRU_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#define LANDRU_TEXT_MAX_CHARS 80

typedef enum LandruRenderTarget {
	LANDRU_RENDER_TARGET_SCREEN,
	LANDRU_RENDER_TARGET_AUXILIARY,
} LandruRenderTarget;

typedef enum LandruActorRenderFlags {
	LANDRU_ACTOR_RENDER_VISIBLE = 1u << 0,
	LANDRU_ACTOR_RENDER_ACTIVE = 1u << 1,
	LANDRU_ACTOR_RENDER_HFLIP = 1u << 2,
	LANDRU_ACTOR_RENDER_VFLIP = 1u << 3,
	LANDRU_ACTOR_RENDER_REMAP_COLOR = 1u << 4,
} LandruActorRenderFlags;

typedef struct LandruActorRenderState {
	uint32_t res_type;
	char res_name[8];
	int16_t id;
	int16_t film_entry_index;
	int16_t x, y;
	int16_t w, h;
	int16_t zplane;
	int16_t state;
	uint16_t flags;
	int16_t xscale, yscale;
	int16_t clip_left, clip_top, clip_right, clip_bottom;
	int16_t prev_x, prev_y;
	int16_t prev_xv, prev_yv;
	int16_t xv, yv;
	int16_t xvf, yvf;
	int16_t prev_xscale, prev_yscale;
	int16_t fore_color;
} LandruActorRenderState;

typedef struct LandruDrawCommand {
	uint32_t res_type;
	char res_name[8];
	int16_t film_entry_index;
	int16_t x, y;
	int16_t w, h;
	int16_t state;
	int16_t clip_left, clip_top, clip_right, clip_bottom;
	uint16_t flags;
	int16_t fore_color;
	LandruRenderTarget target;
} LandruDrawCommand;

typedef struct LandruFilmRenderState {
	uint32_t res_type;
	char res_name[8];
	int16_t x, y;
	int16_t zplane;
	uint16_t cur_cel;
	uint16_t cels;
	uint16_t flags;
} LandruFilmRenderState;

typedef struct LandruTextCommand {
	char text[LANDRU_TEXT_MAX_CHARS];
	int16_t x, y;
	uint8_t color_index;
	uint8_t bold_color_index;
	uint8_t shadow_color_index;
	uint8_t shadow;
	uint8_t font_id;
	LandruRenderTarget target;
	int16_t clip_left, clip_top, clip_right, clip_bottom;
} LandruTextCommand;

typedef enum LandruPaintOp {
	LANDRU_PAINT_FILL_RECT,
	LANDRU_PAINT_FRAME_RECT,
	LANDRU_PAINT_HLINE,
	LANDRU_PAINT_VLINE,
	LANDRU_PAINT_PIXEL,
	LANDRU_PAINT_BEVEL,
	LANDRU_PAINT_FRAME_BEVEL,
	LANDRU_PAINT_DBEVEL,
	LANDRU_PAINT_FRAME_DBEVEL,
	LANDRU_PAINT_XOR_RECT,
	LANDRU_PAINT_SHADE_RECT,
} LandruPaintOp;

typedef struct LandruPaintCommand {
	LandruPaintOp op;
	uint8_t pressed;
	uint8_t colors[5];
	LandruRenderTarget target;
	int16_t x, y, w, h;
	int16_t clip_left, clip_top, clip_right, clip_bottom;
} LandruPaintCommand;

typedef enum LandruCursorKind {
	LANDRU_CURSOR_POINTER,
	LANDRU_CURSOR_WAIT,
	LANDRU_CURSOR_NONE,
} LandruCursorKind;

typedef struct LandruCursorRenderState {
	int16_t x, y;
	int16_t hot_x, hot_y;
	int16_t w, h;
	uint8_t visible;
	LandruCursorKind kind;
} LandruCursorRenderState;

typedef enum LandruFadeKind {
	LANDRU_FADE_NONE,
	LANDRU_FADE_ACTIVE,
} LandruFadeKind;

typedef struct LandruFadeRenderState {
	LandruFadeKind kind;
	uint8_t source_factor;
	uint8_t r, g, b;
	uint8_t freeze_frame;
} LandruFadeRenderState;

typedef struct LandruRenderSink {
	void* userdata;
	void (*actor)(void* userdata, const LandruActorRenderState* state);
	void (*draw)(void* userdata, const LandruDrawCommand* command);
	void (*film)(void* userdata, const LandruFilmRenderState* state);
	void (*text)(void* userdata, const LandruTextCommand* command);
	void (*paint)(void* userdata, const LandruPaintCommand* command);
	void (*cursor)(void* userdata, const LandruCursorRenderState* state);
	void (*fade)(void* userdata, const LandruFadeRenderState* state);
	void (*scene_clock)(void* userdata, int32_t frame, float progress, uint32_t period_us);
	bool (*is_scene_transition)(void* userdata);
} LandruRenderSink;

/* The table is copied. Passing NULL disables render publication. */
bool landru_set_render_sink(const LandruRenderSink* sink);

#endif
