#ifndef TIE_EDITION_H
#define TIE_EDITION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Edition selection for code shared by the TIE95 and TIE98 releases.
 *
 * Matching builds compile only their own edition's code: the predicates are
 * literal constants, so the compilers drop the other edition's branch, and the
 * value selectors expand to that edition's literal. Modern builds select
 * at runtime, with the frontend, the flight logic and the display chosen
 * independently. */
#if defined(TIE_MODERN)
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#define TIE_FRONTEND_TIE98 (TieProfile_UsesTie98Frontend())
#define TIE_FLIGHT_TIE98 (TieProfile_UsesTie98Logic())
#define TIE_DISPLAY_DX5 (TieClassicDisplay_UsesDx5())
#define TIE_FRONTEND_EDITION(tie95, tie98) (TIE_FRONTEND_TIE98 ? (tie98) : (tie95))
#define TIE_FLIGHT_EDITION(tie95, tie98) (TIE_FLIGHT_TIE98 ? (tie98) : (tie95))
#define TIE_DISPLAY_EDITION(vga, dx5) (TIE_DISPLAY_DX5 ? (dx5) : (vga))
#elif defined(TIE98)
#define TIE_FRONTEND_TIE98 1
#define TIE_FLIGHT_TIE98 1
#define TIE_DISPLAY_DX5 1
#define TIE_FRONTEND_EDITION(tie95, tie98) (tie98)
#define TIE_FLIGHT_EDITION(tie95, tie98) (tie98)
#define TIE_DISPLAY_EDITION(vga, dx5) (dx5)
#else
#define TIE_FRONTEND_TIE98 0
#define TIE_FLIGHT_TIE98 0
#define TIE_DISPLAY_DX5 0
#define TIE_FRONTEND_EDITION(tie95, tie98) (tie95)
#define TIE_FLIGHT_EDITION(tie95, tie98) (tie95)
#define TIE_DISPLAY_EDITION(vga, dx5) (vga)
#endif

#ifdef __cplusplus
}
#endif

#endif
