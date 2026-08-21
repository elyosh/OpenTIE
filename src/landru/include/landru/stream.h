#ifndef LANDRU_STREAM_H
#define LANDRU_STREAM_H

#include <stdint.h>

int16_t lstream_Init_Stream_Engine(int32_t buff_size, int32_t prefetch_amount);
int16_t lstream_Exit_Stream_Engine(void);
int16_t lstream_Chain_Stream_File(const char* filename);
int16_t lstream_Unchain_Current_Stream_File(void);
int16_t lstream_Use_Stream_File(const char* filename);
uint32_t lstream_Read_From_Stream_Buffer(void* dest, uint32_t size, int16_t blocking);
int16_t lstream_Get_Stream_Flag(void);
void lstream_Adjust_For_Penalty(uint32_t size);
void lstream_Set_Stream_Tick_Counts(void);

#endif
