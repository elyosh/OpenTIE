/* Per-mission world ambient YAML. The `default` entry maps to slot 0;
 * numeric battle IDs map to slot id + 1. All fields are optional. */

#ifndef TIE_REMASTER_WORLD_AMBIENT_YAML_H
#define TIE_REMASTER_WORLD_AMBIENT_YAML_H

#include "aeron/vfs.h"
#include "tie_remaster/flight/pbr.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Applies an ambient overlay atomically. Missing files and parse errors return false. */
bool TieWorldAmbient_LoadYaml(AeronVfs* vfs, AeronVfsRoot root, const char* yaml_path,
							  TieWorldAmbientLibrary* inout);

/* Emits authored slots only. Returns zero if the output buffer is too small. */
size_t TieWorldAmbient_EmitYaml(const TieWorldAmbientLibrary* in, char* out_buf, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* TIE_REMASTER_WORLD_AMBIENT_YAML_H */
