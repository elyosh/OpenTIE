#ifndef __FSFX_H__
#define __FSFX_H__

#include <stdint.h>

/* In-flight SFX and voice dispatcher. Sound-ID map:
 *   [4..50]    main RMAP sound bank
 *   [51..106]  voice clips (radio chatter, orders, UI phrases)
 *   [107..108] SFXDOE.LFD extras
 *   TIE95: [109..127] mission voice cues
 *   TIE98: [109..110] player engine loops, [111..129] mission voice cues
 *
 * iMUSE resolves integer sound IDs through soundhandles; NULL means unloaded. */

void fsfx_allocsfxbuffer(void);

/* Stop TIE98 engine loops and free loaded samples. */
void fsfx_freesfx(void);

/* Load an RMAP bank and SFXDOE.LFD. Both contain a 16-byte header, 16-byte
 * directory records { BE tag, name[8], size }, then packed samples. */
int16_t fsfx_loadsfx(const char* filename);

/* Load active radio, win, and loss clips from VOICE\<NAME>\<NAME>.LFD into
 * the edition-specific 19-slot mission range. Training and I/O failure clear it. */
int16_t fsfx_loadvoicelfd(void);

/* Trigger a positioned one-shot. Player-local sounds receive priority 126;
 * other sounds cap at 125. Active looping IDs 0x2A..0x2F are not restarted. */
int8_t fsfx_triggersfx(uint16_t sound_id, uint16_t src_obj);

/*
 * Fire the appropriate laser/missile variant based on the firing
 * FlightObject's ship_idx (weapon-species 0x89..0x9A).
 *   0x89..0x93 -> sfx 4..14   (standard lasers)
 *   0x94..0x95 -> sfx 10..11  (heavy lasers)
 *   0x96..0x97 -> sfx 15..16  (ion)
 *   0x98..0x9A -> sfx 17      (missiles/torps)
 */
int8_t fsfx_triggerlasersfx(uint16_t object_idx);

/*
 * Gunsight tone dispatch. mode 0/1 stops both gunsight channels;
 * mode 3 = red lock (sfx 36, stops sfx 35); other = green lock
 * (sfx 35, stops sfx 36). Guards against duplicate triggers.
 */
int32_t fsfx_triggergunsightsfx(int16_t mode);

/*
 * Sustained beam-weapon SFX. firing==0 stops both beam channels.
 * firing!=0 plays sfx 37 (no target) or 38 (bluetarget != 0xFFFF).
 * Stops the other channel first.
 */
int8_t fsfx_triggerbeamsfx(int32_t firing);

/*
 * Radio-voice trigger. If currentdigital is busy the clip is
 * queued on blastqueue (up to 32 entries). Otherwise it starts
 * immediately at full volume, centered pan, max priority.
 * Returns 1 when the clip is started or queued.
 */
int8_t fsfx_triggervoicesfx(uint16_t voice_id);

/*
 * Per-frame voice-queue drainer. Called by TIE_doframe. Starts
 * the next blastqueue entry when currentdigital is idle.
 */
void fsfx_checkblastqueue(void);

/*
 * Per-frame flyby detector. Called by TIE_doframe. Triggers a
 * species-specific flyby SFX when a moving craft crosses into
 * (spec.bound_hwidth + 1024) range of the player. Returns the
 * loop-terminator value (28).
 */
int16_t fsfx_checktieflyby(void);

/*
 * Returns nonzero if an ally (object on the player's side) is
 * present. Used to gate random voice callouts (congrats / kills).
 */
int8_t fsfx_speakeravailable(void);

/* Queue the localized group prefix and wing number for an object's FG name.
 * Voice clips exist only for wing numbers one and two. */
int8_t fsfx_speakobjectname(uint16_t object_idx, uint16_t prefix_voice);

/*
 * Random congratulatory callout. Plays one of 3 kudos clips (76..78),
 * one of 3 exclamations (79..81), and optionally (50%) appends
 * the player's object name.
 */
int8_t fsfx_speakcongrats(void);

/*
 * Speak an order/operation (random phrasing). verb_voice==63 picks
 * between two 'prefix + order + verb' or 'order + suffix' variants;
 * otherwise always uses 'order + suffix (0x3E) + verb'.
 */
int8_t fsfx_speakoperation(uint16_t order_voice, uint16_t verb_voice);

/*
 * Mission-objective callout. 50% chance for a kudos prefix + 'objective'
 * + player name. Always speaks objective_voice, then 'completed' (93),
 * 'mission' (95), and a 25% chance 'update' (96).
 */
int8_t fsfx_speakobjectives(uint16_t objective_voice);

/*
 * Wingman order acknowledgment. Three random tiers (terse/medium/long),
 * then an order-specific tail clip: p->73 protect, q->74 pursue,
 * s->71 strafe, t->72 target, u->69 unknown, v->70 or 100 (50/50).
 * cmdr_mode selects 'target' vs 'enemy' prefix on the final name clip.
 */
