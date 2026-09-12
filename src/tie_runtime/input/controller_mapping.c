#include "tie_runtime/input/controller_mapping.h"
#include "aeron/log.h"
#include "tie_runtime/input/keyboard_mapping.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Physical holds are independent even when several instances share a profile. */
typedef struct ControllerInstance {
	uint32_t id;
	int model;
	bool down[TIE_CONTROLLER_BINDING_CAP];
	bool armed[TIE_CONTROLLER_BINDING_CAP];
	bool dispatched[TIE_CONTROLLER_BINDING_CAP];
	bool primed;
} ControllerInstance;
static struct {
	TieControllerOptions options;
	ControllerInstance instances[AERON_CONTROLLER_MAX];
	uint32_t analog[TIE_CONTROLLER_MODEL_CAP];
	bool suspended, present, throttle_valid;
	int16_t axes[3];
	uint16_t throttle;
	uint32_t throttle_instance, generation;
} g_controller;

static bool TieControllerMapping_SameSource(const AeronControllerDigitalSource* left,
											const AeronControllerDigitalSource* right) {
	return left->kind == right->kind && left->index == right->index &&
		   (left->kind != AERON_CONTROLLER_DIGITAL_HAT || left->hat_direction == right->hat_direction);
}

static bool TieControllerMapping_ProfileEqual(const TieControllerProfile* left,
											  const TieControllerProfile* right) {
	if (left->binding_count != right->binding_count)
		return false;
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const TieInputAxisBinding* a = &left->mapping.axes[axis];
		const TieInputAxisBinding* b = &right->mapping.axes[axis];
		if (a->source != b->source || a->invert != b->invert || a->deadzone != b->deadzone)
			return false;
	}
	for (size_t index = 0; index < left->binding_count; ++index) {
		const TieInputActionBinding* a = &left->bindings[index];
		const TieInputActionBinding* b = &right->bindings[index];
		if (a->action != b->action || !TieControllerMapping_SameSource(&a->source, &b->source) ||
			a->source.threshold != b->source.threshold)
			return false;
	}
	return true;
}

bool TieControllerMapping_Optionsequal(const TieControllerOptions* left, const TieControllerOptions* right) {
	if (!left || !right)
		return left == right;
	if (left->count != right->count)
		return false;
	for (size_t i = 0; i < left->count; ++i) {
		const TieControllerModel *a = &left->models[i], *b = &right->models[i];
		if (strcmp(a->guid, b->guid) || strcmp(a->name, b->name) || a->kind != b->kind ||
			!TieControllerMapping_ProfileEqual(&a->profile, &b->profile))
			return false;
	}
	return true;
}

bool TieControllerMapping_EffectiveAxisInvert(AeronControllerKind kind, TieInputAxis axis, bool invert) {
	/* SDL gamepad Y is positive downward, while positive TIE pitch raises the nose. */
	return kind == AERON_CONTROLLER_KIND_GAMEPAD && axis == TIE_INPUT_AXIS_PITCH ? !invert : invert;
}

static bool TieControllerMapping_ValidationError(char* error, size_t capacity, const char* message) {
	if (error && capacity)
		snprintf(error, capacity, "%s", message);
	return false;
}

