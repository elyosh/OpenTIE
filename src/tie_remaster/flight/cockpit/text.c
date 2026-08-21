/*
 * Cockpit HUD text composition (split out of cockpit_gpu.c). The build
 * functions mirror the engine's panel_update* writers one-to-one and
 * produce TieUIText[] records consumed by TieScene2dText_DrawFestringInSpace.
 */

#include "tie_remaster/flight/cockpit/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tie_remaster/flight/cockpit/common.h"
#include "tie_remaster/flight/cockpit/layout.h"
#include "tie_remaster/scene2d/text.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/storage/storage.h"

uint8_t TieCockpitText_RemapColor(uint8_t logical_color) {
	if (logical_color < 0x40)
		return logical_color;
	return TieTextSnapshot_ColorRemapTable()[logical_color];
}

/* Resolve a per-instrument absolute position from the layout when
 * authored; else fall back to ins.x + classic_offset (matches the
 * engine for 4:3 layouts where coord == classic).
 *
 *   right_at — right edge of a right-aligned text column.
 *   center_at — center X of a centered text column.
 *   label_at — absolute X for a label column (threat readout). */
static int16_t TieCockpitText_ResolveRightAt(const TieCockpitLayout* l, int id, int16_t ins_x,
											 int16_t classic_w) {
	if (l && id >= 0 && id < COCKPIT_LAYOUT_MAX_INSTRUMENTS && l->instruments[id].present &&
		l->instruments[id].right_at > 0)
		return l->instruments[id].right_at;
	return (int16_t)(ins_x + classic_w);
}

static int16_t TieCockpitText_ResolveCenterAt(const TieCockpitLayout* l, int id, int16_t ins_x,
											  int16_t classic_w) {
	if (l && id >= 0 && id < COCKPIT_LAYOUT_MAX_INSTRUMENTS && l->instruments[id].present &&
		l->instruments[id].center_at > 0)
		return l->instruments[id].center_at;
	return (int16_t)(ins_x + classic_w / 2);
}

static int16_t TieCockpitText_ResolveLabelAt(const TieCockpitLayout* l, int id, int16_t classic_x) {
	if (l && id >= 0 && id < COCKPIT_LAYOUT_MAX_INSTRUMENTS && l->instruments[id].present &&
		l->instruments[id].label_at > 0)
		return l->instruments[id].label_at;
	return classic_x;
}

bool TieCockpitText_Push(TieUIText* buf, int* cnt, int cap, int16_t x, int16_t y, const char* s,
						 uint8_t color) {
	if (*cnt >= cap)
		return false;
	TieUIText* t = &buf[(*cnt)++];
	memset(t, 0, sizeof *t);
	t->x = x;
	t->y = y;
	t->color_index = color;
	t->bold_color_index = color;
	t->font_domain = TIE_FONT_DOMAIN_COCKPIT;
	size_t i = 0;
	for (; i + 1 < sizeof t->text && s && s[i]; ++i)
		t->text[i] = s[i];
	t->text[i] = '\0';
	/* No-clip sentinel: compose_text treats clip_right<=clip_left (or
	 * clip_bottom<=clip_top) as "unclipped" and skips the scissor —
	 * matching the convention in compose_paint. Using INT16_MAX here
	 * would scale through layout.scale_x/y and trip Metal validation
	 * (scissor exceeds RT bounds). */
	t->clip_left = 0;
	t->clip_top = 0;
	t->clip_right = 0;
	t->clip_bottom = 0;
	return true;
}

/* ----- Message-bar text composition (msg.c::msg_messagedisplay) -----
 * Paints the body line + the "T:<N>x" time-warp readout. The body's
 * base color comes from the engine's type/side tables; bracket bytes
 * inside templates ('[' / ']') nudge the cursor color via 0xFE escapes
 * baked into the festring text. */
static const uint8_t k_fontcolorconvert[8] = { 0x42, 0x4A, 0x46, 0x4E, 0x52, 0x45, 0x42, 0x52 };
static const uint8_t k_radiosidecolors[4] = { 0x4A, 0x52, 0x46, 0x56 };
static const uint8_t k_eventsidecolors[6] = { 0x52, 0x4A, 0x46, 0x56, 0x4A, 0x56 };

/* msg_messagedisplay base-color picker. Returns the engine-logical
 * color and writes the starting body byte index so the type/side
 * prefix isn't drawn:
 *   type >= 8 : 0x42, body starts at 0
 *   type 0..7 : fontcolorconvert[type], body from byte 1
 *               (type 1 + body[1] in '0'..'3' → radiosidecolors,
 *                consumes byte 1; type 2 → eventsidecolors[side]) */
static uint8_t TieCockpitText_PickMsgBaseColor(const char* body, uint8_t msg_type, uint16_t side,
											   int* out_body_start) {
	const uint8_t type_byte = (uint8_t)body[0];
	if (type_byte >= 8) {
		*out_body_start = 0;
		return 0x42;
	}
	uint8_t color = k_fontcolorconvert[type_byte];
	int start = 1;
	if (type_byte == 1) {
		const uint8_t sub = (uint8_t)body[1];
		if (sub >= '0' && sub <= '3') {
			color = k_radiosidecolors[sub - '0'];
			start = 2;
		}
	} else if (type_byte == 2 && side < (sizeof k_eventsidecolors / sizeof k_eventsidecolors[0])) {
		color = k_eventsidecolors[side];
	}
	*out_body_start = start;
	(void)msg_type;
	return color;
}

