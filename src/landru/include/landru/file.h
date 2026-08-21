#ifndef LANDRU_FILE_H
#define LANDRU_FILE_H

#include <stdint.h>

#include <landru/host.h>

/* File I/O */
LandruFile* lfile_Open_File(LandruFileRoot root, const char* name, const char* mode);
int16_t lfile_Close_File(LandruFile* file);
int16_t lfile_Seek_File(LandruFile* file, int32_t offset, int16_t whence);
int32_t lfile_Tell_File(LandruFile* file);

/* File read (native endian, no swap) */
int16_t lfile_Read_Byte_From_File(LandruFile* file, uint8_t* data);
int16_t lfile_Read_Word_From_File(LandruFile* file, int16_t* data);
int16_t lfile_Read_Long_From_File(LandruFile* file, int32_t* data);
int16_t lfile_Read_Data_From_File(LandruFile* file, void* data, int32_t size);

/* File write (native endian, no swap) */
int16_t lfile_Write_Byte_To_File(LandruFile* file, uint8_t data);
int16_t lfile_Write_Word_To_File(LandruFile* file, int16_t data);
int16_t lfile_Write_Long_To_File(LandruFile* file, int32_t data);
int16_t lfile_Write_Data_To_File(LandruFile* file, const void* data, int32_t size);

/* Buffer read (advances *offset) */
uint8_t lfile_Read_Byte_From_Buffer(const uint8_t* buffer, int32_t* offset);
int16_t lfile_Read_Word_From_Buffer(const uint8_t* buffer, int32_t* offset);
int32_t lfile_Read_Long_From_Buffer(const uint8_t* buffer, int32_t* offset);
void lfile_Read_Data_From_Buffer(const uint8_t* buffer, void* data, int32_t* offset, int32_t size);

/* Buffer write (advances *offset) */
void lfile_Write_Byte_To_Buffer(uint8_t* buffer, int32_t* offset, uint8_t val);
void lfile_Write_Word_To_Buffer(uint8_t* buffer, int32_t* offset, int16_t val);
void lfile_Write_Long_To_Buffer(uint8_t* buffer, int32_t* offset, int32_t val);
void lfile_Write_Data_To_Buffer(uint8_t* buffer, const void* data, int32_t* offset, int32_t size);

/* Byte-order swap */
int16_t lfile_Swap_Word(int16_t value);
int32_t lfile_Swap_DWord(int32_t value);

#endif
