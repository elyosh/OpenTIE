#ifndef TIE_STATIC_H
#define TIE_STATIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void static_drawstaticobject(uint16_t slot_idx);
void static_drawstaticobject_tie98(uint16_t slot_idx);

int16_t static_laserstaticcollide(uint16_t shooter_obj_idx, uint16_t target_slot);
void static_laserhitstatic(uint16_t proj_idx, uint16_t target_slot);
void static_updatemineguns(uint16_t slot_idx);

#ifdef __cplusplus
}
#endif

#endif