int8_t fsfx_speakorderack(int32_t target_idx, int32_t order_char, uint16_t cmdr_mode);

/*
 * Mission-critical kill announcer. Checks if the destroyed craft
 * belongs to any of the 3 active goal records. On match: speak
 * player name + 'critical' (0x55) + 'platform destroyed' (87) or
 * 'craft destroyed' (86) + the caller-supplied action voice.
 * Suppressed once mission.primary_complete == 1. Returns 1 if
 * the announcement was voiced.
 */
int32_t fsfx_checkcriticalcraft(int32_t object_idx, uint16_t action_voice);

/*
 * FSFX-owned positional audio primitives (also used internally).
 *
 * fsfx_calcvolume: 4-tier distance attenuation. 0xFFFF means
 * 'local/player' -> base volume. For positioned sounds: out of
 * range (0), far (base/8), medium (base/4), near (linear interp,
 * capped 127). sounds with id < 0x33 use sounddist[id] and
 * fullvolume[id]; others use a fixed (0x2000, 112).
 */
int16_t fsfx_calcvolume(uint16_t src_obj, uint16_t sound_id);

/*
 * fsfx_calcpan: pan in [0..127], 64 = center. Transforms the
 * object to eye space via the worldeye rotation, takes arctan of
 * (eye_x, eye_z). For sounds in the back hemisphere (|pan|>=90
 * deg) reduces *volume_ptr using a 2D "distance-from-180"
 * attenuation, then mirrors the pan back to the front quadrant.
 */
int32_t fsfx_calcpan(uint16_t src_obj, int16_t* volume_ptr);

/* FSFX-owned handle table and voice queue state. */
#define FSFX_NUM_SOUND_HANDLES 131
#define FSFX_TIE95_SOUND_TABLE_COUNT 128
#define FSFX_TIE98_SOUND_TABLE_COUNT 131
#define FSFX_TIE95_MISSION_VOICE_BASE 109
#define FSFX_TIE98_MISSION_VOICE_BASE 111
#define FSFX_PLAYER_ENGINE_TIE_ID 109
#define FSFX_PLAYER_ENGINE_REBEL_ID 110
#define FSFX_MISSION_VOICE_COUNT 19
#define FSFX_SOUND_NAME_CAPACITY 24
#define FSFX_MISSION_VOICE_PRIMARY 16
#define FSFX_MISSION_VOICE_SECONDARY 17
#define FSFX_MISSION_VOICE_LOSS 18
#define FSFX_BLAST_QUEUE_SIZE 32
#define FSFX_NUM_DIST_ENTRIES 55

extern void* soundhandles[FSFX_NUM_SOUND_HANDLES];
extern uint8_t currentdigital;
extern uint16_t soundhandleinit;

/* Per-mission voice-filename inputs. Populated by fediskio_createpilotrecord
 * from the .tfr pilot save; consumed only by fsfx_loadvoicelfd to build
 * VOICE\<NAME>\<NAME>.LFD. The combat-of-tour mode (mission_mode==5) packs
 * the battle index into voice_id_a as 12+battle so voice_id_a-12 still
 * yields a 0-based battle digit in the loader. */
extern uint8_t voice_id_a;         /* combat-sim ship 0..11, or 12+battle (mode 5) */
extern uint8_t voice_id_b;         /* combat-sim course / mission-mode mission idx */
extern uint8_t voice_tour_battle;  /* tour-mode battle index */
extern uint8_t voice_tour_mission; /* tour-mode mission index in that battle */

/* Voice-queue scheduler state. Storage owned by tie.c per watdbg; declared
 * here because the array size depends on FSFX_BLAST_QUEUE_SIZE. */
extern uint8_t blastflag;
extern uint8_t blastqueue[FSFX_BLAST_QUEUE_SIZE];
extern uint8_t blastcount;
extern uint16_t sounddist[FSFX_NUM_DIST_ENTRIES];
extern uint8_t fullvolume[FSFX_NUM_DIST_ENTRIES];
extern const char* sfxgroupnameptrs[5];

/* Translate the stable mission-voice logical index to the selected
 * edition's numeric sound ID. Returns UINT16_MAX for an invalid index. */
uint16_t fsfx_mission_voice_id(uint16_t logical_index);

/* Canonical LFD name table shared by the recovered FrontendSound layer. */
const char* fsfx_sound_name(uint16_t sound_id);
int fsfx_find_sound_id(const char* name);

/* TIE98 recovered player-engine controller state and entry point. */
extern uint8_t g_playerEngineSoundUpdateEnabled;
extern int16_t g_engineSoundPreviousPlayerSpecies;
void FSFX_UpdatePlayerEngineSound(void);

#endif /* __FSFX_H__ */
