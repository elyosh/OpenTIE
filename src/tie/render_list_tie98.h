#ifndef TIE_RENDER_LIST_TIE98_H
#define TIE_RENDER_LIST_TIE98_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RenderObjectListEntryTIE98 {
	int32_t sortDepth;
	int32_t objectIdx;
	struct RenderObjectListEntryTIE98* next;
} RenderObjectListEntryTIE98;

extern RenderObjectListEntryTIE98* g_renderListHead;

void RenderList_Reset(void);
void RenderList_QueueObject(int objectIdx, int sortDepth);
void RenderList_SortDepthAscending(void);

#ifdef __cplusplus
}
#endif

#endif
