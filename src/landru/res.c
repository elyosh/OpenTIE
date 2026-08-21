#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <landru/file.h>
#include <landru/fourcc.h>
#include <landru/memptr.h>
#include <landru/res.h>

static ResFile* s_first_res_file;
static bool s_resource_module_gbl;

void lres_Create_Resource_Module(void) {
	s_first_res_file = NULL;
	s_resource_module_gbl = true;
}

void lres_Destroy_Resource_Module(void) { s_resource_module_gbl = false; }

ResFile* lres_Alloc_Res_File(void) {
	ResFile* rf = lmemptr_Alloc_System_Pointer(sizeof(ResFile));
	if (rf)
		memset(rf, 0, sizeof(ResFile));
	return rf;
}

void lres_Free_Res_File(ResFile* rf) {
	assert(rf != NULL);
	if (rf->res_map) {
		free(rf->res_map);
		rf->res_map = NULL;
	}
	free(rf);
}

ResFile* lres_Find_File_With_Resource(uint32_t res_type, const char* res_name) {
	ResFile* rf = s_first_res_file;
	while (rf) {
		if (lres_Is_File_In_Resource(rf, res_type, res_name))
			break;
		rf = rf->next;
	}
	return rf;
}

bool lres_Is_File_In_Resource(ResFile* rf, uint32_t res_type, const char* res_name) {
	ResEntry* entry = rf->res_map;
	for (uint32_t i = 0; i < rf->res_count; i++) {
		if (lres_Resource_Compare(&entry[i], res_type, res_name))
			return true;
	}
	return false;
}

ResFile* lres_Open_Resource(const char* fileName) {
	ResFile* rf = lres_Alloc_Res_File();
	if (!rf)
		return NULL;

	strcpy(rf->filename, fileName);
	if (!lres_Fetch_Resource_Files(rf)) {
		lres_Free_Res_File(rf);
		return NULL;
	}

	rf->next = s_first_res_file;
	s_first_res_file = rf;
	return rf;
}

void lres_Close_Resource(ResFile* rf) {
	assert(rf != NULL);
	ResFile* prev = NULL;
	ResFile* cur = s_first_res_file;

	while (cur && cur != rf) {
		prev = cur;
		cur = cur->next;
	}

	if (cur) {
		if (prev)
			prev->next = rf->next;
		else
			s_first_res_file = rf->next;
		lres_Free_Res_File(rf);
	}
}

void* lres_Load_Resource_Data(uint32_t res_type, const char* res_name) {
	assert(res_name != NULL && res_name[0] != '\0');
	ResFile* rf = lres_Find_File_With_Resource(res_type, res_name);
	void* buf = NULL;
	if (rf) {
		LandruFile* fp = lfile_Open_File(LANDRU_FILE_ROOT_ASSET, rf->filename, "rb");
		if (fp) {
			int res_offset;
			uint32_t res_size;
			lres_Get_Resource_Offset(rf, res_type, res_name, &res_offset, &res_size);
			lfile_Seek_File(fp, res_offset, LANDRU_SEEK_SET);
			buf = lres_Resource_Data_To_Handle(fp, res_size);
			lfile_Close_File(fp);
		}
	}
	return buf;
}

ResFile* lres_Open_Resource_Data(uint32_t res_type, const char* res_name) {
	assert(res_name != NULL && res_name[0] != '\0');
	ResFile* rf = lres_Find_File_With_Resource(res_type, res_name);
	if (!rf)
		return NULL;

	LandruFile* fp = lfile_Open_File(LANDRU_FILE_ROOT_ASSET, rf->filename, "rb");
	if (!fp)
		return NULL;

	int res_offset;
	uint32_t res_size;
	lres_Get_Resource_Offset(rf, res_type, res_name, &res_offset, &res_size);
	lfile_Seek_File(fp, res_offset, LANDRU_SEEK_SET);
	rf->file = fp;
	return rf;
}

void lres_Close_Resource_Data(ResFile* rf) {
	assert(rf != NULL);
	if (rf->file) {
		lfile_Close_File(rf->file);
	}
	rf->file = NULL;
}

uint8_t lres_Read_Resource_Byte(ResFile* rf) {
	assert(rf != NULL);
	if (!rf->file)
		return 0;
	uint8_t data;
	lfile_Read_Byte_From_File(rf->file, &data);
	return data;
}

