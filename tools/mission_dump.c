/* TIE mission dumper. File layout:
 *   [2]                version sentinel: if int16 < 0 keep as version, else
 *                      rewind to 0 and treat as the start of the header
 *   [6]                num_fg, num_msg, num_goals (int16 LE)
 *   [450]              EMissionStruct (mission header)
 *   [num_fg * 292]     EFGStruct (flight groups)
 *   [num_msg * 90]     RadioMsg cues
 *   [num_goals * 28]   EMissionGoal records (cut[])
 *   [810]              briefing event/page-command stream + tag table
 *   [32 entries]       briefing title strings (legacy 40 bytes / modern u16-prefixed)
 *   [32 entries]       briefing caption strings (legacy 160 / modern u16-prefixed)
 *   [20 entries]       briefing event/notice strings (legacy 0 / modern u16-prefixed)
 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================
 *   Little-endian readers
 * ==================================================================== */

static uint16_t TieMissionDump_ReadU16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static int16_t TieMissionDump_ReadI16(const uint8_t* p) { return (int16_t)TieMissionDump_ReadU16(p); }

/* ====================================================================
 *   Lookup tables
 * ==================================================================== */

/* CraftType — file-format ship enum (src/tie/shipext.h CraftType). Names
 * sourced from the unofficial Mission_TIE95.txt format spec. Slots 0x46+
 * tail off into "unused" / capital-ship reservations the engine never
 * spawns at runtime; we still print the numeric code for those. */
static const char* const craft_type_names[] = {
	"None",                     /* 0x00 */
	"X-wing",                   /* 0x01 */
	"Y-wing",                   /* 0x02 */
	"A-wing",                   /* 0x03 */
	"B-wing",                   /* 0x04 */
	"TIE Fighter",              /* 0x05 */
	"TIE Interceptor",          /* 0x06 */
	"TIE Bomber",               /* 0x07 */
	"TIE Advanced",             /* 0x08 */
	"TIE Defender",             /* 0x09 */
	"(craft 0x0A)",             /* 0x0A unused */
	"(craft 0x0B)",             /* 0x0B unused */
	"Missile Boat",             /* 0x0C */
	"(craft 0x0D)",             /* 0x0D unused */
	"(craft 0x0E)",             /* 0x0E unused */
	"(craft 0x0F)",             /* 0x0F unused */
	"Assault Gunboat",          /* 0x10 */
	"Shuttle",                  /* 0x11 */
	"Escort Shuttle",           /* 0x12 */
	"System Patrol Craft",      /* 0x13 */
	"Scout Craft",              /* 0x14 */
	"Transport",                /* 0x15 */
	"Assault Transport",        /* 0x16 */
	"Escort Transport",         /* 0x17 */
	"Tug",                      /* 0x18 */
	"Combat Utility Vehicle",   /* 0x19 */
	"Container A",              /* 0x1A */
	"Container B",              /* 0x1B */
	"Container C",              /* 0x1C */
	"Container D",              /* 0x1D */
	"Heavy Lifter",             /* 0x1E */
	"(craft 0x1F)",             /* 0x1F */
	"Bulk Freighter",           /* 0x20 */
	"Cargo Ferry",              /* 0x21 */
	"Modular Conveyor",         /* 0x22 */
	"Container Transport",      /* 0x23 */
	"Medium Transport",         /* 0x24 */
	"Muurian Transport",        /* 0x25 */
	"Corellian Transport",      /* 0x26 */
	"(craft 0x27)",             /* 0x27 */
	"Corellian Corvette",       /* 0x28 */
	"Modified Corvette",        /* 0x29 */
	"Nebulon-B Frigate",        /* 0x2A */
	"Modified Frigate",         /* 0x2B */
	"C-3 Passenger Liner",      /* 0x2C */
	"Carrack Cruiser",          /* 0x2D */
	"Strike Cruiser",           /* 0x2E */
	"Escort Carrier",           /* 0x2F */
	"Dreadnaught",              /* 0x30 */
	"Mon Calamari Cruiser",     /* 0x31 */
	"Interdictor Cruiser",      /* 0x32 */
	"Victory Star Destroyer",   /* 0x33 */
	"Imperial Star Destroyer",  /* 0x34 */
	"Container E",              /* 0x35 */
	"Container F",              /* 0x36 */
	"Container G",              /* 0x37 */
	"Container H",              /* 0x38 */
	"Container I",              /* 0x39 */
	"(craft 0x3A)",             /* 0x3A */
	"(craft 0x3B)",             /* 0x3B */
	"Platform A",               /* 0x3C */
	"Platform B",               /* 0x3D */
	"Platform C",               /* 0x3E */
	"Platform D",               /* 0x3F */
	"Platform E",               /* 0x40 */
	"Platform F",               /* 0x41 */
	"Asteroid R&D Facility",    /* 0x42 */
	"Asteroid Laser Battery",   /* 0x43 */
	"Asteroid Warhead Battery", /* 0x44 */
	"X/7 Factory",              /* 0x45 */
};
#define CRAFT_TYPE_COUNT (sizeof(craft_type_names) / sizeof(craft_type_names[0]))