bool TieControllerMapping_Profilevalidate(const TieControllerProfile* profile, AeronControllerKind kind,
										  char* error, size_t capacity) {
	const int axis_limit =
		kind == AERON_CONTROLLER_KIND_GAMEPAD ? AERON_GAMEPAD_AXIS_COUNT : AERON_CONTROLLER_AXIS_MAX;
	const int button_limit =
		kind == AERON_CONTROLLER_KIND_GAMEPAD ? AERON_GAMEPAD_BUTTON_COUNT : AERON_CONTROLLER_BUTTON_MAX;
	if (!profile || (kind != AERON_CONTROLLER_KIND_GAMEPAD && kind != AERON_CONTROLLER_KIND_JOYSTICK))
		return TieControllerMapping_ValidationError(error, capacity, "invalid controller profile");
	if (profile->binding_count > TIE_CONTROLLER_BINDING_CAP)
		return TieControllerMapping_ValidationError(error, capacity, "controller binding capacity exceeded");
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const TieInputAxisBinding* binding = &profile->mapping.axes[axis];
		if (binding->source < -1 || binding->source >= axis_limit || !isfinite(binding->deadzone) ||
			binding->deadzone < 0.0f || binding->deadzone > (axis == TIE_INPUT_AXIS_THROTTLE ? 0.10f : 1.0f))
			return TieControllerMapping_ValidationError(error, capacity,
														"controller axis binding is invalid");
		if (binding->source < 0)
			continue;
		for (int prior = 0; prior < axis; ++prior)
			if (profile->mapping.axes[prior].source == binding->source)
				return TieControllerMapping_ValidationError(
					error, capacity, "controller axis source is assigned more than once");
	}
	for (size_t index = 0; index < profile->binding_count; ++index) {
		const TieInputActionBinding* binding = &profile->bindings[index];
		const AeronControllerDigitalSource* source = &binding->source;
		if (binding->action <= TIE_INPUT_ACTION_NONE || binding->action >= TIE_INPUT_ACTION_COUNT)
			return TieControllerMapping_ValidationError(error, capacity,
														"controller action binding is invalid");
		if (!isfinite(source->threshold) || source->threshold <= 0.0f || source->threshold > 1.0f)
			return TieControllerMapping_ValidationError(error, capacity,
														"controller digital threshold is invalid");
		switch (source->kind) {
			case AERON_CONTROLLER_DIGITAL_BUTTON:
				if (source->index >= button_limit)
					return TieControllerMapping_ValidationError(error, capacity,
																"controller button index is out of range");
				break;
			case AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE:
			case AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE:
				if (source->index >= axis_limit)
					return TieControllerMapping_ValidationError(error, capacity,
																"controller digital axis is invalid");
				break;
			case AERON_CONTROLLER_DIGITAL_HAT:
				if (kind != AERON_CONTROLLER_KIND_JOYSTICK || source->index >= AERON_CONTROLLER_HAT_MAX ||
					(source->hat_direction != AERON_CONTROLLER_HAT_UP &&
					 source->hat_direction != AERON_CONTROLLER_HAT_RIGHT &&
					 source->hat_direction != AERON_CONTROLLER_HAT_DOWN &&
					 source->hat_direction != AERON_CONTROLLER_HAT_LEFT))
					return TieControllerMapping_ValidationError(error, capacity,
																"controller hat binding is invalid");
				break;
			default:
				return TieControllerMapping_ValidationError(error, capacity,
															"controller source kind is invalid");
		}
		for (size_t prior = 0; prior < index; ++prior)
			if (TieControllerMapping_SameSource(source, &profile->bindings[prior].source))
				return TieControllerMapping_ValidationError(error, capacity,
															"controller source is bound more than once");
	}
	return true;
}

void TieControllerMapping_ClearProfile(TieControllerProfile* profile, AeronControllerKind kind) {
	memset(profile, 0, sizeof *profile);
	for (int i = 0; i < TIE_INPUT_AXIS_COUNT; ++i)
		profile->mapping.axes[i].source = -1;
	/* Raw HOTAS levers commonly decrease their reported value toward full power. */
	profile->mapping.axes[TIE_INPUT_AXIS_THROTTLE].invert = kind == AERON_CONTROLLER_KIND_JOYSTICK;
}

int TieControllerMapping_FindModel(const TieControllerOptions* options, const char* guid) {
	if (options && guid)
		for (size_t i = 0; i < options->count; ++i)
			if (!strcmp(options->models[i].guid, guid))
				return (int)i;
	return -1;
}

