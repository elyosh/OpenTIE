#ifndef TIE_RENDER_LIST_TIE98_H
#define TIE_RENDER_LIST_TIE98_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TIE98_RENDER_OBJECT_LIST_CAPACITY = 184,
};

typedef struct RenderObjectListEntryTIE98 {
	int32_t sortDepth;
	int32_t objectIdx;
	struct RenderObjectListEntryTIE98* next;
} RenderObjectListEntryTIE98;

/* Handle-backed 184-entry storage locked by FEDISKIO_Init_Buffers_and_Fonts. */
extern RenderObjectListEntryTIE98* g_renderObjectListEntries;
extern RenderObjectListEntryTIE98* g_renderListHead;

void RenderList_Reset(void);
void RenderList_QueueObject(int objectIdx, int sortDepth);
void RenderList_SortDepthAscending(void);

#ifdef __cplusplus
}
#endif

#endif
