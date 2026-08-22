/* Edit the mission selection in a TIE Fighter .tfr pilot save. */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

enum {
	PILOT_RECORD_SIZE = 1928,
	PILOT_FILE_SIZE = 2 * PILOT_RECORD_SIZE,
	PILOT_BATTLE_COUNT = 20,
	PILOT_MISSIONS_PER_BATTLE = 8,
	PILOT_EXIT_STATUS_OFFSET = 0x001,
	PILOT_GAME_LEVEL_OFFSET = 0x003,
	PILOT_CURRENT_BATTLE_OFFSET = 0x268,
	PILOT_BATTLE_STATUS_OFFSET = 0x269,
	PILOT_BATTLE_CURSOR_OFFSET = 0x27D,
	PILOT_LINKED_DATA_OFFSET = 0x291,
	PILOT_LINKED_DATA_SIZE = 256,
};

_Static_assert(PILOT_LINKED_DATA_OFFSET + PILOT_LINKED_DATA_SIZE <= PILOT_RECORD_SIZE,
			   "pilot record offsets exceed the disk slot");

typedef struct PilotEditOptions {
	const char* input_path;
	const char* output_path;
	unsigned battle;
	unsigned mission;
	bool keep_linked_data;
	bool sync_backup;
} PilotEditOptions;

static void PilotEdit_Usage(FILE* stream, const char* program) {
	fprintf(stream,
			"Usage:\n"
			"  %s show <pilot.tfr>\n"
			"  %s set-mission <pilot.tfr> <battle> <mission> [options]\n"
			"\n"
			"Options:\n"
			"  --output <pilot.tfr>   Write a new pilot file instead of editing in place\n"
			"  --keep-linked-data     Preserve cross-mission attrition state\n"
			"  --sync-backup          Copy the edited current record to the embedded backup\n"
			"\n"
			"Battle and mission numbers are one-based. In-place edits create <pilot.tfr>.bak.\n",
			program, program);
}

static bool PilotEdit_ReadFile(const char* path, uint8_t data[PILOT_FILE_SIZE]) {
	FILE* file = fopen(path, "rb");
	if (!file) {
		fprintf(stderr, "pilot_edit: cannot open %s: %s\n", path, strerror(errno));
		return false;
	}

	const size_t size = fread(data, 1, PILOT_FILE_SIZE, file);
	const int trailing = fgetc(file);
	const bool read_error = ferror(file) != 0;
	if (fclose(file) != 0) {
		fprintf(stderr, "pilot_edit: cannot close %s: %s\n", path, strerror(errno));
		return false;
	}
	if (read_error) {
		fprintf(stderr, "pilot_edit: cannot read %s\n", path);
		return false;
	}
	if (size != PILOT_FILE_SIZE || trailing != EOF) {
		fprintf(stderr, "pilot_edit: %s is not a %u-byte pilot save\n", path, (unsigned)PILOT_FILE_SIZE);
		return false;
	}
	return true;
}

static char* PilotEdit_AppendSuffix(const char* path, const char* suffix) {
	const size_t path_size = strlen(path);
	const size_t suffix_size = strlen(suffix);
	if (path_size > SIZE_MAX - suffix_size - 1)
		return NULL;
	char* result = malloc(path_size + suffix_size + 1);
	if (!result)
		return NULL;
	memcpy(result, path, path_size);
	memcpy(result + path_size, suffix, suffix_size + 1);
	return result;
}

