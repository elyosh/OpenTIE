#ifndef TIE_FLIGHT_TRACE_H
#define TIE_FLIGHT_TRACE_H

#include "tie_runtime/diagnostics/flight_trace_format.h"

#include <stdint.h>

#if defined(TIE_ENABLE_FLIGHT_TRACE)

void TieFlightTrace_BeginMission(const char* mission_path);
void TieFlightTrace_MissionCreated(void);
void TieFlightTrace_EndMission(void);
void TieFlightTrace_Shutdown(void);
void TieFlightTrace_BeginFrame(uint16_t frame_ticks, uint16_t frame_rate);
void TieFlightTrace_SetPhase(TieFlightTracePhase phase);
void TieFlightTrace_ObserveState(void);
void TieFlightTrace_EndFrame(void);

void TieFlightTrace_AiBefore(uint16_t obj_idx);
void TieFlightTrace_AiAfter(uint16_t obj_idx, uint8_t transition_opcode);
void TieFlightTrace_Board(uint16_t actor_ref, uint16_t target_ref, TieFlightTraceBoardKind kind,
						  uint8_t order);
void TieFlightTrace_WeaponSpawn(uint16_t projectile_ref, uint16_t shooter_ref, uint16_t target_ref);
void TieFlightTrace_TargetChange(uint16_t obj_ref, uint16_t old_target_ref, uint16_t new_target_ref);
void TieFlightTrace_Collision(uint16_t actor_ref, uint16_t target_ref, TieFlightTraceCollisionKind kind,
						  int16_t component);
void TieFlightTrace_DamageBefore(uint16_t target_ref);
void TieFlightTrace_DamageAfter(uint16_t target_ref, uint16_t attacker_ref, int16_t component,
							int16_t damage);
void TieFlightTrace_Death(uint16_t target_ref, uint16_t attacker_ref, TieFlightTraceDeathKind kind,
						  int16_t timer);
void TieFlightTrace_Explosion(uint16_t obj_ref, uint8_t variant);
void TieFlightTrace_FgExit(uint16_t obj_ref, uint16_t exit_kind);

#define TIE_FLIGHT_TRACE_BEGIN_MISSION(path) TieFlightTrace_BeginMission(path)
#define TIE_FLIGHT_TRACE_MISSION_CREATED() TieFlightTrace_MissionCreated()
#define TIE_FLIGHT_TRACE_END_MISSION() TieFlightTrace_EndMission()
#define TIE_FLIGHT_TRACE_SHUTDOWN() TieFlightTrace_Shutdown()
#define TIE_FLIGHT_TRACE_BEGIN_FRAME(ticks, rate) TieFlightTrace_BeginFrame((ticks), (rate))
#define TIE_FLIGHT_TRACE_PHASE(phase) TieFlightTrace_SetPhase(phase)
#define TIE_FLIGHT_TRACE_OBSERVE_STATE() TieFlightTrace_ObserveState()
#define TIE_FLIGHT_TRACE_END_FRAME() TieFlightTrace_EndFrame()
#define TIE_FLIGHT_TRACE_AI_BEFORE(ref) TieFlightTrace_AiBefore(ref)
#define TIE_FLIGHT_TRACE_AI_AFTER(ref, opcode) TieFlightTrace_AiAfter((ref), (opcode))
#define TIE_FLIGHT_TRACE_BOARD(actor, target, kind, order) \
	TieFlightTrace_Board((actor), (target), (kind), (order))
#define TIE_FLIGHT_TRACE_WEAPON_SPAWN(projectile, shooter, target) \
	TieFlightTrace_WeaponSpawn((projectile), (shooter), (target))
#define TIE_FLIGHT_TRACE_TARGET_CHANGE(object, old_target, new_target) \
	TieFlightTrace_TargetChange((object), (old_target), (new_target))
#define TIE_FLIGHT_TRACE_COLLISION(actor, target, kind, component) \
	TieFlightTrace_Collision((actor), (target), (kind), (component))
#define TIE_FLIGHT_TRACE_DAMAGE_BEFORE(target) TieFlightTrace_DamageBefore(target)
#define TIE_FLIGHT_TRACE_DAMAGE_AFTER(target, attacker, component, damage) \
	TieFlightTrace_DamageAfter((target), (attacker), (component), (damage))
#define TIE_FLIGHT_TRACE_DEATH(target, attacker, kind, timer) \
	TieFlightTrace_Death((target), (attacker), (kind), (timer))
#define TIE_FLIGHT_TRACE_EXPLOSION(ref, variant) TieFlightTrace_Explosion((ref), (variant))
#define TIE_FLIGHT_TRACE_FG_EXIT(ref, kind) TieFlightTrace_FgExit((ref), (kind))

#else

#define TIE_FLIGHT_TRACE_BEGIN_MISSION(path) ((void)0)
#define TIE_FLIGHT_TRACE_MISSION_CREATED() ((void)0)
#define TIE_FLIGHT_TRACE_END_MISSION() ((void)0)
#define TIE_FLIGHT_TRACE_SHUTDOWN() ((void)0)
#define TIE_FLIGHT_TRACE_BEGIN_FRAME(ticks, rate) ((void)0)
#define TIE_FLIGHT_TRACE_PHASE(phase) ((void)0)
#define TIE_FLIGHT_TRACE_OBSERVE_STATE() ((void)0)
#define TIE_FLIGHT_TRACE_END_FRAME() ((void)0)
#define TIE_FLIGHT_TRACE_AI_BEFORE(ref) ((void)0)
#define TIE_FLIGHT_TRACE_AI_AFTER(ref, opcode) ((void)sizeof(opcode))
#define TIE_FLIGHT_TRACE_BOARD(actor, target, kind, order) ((void)0)
#define TIE_FLIGHT_TRACE_WEAPON_SPAWN(projectile, shooter, target) ((void)0)
#define TIE_FLIGHT_TRACE_TARGET_CHANGE(object, old_target, new_target) ((void)0)
#define TIE_FLIGHT_TRACE_COLLISION(actor, target, kind, component) ((void)0)
#define TIE_FLIGHT_TRACE_DAMAGE_BEFORE(target) ((void)0)
#define TIE_FLIGHT_TRACE_DAMAGE_AFTER(target, attacker, component, damage) ((void)0)
#define TIE_FLIGHT_TRACE_DEATH(target, attacker, kind, timer) ((void)0)
#define TIE_FLIGHT_TRACE_EXPLOSION(ref, variant) ((void)0)
#define TIE_FLIGHT_TRACE_FG_EXIT(ref, kind) ((void)0)

#endif

#endif
