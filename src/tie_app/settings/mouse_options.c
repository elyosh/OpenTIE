#include "tie_app/settings/mouse_options.h"

#include <string.h>

static struct {
	TieMouseFlightOptions persisted;
	TieMouseFlightOptions requested;
	TieMouseOptionsApplyFn apply;
	TieMouseOptionsPersistFn persist;
	void* user;
	bool configured;
	bool dirty;
} g_mouse_options;

static bool TieMouseOptions_Equal(const TieMouseFlightOptions* left, const TieMouseFlightOptions* right) {
	return left->enabled == right->enabled && left->mode == right->mode &&
		   left->sensitivity == right->sensitivity && left->invert_y == right->invert_y;
}

bool TieMouseOptions_Configure(const TieMouseFlightOptions* requested, TieMouseOptionsApplyFn apply,
							   TieMouseOptionsPersistFn persist, void* user) {
	memset(&g_mouse_options, 0, sizeof g_mouse_options);
	if (!TieMouseFlight_OptionsValid(requested) || !apply || !persist)
		return false;
	g_mouse_options.persisted = *requested;
	g_mouse_options.requested = *requested;
	g_mouse_options.apply = apply;
	g_mouse_options.persist = persist;
	g_mouse_options.user = user;
	g_mouse_options.configured = true;
	return true;
}

void TieMouseOptions_Shutdown(void) { memset(&g_mouse_options, 0, sizeof g_mouse_options); }

void TieMouseOptions_Get(TieMouseFlightOptions* out) {
	if (out && g_mouse_options.configured)
		*out = g_mouse_options.requested;
}

bool TieMouseOptions_Set(const TieMouseFlightOptions* options, char* error, size_t error_capacity) {
	if (!g_mouse_options.configured || !TieMouseFlight_OptionsValid(options))
		return false;
	if (TieMouseOptions_Equal(options, &g_mouse_options.requested))
		return true;
	if (!g_mouse_options.apply(&g_mouse_options.requested, options, g_mouse_options.user, error,
							   error_capacity))
		return false;
	g_mouse_options.requested = *options;
	g_mouse_options.dirty = !TieMouseOptions_Equal(&g_mouse_options.requested, &g_mouse_options.persisted);
	return true;
}

bool TieMouseOptions_Flush(char* error, size_t error_capacity) {
	if (!g_mouse_options.configured || !g_mouse_options.dirty)
		return true;
	if (!g_mouse_options.persist(&g_mouse_options.requested, g_mouse_options.user, error, error_capacity))
		return false;
	g_mouse_options.persisted = g_mouse_options.requested;
	g_mouse_options.dirty = false;
	return true;
}
