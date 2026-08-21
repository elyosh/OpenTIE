#include "tie_runtime/flight_assets/native_opt.h"

#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/flight_assets/service.h"

/* PORT: host boundary for acquiring and resolving the native OPT image used by
 * recovered TIE98 renderer functions. This file contains no recovered bodies. */

const void* TieNativeOpt_ResolveAddress(const Tie98OptimizedPolyObject* model, uint32_t address,
										size_t size) {
	if (!model || !address || address < model->serialized_base)
		return NULL;
	const size_t offset = (size_t)(address - model->serialized_base);
	if (offset > model->serialized_size || size > model->serialized_size - offset)
		return NULL;
	return model->serialized_data + offset;
}

const Tie98OptimizedPolyObject* TieNativeOpt_Acquire(uint16_t model_type) {
	Tie98OptApi opts = TieFlightAssets_NativeOptApi();
	if (!opts.acquire)
		return NULL;
	char error[512];
	const Tie98OptimizedPolyObject* model = opts.acquire(opts.context, model_type, error, sizeof error);
	if (!model)
		TieDiagnostics_Log(TIE_LOG_ERROR, "TIE98 native OPT model %u: %s", model_type, error);
	return model;
}

const Tie98OptimizedPolyObject* TieNativeOpt_AcquireNamed(const char* model_name, uint16_t* out_model_type) {
	Tie98OptApi opts = TieFlightAssets_NativeOptApi();
	if (!opts.acquire_named)
		return NULL;
	char error[512];
	const Tie98OptimizedPolyObject* model =
		opts.acquire_named(opts.context, model_name, out_model_type, error, sizeof error);
	if (!model)
		TieDiagnostics_Log(TIE_LOG_ERROR, "TIE98 native OPT model '%s': %s", model_name, error);
	return model;
}
