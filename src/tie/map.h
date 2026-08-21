#ifndef __MAP_H__
#define __MAP_H__

#include "tie/shellext.h"
#include <stdint.h>

/* Combat/training high score file record format.
 * Files: shipNN.hgh (per-ship, 8 missions), battleNN.hgh (per-battle, 20 missions).
 * Written by MAP_Update_Debrief_Combat_Scores, read by COMBAT_Load_Combat_High_Scores.
 *
 * Naturally aligned in memory: name[10] + i32 needs 4-byte alignment,
 * so dropping the original pragma pack(1) widens GameScoreEntry from
 * 16 to 20 bytes and GameScoreHead from 138 to 172 bytes on the host.
 * On-disk layout is unchanged -- the codec helpers below reproduce
 * the canonical 16/138-byte little-endian image the original DOS
 * binary writes. */
typedef struct {
	char name[10];
	int32_t score;
	int16_t status; /* kill count */
} GameScoreEntry;

#define GAMESCOREENTRY_DISK_SIZE 16u

void GameScoreEntry_decode(GameScoreEntry* dst, const uint8_t* src);
void GameScoreEntry_encode(uint8_t* dst, const GameScoreEntry* src);

typedef struct {
	char name[10];
	GameScoreEntry scores[8];
} GameScoreHead;

#define GAMESCOREHEAD_DISK_SIZE 138u

void GameScoreHead_decode(GameScoreHead* dst, const uint8_t* src);
void GameScoreHead_encode(uint8_t* dst, const GameScoreHead* src);

/* Push the MAP/briefing display module as a tie_core task.
 * Scenes: 123=training, 133/134=combat sim, 181=briefing. */
void map_Push_Map_Task(SceneHeadStruct* scene_head);

/* Training pilot medal status — extern per watdbg, set by MAP, read by SHELL */
extern int16_t train_pilot_medal_status;

#endif
