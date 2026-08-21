#ifndef LANDRU_STYLE_H
#define LANDRU_STYLE_H

#include <stdint.h>

#include <landru/actor.h>
#include <landru/rect.h>

typedef enum {
	iconUpArrow = 0,
	iconLeftArrow = 1,
	iconDownArrow = 2,
	iconRightArrow = 3,
	iconBigLeftArrow = 4,
	iconBigRightArrow = 5,
	iconRewind = 6,
	iconSeek = 7,
	iconSmallUp = 8,
	iconSmallLeft = 9,
	iconSmallDown = 10,
	iconSmallRight = 11,
	iconPlus = 12,
	iconMinus = 13,
} StyleIcon;

int16_t lstyle_Get_Style_Base_Color(void);
int16_t lstyle_Get_Style_Up_Color(void);
int16_t lstyle_Get_Style_Down_Color(void);
int16_t lstyle_Style_Paint_Base(Rect* r, int16_t pressed);
int16_t lstyle_Style_Paint_Alert_Base(Rect* r, int16_t pressed);
void lstyle_Style_Paint_Button(Rect* r, int16_t pressed);
int16_t lstyle_Style_Paint_Lit_Button(Rect* r, int16_t pressed);
int16_t lstyle_Style_Paint_Border(Rect* r, int16_t pressed);
int16_t lstyle_Style_Paint_TextField(Rect* r);
void lstyle_Style_Button_Text(const char* str, Rect* r, int16_t pressed);
void lstyle_Style_Small_Button_Text(const char* str, Rect* r, int16_t pressed);
void lstyle_Style_Trim_Base(Rect* r);
void lstyle_Style_Trim_Button(Rect* r);
void lstyle_Style_Trim_Border(Rect* r);
void lstyle_Style_Trim_TextField(Rect* r);
void lstyle_Style_Set_Icon_Actor(Actor* actor);
void lstyle_Style_Clear_Icon_Actor(void);
void lstyle_Style_Draw_Icon(uint8_t icon_id, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff, int16_t lit);
void lstyle_Style_Draw_Centered_Icon(uint8_t icon_id, Rect* clip, Rect* dest, int16_t lit);
void lstyle_Style_Draw_Centered_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t state);

#endif
