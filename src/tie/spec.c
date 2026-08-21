#include "tie/spec.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Packed initial values for 69 SpecData records. */
static const uint8_t spec_data_retail_bytes[] = {
#include "tie/spec_data_retail.h"
};

SpecData spec_data[NUM_SPEC_DATA];

/* Full-width name pointers cannot be stored in the packed 32-bit field. */
const char* spec_name_ptrs[NUM_SPEC_DATA];

/* Blob name pointers are invalid host addresses and are replaced during load. */
__attribute__((constructor)) static void spec_data_init_retail(void) {
	_Static_assert(sizeof(spec_data) == sizeof(spec_data_retail_bytes),
				   "spec_data size differs from Z_TIE__.EXE blob");
	memcpy(spec_data, spec_data_retail_bytes, sizeof(spec_data));
}

// FUNCTION: TIE 0x52EA0
uint16_t spec_getspecnum(uint16_t species_idx) { return species_table[species_idx].spec_num; }

/* ===== Public species-name helpers =====================================
 *
 * Names are keyed by SpecData index and use the SpeciesNameId labels from
 * src/tie/string_table_ids.h. Most labels shift down by one because the
 * leading "Pilot" string is not a spec_data row. The unused row 35 and
 * Muurian row 36 follow species_table's actual spec_num mapping rather than
 * the localized string order.
 *
 * Verified by reading SpecData.internal_name at offset +0x2C of each
 * 236-byte row in the retail .data blob (Z_TIE__.EXE @ 0xC7AB4):
 * spec_data[ 0] = XWING, [ 4] = TIEFTR, [ 8] = TIEDLX (TIE Defender),
 * [11] = MISBOAT, [15] = GUNBOAT. */
typedef struct SpeciesNameRow {
	const char* symbolic; /* enum tag after SPECIES_ (e.g. "X_WING") */
	const char* display;  /* display name from STRINGS.DAT */
} SpeciesNameRow;

