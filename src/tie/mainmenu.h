#ifndef TIE_MAINMENU_H
#define TIE_MAINMENU_H

#include "tie/menudata.h"
#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the missing resource name on failure, NULL on success.
 * CloseScene also releases a partially opened scene. */
const char* mainmenu_OpenScene(SceneHeadStruct* scene_head, const MainMenuLayout* layout, ResFile** resource);
void mainmenu_CloseScene(ResFile* resource);

#ifdef __cplusplus
}
#endif

#endif
