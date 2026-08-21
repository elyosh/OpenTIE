#ifndef LIBIMUSE_INTERNAL_COMMANDS_H
#define LIBIMUSE_INTERNAL_COMMANDS_H

#include <stdint.h>

#include <imuse/handle.h>

/*
 * Internal entry points for the COMMANDS module.
 *
 * The public surface lives in <imuse/commands.h> (lifecycle, advance,
 * mix, sound control). This header declares the helpers that survive
 * I9 as engine-internal only:
 *
 *   - ImCommands_Init / ImCommands_Terminate are called by the
 *     imuse_create / imuse_destroy implementations (state.c).
 *   - ImCommands_ExecOpcode is the trigger-replay dispatcher: when
 *     the TRIGGERS module fires a stored opcode (< 30) it routes
 *     through here. Public callers go through the typed entry points
 *     (imuse_start_sound, imuse_set_param, …) instead.
 */

struct ImuseHost;
struct ImuseConfig;
struct ImuseMidiBackend;

int ImCommands_Init(imuse_t* im, const struct ImuseHost* host, const struct ImuseConfig* cfg,
					struct ImuseMidiBackend* backend);
int ImCommands_Terminate(imuse_t* im);
int ImCommands_ExecOpcode(imuse_t* im, int opcode, const intptr_t args[10]);

#endif /* LIBIMUSE_INTERNAL_COMMANDS_H */
