#include "tie_runtime/storage/mission_records.h"
#include "tie/player.h"
#include "util/binio.h"

#include <string.h>

/* Native endian conversion for the original mission and briefing records. */

static void ECondStruct_decode(ECondStruct* dst, const uint8_t* src) {
	dst->cond = src[0];
	dst->type = src[1];
	dst->id = src[2];
	dst->pct = src[3];
}

static void ECondStruct_encode(uint8_t* dst, const ECondStruct* src) {
	dst[0] = src->cond;
	dst[1] = src->type;
	dst[2] = src->id;
	dst[3] = src->pct;
}

static void EAIStruct_decode(EAIStruct* dst, const uint8_t* src) {
	dst->order = src[0x00];
	dst->speed = src[0x01];
	memcpy(dst->var, src + 0x02, 4);
	memcpy(dst->target_type, src + 0x06, 2);
	memcpy(dst->target_id, src + 0x08, 2);
	dst->target_op = src[0x0A];
	dst->target_unused = src[0x0B];
	dst->pri_type = src[0x0C];
	dst->pri_id = src[0x0D];
	dst->sec_type = src[0x0E];
	dst->sec_id = src[0x0F];
	dst->pri_sec_op = src[0x10];
	dst->pri_sec_unused = src[0x11];
}

static void EAIStruct_encode(uint8_t* dst, const EAIStruct* src) {
	dst[0x00] = src->order;
	dst[0x01] = src->speed;
	memcpy(dst + 0x02, src->var, 4);
	memcpy(dst + 0x06, src->target_type, 2);
	memcpy(dst + 0x08, src->target_id, 2);
	dst[0x0A] = src->target_op;
	dst[0x0B] = src->target_unused;
	dst[0x0C] = src->pri_type;
	dst[0x0D] = src->pri_id;
	dst[0x0E] = src->sec_type;
	dst[0x0F] = src->sec_id;
	dst[0x10] = src->pri_sec_op;
	dst[0x11] = src->pri_sec_unused;
}

void EFGStruct_decode(EFGStruct* dst, const uint8_t* src) {
	int i;

	memcpy(dst->name, src + 0x00, 12);
	memcpy(dst->cmdr, src + 0x0C, 12);
	memcpy(dst->contents[0], src + 0x18, 12);
	memcpy(dst->contents[1], src + 0x24, 12);

	dst->special_craft = src[0x30];
	dst->special_flag = src[0x31];
	dst->species = src[0x32];
	dst->count = src[0x33];
	dst->version = src[0x34];
	dst->warhead = src[0x35];
	dst->beam = src[0x36];
	dst->side = src[0x37];
	dst->skill = src[0x38];
	dst->camoflage = src[0x39];
	dst->camo_flag = src[0x3A];
	dst->camo_unused = src[0x3B];
	dst->formation = src[0x3C];
	dst->form_spacing = src[0x3D];
	dst->set = src[0x3E];
	dst->set_unused = src[0x3F];
	dst->waves = src[0x40];
	dst->wave_delay = src[0x41];
	dst->player_flag = src[0x42];
	dst->heading = src[0x43];
	dst->pitch = src[0x44];
	dst->rotation = src[0x45];
	dst->link_flag = src[0x46];
	dst->link_code = src[0x47];
	dst->link_unused = src[0x48];
	dst->difficulty = src[0x49];

	ECondStruct_decode(&dst->start_cond[0], src + 0x4A);
	ECondStruct_decode(&dst->start_cond[1], src + 0x4E);

	dst->start_op = src[0x52];
	dst->start_unused = src[0x53];
	dst->start_delay_min = src[0x54];
	dst->start_delay_sec = src[0x55];

	ECondStruct_decode(&dst->stop_cond, src + 0x56);

	dst->stop_min = src[0x5A];
	dst->stop_sec = src[0x5B];
	dst->stop_abort = src[0x5C];
	dst->stop_unused = src[0x5D];
	dst->cur_start_fg = br_i16le(src + 0x5E);
	dst->start_fg = src[0x60];
	dst->start_fg_used = src[0x61];
	dst->pri_stop_fg = src[0x62];
	dst->pri_stop_fg_used = src[0x63];
	dst->sec_stop_fg = src[0x64];
	dst->sec_stop_fg_used = src[0x65];
	dst->capture_fg = src[0x66];
	dst->capture_fg_used = src[0x67];

	EAIStruct_decode(&dst->ai[0], src + 0x68);
	EAIStruct_decode(&dst->ai[1], src + 0x7A);
	EAIStruct_decode(&dst->ai[2], src + 0x8C);

	dst->pri_win_cond = src[0x9E];
	dst->pri_win_pct = src[0x9F];
	dst->sec_win_cond = src[0xA0];
	dst->sec_win_pct = src[0xA1];
	dst->loss_cond = src[0xA2];
	dst->loss_pct = src[0xA3];
	dst->bonus_cond = src[0xA4];
	dst->bonus_pct = src[0xA5];
	dst->bonus_points = (int8_t)src[0xA6];
	dst->bonus_unused = src[0xA7];

	for (i = 0; i < 15; ++i)
		dst->way_x[i] = br_i16le(src + 0xA8 + i * 2);
	for (i = 0; i < 15; ++i)
		dst->way_y[i] = br_i16le(src + 0xC6 + i * 2);
	for (i = 0; i < 15; ++i)
		dst->way_z[i] = br_i16le(src + 0xE4 + i * 2);
	for (i = 0; i < 15; ++i)
		dst->way_used[i] = br_i16le(src + 0x102 + i * 2);

	dst->way_shown = src[0x120];
	dst->way_unused = src[0x121];
	dst->way_brief_link = src[0x122];
	dst->way_brief_shown = src[0x123];
}

