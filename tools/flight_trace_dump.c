#include "tie_runtime/diagnostics/flight_trace_format.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBJECT_REF_LIMIT 0x3840u

enum TraceObjectGenus {
	TRACE_GENUS_PROJECTILE_PLAYER = 6,
	TRACE_GENUS_PROJECTILE_NPC = 7,
	TRACE_GENUS_DEBRIS = 11,
	TRACE_GENUS_EXPLOSION = 13,
};

typedef struct TraceFile {
	uint8_t* data;
	size_t size;
	uint32_t used;
	uint16_t header_size;
	char fg_names[48][TIE_FLIGHT_TRACE_FG_NAME_SIZE + 1];
} TraceFile;

typedef struct Options {
	const char* path;
	const char* diff_path;
	const char* fg_filter;
	int object_filter;
	int around_frame;
	int radius;
	int jsonl;
	int show_frames;
	int show_ai_scheduler;
	int show_transient_objects;
} Options;

static uint16_t get_u16(const uint8_t* src) { return (uint16_t)(src[0] | ((uint16_t)src[1] << 8)); }

static int16_t get_i16(const uint8_t* src) { return (int16_t)get_u16(src); }

static uint32_t get_u32(const uint8_t* src) {
	return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
		   ((uint32_t)src[3] << 24);
}

static int32_t get_i32(const uint8_t* src) { return (int32_t)get_u32(src); }

static uint64_t get_u64(const uint8_t* src) {
	return (uint64_t)get_u32(src) | ((uint64_t)get_u32(src + 4) << 32);
}

static const char* record_name(uint16_t type) {
	static const char* names[] = {"invalid", "fg_def", "frame", "spawn", "removed", "ai",
								  "board", "weapon", "collision", "damage", "death", "explosion",
								  "fg_state", "mission_state", "target_change", "overflow", "fg_exit"};
	return type < sizeof names / sizeof names[0] ? names[type] : "unknown";
}

static const char* phase_name(uint16_t phase) {
	static const char* names[] = {"setup", "time", "fg_status", "ai", "weapons", "dynamics",
								  "render", "collision", "move", "animation", "objectives", "end_frame"};
	return phase < sizeof names / sizeof names[0] ? names[phase] : "unknown";
}

static const char* board_name(uint8_t kind) {
	static const char* names[] = {"unknown", "approach", "aligning", "docked", "transfer",
								  "captured", "departing", "complete"};
	return kind < sizeof names / sizeof names[0] ? names[kind] : "unknown";
}

static const char* cause_name(uint8_t cause) {
	static const char* names[] = {"unknown", "craft_collision", "projectile", "static_collision",
								  "ejected", "scripted"};
	return cause < sizeof names / sizeof names[0] ? names[cause] : "unknown";
}

static const char* collision_name(uint8_t kind) {
	static const char* names[] = {"unknown", "craft", "projectile", "static"};
	return kind < sizeof names / sizeof names[0] ? names[kind] : "unknown";
}

static const char* exit_name(uint16_t kind) {
	switch (kind) {
		case TIE_TRACE_EXIT_DESTROYED:
			return "destroyed";
		case TIE_TRACE_EXIT_HYPERSPACE:
			return "hyperspace";
		case TIE_TRACE_EXIT_ENTERED_HANGAR:
			return "hangar";
		case TIE_TRACE_EXIT_PASSENGER:
			return "passenger";
		case TIE_TRACE_EXIT_SCRIPTED:
			return "scripted";
		default:
			return "unknown";
	}
}

static const char* removal_name(uint16_t kind) {
	switch (kind) {
		case TIE_TRACE_REMOVAL_CLEARED:
			return "cleared";
		case TIE_TRACE_REMOVAL_REPLACED:
			return "replaced";
		default:
			return "unknown";
	}
}

static const char* termination_name(uint32_t flags) {
	if (flags & TIE_TRACE_HEADER_FLAG_APPLICATION_SHUTDOWN)
		return "application_shutdown";
	if (flags & TIE_TRACE_HEADER_FLAG_MISSION_ENDED)
		return "mission_ended";
	return "unknown";
}

static const char* fg_name(const TraceFile* trace_file, uint8_t fg) {
	return fg < 48 ? trace_file->fg_names[fg] : "invalid";
}

