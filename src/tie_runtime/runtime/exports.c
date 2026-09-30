#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/species_id.h"

#include "tie/create.h"
#include "tie/gate.h"
#include "tie/spec.h"
#include "tie/species.h"
#include "tie/tie.h"

#include <string.h>

extern uint8_t materialcolors[720];
extern const uint8_t highlightmapping[9];
extern const uint8_t targetmapping[39];

uint8_t TieRecoveredData_MeshTypeInitialHp(uint8_t mesh_type) {
	if (mesh_type >= 32)
		return 0xFF;
	return initialdamagestate[mesh_type];
}

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

const void* tie_laser_species_poly(uint16_t species_idx, size_t* out_size) {
	const uint8_t* blob = NULL;
	size_t size = 0;

	if (species_idx >= TIE_SPECIES_PROJECTILE_FIRST &&
		species_idx < TIE_SPECIES_PROJECTILE_FIRST + TIE_SPECIES_PROJECTILE_COUNT)
		blob = projectiledataptrs[species_idx - TIE_SPECIES_PROJECTILE_FIRST];
	if (blob == torpedodata || blob == concussiondata || blob == rocketdata || blob == magneticpulsedata)
		size = sizeof torpedodata;
	else if (blob)
		size = sizeof rebellaserdata;
	if (out_size)
		*out_size = size;
	return blob;
}

/* Advanced after each completed fediskio_loadspecies. Hosts compare it with
 * their cached value to detect that species_table model storage changed. */
static uint32_t s_mission_load_generation;

void TieRecoveredData_AdvanceMissionLoadGeneration(void) { s_mission_load_generation++; }

uint32_t TieRecoveredData_MissionLoadGeneration(void) { return s_mission_load_generation; }

/* Mirrors the species filters of fediskio_loadspecies. */
static bool TieRecoveredData_SpeciesLfdLocation(uint16_t species_idx, uint8_t expected_source,
												TieSpeciesLfdLocation* out) {
	const SpeciesEntry* entry;

	if (out)
		memset(out, 0, sizeof *out);
	if (!out || species_idx >= NUM_SPECIES)
		return false;

	entry = &species_table[species_idx];
	if (!(entry->flags & 2) || !(entry->load_flags & 0x18) || (entry->load_flags & 3) != expected_source ||
		((entry->load_flags & 0x40) && !mission.train_craft_type) || !entry->model_handle ||
		entry->lfd_file >= 3)
		return false;

	out->entry = entry->lfd_entry;
	out->resource_set = tie_is_high_resolution_flight() ? TIE_SPECIES_LFD_RES640 : TIE_SPECIES_LFD_RES320;
	out->lfd_file = entry->lfd_file;
	return true;
}

bool TieRecoveredData_SpeciesDosModelLocation(uint16_t species_idx, TieSpeciesLfdLocation* out) {
	return TieRecoveredData_SpeciesLfdLocation(species_idx, 1, out);
}

bool TieRecoveredData_SpeciesXactLocation(uint16_t species_idx, TieSpeciesLfdLocation* out) {
	return TieRecoveredData_SpeciesLfdLocation(species_idx, 2, out);
}