/* Convert `body[start..]` to a festring string with 0xFE+color
 * escapes baked in. Tracks the engine's bracket-driven color nudges
 * (msg_messagedisplay): '[' = cur+1 (wrap 0xD4→0xD3), ']' = cur-1
 * (wrap 0xD3→0xD4); bracket bytes themselves are dropped. Caps at 70
 * printable glyphs (engine's chars_out < 0x46 gate). Auto-appends
 * '.' when the last printable isn't '?', '!', ':' or ' ' (templates
 * are authored without the trailing period). */
static int TieCockpitText_BuildMsgBodyWithColors(const char* body, int start, uint8_t base_color, char* out,
												 int out_cap) {
	int n = 0;
	int printed = 0;
	uint8_t cur = base_color;
	char last_ch = '\0';

#define APPEND(b)                                                                                            \
	do {                                                                                                     \
		if (n + 1 >= out_cap)                                                                                \
			goto done;                                                                                       \
		out[n++] = (char)(b);                                                                                \
	} while (0)

	for (int i = start; body[i] && printed < 70; ++i) {
		const unsigned char c = (unsigned char)body[i];
		if (c == '[') {
			cur = (cur == 0xD4u) ? (uint8_t)(cur - 1) : (uint8_t)(cur + 1);
			APPEND(0xFE);
			APPEND(cur);
			continue;
		}
		if (c == ']') {
			cur = (cur == 0xD3u) ? (uint8_t)(cur + 1) : (uint8_t)(cur - 1);
			APPEND(0xFE);
			APPEND(cur);
			continue;
		}
		APPEND(c);
		last_ch = (char)c;
		printed++;
	}

	if (printed > 0 && last_ch != '?' && last_ch != '!' && last_ch != ':' && last_ch != ' ') {
		APPEND('.');
	}

done:
	out[n] = '\0';
#undef APPEND
	return n;
}

int TieCockpitText_BuildmsgBarText(const TieSnapshot* snap, int line_top, int line_left, int line_right,
								   TieUIText* out, int cap) {
	int n = 0;

	/* Body — type/side base color + inline color escapes for the
	 * '[' / ']' nudges templates use to emphasise values (engine
	 * loop in msg.c::msg_messagedisplay, msg.c:155-174). */
	if (snap->hud.msg_bar.present) {
		int body_start = 0;
		const uint8_t logical = TieCockpitText_PickMsgBaseColor(
			snap->hud.msg_bar.body, snap->hud.msg_bar.msg_type, snap->hud.msg_bar.side, &body_start);
		const uint8_t base = TieCockpitText_RemapColor(logical);
		char encoded[sizeof((TieUIText*)0)->text];
		int len = TieCockpitText_BuildMsgBodyWithColors(snap->hud.msg_bar.body, body_start, base, encoded,
														(int)sizeof encoded);
		if (len > 0) {
			if (TieCockpitText_Push(out, &n, cap, (int16_t)line_left, (int16_t)line_top, encoded, base))
				out[n - 1].font_id = 4; /* tiny64 */
		}
	}

	/* Time-warp "T:<N>x" — engine paints 3 separate runs at the same
	 * cursor; HD packs them in one record with 0xFE+color escapes
	 * between segments (label 0x46, digits 0x4E, 'x' 0x43). */
	{
		char tbuf[24];
		const uint8_t c_label = TieCockpitText_RemapColor(0x46);
		const uint8_t c_num = TieCockpitText_RemapColor(0x4E);
		const uint8_t c_x = TieCockpitText_RemapColor(0x43);
		int k = 0;
		tbuf[k++] = 'T';
		tbuf[k++] = ':';
		tbuf[k++] = (char)0xFE;
		tbuf[k++] = (char)c_num;
		char nstr[8];
		snprintf(nstr, sizeof nstr, "%2u", (unsigned)snap->hud.msg_bar.accelerated_time);
		for (int i = 0; nstr[i]; ++i)
			tbuf[k++] = nstr[i];
		tbuf[k++] = (char)0xFE;
		tbuf[k++] = (char)c_x;
		tbuf[k++] = 'x';
		tbuf[k] = '\0';

		if (TieCockpitText_Push(out, &n, cap, (int16_t)line_right, (int16_t)line_top, tbuf, c_label))
			out[n - 1].font_id = 4;
	}
	return n;
}

/* panelrts_outnum digit field. `digits` = total cell count, `minpad` =
 * left-pad-with-space threshold (forces digits printed even when zero).
 * Positions > minpad print as space when the leading-nonzero latch
 * hasn't fired. Engine truncates `value` to low 16 bits (panelrts.c:61).
 * Out buffer must hold ndigits+1 bytes. */