static int read_trace(const char* path, TraceFile* trace_file) {
	memset(trace_file, 0, sizeof *trace_file);
	FILE* file = fopen(path, "rb");
	if (!file) {
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
		return 0;
	}
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return 0;
	}
	const long length = ftell(file);
	if (length < (long)TIE_FLIGHT_TRACE_HEADER_SIZE || fseek(file, 0, SEEK_SET) != 0) {
		fprintf(stderr, "%s: truncated flight trace\n", path);
		fclose(file);
		return 0;
	}
	trace_file->size = (size_t)length;
	trace_file->data = malloc(trace_file->size);
	if (!trace_file->data || fread(trace_file->data, 1, trace_file->size, file) != trace_file->size) {
		fprintf(stderr, "%s: could not read flight trace\n", path);
		free(trace_file->data);
		trace_file->data = NULL;
		fclose(file);
		return 0;
	}
	fclose(file);
	if (memcmp(trace_file->data + TIE_TRACE_HDR_MAGIC, TIE_FLIGHT_TRACE_MAGIC, 8) != 0 ||
		get_u16(trace_file->data + TIE_TRACE_HDR_VERSION) != TIE_FLIGHT_TRACE_VERSION) {
		fprintf(stderr, "%s: unsupported flight trace format\n", path);
		free(trace_file->data);
		trace_file->data = NULL;
		return 0;
	}
	trace_file->header_size = get_u16(trace_file->data + TIE_TRACE_HDR_HEADER_SIZE);
	trace_file->used = get_u32(trace_file->data + TIE_TRACE_HDR_BYTES_USED);
	if (trace_file->header_size < TIE_FLIGHT_TRACE_HEADER_SIZE || trace_file->used > trace_file->size ||
		trace_file->used < trace_file->header_size) {
		fprintf(stderr, "%s: invalid flight trace bounds\n", path);
		free(trace_file->data);
		trace_file->data = NULL;
		return 0;
	}
	return 1;
}

static void load_fg_names(TraceFile* trace_file) {
	uint32_t offset = trace_file->header_size;
	while (offset + TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE <= trace_file->used) {
		const uint8_t* record = trace_file->data + offset;
		const uint16_t size = get_u16(record + 2);
		if (size < TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE || size > trace_file->used - offset)
			break;
		if (get_u16(record) == TIE_TRACE_RECORD_FG_DEF &&
			size >= TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE + 16) {
			const uint8_t* payload = record + TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
			if (payload[0] < 48) {
				char* name = trace_file->fg_names[payload[0]];
				memcpy(name, payload + 4, TIE_FLIGHT_TRACE_FG_NAME_SIZE);
				name[TIE_FLIGHT_TRACE_FG_NAME_SIZE] = '\0';
				size_t length = TIE_FLIGHT_TRACE_FG_NAME_SIZE;
				while (length && (name[length - 1] == '\0' || name[length - 1] == ' '))
					name[--length] = '\0';
			}
		}
		offset += size;
	}
}

static int parse_int(const char* text, int* value) {
	char* end = NULL;
	const long parsed = strtol(text, &end, 0);
	if (!text[0] || !end || *end || parsed < 0 || parsed > 0x7FFFFFFF)
		return 0;
	*value = (int)parsed;
	return 1;
}

static int parse_options(int argc, char** argv, Options* options) {
	*options = (Options){0};
	options->object_filter = -1;
	options->around_frame = -1;
	options->radius = 5;
	if (argc < 2)
		return 0;
	options->path = argv[1];
	for (int i = 2; i < argc; ++i) {
		if (strcmp(argv[i], "--jsonl") == 0) {
			options->jsonl = 1;
		} else if (strcmp(argv[i], "--frames") == 0) {
			options->show_frames = 1;
		} else if (strcmp(argv[i], "--ai-scheduler") == 0) {
			options->show_ai_scheduler = 1;
		} else if (strcmp(argv[i], "--transient-objects") == 0) {
			options->show_transient_objects = 1;
		} else if (strcmp(argv[i], "--all") == 0) {
			options->show_frames = 1;
			options->show_ai_scheduler = 1;
			options->show_transient_objects = 1;
		} else if (i + 1 < argc && strcmp(argv[i], "--fg") == 0) {
			options->fg_filter = argv[++i];
		} else if (i + 1 < argc && strcmp(argv[i], "--object") == 0) {
			if (!parse_int(argv[++i], &options->object_filter) || options->object_filter > 0xFFFF)
				return 0;
		} else if (i + 1 < argc && strcmp(argv[i], "--around-frame") == 0) {
			if (!parse_int(argv[++i], &options->around_frame))
				return 0;
		} else if (i + 1 < argc && strcmp(argv[i], "--radius") == 0) {
			if (!parse_int(argv[++i], &options->radius))
				return 0;
		} else if (i + 1 < argc && strcmp(argv[i], "--diff") == 0) {
			options->diff_path = argv[++i];
		} else {
			return 0;
		}
	}
	return 1;
}

static int resolve_fg(const TraceFile* trace_file, const char* filter) {
	if (!filter)
		return -1;
	int value;
	if (parse_int(filter, &value) && value < 48)
		return value;
	for (int i = 0; i < 48; ++i)
		if (strcmp(trace_file->fg_names[i], filter) == 0)
			return i;
	return -2;
}