static const SpeciesNameRow s_species_names[TIE_SPEC_COUNT] = {
	{ "X_WING", "X-wing" },                           /* 0 */
	{ "Y_WING", "Y-wing" },                           /* 1 */
	{ "A_WING", "A-wing" },                           /* 2 */
	{ "B_WING", "B-wing" },                           /* 3 */
	{ "TIE_FIGHTER", "TIE Fighter" },                 /* 4 */
	{ "TIE_INTERCEPTOR", "TIE Interceptor" },         /* 5 */
	{ "TIE_BOMBER", "TIE Bomber" },                   /* 6 */
	{ "TIE_ADVANCED", "TIE Advanced" },               /* 7 */
	{ "TIE_DEFENDER", "TIE Defender" },               /* 8  internal "TIEDLX" */
	{ "EMPTY", "(empty)" },                           /* 9 */
	{ "EMPTY_2", "(empty)" },                         /* 10 */
	{ "MISSILE_BOAT", "Missile Boat" },               /* 11 */
	{ "T_WING", "T-wing" },                           /* 12 */
	{ "Z_95_HEADHUNTER", "Z-95 Headhunter" },         /* 13 */
	{ "R_41_STARCHASER", "R-41 Starchaser" },         /* 14 */
	{ "ASSAULT_GUNBOAT", "Assault Gunboat" },         /* 15 */
	{ "SHUTTLE", "Shuttle" },                         /* 16 */
	{ "ESCORT_SHUTTLE", "Escort Shuttle" },           /* 17 */
	{ "PATROL_CRAFT", "Patrol Craft" },               /* 18 */
	{ "SCOUT_CRAFT", "Scout Craft" },                 /* 19 */
	{ "TRANSPORT", "Transport" },                     /* 20 */
	{ "ASSAULT_TRANSPORT", "Assault Transport" },     /* 21 */
	{ "ESCORT_TRANSPORT", "Escort Transport" },       /* 22 */
	{ "TUG", "Tug" },                                 /* 23 */
	{ "UTILITY_TUG", "Utility Tug" },                 /* 24 */
	{ "CONTAINER_A", "Container A" },                 /* 25 */
	{ "CONTAINER_B", "Container B" },                 /* 26 */
	{ "CONTAINER_C", "Container C" },                 /* 27 */
	{ "CONTAINER_D", "Container D" },                 /* 28 */
	{ "HEAVY_LIFTER", "Heavy Lifter" },               /* 29 */
	{ "EMPTY_3", "(empty)" },                         /* 30 */
	{ "FREIGHTER", "Freighter" },                     /* 31 */
	{ "CARGO_FERRY", "Cargo Ferry" },                 /* 32 */
	{ "MODULAR_CONVEYOR", "Modular Conveyor" },       /* 33 */
	{ "CONTAINER_TRANSPORT", "Container Transport" }, /* 34 */
	{ "EMPTY_4", "(empty)" },                         /* 35 */
	{ "MUURIAN_TRANSPORT", "Muurian Transport" },     /* 36 */
	{ "CORELLIAN_TRANSPORT", "Corellian Transport" }, /* 37 */
	{ "EMPTY_5", "(empty)" },                         /* 38 */
	{ "CORELLIAN_CORVETTE", "Corellian Corvette" },   /* 39 */
	{ "MOD_CORVETTE", "Mod. Corvette" },              /* 40 */
	{ "NEBULON_B_FRIGATE", "Nebulon B Frigate" },     /* 41 */
	{ "NEBULON_B_2_FRIGATE", "Nebulon B-2 Frigate" }, /* 42 */
	{ "C_3_PASSENGER_LINER", "C-3 Passenger Liner" }, /* 43 */
	{ "CARRACK_CRUISER", "Carrack Cruiser" },         /* 44 */
	{ "STRIKE_CRUISER", "Strike Cruiser" },           /* 45 */
	{ "ESCORT_CARRIER", "Escort Carrier" },           /* 46 */
	{ "DREADNAUGHT", "Dreadnaught" },                 /* 47 */
	{ "CALAMARI_CRUISER", "Calamari Cruiser" },       /* 48 */
	{ "LT_CALAMARI_CRUISER", "Lt Calamari Cruiser" }, /* 49 */
	{ "INTERDICTOR", "Interdictor" },                 /* 50 */
	{ "VICTORY_STAR_DEST", "Victory Star Dest" },     /* 51 */
	{ "IMPERIAL_STAR_DEST", "Imperial Star Dest" },   /* 52 */
	{ "SUPERSTAR", "Superstar" },                     /* 53 */
	{ "CONTAINER_E", "Container E" },                 /* 54 */
	{ "CONTAINER_F", "Container F" },                 /* 55 */
	{ "CONTAINER_G", "Container G" },                 /* 56 */
	{ "CONTAINER_H", "Container H" },                 /* 57 */
	{ "CONTAINER_I", "Container I" },                 /* 58 */
	{ "PLATFORM_XQ1", "Platform XQ1" },               /* 59 */
	{ "PLATFORM_XQ2", "Platform XQ2" },               /* 60 */
	{ "PLATFORM_XQ3", "Platform XQ3" },               /* 61 */
	{ "PLATFORM_XQ4", "Platform XQ4" },               /* 62 */
	{ "PLATFORM_XQ5", "Platform XQ5" },               /* 63 */
	{ "PLATFORM_XQ6", "Platform XQ6" },               /* 64 */
	{ "PLATFORM", "Platform" },                       /* 65 */
	{ "PLATFORM_2", "Platform" },                     /* 66 */
	{ "PLATFORM_3", "Platform" },                     /* 67 */
	/* spec_data[68] has no corresponding SpeciesNameId entry — the
	 * auto-gen header's 69 cells start with a "Pilot" cell that isn't
	 * a spec_data row, so the count works out to 68 named slots + one
	 * tail-end empty. Use a sentinel name so the lookup helper still
	 * has a valid pointer for every TIE_SPEC_COUNT index. */
	{ "UNUSED_68", "(unused)" }, /* 68 */
};

/* Projectile species are not part of spec_data and have no spec_num.
 * Keep their established PROJECTILE_N YAML names compatible; compiled
 * code uses the semantic TieSpeciesId constants recovered from the
 * projectile dispatch table and TIE98 asset mapping. */
static const SpeciesNameRow s_projectile_names[TIE_SPECIES_PROJECTILE_COUNT] = {
	{ "PROJECTILE_137", "Projectile 137" }, /* 137 */
	{ "PROJECTILE_138", "Projectile 138" }, /* 138 */
	{ "PROJECTILE_139", "Projectile 139" }, /* 139 */
	{ "PROJECTILE_140", "Laser Green" },    /* 140 */
	{ "PROJECTILE_141", "Projectile 141" }, /* 141 */
	{ "PROJECTILE_142", "Projectile 142" }, /* 142 */
	{ "PROJECTILE_143", "Projectile 143" }, /* 143 torpedo */
	{ "PROJECTILE_144", "Projectile 144" }, /* 144 */
	{ "PROJECTILE_145", "Projectile 145" }, /* 145 (engine aliases mesh of 138) */
	{ "PROJECTILE_146", "Projectile 146" }, /* 146 (aliases 140) */
	{ "PROJECTILE_147", "Projectile 147" }, /* 147 (aliases 142) */
	{ "PROJECTILE_148", "Projectile 148" }, /* 148 (aliases 143) */
	{ "PROJECTILE_149", "Projectile 149" }, /* 149 (aliases 144) */
	{ "PROJECTILE_150", "Projectile 150" }, /* 150 (no mesh in retail) */
	{ "PROJECTILE_151", "Projectile 151" }, /* 151 */
	{ "PROJECTILE_152", "Projectile 152" }, /* 152 */
	{ "PROJECTILE_153", "Projectile 153" }, /* 153 (aliases 152) */
	{ "PROJECTILE_154", "Projectile 154" }, /* 154 (aliases 152) */
};

