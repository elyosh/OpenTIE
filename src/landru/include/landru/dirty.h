#ifndef LANDRU_DIRTY_H
#define LANDRU_DIRTY_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>

void ldirty_Create_Dirty_List_Module(int16_t max_rects);
void ldirty_Destroy_Dirty_List_Module(void);
void ldirty_Set_Dirty_Disable(void);
void ldirty_Clear_Dirty_Disable(void);
int16_t ldirty_Is_Dirty_Disable(void);
void ldirty_Set_Dirty_Merge(void);
void ldirty_Clear_Dirty_Merge(void);
int16_t ldirty_Is_Dirty_Merge(void);
void ldirty_Clear_Master_Dirty_List(void);
void ldirty_Dirty_Master_Rect(Rect* r);
void ldirty_Max_Dirty_List(void);
int ldirty_Dirty_Rect(Rect* r);
void ldirty_Swap_Dirty_List(int16_t save_working);
void ldirty_Prepare_Dirty_List(void);
int ldirty_Next_Dirty_Rect(Rect* dst);
int ldirty_Next_Dirty_Rect_Or_Restore(Rect* dst);
void ldirty_Prepare_Old_Dirty_List(void);
int ldirty_Next_Old_Dirty_Rect(Rect* dst);

#endif
