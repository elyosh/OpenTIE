#ifndef TIE_RUNTIME_SCORE_TABLES_H
#define TIE_RUNTIME_SCORE_TABLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Canonical TIE98 layouts used by both frontends. Legacy TIE95 files remain readable. */
#define TRAIN_SCORE_ENTRY_COUNT 10
#define TRAIN_SCORE_NAME_CAPACITY 33

typedef struct TrainingScoreEntry {
	char name[TRAIN_SCORE_NAME_CAPACITY];
	int32_t score;
	int16_t level;
} TrainingScoreEntry;

bool TieScoreTables_LoadTraining(const char* filename,
								TrainingScoreEntry entries[TRAIN_SCORE_ENTRY_COUNT]);
bool TieScoreTables_SaveTraining(const char* filename,
								const TrainingScoreEntry entries[TRAIN_SCORE_ENTRY_COUNT]);

#define GAME_SCORE_ENTRY_COUNT 10
#define GAME_SCORE_MISSION_NAME_CAPACITY 11
#define GAME_SCORE_NAME_CAPACITY 35

typedef struct GameScoreEntry {
	char name[GAME_SCORE_NAME_CAPACITY];
	int32_t score;
	int16_t status;
} GameScoreEntry;

typedef struct GameScoreHead {
	char name[GAME_SCORE_MISSION_NAME_CAPACITY];
	GameScoreEntry scores[GAME_SCORE_ENTRY_COUNT];
} GameScoreHead;

bool TieScoreTables_LoadGame(const char* filename, GameScoreHead* records, size_t capacity,
							 int16_t* out_count);
bool TieScoreTables_SaveGame(const char* filename, const GameScoreHead* records, int16_t count);

#endif
