#ifndef TIE_REMASTER_DEBUG_TOOLS_H
#define TIE_REMASTER_DEBUG_TOOLS_H

/*
 * TIE debug tools — registration entry point for the Aeron debug
 * overlay (aeron/debug.h). Compiled only when the build enables
 * AERON_DEBUG_UI; a no-op stub TU covers the disabled configuration so
 * main.c calls unconditionally.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Register every TIE inspector with Aeron_DebugRegisterTool and run
 * the tools' init hooks. Call once after the remaster modules are up
 * (the tools poke flight_gpu / cockpit_gpu process-wide state). */
void TieDebugTools_Register(void);

/* Run the tools' shutdown hooks. Call before remaster shutdown. */
void TieDebugTools_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