static void TieCockpitText_FormatDigitField(char* out, size_t out_cap, int value, int digits, int minpad) {
	if (digits < 1)
		digits = 1;
	if ((size_t)digits + 1 > out_cap)
		digits = (int)out_cap - 1;
	if (minpad < 1)
		minpad = 1;
	if (minpad > digits)
		minpad = digits;

	unsigned rolling = (unsigned)(uint16_t)value;
	int leading_nonzero = 0;
	static const unsigned placevalue[6] = { 1, 1, 10, 100, 1000, 10000 };

	int oi = 0;
	for (int pos = digits; pos >= 1; --pos) {
		unsigned divisor = (pos <= 5) ? placevalue[pos] : 10000u;
		unsigned digit = rolling / divisor;
		rolling = rolling - digit * divisor;
		char ch;
		if (leading_nonzero || pos <= minpad || digit != 0) {
			leading_nonzero = 1;
			if (digit > 9)
				digit = 9;
			ch = (char)('0' + digit);
		} else {
			ch = ' ';
		}
		out[oi++] = ch;
	}
	out[oi] = '\0';
}

/* Emit one cockpit-font record with the engine's opaque glyph-cell
 * backcolor fill. Colors are pre-remap; TieCockpitText_RemapColor is applied
 * here. `bg_logical == 0xFF` skips the fill (title strip paints over
 * the cockpit bitmap). Returns false on NULL/empty/full-buffer. */
static bool TieCockpitText_EmitPanelTextFont(TieUIText* out, int* n, int cap, int16_t x, int16_t y,
											 const char* s, uint8_t color_logical, uint8_t bg_logical,
											 uint8_t font_id) {
	if (!s || !s[0])
		return false;
	if (!TieCockpitText_Push(out, n, cap, x, y, s, TieCockpitText_RemapColor(color_logical)))
		return false;
	TieUIText* t = &out[*n - 1];
	t->font_id = font_id;
	if (bg_logical != 0xFFu) {
		t->background = 1;
		t->background_color_index = TieCockpitText_RemapColor(bg_logical);
	}
	return true;
}

/* MICRO64 (engine setfontsize(2)) wrapper — most cockpit text. */
static inline bool TieCockpitText_EmitPanelText(TieUIText* out, int* n, int cap, int16_t x, int16_t y,
												const char* s, uint8_t color_logical, uint8_t bg_logical) {
	return TieCockpitText_EmitPanelTextFont(out, n, cap, x, y, s, color_logical, bg_logical, FONT_MICRO64);
}

/* MICRO64 width in classic-coord px; 0 on null inputs so right-align/
 * center math falls through to ins->x. */
static inline float TieCockpitText_MeasureClassic(struct TieScene2dTextRenderer* text_renderer,
												  const TieScene2dTextSpace* space, const char* s) {
	if (!text_renderer || !space || !s || !s[0])
		return 0.0f;
	return TieScene2dTextRenderer_MeasureFestringClassic(text_renderer, TIE_FONT_DOMAIN_COCKPIT, FONT_MICRO64,
														 space, s);
}

/* festring_outstringright: x = right_at - measured - 2.
 * right_at is the absolute X of the column's right edge. */
static void TieCockpitText_EmitPanelTextRightAligned(struct TieScene2dTextRenderer* text_renderer,
													 const TieScene2dTextSpace* space, TieUIText* out, int* n,
													 int cap, const TieHudInstrument* ins, int16_t right_at,
													 const char* s, uint8_t color_logical,
													 uint8_t bg_logical) {
	if (!s || !s[0] || !TieCockpitCommon_InstrumentActive(ins))
		return;
	float tw = TieCockpitText_MeasureClassic(text_renderer, space, s);
	int16_t x = (int16_t)(right_at - (int)tw - 2);
	if (x < (int16_t)ins->x)
		x = (int16_t)ins->x;
	TieCockpitText_EmitPanelText(out, n, cap, x, (int16_t)ins->y, s, color_logical, bg_logical);
}

/* festring_outstringcenter: x = center_at - measured/2, clamped to ins->x.
 * center_at is the absolute X of the column's center. */
static void TieCockpitText_EmitPanelTextCentered(struct TieScene2dTextRenderer* text_renderer,
												 const TieScene2dTextSpace* space, TieUIText* out, int* n,
												 int cap, const TieHudInstrument* ins, int16_t center_at,
												 const char* s, uint8_t color_logical, uint8_t bg_logical) {
	if (!s || !s[0] || !TieCockpitCommon_InstrumentActive(ins))
		return;
	float tw = TieCockpitText_MeasureClassic(text_renderer, space, s);
	int16_t x = (int16_t)(center_at - (int)(tw * 0.5f));
	if (x < (int16_t)ins->x)
		x = (int16_t)ins->x;
	TieCockpitText_EmitPanelText(out, n, cap, x, (int16_t)ins->y, s, color_logical, bg_logical);
}

/* panel_updatevalue / panel_updatehardpoint digit field — value,
 * color, and digit count all resolved engine-side on the instrument. */
static void TieCockpitText_EmitPanelValue(TieUIText* out, int* n, int cap, const TieHudInstrument* ins,
										  int minpad, uint8_t bg_logical) {
	if (!TieCockpitCommon_InstrumentActive(ins) || ins->value < 0 || ins->digits == 0)
		return;
	int digits = ins->digits;
	if (digits > 6)
		digits = 6;
	char buf[8];
	TieCockpitText_FormatDigitField(buf, sizeof buf, ins->value, digits, minpad);
	TieCockpitText_EmitPanelText(out, n, cap, (int16_t)ins->x, (int16_t)ins->y, buf, ins->color, bg_logical);
}