static int object_record(uint16_t type) {
	return type == TIE_TRACE_RECORD_OBJECT_SPAWN || type == TIE_TRACE_RECORD_OBJECT_REMOVED ||
		   type == TIE_TRACE_RECORD_FG_EXIT ||
		   type == TIE_TRACE_RECORD_AI_CHANGE || type == TIE_TRACE_RECORD_BOARD ||
		   type == TIE_TRACE_RECORD_WEAPON || type == TIE_TRACE_RECORD_COLLISION ||
		   type == TIE_TRACE_RECORD_DAMAGE || type == TIE_TRACE_RECORD_DEATH ||
		   type == TIE_TRACE_RECORD_EXPLOSION || type == TIE_TRACE_RECORD_TARGET_CHANGE;
}

static int related_ref(uint16_t type, const uint8_t* payload, uint16_t payload_size, uint16_t ref) {
	if (object_record(type) && payload_size >= TIE_FLIGHT_TRACE_OBJECT_SIZE && get_u16(payload) == ref)
		return 1;
	if ((type == TIE_TRACE_RECORD_BOARD || type == TIE_TRACE_RECORD_COLLISION) && payload_size >= 96 &&
		get_u16(payload + 48) == ref)
		return 1;
	if ((type == TIE_TRACE_RECORD_WEAPON || type == TIE_TRACE_RECORD_DAMAGE ||
		 type == TIE_TRACE_RECORD_TARGET_CHANGE) && payload_size >= 72 &&
		(get_u16(payload + 48) == ref || get_u16(payload + 60) == ref))
		return 1;
	return (type == TIE_TRACE_RECORD_DEATH || type == TIE_TRACE_RECORD_EXPLOSION) &&
		   payload_size >= 60 && get_u16(payload + 48) == ref;
}

static int related_fg(uint16_t type, const uint8_t* payload, uint16_t payload_size, uint8_t fg) {
	if (type == TIE_TRACE_RECORD_FG_DEF || type == TIE_TRACE_RECORD_FG_STATE)
		return payload_size > 0 && payload[0] == fg;
	if (object_record(type) && payload_size >= TIE_FLIGHT_TRACE_OBJECT_SIZE && payload[8] == fg)
		return 1;
	if ((type == TIE_TRACE_RECORD_BOARD || type == TIE_TRACE_RECORD_COLLISION) && payload_size >= 96 &&
		payload[56] == fg)
		return 1;
	if ((type == TIE_TRACE_RECORD_WEAPON || type == TIE_TRACE_RECORD_DAMAGE ||
		 type == TIE_TRACE_RECORD_TARGET_CHANGE) && payload_size >= 72 &&
		(payload[56] == fg || payload[68] == fg))
		return 1;
	return (type == TIE_TRACE_RECORD_DEATH || type == TIE_TRACE_RECORD_EXPLOSION) &&
		   payload_size >= 60 && payload[56] == fg;
}

static int ai_scheduler_only(const uint8_t* payload, uint16_t payload_size) {
	if (payload_size < 83 || get_u16(payload + 70) == get_u16(payload + 72))
		return 0;
	return payload[48] == payload[49] && payload[50] == payload[51] &&
		   payload[52] == payload[53] && payload[54] == payload[55] &&
		   payload[56] == payload[57] && payload[58] == payload[59] &&
		   payload[60] == payload[61] && get_u16(payload + 62) == get_u16(payload + 64) &&
		   get_u16(payload + 66) == get_u16(payload + 68);
}

static int transient_genus(uint8_t genus) {
	return genus == TRACE_GENUS_PROJECTILE_PLAYER || genus == TRACE_GENUS_PROJECTILE_NPC ||
		   genus == TRACE_GENUS_DEBRIS || genus == TRACE_GENUS_EXPLOSION;
}

static int default_hidden(uint16_t type, const uint8_t* payload, uint16_t payload_size,
						  const Options* options) {
	if (type == TIE_TRACE_RECORD_FRAME && !options->show_frames)
		return 1;
	if (type == TIE_TRACE_RECORD_AI_CHANGE && !options->show_ai_scheduler &&
		ai_scheduler_only(payload, payload_size))
		return 1;
	if (!options->show_transient_objects && payload_size >= TIE_FLIGHT_TRACE_OBJECT_SIZE &&
		(type == TIE_TRACE_RECORD_OBJECT_SPAWN || type == TIE_TRACE_RECORD_OBJECT_REMOVED ||
		 type == TIE_TRACE_RECORD_EXPLOSION) &&
		transient_genus(payload[TIE_TRACE_OBJECT_GENUS]))
		return 1;
	return 0;
}

