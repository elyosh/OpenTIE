#include "tie_runtime/diagnostics/flight_trace.h"

#if defined(TIE_ENABLE_FLIGHT_TRACE)

#include "tie/laser.h"
#include "tie/math2.h"
#include "tie/shipext.h"
#include "tie/tie.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef TIE_VERSION_STRING
#define TIE_VERSION_STRING "unknown"
#endif

#define TRACE_CAPACITY (16u * 1024u * 1024u)
#define TRACE_CRITICAL_RESERVE (1024u * 1024u)
#define TRACE_NORMAL_LIMIT (TRACE_CAPACITY - TRACE_CRITICAL_RESERVE)
#define TRACE_INVALID_REF 0xFFFFu
#define TRACE_FILE_PREFIX "flighttrace-"
#define TRACE_FILE_SUFFIX ".otft"
#define TRACE_RETAIN_COUNT 5u

typedef struct TraceObjectShadow {
	uint32_t generation;
	uint16_t id;
	uint16_t target;
	uint16_t owner;
	int16_t collision_radius;
	uint8_t occupied;
	uint8_t fg;
	uint8_t species;
	uint8_t genus;
	uint8_t side;
	uint8_t craft_index;
	uint8_t mode;
	uint8_t submode;
	uint8_t flight_flag;
	uint8_t dock_flags;
	uint8_t category;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t vx;
	int32_t vy;
	int32_t vz;
} TraceObjectShadow;

typedef struct TraceIdentity {
	uint32_t generation;
	uint16_t ref;
	uint16_t id;
	uint8_t fg;
	uint8_t species;
	uint8_t craft_index;
} TraceIdentity;

typedef struct TraceAiState {
	uint16_t plan_state;
	uint16_t target;
	uint16_t attacker;
	int32_t timer;
	uint8_t order;
	uint8_t default_order;
	uint8_t mode;
	uint8_t submode;
	uint8_t flight_flag;
	uint8_t dock_flags;
	uint8_t ai_entry;
} TraceAiState;

typedef struct TraceDamageState {
	int16_t front_shield;
	int16_t rear_shield;
	uint16_t hull_damage;
	uint16_t hull_max;
	uint16_t status;
	uint16_t working;
	int16_t death_timer;
	uint8_t flight_flag;
} TraceDamageState;

typedef struct TraceMissionState {
	uint8_t end_flag;
	uint8_t player_status;
	uint8_t primary;
	uint8_t secondary;
	uint8_t bonus;
	uint8_t radio[16];
} TraceMissionState;

typedef struct TracePendingCause {
	TraceIdentity responsible;
	uint32_t victim_generation;
	uint8_t cause;
	uint8_t valid;
	uint8_t terminal;
} TracePendingCause;

typedef struct TraceSavedFile {
	char name[TIE_DIR_NAME_MAX];
	uint64_t timestamp;
	uint8_t current;
} TraceSavedFile;

typedef struct FlightTraceRecorder {
	uint8_t data[TRACE_CAPACITY];
	uint32_t used;
	uint32_t records;
	uint32_t dropped;
	uint32_t dropped_critical;
	uint32_t frame;
	uint32_t sequence;
	uint64_t start_time;
	TieFlightTracePhase phase;
	uint8_t active;
	uint8_t num_fg;
	uint8_t overflow_emitted;
	TraceObjectShadow objects[NUM_OBJECTS];
	TraceObjectShadow statics[NUM_STATIC_OBJECTS];
	uint32_t object_generation[NUM_OBJECTS];
	uint32_t static_generation[NUM_STATIC_OBJECTS];
	TracePendingCause pending_cause[NUM_OBJECTS];
	FGStatus fg[48];
	TraceMissionState mission;
	TraceAiState ai_before[NUM_CRAFTS];
	TraceAiState ai_observed[NUM_CRAFTS];
	TraceDamageState damage_before[NUM_CRAFTS];
	uint8_t ai_before_valid[NUM_CRAFTS];
	uint8_t ai_observed_valid[NUM_CRAFTS];
	uint8_t damage_before_valid[NUM_CRAFTS];
} FlightTraceRecorder;

static FlightTraceRecorder trace;

static void put_u16(uint8_t* dst, uint16_t value) {
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t* dst, uint32_t value) {
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
	dst[2] = (uint8_t)(value >> 16);
	dst[3] = (uint8_t)(value >> 24);
}

static void put_u64(uint8_t* dst, uint64_t value) {
	put_u32(dst, (uint32_t)value);
	put_u32(dst + 4, (uint32_t)(value >> 32));
}

static uint8_t* append_record(TieFlightTraceRecordType type, uint16_t payload_size, uint16_t flags) {
	const uint32_t record_size = TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE + payload_size;
	uint8_t* record = trace.data + trace.used;
	put_u16(record, (uint16_t)type);
	put_u16(record + 2, (uint16_t)record_size);
	put_u32(record + 4, trace.frame);
	put_u32(record + 8, trace.sequence++);
	put_u16(record + 12, (uint16_t)trace.phase);
	put_u16(record + 14, flags);
	trace.used += record_size;
	++trace.records;
	return record + TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE;
}

static void emit_overflow(void) {
	if (trace.overflow_emitted || trace.used + TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE + 8 > TRACE_CAPACITY)
		return;
	trace.overflow_emitted = 1;
	uint8_t* payload = append_record(TIE_TRACE_RECORD_OVERFLOW, 8, TIE_TRACE_RECORD_FLAG_CRITICAL);
	put_u32(payload, trace.dropped);
	put_u32(payload + 4, trace.dropped_critical);
}

static uint8_t* begin_record(TieFlightTraceRecordType type, uint16_t payload_size, int critical) {
	const uint32_t record_size = TIE_FLIGHT_TRACE_RECORD_HEADER_SIZE + payload_size;
	const uint32_t limit = critical ? TRACE_CAPACITY : TRACE_NORMAL_LIMIT;
	if (!trace.active)
		return NULL;
	if (trace.used > limit || record_size > limit - trace.used) {
		++trace.dropped;
		if (critical)
			++trace.dropped_critical;
		emit_overflow();
		return NULL;
	}
	return append_record(type, payload_size, critical ? TIE_TRACE_RECORD_FLAG_CRITICAL : 0);
}

