#ifndef LANDRU_MEMCOM_H
#define LANDRU_MEMCOM_H

#include <stdint.h>

void memcom_Add_Memory_Callback(void (*callback)(int16_t), int16_t flags);
void memcom_Free_Memory_Callback(void (*callback)(int16_t));

#endif