static int record_matches(uint16_t type, const uint8_t* payload, uint16_t payload_size, uint32_t frame,
						  const Options* options, int fg_filter) {
	if (default_hidden(type, payload, payload_size, options))
		return 0;
	if (options->around_frame >= 0) {
		const uint32_t center = (uint32_t)options->around_frame;
		const uint32_t radius = (uint32_t)options->radius;
		if ((frame < center && center - frame > radius) || (frame > center && frame - center > radius))
			return 0;
	}
	if (options->object_filter >= 0 &&
		!related_ref(type, payload, payload_size, (uint16_t)options->object_filter))
		return 0;
	if (fg_filter >= 0 && !related_fg(type, payload, payload_size, (uint8_t)fg_filter))
		return 0;
	return 1;
}

static void print_object(const TraceFile* trace_file, const uint8_t* p) {
	const uint8_t fg = p[TIE_TRACE_OBJECT_FG];
	printf("obj=%u:%u id=%u fg=%u", get_u16(p + TIE_TRACE_OBJECT_REF),
		   get_u32(p + TIE_TRACE_OBJECT_GENERATION), get_u16(p + TIE_TRACE_OBJECT_ID), fg);
	if (fg < 48 && trace_file->fg_names[fg][0])
		printf("(%s)", trace_file->fg_names[fg]);
	printf(" species=%u craft=%u genus=%u side=%u mode=%u/%u flight=%u dock=%02x radius=%d "
		   "target=%u owner=%u pos=(%d,%d,%d) vel=(%d,%d,%d)",
		   p[TIE_TRACE_OBJECT_SPECIES], p[TIE_TRACE_OBJECT_CRAFT_INDEX], p[TIE_TRACE_OBJECT_GENUS],
		   p[TIE_TRACE_OBJECT_SIDE], p[TIE_TRACE_OBJECT_MODE], p[TIE_TRACE_OBJECT_SUBMODE],
		   p[TIE_TRACE_OBJECT_FLIGHT_FLAG], p[TIE_TRACE_OBJECT_DOCK_FLAGS],
		   get_i16(p + TIE_TRACE_OBJECT_COLLISION_RADIUS), get_u16(p + TIE_TRACE_OBJECT_TARGET),
		   get_u16(p + TIE_TRACE_OBJECT_OWNER), get_i32(p + TIE_TRACE_OBJECT_X),
		   get_i32(p + TIE_TRACE_OBJECT_Y), get_i32(p + TIE_TRACE_OBJECT_Z),
		   get_i32(p + TIE_TRACE_OBJECT_VX), get_i32(p + TIE_TRACE_OBJECT_VY),
		   get_i32(p + TIE_TRACE_OBJECT_VZ));
}

static void print_identity(const char* label, const uint8_t* p) {
	printf(" %s=%u:%u(id=%u fg=%u species=%u craft=%u)", label,
		   get_u16(p + TIE_TRACE_IDENTITY_REF), get_u32(p + TIE_TRACE_IDENTITY_GENERATION),
		   get_u16(p + TIE_TRACE_IDENTITY_ID), p[TIE_TRACE_IDENTITY_FG],
		   p[TIE_TRACE_IDENTITY_SPECIES], p[TIE_TRACE_IDENTITY_CRAFT_INDEX]);
}

static void print_fg_state(const TraceFile* trace_file, const uint8_t* p) {
	printf("fg=%u(%s) active=%u waves=%u arrival=%u delay=%u primary=%u secondary=%u complete=%u cond=",
		   p[0], fg_name(trace_file, p[0]), p[1], p[2], p[3], get_u16(p + 4), p[44], p[45], p[46]);
	for (int i = 0; i < 9; ++i)
		printf("%s%u/%u:%u/%u", i ? "," : "", p[8 + i * 2], p[9 + i * 2], p[26 + i * 2],
			   p[27 + i * 2]);
}