static uint32_t ref_generation(uint16_t ref) {
	if (ref < NUM_OBJECTS)
		return trace.object_generation[ref];
	if (ref >= OBJ_REF_STATIC_BASE && ref < OBJ_REF_STATIC_BASE + NUM_STATIC_OBJECTS)
		return trace.static_generation[ref - OBJ_REF_STATIC_BASE];
	return 0;
}

static uint16_t object_id(uint16_t ref) {
	if (ref < NUM_OBJECTS)
		return objects[ref].idnumber;
	if (ref >= OBJ_REF_STATIC_BASE && ref < OBJ_REF_STATIC_BASE + NUM_STATIC_OBJECTS)
		return staticobjects[ref - OBJ_REF_STATIC_BASE].idnumber;
	return TRACE_INVALID_REF;
}

static TraceObjectShadow capture_object(uint16_t ref) {
	TraceObjectShadow state;
	memset(&state, 0, sizeof state);
	state.generation = ref_generation(ref);
	state.id = TRACE_INVALID_REF;
	state.target = TRACE_INVALID_REF;
	state.owner = TRACE_INVALID_REF;
	state.craft_index = 0xFFu;
	if (ref < NUM_OBJECTS) {
		const FlightObject* o = &objects[ref];
		state.occupied = o->ship_idx != 0;
		state.id = o->idnumber;
		state.fg = o->fg_idx;
		state.species = o->ship_idx;
		state.genus = o->genus;
		state.side = o->side;
		state.category = o->category;
		state.collision_radius = o->collision_radius;
		state.x = o->world_x;
		state.y = o->world_y;
		state.z = o->world_z;
		state.vx = (int32_t)((uint32_t)o->world_x - (uint32_t)o->world_x_prev);
		state.vy = (int32_t)((uint32_t)o->world_y - (uint32_t)o->world_y_prev);
		state.vz = (int32_t)((uint32_t)o->world_z - (uint32_t)o->world_z_prev);
		if (state.occupied && ref < NUM_CRAFTS && o->craft_ptr) {
			state.craft_index = o->craft_ptr->craft_idx_in_fg;
			state.mode = o->craft_ptr->mode_byte;
			state.submode = o->craft_ptr->mode_subbyte;
			state.flight_flag = o->craft_ptr->flight_flag;
			state.dock_flags = o->craft_ptr->dock_state_flags;
			state.target = (uint16_t)o->craft_ptr->ai_target_ref;
		} else if (state.occupied && ref >= NUM_CRAFTS && ref < WARHEAD_SLOT_END &&
				   (o->genus == GENUS_PROJECTILE_PLAYER || o->genus == GENUS_PROJECTILE_NPC)) {
			state.target = warheads[ref - NUM_CRAFTS].target_obj;
			state.owner = (uint16_t)o->self_idx;
		}
	} else if (ref >= OBJ_REF_STATIC_BASE && ref < OBJ_REF_STATIC_BASE + NUM_STATIC_OBJECTS) {
		const StaticObject* o = &staticobjects[ref - OBJ_REF_STATIC_BASE];
		state.occupied = o->species != 0;
		state.id = o->idnumber;
		if (!state.occupied)
			return state;
		state.fg = o->fg_idx;
		state.species = o->species;
		state.genus = o->ship_class;
		state.side = fg_array[o->fg_idx].side;
		state.x = (int32_t)o->world_x * 256;
		state.y = (int32_t)o->world_y * 256;
		state.z = (int32_t)o->world_z * 256;
	}
	return state;
}

static void encode_shadow(uint8_t* dst, uint16_t ref, const TraceObjectShadow* state) {
	memset(dst, 0, TIE_FLIGHT_TRACE_OBJECT_SIZE);
	put_u16(dst + TIE_TRACE_OBJECT_REF, ref);
	put_u16(dst + TIE_TRACE_OBJECT_ID, state->id);
	put_u32(dst + TIE_TRACE_OBJECT_GENERATION, state->generation);
	dst[TIE_TRACE_OBJECT_FG] = state->fg;
	dst[TIE_TRACE_OBJECT_SPECIES] = state->species;
	dst[TIE_TRACE_OBJECT_GENUS] = state->genus;
	dst[TIE_TRACE_OBJECT_SIDE] = state->side;
	dst[TIE_TRACE_OBJECT_CRAFT_INDEX] = state->craft_index;
	dst[TIE_TRACE_OBJECT_MODE] = state->mode;
	dst[TIE_TRACE_OBJECT_SUBMODE] = state->submode;
	dst[TIE_TRACE_OBJECT_FLIGHT_FLAG] = state->flight_flag;
	dst[TIE_TRACE_OBJECT_DOCK_FLAGS] = state->dock_flags;
	dst[TIE_TRACE_OBJECT_CATEGORY] = state->category;
	put_u16(dst + TIE_TRACE_OBJECT_COLLISION_RADIUS, (uint16_t)state->collision_radius);
	put_u16(dst + TIE_TRACE_OBJECT_TARGET, state->target);
	put_u16(dst + TIE_TRACE_OBJECT_OWNER, state->owner);
	put_u32(dst + TIE_TRACE_OBJECT_X, (uint32_t)state->x);
	put_u32(dst + TIE_TRACE_OBJECT_Y, (uint32_t)state->y);
	put_u32(dst + TIE_TRACE_OBJECT_Z, (uint32_t)state->z);
	put_u32(dst + TIE_TRACE_OBJECT_VX, (uint32_t)state->vx);
	put_u32(dst + TIE_TRACE_OBJECT_VY, (uint32_t)state->vy);
	put_u32(dst + TIE_TRACE_OBJECT_VZ, (uint32_t)state->vz);
}

