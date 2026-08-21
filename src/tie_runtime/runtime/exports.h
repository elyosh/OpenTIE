#ifndef TIE_RUNTIME_EXPORTS_H
#define TIE_RUNTIME_EXPORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_runtime/species_id.h"

const uint8_t* TieRecoveredData_MaterialColors(void);
const uint8_t* TieRecoveredData_HighlightMapping(void);
const uint8_t* TieRecoveredData_TargetMapping(void);
const char* TieRecoveredData_GateLabel(int idx);
uint32_t TieRecoveredData_MissionLoadGeneration(void);

typedef enum TieSpeciesLfdResourceSet {
	TIE_SPECIES_LFD_RES320,
	TIE_SPECIES_LFD_RES640,
} TieSpeciesLfdResourceSet;

typedef enum TieSpeciesLfdFile {
	TIE_SPECIES_LFD_SPECIES,
	TIE_SPECIES_LFD_SPECIES2,
	TIE_SPECIES_LFD_SPECIES3,
} TieSpeciesLfdFile;

typedef struct TieSpeciesLfdLocation {
	uint16_t entry;
	uint8_t resource_set;
	uint8_t lfd_file;
} TieSpeciesLfdLocation;

bool TieRecoveredData_SpeciesDosModelLocation(uint16_t species_idx, TieSpeciesLfdLocation* out);
bool TieRecoveredData_SpeciesXactLocation(uint16_t species_idx, TieSpeciesLfdLocation* out);
const void* tie_laser_species_poly(uint16_t species_idx, size_t* out_size);

typedef struct TieShipHardpoints {
	int16_t engine[3];
	int16_t cockpit[3];
	int16_t gun_muzzle[3];
} TieShipHardpoints;

bool TieRecoveredData_ShipHardpoints(uint16_t species_idx, TieShipHardpoints* out);

#define TIE_SPEC_COUNT 69
#define TIE_SPECIES_COUNT TIE_SPECIES_ID_COUNT

const char* tie_species_display_name(uint16_t species_idx);
const char* tie_species_symbolic_name(uint16_t species_idx);
int TieRecoveredData_SpeciesLookup(const char* yaml_value);
uint8_t TieRecoveredData_MeshTypeInitialHp(uint8_t mesh_type);

#endif