static void print_object_event(const TraceFile* trace_file, uint16_t type, const uint8_t* p,
							   uint16_t payload_size) {
	print_object(trace_file, p);
	if (type == TIE_TRACE_RECORD_FG_EXIT && payload_size >= 50) {
		printf(" exit=%s(%u)", exit_name(get_u16(p + 48)), get_u16(p + 48));
	} else if (type == TIE_TRACE_RECORD_OBJECT_REMOVED && payload_size >= 50) {
		printf(" removal=%s(%u)", removal_name(get_u16(p + 48)), get_u16(p + 48));
	} else if (type == TIE_TRACE_RECORD_AI_CHANGE && payload_size >= 83) {
		printf(" order=%u->%u default=%u->%u mode=%u/%u->%u/%u flight=%u->%u dock=%02x->%02x "
			   "entry=%u->%u target=%u->%u attacker=%u->%u plan=%u->%u timer=%d->%d opcode=%u",
			   p[48], p[49], p[50], p[51], p[52], p[54], p[53], p[55], p[56], p[57], p[58], p[59],
			   p[60], p[61], get_u16(p + 62), get_u16(p + 64), get_u16(p + 66), get_u16(p + 68),
			   get_u16(p + 70), get_u16(p + 72), get_i32(p + 74), get_i32(p + 78), p[82]);
	} else if (type == TIE_TRACE_RECORD_BOARD && payload_size >= 110) {
		printf(" target{");
		print_object(trace_file, p + 48);
		printf("} event=%s order=%u push=(%d,%d,%d)", board_name(p[96]), p[97], get_i32(p + 98),
			   get_i32(p + 102), get_i32(p + 106));
	} else if (type == TIE_TRACE_RECORD_WEAPON && payload_size >= 72) {
		print_identity("shooter", p + 48);
		print_identity("target", p + 60);
	} else if (type == TIE_TRACE_RECORD_TARGET_CHANGE && payload_size >= 72) {
		print_identity("old_target", p + 48);
		print_identity("new_target", p + 60);
	} else if (type == TIE_TRACE_RECORD_COLLISION && payload_size >= 100) {
		printf(" other{");
		print_object(trace_file, p + 48);
		printf("} kind=%s component=%d", collision_name(p[96]), get_i16(p + 98));
	} else if (type == TIE_TRACE_RECORD_DAMAGE && payload_size >= 106) {
		print_identity("attacker", p + 48);
		print_identity("responsible", p + 60);
		printf(" cause=%s component=%d raw=%d shields=%d->%d,%d->%d hull=%u->%u/%u "
			   "status=%x->%x working=%x->%x death_timer=%d->%d flight=%u->%u",
			   cause_name(p[72]), get_i16(p + 74), get_i16(p + 76), get_i16(p + 78), get_i16(p + 80),
			   get_i16(p + 82), get_i16(p + 84), get_u16(p + 86), get_u16(p + 88), get_u16(p + 90),
			   get_u16(p + 92), get_u16(p + 94), get_u16(p + 96), get_u16(p + 98), get_i16(p + 100),
			   get_i16(p + 102), p[104], p[105]);
	} else if (type == TIE_TRACE_RECORD_DEATH && payload_size >= 64) {
		print_identity("responsible", p + 48);
		printf(" timer=%d kind=%u cause=%s", get_i16(p + 60), p[62], cause_name(p[63]));
	} else if (type == TIE_TRACE_RECORD_EXPLOSION && payload_size >= 62) {
		print_identity("responsible", p + 48);
		printf(" cause=%s variant=%u", cause_name(p[60]), p[61]);
	}
}

static void print_human_record(const TraceFile* trace_file, const uint8_t* record) {
	const uint16_t type = get_u16(record);
	const uint16_t size = get_u16(record + 2);
	const uint8_t* p = record + TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
	const uint16_t payload_size = size - TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
	printf("f=%u seq=%u %-10s %-13s ", get_u32(record + 4), get_u32(record + 8),
		   phase_name(get_u16(record + 12)), record_name(type));
	if (type == TIE_TRACE_RECORD_FG_DEF && payload_size >= 16) {
		printf("fg=%u name=%s species=%u side=%u count=%u", p[0], fg_name(trace_file, p[0]), p[1],
			   p[2], p[3]);
	} else if (type == TIE_TRACE_RECORD_FRAME && payload_size >= 20) {
		printf("ticks=%u rate=%u time=%02u:%02u:%02u.%u rng=%u hash=%08x score=%d", get_u16(p),
			   get_u16(p + 2), p[4], p[5], p[6], get_u16(p + 8), get_u16(p + 10), get_u32(p + 12),
			   get_i32(p + 16));
	} else if (type == TIE_TRACE_RECORD_FG_STATE && payload_size >= 47) {
		print_fg_state(trace_file, p);
	} else if (type == TIE_TRACE_RECORD_MISSION_STATE && payload_size >= 21) {
		printf("end=%u player=%u objectives=%u/%u/%u radio=", p[0], p[1], p[2], p[3], p[4]);
		for (int i = 0; i < 16; ++i)
			if (p[5 + i])
				printf("%d,", i);
	} else if (type == TIE_TRACE_RECORD_OVERFLOW && payload_size >= 8) {
		printf("dropped_at_marker=%u critical_dropped_at_marker=%u", get_u32(p), get_u32(p + 4));
	} else if (object_record(type) && payload_size >= TIE_FLIGHT_TRACE_OBJECT_SIZE) {
		print_object_event(trace_file, type, p, payload_size);
	} else {
		printf("type=%u payload_size=%u", type, payload_size);
	}
	putchar('\n');
}

