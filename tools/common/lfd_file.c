#include "lfd_file.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool TieLfdFile_Fail(TieLfdFile* lfd, char* error, size_t error_capacity, const char* format, ...) {
	if (error && error_capacity) {
		va_list args;
		va_start(args, format);
		vsnprintf(error, error_capacity, format, args);
		va_end(args);
	}
	TieLfdFile_Close(lfd);
	return false;
}

bool TieLfdFile_Open(TieLfdFile* lfd, const char* path, char* error, size_t error_capacity) {
	if (!lfd || !path)
		return false;
	memset(lfd, 0, sizeof *lfd);

	FILE* file = fopen(path, "rb");
	if (!file)
		return TieLfdFile_Fail(lfd, error, error_capacity, "cannot open %s: %s", path, strerror(errno));
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return TieLfdFile_Fail(lfd, error, error_capacity, "cannot seek %s", path);
	}
	const long file_size = ftell(file);
	if (file_size < 0) {
		fclose(file);
		return TieLfdFile_Fail(lfd, error, error_capacity, "cannot determine the size of %s", path);
	}
	if (file_size < 16) {
		fclose(file);
		return TieLfdFile_Fail(lfd, error, error_capacity, "%s: file too small to be an LFD", path);
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return TieLfdFile_Fail(lfd, error, error_capacity, "cannot seek %s", path);
	}

	lfd->size = (size_t)file_size;
	lfd->data = malloc(lfd->size);
	if (!lfd->data) {
		fclose(file);
		return TieLfdFile_Fail(lfd, error, error_capacity, "out of memory loading %s", path);
	}
	if (fread(lfd->data, 1, lfd->size, file) != lfd->size) {
		fclose(file);
		return TieLfdFile_Fail(lfd, error, error_capacity, "short read on %s", path);
	}
	if (fclose(file) != 0)
		return TieLfdFile_Fail(lfd, error, error_capacity, "cannot close %s", path);

	TieLfdIndex parsed = { 0 };
	TieFormatError format_error = { 0 };
	if (!TieLfdIndex_Parse(lfd->data, lfd->size, lfd->size, &parsed, &format_error))
		return TieLfdFile_Fail(lfd, error, error_capacity, "%s: %s", path, format_error.message);
	lfd->entries = parsed.entries;
	lfd->count = parsed.count;
	parsed.entries = NULL;
	TieLfdIndex_Free(&parsed);
	return true;
}

void TieLfdFile_Close(TieLfdFile* lfd) {
	if (!lfd)
		return;
	free(lfd->entries);
	free(lfd->data);
	memset(lfd, 0, sizeof *lfd);
}

const TieLfdFileEntry* TieLfdFile_Find(const TieLfdFile* lfd, uint32_t type, const char* name) {
	if (!lfd || !name)
		return NULL;
	char padded_name[9] = { 0 };
	for (size_t index = 0; index < 8 && name[index]; ++index)
		padded_name[index] = name[index];
	for (uint32_t index = 0; index < lfd->count; ++index) {
		const TieLfdFileEntry* entry = &lfd->entries[index];
		if (entry->type == type && memcmp(entry->name, padded_name, 8) == 0)
			return entry;
	}
	return NULL;
}

const uint8_t* TieLfdFile_Data(const TieLfdFile* lfd, const TieLfdFileEntry* entry) {
	return lfd->data + entry->payload_offset;
}

const TieLfdFileEntry* TieLfdFileChain_Find(const TieLfdFileChain* chain, uint32_t type, const char* name,
											const TieLfdFile** out_owner) {
	if (chain) {
		for (int index = 0; index < chain->count; ++index) {
			const TieLfdFileEntry* entry = TieLfdFile_Find(chain->files[index], type, name);
			if (entry) {
				if (out_owner)
					*out_owner = chain->files[index];
				return entry;
			}
		}
	}
	if (out_owner)
		*out_owner = NULL;
	return NULL;
}
