#include "tie_runtime/storage/pilot_storage.h"
#include "util/binio.h"

#include <string.h>

void PilotRecord_decode(PilotRecord* dst, const uint8_t* src) {
	int i;
	int s;
	int c;
	int b;
	int m;

	dst->version = src[0x000];
	dst->exit_status = src[0x001];
	dst->rank = src[0x002];
	dst->game_level = src[0x003];
	dst->score = br_i32le(src + 0x004);
	dst->avg_score = br_u16le(src + 0x008);
	dst->secret_order_rank = src[0x00A];
	dst->reserved_0b = src[0x00B];
	dst->secret_completions = br_u16le(src + 0x00C);
	dst->secret_score = br_i32le(src + 0x00E);
	memcpy(dst->reserved_12, src + 0x012, 10);

	dst->cur_train_ship = src[0x01C];
	memcpy(dst->train_level, src + 0x01D, NUM_SHIPS);
	dst->reserved_29 = src[0x029];
	for (i = 0; i < NUM_SHIPS; ++i)
		dst->train_score[i] = br_i32le(src + 0x02A + i * 4);
	memcpy(dst->train_max_level, src + 0x05A, NUM_SHIPS);

	dst->cur_combat_ship = src[0x066];
	memcpy(dst->combat_course_cursor, src + 0x067, SHIP_INFO_SIZE);
	dst->reserved_87 = src[0x087];
	for (s = 0; s < NUM_SHIPS; ++s)
		for (c = 0; c < 8; ++c)
			dst->combat_score[s][c] = br_i32le(src + 0x088 + (s * 8 + c) * 4);
	for (s = 0; s < NUM_SHIPS; ++s)
		memcpy(dst->combat_complete[s], src + 0x208 + s * 8, 8);

	dst->cur_battle = src[0x268];
	memcpy(dst->battle_status, src + 0x269, NUM_BATTLES);
	memcpy(dst->battle_cursor, src + 0x27D, NUM_BATTLES);
	memcpy(dst->linked_data, src + 0x291, 256);
	memcpy(dst->secret_complete_bits, src + 0x391, NUM_BATTLES);
	memcpy(dst->mission_bonus_bits, src + 0x3A5, NUM_BATTLES);
	memcpy(dst->reserved_3b9, src + 0x3B9, 29);
	memcpy(dst->reserved_3d6, src + 0x3D6, 4);

	for (b = 0; b < NUM_BATTLES; ++b)
		for (m = 0; m < 8; ++m)
			dst->tour_score[b][m] = br_i32le(src + 0x3DA + (b * 8 + m) * 4);

	dst->total_kills = br_u16le(src + 0x65A);
	dst->total_captures = br_u16le(src + 0x65C);
	memcpy(dst->reserved_65e, src + 0x65E, 2);
	for (i = 0; i < 69; ++i)
		dst->kills_by_ship_type[i] = br_u16le(src + 0x660 + i * 2);
	for (i = 0; i < 69; ++i)
		dst->captures_by_ship_type[i] = br_u16le(src + 0x6EA + i * 2);

	dst->laser_total = br_i32le(src + 0x774);
	dst->laser_hits = br_i32le(src + 0x778);
	memcpy(dst->reserved_77c, src + 0x77C, 4);
	dst->warhead_total = br_u16le(src + 0x780);
	dst->warhead_hits = br_u16le(src + 0x782);
	memcpy(dst->reserved_784, src + 0x784, 2);
	dst->ejection_count = br_u16le(src + 0x786);
}

void PilotRecord_encode(uint8_t* dst, const PilotRecord* src) {
	int i;
	int s;
	int c;
	int b;
	int m;

	dst[0x000] = src->version;
	dst[0x001] = src->exit_status;
	dst[0x002] = src->rank;
	dst[0x003] = src->game_level;
	bw_i32le(dst + 0x004, src->score);
	bw_u16le(dst + 0x008, src->avg_score);
	dst[0x00A] = src->secret_order_rank;
	dst[0x00B] = src->reserved_0b;
	bw_u16le(dst + 0x00C, src->secret_completions);
	bw_i32le(dst + 0x00E, src->secret_score);
	memcpy(dst + 0x012, src->reserved_12, 10);

	dst[0x01C] = src->cur_train_ship;
	memcpy(dst + 0x01D, src->train_level, NUM_SHIPS);
	dst[0x029] = src->reserved_29;
	for (i = 0; i < NUM_SHIPS; ++i)
		bw_i32le(dst + 0x02A + i * 4, src->train_score[i]);
	memcpy(dst + 0x05A, src->train_max_level, NUM_SHIPS);

	dst[0x066] = src->cur_combat_ship;
	memcpy(dst + 0x067, src->combat_course_cursor, SHIP_INFO_SIZE);
	dst[0x087] = src->reserved_87;
	for (s = 0; s < NUM_SHIPS; ++s)
		for (c = 0; c < 8; ++c)
			bw_i32le(dst + 0x088 + (s * 8 + c) * 4, src->combat_score[s][c]);
	for (s = 0; s < NUM_SHIPS; ++s)
		memcpy(dst + 0x208 + s * 8, src->combat_complete[s], 8);

	dst[0x268] = src->cur_battle;
	memcpy(dst + 0x269, src->battle_status, NUM_BATTLES);
	memcpy(dst + 0x27D, src->battle_cursor, NUM_BATTLES);
	memcpy(dst + 0x291, src->linked_data, 256);
	memcpy(dst + 0x391, src->secret_complete_bits, NUM_BATTLES);
	memcpy(dst + 0x3A5, src->mission_bonus_bits, NUM_BATTLES);
	memcpy(dst + 0x3B9, src->reserved_3b9, 29);
	memcpy(dst + 0x3D6, src->reserved_3d6, 4);

	for (b = 0; b < NUM_BATTLES; ++b)
		for (m = 0; m < 8; ++m)
			bw_i32le(dst + 0x3DA + (b * 8 + m) * 4, src->tour_score[b][m]);

	bw_u16le(dst + 0x65A, src->total_kills);
	bw_u16le(dst + 0x65C, src->total_captures);
	memcpy(dst + 0x65E, src->reserved_65e, 2);
	for (i = 0; i < 69; ++i)
		bw_u16le(dst + 0x660 + i * 2, src->kills_by_ship_type[i]);
	for (i = 0; i < 69; ++i)
		bw_u16le(dst + 0x6EA + i * 2, src->captures_by_ship_type[i]);

	bw_i32le(dst + 0x774, src->laser_total);
	bw_i32le(dst + 0x778, src->laser_hits);
	memcpy(dst + 0x77C, src->reserved_77c, 4);
	bw_u16le(dst + 0x780, src->warhead_total);
	bw_u16le(dst + 0x782, src->warhead_hits);
	memcpy(dst + 0x784, src->reserved_784, 2);
	bw_u16le(dst + 0x786, src->ejection_count);
}
