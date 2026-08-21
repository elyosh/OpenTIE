#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <landru/bitmap.h>
#include <landru/canvas.h>
#include <landru/font.h>
#include <landru/memptr.h>
#include <landru/rect.h>
#include <landru/render.h>
#include <landru/res.h>

#include "render_internal.h"
#include <landru/fourcc.h>

static FontStruct* s_cur_font = NULL;
static FontStruct* s_fonts[6];
static bool s_font_module = false;
static uint16_t s_cur_font_id = 0;
static uint16_t s_font_clr = 10;
static int16_t s_font_x = 0;
static int16_t s_font_y = 0;
static int16_t s_font_left = 0;
static int16_t s_font_top = 0;

void lfont_Create_Font_Module(void) {
	for (int i = 0; i < 6; i++) {
		s_fonts[i] = NULL;
	}
	s_cur_font = NULL;
	s_cur_font_id = 0xffff;
	s_font_clr = 0xf;
	s_font_x = 0;
	s_font_y = 0;
	s_font_module = true;
}

void lfont_Destroy_Font_Module(void) {
	if (!s_font_module)
		return;

	for (int i = 0; i < 6; i++) {
		if (s_fonts[i]) {
			lfont_Free_Font_From_System(i);
		}
	}
}

FontStruct* lfont_Alloc_Font(uint16_t fontId) {
	FontStruct* font = lmemptr_Alloc_System_Pointer(sizeof(FontStruct));
	if (font) {
		lfont_Init_Font(font);
		lfont_Add_Font_To_System(fontId, font);
	}
	return font;
}

void lfont_Free_Font(FontStruct* font) {
	if (!font)
		return;

	if (font->widthArray)
		free(font->widthArray);

	if (font->data)
		free(font->data);

	free(font);
}

void lfont_Init_Font(FontStruct* font) {
	assert(font != NULL);

	font->firstChar = 0x20;
	font->numChars = 0;
	font->spaceBetween = 1;
	font->linesBetween = 1;
	font->width = 0;
	font->height = 0;
	font->baseline = 0;
	font->charSize = 0;
	font->boldColor = 2;
	font->shadowColor = 16;
	font->shadow = 0;
	font->widthArray = NULL;
	font->data = NULL;
	font->res_type = 0;
}

bool lfont_Add_Font_To_System(uint16_t fontId, FontStruct* font) {
	assert(fontId >= 0 && fontId < 6);

	if (s_fonts[fontId]) {
		lfont_Free_Font_From_System(fontId);
	}

	s_fonts[fontId] = font;
	return true;
}

void lfont_Free_Font_From_System(uint16_t fontId) {
	if (s_fonts[fontId]) {
		lfont_Free_Font(s_fonts[fontId]);
		s_fonts[fontId] = NULL;
	}
}

bool lfont_Res_Font(const char* fontName, uint16_t fontId) {
	ResFile* resFile = lres_Open_Resource_Data(FOURCC_FONT, fontName);
	if (!resFile) {
		return false;
	}

	FontStruct* font = lfont_Alloc_Font(fontId);
	if (!font) {
		lres_Close_Resource_Data(resFile);
		return false;
	}

	font->firstChar = lres_Read_Resource_Word(resFile);
	font->numChars = lres_Read_Resource_Word(resFile);
	font->width = lres_Read_Resource_Word(resFile);
	font->height = lres_Read_Resource_Word(resFile);
	font->charSize = (font->width >> 3) * font->height;
	assert(font->height < 23);
	font->baseline = lres_Read_Resource_Word(resFile);
	font->isColor = lres_Read_Resource_Word(resFile);
	font->widthArray = lres_Read_Resource_Data(resFile, font->numChars);
	assert(font->widthArray != NULL);

	font->data = lres_Read_Resource_Data(resFile, font->numChars * font->charSize);
	assert(font->data != NULL);

	lres_Close_Resource_Data(resFile);
	return true;
}

uint16_t lfont_Get_FontID_Height(uint16_t fontId) {
	if (s_fonts[fontId]) {
		return s_fonts[fontId]->height;
	}
	return 0;
}

