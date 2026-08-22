#include "tie_runtime/storage/score_tables.h"

#include <string.h>

#include "tie_runtime/storage/storage.h"
#include "util/binio.h"

enum {
	TRAIN_SCORE_NAME_DISK_SIZE = 32,
	TRAIN_SCORE_ENTRY_DISK_SIZE = TRAIN_SCORE_NAME_DISK_SIZE + 4 + 2,
	TRAIN_SCORE_FILE_DISK_SIZE = TRAIN_SCORE_ENTRY_COUNT * TRAIN_SCORE_ENTRY_DISK_SIZE,
	TRAIN_SCORE_TIE95_ENTRY_COUNT = 8,
	TRAIN_SCORE_TIE95_NAME_DISK_SIZE = 10,
	TRAIN_SCORE_TIE95_ENTRY_DISK_SIZE = TRAIN_SCORE_TIE95_NAME_DISK_SIZE + 4 + 2,
	TRAIN_SCORE_TIE95_FILE_DISK_SIZE =
		TRAIN_SCORE_TIE95_ENTRY_COUNT * TRAIN_SCORE_TIE95_ENTRY_DISK_SIZE,
	GAME_SCORE_MISSION_NAME_DISK_SIZE = 10,
	GAME_SCORE_NAME_DISK_SIZE = 34,
	GAME_SCORE_ENTRY_DISK_SIZE = GAME_SCORE_NAME_DISK_SIZE + 4 + 2,
	GAME_SCORE_HEAD_DISK_SIZE =
		GAME_SCORE_MISSION_NAME_DISK_SIZE + GAME_SCORE_ENTRY_COUNT * GAME_SCORE_ENTRY_DISK_SIZE,
	GAME_SCORE_TIE95_ENTRY_COUNT = 8,
	GAME_SCORE_TIE95_NAME_DISK_SIZE = 10,
	GAME_SCORE_TIE95_ENTRY_DISK_SIZE = GAME_SCORE_TIE95_NAME_DISK_SIZE + 4 + 2,
	GAME_SCORE_HEAD_TIE95_DISK_SIZE =
		GAME_SCORE_MISSION_NAME_DISK_SIZE + GAME_SCORE_TIE95_ENTRY_COUNT * GAME_SCORE_TIE95_ENTRY_DISK_SIZE,
	GAME_SCORE_MISSION_CAPACITY = 20,
};

static size_t fixed_string_length(const char* str, size_t limit) {
	size_t length = 0;
	while (length < limit && str[length])
		length++;
	return length;
}

static void decode_fixed_string(char* dst, size_t capacity, const uint8_t* src, size_t width) {
	size_t length = 0;
	while (length < width && src[length])
		length++;
	if (length >= capacity)
		length = capacity - 1;
	memcpy(dst, src, length);
	dst[length] = '\0';
}

static void encode_fixed_string(uint8_t* dst, size_t width, const char* src) {
	const size_t length = fixed_string_length(src, width);
	memset(dst, 0, width);
	memcpy(dst, src, length);
}

static long score_file_size(TieFile* file) {
	if (TieStorage_Seek(file, 0, TIE_SEEK_END) != 0)
		return -1;
	const long size = TieStorage_Tell(file);
	if (size < 0 || TieStorage_Seek(file, 0, TIE_SEEK_SET) != 0)
		return -1;
	return size;
}

static void decode_training_entry(TrainingScoreEntry* dst, const uint8_t* src, size_t name_width) {
	decode_fixed_string(dst->name, sizeof dst->name, src, name_width);
	dst->score = br_i32le(src + name_width);
	dst->level = br_i16le(src + name_width + 4);
}

static void encode_training_entry(uint8_t* dst, const TrainingScoreEntry* src) {
	encode_fixed_string(dst, TRAIN_SCORE_NAME_DISK_SIZE, src->name);
	bw_i32le(dst + TRAIN_SCORE_NAME_DISK_SIZE, src->score);
	bw_i16le(dst + TRAIN_SCORE_NAME_DISK_SIZE + 4, src->level);
}