/* Retail STRINGS.DAT cell indices used by fediskio_loadstringdata. */
#define STR_CELL_UNKNOWNSTRING 598  /* unknownstring */
#define STR_CELL_MESSAGETABLE_0 270 /* + N for messagetable[N] */
#define STR_CELL_DISTSTRING 514
#define STR_CELL_SHIELDSTRING 515
#define STR_CELL_HULLSTRING 516
#define STR_CELL_SYSSTRING 517
#define STR_CELL_CURRENTORDERSTRING 521

/* NULL → caller skips the draw (tie_core not booted yet, or empty cell). */
static const char* TieCockpitText_StringCell(int cell) {
	const char* s = TieTextSnapshot_StringCell(cell);
	if (!s || !s[0])
		return NULL;
	return s;
}

int TieCockpitText_BuildclockText(const TieSnapshot* snap, const TieHudInstrument* instruments,
								  TieUIText* out, int cap) {
	if (snap->cockpit.view_idx != 0)
		return 0;
	const TieHudState* h = &snap->hud;
	const TieHudInstrument* ins = &instruments[TIE_HUDI_CLOCK_DIGITS];
	if (!TieCockpitCommon_InstrumentActive(ins) || !h->mission_clock_text[0])
		return 0;

	int n = 0;
	TieCockpitText_EmitPanelText(out, &n, cap, (int16_t)ins->x, (int16_t)ins->y, h->mission_clock_text,
								 ins->color, COCKPIT_BG_HUD);
	return n;
}

/* Cargo string painted by panel_updatecmd into the snapshot. NULL
 * when empty so the emit helper drops the record. */
static const char* TieCockpitText_ResolveCargoText(const TieHudState* h) {
	return h->target_cargo[0] ? h->target_cargo : NULL;
}

/* Alternate-camera title strip (idx 33). The engine draws this whenever the
 * requested pilot view resolves to panel source 17, including inherited
 * look-around views. Its X coordinate is the center of the active frame. */
static int TieCockpitText_BuildViewTitle(struct TieScene2dTextRenderer* text_renderer,
										 const TieSnapshot* snap, const TieHudInstrument* instruments,
										 const TieScene2dTextSpace* space, TieUIText* out, int cap) {
	if (!snap->cockpit.view_title[0])
		return 0;

	int n = 0;
	const TieHudInstrument* ins = &instruments[TIE_HUDI_VIEW17_TITLE];
	const float text_w = TieCockpitText_MeasureClassic(text_renderer, space, snap->cockpit.view_title);
	const int center_x = (space && space->classic_w > 0) ? space->classic_w / 2 : (int)ins->x;
	int x = center_x - (int)(text_w * 0.5f);

	/* Retail clamps centering to a 120-SVGA-pixel (80 VGA) strip. Scale
	 * that strip with frame height so 16:9 layouts retain the original
	 * title-area proportions. */
	if (space && space->classic_h > 0 && snap->cockpit.classic_h > 0) {
		const int half_classic = TieCockpitCommon_IsSvga(snap) ? 60 : 40;
		const int half_strip = half_classic * space->classic_h / snap->cockpit.classic_h;
		if (x < center_x - half_strip)
			x = center_x - half_strip;
	}
	if (x < 0)
		x = 0;

	TieCockpitText_EmitPanelText(out, &n, cap, (int16_t)x, (int16_t)ins->y, snap->cockpit.view_title, 0x43,
								 COCKPIT_BG_HUD);
	return n;
}

/* panel_updatespeed (idx 24, panel.c:668) + panel_updatethrottle
 * (idx 25, panel.c:681). Both setbackcolor(0x40) before painting. */
static int TieCockpitText_BuildPlayerReadouts(const TieHudInstrument* instruments, TieUIText* out, int cap) {
	int n = 0;
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_SPEED_DIGITS], 1, COCKPIT_BG_HUD);
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_THROTTLE_DIGITS], 1, COCKPIT_BG_HUD);
	return n;
}

/* panel_updatereplaystuff %-remaining digit (idx 32, panel.c:628). */
static int TieCockpitText_BuildRecordingPct(const TieHudInstrument* instruments, TieUIText* out, int cap) {
	if (!instruments[TIE_HUDI_REC_LED].value)
		return 0;
	int n = 0;
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_REC_PCT], 1, COCKPIT_BG_HUD);
	return n;
}

/* panel_updatehardpoint ammo digits (idx 15..18). */
static int TieCockpitText_BuildMissileAmmo(const TieHudInstrument* instruments, TieUIText* out, int cap) {
	int n = 0;
	for (int i = 0; i < 4; ++i)
		TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_MISSILE_AMMO_FIRST + i], 1,
									  COCKPIT_BG_AMMO);
	return n;
}