/* IFF allegiance (EFGStruct.side). Mission_TIE95 documents 0..3 named
 * sides; 4..7 take their names from EMissionStruct.neutral_name[] but we
 * only know the numeric slot here. */
static const char* const side_names[] = {
	"Rebel",    /* 0 */
	"Imperial", /* 1 */
	"Neutral",  /* 2 */
	"Other",    /* 3 */
};

/* AI skill (EFGStruct.skill). */
static const char* const skill_names[] = {
	"Rookie", "Officer", "Veteran", "Ace", "Top Ace", "Random",
};

/* Status / loadout variant (EFGStruct.version). The slot also doubles as
 * a starting-state byte for capital ships in Mission_TIE95. */
static const char* const status_names[] = {
	"Normal",      "2x Warheads", "1/2 Warheads",  "Disabled", "Hyper-buoy", "Half Shields",
	"1/4 Shields", "No Lasers",   "No Hyperdrive", "(0x9)",    "(0xA)",      "(0xB)",
};

/* Warhead loadout (EFGStruct.warhead). 0 means "default for craft". */
static const char* const warhead_names[] = {
	"None",       "Concussion Missiles",      "Proton Torpedoes",      "Heavy Rockets",
	"Space Bomb", "Adv. Concussion Missiles", "Adv. Proton Torpedoes", "Mag Pulse",
};

/* Beam weapon (EFGStruct.beam). */
static const char* const beam_names[] = {
	"None",
	"Tractor Beam",
	"Jamming Beam",
};

/* Markings color (EFGStruct.camoflage). */
static const char* const markings_names[] = {
	"Red",
	"Gold",
	"Blue",
	"Green",
};

/* Formation (EFGStruct.formation). */
static const char* const formation_names[] = {
	"Vic",           "Finger Four", "Line Astern", "Line Abreast", "Echelon Right", "Echelon Left",
	"Double Astern", "Diamond",     "Stacked",     "Spread",       "Hi-Lo",         "Spiral",
};

/* Win/loss/bonus condition codes (ECondStruct.cond). Mapping derived
 * from where each fgstatus.cond[N].detail bucket is bumped in the
 * engine (collide.c, paiman.c, create.c, score_craftexitscoring) and
 * which bucket score_checkcondition reads per cond code. NOT the
 * Mission_TIE95.txt off-by-one mapping — that one mislabels several
 * codes; verified live by the in-game radio-msg fire on B1M1FM.TIE
 * "Warning! Illegal cargo" (cond=5 fires on inspection of Onece 3). */
static const char* const cond_names[] = {
	"always true",                   /*  0 */
	"arrived",                       /*  1 cond[0].detail (spawn)             */
	"destroyed",                     /*  2 cond[1].count (exit_kind=2)        */
	"attacked",                      /*  3 cond[2].detail (was_hit_flag)      */
	"captured",                      /*  4 cond[3].detail (paiman case 0x1F)  */
	"inspected",                     /*  5 cond[4].detail (proximity / board) */
	"boarded",                       /*  6 cond[5].detail (board_count 0->1)  */
	"docked",                        /*  7 cond[6].detail (capture_count 0->1)*/
	"disabled",                      /*  8 cond[7].detail (systems-down)      */
	"identified/survived",           /*  9 cond[0].detail - cond[1].count     */
	"always false",                  /* 10 */
	"resistance",                    /* 11 (no per-FG accumulation)           */
	"come and go",                   /* 12 cond[1].detail + cond[2].count     */
	"unused (13)",                   /* 13 */
	"primary objectives complete",   /* 14 mission-level                      */
	"primary objectives failed",     /* 15 */
	"secondary objectives complete", /* 16 */
	"secondary objectives failed",   /* 17 */
	"bonus objectives complete",     /* 18 */
	"bonus objectives failed",       /* 19 */
	"reinforced by",                 /* 20 */
	"alive (live predicate)",        /* 21 */
	"present (live predicate)",      /* 22 */
	"FG idle (live predicate)",      /* 23 */
	"witnessed event",               /* 24 */
	"identified",                    /* 25 */
};

