/*
 * panel_int — implementation.
 *
 * Byte-for-byte parity with src/tie/panel.c::PanelViewDef_decode and
 * HudInstrument_decode. Field offsets verified against retail Z_TIE__.EXE
 * and against decoded test files in CP640/.
 */
#include "panel_int.h"

#include "imgbake/byteio.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void TieCockpitPanelInt_SetError(char* err, size_t errsz, const char* fmt, ...) {
	if (!err || !errsz)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, errsz, fmt, ap);
	va_end(ap);
}

static void TieCockpitPanelInt_DecodeView(TieCockpitPanelIntView* dst, const uint8_t* src) {
	dst->flags = src[0x00];
	memcpy(dst->name, src + 0x01, 9);
	dst->name[9] = '\0';
	dst->pos_x = rd_u16(src + 0x0A);
	dst->pos_y = rd_u16(src + 0x0C);
	dst->width = rd_u16(src + 0x0E);
	dst->depth = rd_u16(src + 0x10);
	dst->yoffset = rd_i16(src + 0x12);
	memcpy(dst->title, src + 0x14, 16);
	dst->title[16] = '\0';
}

static void TieCockpitPanelInt_DecodeInstr(TieCockpitPanelIntInstruction* dst, const uint8_t* src) {
	dst->x = rd_u16(src + 0x00);
	dst->y = rd_u16(src + 0x02);
	dst->param1 = src[0x04];
	dst->param2 = src[0x05];
}

bool TieCockpitPanelInt_Open(TieCockpitPanelInt* out, const char* path, char* err, size_t errsz) {
	memset(out, 0, sizeof *out);

	FILE* fp = fopen(path, "rb");
	if (!fp) {
		TieCockpitPanelInt_SetError(err, errsz, "cannot open %s: %s", path, strerror(errno));
		return false;
	}

	uint8_t buf[PANEL_INT_FILE_SIZE];
	size_t n = fread(buf, 1, sizeof buf, fp);
	int more = (fgetc(fp) != EOF);
	fclose(fp);

	if (n != sizeof buf || more) {
		TieCockpitPanelInt_SetError(err, errsz, "%s: expected %u bytes, got %zu%s", path, PANEL_INT_FILE_SIZE,
									n, more ? "+ (file longer)" : "");
		return false;
	}

	const uint8_t* p = buf;
	for (int i = 0; i < PANEL_INT_NUM_VIEWS; ++i, p += 36)
		TieCockpitPanelInt_DecodeView(&out->views[i], p);
	for (int i = 0; i < PANEL_INT_NUM_INSTRUMENTS; ++i, p += 6)
		TieCockpitPanelInt_DecodeInstr(&out->instruments[i], p);
	memcpy(out->parts, p, PANEL_INT_NUM_PARTS);

	memcpy(out->parts_basename, out->parts, 8);
	out->parts_basename[8] = '\0';
	out->parts_shape_count = (uint16_t)out->parts[9] + (uint16_t)out->parts[10];

	return true;
}
