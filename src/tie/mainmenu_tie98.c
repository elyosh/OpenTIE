#include "tie/menudata.h"
#include "tie/shellext.h"

const MainMenuLayout mainmenu_layout_tie98 = {
	"mm640.lfd",
	"main_00",
	NULL,
	{ "hr_mhngr", "hr_md3_1", "hr_md2_1", "hr_md1_2", "hr_md1_3", "hr_md1_1", "hr_md2_2", "hr_md2_3" },
	640,
	480,
	387,
	387,
	{ { 19, 66, 340, 142 },
	  { 0 },
	  { 172, 163, 248, 210 },
	  { 104, 237, 154, 299 },
	  { 266, 212, 331, 260 },
	  { 0, 268, 55, 357 },
	  { 433, 159, 512, 215 },
	  { 549, 168, 604, 226 } },
	{ SCENE_BRIEF, 0, SCENE_TOUR_DESK, SCENE_TRAIN_TRANSITION, SCENE_COMBAT_TRANSITION, SCENE_EXIT,
	  SCENE_BLUEPRINT, SCENE_FILM_VIEWER },
	{ 0, txtMainCustom, 0, txtMainTech, txtMainFilm, txtMainRegister, txtMainTrain, txtMainCombat },
	2,
	7,
};