void lfont_Get_FontID_Spacing(uint16_t fontId, int16_t* spacingX, int16_t* spacingY) {
	if (!s_fonts[fontId]) {
		*spacingX = 0;
		*spacingY = 0;
	} else {
		*spacingX = s_fonts[fontId]->spaceBetween;
		*spacingY = s_fonts[fontId]->linesBetween;
	}
}

void lfont_Enable_FontID_Shadow(uint16_t fontId) {
	if (s_fonts[fontId]) {
		s_fonts[fontId]->shadow = 1;
	}
}

void lfont_Disable_FontID_Shadow(uint16_t fontId) {
	if (s_fonts[fontId]) {
		s_fonts[fontId]->shadow = 0;
	}
}

uint16_t lfont_Set_FontID_Bold_Color(uint16_t fontId, uint16_t color) {
	uint16_t prevColor = 0;
	if (s_fonts[fontId]) {
		prevColor = s_fonts[fontId]->boldColor;
		s_fonts[fontId]->boldColor = color;
	}
	return prevColor;
}

uint16_t lfont_Get_FontID_Bold_Color(uint16_t fontId) {
	if (s_fonts[fontId]) {
		return s_fonts[fontId]->boldColor;
	} else {
		return 2;
	}
}

uint16_t lfont_Set_Font(uint16_t fontId) {
	uint16_t curFont = s_cur_font_id;
	if (fontId >= 0 && fontId < 6) {
		s_cur_font_id = fontId;
		s_cur_font = s_fonts[fontId];
	} else {
		s_cur_font_id = 0xffff;
		s_cur_font = NULL;
	}

	return curFont;
}

uint16_t lfont_Get_Font(void) { return s_cur_font_id; }

void lfont_Set_Font_Color(uint16_t color) { s_font_clr = color; }

uint16_t lfont_Get_Font_Color(void) { return s_font_clr; }

int16_t lfont_Get_String_Width(const char* text) {
	if (!s_cur_font)
		return 0;

	const uint8_t* src = (const uint8_t*)text;
	uint8_t firstChar = s_cur_font->firstChar;
	uint8_t lastChar = firstChar + s_cur_font->numChars - 1;
	int16_t width = 0;
	uint8_t* widthArray = s_cur_font->widthArray;

	while (*src) {
		uint8_t ch = *src;
		if (ch >= firstChar && ch <= lastChar) {
			width += widthArray[ch - firstChar];
			if (src[1] != 0) {
				width += s_cur_font->spaceBetween;
			}
		}
		src++;
	}
	if (s_cur_font->shadow) {
		width++;
	}

	return width;
}

void lfont_Get_String_Rect(const char* text, Rect* outRect) {
	if (!s_cur_font) {
		lrect_Clear_Rect(outRect);
		return;
	}

	int16_t width = lfont_Get_String_Width(text);
	if (s_cur_font->shadow) {
		lrect_Set_Rect(outRect, 0, 0, width, s_cur_font->height + 1);
	} else {
		lrect_Set_Rect(outRect, 0, 0, width, s_cur_font->height);
	}
}

void lfont_Allign_String_In_Rect(const char* text, Rect* parentRect, uint8_t xAlign, uint8_t yAlign) {
	if (!s_cur_font)
		return;

	Rect stringRect;
	lfont_Get_String_Rect(text, &stringRect);
	lrect_Allign_Rect(&stringRect, parentRect, xAlign, yAlign);
	s_font_x = stringRect.left;
	s_font_y = stringRect.top;
}

void lfont_Draw_Text(const char* text) {
	if (!s_cur_font)
		return;

	lfont_Draw_Font_Text(text, s_cur_font->widthArray, s_cur_font->data);
}

