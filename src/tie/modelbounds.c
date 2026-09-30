#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/tie.h"

/* PORT: the original reads g_model_bounds_min/max after
 * ModelBounds_EnsureCached. The host model view carries the same aggregate
 * bounds; non-OPT entries leave the original cache zeroed. */

// FUNCTION: TIE98 0x43B8C0
// ModelBounds_GetMaxExtent; same name in OpenXWA.
int modelbounds_getmaxextent(uint16_t model_type) {
	const TieModelBounds* value;
	float size_x;
	float size_y;
	float size_z;

	if (model_type != 0 && (species_table[model_type].load_flags & 1) == 0)
		return 0;
	value = &modelmesh_require_model(model_type)->bounds;
	size_x = value->max.x - value->min.x;
	size_y = value->max.y - value->min.y;
	size_z = value->max.z - value->min.z;
	if (size_y >= size_x && size_y >= size_z)
		return (int)size_y;
	if (size_z >= size_x && size_z >= size_y)
		return (int)size_z;
	return (int)size_x;
}

// FUNCTION: TIE98 0x43B970
// ModelBounds_GetMinX
int modelbounds_getminx(uint16_t m) {
	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	return (int)modelmesh_require_model(m)->bounds.min.x;
}
// FUNCTION: TIE98 0x43B9A0
// ModelBounds_GetMinY
int modelbounds_getminy(uint16_t m) {
	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	return (int)modelmesh_require_model(m)->bounds.min.y;
}
// FUNCTION: TIE98 0x43B9D0
// ModelBounds_GetMinZ
int modelbounds_getminz(uint16_t m) {
	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	return (int)modelmesh_require_model(m)->bounds.min.z;
}
// FUNCTION: TIE98 0x43BA00
// ModelBounds_GetMaxX
int modelbounds_getmaxx(uint16_t m) {
	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	return (int)modelmesh_require_model(m)->bounds.max.x;
}
// FUNCTION: TIE98 0x43BA30
// ModelBounds_GetMaxY
int modelbounds_getmaxy(uint16_t m) {
	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	return (int)modelmesh_require_model(m)->bounds.max.y;
}
// FUNCTION: TIE98 0x43BA60
// ModelBounds_GetMaxZ
int modelbounds_getmaxz(uint16_t m) {
	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	return (int)modelmesh_require_model(m)->bounds.max.z;
}

// FUNCTION: TIE98 0x43BA90
// ModelBounds_GetSizeX
int modelbounds_getsizex(uint16_t m) {
	const TieModelBounds* value;

	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	value = &modelmesh_require_model(m)->bounds;
	return (int)(value->max.x - value->min.x);
}
// FUNCTION: TIE98 0x43BAD0
// ModelBounds_GetSizeY
int modelbounds_getsizey(uint16_t m) {
	const TieModelBounds* value;

	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	value = &modelmesh_require_model(m)->bounds;
	return (int)(value->max.y - value->min.y);
}
// FUNCTION: TIE98 0x43BB10
// ModelBounds_GetSizeZ
int modelbounds_getsizez(uint16_t m) {
	const TieModelBounds* value;

	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	value = &modelmesh_require_model(m)->bounds;
	return (int)(value->max.z - value->min.z);
}