static bool GuidValid(const char* guid) {
	bool nonzero = false;
	for (int i = 0; i < 32; ++i) {
		if (!((guid[i] >= '0' && guid[i] <= '9') || (guid[i] >= 'a' && guid[i] <= 'f')))
			return false;
		nonzero |= guid[i] != '0';
	}
	return nonzero && guid[32] == 0;
}

bool TieControllerMapping_OptionsValid(const TieControllerOptions* options, char* error, size_t capacity) {
	if (!options || options->count > TIE_CONTROLLER_MODEL_CAP)
		return TieControllerMapping_ValidationError(error, capacity, "controller model capacity exceeded");
	for (size_t i = 0; i < options->count; ++i) {
		const TieControllerModel* m = &options->models[i];
		if (!GuidValid(m->guid) || !memchr(m->name, 0, sizeof m->name))
			return TieControllerMapping_ValidationError(error, capacity, "invalid controller model identity");
		if (!TieControllerMapping_Profilevalidate(&m->profile, m->kind, error, capacity))
			return false;
		for (size_t j = 0; j < i; ++j) {
			if (!strcmp(m->guid, options->models[j].guid))
				return TieControllerMapping_ValidationError(error, capacity,
															"duplicate controller model GUID");
			for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis)
				if (m->profile.mapping.axes[axis].source >= 0 &&
					options->models[j].profile.mapping.axes[axis].source >= 0)
					return TieControllerMapping_ValidationError(error, capacity,
																"logical axis has multiple model owners");
		}
	}
	return true;
}

bool TieControllerMapping_AddModel(TieControllerOptions* options, const AeronControllerSnapshot* device,
								   char* error, size_t capacity) {
	if (!device || !GuidValid(device->guid))
		return TieControllerMapping_ValidationError(error, capacity, "controller has no usable model GUID");
	if (TieControllerMapping_FindModel(options, device->guid) >= 0)
		return true;
	if (options->count >= TIE_CONTROLLER_MODEL_CAP)
		return TieControllerMapping_ValidationError(error, capacity, "controller model capacity exceeded");
	TieControllerModel* m = &options->models[options->count++];
	memset(m, 0, sizeof *m);
	memcpy(m->guid, device->guid, sizeof m->guid);
	snprintf(m->name, sizeof m->name, "%s", device->name);
	m->kind = device->kind;
	TieControllerMapping_ClearProfile(&m->profile, device->kind);
	return true;
}

bool TieControllerMapping_InitializeGamepads(TieControllerOptions* options,
											 const TieControllerProfile* defaults,
											 const AeronInputSnapshot* input, char* error, size_t capacity) {
	const AeronControllerSnapshot* sorted[AERON_CONTROLLER_MAX];
	int count = 0;
	if (!input)
		return true;
	for (int slot = 0; slot < AERON_CONTROLLER_MAX; ++slot) {
		const AeronControllerSnapshot* d = &input->controllers[slot];
		if (!d->connected || d->kind != AERON_CONTROLLER_KIND_GAMEPAD)
			continue;
		int j = count++;
		while (j > 0 && strcmp(sorted[j - 1]->guid, d->guid) > 0) {
			sorted[j] = sorted[j - 1];
			--j;
		}
		sorted[j] = d;
	}
	for (int i = 0; i < count; ++i) {
		if (TieControllerMapping_FindModel(options, sorted[i]->guid) >= 0)
			continue;
		if (!TieControllerMapping_AddModel(options, sorted[i], error, capacity))
			return false;
		TieControllerProfile* p = &options->models[options->count - 1].profile;
		*p = *defaults;
		for (size_t j = 0; j + 1 < options->count; ++j)
			for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis)
				if (options->models[j].profile.mapping.axes[axis].source >= 0)
					p->mapping.axes[axis].source = -1;
	}
	return true;
}

