#include <landru/memcom.h>

/*
 * XMEMCOM — memory compaction callbacks (stub).
 *
 * In the binary, the handle-based allocator calls registered callbacks
 * before/after compacting the handle table (e.g. iMUSE pauses during
 * compaction). With malloc/free there is no compaction, so these are no-ops.
 */

void memcom_Add_Memory_Callback(void (*callback)(int16_t), int16_t flags) {
	(void)callback;
	(void)flags;
}

void memcom_Free_Memory_Callback(void (*callback)(int16_t)) { (void)callback; }
