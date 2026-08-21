/*
 * Compiled CMD-CRT occlusion masks shared by tie_core, runtime conversion,
 * and the offline cockpit tool.
 *
 * Layout: scanline-RLE tables, [sign_byte] [run0] [run1] ... per row.
 */

#ifndef TIE_COCKPIT_MASKS_H
#define TIE_COCKPIT_MASKS_H

#include <stddef.h>
#include <stdint.h>

/* 320x200 (CMD-CRT @ 200-byte stride) -- one table per craft variant. */
extern const uint8_t cmdmaskdata[200];
extern const uint8_t gunboatcmdmaskdata[200];
extern const uint8_t tieadvcmdmaskdata[200];
extern const uint8_t missileboatcmdmaskdata[200];

/* 640x480 (CMD-CRT @ 480-byte stride). The tieadv7/8 / spec4/5 variants
 * are selected by player_spec_num at runtime. */
extern const uint8_t cmd640maskdata[480];
extern const uint8_t gunboatcmd640maskdata[480];
extern const uint8_t tieadv7cmd640maskdata[480]; /* player_spec_num 7 */
extern const uint8_t tieadv8cmd640maskdata[480]; /* player_spec_num 8 */
extern const uint8_t spec5cmd640maskdata[480];   /* player_spec_num 5 */
extern const uint8_t spec4cmd640maskdata[480];   /* player_spec_num 4 */
extern const uint8_t missileboatcmd640maskdata[480];

const uint8_t* TieCockpitMaskData_Get(uint8_t variant, uint16_t classic_width, size_t* out_size);

#endif