const AeronControllerSnapshot* TieControllerMapping_Resolve(const TieControllerModel* model,
															const AeronInputSnapshot* input,
															uint32_t preferred) {
	const AeronControllerSnapshot* best = NULL;
	if (!input)
		return NULL;
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		const AeronControllerSnapshot* d = &input->controllers[i];
		if (!d->connected || strcmp(model->guid, d->guid) || d->kind != model->kind)
			continue;
		if (d->instance_id == preferred)
			return d;
		if (!best || d->instance_id < best->instance_id)
			best = d;
	}
	return best;
}

static void ReleaseInstance(ControllerInstance* state) {
	if (state->id) {
		const TieControllerProfile* p = &g_controller.options.models[state->model].profile;
		for (size_t i = 0; i < p->binding_count; ++i)
			if (state->dispatched[i])
				TieInputActions_DispatchController(p->bindings[i].action, false);
	}
	memset(state, 0, sizeof *state);
}

bool TieControllerMapping_SetOptions(const TieControllerOptions* options) {
	static const TieControllerOptions empty = { 0 };
	if (!options)
		options = &empty;
	char error[128];
	if (!TieControllerMapping_OptionsValid(options, error, sizeof error)) {
		Aeron_LogWarn("tie.input", "%s", error);
		return false;
	}
	if (TieControllerMapping_Optionsequal(options, &g_controller.options))
		return true;
	uint32_t analog[TIE_CONTROLLER_MODEL_CAP] = { 0 };
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		ControllerInstance* state = &g_controller.instances[i];
		if (!state->id)
			continue;
		const TieControllerModel* old = &g_controller.options.models[state->model];
		int next = TieControllerMapping_FindModel(options, old->guid);
		if (next < 0 || old->kind != options->models[next].kind ||
			!TieControllerMapping_ProfileEqual(&old->profile, &options->models[next].profile))
			ReleaseInstance(state);
	}
	for (size_t i = 0; i < options->count; ++i) {
		int old = TieControllerMapping_FindModel(&g_controller.options, options->models[i].guid);
		if (old >= 0 && options->models[i].kind == g_controller.options.models[old].kind)
			analog[i] = g_controller.analog[old];
	}
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		ControllerInstance* state = &g_controller.instances[i];
		if (state->id)
			state->model =
				TieControllerMapping_FindModel(options, g_controller.options.models[state->model].guid);
	}
	/* Only changes to the effective throttle binding invalidate its movement baseline. */
	for (size_t i = 0; i < g_controller.options.count; ++i) {
		const TieControllerModel* old = &g_controller.options.models[i];
		const TieInputAxisBinding* a = &old->profile.mapping.axes[TIE_INPUT_AXIS_THROTTLE];
		if (a->source < 0)
			continue;
		int n = TieControllerMapping_FindModel(options, old->guid);
		if (n < 0 || old->kind != options->models[n].kind ||
			a->source != options->models[n].profile.mapping.axes[TIE_INPUT_AXIS_THROTTLE].source ||
			a->invert != options->models[n].profile.mapping.axes[TIE_INPUT_AXIS_THROTTLE].invert ||
			a->deadzone != options->models[n].profile.mapping.axes[TIE_INPUT_AXIS_THROTTLE].deadzone)
			TieInput_ResetThrottle();
	}
	g_controller.options = *options;
	memcpy(g_controller.analog, analog, sizeof analog);
	return true;
}

void TieControllerMapping_Suspend(void) {
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i)
		ReleaseInstance(&g_controller.instances[i]);
	g_controller.suspended = true;
	g_controller.present = false;
	g_controller.throttle_valid = false;
	memset(g_controller.axes, 0, sizeof g_controller.axes);
	TieInput_ResetThrottle();
}
void TieControllerMapping_Resume(void) {
	g_controller.suspended = false;
	TieInput_ResetThrottle();
}

