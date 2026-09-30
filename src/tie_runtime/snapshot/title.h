#ifndef TIE_RUNTIME_SNAPSHOT_TITLE_H
#define TIE_RUNTIME_SNAPSHOT_TITLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void TieTitleSnapshot_Reset(void);
void TieTitleSnapshot_SetOrigin(int index, int16_t y);
int TieTitleSnapshot_LineCount(void);
bool TieTitleSnapshot_ReadLine(int index, char* text, size_t capacity, float* initial_y);

#endif
