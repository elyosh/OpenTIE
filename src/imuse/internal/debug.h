#ifndef __IMUSE_DEBUG_H__
#define __IMUSE_DEBUG_H__

#include <stdarg.h>

#include <imuse/commands.h> /* ImuseLogLevel */
#include <imuse/handle.h>

/*
 * iMUSE engine — DEBUG module.
 *
 * Central log sink used by every module. In the DOS build this drove
 * an on-screen text buffer; in libimuse messages are formatted into
 * a stack buffer and dispatched to the host log callback registered
 * via ImuseHost::logFunc. NULL log = silent.
 *
 * Use the level-aware helpers (LogTrace / LogInfo / LogWarn /
 * LogError) at call sites; LogMsg defaults to INFO. The trace level
 * is for high-volume per-event sites that the host typically wants
 * filtered out.
 */

void ImDebug_LogMsg(imuse_t* im, const char* fmt, ...);
void ImDebug_LogTrace(imuse_t* im, const char* fmt, ...);
void ImDebug_LogInfo(imuse_t* im, const char* fmt, ...);
void ImDebug_LogWarn(imuse_t* im, const char* fmt, ...);
void ImDebug_LogError(imuse_t* im, const char* fmt, ...);

void ImDebug_VLog(imuse_t* im, ImuseLogLevel level, const char* fmt, va_list ap);
void ImDebug_VLogMsg(imuse_t* im, const char* fmt, va_list ap);

#endif /* __IMUSE_DEBUG_H__ */
