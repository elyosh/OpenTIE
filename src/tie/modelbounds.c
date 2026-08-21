#include "tie/modelbounds.h"

#include "tie/modelmesh.h"
#include "tie/shell.h"
#include "tie/tie.h"

static const TieModelBounds* bounds(uint16_t model_type) {
	static const TieModelBounds empty_bounds;
	// PORT: original non-OPT entries leave their bounds cache zeroed.
	if (model_type != 0 && (species_table[model_type].load_flags & 1) == 0)
		return &empty_bounds;
	return &modelmesh_require_model(model_type)->bounds;
}

static int max_extent(const TieModelBounds* value) {
	float extent = value->max.x - value->min.x;
	const float y = value->max.y - value->min.y;
	const float z = value->max.z - value->min.z;
	if (y > extent)
		extent = y;
	if (z > extent)
		extent = z;
	return (int)extent;
}

// FUNCTION: TIE98 0x43B8C0 ModelBounds_GetMaxExtent; same name in OpenXWA.
int modelbounds_getmaxextent(uint16_t model_type) { return max_extent(bounds(model_type)); }

/* MODERN ADAPTATION: frontend BPFLIGHT renders the stock TIE98 OPT even when
 * authored GLB models drive flight simulation. Query the model view owned by
 * that same stock repository without changing the recovered flight registry. */
int modelbounds_getmaxextent_from_api(const TieFlightModelApi* models, uint16_t model_type) {
	if (!models || !models->acquire)
		shell_programexit("TIE98 original model repository is unavailable");
	char error[768] = { 0 };
	const TieFlightModelView* model = models->acquire(models->context, model_type, error, sizeof error);
	if (!model)
		shell_programexit(error[0] ? error : "TIE98 original model is unavailable");
	return max_extent(&model->bounds);
}

// FUNCTION: TIE98 0x43B970 ModelBounds_GetMinX
int modelbounds_getminx(uint16_t m) { return (int)bounds(m)->min.x; }
// FUNCTION: TIE98 0x43B9A0 ModelBounds_GetMinY
int modelbounds_getminy(uint16_t m) { return (int)bounds(m)->min.y; }
// FUNCTION: TIE98 0x43B9D0 ModelBounds_GetMinZ
int modelbounds_getminz(uint16_t m) { return (int)bounds(m)->min.z; }
// FUNCTION: TIE98 0x43BA00 ModelBounds_GetMaxX
int modelbounds_getmaxx(uint16_t m) { return (int)bounds(m)->max.x; }
// FUNCTION: TIE98 0x43BA30 ModelBounds_GetMaxY
int modelbounds_getmaxy(uint16_t m) { return (int)bounds(m)->max.y; }
// FUNCTION: TIE98 0x43BA60 ModelBounds_GetMaxZ
int modelbounds_getmaxz(uint16_t m) { return (int)bounds(m)->max.z; }

// FUNCTION: TIE98 0x43BA90 ModelBounds_GetSizeX
int modelbounds_getsizex(uint16_t m) {
	const TieModelBounds* value = bounds(m);
	return (int)(value->max.x - value->min.x);
}
// FUNCTION: TIE98 0x43BAD0 ModelBounds_GetSizeY
int modelbounds_getsizey(uint16_t m) {
	const TieModelBounds* value = bounds(m);
	return (int)(value->max.y - value->min.y);
}
// FUNCTION: TIE98 0x43BB10 ModelBounds_GetSizeZ
int modelbounds_getsizez(uint16_t m) {
	const TieModelBounds* value = bounds(m);
	return (int)(value->max.z - value->min.z);
}