bool TieScoreTables_LoadTraining(const char* filename,
								TrainingScoreEntry entries[TRAIN_SCORE_ENTRY_COUNT]) {
	TieFile* file = TieStorage_Open(TIE_FILE_ROOT_USER, filename, "rb");
	if (!file)
		return false;

	const long file_size = score_file_size(file);
	uint8_t bytes[TRAIN_SCORE_FILE_DISK_SIZE];
	TrainingScoreEntry decoded[TRAIN_SCORE_ENTRY_COUNT] = { 0 };
	bool ok = false;

	if (file_size == TRAIN_SCORE_FILE_DISK_SIZE &&
		TieStorage_Read(bytes, 1, TRAIN_SCORE_FILE_DISK_SIZE, file) == TRAIN_SCORE_FILE_DISK_SIZE) {
		for (int i = 0; i < TRAIN_SCORE_ENTRY_COUNT; i++)
			decode_training_entry(&decoded[i], bytes + i * TRAIN_SCORE_ENTRY_DISK_SIZE,
								  TRAIN_SCORE_NAME_DISK_SIZE);
		ok = true;
	} else if (file_size == TRAIN_SCORE_TIE95_FILE_DISK_SIZE &&
			   TieStorage_Read(bytes, 1, TRAIN_SCORE_TIE95_FILE_DISK_SIZE, file) ==
				   TRAIN_SCORE_TIE95_FILE_DISK_SIZE) {
		for (int i = 0; i < TRAIN_SCORE_TIE95_ENTRY_COUNT; i++)
			decode_training_entry(&decoded[i], bytes + i * TRAIN_SCORE_TIE95_ENTRY_DISK_SIZE,
								  TRAIN_SCORE_TIE95_NAME_DISK_SIZE);
		ok = true;
	}

	TieStorage_Close(file);
	if (ok)
		memcpy(entries, decoded, sizeof decoded);
	return ok;
}

bool TieScoreTables_SaveTraining(const char* filename,
								const TrainingScoreEntry entries[TRAIN_SCORE_ENTRY_COUNT]) {
	uint8_t bytes[TRAIN_SCORE_FILE_DISK_SIZE];
	for (int i = 0; i < TRAIN_SCORE_ENTRY_COUNT; i++)
		encode_training_entry(bytes + i * TRAIN_SCORE_ENTRY_DISK_SIZE, &entries[i]);
	return TieStorage_WriteAllAtomic(TIE_FILE_ROOT_USER, filename, bytes, sizeof bytes) == 0;
}

static void decode_game_entry(GameScoreEntry* dst, const uint8_t* src) {
	decode_fixed_string(dst->name, sizeof dst->name, src, GAME_SCORE_NAME_DISK_SIZE);
	dst->score = br_i32le(src + GAME_SCORE_NAME_DISK_SIZE);
	dst->status = br_i16le(src + GAME_SCORE_NAME_DISK_SIZE + 4);
}

static void encode_game_entry(uint8_t* dst, const GameScoreEntry* src) {
	encode_fixed_string(dst, GAME_SCORE_NAME_DISK_SIZE, src->name);
	bw_i32le(dst + GAME_SCORE_NAME_DISK_SIZE, src->score);
	bw_i16le(dst + GAME_SCORE_NAME_DISK_SIZE + 4, src->status);
}

static void decode_game_head(GameScoreHead* dst, const uint8_t* src) {
	decode_fixed_string(dst->name, sizeof dst->name, src, GAME_SCORE_MISSION_NAME_DISK_SIZE);
	for (int i = 0; i < GAME_SCORE_ENTRY_COUNT; i++)
		decode_game_entry(&dst->scores[i],
					  src + GAME_SCORE_MISSION_NAME_DISK_SIZE + i * GAME_SCORE_ENTRY_DISK_SIZE);
}

