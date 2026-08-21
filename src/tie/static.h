#ifndef __STATIC_H__
#define __STATIC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void static_drawstaticobject(uint16_t slot_idx);
void static_drawstaticobject_tie98(uint16_t slot_idx);

int16_t static_laserstaticcollide(uint16_t shooter_obj_idx, uint16_t target_slot);
int16_t static_laserhitstatic(uint16_t proj_idx, uint16_t target_slot);
int16_t static_updatemineguns(uint16_t slot_idx);

#endif
