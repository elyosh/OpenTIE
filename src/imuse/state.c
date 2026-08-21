#include <imuse/commands.h>

#include "internal/commands.h" /* ImCommands_Init / Terminate */
#include "internal/midi_backend.h"
#include "internal/state.h"

#include <stdlib.h>
#include <string.h>

/* Each call creates independent session state.
 * Ownership contract:
 *   imuse_create unconditionally takes ownership of any non-NULL
 *   `backend` argument. On success the backend lives until
 *   imuse_destroy. On any failure path inside imuse_create (NULL
 *   host/cfg, calloc failure, ImCommands_Init failure) the backend
 *   is released here before returning NULL — the host never sees
 *   it again. host and cfg are read once and copied by value; the
 *   caller may free / reuse the structs immediately on return.
 */

imuse_t* imuse_create(const ImuseHost* host, const ImuseConfig* cfg, ImuseMidiBackend* backend) {
	if (!host || !cfg) {
		ImMidi_BackendRelease(backend);
		return NULL;
	}
	imuse_t* im = calloc(1, sizeof(*im));
	if (!im) {
		ImMidi_BackendRelease(backend);
		return NULL;
	}
	int rc = ImCommands_Init(im, host, cfg, backend);
	if (rc < 0) {
		/* ImCommands_Init releases the backend on its own rollback
		 * path (see commands.c). */
		free(im);
		return NULL;
	}
	return im;
}

int imuse_destroy(imuse_t* im) {
	if (!im)
		return -1;
	int rc = ImCommands_Terminate(im);
	free(im);
	return rc;
}
