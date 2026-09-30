#ifndef TIE_RUNTIME_MAINMENU_TASK_H
#define TIE_RUNTIME_MAINMENU_TASK_H

#include "tie/shellext.h"
#include "tie/textext.h"

#include <stdint.h>

/* PORT: per-edition main-menu layout selected at runtime by the frontend
 * profile. The matching builds use the original per-edition constants. */
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

void TieMainMenu_Begin(SceneHeadStruct* scene_head);

void TieMainMenu_RunView(ResFile* resource, const char* missing_resource);

#endif
