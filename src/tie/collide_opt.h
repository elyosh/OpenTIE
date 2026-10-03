#ifndef TIE_COLLIDE_OPT_H
#define TIE_COLLIDE_OPT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int collide_checksweptmodelcollision(uint16_t source_object_index, uint16_t target_object_index);

int collide_checksweptmodelmeshcollision(int model_type, int mesh_index, int32_t start_x, int32_t start_y,
										 int32_t start_z, int32_t end_x, int32_t end_y, int32_t end_z);

#ifdef __cplusplus
}
#endif

#endif