/* panel_updatecmd — lower CMD readout. Skips entirely with no target. */
static int TieCockpitText_BuildPanelCmd(struct TieScene2dTextRenderer* text_renderer, const TieSnapshot* snap,
										const TieHudInstrument* instruments, const TieCockpitLayout* layout,
										const TieScene2dTextSpace* space, TieUIText* out, int cap) {
	const TieHudState* h = &snap->hud;
	if (h->target_obj_slot == 0xFFFFu)
		return 0;
	const bool tgt_craft = (h->target_obj_slot < 0x3800u);
	const bool is_svga = TieCockpitCommon_IsSvga(snap);
	/* Engine column widths used to fall back when no layout-authored
	 * absolute position is supplied. */
	const int16_t cmd_text_w_classic = is_svga ? 70 : 40;
	const int16_t cmd_name_w_classic = is_svga ? 160 : 80;
	/* Labels share their color via instruments[86..89].color (set
	 * engine-side by the resolution-dependent paint at panel.c:1976).
	 * Separators and the target-name base color reuse the label color. */
	const uint8_t label_color = instruments[86].color;
	int n = 0;

	static const int label_cells[4] = {
		STR_CELL_SYSSTRING,
		STR_CELL_DISTSTRING,
		STR_CELL_SHIELDSTRING,
		STR_CELL_HULLSTRING,
	};
	for (int i = 0; i < 4; ++i) {
		const TieHudInstrument* ins = &instruments[86 + i];
		if (!TieCockpitCommon_InstrumentActive(ins))
			continue;
		TieCockpitText_EmitPanelText(out, &n, cap, (int16_t)ins->x, (int16_t)ins->y,
									 TieCockpitText_StringCell(label_cells[i]), ins->color, COCKPIT_BG_CMD);
	}

	/* '%' / '.' separator offsets — engine 0xC057C (3 spaces) and
	 * 0xC0580 (2 spaces). The 3-space variant puts '%' past the
	 * digit cells; 2 spaces lands it inside cell-3 (the "% clipped"
	 * bug we saw both in tie_core and in HD before this fix). */
	const float sep_pct = TieCockpitText_MeasureClassic(text_renderer, space, "   ");
	const float sep_dist = TieCockpitText_MeasureClassic(text_renderer, space, "  ");

#define EMIT_DIGIT_WITH_SEP(id_, minpad_, off_, ch_)                                                         \
	do {                                                                                                     \
		const TieHudInstrument* ins = &instruments[id_];                                                     \
		TieCockpitText_EmitPanelValue(out, &n, cap, ins, (minpad_), COCKPIT_BG_CMD);                         \
		if ((ins->x || ins->y) && ins->value >= 0)                                                           \
			TieCockpitText_EmitPanelText(out, &n, cap, (int16_t)((int)ins->x + (int)(off_)),                 \
										 (int16_t)ins->y, (ch_), label_color, COCKPIT_BG_CMD);               \
	} while (0)

	EMIT_DIGIT_WITH_SEP(TIE_HUDI_TARGET_SUBSYSTEM_PCT, 1, sep_pct, "%");
	EMIT_DIGIT_WITH_SEP(TIE_HUDI_TARGET_DIST_KM_INT, 1, sep_dist, ".");
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_TARGET_DIST_KM_FRAC], 2,
								  COCKPIT_BG_CMD);
	EMIT_DIGIT_WITH_SEP(TIE_HUDI_TARGET_SHIELD_PCT, 1, sep_pct, "%");
	EMIT_DIGIT_WITH_SEP(TIE_HUDI_TARGET_HULL_PCT, 1, sep_pct, "%");
#undef EMIT_DIGIT_WITH_SEP

	/* Cargo (idx 63) + subsystem focus (idx 65) — colors set by
	 * panel_updatecmd at the paint sites. */
	if (tgt_craft) {
		const TieHudInstrument* ins = &instruments[63];
		const int16_t right_at =
			TieCockpitText_ResolveRightAt(layout, 63, (int16_t)ins->x, cmd_text_w_classic);
		TieCockpitText_EmitPanelTextRightAligned(text_renderer, space, out, &n, cap, ins, right_at,
												 TieCockpitText_ResolveCargoText(h), ins->color,
												 COCKPIT_BG_CMD);
	}
	if (h->target_subsystem_text[0]) {
		const TieHudInstrument* ins = &instruments[65];
		const int16_t right_at =
			TieCockpitText_ResolveRightAt(layout, 65, (int16_t)ins->x, cmd_text_w_classic);
		TieCockpitText_EmitPanelTextRightAligned(text_renderer, space, out, &n, cap, ins, right_at,
												 h->target_subsystem_text, ins->color, COCKPIT_BG_CMD);
	}

	/* Target name (idx 90) — engine leaves textcolor at label_color
	 * when painting; 0xFE escapes in target_name override per-glyph. */
	{
		const TieHudInstrument* ins = &instruments[90];
		const int16_t center_at =
			TieCockpitText_ResolveCenterAt(layout, 90, (int16_t)ins->x, cmd_name_w_classic);
		TieCockpitText_EmitPanelTextCentered(text_renderer, space, out, &n, cap, ins, center_at,
											 h->target_name, ins->color, COCKPIT_BG_CMD);
	}
	return n;
}

/* Right-aligned numeric formatter mirroring engine panelrts_outnum /
 * gate_outdnum: print `value` in a field of `digits` chars, padding the
 * leading positions with spaces; `min_digits` forces at least N digits
 * (so value=0 prints "0", not all spaces). buf must hold digits+1. */