/* GoalTargetType — what the cond is matched against (ECondStruct.type).
 * Mirrors src/tie/score.h GoalTargetType_tag. */
static const char* const target_type_names[] = {
	"flight group",      /* 0 (placeholder; engine starts at 1) */
	"flight group",      /* 1 GTT_FG */
	"species",           /* 2 GTT_SPECIES */
	"genus",             /* 3 GTT_GENUS */
	"family",            /* 4 GTT_FAMILY */
	"side (IFF)",        /* 5 GTT_SIDE */
	"AI order",          /* 6 GTT_AI_ORDER */
	"craft attribute",   /* 7 GTT_CRAFT_ATTR */
	"all in set",        /* 8 GTT_ALL_IN_SET */
	"skill",             /* 9 GTT_SKILL */
	"version (loadout)", /* 10 GTT_VERSION */
	"all flight groups", /* 11 GTT_ALL_FG */
};

/* Quantifier (ECondStruct.pct). Drives "how many" out of the matched set. */
static const char* const amount_op_names[] = {
	"100%",                  /*  0 all matched */
	"75%",                   /*  1 */
	"50%",                   /*  2 */
	"25%",                   /*  3 */
	"any",                   /*  4 */
	"all but one",           /*  5 */
	"specific craft",        /*  6 */
	"all except 'waiting'",  /*  7 */
	"all except player",     /*  8 */
	"player FG",             /*  9 */
	"100% destroyed",        /* 10 */
	"75% destroyed",         /* 11 */
	"50% destroyed",         /* 12 */
	"25% destroyed",         /* 13 */
	"any destroyed",         /* 14 */
	"all but one destroyed", /* 15 */
};

/* AI orders (EAIStruct.order). 0..32 valid; ordersldr/flw map to leader/
 * follower behaviour bytes. Names are TIE95-format conventional. */
static const char* const ai_order_names[] = {
	"Hold Steady",                  /*  0 */
	"Go Home",                      /*  1 */
	"Circle and Ignore",            /*  2 */
	"Fly Once and Ignore",          /*  3 */
	"Circle and Evade",             /*  4 */
	"Fly Once and Evade",           /*  5 */
	"Close Escort",                 /*  6 */
	"Loose Escort",                 /*  7 */
	"Trail Target",                 /*  8 */
	"Intercept",                    /*  9 */
	"Rendezvous",                   /* 10 */
	"Disabled",                     /* 11 */
	"Board to Deliver",             /* 12 */
	"Board to Take",                /* 13 */
	"Board to Exchange",            /* 14 */
	"Board to Capture",             /* 15 */
	"Board to Destroy",             /* 16 */
	"Pick Up",                      /* 17 */
	"Drop Off",                     /* 18 */
	"Wait",                         /* 19 */
	"SS Wait",                      /* 20 */
	"SS Patrol Loop",               /* 21 */
	"SS Await Return",              /* 22 */
	"SS Await Launch",              /* 23 */
	"SS Await Boarding",            /* 24 */
	"Attack Targets",               /* 25 */
	"Attack Escorts",               /* 26 */
	"Attack Attackers",             /* 27 */
	"Attack 'No-Escort' Ships",     /* 28 */
	"Attack Freighters/Transports", /* 29 */
	"Attack Capital Ships",         /* 30 */
	"Attack Fighters",              /* 31 */
	"Rescue From",                  /* 32 */
};

/* Throttle (EAIStruct.speed). Mission_TIE95 names the slots as throttle
 * commands; values 0x0A and beyond shift into per-second / per-minute
 * timer encodings the engine decodes through throttleconvert. */
static const char* const throttle_names[] = {
	"Throttle 100%", "Throttle 75%", "Throttle 50%", "Throttle 25%", "Throttle Full Stop",
	"Match Target",  "Half Target",  "Pursue",       "Pursue Hard",  "Engage Aft",
};

/* Briefing page-command opcodes. Mirrors BriefCmd in src/tie/player.h
 * and the param-count table map_cmd_size in src/tie/player.c. Entries 35
 * and 36 are sentinels. */
