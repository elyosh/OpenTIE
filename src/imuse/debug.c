#include "internal/debug.h"
#include "internal/state.h"

#include <stdarg.h>
#include <stdio.h>

/*
 * libimuse — debug log dispatcher.
 *
 * Formats messages into a stack buffer and forwards to the host log
 * callback latched on ImCommandsState. No-op when the host left
 * logFunc NULL or when called before ImCommands_Init has run.
 *
 * Buffer size: 512 bytes is enough for every log message in the
 * library today (longest is around 110 chars). vsnprintf truncates
 * cleanly on overflow.
 */

#define IMUSE_LOG_BUF_SIZE 512

void ImDebug_VLog(imuse_t* im, ImuseLogLevel level, const char* fmt, va_list ap) {
	if (!im || !im->commands.logFunc || !fmt)
		return;

	char buf[IMUSE_LOG_BUF_SIZE];
	vsnprintf(buf, sizeof buf, fmt, ap);
	im->commands.logFunc(im->commands.logUser, level, buf);
}

void ImDebug_VLogMsg(imuse_t* im, const char* fmt, va_list ap) { ImDebug_VLog(im, IMUSE_LOG_INFO, fmt, ap); }

void ImDebug_LogMsg(imuse_t* im, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	ImDebug_VLog(im, IMUSE_LOG_INFO, fmt, ap);
	va_end(ap);
}

void ImDebug_LogTrace(imuse_t* im, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	ImDebug_VLog(im, IMUSE_LOG_TRACE, fmt, ap);
	va_end(ap);
}

void ImDebug_LogInfo(imuse_t* im, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	ImDebug_VLog(im, IMUSE_LOG_INFO, fmt, ap);
	va_end(ap);
}

void ImDebug_LogWarn(imuse_t* im, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	ImDebug_VLog(im, IMUSE_LOG_WARN, fmt, ap);
	va_end(ap);
}

void ImDebug_LogError(imuse_t* im, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	ImDebug_VLog(im, IMUSE_LOG_ERROR, fmt, ap);
	va_end(ap);
}
