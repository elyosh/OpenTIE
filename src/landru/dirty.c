#include <stdlib.h>
#include <string.h>

#include <landru/dirty.h>
#include <landru/rect.h>

// GLOBAL: TIE 0xD2F34
static bool dirty_module_gbl;
// GLOBAL: TIE 0xFBCBC
static Rect* dirty_list_gbl[2];
// GLOBAL: TIE 0xFBCB8
static int16_t num_dirty_rects_gbl[2];
// GLOBAL: TIE 0xFBCB4
static int16_t master_rect_used_gbl[2];
// GLOBAL: TIE 0xFBCC8
static Rect* master_list_gbl;
// GLOBAL: TIE 0xFBCCA
static int16_t num_master_rects_gbl;
// GLOBAL: TIE 0xFBCD0
static Rect* working_list_gbl;
// GLOBAL: TIE 0xFBCC2
static int16_t num_working_rects_gbl;
// GLOBAL: TIE 0xFBCC0
static int16_t cur_working_rect_gbl;
// GLOBAL: TIE 0xFBCC6
static int16_t cur_dirty_rect_gbl;
static int16_t cur_dirty_list_gbl;
// GLOBAL: TIE 0xFBCD2
static int16_t max_dirty_rects_gbl;
// GLOBAL: TIE 0xFBCCE
static int16_t merge_dirty_lists_gbl;
// GLOBAL: TIE 0xFBCC4
static int16_t disable_dirty_lists_gbl;

void ldirty_Create_Dirty_List_Module(int16_t max_rects) {
	max_dirty_rects_gbl = max_rects;
	int alloc_size = sizeof(Rect) * max_rects;

	master_list_gbl = malloc(alloc_size);
	dirty_list_gbl[0] = malloc(alloc_size);
	dirty_list_gbl[1] = malloc(alloc_size);
	working_list_gbl = malloc(alloc_size);

	num_master_rects_gbl = 0;
	master_rect_used_gbl[0] = 0;
	master_rect_used_gbl[1] = 0;
	num_dirty_rects_gbl[0] = 0;
	num_dirty_rects_gbl[1] = 0;
	num_working_rects_gbl = 0;
	cur_working_rect_gbl = 0;
	cur_dirty_list_gbl = 0;
	merge_dirty_lists_gbl = 0;
	disable_dirty_lists_gbl = 0;

	if (dirty_list_gbl[0] && dirty_list_gbl[1] && master_list_gbl && working_list_gbl)
		dirty_module_gbl = true;
	else {
		free(master_list_gbl);
		master_list_gbl = NULL;
		free(dirty_list_gbl[0]);
		dirty_list_gbl[0] = NULL;
		free(dirty_list_gbl[1]);
		dirty_list_gbl[1] = NULL;
		free(working_list_gbl);
		working_list_gbl = NULL;
		dirty_module_gbl = false;
	}
}

void ldirty_Destroy_Dirty_List_Module(void) {
	if (dirty_module_gbl) {
		free(master_list_gbl);
		master_list_gbl = NULL;
		free(dirty_list_gbl[0]);
		dirty_list_gbl[0] = NULL;
		free(dirty_list_gbl[1]);
		dirty_list_gbl[1] = NULL;
		free(working_list_gbl);
		working_list_gbl = NULL;
		dirty_module_gbl = false;
	}
}

void ldirty_Set_Dirty_Disable(void) { disable_dirty_lists_gbl = 1; }
void ldirty_Clear_Dirty_Disable(void) { disable_dirty_lists_gbl = 0; }
int16_t ldirty_Is_Dirty_Disable(void) { return disable_dirty_lists_gbl; }
void ldirty_Set_Dirty_Merge(void) { merge_dirty_lists_gbl = 1; }
void ldirty_Clear_Dirty_Merge(void) { merge_dirty_lists_gbl = 0; }
int16_t ldirty_Is_Dirty_Merge(void) { return merge_dirty_lists_gbl; }

void ldirty_Clear_Master_Dirty_List(void) { num_master_rects_gbl = 0; }

void ldirty_Dirty_Master_Rect(Rect* r) {
	if (num_master_rects_gbl < max_dirty_rects_gbl) {
		lrect_Copy_Rect(&master_list_gbl[num_master_rects_gbl], r);
		num_master_rects_gbl++;
	}
}

void ldirty_Max_Dirty_List(void) {
	memcpy(working_list_gbl, master_list_gbl, sizeof(Rect) * num_master_rects_gbl);
	num_working_rects_gbl = num_master_rects_gbl;
	master_rect_used_gbl[cur_dirty_list_gbl] = 1;
}

