#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/diagnostics/diagnostics.h"

#include "tie/gate.h"
#include "tie/spec.h"
#include "tie/tie.h"

#include <string.h>

extern uint8_t materialcolors[720];
extern const uint8_t highlightmapping[9];
extern const uint8_t targetmapping[39];

const uint8_t* TieRecoveredData_MaterialColors(void) { return materialcolors; }

const uint8_t* TieRecoveredData_HighlightMapping(void) { return highlightmapping; }

const uint8_t* TieRecoveredData_TargetMapping(void) { return targetmapping; }

const char* TieRecoveredData_GateLabel(int idx) {
	/* These assignments mirror fediskio_loadstringdata. */
	switch (idx) {
		case 0:
			return (const char*)gatelevelstr;
		case 1:
			return (const char*)gateremainstr;
		case 2:
			return (const char*)gatepassedstr;
		case 3:
			return (const char*)targetshitstr;
		case 4:
			return (const char*)scorestr;
		default:
			return NULL;
	}
}

bool TieRecoveredData_ShipHardpoints(uint16_t species_idx, TieShipHardpoints* out) {
	if (!out)
		return false;
	memset(out, 0, sizeof *out);
	if (species_idx >= NUM_SPECIES)
		return false;
	uint16_t spec_num = spec_getspecnum(species_idx);
	if (spec_num >= NUM_SPEC_DATA)
		return false;
	const SpecData* sd = &spec_data[spec_num];
	/* Convert SpecData's side/up/forward order to side/forward/up. */
	out->engine[0] = sd->engine_x;
	out->engine[1] = sd->engine_z;
	out->engine[2] = sd->engine_y;
	out->cockpit[0] = sd->cockpit_x;
	out->cockpit[1] = sd->cockpit_z;
	out->cockpit[2] = sd->cockpit_y;
	out->gun_muzzle[0] = 0;
	out->gun_muzzle[1] = sd->gun_muzzle_fwd;
	out->gun_muzzle[2] = sd->gun_muzzle_up;
	return true;
}