static void TieCockpitText_FormatOutnum(char* buf, int buf_cap, int32_t value, int digits, int min_digits) {
	if (digits < 1)
		digits = 1;
	if (digits > buf_cap - 1)
		digits = buf_cap - 1;
	if (min_digits > digits)
		min_digits = digits;
	int32_t abs_v = (value < 0) ? -value : value;
	int started = 0;
	for (int pos = 0; pos < digits; ++pos) {
		int rank = digits - 1 - pos;
		int32_t div = 1;
		for (int k = 0; k < rank; ++k)
			div *= 10;
		int d = (int)(abs_v / div);
		abs_v -= (int32_t)d * div;
		if (d > 9)
			d = 9;
		if (d == 0 && !started && rank + 1 > min_digits)
			buf[pos] = ' ';
		else {
			started = 1;
			buf[pos] = (char)('0' + d);
		}
	}
	buf[digits] = '\0';
}

/* gate_trainingupdatecrt (gate.c:858-985) — replaces panel_updatecmd
 * when mission.train_craft_type != 0. Five label+value rows; layout
 * splits on flightResolution and on a per-craft-spec left/right pick. */
static int TieCockpitText_BuildPanelTrainingCrt(const TieSnapshot* snap, const TieHudInstrument* instruments,
												TieUIText* out, int cap) {
	const TieHudState* h = &snap->hud;
	if (!h->training.active)
		return 0;
	/* Origin from instrument 2 (the CMD 3D CRT slot); engine reads
	 * it as (instruments[2].x, instruments[2].y) at panel.c:1946. */
	const TieHudInstrument* ins = &instruments[TIE_HUDI_CMD_3D_CRT];
	int16_t x_origin = (int16_t)ins->x;
	int16_t y_origin = (int16_t)ins->y;

	const bool is_svga = TieCockpitCommon_IsSvga(snap);
	int16_t y, side_offset, level_label_x, level_value_x, score_label_x;
	int16_t gates_col_x, score_col_x;
	uint8_t fh;
	if (is_svga) {
		x_origin += 10;
		y = (int16_t)(y_origin - 10);
		side_offset = 16;
		level_label_x = 52;
		level_value_x = 106;
		score_label_x = 40;
		gates_col_x = 150;
		score_col_x = 90;
		fh = 21; /* fontheight after setfontsize(1) for SVGA */
	} else {
		y = (int16_t)(y_origin - 6);
		side_offset = 8;
		level_label_x = 26;
		level_value_x = 53;
		score_label_x = 20;
		gates_col_x = 75;
		score_col_x = 45;
		fh = 9; /* fontheight after setfontsize(1) for VGA */
	}

	/* Engine panel.c:889-901 spec-based left/right pick. */
	const uint32_t spec_plus_1 = (uint32_t)h->training.player_spec_num + 1;
	int draw_right;
	if (spec_plus_1 < 12) {
		if (spec_plus_1 < 8 || spec_plus_1 > 9)
			draw_right = 0;
		else
			draw_right = 1;
	} else if (spec_plus_1 <= 12 || h->training.player_spec_num == 15) {
		draw_right = 1;
	} else {
		draw_right = 0;
	}
	int16_t crt_x;
	if (snap->legacy_render_convention != TIE_FLIGHT_LEGACY_RENDER_TIE95 && is_svga &&
		h->training.player_spec_num == 4)
		crt_x = x_origin;
	else
		crt_x = draw_right ? (int16_t)(x_origin + side_offset) : (int16_t)(x_origin - side_offset);

/* Engine festring_setfontsize(1) → TINY64 for every glyph in this
 * region. setfontsize(2) is restored at the very end of
 * gate_trainingupdatecrt; HD doesn't carry that global state, so
 * each record specifies its font_id directly. */
#define EMIT_TINY(x_, y_, s_, c_)                                                                            \
	TieCockpitText_EmitPanelTextFont(out, &n, cap, (x_), (y_), (s_), (c_), COCKPIT_BG_CMD, FONT_TINY64)

	int n = 0;
	char digits[16];

	/* LEVEL — label color 0x49, value color 0x4A. */
	const char* lvl_label = TieRecoveredData_GateLabel(0);
	if (lvl_label && lvl_label[0])
		EMIT_TINY((int16_t)(crt_x + level_label_x), y, lvl_label, 0x49);
	TieCockpitText_FormatOutnum(digits, sizeof digits, h->training.level, 2, 2);
	EMIT_TINY((int16_t)(crt_x + level_value_x), y, digits, 0x4A);

	/* REMAIN — label color 0x45, value color 0x46. */
	const int16_t y_remain = (int16_t)(y + fh + 1);
	const char* rem_label = TieRecoveredData_GateLabel(1);
	if (rem_label && rem_label[0])
		EMIT_TINY(crt_x, y_remain, rem_label, 0x45);
	TieCockpitText_FormatOutnum(digits, sizeof digits, h->training.gates_remaining, 3, 1);
	EMIT_TINY((int16_t)(crt_x + gates_col_x), y_remain, digits, 0x46);

	/* PASSED — engine uses the same color block as REMAIN, set once. */
	const int16_t y_plus_1 = (int16_t)(y + 1);
	const int16_t y_passed = (int16_t)(y_plus_1 + 2 * fh);
	const char* pas_label = TieRecoveredData_GateLabel(2);
	if (pas_label && pas_label[0])
		EMIT_TINY(crt_x, y_passed, pas_label, 0x45);
	TieCockpitText_FormatOutnum(digits, sizeof digits, h->training.gates_passed, 3, 1);
	EMIT_TINY((int16_t)(crt_x + gates_col_x), y_passed, digits, 0x46);

	/* TARGETS HIT — label color 0x4D, value color 0x4E. */
	const int16_t y_targets = (int16_t)(y_plus_1 + 3 * fh);
	const char* tgt_label = TieRecoveredData_GateLabel(3);
	if (tgt_label && tgt_label[0])
		EMIT_TINY(crt_x, y_targets, tgt_label, 0x4D);
	TieCockpitText_FormatOutnum(digits, sizeof digits, h->training.targets_hit, 3, 1);
	EMIT_TINY((int16_t)(crt_x + gates_col_x), y_targets, digits, 0x4E);

	/* SCORE — label color 0x51, value color 0x52, 6 digits. */
	const int16_t y_score = (int16_t)(y_plus_1 + 4 * fh);
	const char* sc_label = TieRecoveredData_GateLabel(4);
	if (sc_label && sc_label[0])
		EMIT_TINY((int16_t)(crt_x + score_label_x), y_score, sc_label, 0x51);
	TieCockpitText_FormatOutnum(digits, sizeof digits, h->training.score, 6, 1);
	EMIT_TINY((int16_t)(crt_x + score_col_x), y_score, digits, 0x52);

#undef EMIT_TINY
	return n;
}