int ldirty_Dirty_Rect(Rect* r) {
	if (master_rect_used_gbl[cur_dirty_list_gbl])
		return 0;
	if (lrect_Empty_Rect(r))
		return 1;

	Rect* rects = working_list_gbl;
	int16_t count = num_working_rects_gbl;
	Rect new_rect;
	lrect_Copy_Rect(&new_rect, r);

	bool merged;
	do {
		merged = false;
		for (int16_t j = 0; j < count; j++) {
			/* Check overlap */
			if (new_rect.right <= rects[j].left || rects[j].right <= new_rect.left)
				continue;
			if (new_rect.bottom <= rects[j].top || rects[j].bottom <= new_rect.top)
				continue;

			int16_t x_overlap_a = new_rect.right - rects[j].left;
			int16_t x_overlap_b = rects[j].right - new_rect.left;
			int16_t x_overlap = (x_overlap_a < x_overlap_b) ? x_overlap_a : x_overlap_b;

			int16_t y_overlap_a = new_rect.bottom - rects[j].top;
			int16_t y_overlap_b = rects[j].bottom - new_rect.top;
			int16_t y_overlap = (y_overlap_a < y_overlap_b) ? y_overlap_a : y_overlap_b;

			int16_t half_new_w = (new_rect.right - new_rect.left) >> 1;
			int16_t half_ext_w = (rects[j].right - rects[j].left) >> 1;
			int16_t half_new_h = (new_rect.bottom - new_rect.top) >> 1;
			int16_t half_ext_h = (rects[j].bottom - rects[j].top) >> 1;

			if ((x_overlap >= half_new_w || x_overlap >= half_ext_w) &&
				(y_overlap >= half_new_h || y_overlap >= half_ext_h)) {
				lrect_Enclose_Rect(&new_rect, &rects[j]);
				rects[j] = rects[--count];
				merged = true;
				break;
			}
		}
	} while (merged);

	if (count >= max_dirty_rects_gbl) {
		memcpy(working_list_gbl, master_list_gbl, sizeof(Rect) * num_master_rects_gbl);
		num_working_rects_gbl = num_master_rects_gbl;
		master_rect_used_gbl[cur_dirty_list_gbl] = 1;
		return 0;
	}

	rects[count] = new_rect;
	num_working_rects_gbl = count + 1;
	return 1;
}

void ldirty_Swap_Dirty_List(int16_t save_working) {
	int16_t cur = cur_dirty_list_gbl;
	if (save_working) {
		memcpy(dirty_list_gbl[cur], working_list_gbl, sizeof(Rect) * num_working_rects_gbl);
		num_dirty_rects_gbl[cur] = num_working_rects_gbl;
	}
	cur_dirty_list_gbl ^= 1;
	cur = cur_dirty_list_gbl;
	if (disable_dirty_lists_gbl) {
		memcpy(working_list_gbl, master_list_gbl, sizeof(Rect) * num_master_rects_gbl);
		num_working_rects_gbl = num_master_rects_gbl;
		master_rect_used_gbl[cur] = 1;
	} else {
		num_working_rects_gbl = 0;
		master_rect_used_gbl[cur] = 0;
	}
}

void ldirty_Prepare_Dirty_List(void) {
	int16_t cur = cur_dirty_list_gbl;
	memcpy(dirty_list_gbl[cur], working_list_gbl, sizeof(Rect) * num_working_rects_gbl);
	num_dirty_rects_gbl[cur] = num_working_rects_gbl;

	if (merge_dirty_lists_gbl) {
		int16_t prev = cur ^ 1;
		if (master_rect_used_gbl[0] || master_rect_used_gbl[1]) {
			memcpy(working_list_gbl, master_list_gbl, sizeof(Rect) * num_master_rects_gbl);
			num_working_rects_gbl = num_master_rects_gbl;
		} else if (!num_working_rects_gbl) {
			memcpy(working_list_gbl, dirty_list_gbl[prev], sizeof(Rect) * num_dirty_rects_gbl[prev]);
			num_working_rects_gbl = num_dirty_rects_gbl[prev];
		} else {
			Rect* prev_list = dirty_list_gbl[prev];
			for (int16_t i = 0; i < num_dirty_rects_gbl[prev]; i++) {
				if (!ldirty_Dirty_Rect(&prev_list[i]))
					break;
			}
		}
	}
	cur_working_rect_gbl = 0;
}

int ldirty_Next_Dirty_Rect(Rect* dst) {
	if (cur_working_rect_gbl >= num_working_rects_gbl) {
		cur_dirty_list_gbl ^= 1;
		if (disable_dirty_lists_gbl) {
			ldirty_Max_Dirty_List();
		} else {
			num_working_rects_gbl = 0;
			master_rect_used_gbl[cur_dirty_list_gbl] = 0;
		}
		return 0;
	}
	lrect_Copy_Rect(dst, &working_list_gbl[cur_working_rect_gbl]);
	cur_working_rect_gbl++;
	return 1;
}

int ldirty_Next_Dirty_Rect_Or_Restore(Rect* dst) {
	if (cur_working_rect_gbl >= num_working_rects_gbl) {
		int16_t cur = cur_dirty_list_gbl;
		memcpy(working_list_gbl, dirty_list_gbl[cur], sizeof(Rect) * num_dirty_rects_gbl[cur]);
		num_working_rects_gbl = num_dirty_rects_gbl[cur];
		return 0;
	}
	lrect_Copy_Rect(dst, &working_list_gbl[cur_working_rect_gbl]);
	cur_working_rect_gbl++;
	return 1;
}

void ldirty_Prepare_Old_Dirty_List(void) { cur_dirty_rect_gbl = 0; }

int ldirty_Next_Old_Dirty_Rect(Rect* dst) {
	int16_t cur = cur_dirty_list_gbl;
	if (cur_dirty_rect_gbl >= num_dirty_rects_gbl[cur]) {
		cur_dirty_list_gbl ^= 1;
		if (disable_dirty_lists_gbl) {
			ldirty_Max_Dirty_List();
		} else {
			num_working_rects_gbl = 0;
			master_rect_used_gbl[cur_dirty_list_gbl] = 0;
		}
		return 0;
	}
	lrect_Copy_Rect(dst, &dirty_list_gbl[cur][cur_dirty_rect_gbl]);
	cur_dirty_rect_gbl++;
	return 1;
}