static void encode_object(uint8_t* dst, uint16_t ref) {
	const TraceObjectShadow state = capture_object(ref);
	encode_shadow(dst, ref, &state);
}

static TraceIdentity capture_identity(uint16_t ref) {
	TraceIdentity identity = {ref_generation(ref), ref, object_id(ref), 0xFFu, 0, 0xFFu};
	if (ref < NUM_OBJECTS) {
		identity.fg = objects[ref].fg_idx;
		identity.species = objects[ref].ship_idx;
		if (objects[ref].ship_idx && ref < NUM_CRAFTS && objects[ref].craft_ptr)
			identity.craft_index = objects[ref].craft_ptr->craft_idx_in_fg;
	} else if (ref >= OBJ_REF_STATIC_BASE && ref < OBJ_REF_STATIC_BASE + NUM_STATIC_OBJECTS) {
		const StaticObject* o = &staticobjects[ref - OBJ_REF_STATIC_BASE];
		if (o->species) {
			identity.fg = o->fg_idx;
			identity.species = o->species;
		}
	}
	return identity;
}

static void encode_identity(uint8_t* dst, const TraceIdentity* identity) {
	memset(dst, 0, TIE_FLIGHT_TRACE_IDENTITY_SIZE);
	put_u16(dst + TIE_TRACE_IDENTITY_REF, identity->ref);
	put_u16(dst + TIE_TRACE_IDENTITY_ID, identity->id);
	put_u32(dst + TIE_TRACE_IDENTITY_GENERATION, identity->generation);
	dst[TIE_TRACE_IDENTITY_FG] = identity->fg;
	dst[TIE_TRACE_IDENTITY_SPECIES] = identity->species;
	dst[TIE_TRACE_IDENTITY_CRAFT_INDEX] = identity->craft_index;
}

static TraceAiState capture_ai(uint16_t obj_idx) {
	const CraftData* c = objects[obj_idx].craft_ptr;
	TraceAiState state;
	memset(&state, 0, sizeof state);
	state.order = c->current_order;
	state.default_order = c->default_order_ldr;
	state.mode = c->mode_byte;
	state.submode = c->mode_subbyte;
	state.flight_flag = c->flight_flag;
	state.dock_flags = c->dock_state_flags;
	state.ai_entry = c->ai_state_1C;
	state.plan_state = c->ai_plan_state;
	state.target = (uint16_t)c->ai_target_ref;
	state.attacker = c->attacker_idx;
	state.timer = c->maneuver_timer;
	return state;
}

static int ai_semantic_equal(const TraceAiState* a, const TraceAiState* b) {
	return a->order == b->order && a->default_order == b->default_order && a->mode == b->mode &&
		   a->submode == b->submode && a->flight_flag == b->flight_flag &&
		   a->dock_flags == b->dock_flags && a->ai_entry == b->ai_entry &&
		   (a->plan_state == 0) == (b->plan_state == 0) && a->target == b->target &&
		   a->attacker == b->attacker;
}

static TraceDamageState capture_damage(uint16_t obj_idx) {
	const CraftData* c = objects[obj_idx].craft_ptr;
	TraceDamageState state = {c->forward_shield, c->rear_shield, c->hull_damage, c->hull_max,
							  c->status_flags, c->working_subsystems, objects[obj_idx].death_timer,
							  c->flight_flag};
	return state;
}

static TraceMissionState capture_mission(void) {
	TraceMissionState state = {mission.end_flag, mission.player_status, mission.primary_complete,
							   mission.secondary_complete, mission.bonus_complete, {0}};
	memcpy(state.radio, mission.radiomsg_triggered, sizeof state.radio);
	return state;
}

static void emit_object_removed(uint16_t ref, const TraceObjectShadow* old,
								TieFlightTraceRemovalKind removal_kind) {
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_OBJECT_REMOVED, 50, 1);
	if (!payload)
		return;
	encode_shadow(payload, ref, old);
	put_u16(payload + 48, (uint16_t)removal_kind);
}

static void emit_current_object(uint16_t ref, TieFlightTraceRecordType type) {
	uint8_t* payload = begin_record(type, 48, 0);
	if (!payload)
		return;
	encode_object(payload, ref);
}

static int object_was_replaced(const TraceObjectShadow* old, const TraceObjectShadow* current) {
	return old->occupied && current->occupied &&
		   (old->id != current->id || old->species != current->species || old->genus != current->genus);
}

static void observe_flight_objects(void) {
	for (uint16_t i = 0; i < NUM_OBJECTS; ++i) {
		TraceObjectShadow current = capture_object(i);
		TraceObjectShadow* old = &trace.objects[i];
		const int replaced = object_was_replaced(old, &current);
		if (old->occupied && (!current.occupied || replaced))
			emit_object_removed(i, old,
							replaced ? TIE_TRACE_REMOVAL_REPLACED : TIE_TRACE_REMOVAL_CLEARED);
		if (current.occupied && (!old->occupied || replaced)) {
			current.generation = ++trace.object_generation[i];
			trace.pending_cause[i].valid = 0;
			emit_current_object(i, TIE_TRACE_RECORD_OBJECT_SPAWN);
		}
		*old = current;
	}
}

static void observe_static_objects(void) {
	for (uint16_t i = 0; i < NUM_STATIC_OBJECTS; ++i) {
		const uint16_t ref = (uint16_t)(OBJ_REF_STATIC_BASE + i);
		TraceObjectShadow current = capture_object(ref);
		TraceObjectShadow* old = &trace.statics[i];
		const int replaced = object_was_replaced(old, &current);
		if (old->occupied && (!current.occupied || replaced))
			emit_object_removed(ref, old,
							replaced ? TIE_TRACE_REMOVAL_REPLACED : TIE_TRACE_REMOVAL_CLEARED);
		if (current.occupied && (!old->occupied || replaced)) {
			current.generation = ++trace.static_generation[i];
			emit_current_object(ref, TIE_TRACE_RECORD_OBJECT_SPAWN);
		}
		*old = current;
	}
}