static void print_json_object(const uint8_t* p) {
	printf("{\"ref\":%u,\"generation\":%u,\"id\":%u,\"fg\":%u,\"species\":%u,"
		   "\"craft\":%u,\"genus\":%u,\"side\":%u,\"mode\":%u,\"submode\":%u,"
		   "\"flight\":%u,\"dock_flags\":%u,\"radius\":%d,\"target\":%u,\"owner\":%u,"
		   "\"position\":[%d,%d,%d],\"velocity\":[%d,%d,%d]}",
		   get_u16(p), get_u32(p + 4), get_u16(p + 2), p[8], p[9], p[12], p[10], p[11], p[13],
		   p[14], p[15], p[16], get_i16(p + 18), get_u16(p + 20), get_u16(p + 22), get_i32(p + 24),
		   get_i32(p + 28), get_i32(p + 32), get_i32(p + 36), get_i32(p + 40), get_i32(p + 44));
}

static void print_json_identity(const uint8_t* p) {
	printf("{\"ref\":%u,\"generation\":%u,\"id\":%u,\"fg\":%u,\"species\":%u,\"craft\":%u}",
		   get_u16(p), get_u32(p + 4), get_u16(p + 2), p[8], p[9], p[10]);
}

static void print_json_string(const uint8_t* text, size_t limit) {
	putchar('"');
	for (size_t i = 0; i < limit && text[i]; ++i) {
		const uint8_t c = text[i];
		if (c == '"' || c == '\\')
			printf("\\%c", c);
		else if (c >= 0x20)
			putchar(c);
		else
			printf("\\u%04x", c);
	}
	putchar('"');
}

static void print_json_details(uint16_t type, const uint8_t* p, uint16_t payload_size) {
	if (type == TIE_TRACE_RECORD_FG_DEF && payload_size >= 16) {
		printf(",\"fg\":%u,\"species\":%u,\"side\":%u,\"count\":%u,\"name\":", p[0], p[1],
			   p[2], p[3]);
		print_json_string(p + 4, TIE_FLIGHT_TRACE_FG_NAME_SIZE);
	} else if (type == TIE_TRACE_RECORD_FRAME && payload_size >= 20) {
		printf(",\"ticks\":%u,\"rate\":%u,\"clock\":[%u,%u,%u,%u],\"rng\":%u,"
			   "\"hash\":%u,\"score\":%d", get_u16(p), get_u16(p + 2), p[4], p[5], p[6],
			   get_u16(p + 8), get_u16(p + 10), get_u32(p + 12), get_i32(p + 16));
	} else if (type == TIE_TRACE_RECORD_FG_STATE && payload_size >= 47) {
		printf(",\"fg\":%u,\"active\":%u,\"waves\":%u,\"arrival\":%u,\"arrival_delay\":%u,"
			   "\"world_position\":%u,\"primary\":%u,\"secondary\":%u,\"complete\":%u,"
			   "\"conditions\":[", p[0], p[1], p[2], p[3], get_u16(p + 4), get_u16(p + 6), p[44],
			   p[45], p[46]);
		for (int i = 0; i < 9; ++i)
			printf("%s[%u,%u,%u,%u]", i ? "," : "", p[8 + i * 2], p[9 + i * 2],
				   p[26 + i * 2], p[27 + i * 2]);
		putchar(']');
	} else if (type == TIE_TRACE_RECORD_MISSION_STATE && payload_size >= 21) {
		printf(",\"end\":%u,\"player_status\":%u,\"primary\":%u,\"secondary\":%u,\"bonus\":%u",
			   p[0], p[1], p[2], p[3], p[4]);
		printf(",\"radio\":[");
		for (int i = 0; i < 16; ++i)
			printf("%s%u", i ? "," : "", p[5 + i]);
		putchar(']');
	} else if (type == TIE_TRACE_RECORD_OVERFLOW && payload_size >= 8) {
		printf(",\"dropped\":%u,\"critical_dropped\":%u", get_u32(p), get_u32(p + 4));
	} else {
		printf(",\"payload_size\":%u", payload_size);
	}
}

