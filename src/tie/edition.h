#ifndef TIE_EDITION_H
#define TIE_EDITION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Frontend values that differ only between the TIE95 (320x200) and TIE98
 * (640x480) releases. Matching builds get the retail literal of their
 * edition; modern builds select it from the active frontend profile. */
#if defined(TIE_MODERN)
#include "tie_runtime/runtime/profile.h"
#define TIE_EDITION(tie95, tie98) (TieProfile_UsesTie98Frontend() ? (tie98) : (tie95))
#elif defined(TIE98)
#define TIE_EDITION(tie95, tie98) (tie98)
#else
#define TIE_EDITION(tie95, tie98) (tie95)
#endif

#ifdef __cplusplus
}
#endif

#endif