static void observe_objects(void) {
	observe_flight_objects();
	observe_static_objects();
}

static void encode_fg_state(uint8_t* dst, uint8_t fg_idx, const FGStatus* state) {
	dst[0] = fg_idx;
	dst[1] = state->active;
	dst[2] = state->waves_remaining;
	dst[3] = state->arrival_triggered;
	put_u16(dst + 4, state->arrival_delay);
	put_u16(dst + 6, state->world_position);
	for (unsigned int i = 0; i < 9; ++i) {
		dst[8 + i * 2] = state->cond[i].count;
		dst[9 + i * 2] = state->cond[i].detail;
		dst[26 + i * 2] = state->cond_id[i].count;
		dst[27 + i * 2] = state->cond_id[i].detail;
	}
	dst[44] = state->primary_status;
	dst[45] = state->secondary_status;
	dst[46] = state->fg_complete;
}

static void observe_mission_state(void) {
	for (uint8_t i = 0; i < trace.num_fg; ++i) {
		if (memcmp(&trace.fg[i], &fgstatus[i], sizeof(FGStatus)) != 0) {
			uint8_t* payload = begin_record(TIE_TRACE_RECORD_FG_STATE, 47, 0);
			if (payload)
				encode_fg_state(payload, i, &fgstatus[i]);
			trace.fg[i] = fgstatus[i];
		}
	}
	const TraceMissionState current = capture_mission();
	if (memcmp(&trace.mission, &current, sizeof current) != 0) {
		uint8_t* payload = begin_record(TIE_TRACE_RECORD_MISSION_STATE, 21, 0);
		if (payload) {
			payload[0] = current.end_flag;
			payload[1] = current.player_status;
			payload[2] = current.primary;
			payload[3] = current.secondary;
			payload[4] = current.bonus;
			memcpy(payload + 5, current.radio, sizeof current.radio);
		}
		trace.mission = current;
	}
}

static void hash_u32(uint32_t* hash, uint32_t value) {
	for (unsigned int i = 0; i < 4; ++i) {
		*hash ^= (uint8_t)(value >> (i * 8));
		*hash *= 16777619u;
	}
}

static uint32_t world_hash(void) {
	uint32_t hash = 2166136261u;
	for (uint16_t i = 0; i < NUM_OBJECTS; ++i) {
		const FlightObject* o = &objects[i];
		hash_u32(&hash, o->ship_idx);
		if (!o->ship_idx)
			continue;
		hash_u32(&hash, (uint32_t)o->idnumber | ((uint32_t)o->ship_idx << 16) |
						 ((uint32_t)o->genus << 24));
		hash_u32(&hash, (uint32_t)o->side | ((uint32_t)o->fg_idx << 8) |
						 ((uint32_t)(uint16_t)o->death_timer << 16));
		hash_u32(&hash, (uint32_t)o->world_x);
		hash_u32(&hash, (uint32_t)o->world_y);
		hash_u32(&hash, (uint32_t)o->world_z);
		hash_u32(&hash, (uint32_t)o->world_x_prev);
		hash_u32(&hash, (uint32_t)o->world_y_prev);
		hash_u32(&hash, (uint32_t)o->world_z_prev);
		hash_u32(&hash, (uint32_t)(uint16_t)o->pitch | ((uint32_t)(uint16_t)o->heading << 16));
		hash_u32(&hash, (uint32_t)(uint16_t)o->roll | ((uint32_t)(uint16_t)o->current_speed << 16));
		hash_u32(&hash, (uint32_t)(uint16_t)o->collision_radius);
		if (i < NUM_CRAFTS && o->ship_idx && o->craft_ptr) {
			const CraftData* c = o->craft_ptr;
			hash_u32(&hash, (uint32_t)c->current_order | ((uint32_t)c->mode_byte << 8) |
							 ((uint32_t)c->mode_subbyte << 16) | ((uint32_t)c->flight_flag << 24));
			hash_u32(&hash, (uint32_t)(uint16_t)c->ai_target_ref |
							 ((uint32_t)c->dock_state_flags << 16));
			hash_u32(&hash, (uint32_t)c->hull_damage | ((uint32_t)c->status_flags << 16));
			hash_u32(&hash, (uint32_t)(uint16_t)c->forward_shield |
							 ((uint32_t)(uint16_t)c->rear_shield << 16));
			hash_u32(&hash, (uint32_t)c->ai_plan_state | ((uint32_t)c->working_subsystems << 16));
			hash_u32(&hash, (uint32_t)c->maneuver_timer);
			hash_u32(&hash, (uint32_t)c->push_accum_x);
			hash_u32(&hash, (uint32_t)c->push_accum_y);
			hash_u32(&hash, (uint32_t)c->push_accum_z);
		} else if (i >= NUM_CRAFTS && i < WARHEAD_SLOT_END &&
				   (o->genus == GENUS_PROJECTILE_PLAYER || o->genus == GENUS_PROJECTILE_NPC)) {
			hash_u32(&hash, (uint32_t)warheads[i - NUM_CRAFTS].target_obj |
							 ((uint32_t)warheads[i - NUM_CRAFTS].homing_tier << 16));
		}
	}
	for (uint16_t i = 0; i < NUM_STATIC_OBJECTS; ++i) {
		const StaticObject* o = &staticobjects[i];
		hash_u32(&hash, o->species);
		if (!o->species)
			continue;
		hash_u32(&hash, (uint32_t)o->idnumber | ((uint32_t)o->species << 16) |
						 ((uint32_t)o->ship_class << 24));
		hash_u32(&hash, (uint32_t)(uint16_t)o->world_x | ((uint32_t)(uint16_t)o->world_y << 16));
		hash_u32(&hash, (uint32_t)(uint16_t)o->world_z | ((uint32_t)o->fg_idx << 16));
		hash_u32(&hash, o->status_flags);
	}
	for (uint8_t i = 0; i < trace.num_fg; ++i) {
		const FGStatus* fg = &fgstatus[i];
		hash_u32(&hash, (uint32_t)fg->active | ((uint32_t)fg->waves_remaining << 8) |
						 ((uint32_t)fg->arrival_triggered << 16));
		hash_u32(&hash, (uint32_t)fg->arrival_delay | ((uint32_t)fg->world_position << 16));
		for (unsigned int j = 0; j < 9; ++j)
			hash_u32(&hash, (uint32_t)fg->cond[j].count | ((uint32_t)fg->cond[j].detail << 8) |
							 ((uint32_t)fg->cond_id[j].count << 16) |
							 ((uint32_t)fg->cond_id[j].detail << 24));
		hash_u32(&hash, (uint32_t)fg->primary_status | ((uint32_t)fg->secondary_status << 8) |
						 ((uint32_t)fg->fg_complete << 16));
	}
	const TraceMissionState state = capture_mission();
	for (unsigned int i = 0; i < sizeof state; ++i) {
		hash ^= ((const uint8_t*)&state)[i];
		hash *= 16777619u;
	}
	return hash;
}