static void LogUnavailableControls(const TieControllerModel* model, const AeronControllerSnapshot* device) {
	int missing = 0;
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		int source = model->profile.mapping.axes[axis].source;
		if (source >= 0 && !Aeron_ControllerAxisAvailable(device, source))
			++missing;
	}
	for (size_t i = 0; i < model->profile.binding_count; ++i) {
		const AeronControllerDigitalSource* source = &model->profile.bindings[i].source;
		bool available = false;
		if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON)
			available = model->kind == AERON_CONTROLLER_KIND_GAMEPAD
							? (device->gamepad_available_buttons & (1u << source->index)) != 0
							: source->index < device->button_count;
		else if (source->kind == AERON_CONTROLLER_DIGITAL_HAT)
			available = source->index < device->hat_count;
		else
			available = Aeron_ControllerAxisAvailable(device, source->index);
		if (!available)
			++missing;
	}
	if (missing || device->controls_truncated)
		Aeron_LogWarn("tie.input", "controller '%s': %d unavailable configured controls%s", device->name,
					  missing, device->controls_truncated ? ", hardware controls truncated" : "");
}

static void SampleDigital(ControllerInstance* state, const AeronControllerSnapshot* device) {
	const TieControllerModel* model = &g_controller.options.models[state->model];
	const TieControllerProfile* profile = &model->profile;
	if (!state->primed)
		LogUnavailableControls(model, device);
	for (size_t i = 0; i < profile->binding_count; ++i) {
		bool down = Aeron_ControllerDigitalSourceDown(device, &profile->bindings[i].source, state->down[i]);
		if (!state->primed)
			state->armed[i] = !down;
		else if (!down) {
			if (state->dispatched[i])
				TieInputActions_DispatchController(profile->bindings[i].action, false);
			state->dispatched[i] = false;
			state->armed[i] = true;
		} else if (state->armed[i] && !state->down[i]) {
			TieInputActions_DispatchController(profile->bindings[i].action, true);
			state->dispatched[i] = true;
		}
		state->down[i] = down;
	}
	state->primed = true;
}

uint16_t TieControllerMapping_ThrottlePosition(int16_t raw, AeronControllerKind kind, int source,
											   bool invert) {
	const uint32_t end_tolerance = (uint32_t)ceilf(TIE_INPUT_THROTTLE_ENDPOINT_PERCENT * UINT16_MAX / 100.0f);
	const uint32_t range = UINT16_MAX - 2 * end_tolerance;
	uint32_t position = (int32_t)raw + 32768;
	if (kind == AERON_CONTROLLER_KIND_GAMEPAD && source >= AERON_GAMEPAD_AXIS_LEFT_TRIGGER)
		position = ((uint32_t)(raw < 0 ? 0 : raw) * UINT16_MAX + 16383u) / 32767u;
	if (invert)
		position = UINT16_MAX - position;
	/* Saturate both endpoints and scale the remaining travel continuously. */
	if (position <= end_tolerance)
		return 0;
	if (position >= UINT16_MAX - end_tolerance)
		return UINT16_MAX;
	return (uint16_t)(((position - end_tolerance) * UINT16_MAX + range / 2) / range);
}

static void SampleAnalog(size_t index, const AeronControllerSnapshot* d, TieInputMapping* mapping) {
	const TieControllerModel* model = &g_controller.options.models[index];
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		TieInputAxisBinding b = model->profile.mapping.axes[axis];
		if (!Aeron_ControllerAxisAvailable(d, b.source))
			continue;
		int16_t raw = Aeron_ControllerAxisValue(d, b.source);
		if (axis == TIE_INPUT_AXIS_THROTTLE) {
			g_controller.throttle =
				TieControllerMapping_ThrottlePosition(raw, model->kind, b.source, b.invert);
			g_controller.throttle_valid = true;
			if (g_controller.throttle_instance != d->instance_id) {
				g_controller.throttle_instance = d->instance_id;
				++g_controller.generation;
			}
		} else {
			int value = ((int32_t)raw * 127) / 32767;
			if (value < -127)
				value = -127;
			/* Landru also reads these slots for its menu cursor. Flight polarity
			 * belongs in TieInputMapping, which only feinput_getrawinput applies. */
			g_controller.axes[axis] = (int16_t)value;
			mapping->axes[axis] = (TieInputAxisBinding) {
				.source = (int8_t)axis,
				.invert = TieControllerMapping_EffectiveAxisInvert(model->kind, (TieInputAxis)axis, b.invert),
				.deadzone = b.deadzone,
			};
		}
	}
}

