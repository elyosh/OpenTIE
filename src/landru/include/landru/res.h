#ifndef LANDRU_RES_H
#define LANDRU_RES_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/host.h>

typedef struct ResEntry ResEntry;
typedef struct ResFile ResFile;

struct ResEntry {
	uint32_t type;
	char name[8];
	uint32_t size;
};

struct ResFile {
	ResFile* next;
	char filename[50];
	LandruFile* file;
	ResEntry* res_map;
	uint32_t res_count;
};

void lres_Create_Resource_Module(void);
void lres_Destroy_Resource_Module(void);
ResFile* lres_Alloc_Res_File(void);
void lres_Free_Res_File(ResFile* resFile);
ResFile* lres_Find_File_With_Resource(uint32_t res_type, const char* res_name);
bool lres_Is_File_In_Resource(ResFile* resFile, uint32_t res_type, const char* res_name);
ResFile* lres_Open_Resource(const char* fileName);
void lres_Close_Resource(ResFile* resFile);
void* lres_Load_Resource_Data(uint32_t res_type, const char* res_name);
ResFile* lres_Open_Resource_Data(uint32_t res_type, const char* res_name);
void lres_Close_Resource_Data(ResFile* resFile);
uint8_t lres_Read_Resource_Byte(ResFile* resFile);
uint16_t lres_Read_Resource_Word(ResFile* resFile);
uint32_t lres_Read_Resource_Long(ResFile* resFile);
void lres_Read_Resource_Buffer_Data(ResFile* resFile, void* dest, uint32_t len);
void* lres_Read_Resource_Data(ResFile* resFile, int len);
bool lres_Resource_Data_To_Buffer(LandruFile* fileHandle, void* buf, uint32_t len);
void* lres_Resource_Data_To_Handle(LandruFile* fileHandle, int len);
bool lres_Get_Resource_Offset(ResFile* resFile, uint32_t res_type, const char* res_name, int* out_offset,
							  uint32_t* out_size);
bool lres_Fetch_Resource_Files(ResFile* resFile);
bool lres_Resource_Compare(ResEntry* entry, uint32_t res_type, const char* res_name);
void lres_Res_Type_To_String(uint32_t type, char* out);
void lres_Res_String_To_Type(uint32_t* out_type, const char* name);

#endif