static void emit_ai_change(uint16_t obj_idx, const TraceAiState* before, const TraceAiState* after,
						   uint8_t transition_opcode) {
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_AI_CHANGE, 83, 1);
	if (!payload)
		return;
	encode_object(payload, obj_idx);
	payload[48] = before->order;
	payload[49] = after->order;
	payload[50] = before->default_order;
	payload[51] = after->default_order;
	payload[52] = before->mode;
	payload[53] = after->mode;
	payload[54] = before->submode;
	payload[55] = after->submode;
	payload[56] = before->flight_flag;
	payload[57] = after->flight_flag;
	payload[58] = before->dock_flags;
	payload[59] = after->dock_flags;
	payload[60] = before->ai_entry;
	payload[61] = after->ai_entry;
	put_u16(payload + 62, before->target);
	put_u16(payload + 64, after->target);
	put_u16(payload + 66, before->attacker);
	put_u16(payload + 68, after->attacker);
	put_u16(payload + 70, before->plan_state);
	put_u16(payload + 72, after->plan_state);
	put_u32(payload + 74, (uint32_t)before->timer);
	put_u32(payload + 78, (uint32_t)after->timer);
	payload[82] = transition_opcode;
}

static void observe_ai(void) {
	for (uint16_t i = 0; i < NUM_CRAFTS; ++i) {
		if (!objects[i].ship_idx || !objects[i].craft_ptr) {
			trace.ai_observed_valid[i] = 0;
			continue;
		}
		const TraceAiState current = capture_ai(i);
		if (trace.ai_observed_valid[i] && !ai_semantic_equal(&trace.ai_observed[i], &current))
			emit_ai_change(i, &trace.ai_observed[i], &current, 0);
		trace.ai_observed[i] = current;
		trace.ai_observed_valid[i] = 1;
	}
}

static uint32_t mission_checksum(const char* mission_path) {
	if (!mission_path)
		return 0;
	TieFile* file = TieStorage_Open(TIE_FILE_ROOT_FLIGHT_ASSET, mission_path, "rb");
	if (!file)
		return 0;
	uint32_t hash = 2166136261u;
	uint8_t buffer[4096];
	for (;;) {
		const size_t count = TieStorage_Read(buffer, 1, sizeof buffer, file);
		for (size_t i = 0; i < count; ++i) {
			hash ^= buffer[i];
			hash *= 16777619u;
		}
		if (count != sizeof buffer)
			break;
	}
	TieStorage_Close(file);
	return hash;
}

static void set_pending_cause(uint16_t victim_ref, TieFlightTraceCause cause, uint16_t responsible_ref) {
	if (victim_ref >= NUM_OBJECTS)
		return;
	TracePendingCause* pending = &trace.pending_cause[victim_ref];
	if (pending->terminal && pending->victim_generation == trace.object_generation[victim_ref])
		return;
	pending->valid = 1;
	pending->terminal = 0;
	pending->victim_generation = trace.object_generation[victim_ref];
	pending->cause = (uint8_t)cause;
	pending->responsible = capture_identity(responsible_ref);
}

static void freeze_pending_cause(uint16_t victim_ref, TieFlightTraceCause cause,
								 const TraceIdentity* responsible) {
	if (victim_ref >= NUM_OBJECTS)
		return;
	TracePendingCause* pending = &trace.pending_cause[victim_ref];
	pending->valid = 1;
	pending->terminal = 1;
	pending->victim_generation = trace.object_generation[victim_ref];
	pending->cause = (uint8_t)cause;
	pending->responsible = *responsible;
}

static TieFlightTraceCause resolve_cause(uint16_t victim_ref, uint16_t attacker_ref,
									 TraceIdentity* responsible) {
	*responsible = capture_identity(attacker_ref);
	if (victim_ref < NUM_OBJECTS) {
		const TracePendingCause* pending = &trace.pending_cause[victim_ref];
		if (pending->valid && pending->victim_generation == trace.object_generation[victim_ref]) {
			*responsible = pending->responsible;
			return (TieFlightTraceCause)pending->cause;
		}
	}
	if (attacker_ref >= OBJ_REF_STATIC_BASE && attacker_ref < OBJ_REF_STATIC_BASE + NUM_STATIC_OBJECTS)
		return TIE_TRACE_CAUSE_STATIC_COLLISION;
	if (attacker_ref < NUM_OBJECTS &&
		(objects[attacker_ref].genus == GENUS_PROJECTILE_PLAYER ||
		 objects[attacker_ref].genus == GENUS_PROJECTILE_NPC)) {
		*responsible = capture_identity((uint16_t)objects[attacker_ref].self_idx);
		return TIE_TRACE_CAUSE_PROJECTILE;
	}
	return TIE_TRACE_CAUSE_UNKNOWN;
}