void TieControllerMapping_Update(const AeronInputSnapshot* input) {
	if (g_controller.suspended)
		return;
	if (!input || !input->has_focus) {
		TieControllerMapping_Suspend();
		g_controller.suspended = false;
		return;
	}
	g_controller.present = false;
	g_controller.throttle_valid = false;
	memset(g_controller.axes, 0, sizeof g_controller.axes);
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		ControllerInstance* state = &g_controller.instances[i];
		bool found = false;
		for (int j = 0; j < AERON_CONTROLLER_MAX && state->id; ++j) {
			const AeronControllerSnapshot* d = &input->controllers[j];
			const TieControllerModel* m = &g_controller.options.models[state->model];
			if (d->connected && d->instance_id == state->id && !strcmp(d->guid, m->guid) &&
				d->kind == m->kind)
				found = true;
		}
		if (!found)
			ReleaseInstance(state);
	}
	TieInputMapping mapping = { 0 };
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis)
		mapping.axes[axis].source = -1;
	for (size_t model = 0; model < g_controller.options.count; ++model) {
		const AeronControllerSnapshot* analog = TieControllerMapping_Resolve(
			&g_controller.options.models[model], input, g_controller.analog[model]);
		g_controller.analog[model] = analog ? analog->instance_id : 0;
		if (analog) {
			g_controller.present = true;
			SampleAnalog(model, analog, &mapping);
		}
		uint32_t previous = 0;
		for (int n = 0; n < AERON_CONTROLLER_MAX; ++n) {
			const AeronControllerSnapshot* d = NULL;
			for (int j = 0; j < AERON_CONTROLLER_MAX; ++j) {
				const AeronControllerSnapshot* candidate = &input->controllers[j];
				if (candidate->connected && candidate->instance_id > previous &&
					!strcmp(candidate->guid, g_controller.options.models[model].guid) &&
					candidate->kind == g_controller.options.models[model].kind &&
					(!d || candidate->instance_id < d->instance_id))
					d = candidate;
			}
			if (!d)
				break;
			previous = d->instance_id;
			ControllerInstance* state = NULL;
			for (int j = 0; j < AERON_CONTROLLER_MAX; ++j)
				if (g_controller.instances[j].id == d->instance_id)
					state = &g_controller.instances[j];
			if (!state)
				for (int j = 0; j < AERON_CONTROLLER_MAX; ++j)
					if (!g_controller.instances[j].id) {
						state = &g_controller.instances[j];
						state->id = d->instance_id;
						state->model = (int)model;
						break;
					}
			if (state)
				SampleDigital(state, d);
		}
	}
	if (!g_controller.throttle_valid) {
		g_controller.throttle_instance = 0;
		TieInput_ResetThrottle();
	}
	TieInput_SetMapping(&mapping);
}

uint32_t TieControllerMapping_AnalogInstance(const char* guid) {
	int i = TieControllerMapping_FindModel(&g_controller.options, guid);
	return i < 0 ? 0 : g_controller.analog[i];
}
bool TieControllerMapping_ThrottleSample(uint16_t* position, uint32_t* generation) {
	*position = g_controller.throttle;
	*generation = g_controller.generation;
	return g_controller.throttle_valid;
}
int TieControllerMapping_Present(void) { return g_controller.present; }
void TieControllerMapping_Read(int16_t* axes, int count, uint16_t* buttons) {
	if (axes && count > 0) {
		memset(axes, 0, (size_t)count * sizeof *axes);
		for (int i = 0; i < count && i < 3; ++i)
			axes[i] = g_controller.axes[i];
	}
	if (buttons)
		*buttons = TieKeyboardMapping_ReadButtons();
}