static void print_json_object_event(uint16_t type, const uint8_t* p, uint16_t payload_size) {
	printf(",\"object\":");
	print_json_object(p);
	if (type == TIE_TRACE_RECORD_FG_EXIT && payload_size >= 50)
		printf(",\"exit_kind\":%u", get_u16(p + 48));
	else if (type == TIE_TRACE_RECORD_OBJECT_REMOVED && payload_size >= 50)
		printf(",\"removal_kind\":%u", get_u16(p + 48));
	else if (type == TIE_TRACE_RECORD_AI_CHANGE && payload_size >= 83)
		printf(",\"order\":[%u,%u],\"default_order\":[%u,%u],\"mode\":[%u,%u],"
			   "\"submode\":[%u,%u],\"flight\":[%u,%u],"
			   "\"dock_flags\":[%u,%u],\"ai_entry\":[%u,%u],\"target\":[%u,%u],"
			   "\"attacker\":[%u,%u],\"plan_state\":[%u,%u],\"timer\":[%d,%d],\"opcode\":%u",
			   p[48], p[49], p[50], p[51], p[52], p[53], p[54], p[55], p[56], p[57], p[58], p[59],
			   p[60], p[61], get_u16(p + 62), get_u16(p + 64), get_u16(p + 66), get_u16(p + 68),
			   get_u16(p + 70), get_u16(p + 72), get_i32(p + 74), get_i32(p + 78), p[82]);
	else if ((type == TIE_TRACE_RECORD_BOARD || type == TIE_TRACE_RECORD_COLLISION) && payload_size >= 96) {
		printf(",\"other\":");
		print_json_object(p + 48);
		if (type == TIE_TRACE_RECORD_BOARD && payload_size >= 110)
			printf(",\"board_kind\":%u,\"order\":%u,\"push\":[%d,%d,%d]", p[96], p[97],
				   get_i32(p + 98), get_i32(p + 102), get_i32(p + 106));
		else if (payload_size >= 100)
			printf(",\"collision_kind\":%u,\"component\":%d", p[96], get_i16(p + 98));
	} else if ((type == TIE_TRACE_RECORD_WEAPON || type == TIE_TRACE_RECORD_TARGET_CHANGE) &&
			   payload_size >= 72) {
		printf(type == TIE_TRACE_RECORD_WEAPON ? ",\"shooter\":" : ",\"old_target\":");
		print_json_identity(p + 48);
		printf(type == TIE_TRACE_RECORD_WEAPON ? ",\"target\":" : ",\"new_target\":");
		print_json_identity(p + 60);
	} else if (type == TIE_TRACE_RECORD_DAMAGE && payload_size >= 106) {
		printf(",\"attacker\":");
		print_json_identity(p + 48);
		printf(",\"responsible\":");
		print_json_identity(p + 60);
		printf(",\"cause\":%u,\"component\":%d,\"raw_damage\":%d,\"hull\":[%u,%u,%u],"
			   "\"shields\":[%d,%d,%d,%d],\"status\":[%u,%u],\"working\":[%u,%u],"
			   "\"death_timer\":[%d,%d],\"flight\":[%u,%u]", p[72], get_i16(p + 74), get_i16(p + 76),
			   get_u16(p + 86), get_u16(p + 88), get_u16(p + 90), get_i16(p + 78), get_i16(p + 80),
			   get_i16(p + 82), get_i16(p + 84), get_u16(p + 92), get_u16(p + 94), get_u16(p + 96),
			   get_u16(p + 98), get_i16(p + 100), get_i16(p + 102), p[104], p[105]);
	} else if ((type == TIE_TRACE_RECORD_DEATH || type == TIE_TRACE_RECORD_EXPLOSION) &&
			   payload_size >= 60) {
		printf(",\"responsible\":");
		print_json_identity(p + 48);
		if (type == TIE_TRACE_RECORD_DEATH && payload_size >= 64)
			printf(",\"timer\":%d,\"death_kind\":%u,\"cause\":%u", get_i16(p + 60), p[62], p[63]);
		else if (payload_size >= 62)
			printf(",\"cause\":%u,\"variant\":%u", p[60], p[61]);
	}
}

static void print_json_record(const uint8_t* record) {
	const uint16_t type = get_u16(record);
	const uint16_t size = get_u16(record + 2);
	const uint8_t* p = record + TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
	const uint16_t payload_size = size - TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
	printf("{\"frame\":%u,\"sequence\":%u,\"phase\":\"%s\",\"event\":\"%s\",\"type\":%u,"
		   "\"flags\":%u",
		   get_u32(record + 4), get_u32(record + 8), phase_name(get_u16(record + 12)),
		   record_name(type), type, get_u16(record + 14));
	if (object_record(type) && payload_size >= TIE_FLIGHT_TRACE_OBJECT_SIZE)
		print_json_object_event(type, p, payload_size);
	else
		print_json_details(type, p, payload_size);
	printf("}\n");
}

