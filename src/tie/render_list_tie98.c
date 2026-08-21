#include "tie/render_list_tie98.h"

#include <stddef.h>

#define TIE98_RENDER_OBJECT_LIST_CAPACITY 184

// GLOBAL: TIE98 0x591904
static int g_renderObjectListCount;

// PORT: replaces the original handle-backed 184-entry allocation.
static RenderObjectListEntryTIE98 g_renderObjectListEntries[TIE98_RENDER_OBJECT_LIST_CAPACITY];

// GLOBAL: TIE98 0x591E38
RenderObjectListEntryTIE98* g_renderListHead;

// FUNCTION: TIE98 0x48DD50
void RenderList_Reset(void) {
	g_renderObjectListCount = 0;
	g_renderListHead = NULL;
}

// FUNCTION: TIE98 0x48DCE0
void RenderList_QueueObject(int objectIdx, int sortDepth) {
	if (g_renderObjectListCount >= TIE98_RENDER_OBJECT_LIST_CAPACITY)
		return;

	RenderObjectListEntryTIE98* entry = &g_renderObjectListEntries[g_renderObjectListCount];
	entry->sortDepth = sortDepth;
	entry->objectIdx = objectIdx;
	entry->next = g_renderListHead;
	g_renderListHead = entry;
	++g_renderObjectListCount;
}

// FUNCTION: TIE98 0x48DE50
void RenderList_SortDepthAscending(void) {
	int object_count = g_renderObjectListCount;
	int run_length = 1;
	if (object_count <= run_length)
		return;

	do {
		RenderObjectListEntryTIE98* right_run = g_renderListHead;
		RenderObjectListEntryTIE98* previous = NULL;
		RenderObjectListEntryTIE98* left_run = right_run;
		RenderObjectListEntryTIE98* left_tail;
		int processed_count = 0;

		if (object_count > processed_count) {
			for (;;) {
				int left_count = 0;
				while (run_length > 0) {
					if (right_run == NULL)
						break;
					++left_count;
					left_tail = right_run;
					right_run = right_run->next;
					if (left_count >= run_length)
						break;
				}
				if (right_run == NULL)
					break;

				int right_count = 0;
				if (run_length > 0) {
					while (right_run != NULL) {
						const int right_depth = right_run->sortDepth;
						if (left_run->sortDepth <= right_depth) {
							do {
								previous = left_run;
								left_run = left_run->next;
							} while (previous != left_tail && left_run->sortDepth <= right_depth);
						}

						if (previous == left_tail)
							break;
						left_tail->next = right_run->next;
						if (previous != NULL) {
							previous->next = right_run;
							previous = right_run;
							right_run->next = left_run;
						} else {
							g_renderListHead = right_run;
							right_run->next = left_run;
							previous = g_renderListHead;
						}

						right_run = left_tail->next;
						if (right_run == NULL)
							break;
						++right_count;
						if (right_count >= run_length)
							break;
					}
				}

				if (previous == left_tail) {
					while (right_count < run_length) {
						if (right_run == NULL)
							break;
						++right_count;
						left_tail = right_run;
						right_run = right_run->next;
					}
				}

				left_run = right_run;
				previous = left_tail;
				if (right_run == NULL)
					break;
				processed_count += run_length * 2;
				if (processed_count >= g_renderObjectListCount)
					break;
			}
		}

		object_count = g_renderObjectListCount;
		run_length += run_length;
	} while (run_length < object_count);
}
