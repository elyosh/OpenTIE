#ifndef TIE_COLLIDE_OPT_H
#define TIE_COLLIDE_OPT_H

#include <stdint.h>

uint16_t collide_checksweptmodelcollision(uint16_t source_object_index, uint16_t target_object_index);

uint16_t collide_checksweptmodelmeshcollision(uint8_t model_type, uint16_t mesh_index, int32_t start_x,
											  int32_t start_y, int32_t start_z, int32_t end_x, int32_t end_y,
											  int32_t end_z);

#endif
