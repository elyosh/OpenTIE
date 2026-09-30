#ifndef TIE_RUNTIME_AUDIO_FLIGHT_SOUND_BANK_H
#define TIE_RUNTIME_AUDIO_FLIGHT_SOUND_BANK_H

#include <stdint.h>

/* Port-owned flight sound-bank bookkeeping for the recovered FSFX module.
 * The selected flight edition fixes the numeric sound-table layout; loaded
 * samples live in FSFX soundhandles and their "BANK:RECORD" names in
 * FSFX soundnames for the TIE98 name-based FrontendSound layer. */

typedef struct TieFlightSoundLayout {
	uint16_t table_count;
	uint16_t mission_voice_base;
	uint16_t mission_voice_count;
	uint8_t has_player_engine_loops;
} TieFlightSoundLayout;

/* Numeric sound-table layout for the selected flight edition. */
TieFlightSoundLayout TieFlightSound_Layout(void);

/* Translate the stable mission-voice logical index to the selected
 * edition's numeric sound ID. Returns UINT16_MAX for an invalid index. */
uint16_t TieFlightSound_MissionVoiceId(uint16_t logical_index);

/* Canonical LFD name table shared by the recovered FrontendSound layer. */
const char* TieFlightSound_Name(uint16_t sound_id);
int TieFlightSound_FindId(const char* name);
void TieFlightSound_StoreName(uint16_t sound_id, const char* bank_path, const uint8_t record_name[8]);

/* Load one RMAP sound bank into soundhandles[start_idx ..end_idx). max_records
 * caps the usable slice when nonzero. Returns the number of handles allocated. */
int TieFlightSound_LoadBank(const char* filename, int start_idx, int end_idx, int max_records);

#endif