static int dump_trace(TraceFile* trace_file, const Options* options) {
	load_fg_names(trace_file);
	const int fg_filter = resolve_fg(trace_file, options->fg_filter);
	if (fg_filter == -2) {
		fprintf(stderr, "unknown flight group: %s\n", options->fg_filter);
		return 0;
	}
	if (!options->jsonl)
		printf("mission=%s checksum=%08x build=%s termination=%s profile=%u timing=%u difficulty=%u records=%u "
			   "dropped=%u critical_dropped=%u started=%llu\n",
			   trace_file->data + TIE_TRACE_HDR_MISSION,
			   get_u32(trace_file->data + TIE_TRACE_HDR_MISSION_CHECKSUM),
			   trace_file->data + TIE_TRACE_HDR_BUILD,
			   termination_name(get_u32(trace_file->data + TIE_TRACE_HDR_FLAGS)),
			   get_u32(trace_file->data + TIE_TRACE_HDR_PROFILE),
			   get_u32(trace_file->data + TIE_TRACE_HDR_TIMING_MODE),
			   get_u32(trace_file->data + TIE_TRACE_HDR_DIFFICULTY),
			   get_u32(trace_file->data + TIE_TRACE_HDR_RECORD_COUNT),
			   get_u32(trace_file->data + TIE_TRACE_HDR_DROPPED),
			   get_u32(trace_file->data + TIE_TRACE_HDR_DROPPED_CRITICAL),
			   (unsigned long long)get_u64(trace_file->data + TIE_TRACE_HDR_START_TIME));
	uint32_t offset = trace_file->header_size;
	while (offset < trace_file->used) {
		const uint8_t* record = trace_file->data + offset;
		if (trace_file->used - offset < TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE) {
			fprintf(stderr, "truncated record header at offset %u\n", offset);
			return 0;
		}
		const uint16_t size = get_u16(record + 2);
		if (size < TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE || size > trace_file->used - offset) {
			fprintf(stderr, "corrupt record at offset %u\n", offset);
			return 0;
		}
		const uint16_t type = get_u16(record);
		const uint8_t* payload = record + TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
		const uint16_t payload_size = size - TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
		if (record_matches(type, payload, payload_size, get_u32(record + 4), options, fg_filter)) {
			if (options->jsonl)
				print_json_record(record);
			else
				print_human_record(trace_file, record);
		}
		offset += size;
	}
	return 1;
}

static int diff_traces(TraceFile* first, TraceFile* second) {
	const uint32_t first_checksum = get_u32(first->data + TIE_TRACE_HDR_MISSION_CHECKSUM);
	const uint32_t second_checksum = get_u32(second->data + TIE_TRACE_HDR_MISSION_CHECKSUM);
	if (first_checksum != second_checksum ||
		get_u32(first->data + TIE_TRACE_HDR_PROFILE) != get_u32(second->data + TIE_TRACE_HDR_PROFILE) ||
		get_u32(first->data + TIE_TRACE_HDR_TIMING_MODE) !=
			get_u32(second->data + TIE_TRACE_HDR_TIMING_MODE) ||
		get_u32(first->data + TIE_TRACE_HDR_DIFFICULTY) !=
			get_u32(second->data + TIE_TRACE_HDR_DIFFICULTY)) {
		printf("trace metadata differs (mission checksum/profile/timing/difficulty)\n");
		return 1;
	}
	uint32_t a = first->header_size;
	uint32_t b = second->header_size;
	uint32_t index = 0;
	while (a < first->used && b < second->used) {
		const uint16_t as = get_u16(first->data + a + 2);
		const uint16_t bs = get_u16(second->data + b + 2);
		if (as < TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE || bs < TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE ||
			as > first->used - a || bs > second->used - b)
			break;
		if (as != bs || memcmp(first->data + a, second->data + b, as) != 0) {
			printf("first divergence at record %u\nA: ", index);
			print_human_record(first, first->data + a);
			printf("B: ");
			print_human_record(second, second->data + b);
			return 1;
		}
		a += as;
		b += bs;
		++index;
	}
	if (a == first->used && b == second->used)
		printf("traces contain the same event stream\n");
	else
		printf("first divergence at record %u: one trace ended or is corrupt\n", index);
	return 1;
}

int main(int argc, char** argv) {
	Options options;
	if (!parse_options(argc, argv, &options)) {
		fprintf(stderr, "usage: flight_trace_dump TRACE [--fg INDEX|NAME] [--object REF] "
						"[--around-frame N] [--radius N] [--jsonl] [--frames] "
						"[--ai-scheduler] [--transient-objects] [--all] [--diff TRACE]\n");
		return 2;
	}
	TraceFile first;
	if (!read_trace(options.path, &first))
		return 1;
	int ok;
	if (options.diff_path) {
		TraceFile second;
		if (!read_trace(options.diff_path, &second)) {
			free(first.data);
			return 1;
		}
		load_fg_names(&first);
		load_fg_names(&second);
		ok = diff_traces(&first, &second);
		free(second.data);
	} else {
		ok = dump_trace(&first, &options);
	}
	free(first.data);
	return ok ? 0 : 1;
}