/* gate_updatebonuspoints (gate.c:634-665) — timer + bonus row painted
 * only while the per-section countdown task runs. Engine: setfontsize(1)
 * → TINY64, setbackcolor(0x2C), settextcolor(0x43). */
static int TieCockpitText_BuildPanelBonusBar(const TieSnapshot* snap, TieUIText* out, int cap) {
	const TieHudState* h = &snap->hud;
	if (!h->training.active || !h->training.bonus_active)
		return 0;
	const bool is_svga = TieCockpitCommon_IsSvga(snap);
	int16_t y, bonus_x, timer_x;
	if (is_svga) {
		y = 456;
		bonus_x = 465;
		timer_x = 360;
	} else {
		y = 190;
		bonus_x = 255;
		timer_x = 200;
	}

	int n = 0;
	char mm[4], ss[4], timer[8], bonus[8];

	/* Timer "MM:SS" — engine emits panelrts_outnum(min,2,2) + ':' +
	 * panelrts_outnum(sec,2,2) at one cursor (gate.c:657-659). */
	TieCockpitText_FormatOutnum(mm, sizeof mm, h->training.timer_min, 2, 2);
	TieCockpitText_FormatOutnum(ss, sizeof ss, h->training.timer_sec, 2, 2);
	snprintf(timer, sizeof timer, "%s:%s", mm, ss);
	TieCockpitText_EmitPanelTextFont(out, &n, cap, timer_x, y, timer, 0x43, COCKPIT_BG_THREAT, FONT_TINY64);

	/* Bonus: 5 digits, force every digit (gate.c:661 min_digits=5). */
	TieCockpitText_FormatOutnum(bonus, sizeof bonus, (int32_t)(uint16_t)h->training.bonus, 5, 5);
	TieCockpitText_EmitPanelTextFont(out, &n, cap, bonus_x, y, bonus, 0x43, COCKPIT_BG_THREAT, FONT_TINY64);
	return n;
}

/* panel_updatethreatname + panel_updatethreatweapons (pilotview 20). */
static void TieCockpitText_EmitThreatLabelValueStr(TieUIText* out, int* n, int cap, int16_t left_col,
												   const TieHudInstrument* ins, const char* label,
												   const char* value) {
	if (!TieCockpitCommon_InstrumentActive(ins))
		return;
	/* Threat-view label color is a fixed engine constant (0x45) at all
	 * four label slots; the value color comes from ins->color. */
	if (label && label[0])
		TieCockpitText_EmitPanelText(out, n, cap, left_col, (int16_t)ins->y, label, 0x45, COCKPIT_BG_THREAT);
	if (value && value[0])
		TieCockpitText_EmitPanelText(out, n, cap, (int16_t)ins->x, (int16_t)ins->y, value, ins->color,
									 COCKPIT_BG_THREAT);
}

/* The classic order row changes color without resetting its cursor. Keep it
 * as one festring run so the order starts at the label's exact glyph advance. */
static void TieCockpitText_EmitThreatOrder(TieUIText* out, int* n, int cap, int16_t left_col,
										   const TieHudInstrument* ins, const char* label,
										   const char* value) {
	if (!TieCockpitCommon_InstrumentActive(ins) || !label || !label[0])
		return;

	char row[sizeof((TieUIText*)0)->text];
	size_t pos = 0;
	while (label[pos] && pos + 1 < sizeof row) {
		row[pos] = label[pos];
		++pos;
	}
	if (value && value[0] && pos + 3 < sizeof row) {
		row[pos++] = (char)0xFE;
		row[pos++] = (char)TieCockpitText_RemapColor(ins->color);
		for (size_t i = 0; value[i] && pos + 1 < sizeof row; ++i)
			row[pos++] = value[i];
	}
	row[pos] = '\0';
	TieCockpitText_EmitPanelText(out, n, cap, left_col, (int16_t)ins->y, row, 0x45, COCKPIT_BG_THREAT);
}