static const char* const brief_op_names[] = {
	"NoOp",            /*  0 */
	"Seek",            /*  1 */
	"(unused 2)",      /*  2 */
	"ClearParagraphs", /*  3 */
	"ShowParagraph0",  /*  4 */
	"ShowParagraph1",  /*  5 */
	"MapCenter",       /*  6 (target_x, target_y) */
	"MapZoom",         /*  7 (scale_x, scale_y) */
	"ClearTargets",    /*  8 */
	"ShowTarget0",
	"ShowTarget1", /*  9, 10 */
	"ShowTarget2",
	"ShowTarget3", /* 11, 12 */
	"ShowTarget4",
	"ShowTarget5", /* 13, 14 */
	"ShowTarget6",
	"ShowTarget7", /* 15, 16 */
	"ClearText",   /* 17 */
	"ShowText0",
	"ShowText1", /* 18, 19 (text_id, x, y, color) */
	"ShowText2",
	"ShowText3", /* 20, 21 */
	"ShowText4",
	"ShowText5", /* 22, 23 */
	"ShowText6",
	"ShowText7", /* 24, 25 */
	"(unused 26)",
	"(unused 27)", /* 26, 27 */
	"(unused 28)",
	"(unused 29)", /* 28, 29 */
	"(unused 30)",
	"(unused 31)", /* 30, 31 */
	"(unused 32)",
	"(unused 33)", /* 32, 33 */
	"EndPage",     /* 34 */
};