/* Internal: species_table[i].spec_num lookup with a range guard.
 * Returns UINT16_MAX for out-of-range or for sentinel slots whose
 * spec_num field is 255 (= no spec_data row). */
static uint16_t spec_num_for_species_idx(uint16_t species_idx) {
	if (species_idx >= NUM_SPECIES)
		return UINT16_MAX;
	uint8_t s = species_table[species_idx].spec_num;
	return (s >= TIE_SPEC_COUNT) ? UINT16_MAX : (uint16_t)s;
}

/* Return the first species row for a spec number. */
static uint16_t species_idx_for_spec_num(uint16_t spec_num) {
	if (spec_num >= TIE_SPEC_COUNT)
		return UINT16_MAX;
	for (uint16_t i = 0; i < NUM_SPECIES; ++i) {
		if (species_table[i].spec_num == (uint8_t)spec_num)
			return i;
	}
	return UINT16_MAX;
}

/* Resolve a projectile species_idx to its s_projectile_names[] row. */
static const SpeciesNameRow* projectile_name_row(uint16_t species_idx) {
	if (species_idx < TIE_SPECIES_PROJECTILE_FIRST)
		return NULL;
	uint16_t i = species_idx - TIE_SPECIES_PROJECTILE_FIRST;
	if (i >= TIE_SPECIES_PROJECTILE_COUNT)
		return NULL;
	return &s_projectile_names[i];
}

const char* tie_species_display_name(uint16_t species_idx) {
	uint16_t s = spec_num_for_species_idx(species_idx);
	if (s != UINT16_MAX)
		return s_species_names[s].display;
	const SpeciesNameRow* p = projectile_name_row(species_idx);
	return p ? p->display : "";
}

const char* tie_species_symbolic_name(uint16_t species_idx) {
	uint16_t s = spec_num_for_species_idx(species_idx);
	if (s != UINT16_MAX)
		return s_species_names[s].symbolic;
	const SpeciesNameRow* p = projectile_name_row(species_idx);
	return p ? p->symbolic : "";
}

/* Case-insensitive equality, ignoring all underscores / dashes / spaces.
 * Lets us match "X_WING" ↔ "x-wing" ↔ "X Wing" through one helper. */
static int loose_equal(const char* a, const char* b) {
	while (*a || *b) {
		while (*a == '_' || *a == '-' || *a == ' ')
			++a;
		while (*b == '_' || *b == '-' || *b == ' ')
			++b;
		if (!*a || !*b)
			return *a == *b;
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		++a;
		++b;
	}
	return 1;
}

static int prefix_equal_ignore_case(const char* value, const char* prefix) {
	while (*prefix) {
		if (!*value || tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
			return 0;
		++value;
		++prefix;
	}
	return 1;
}

int TieRecoveredData_SpeciesLookup(const char* yaml_value) {
	if (!yaml_value || !*yaml_value)
		return -1;

	/* Pure-integer fast path: accept "5" → species_idx 5 directly.
	 * Authors who already know the snapshot's ship_idx can paste it
	 * verbatim. The 161-entry range matches species_table[]. */
	const char* p = yaml_value;
	while (*p == ' ' || *p == '\t')
		++p;
	if (*p == '-' || (*p >= '0' && *p <= '9')) {
		char* end = NULL;
		long n = strtol(p, &end, 10);
		if (end && end != p) {
			while (*end == ' ' || *end == '\t')
				++end;
			if (*end == '\0') {
				if (n < 0 || n >= NUM_SPECIES)
					return -1;
				return (int)n;
			}
		}
	}

	/* Symbolic / display-name path: SpeciesNameId tag → spec_num →
	 * species_idx via species_table reverse lookup. The renderer
	 * never sees spec_num — it stays internal to this helper. */
	const char* needle = yaml_value;
	if (prefix_equal_ignore_case(needle, "SPECIES_"))
		needle += 8;

	for (uint16_t s = 0; s < TIE_SPEC_COUNT; ++s) {
		if (loose_equal(needle, s_species_names[s].symbolic) ||
			loose_equal(needle, s_species_names[s].display)) {
			uint16_t idx = species_idx_for_spec_num(s);
			return (idx == UINT16_MAX) ? -1 : (int)idx;
		}
	}

	/* Projectile names are stored in ascending TieSpeciesId order. */
	for (uint16_t i = 0; i < TIE_SPECIES_PROJECTILE_COUNT; ++i) {
		if (loose_equal(needle, s_projectile_names[i].symbolic) ||
			loose_equal(needle, s_projectile_names[i].display)) {
			return (int)(TIE_SPECIES_PROJECTILE_FIRST + i);
		}
	}
	return -1;
}