void EFGStruct_encode(uint8_t* dst, const EFGStruct* src) {
	int i;

	memcpy(dst + 0x00, src->name, 12);
	memcpy(dst + 0x0C, src->cmdr, 12);
	memcpy(dst + 0x18, src->contents[0], 12);
	memcpy(dst + 0x24, src->contents[1], 12);

	dst[0x30] = src->special_craft;
	dst[0x31] = src->special_flag;
	dst[0x32] = src->species;
	dst[0x33] = src->count;
	dst[0x34] = src->version;
	dst[0x35] = src->warhead;
	dst[0x36] = src->beam;
	dst[0x37] = src->side;
	dst[0x38] = src->skill;
	dst[0x39] = src->camoflage;
	dst[0x3A] = src->camo_flag;
	dst[0x3B] = src->camo_unused;
	dst[0x3C] = src->formation;
	dst[0x3D] = src->form_spacing;
	dst[0x3E] = src->set;
	dst[0x3F] = src->set_unused;
	dst[0x40] = src->waves;
	dst[0x41] = src->wave_delay;
	dst[0x42] = src->player_flag;
	dst[0x43] = src->heading;
	dst[0x44] = src->pitch;
	dst[0x45] = src->rotation;
	dst[0x46] = src->link_flag;
	dst[0x47] = src->link_code;
	dst[0x48] = src->link_unused;
	dst[0x49] = src->difficulty;

	ECondStruct_encode(dst + 0x4A, &src->start_cond[0]);
	ECondStruct_encode(dst + 0x4E, &src->start_cond[1]);

	dst[0x52] = src->start_op;
	dst[0x53] = src->start_unused;
	dst[0x54] = src->start_delay_min;
	dst[0x55] = src->start_delay_sec;

	ECondStruct_encode(dst + 0x56, &src->stop_cond);

	dst[0x5A] = src->stop_min;
	dst[0x5B] = src->stop_sec;
	dst[0x5C] = src->stop_abort;
	dst[0x5D] = src->stop_unused;
	bw_i16le(dst + 0x5E, src->cur_start_fg);
	dst[0x60] = src->start_fg;
	dst[0x61] = src->start_fg_used;
	dst[0x62] = src->pri_stop_fg;
	dst[0x63] = src->pri_stop_fg_used;
	dst[0x64] = src->sec_stop_fg;
	dst[0x65] = src->sec_stop_fg_used;
	dst[0x66] = src->capture_fg;
	dst[0x67] = src->capture_fg_used;

	EAIStruct_encode(dst + 0x68, &src->ai[0]);
	EAIStruct_encode(dst + 0x7A, &src->ai[1]);
	EAIStruct_encode(dst + 0x8C, &src->ai[2]);

	dst[0x9E] = src->pri_win_cond;
	dst[0x9F] = src->pri_win_pct;
	dst[0xA0] = src->sec_win_cond;
	dst[0xA1] = src->sec_win_pct;
	dst[0xA2] = src->loss_cond;
	dst[0xA3] = src->loss_pct;
	dst[0xA4] = src->bonus_cond;
	dst[0xA5] = src->bonus_pct;
	dst[0xA6] = (uint8_t)src->bonus_points;
	dst[0xA7] = src->bonus_unused;

	for (i = 0; i < 15; ++i)
		bw_i16le(dst + 0xA8 + i * 2, src->way_x[i]);
	for (i = 0; i < 15; ++i)
		bw_i16le(dst + 0xC6 + i * 2, src->way_y[i]);
	for (i = 0; i < 15; ++i)
		bw_i16le(dst + 0xE4 + i * 2, src->way_z[i]);
	for (i = 0; i < 15; ++i)
		bw_i16le(dst + 0x102 + i * 2, src->way_used[i]);

	dst[0x120] = src->way_shown;
	dst[0x121] = src->way_unused;
	dst[0x122] = src->way_brief_link;
	dst[0x123] = src->way_brief_shown;
}