static int parse_trace_filename(const char* name, uint64_t* timestamp) {
	const size_t prefix_length = sizeof TRACE_FILE_PREFIX - 1;
	const size_t suffix_length = sizeof TRACE_FILE_SUFFIX - 1;
	const size_t name_length = name ? strlen(name) : 0;
	if (name_length <= prefix_length + suffix_length ||
		memcmp(name, TRACE_FILE_PREFIX, prefix_length) != 0 ||
		memcmp(name + name_length - suffix_length, TRACE_FILE_SUFFIX, suffix_length) != 0)
		return 0;

	const char* separator = strrchr(name, '-');
	const char* end = name + name_length - suffix_length;
	if (!separator || separator <= name + prefix_length || separator + 1 == end)
		return 0;

	uint64_t value = 0;
	for (const char* digit = separator + 1; digit < end; ++digit) {
		if (*digit < '0' || *digit > '9')
			return 0;
		const uint8_t numeric = (uint8_t)(*digit - '0');
		if (value > (UINT64_MAX - numeric) / 10u)
			return 0;
		value = value * 10u + numeric;
	}
	*timestamp = value;
	return 1;
}

static int trace_file_is_newer(const TraceSavedFile* left, const TraceSavedFile* right) {
	if (left->current != right->current)
		return left->current > right->current;
	if (left->timestamp != right->timestamp)
		return left->timestamp > right->timestamp;
	return strcmp(left->name, right->name) > 0;
}

static void remove_old_trace(const char* name) {
	if (TieStorage_Remove(TIE_FILE_ROOT_USER, name) != 0)
		TieDiagnostics_Log(TIE_LOG_WARN, "Could not remove old flight trace %s\n", name);
}

static void prune_old_traces(const char* current_filename) {
	TieDir* directory = TieStorage_DirOpen(TIE_FILE_ROOT_USER, ".");
	if (!directory) {
		TieDiagnostics_Log(TIE_LOG_WARN, "Could not enumerate flight traces for retention\n");
		return;
	}

	TraceSavedFile retained[TRACE_RETAIN_COUNT];
	size_t retained_count = 0;
	TieDirEntry entry;
	while (TieStorage_DirNext(directory, &entry)) {
		TraceSavedFile candidate;
		if (entry.is_dir || !parse_trace_filename(entry.name, &candidate.timestamp))
			continue;
		snprintf(candidate.name, sizeof candidate.name, "%s", entry.name);
		candidate.current = strcmp(entry.name, current_filename) == 0;

		if (retained_count < TRACE_RETAIN_COUNT) {
			retained[retained_count++] = candidate;
			continue;
		}

		size_t oldest = 0;
		for (size_t i = 1; i < retained_count; ++i) {
			if (trace_file_is_newer(&retained[oldest], &retained[i]))
				oldest = i;
		}
		if (trace_file_is_newer(&candidate, &retained[oldest])) {
			remove_old_trace(retained[oldest].name);
			retained[oldest] = candidate;
		} else {
			remove_old_trace(candidate.name);
		}
	}
	TieStorage_DirClose(directory);
}

void TieFlightTrace_BeginMission(const char* mission_path) {
	memset(&trace, 0, sizeof trace);
	trace.active = 1;
	trace.phase = TIE_TRACE_PHASE_SETUP;
	trace.used = TIE_FLIGHT_TRACE_HEADER_SIZE;
	trace.start_time = (uint64_t)time(NULL);
	trace.num_fg = mission_file_header.num_fg > 48 ? 48 : (uint8_t)mission_file_header.num_fg;
	memcpy(trace.data + TIE_TRACE_HDR_MAGIC, TIE_FLIGHT_TRACE_MAGIC, 8);
	put_u16(trace.data + TIE_TRACE_HDR_VERSION, TIE_FLIGHT_TRACE_VERSION);
	put_u16(trace.data + TIE_TRACE_HDR_HEADER_SIZE, TIE_FLIGHT_TRACE_HEADER_SIZE);
	put_u32(trace.data + TIE_TRACE_HDR_CAPACITY, TRACE_CAPACITY);
	put_u32(trace.data + TIE_TRACE_HDR_PROFILE, (uint32_t)TieProfile_Flight()->version);
	put_u32(trace.data + TIE_TRACE_HDR_DIFFICULTY, mission.difficulty);
	put_u16(trace.data + TIE_TRACE_HDR_INITIAL_RNG, (uint16_t)math2_randomseed);
	put_u16(trace.data + TIE_TRACE_HDR_NUM_FG, trace.num_fg);
	put_u64(trace.data + TIE_TRACE_HDR_START_TIME, trace.start_time);
	put_u32(trace.data + TIE_TRACE_HDR_MISSION_CHECKSUM, mission_checksum(mission_path));
	put_u32(trace.data + TIE_TRACE_HDR_TIMING_MODE, (uint32_t)TieProfile_Flight()->update_rate);
	if (mission_path)
		snprintf((char*)trace.data + TIE_TRACE_HDR_MISSION, 64, "%s", mission_path);
	snprintf((char*)trace.data + TIE_TRACE_HDR_BUILD, 32, "%s", TIE_VERSION_STRING);
	for (uint8_t i = 0; i < trace.num_fg; ++i) {
		uint8_t* payload = begin_record(TIE_TRACE_RECORD_FG_DEF, 16, 0);
		if (!payload)
			break;
		payload[0] = i;
		payload[1] = fg_array[i].species;
		payload[2] = fg_array[i].side;
		payload[3] = fg_array[i].count;
		memcpy(payload + 4, fg_array[i].name, TIE_FLIGHT_TRACE_FG_NAME_SIZE);
	}
}

void TieFlightTrace_MissionCreated(void) {
	if (!trace.active)
		return;
	memset(trace.fg, 0xFF, sizeof trace.fg);
	memset(&trace.mission, 0xFF, sizeof trace.mission);
	TieFlightTrace_ObserveState();
}

