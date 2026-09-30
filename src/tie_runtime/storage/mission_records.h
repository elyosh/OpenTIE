#ifndef TIE_RUNTIME_STORAGE_MISSION_RECORDS_H
#define TIE_RUNTIME_STORAGE_MISSION_RECORDS_H

#include "tie/shipext.h"

struct EBriefPage;

void EFGStruct_decode(EFGStruct* dst, const uint8_t* src);
void EFGStruct_encode(uint8_t* dst, const EFGStruct* src);
void EMissionStruct_decode(EMissionStruct* dst, const uint8_t* src);
void EMissionStruct_encode(uint8_t* dst, const EMissionStruct* src);
void MissionFile_decode(MissionFile* dst, const uint8_t* src);
void MissionFile_encode(uint8_t* dst, const MissionFile* src);
void TieBriefing_DecodePage(struct EBriefPage* dst, const uint8_t* src);

#endif