static bool PilotEdit_ReplaceFile(const char* temporary, const char* destination) {
#ifdef _WIN32
	if (MoveFileExA(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return true;
	fprintf(stderr, "pilot_edit: cannot replace %s (Windows error %lu)\n", destination,
			(unsigned long)GetLastError());
#else
	if (rename(temporary, destination) == 0)
		return true;
	fprintf(stderr, "pilot_edit: cannot replace %s: %s\n", destination, strerror(errno));
#endif
	return false;
}

static bool PilotEdit_WriteFileAtomic(const char* path, const uint8_t data[PILOT_FILE_SIZE]) {
	char* temporary = PilotEdit_AppendSuffix(path, ".tmp");
	if (!temporary) {
		fprintf(stderr, "pilot_edit: cannot allocate temporary path\n");
		return false;
	}

	FILE* file = fopen(temporary, "wb");
	if (!file) {
		fprintf(stderr, "pilot_edit: cannot open %s: %s\n", temporary, strerror(errno));
		free(temporary);
		return false;
	}

	bool success = fwrite(data, 1, PILOT_FILE_SIZE, file) == PILOT_FILE_SIZE;
	if (success && fflush(file) != 0)
		success = false;
	if (fclose(file) != 0)
		success = false;
	if (!success) {
		fprintf(stderr, "pilot_edit: cannot write %s: %s\n", temporary, strerror(errno));
		remove(temporary);
		free(temporary);
		return false;
	}

	success = PilotEdit_ReplaceFile(temporary, path);
	if (!success)
		remove(temporary);
	free(temporary);
	return success;
}

static bool PilotEdit_ValidateRecord(const uint8_t* record, const char* slot_name) {
	const unsigned battle = record[PILOT_CURRENT_BATTLE_OFFSET];
	if (battle >= PILOT_BATTLE_COUNT) {
		fprintf(stderr, "pilot_edit: %s slot has invalid current battle %u\n", slot_name, battle);
		return false;
	}
	for (unsigned index = 0; index < PILOT_BATTLE_COUNT; ++index) {
		const unsigned status = record[PILOT_BATTLE_STATUS_OFFSET + index];
		const unsigned cursor = record[PILOT_BATTLE_CURSOR_OFFSET + index];
		if (status > 3) {
			fprintf(stderr, "pilot_edit: %s slot has invalid status %u for battle %u\n", slot_name, status,
					index + 1);
			return false;
		}
		if (cursor > PILOT_MISSIONS_PER_BATTLE) {
			fprintf(stderr, "pilot_edit: %s slot has invalid mission cursor %u for battle %u\n", slot_name,
					cursor, index + 1);
			return false;
		}
	}
	return true;
}

static bool PilotEdit_ValidateFile(const uint8_t data[PILOT_FILE_SIZE]) {
	return PilotEdit_ValidateRecord(data, "current") &&
		   PilotEdit_ValidateRecord(data + PILOT_RECORD_SIZE, "backup");
}

static const char* PilotEdit_StatusName(unsigned status) {
	switch (status) {
		case 0:
			return "not started";
		case 1:
			return "active";
		case 2:
			return "failed";
		case 3:
			return "completed";
		default:
			return "invalid";
	}
}

static const char* PilotEdit_DifficultyName(unsigned difficulty) {
	switch (difficulty) {
		case 0:
			return "easy";
		case 1:
			return "medium";
		case 2:
			return "hard";
		default:
			return "unknown";
	}
}

static void PilotEdit_ShowRecord(const uint8_t* record, const char* name) {
	const unsigned battle = record[PILOT_CURRENT_BATTLE_OFFSET];
	const unsigned mission = record[PILOT_BATTLE_CURSOR_OFFSET + battle];
	const unsigned status = record[PILOT_BATTLE_STATUS_OFFSET + battle];
	const unsigned difficulty = record[PILOT_GAME_LEVEL_OFFSET];

	printf("%s:\n", name);
	printf("  pilot status: %s", record[PILOT_EXIT_STATUS_OFFSET] ? "unavailable" : "alive");
	if (record[PILOT_EXIT_STATUS_OFFSET])
		printf(" (%u)", (unsigned)record[PILOT_EXIT_STATUS_OFFSET]);
	printf("\n");
	printf("  difficulty: %s (%u)\n", PilotEdit_DifficultyName(difficulty), difficulty);
	printf("  battle: %u (%s)\n", battle + 1, PilotEdit_StatusName(status));
	printf("  mission: %u\n", mission + 1);
}

static int PilotEdit_Show(const char* path) {
	uint8_t data[PILOT_FILE_SIZE];
	if (!PilotEdit_ReadFile(path, data) || !PilotEdit_ValidateFile(data))
		return 1;
	PilotEdit_ShowRecord(data, "Current slot");
	printf("\n");
	PilotEdit_ShowRecord(data + PILOT_RECORD_SIZE, "Backup slot");
	return 0;
}

static bool PilotEdit_ParseIndex(const char* text, unsigned maximum, const char* name, unsigned* out) {
	char* end = NULL;
	errno = 0;
	const unsigned long value = strtoul(text, &end, 10);
	if (errno != 0 || !text[0] || !end || *end || value < 1 || value > maximum) {
		fprintf(stderr, "pilot_edit: %s must be between 1 and %u\n", name, maximum);
		return false;
	}
	*out = (unsigned)value;
	return true;
}

static bool PilotEdit_ParseSetMissionOptions(int argc, char** argv, PilotEditOptions* options) {
	if (argc < 5 || !PilotEdit_ParseIndex(argv[3], PILOT_BATTLE_COUNT, "battle", &options->battle) ||
		!PilotEdit_ParseIndex(argv[4], PILOT_MISSIONS_PER_BATTLE, "mission", &options->mission))
		return false;

	options->input_path = argv[2];
	for (int index = 5; index < argc; ++index) {
		if (strcmp(argv[index], "--output") == 0) {
			if (++index >= argc || options->output_path) {
				fprintf(stderr, "pilot_edit: --output requires one path\n");
				return false;
			}
			options->output_path = argv[index];
		} else if (strcmp(argv[index], "--keep-linked-data") == 0) {
			options->keep_linked_data = true;
		} else if (strcmp(argv[index], "--sync-backup") == 0) {
			options->sync_backup = true;
		} else {
			fprintf(stderr, "pilot_edit: unknown option %s\n", argv[index]);
			return false;
		}
	}
	return true;
}

static void PilotEdit_SelectMission(uint8_t* record, unsigned battle_number, unsigned mission_number,
									bool keep_linked_data) {
	const unsigned battle = battle_number - 1;
	record[PILOT_EXIT_STATUS_OFFSET] = 0;
	record[PILOT_CURRENT_BATTLE_OFFSET] = (uint8_t)battle;

	/* Completed earlier battles satisfy every tour prerequisite gate. */
	for (unsigned index = 0; index < battle; ++index)
		record[PILOT_BATTLE_STATUS_OFFSET + index] = 3;
	record[PILOT_BATTLE_STATUS_OFFSET + battle] = 1;
	record[PILOT_BATTLE_CURSOR_OFFSET + battle] = (uint8_t)(mission_number - 1);
	if (!keep_linked_data)
		memset(record + PILOT_LINKED_DATA_OFFSET, 0, PILOT_LINKED_DATA_SIZE);
}

static int PilotEdit_SetMission(const PilotEditOptions* options) {
	uint8_t data[PILOT_FILE_SIZE];
	if (!PilotEdit_ReadFile(options->input_path, data) || !PilotEdit_ValidateFile(data))
		return 1;
	uint8_t original[PILOT_FILE_SIZE];
	memcpy(original, data, sizeof original);

	PilotEdit_SelectMission(data, options->battle, options->mission, options->keep_linked_data);
	if (options->sync_backup)
		memcpy(data + PILOT_RECORD_SIZE, data, PILOT_RECORD_SIZE);

	const char* destination = options->output_path ? options->output_path : options->input_path;
	const bool in_place = strcmp(destination, options->input_path) == 0;
	if (in_place) {
		char* backup_path = PilotEdit_AppendSuffix(options->input_path, ".bak");
		if (!backup_path) {
			fprintf(stderr, "pilot_edit: cannot allocate backup path\n");
			return 1;
		}
		const bool backup_written = PilotEdit_WriteFileAtomic(backup_path, original);
		free(backup_path);
		if (!backup_written)
			return 1;
	}

	if (!PilotEdit_WriteFileAtomic(destination, data))
		return 1;
	printf("Selected battle %u, mission %u in %s%s.\n", options->battle, options->mission, destination,
		   options->sync_backup ? " (backup slot synchronized)" : "");
	return 0;
}

int main(int argc, char** argv) {
	if (argc == 3 && strcmp(argv[1], "show") == 0)
		return PilotEdit_Show(argv[2]);
	if (argc >= 2 && strcmp(argv[1], "set-mission") == 0) {
		PilotEditOptions options = { 0 };
		if (!PilotEdit_ParseSetMissionOptions(argc, argv, &options)) {
			PilotEdit_Usage(stderr, argv[0]);
			return 2;
		}
		return PilotEdit_SetMission(&options);
	}
	PilotEdit_Usage(stderr, argv[0]);
	return 2;
}