static void encode_game_head(uint8_t* dst, const GameScoreHead* src) {
	encode_fixed_string(dst, GAME_SCORE_MISSION_NAME_DISK_SIZE, src->name);
	for (int i = 0; i < GAME_SCORE_ENTRY_COUNT; i++)
		encode_game_entry(dst + GAME_SCORE_MISSION_NAME_DISK_SIZE + i * GAME_SCORE_ENTRY_DISK_SIZE,
						  &src->scores[i]);
}

static void decode_tie95_game_head(GameScoreHead* dst, const uint8_t* src) {
	decode_fixed_string(dst->name, sizeof dst->name, src, GAME_SCORE_MISSION_NAME_DISK_SIZE);
	for (int i = 0; i < GAME_SCORE_TIE95_ENTRY_COUNT; i++) {
		const uint8_t* entry =
			src + GAME_SCORE_MISSION_NAME_DISK_SIZE + i * GAME_SCORE_TIE95_ENTRY_DISK_SIZE;
		decode_fixed_string(dst->scores[i].name, sizeof dst->scores[i].name, entry,
							GAME_SCORE_TIE95_NAME_DISK_SIZE);
		dst->scores[i].score = br_i32le(entry + GAME_SCORE_TIE95_NAME_DISK_SIZE);
		dst->scores[i].status = br_i16le(entry + GAME_SCORE_TIE95_NAME_DISK_SIZE + 4);
	}
}

bool TieScoreTables_LoadGame(const char* filename, GameScoreHead* records, size_t capacity,
							 int16_t* out_count) {
	if (!records || !capacity || !out_count)
		return false;
	*out_count = 0;

	TieFile* file = TieStorage_Open(TIE_FILE_ROOT_USER, filename, "rb");
	if (!file)
		return false;
	const long file_size = score_file_size(file);
	uint8_t count_bytes[2];
	if (file_size < 2 || TieStorage_Read(count_bytes, 1, sizeof count_bytes, file) != sizeof count_bytes) {
		TieStorage_Close(file);
		return false;
	}
	const int16_t count = br_i16le(count_bytes);
	if (count < 0 || (size_t)count > capacity) {
		TieStorage_Close(file);
		return false;
	}

	const long tie98_size = 2 + count * GAME_SCORE_HEAD_DISK_SIZE;
	const long tie95_size = 2 + count * GAME_SCORE_HEAD_TIE95_DISK_SIZE;
	const bool tie98 = file_size == tie98_size;
	if (!tie98 && file_size != tie95_size) {
		TieStorage_Close(file);
		return false;
	}

	memset(records, 0, capacity * sizeof *records);
	uint8_t bytes[GAME_SCORE_HEAD_DISK_SIZE];
	const size_t record_size = tie98 ? GAME_SCORE_HEAD_DISK_SIZE : GAME_SCORE_HEAD_TIE95_DISK_SIZE;
	for (int i = 0; i < count; i++) {
		if (TieStorage_Read(bytes, 1, record_size, file) != record_size) {
			memset(records, 0, capacity * sizeof *records);
			TieStorage_Close(file);
			return false;
		}
		if (tie98)
			decode_game_head(&records[i], bytes);
		else
			decode_tie95_game_head(&records[i], bytes);
	}

	TieStorage_Close(file);
	*out_count = count;
	return true;
}

bool TieScoreTables_SaveGame(const char* filename, const GameScoreHead* records, int16_t count) {
	if (!records || count < 0 || count > GAME_SCORE_MISSION_CAPACITY)
		return false;

	uint8_t bytes[2 + GAME_SCORE_MISSION_CAPACITY * GAME_SCORE_HEAD_DISK_SIZE];
	bw_i16le(bytes, count);
	for (int i = 0; i < count; i++)
		encode_game_head(bytes + 2 + i * GAME_SCORE_HEAD_DISK_SIZE, &records[i]);
	const size_t size = 2 + (size_t)count * GAME_SCORE_HEAD_DISK_SIZE;
	return TieStorage_WriteAllAtomic(TIE_FILE_ROOT_USER, filename, bytes, size) == 0;
}
