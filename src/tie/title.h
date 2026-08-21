#ifndef __TITLE_H__
#define __TITLE_H__

#include "tie/shellext.h"
#include <stdint.h>

/* Push the title scene as a tie_core task. */
void title_Push_Title_Task(SceneHeadStruct* scene_head);

/* Emit the live perspective text-crawl state into the snapshot's
 * title_crawl_lines channel. Called from the per-tick emit pipeline
 * in the runtime tick pipeline; no-op when no crawl is active. */

#endif
