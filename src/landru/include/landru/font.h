#ifndef LANDRU_FONT_H
#define LANDRU_FONT_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>

typedef struct {
	uint32_t res_type;
	uint16_t firstChar;
	uint16_t numChars;
	int16_t spaceBetween;
	int16_t linesBetween;
	int16_t width;
	int16_t height;
	int16_t baseline;
	int16_t charSize;
	int16_t boldColor;
	int16_t shadowColor;
	int16_t shadow;
	int16_t isColor;
	uint8_t* widthArray;
	uint8_t* data;
} FontStruct;

extern uint8_t* s_draw_buff;

void lfont_Create_Font_Module(void);
void lfont_Destroy_Font_Module(void);
FontStruct* lfont_Alloc_Font(uint16_t fontId);
void lfont_Free_Font(FontStruct* font);
void lfont_Init_Font(FontStruct* font);
bool lfont_Add_Font_To_System(uint16_t fontId, FontStruct* font);
void lfont_Free_Font_From_System(uint16_t fontId);
bool lfont_Res_Font(const char* fontName, uint16_t fontId);
uint16_t lfont_Get_FontID_Height(uint16_t fontId);
void lfont_Set_FontID_Spacing(uint16_t fontId, int16_t hspc, int16_t vspc);
void lfont_Get_FontID_Spacing(uint16_t fontId, int16_t* spacingX, int16_t* spacingY);
void lfont_Enable_FontID_Shadow(uint16_t fontId);
void lfont_Disable_FontID_Shadow(uint16_t fontId);
int16_t lfont_Is_FontID_Shadow(uint16_t fontId);
uint16_t lfont_Set_FontID_Shadow_Color(uint16_t fontId, uint16_t color);
uint16_t lfont_Get_FontID_Shadow_Color(uint16_t fontId);
uint16_t lfont_Set_FontID_Bold_Color(uint16_t fontId, uint16_t color);
uint16_t lfont_Get_FontID_Bold_Color(uint16_t fontId);
uint16_t lfont_Set_Font(uint16_t fontId);
uint16_t lfont_Get_Font(void);
void lfont_Set_Font_Color(uint16_t color);
uint16_t lfont_Get_Font_Color(void);
void lfont_Set_Font_Pos(int16_t x, int16_t y);
void lfont_Get_Font_Pos(int16_t* out_x, int16_t* out_y);
void lfont_Move_Font_Pos(int16_t dx, int16_t dy);
void lfont_Return_Font_Pos(int16_t line_height);
void lfont_Home_Font_Pos(void);
int16_t lfont_Get_String_Width(const char* text);
void lfont_Get_String_Rect(const char* text, Rect* outRect);
void lfont_Allign_String_In_Rect(const char* text, Rect* parentRect, uint8_t xAlign, uint8_t yAlign);
void lfont_Draw_Text(const char* text);
void lfont_Draw_Font_Text(const char* text, uint8_t* widthArray, uint8_t* fontData);
void lfont_Print_Centered_Text(const char* str, Rect* r, int16_t color, int16_t font_id);
void lfont_Print_Clipped_Text(const char* str, int16_t x, int16_t y, int16_t font_id, int16_t color);
int16_t lfont_Draw_Clipped_Text(const char* str);
void lfont_Draw_Alligned_Text(const char* str, Rect* r, uint8_t halign, uint8_t valign);

typedef void (*textDrawFunc)(uint8_t* dst, uint16_t lineStride, uint8_t color, uint8_t shadowColor);

#endif