void EMissionStruct_decode(EMissionStruct* dst, const uint8_t* src) {
	dst->time_min = src[0x00];
	dst->time_sec = src[0x01];
	dst->win_type = src[0x02];
	dst->backdrop = src[0x03];
	dst->rescue = src[0x04];
	dst->all_way_shown = src[0x05];
	memcpy(dst->mis_var, src + 0x06, 8);
	dst->win_bonus[0] = (int8_t)src[0x0E];
	dst->win_bonus[1] = (int8_t)src[0x0F];
	memcpy(dst->win_msg1[0], src + 0x010, 64);
	memcpy(dst->win_msg1[1], src + 0x050, 64);
	memcpy(dst->win_msg2[0], src + 0x090, 64);
	memcpy(dst->win_msg2[1], src + 0x0D0, 64);
	memcpy(dst->loss_msg[0], src + 0x110, 64);
	memcpy(dst->loss_msg[1], src + 0x150, 64);
	dst->loss_msg_delay = src[0x190];
	dst->loss_unused = src[0x191];
	memcpy(dst->neutral_name[0], src + 0x192, 12);
	memcpy(dst->neutral_name[1], src + 0x19E, 12);
	memcpy(dst->neutral_name[2], src + 0x1AA, 12);
	memcpy(dst->neutral_name[3], src + 0x1B6, 12);
}

void EMissionStruct_encode(uint8_t* dst, const EMissionStruct* src) {
	dst[0x00] = src->time_min;
	dst[0x01] = src->time_sec;
	dst[0x02] = src->win_type;
	dst[0x03] = src->backdrop;
	dst[0x04] = src->rescue;
	dst[0x05] = src->all_way_shown;
	memcpy(dst + 0x06, src->mis_var, 8);
	dst[0x0E] = (uint8_t)src->win_bonus[0];
	dst[0x0F] = (uint8_t)src->win_bonus[1];
	memcpy(dst + 0x010, src->win_msg1[0], 64);
	memcpy(dst + 0x050, src->win_msg1[1], 64);
	memcpy(dst + 0x090, src->win_msg2[0], 64);
	memcpy(dst + 0x0D0, src->win_msg2[1], 64);
	memcpy(dst + 0x110, src->loss_msg[0], 64);
	memcpy(dst + 0x150, src->loss_msg[1], 64);
	dst[0x190] = src->loss_msg_delay;
	dst[0x191] = src->loss_unused;
	memcpy(dst + 0x192, src->neutral_name[0], 12);
	memcpy(dst + 0x19E, src->neutral_name[1], 12);
	memcpy(dst + 0x1AA, src->neutral_name[2], 12);
	memcpy(dst + 0x1B6, src->neutral_name[3], 12);
}

void MissionFile_decode(MissionFile* dst, const uint8_t* src) {
	dst->num_fg = br_i16le(src + 0x00);
	dst->num_msg = br_i16le(src + 0x02);
	dst->num_goals = br_i16le(src + 0x04);
	EMissionStruct_decode(&dst->mission, src + 0x06);
}

void MissionFile_encode(uint8_t* dst, const MissionFile* src) {
	bw_i16le(dst + 0x00, src->num_fg);
	bw_i16le(dst + 0x02, src->num_msg);
	bw_i16le(dst + 0x04, src->num_goals);
	EMissionStruct_encode(dst + 0x06, &src->mission);
}

void TieBriefing_DecodePage(EBriefPage* dst, const uint8_t* src) {
	dst->len = br_i16le(src + 0x00);
	dst->time = br_i16le(src + 0x02);
	dst->index = br_i16le(src + 0x04);
	dst->size = br_i16le(src + 0x06);
	dst->tile = br_i16le(src + 0x08);
	for (int i = 0; i < 400; ++i)
		dst->commands[i] = br_i16le(src + 0x0A + i * 2);
}