static void finish_trace(uint32_t header_flags) {
	if (!trace.active)
		return;
	trace.phase = TIE_TRACE_PHASE_END_FRAME;
	TieFlightTrace_ObserveState();
	put_u32(trace.data + TIE_TRACE_HDR_FLAGS, header_flags);
	put_u32(trace.data + TIE_TRACE_HDR_BYTES_USED, trace.used);
	put_u32(trace.data + TIE_TRACE_HDR_RECORD_COUNT, trace.records);
	put_u32(trace.data + TIE_TRACE_HDR_DROPPED, trace.dropped);
	put_u32(trace.data + TIE_TRACE_HDR_DROPPED_CRITICAL, trace.dropped_critical);
	char mission_name[48] = "mission";
	const char* full = (const char*)trace.data + TIE_TRACE_HDR_MISSION;
	const char* base = strrchr(full, '/');
	const char* windows_base = strrchr(full, '\\');
	if (windows_base && (!base || windows_base > base))
		base = windows_base;
	base = base ? base + 1 : full;
	if (*base)
		snprintf(mission_name, sizeof mission_name, "%s", base);
	char* dot = strrchr(mission_name, '.');
	if (dot)
		*dot = '\0';
	char filename[96];
	snprintf(filename, sizeof filename, TRACE_FILE_PREFIX "%s-%llu" TRACE_FILE_SUFFIX, mission_name,
			 (unsigned long long)trace.start_time);
	const int result = TieStorage_WriteAllAtomic(TIE_FILE_ROOT_USER, filename, trace.data, trace.used);
	if (result == 0) {
		TieDiagnostics_Log(TIE_LOG_INFO,
					   "Flight trace saved as %s (%u records, %u dropped, %u critical dropped)\n",
					   filename, trace.records, trace.dropped, trace.dropped_critical);
		prune_old_traces(filename);
	} else {
		TieDiagnostics_Log(TIE_LOG_ERROR, "Could not save flight trace %s\n", filename);
	}
	trace.active = 0;
}

void TieFlightTrace_EndMission(void) { finish_trace(TIE_TRACE_HEADER_FLAG_MISSION_ENDED); }

void TieFlightTrace_Shutdown(void) { finish_trace(TIE_TRACE_HEADER_FLAG_APPLICATION_SHUTDOWN); }

void TieFlightTrace_BeginFrame(uint16_t frame_ticks, uint16_t frame_rate) {
	if (!trace.active)
		return;
	++trace.frame;
	trace.phase = TIE_TRACE_PHASE_TIME;
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_FRAME, 20, 0);
	if (!payload)
		return;
	put_u16(payload, frame_ticks);
	put_u16(payload + 2, frame_rate);
	payload[4] = _date.hour;
	payload[5] = _date.minute;
	payload[6] = _date.second;
	payload[7] = 0;
	put_u16(payload + 8, (uint16_t)_date.subsec);
	put_u16(payload + 10, (uint16_t)math2_randomseed);
	put_u32(payload + 12, world_hash());
	put_u32(payload + 16, (uint32_t)mission.mission_score);
}

void TieFlightTrace_SetPhase(TieFlightTracePhase phase) { trace.phase = phase; }

void TieFlightTrace_ObserveState(void) {
	if (!trace.active)
		return;
	observe_objects();
	observe_mission_state();
	observe_ai();
}

void TieFlightTrace_EndFrame(void) {
	if (!trace.active)
		return;
	trace.phase = TIE_TRACE_PHASE_END_FRAME;
	TieFlightTrace_ObserveState();
}

void TieFlightTrace_AiBefore(uint16_t obj_idx) {
	if (!trace.active || obj_idx >= NUM_CRAFTS || !objects[obj_idx].craft_ptr)
		return;
	trace.ai_before[obj_idx] = capture_ai(obj_idx);
	trace.ai_before_valid[obj_idx] = 1;
}

void TieFlightTrace_AiAfter(uint16_t obj_idx, uint8_t transition_opcode) {
	if (!trace.active || obj_idx >= NUM_CRAFTS || !trace.ai_before_valid[obj_idx])
		return;
	const TraceAiState after = capture_ai(obj_idx);
	const TraceAiState* before = &trace.ai_before[obj_idx];
	trace.ai_before_valid[obj_idx] = 0;
	if (!ai_semantic_equal(before, &after))
		emit_ai_change(obj_idx, before, &after, transition_opcode);
	trace.ai_observed[obj_idx] = after;
	trace.ai_observed_valid[obj_idx] = 1;
}

void TieFlightTrace_Board(uint16_t actor_ref, uint16_t target_ref, TieFlightTraceBoardKind kind,
						  uint8_t order) {
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_BOARD, 110, 1);
	if (!payload)
		return;
	encode_object(payload, actor_ref);
	encode_object(payload + 48, target_ref);
	payload[96] = (uint8_t)kind;
	payload[97] = order;
	if (actor_ref < NUM_CRAFTS && objects[actor_ref].craft_ptr) {
		put_u32(payload + 98, (uint32_t)objects[actor_ref].craft_ptr->push_accum_x);
		put_u32(payload + 102, (uint32_t)objects[actor_ref].craft_ptr->push_accum_y);
		put_u32(payload + 106, (uint32_t)objects[actor_ref].craft_ptr->push_accum_z);
	}
}

void TieFlightTrace_WeaponSpawn(uint16_t projectile_ref, uint16_t shooter_ref, uint16_t target_ref) {
	if (!trace.active || projectile_ref >= NUM_OBJECTS)
		return;
	TraceObjectShadow* old = &trace.objects[projectile_ref];
	if (old->occupied)
		emit_object_removed(projectile_ref, old, TIE_TRACE_REMOVAL_REPLACED);
	++trace.object_generation[projectile_ref];
	trace.pending_cause[projectile_ref].valid = 0;
	TraceObjectShadow current = capture_object(projectile_ref);
	current.generation = trace.object_generation[projectile_ref];
	*old = current;
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_WEAPON, 72, 0);
	if (!payload)
		return;
	encode_shadow(payload, projectile_ref, &current);
	const TraceIdentity shooter = capture_identity(shooter_ref);
	const TraceIdentity target = capture_identity(target_ref);
	encode_identity(payload + 48, &shooter);
	encode_identity(payload + 60, &target);
}

