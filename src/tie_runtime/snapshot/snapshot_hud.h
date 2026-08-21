#ifndef TIE_RUNTIME_SNAPSHOT_HUD_H
#define TIE_RUNTIME_SNAPSHOT_HUD_H

#include <stdbool.h>
#include <stdint.h>

void TieHudSnapshot_RecordPipCamera(uint16_t target_slot);
void TieHudSnapshot_RecordPipSubsystem(uint16_t target_slot);
void TieHudSnapshot_RecordRadarBlip(bool forward, int index, uint8_t color, int16_t offset_x,
									int16_t offset_y);
void TieHudSnapshot_RecordRadarBracket(bool forward, int16_t offset_x, int16_t offset_y);
void TieHudSnapshot_RecordLaserCharge(uint16_t index, int16_t led_count, uint8_t filled_frame);
void TieHudSnapshot_RecordInstrumentDisplay(uint16_t index, int16_t value, uint8_t color, uint8_t digits);
void TieHudSnapshot_Capture(void);

#endif
