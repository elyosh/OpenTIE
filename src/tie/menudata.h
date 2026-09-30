#ifndef TIE_MENUDATA_H
#define TIE_MENUDATA_H

#include "tie/textext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MainMenuLayout {
	const char* archive;
	const char* film;
	const char* background;
	const char* door_names[8];
	int16_t width, height;
	int16_t mouse_x, mouse_y;
	int16_t button_bounds[8][4];
	int16_t exit_scene[8];
	TIEText title_text[8];
	int16_t title_font;
	int16_t outcome_input_id;
} MainMenuLayout;

extern const MainMenuLayout mainmenu_layout_tie95;
extern const MainMenuLayout mainmenu_layout_tie98;

#ifdef __cplusplus
}
#endif

#endif
