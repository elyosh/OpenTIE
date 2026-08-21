#ifndef TIE_LOG_H
#define TIE_LOG_H

/* Watcom C has no __attribute__; annotations here are advisory. */
#if defined(__WATCOMC__)
#define __attribute__(x)
#endif

/* Standalone log surface — separated from host.h so consumers
 * that only need the log API (e.g. the cutscene compositor used by
 * filmview without tie_core linkage) don't drag in the full host
 * vtable, snapshot types, file shims, etc.
 *
 * Levels are coarse: error / warn / info / trace. The application decides
 * filtering and routing (stderr, libretro log callback, etc.).
 *
 * Anyone who links the cutscene compositor must provide an
 * implementation of TieDiagnostics_Log. tie_core has its own in host.c; filmview
 * supplies a small stderr stub.
 *
 * Direct printf / fprintf / puts in tie_core sources is forbidden by
 * the architecture doc — replace with TieDiagnostics_Log calls. */

typedef enum {
	TIE_LOG_ERROR = 0,
	TIE_LOG_WARN = 1,
	TIE_LOG_INFO = 2,
	TIE_LOG_TRACE = 3,
} TieLogLevel;

#ifdef __cplusplus
extern "C" {
#endif

void TieDiagnostics_Log(TieLogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void TieDiagnostics_Fatal(const char* message);
void TieDiagnostics_RendererFailure(const char* operation);

#ifdef __cplusplus
}
#endif

#endif