void TieFlightTrace_TargetChange(uint16_t obj_ref, uint16_t old_target_ref, uint16_t new_target_ref) {
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_TARGET_CHANGE, 72, 1);
	if (!payload)
		return;
	encode_object(payload, obj_ref);
	const TraceIdentity old_target = capture_identity(old_target_ref);
	const TraceIdentity new_target = capture_identity(new_target_ref);
	encode_identity(payload + 48, &old_target);
	encode_identity(payload + 60, &new_target);
	if (obj_ref < NUM_OBJECTS)
		trace.objects[obj_ref].target = new_target_ref;
}

void TieFlightTrace_Collision(uint16_t actor_ref, uint16_t target_ref, TieFlightTraceCollisionKind kind,
						  int16_t component) {
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_COLLISION, 100, 1);
	if (payload) {
		encode_object(payload, actor_ref);
		encode_object(payload + 48, target_ref);
		payload[96] = (uint8_t)kind;
		payload[97] = 0;
		put_u16(payload + 98, (uint16_t)component);
	}
	if (kind == TIE_TRACE_COLLISION_CRAFT) {
		set_pending_cause(actor_ref, TIE_TRACE_CAUSE_CRAFT_COLLISION, target_ref);
		set_pending_cause(target_ref, TIE_TRACE_CAUSE_CRAFT_COLLISION, actor_ref);
	} else if (kind == TIE_TRACE_COLLISION_PROJECTILE) {
		uint16_t owner_ref = actor_ref;
		if (actor_ref < NUM_OBJECTS)
			owner_ref = (uint16_t)objects[actor_ref].self_idx;
		set_pending_cause(actor_ref, TIE_TRACE_CAUSE_PROJECTILE, owner_ref);
		set_pending_cause(target_ref, TIE_TRACE_CAUSE_PROJECTILE, owner_ref);
	} else if (kind == TIE_TRACE_COLLISION_STATIC) {
		set_pending_cause(actor_ref, TIE_TRACE_CAUSE_STATIC_COLLISION, target_ref);
	}
}

void TieFlightTrace_DamageBefore(uint16_t target_ref) {
	if (!trace.active || target_ref >= NUM_CRAFTS || !objects[target_ref].craft_ptr)
		return;
	trace.damage_before[target_ref] = capture_damage(target_ref);
	trace.damage_before_valid[target_ref] = 1;
}

void TieFlightTrace_DamageAfter(uint16_t target_ref, uint16_t attacker_ref, int16_t component,
							int16_t damage) {
	if (!trace.active || target_ref >= NUM_CRAFTS || !trace.damage_before_valid[target_ref])
		return;
	const TraceDamageState after = capture_damage(target_ref);
	const TraceDamageState* before = &trace.damage_before[target_ref];
	trace.damage_before_valid[target_ref] = 0;
	TraceIdentity responsible;
	const TieFlightTraceCause cause = resolve_cause(target_ref, attacker_ref, &responsible);
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_DAMAGE, 106, 1);
	if (!payload)
		return;
	encode_object(payload, target_ref);
	const TraceIdentity attacker = capture_identity(attacker_ref);
	encode_identity(payload + 48, &attacker);
	encode_identity(payload + 60, &responsible);
	payload[72] = (uint8_t)cause;
	payload[73] = 0;
	put_u16(payload + 74, (uint16_t)component);
	put_u16(payload + 76, (uint16_t)damage);
	put_u16(payload + 78, (uint16_t)before->front_shield);
	put_u16(payload + 80, (uint16_t)after.front_shield);
	put_u16(payload + 82, (uint16_t)before->rear_shield);
	put_u16(payload + 84, (uint16_t)after.rear_shield);
	put_u16(payload + 86, before->hull_damage);
	put_u16(payload + 88, after.hull_damage);
	put_u16(payload + 90, after.hull_max);
	put_u16(payload + 92, before->status);
	put_u16(payload + 94, after.status);
	put_u16(payload + 96, before->working);
	put_u16(payload + 98, after.working);
	put_u16(payload + 100, (uint16_t)before->death_timer);
	put_u16(payload + 102, (uint16_t)after.death_timer);
	payload[104] = before->flight_flag;
	payload[105] = after.flight_flag;
}

void TieFlightTrace_Death(uint16_t target_ref, uint16_t attacker_ref, TieFlightTraceDeathKind kind,
						  int16_t timer) {
	TraceIdentity responsible;
	TieFlightTraceCause cause = resolve_cause(target_ref, attacker_ref, &responsible);
	if (kind == TIE_TRACE_DEATH_EJECTED) {
		cause = TIE_TRACE_CAUSE_EJECTED;
		responsible = capture_identity(attacker_ref);
	}
	freeze_pending_cause(target_ref, cause, &responsible);
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_DEATH, 64, 1);
	if (!payload)
		return;
	encode_object(payload, target_ref);
	encode_identity(payload + 48, &responsible);
	put_u16(payload + 60, (uint16_t)timer);
	payload[62] = (uint8_t)kind;
	payload[63] = (uint8_t)cause;
}

void TieFlightTrace_Explosion(uint16_t obj_ref, uint8_t variant) {
	TraceIdentity responsible;
	const TieFlightTraceCause cause = resolve_cause(obj_ref, TRACE_INVALID_REF, &responsible);
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_EXPLOSION, 62, 1);
	if (!payload)
		return;
	encode_object(payload, obj_ref);
	encode_identity(payload + 48, &responsible);
	payload[60] = (uint8_t)cause;
	payload[61] = variant;
}

void TieFlightTrace_FgExit(uint16_t obj_ref, uint16_t exit_kind) {
	if (!trace.active)
		return;
	uint8_t* payload = begin_record(TIE_TRACE_RECORD_FG_EXIT, 50, 1);
	if (!payload)
		return;
	encode_object(payload, obj_ref);
	put_u16(payload + 48, exit_kind);
}

#else

typedef int tie_flight_trace_disabled_translation_unit;

#endif