static int TieCockpitText_BuildPanelThreat(struct TieScene2dTextRenderer* text_renderer,
										   const TieSnapshot* snap, const TieHudInstrument* instruments,
										   const TieCockpitLayout* layout, const TieScene2dTextSpace* space,
										   TieUIText* out, int cap) {
	const TieHudState* h = &snap->hud;
	if (h->target_obj_slot == 0xFFFFu)
		return 0;
	const bool tgt_craft = (h->target_obj_slot < 0x3800u);
	const bool is_svga = TieCockpitCommon_IsSvga(snap);
	/* Engine constants for the threat label column + target-name
	 * centering — fallback when no layout-authored override. */
	const int16_t threat_label_x_classic = is_svga ? 132 : 66;
	const int16_t threat_name_w_classic = is_svga ? 114 : 76;
	int n = 0;

	/* Idx 69 — centered target name. */
	{
		const TieHudInstrument* ins = &instruments[69];
		const int16_t center_at =
			TieCockpitText_ResolveCenterAt(layout, 69, (int16_t)ins->x, threat_name_w_classic);
		TieCockpitText_EmitPanelTextCentered(text_renderer, space, out, &n, cap, ins, center_at,
											 h->target_name, ins->color, COCKPIT_BG_THREAT);
	}

	/* Idx 70 — cargo. */
	if (tgt_craft) {
		const TieHudInstrument* ins = &instruments[70];
		if (ins->x || ins->y)
			TieCockpitText_EmitPanelText(out, &n, cap, (int16_t)ins->x, (int16_t)ins->y,
										 TieCockpitText_ResolveCargoText(h), ins->color, COCKPIT_BG_THREAT);
	}

	/* Distance + shield/hull % digits. */
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_THREAT_DIST_KM_INT], 1,
								  COCKPIT_BG_THREAT);
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_THREAT_DIST_KM_FRAC], 2,
								  COCKPIT_BG_THREAT);
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_THREAT_SHIELD_PCT], 1,
								  COCKPIT_BG_THREAT);
	TieCockpitText_EmitPanelValue(out, &n, cap, &instruments[TIE_HUDI_THREAT_HULL_PCT], 1, COCKPIT_BG_THREAT);

	/* Order / link / ETA lines — labels, values, and value colors all
	 * resolved engine-side at the paint sites. Each row resolves its
	 * label-column X via the layout (or classic constant fallback). */
	TieCockpitText_EmitThreatOrder(
		out, &n, cap, TieCockpitText_ResolveLabelAt(layout, 79, threat_label_x_classic), &instruments[79],
		TieCockpitText_StringCell(STR_CELL_CURRENTORDERSTRING), h->target_order_text);
	TieCockpitText_EmitThreatLabelValueStr(
		out, &n, cap, TieCockpitText_ResolveLabelAt(layout, 80, threat_label_x_classic), &instruments[80],
		h->target_link_target_label, h->target_link_name);
	TieCockpitText_EmitThreatLabelValueStr(
		out, &n, cap, TieCockpitText_ResolveLabelAt(layout, 81, threat_label_x_classic), &instruments[81],
		h->target_link_dist_label, h->target_link_dist_text);
	TieCockpitText_EmitThreatLabelValueStr(out, &n, cap,
										   TieCockpitText_ResolveLabelAt(layout, 82, threat_label_x_classic),
										   &instruments[82], h->target_eta_label, h->target_eta_text);
	return n;
}

int TieCockpitText_BuildHudText(struct TieScene2dTextRenderer* text_renderer, const TieSnapshot* snap,
								const TieHudInstrument* instruments, const TieCockpitLayout* layout,
								const TieScene2dTextSpace* space, TieUIText* out, int cap) {
	const uint8_t v = snap->cockpit.view_idx;
	if (snap->cockpit.view_title_visible)
		return TieCockpitText_BuildViewTitle(text_renderer, snap, instruments, space, out, cap);
	if (v == 18)
		return 0;
	int n = 0;
	if (v == 0) {
		n += TieCockpitText_BuildPlayerReadouts(instruments, out + n, cap - n);
		n += TieCockpitText_BuildRecordingPct(instruments, out + n, cap - n);
		n += TieCockpitText_BuildMissileAmmo(instruments, out + n, cap - n);
		/* Training missions replace the CMD readout with the gate CRT
		 * stats (mission.train_craft_type != 0 → gate_trainingupdatecrt
		 * in classic, panel.c:1945-1949) and add a separate bonus row. */
		if (snap->hud.training.active) {
			n += TieCockpitText_BuildPanelTrainingCrt(snap, instruments, out + n, cap - n);
			n += TieCockpitText_BuildPanelBonusBar(snap, out + n, cap - n);
		} else {
			n += TieCockpitText_BuildPanelCmd(text_renderer, snap, instruments, layout, space, out + n,
											  cap - n);
		}
	} else if (v == 20) {
		n += TieCockpitText_BuildPanelThreat(text_renderer, snap, instruments, layout, space, out + n,
											 cap - n);
	}
	return n;
}