/* Param count per briefing opcode (mirrors map_cmd_size in player.c). */
static const uint8_t brief_op_argc[] = {
	0, 0, 1, 0, 1, 1, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* ====================================================================
 *   Pretty printers
 * ==================================================================== */

static const char* TieMissionDump_Lookup(const char* const* table, size_t count, unsigned idx,
										 const char* fallback) {
	return (idx < count) ? table[idx] : fallback;
}

/* Print a NUL-padded fixed-length string field, escaping non-print bytes. */
static void TieMissionDump_PrintFixedStr(const char* label, const uint8_t* buf, size_t n) {
	printf("%s\"", label);
	for (size_t i = 0; i < n && buf[i]; i++) {
		uint8_t c = buf[i];
		if (c == '\\' || c == '"')
			printf("\\%c", c);
		else if (c == '\n')
			printf("\\n");
		else if (c == '\r')
			printf("\\r");
		else if (isprint(c))
			putchar(c);
		else
			printf("\\x%02x", c);
	}
	printf("\"\n");
}

static void TieMissionDump_PrintCond(const char* label, uint8_t cond, uint8_t type, uint8_t id, uint8_t pct) {
	printf(
		"%scond=%u (%s)  type=%u (%s)  id=%u  amount=%u (%s)\n", label, cond,
		TieMissionDump_Lookup(cond_names, sizeof(cond_names) / sizeof(*cond_names), cond, "?"), type,
		TieMissionDump_Lookup(target_type_names, sizeof(target_type_names) / sizeof(*target_type_names), type,
							  "?"),
		id, pct,
		TieMissionDump_Lookup(amount_op_names, sizeof(amount_op_names) / sizeof(*amount_op_names), pct, "?"));
}

/* ====================================================================
 *   Section dumpers
 * ==================================================================== */

static void TieMissionDump_DumpMissionHeader(const uint8_t* p, int16_t version, int16_t num_fg,
											 int16_t num_msg, int16_t num_goals) {
	printf("=== Mission Header (EMissionStruct, 450 bytes) ===\n");
	printf("Version:                  %d (0x%04x)\n", (int)version, (uint16_t)version);
	printf("Flight groups:            %d\n", num_fg);
	printf("Radio messages:           %d\n", num_msg);
	printf("Goals (cut[]):            %d\n", num_goals);
	printf("\n");
	printf("Time limit:               %u min %u sec\n", p[0x00], p[0x01]);
	printf("Win type:                 %u  (1=any, 2=officer, 3=secret)\n", p[0x02]);
	printf("Backdrop seed:            %d\n", (int8_t)p[0x03]);
	printf("Rescue:                   %u\n", p[0x04]);
	printf("All waypoints shown:      %u\n", p[0x05]);
	printf("Mission variables:        ");
	for (int i = 0; i < 8; i++)
		printf("%02x ", p[0x06 + i]);
	printf("\n");
	printf("Win bonus:                officer=%d  secret=%d\n", (int8_t)p[0x0E], (int8_t)p[0x0F]);
	TieMissionDump_PrintFixedStr("Win msg 1 (officer):     ", &p[0x10], 64);
	TieMissionDump_PrintFixedStr("Win msg 1 (secret):      ", &p[0x50], 64);
	TieMissionDump_PrintFixedStr("Win msg 2 (officer):     ", &p[0x90], 64);
	TieMissionDump_PrintFixedStr("Win msg 2 (secret):      ", &p[0xD0], 64);
	TieMissionDump_PrintFixedStr("Loss msg (officer):      ", &p[0x110], 64);
	TieMissionDump_PrintFixedStr("Loss msg (secret):       ", &p[0x150], 64);
	printf("Loss msg delay:           %u\n", p[0x190]);
	for (int i = 0; i < 4; i++) {
		char lbl[32];
		snprintf(lbl, sizeof(lbl), "IFF %d name:               ", 3 + i);
		TieMissionDump_PrintFixedStr(lbl, &p[0x192 + i * 12], 12);
	}
}

static void TieMissionDump_DumpFlightGroup(const uint8_t* p, int idx) {
	printf("=== Flight Group %d (292 bytes) ===\n", idx);
	TieMissionDump_PrintFixedStr("  Name:                ", &p[0x00], 12);
	TieMissionDump_PrintFixedStr("  Commander:           ", &p[0x0C], 12);
	TieMissionDump_PrintFixedStr("  Cargo:               ", &p[0x18], 12);
	TieMissionDump_PrintFixedStr("  Special cargo:       ", &p[0x24], 12);
	printf("  Special craft slot:    %u\n", p[0x30]);
	printf("  Special flag:          %u\n", p[0x31]);
	uint8_t species = p[0x32];
	printf("  Species (CraftType):   %u (%s)\n", species,
		   TieMissionDump_Lookup(craft_type_names, CRAFT_TYPE_COUNT, species, "(unknown)"));
	printf("  Count:                 %u\n", p[0x33]);
	printf("  Status:                %u (%s)\n", p[0x34],
		   TieMissionDump_Lookup(status_names, sizeof(status_names) / sizeof(*status_names), p[0x34], "?"));
	printf(
		"  Warhead:               %u (%s)\n", p[0x35],
		TieMissionDump_Lookup(warhead_names, sizeof(warhead_names) / sizeof(*warhead_names), p[0x35], "?"));
	printf("  Beam:                  %u (%s)\n", p[0x36],
		   TieMissionDump_Lookup(beam_names, sizeof(beam_names) / sizeof(*beam_names), p[0x36], "?"));
	printf("  Side (IFF):            %u (%s)\n", p[0x37],
		   TieMissionDump_Lookup(side_names, sizeof(side_names) / sizeof(*side_names), p[0x37],
								 "neutral_name[]"));
	printf("  Skill:                 %u (%s)\n", p[0x38],
		   TieMissionDump_Lookup(skill_names, sizeof(skill_names) / sizeof(*skill_names), p[0x38], "?"));
	printf("  Markings:              %u (%s)\n", p[0x39],
		   TieMissionDump_Lookup(markings_names, sizeof(markings_names) / sizeof(*markings_names), p[0x39],
								 "?"));
	printf("  Player obey-orders:    %u\n", p[0x3A]);
	printf("  Formation:             %u (%s)\n", p[0x3C],
		   TieMissionDump_Lookup(formation_names, sizeof(formation_names) / sizeof(*formation_names), p[0x3C],
								 "?"));
	printf("  Formation spacing:     %u\n", p[0x3D]);
	printf("  Global group:          %u\n", p[0x3E]);
	printf("  Waves (additional):    %u  delay=%u\n", p[0x40], p[0x41]);
	printf("  Player flag:           %u (1-indexed; 0=AI)\n", p[0x42]);
	printf("  Heading/Pitch/Roll:    %u / %u / %u\n", p[0x43], p[0x44], p[0x45]);
	printf("  Permadeath:            link_flag=%u  link_code=%u\n", p[0x46], p[0x47]);
	printf("  Difficulty:            %u\n", p[0x49]);

	printf("  Arrival cond [0]:      ");
	TieMissionDump_PrintCond("", p[0x4A], p[0x4B], p[0x4C], p[0x4D]);
	printf("  Arrival cond [1]:      ");
	TieMissionDump_PrintCond("", p[0x4E], p[0x4F], p[0x50], p[0x51]);
	printf("  Arrival op:            %u (%s)\n", p[0x52], p[0x52] == 1 ? "OR" : "AND");
	printf("  Arrival delay:         %u min %u sec\n", p[0x54], p[0x55]);

	printf("  Departure cond:        ");
	TieMissionDump_PrintCond("", p[0x56], p[0x57], p[0x58], p[0x59]);
	printf("  Departure timer:       %u min %u sec   abort=%u\n", p[0x5A], p[0x5B], p[0x5C]);
	printf("  Cur start FG (editor): %d\n", (int16_t)TieMissionDump_ReadI16(&p[0x5E]));
	printf("  Mothership arrive:     fg=%u  used=%u\n", p[0x60], p[0x61]);
	printf("  Mothership depart 1:   fg=%u  used=%u\n", p[0x62], p[0x63]);
	printf("  Mothership depart 2:   fg=%u  used=%u\n", p[0x64], p[0x65]);
	printf("  Mothership capture:    fg=%u  used=%u\n", p[0x66], p[0x67]);

	for (int i = 0; i < 3; i++) {
		const uint8_t* a = &p[0x68 + i * 18];
		printf("  AI order [%d]:          %u (%s)  speed=%u (%s)\n", i, a[0],
			   TieMissionDump_Lookup(ai_order_names, sizeof(ai_order_names) / sizeof(*ai_order_names), a[0],
									 "?"),
			   a[1],
			   TieMissionDump_Lookup(throttle_names, sizeof(throttle_names) / sizeof(*throttle_names), a[1],
									 "(timer/raw)"));
		printf("    var:                 %02x %02x %02x %02x\n", a[2], a[3], a[4], a[5]);
		printf("    target1:             type=%u id=%u\n", a[6], a[8]);
		printf("    target2:             type=%u id=%u  op=%s\n", a[7], a[9], a[10] == 1 ? "OR" : "AND");
		printf("    target3:             type=%u id=%u\n", a[12], a[13]);
		printf("    target4:             type=%u id=%u  op=%s\n", a[14], a[15], a[16] == 1 ? "OR" : "AND");
	}

	printf("  Primary win:           ");
	TieMissionDump_PrintCond("", p[0x9E], 0, 0, p[0x9F]);
	printf("  Secondary win:         ");
	TieMissionDump_PrintCond("", p[0xA0], 0, 0, p[0xA1]);
	printf("  Loss:                  ");
	TieMissionDump_PrintCond("", p[0xA2], 0, 0, p[0xA3]);
	printf("  Bonus:                 ");
	TieMissionDump_PrintCond("", p[0xA4], 0, 0, p[0xA5]);
	printf("  Bonus points:          %d\n", (int8_t)p[0xA6]);

	printf("  Waypoints (15):\n");
	const uint8_t* wx = &p[0xA8];
	const uint8_t* wy = &p[0xC6];
	const uint8_t* wz = &p[0xE4];
	const uint8_t* wu = &p[0x102];
	static const char* wp_role[8] = {
		"start1", "start2", "start3", "start4", "hyp", "rdvz", "brief", "?",
	};
	for (int i = 0; i < 15; i++) {
		int16_t used = TieMissionDump_ReadI16(&wu[i * 2]);
		if (!used)
			continue;
		const char* role = (i < 8) ? wp_role[i] : (i == 14 ? "brief-final" : "extra");
		printf("    WP[%2d %-10s] x=%6d y=%6d z=%6d  used=%d\n", i, role, TieMissionDump_ReadI16(&wx[i * 2]),
			   TieMissionDump_ReadI16(&wy[i * 2]), TieMissionDump_ReadI16(&wz[i * 2]), used);
	}
	printf("  Waypoints shown flag:  %u\n", p[0x120]);
	printf("  Brief link:            %u  shown=%u\n", p[0x122], p[0x123]);
}

static void TieMissionDump_DumpRadioMsg(const uint8_t* p, int idx) {
	printf("=== Radio Message %d (90 bytes) ===\n", idx);
	TieMissionDump_PrintFixedStr("  Text:                ", &p[0x00], 64);
	printf("  Trigger cond [0]:      ");
	TieMissionDump_PrintCond("", p[0x40], p[0x41], p[0x42], p[0x43]);
	printf("  Trigger cond [1]:      ");
	TieMissionDump_PrintCond("", p[0x44], p[0x45], p[0x46], p[0x47]);
	printf("  Editor block:          ");
	for (int i = 0x48; i < 0x58; i++)
		printf("%02x ", p[i]);
	printf("\n");
	printf("  Countdown delay:       %u\n", p[0x58]);
	printf("  Subcond join:          %s\n", p[0x59] == 1 ? "OR" : "AND");
}

static void TieMissionDump_DumpGoal(const uint8_t* p, int idx) {
	const char* role = (idx == 0)   ? "primary"
					   : (idx == 1) ? "secondary"
					   : (idx == 2) ? "bonus"
									: "extra/unused";
	printf("=== Goal %d (%s) (28 bytes) ===\n", idx, role);
	printf("  Subcond [0]:           ");
	TieMissionDump_PrintCond("", p[0x00], p[0x01], p[0x02], p[0x03]);
	printf("  Subcond [1]:           ");
	TieMissionDump_PrintCond("", p[0x04], p[0x05], p[0x06], p[0x07]);
	TieMissionDump_PrintFixedStr("  Editor name:         ", &p[0x08], 17);
	printf("  Subcond join:          %s\n", p[0x19] == 1 ? "OR" : "AND");
	printf("  Pad:                   %02x %02x\n", p[0x1A], p[0x1B]);
}

/* ====================================================================
 *   Briefing stream
 * ==================================================================== */

/* Briefing block layout (EBriefPage in src/tie/player.h, 810 bytes):
 *   +0x00  len     i16  total length (page-end tick)
 *   +0x02  time    i16  current time cursor
 *   +0x04  index   i16  current command index
 *   +0x06  size    i16  page data size
 *   +0x08  tile    i16
 *   +0x0A  commands[400]  page-command stream (time, opcode, args...) */
static void TieMissionDump_DumpBriefingBlock(const uint8_t* blk) {
	printf("=== Briefing Block (810 bytes, EBriefPage) ===\n");
	printf("  len:                   %d\n", TieMissionDump_ReadI16(&blk[0x00]));
	printf("  time:                  %d\n", TieMissionDump_ReadI16(&blk[0x02]));
	printf("  index:                 %d\n", TieMissionDump_ReadI16(&blk[0x04]));
	printf("  size:                  %d\n", TieMissionDump_ReadI16(&blk[0x06]));
	printf("  tile:                  %d\n", TieMissionDump_ReadI16(&blk[0x08]));
	printf("  Commands:\n");

	const uint8_t* evt = &blk[0x0A];
	const size_t evt_words = 400;
	size_t i = 0;
	while (i + 1 < evt_words) {
		uint16_t time = TieMissionDump_ReadU16(&evt[i * 2]);
		uint16_t opcode = TieMissionDump_ReadU16(&evt[(i + 1) * 2]);
		i += 2;

		const char* opname = TieMissionDump_Lookup(
			brief_op_names, sizeof(brief_op_names) / sizeof(*brief_op_names), opcode, "?");
		uint8_t argc = (opcode < sizeof(brief_op_argc) / sizeof(*brief_op_argc)) ? brief_op_argc[opcode] : 0;
		printf("    [t=%5u] op=%2u (%-16s) args:", time, opcode, opname);
		for (uint8_t k = 0; k < argc && i < evt_words; k++) {
			printf(" %d", TieMissionDump_ReadI16(&evt[i * 2]));
			i++;
		}
		printf("\n");

		/* EndPage is the canonical terminator; treat (0,0) padding as
		 * a soft end too once we've consumed at least one real command. */
		if (opcode == 34 /* BCMD_END_PAGE */)
			break;
		if (time == 0 && opcode == 0)
			break;
	}
}

/* Briefing strings: 32 titles + 32 captions + 20 events. In a modern
 * file (version word & 0x8000 set), every string is u16-length prefixed.
 * In a legacy file the strings are fixed-size (40 / 160 / -). */
static int TieMissionDump_DumpBriefingStrings(FILE* f, int modern, const char* label, int count,
											  int legacy_size) {
	printf("=== Briefing %s (%d entries) ===\n", label, count);
	for (int i = 0; i < count; i++) {
		uint8_t hdr[2];
		uint16_t len;
		if (modern) {
			if (fread(hdr, 1, 2, f) != 2) {
				printf("  [%d] (truncated header)\n", i);
				return 0;
			}
			len = TieMissionDump_ReadU16(hdr);
		} else {
			len = (uint16_t)legacy_size;
		}
		if (len == 0) {
			printf("  [%2d] (empty)\n", i);
			continue;
		}
		uint8_t* buf = (uint8_t*)malloc(len + 1);
		if (!buf) {
			perror("malloc");
			return 0;
		}
		size_t got = fread(buf, 1, len, f);
		if (got != len) {
			printf("  [%2d] (truncated body, got %zu of %u)\n", i, got, len);
			free(buf);
			return 0;
		}
		buf[len] = '\0';
		char ilbl[32];
		snprintf(ilbl, sizeof(ilbl), "  [%2d] (%4u) ", i, len);
		TieMissionDump_PrintFixedStr(ilbl, buf, len);
		free(buf);
	}
	return 1;
}

/* ====================================================================
 *   File reader
 * ==================================================================== */

static int TieMissionDump_ReadBlock(FILE* f, void* buf, size_t n, const char* what) {
	size_t got = fread(buf, 1, n, f);
	if (got != n) {
		fprintf(stderr, "read error on %s: wanted %zu bytes, got %zu\n", what, n, got);
		return 0;
	}
	return 1;
}

static int TieMissionDump_DumpMissionFile(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
		return 0;
	}

	printf("# Mission file: %s\n\n", path);

	/* CREATE_loadmission: read 2 bytes; if the int16 is > 0 the file has
	 * no version prefix and we rewind to 0. Otherwise (negative or zero)
	 * the file starts with a version sentinel. */
	uint8_t verbuf[2];
	if (!TieMissionDump_ReadBlock(f, verbuf, 2, "version word"))
		goto fail;
	int16_t version = TieMissionDump_ReadI16(verbuf);
	if (version > 0) {
		fseek(f, 0, SEEK_SET);
		version = 0; /* legacy: header starts here */
	}
	int modern = (uint16_t)version & 0x8000;

	uint8_t mfh_buf[456];
	if (!TieMissionDump_ReadBlock(f, mfh_buf, 456, "MissionFile header"))
		goto fail;
	int16_t num_fg = TieMissionDump_ReadI16(&mfh_buf[0]);
	int16_t num_msg = TieMissionDump_ReadI16(&mfh_buf[2]);
	int16_t num_goals = TieMissionDump_ReadI16(&mfh_buf[4]);

	if (num_fg < 0 || num_fg > 48) {
		fprintf(stderr, "implausible num_fg=%d (clamping to 48)\n", num_fg);
		num_fg = (num_fg > 48) ? 48 : 0;
	}
	if (num_msg < 0 || num_msg > 16) {
		fprintf(stderr, "implausible num_msg=%d (clamping to 16)\n", num_msg);
		num_msg = (num_msg > 16) ? 16 : 0;
	}
	if (num_goals < 0 || num_goals > 4) {
		fprintf(stderr, "implausible num_goals=%d (clamping to 4)\n", num_goals);
		num_goals = (num_goals > 4) ? 4 : 0;
	}

	TieMissionDump_DumpMissionHeader(&mfh_buf[6], version, num_fg, num_msg, num_goals);
	printf("\n");

	for (int i = 0; i < num_fg; i++) {
		uint8_t fg_buf[292];
		if (!TieMissionDump_ReadBlock(f, fg_buf, 292, "EFGStruct"))
			goto fail;
		TieMissionDump_DumpFlightGroup(fg_buf, i);
		printf("\n");
	}

	for (int i = 0; i < num_msg; i++) {
		uint8_t msg_buf[90];
		if (!TieMissionDump_ReadBlock(f, msg_buf, 90, "RadioMsg"))
			goto fail;
		TieMissionDump_DumpRadioMsg(msg_buf, i);
		printf("\n");
	}

	for (int i = 0; i < num_goals; i++) {
		uint8_t goal_buf[28];
		if (!TieMissionDump_ReadBlock(f, goal_buf, 28, "EMissionGoal"))
			goto fail;
		TieMissionDump_DumpGoal(goal_buf, i);
		printf("\n");
	}

	/* Briefing data only present in retail builds — peek for EOF. */
	long brief_start = ftell(f);
	uint8_t brief_blk[810];
	size_t got = fread(brief_blk, 1, 810, f);
	if (got == 0) {
		printf("# (no briefing data)\n");
		fclose(f);
		return 1;
	}
	if (got != 810) {
		fprintf(stderr,
				"warning: briefing block truncated at offset %ld "
				"(got %zu of 810)\n",
				brief_start, got);
		fclose(f);
		return 1;
	}
	TieMissionDump_DumpBriefingBlock(brief_blk);
	printf("\n");

	/* Three string tables follow the EBriefPage block. Their semantics
	 * (text_data / para_data / talk_data) come from EBriefStruct in
	 * src/tie/player.h; see PLAYER_Load_Display_Map for the load loop. */
	if (!TieMissionDump_DumpBriefingStrings(f, modern, "Tag Labels (text_data, 32)", 32, 40))
		goto done;
	printf("\n");
	if (!TieMissionDump_DumpBriefingStrings(f, modern, "Paragraphs (para_data, 32)", 32, 160))
		goto done;
	printf("\n");
	if (!TieMissionDump_DumpBriefingStrings(f, modern, "Talk Pages (talk_data, 20)", 20, 0))
		goto done;
	printf("\n");

	long pos = ftell(f);
	fseek(f, 0, SEEK_END);
	long end = ftell(f);
	if (pos < end) {
		printf("# %ld trailing bytes after parsed sections (offset %ld..%ld)\n", end - pos, pos, end);
	}

done:
	fclose(f);
	return 1;
fail:
	fclose(f);
	return 0;
}

int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <mission.TIE>\n", argv[0]);
		return 2;
	}
	return TieMissionDump_DumpMissionFile(argv[1]) ? 0 : 1;
}
