#include "tie_runtime/diagnostics/diagnostics.h"

#include "aeron/aeron.h"
#include "aeron/log.h"

#include <stdarg.h>

void TieDiagnostics_Log(TieLogLevel level, const char* format, ...) {
	AeronLogLevel aeron_level = AERON_LOG_INFO;
	if (level == TIE_LOG_ERROR)
		aeron_level = AERON_LOG_ERROR;
	else if (level == TIE_LOG_WARN)
		aeron_level = AERON_LOG_WARN;
	else if (level == TIE_LOG_TRACE)
		aeron_level = AERON_LOG_TRACE;
	va_list args;
	va_start(args, format);
	Aeron_LogMessageV(aeron_level, "tie.classic", format, args);
	va_end(args);
}

void TieDiagnostics_Fatal(const char* message) { Aeron_RequestFatalError("OpenTIE", message); }

void TieDiagnostics_RendererFailure(const char* operation) { Aeron_RequestFatalRendererError(operation); }