/* Font glyph draw callbacks — all share the same 4-param signature to match
   the function pointer table. Most don't use lineStride/shadowColor. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void lfont_Draw_Font_00(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {}

void lfont_Draw_Font_01(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[3] = color;
}

void lfont_Draw_Font_02(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[2] = color;
}

void lfont_Draw_Font_03(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[2] = color;
	dst[3] = color;
}

void lfont_Draw_Font_04(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
}

void lfont_Draw_Font_05(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
	dst[3] = color;
}

void lfont_Draw_Font_06(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
	dst[2] = color;
}

void lfont_Draw_Font_07(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
	dst[2] = color;
	dst[3] = color;
}

void lfont_Draw_Font_08(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
}

void lfont_Draw_Font_09(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[3] = color;
}

void lfont_Draw_Font_10(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[2] = color;
}

void lfont_Draw_Font_11(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[2] = color;
	dst[3] = color;
}

void lfont_Draw_Font_12(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
}

void lfont_Draw_Font_13(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
	dst[3] = color;
}

void lfont_Draw_Font_14(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
	dst[2] = color;
}

void lfont_Draw_Font_15(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
	dst[2] = color;
	dst[3] = color;
}

void lfont_Draw_Font_Shadow_00(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	(void)dst;
	(void)lineStride;
	(void)color;
	(void)shadowColor;
}

void lfont_Draw_Font_Shadow_01(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[3] = color;
	dst[4] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

void lfont_Draw_Font_Shadow_02(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[2] = color;
	dst[3] = shadowColor;
	dst[lineStride + 2] = shadowColor;
}

void lfont_Draw_Font_Shadow_03(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[2] = color;
	dst[3] = color;
	dst[4] = shadowColor;
	dst[lineStride + 2] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

void lfont_Draw_Font_Shadow_04(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
	dst[2] = shadowColor;
	dst[lineStride + 1] = shadowColor;
}

void lfont_Draw_Font_Shadow_05(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
	dst[3] = color;
	dst[2] = shadowColor;
	dst[4] = shadowColor;
	dst[lineStride + 1] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

void lfont_Draw_Font_Shadow_06(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
	dst[2] = color;
	dst[3] = shadowColor;
	dst[lineStride + 1] = shadowColor;
	dst[lineStride + 2] = shadowColor;
}

void lfont_Draw_Font_Shadow_07(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[1] = color;
	dst[2] = color;
	dst[3] = color;
	dst[4] = shadowColor;
	dst[lineStride + 1] = shadowColor;
	dst[lineStride + 2] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

void lfont_Draw_Font_Shadow_08(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = shadowColor;
	dst[lineStride] = shadowColor;
}

void lfont_Draw_Font_Shadow_09(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[3] = color;
	dst[1] = shadowColor;
	dst[4] = shadowColor;
	dst[lineStride] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

void lfont_Draw_Font_Shadow_10(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[2] = color;
	dst[1] = shadowColor;
	dst[3] = shadowColor;
	dst[lineStride] = shadowColor;
	dst[lineStride + 2] = shadowColor;
}

void lfont_Draw_Font_Shadow_11(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[2] = color;
	dst[3] = color;
	dst[1] = shadowColor;
	dst[4] = shadowColor;
	dst[lineStride] = shadowColor;
	dst[lineStride + 2] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

void lfont_Draw_Font_Shadow_12(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
	dst[2] = shadowColor;
	dst[lineStride] = shadowColor;
	dst[lineStride + 1] = shadowColor;
}

void lfont_Draw_Font_Shadow_13(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
	dst[3] = color;
	dst[2] = shadowColor;
	dst[4] = shadowColor;
	dst[lineStride] = shadowColor;
	dst[lineStride + 1] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

void lfont_Draw_Font_Shadow_14(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
	dst[2] = color;
	dst[3] = shadowColor;
	dst[lineStride] = shadowColor;
	dst[lineStride + 1] = shadowColor;
	dst[lineStride + 2] = shadowColor;
}

void lfont_Draw_Font_Shadow_15(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor) {
	dst[0] = color;
	dst[1] = color;
	dst[2] = color;
	dst[3] = color;
	dst[4] = shadowColor;
	dst[lineStride] = shadowColor;
	dst[lineStride + 1] = shadowColor;
	dst[lineStride + 2] = shadowColor;
	dst[lineStride + 3] = shadowColor;
}

static textDrawFunc textDrawFuncs[32] = {
	lfont_Draw_Font_00,        lfont_Draw_Font_01,        lfont_Draw_Font_02,
	lfont_Draw_Font_03,        lfont_Draw_Font_04,        lfont_Draw_Font_05,
	lfont_Draw_Font_06,        lfont_Draw_Font_07,        lfont_Draw_Font_08,
	lfont_Draw_Font_09,        lfont_Draw_Font_10,        lfont_Draw_Font_11,
	lfont_Draw_Font_12,        lfont_Draw_Font_13,        lfont_Draw_Font_14,
	lfont_Draw_Font_15,        lfont_Draw_Font_Shadow_00, lfont_Draw_Font_Shadow_01,
	lfont_Draw_Font_Shadow_02, lfont_Draw_Font_Shadow_03, lfont_Draw_Font_Shadow_04,
	lfont_Draw_Font_Shadow_05, lfont_Draw_Font_Shadow_06, lfont_Draw_Font_Shadow_07,
	lfont_Draw_Font_Shadow_08, lfont_Draw_Font_Shadow_09, lfont_Draw_Font_Shadow_10,
	lfont_Draw_Font_Shadow_11, lfont_Draw_Font_Shadow_12, lfont_Draw_Font_Shadow_13,
	lfont_Draw_Font_Shadow_14, lfont_Draw_Font_Shadow_15
};

#pragma GCC diagnostic pop

void lfont_Draw_Font_Text(const char* text, uint8_t* widthArray, uint8_t* fontData) {
	uint8_t curColor = s_font_clr;
	uint8_t boldColor = s_cur_font->boldColor;

	uint8_t* dstBuf = (uint8_t*)draw_buff_gbl + (draw_w_gbl * s_font_y) + s_font_x;
	uint16_t shadowOffset = s_cur_font->shadow ? 16 : 0;

	while (true) {
		uint8_t curChar = *text++;

		switch (curChar) {
			case 0:
				return;
				break;

			case 1:
				curColor = s_font_clr;
				break;

			case 2:
				curColor = boldColor;
				break;

			default: {
				int16_t charId = curChar - s_cur_font->firstChar;
				if (curChar < s_cur_font->firstChar || charId >= s_cur_font->numChars)
					continue;

				uint8_t charWidth = widthArray[charId];
				if ((charWidth & 7) != 0) {
					charWidth = (charWidth | 7) + 1;
				}
				charWidth = charWidth >> 3;

				s_font_x += widthArray[charId] + s_cur_font->spaceBetween;
				uint8_t* src = fontData + (charId * s_cur_font->charSize);
				uint8_t* lineDst = dstBuf;
				for (int y = 0; y < s_cur_font->height; y++) {
					uint8_t* dst = lineDst;
					for (int i = 0; i < charWidth; i++) {
						uint8_t opcode = *src++;
						textDrawFuncs[(opcode >> 4) + shadowOffset](dst, draw_w_gbl, curColor,
																	s_cur_font->shadowColor);
						dst += 4;
						textDrawFuncs[(opcode & 0xf) + shadowOffset](dst, draw_w_gbl, curColor,
																	 s_cur_font->shadowColor);
						dst += 4;
					}
					lineDst += draw_w_gbl;
					src += (s_cur_font->width >> 3) - charWidth;
				}
				dstBuf += widthArray[charId] + s_cur_font->spaceBetween;
			} break;
		}
	}
}

void lfont_Set_FontID_Spacing(uint16_t fontId, int16_t hspc, int16_t vspc) {
	if (s_fonts[fontId]) {
		s_fonts[fontId]->spaceBetween = hspc;
		s_fonts[fontId]->linesBetween = vspc;
	}
}

uint16_t lfont_Get_FontID_Shadow_Color(uint16_t fontId) {
	if (s_fonts[fontId])
		return s_fonts[fontId]->shadowColor;
	return 16;
}

uint16_t lfont_Set_FontID_Shadow_Color(uint16_t fontId, uint16_t color) {
	uint16_t prev = 16;
	if (s_fonts[fontId]) {
		prev = s_fonts[fontId]->shadowColor;
		s_fonts[fontId]->shadowColor = color;
	}
	return prev;
}

int16_t lfont_Is_FontID_Shadow(uint16_t fontId) {
	if (s_fonts[fontId])
		return s_fonts[fontId]->shadow;
	return 0;
}

void lfont_Set_Font_Pos(int16_t x, int16_t y) {
	s_font_x = x;
	s_font_y = y;
	s_font_left = x;
	s_font_top = y;
}

void lfont_Get_Font_Pos(int16_t* out_x, int16_t* out_y) {
	*out_x = s_font_x;
	*out_y = s_font_y;
}

void lfont_Move_Font_Pos(int16_t dx, int16_t dy) {
	s_font_x += dx;
	s_font_y += dy;
}

void lfont_Return_Font_Pos(int16_t line_height) {
	s_font_x = s_font_left;
	s_font_y += line_height;
}

void lfont_Home_Font_Pos(void) {
	s_font_x = s_font_left;
	s_font_y = s_font_top;
}

void lfont_Print_Centered_Text(const char* str, Rect* r, int16_t color, int16_t font_id) {
	lfont_Set_Font(font_id);
	lfont_Set_Font_Color(color);
	lfont_Allign_String_In_Rect(str, r, 1, 1);
	lfont_Draw_Clipped_Text(str);
}

void lfont_Print_Clipped_Text(const char* str, int16_t x, int16_t y, int16_t font_id, int16_t color) {
	lfont_Set_Font(font_id);
	lfont_Set_Font_Color(color);
	s_font_x = x;
	s_font_y = y;
	lfont_Draw_Clipped_Text(str);
}

int16_t lfont_Draw_Clipped_Text(const char* str) {
	/* All public text helpers converge here. Preserve the draw-time color,
	 * toggle, shadow, target and clipping state in one publication path. */
	/* Suppress emits when the active drawing target is an off-screen
	 * scratch. The text-specific gate also permits callers that publish
	 * persistent text separately to avoid duplicate records. */
	if (str && s_cur_font && lcanvas_Render_Text_Emit_Allowed()) {
		LandruTextCommand command = { 0 };
		size_t len = strlen(str);
		if (len >= sizeof(command.text))
			len = sizeof(command.text) - 1;
		memcpy(command.text, str, len);
		command.text[len] = '\0';
		command.x = s_font_x;
		command.y = s_font_y;
		command.color_index = (uint8_t)s_font_clr;
		command.bold_color_index = (uint8_t)s_cur_font->boldColor;
		command.shadow_color_index = (uint8_t)s_cur_font->shadowColor;
		command.shadow = s_cur_font->shadow ? 1 : 0;
		command.font_id = (uint8_t)lfont_Get_Font();
		command.target = lcanvas_Render_Emit_Target();
		Rect clip;
		lcanvas_Get_Drawing_Canvas_Clip(&clip);
		command.clip_left = clip.left;
		command.clip_top = clip.top;
		command.clip_right = clip.right;
		command.clip_bottom = clip.bottom;
		landru_render_text(&command);
	}

	Rect a, b;
	lfont_Get_String_Rect(str, &a);
	lrect_Offset_Rect(&a, s_font_x, s_font_y);
	b = a;

	if (!lcanvas_Clip_Rect_To_Canvas(&b)) {
		s_font_x += a.right - a.left;
		return 0;
	}

	if (lrect_Equal_Rect(&a, &b)) {
		lfont_Draw_Text(str);
		return 1;
	}

	/* Partially clipped — render to temp bitmap, blit clipped result */
	int16_t save_x = s_font_x;
	int16_t save_y = s_font_y;
	s_font_x = 0;
	s_font_y = 0;

	int16_t w = a.right - a.left;
	int16_t h = a.bottom - a.top;
	if (w & 0xF)
		w = (w | 0xF) + 1;

	BitmapStruct bm;
	lbitmap_Init_Bitmap(&bm);
	bm.w = w;
	bm.h = h;
	bm.data = calloc(w * h, 1);
	bm.flags = 1;

	if (bm.data) {
		lbitmap_Max_Bitmap_Clipping(&bm);
		lcanvas_Set_Drawing_Canvas_Bitmap(&bm);
		lcanvas_Erase_Canvas();
		lfont_Draw_Text(str);
		lcanvas_Set_Drawing_Canvas();
		lcanvas_Copy_Bitmap_To_Canvas(&bm, save_x, save_y);
		free(bm.data);
	}

	s_font_x += save_x;
	s_font_y += save_y;
	return 1;
}

void lfont_Draw_Alligned_Text(const char* str, Rect* r, uint8_t halign, uint8_t valign) {
	lfont_Allign_String_In_Rect(str, r, halign, valign);
	lfont_Draw_Clipped_Text(str);
}
