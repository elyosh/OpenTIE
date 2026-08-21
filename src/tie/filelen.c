/*
 * FILELEN -- file-length helper.
 *
 * Single function: FILELEN_filelength (retail 0x891C5). The binary uses
 * fseek(fp, 0, TIE_SEEK_END) / ftell / rewind; same shape here.
 */

#include "tie/filelen.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include <stdint.h>

int32_t filelen_filelength(TieFile* fp) {
	if (!fp)
		return 0;
	if (TieStorage_Seek(fp, 0L, TIE_SEEK_END) != 0)
		return 0;
	long n = TieStorage_Tell(fp);
	if (TieStorage_Seek(fp, 0L, TIE_SEEK_SET) != 0)
		return 0;
	return (n < 0) ? 0 : (int32_t)n;
}
