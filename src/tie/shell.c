#include "tie/shell.h"
#include "tie/gamesnd.h"

#ifdef TIE_MODERN
#include "tie_runtime/diagnostics/diagnostics.h"
#endif

#include <stdio.h>
#include <stdlib.h>

// GLOBAL: TIE98 0x5F3464
SceneHeadStruct* sHead_gbl;
int16_t digital_exists;

// FUNCTION: TIE95 0x67EFA
void shell_programexit(const char* str) {
	shellext_Close_Landru(0);
	gamesnd_Close_Pre_iMuse();
#ifdef TIE_MODERN
	TieDiagnostics_Log(TIE_LOG_ERROR, "%s", str);
	TieDiagnostics_Fatal(str);
	exit(EXIT_FAILURE);
#else
	printf(str);
	exit(0);
#endif
}