uint16_t lres_Read_Resource_Word(ResFile* rf) {
	assert(rf != NULL);
	if (!rf->file)
		return 0;
	int16_t data;
	lfile_Read_Word_From_File(rf->file, &data);
	return data;
}

uint32_t lres_Read_Resource_Long(ResFile* rf) {
	assert(rf != NULL);
	if (!rf->file)
		return 0;
	int32_t data;
	lfile_Read_Long_From_File(rf->file, &data);
	return data;
}

void lres_Read_Resource_Buffer_Data(ResFile* rf, void* dest, uint32_t len) {
	assert(rf != NULL);
	assert(dest != NULL);
	assert(len != 0);
	if (rf->file) {
		lres_Resource_Data_To_Buffer(rf->file, dest, len);
	}
}

void* lres_Read_Resource_Data(ResFile* rf, int len) {
	assert(rf != NULL);
	if (rf->file) {
		return lres_Resource_Data_To_Handle(rf->file, len);
	}
	return NULL;
}

bool lres_Resource_Data_To_Buffer(LandruFile* fileHandle, void* buf, uint32_t len) {
	assert(fileHandle != NULL);
	assert(buf != NULL);
	assert(len != 0);
	return lfile_Read_Data_From_File(fileHandle, buf, len);
}

void* lres_Resource_Data_To_Handle(LandruFile* fileHandle, int len) {
	assert(fileHandle != NULL);
	assert(len != 0);
	void* buf = malloc(len);
	if (!buf)
		return NULL;
	if (!lfile_Read_Data_From_File(fileHandle, buf, len)) {
		free(buf);
		return NULL;
	}
	return buf;
}

bool lres_Get_Resource_Offset(ResFile* rf, uint32_t res_type, const char* res_name, int* out_offset,
							  uint32_t* out_size) {
	int cur_offset = (rf->res_count + 2) * 16;
	ResEntry* entry = rf->res_map;

	for (uint32_t i = 0; i < rf->res_count; i++) {
		if (lres_Resource_Compare(&entry[i], res_type, res_name)) {
			*out_size = entry[i].size;
			*out_offset = cur_offset;
			return true;
		}
		cur_offset += entry[i].size + 16;
	}

	*out_size = 0;
	*out_offset = 0;
	return false;
}

bool lres_Fetch_Resource_Files(ResFile* rf) {
	assert(rf != NULL);

	LandruFile* fp = lfile_Open_File(LANDRU_FILE_ROOT_ASSET, rf->filename, "rb");
	if (!fp)
		return false;

	int32_t res_type;
	char hdr_name[8];
	int32_t rmap_len;

	lfile_Read_Long_From_File(fp, &res_type);
	res_type = lfile_Swap_DWord(res_type);
	lfile_Read_Data_From_File(fp, hdr_name, 8);
	lfile_Read_Long_From_File(fp, &rmap_len);

	bool ok = true;
	if (res_type != FOURCC_RMAP) {
		ok = false;
	} else {
		rf->res_map = lres_Resource_Data_To_Handle(fp, rmap_len);
		if (!rf->res_map) {
			ok = false;
		} else {
			rf->res_count = rmap_len / 16;
			ResEntry* entry = rf->res_map;
			for (uint32_t i = 0; i < rf->res_count; i++) {
				entry[i].type = lfile_Swap_DWord(entry[i].type);
			}
		}
	}

	lfile_Close_File(fp);
	return ok;
}

bool lres_Resource_Compare(ResEntry* entry, uint32_t res_type, const char* res_name) {
	assert(entry != NULL);
	assert(res_name != NULL && res_name[0] != '\0');

	if (res_type != entry->type)
		return false;

	int i;
	for (i = 0; i < 8 && res_name[i] != '\0'; i++) {
		if (entry->name[i] != res_name[i])
			return false;
	}

	if (i < 8 && entry->name[i] != '\0')
		return false;

	return true;
}

void lres_Res_Type_To_String(uint32_t type, char* out) {
	out[0] = (type >> 24) & 0xFF;
	out[1] = (type >> 16) & 0xFF;
	out[2] = (type >> 8) & 0xFF;
	out[3] = type & 0xFF;
	out[4] = '\0';
}

void lres_Res_String_To_Type(uint32_t* out_type, const char* name) {
	assert(name != NULL && name[0] != '\0');
	*out_type = ((uint32_t)(uint8_t)name[0] << 24) | ((uint32_t)(uint8_t)name[1] << 16) |
				((uint32_t)(uint8_t)name[2] << 8) | (uint8_t)name[3];
}
