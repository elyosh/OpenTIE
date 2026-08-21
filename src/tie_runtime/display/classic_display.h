#ifndef TIE_RUNTIME_DISPLAY_CLASSIC_DISPLAY_H
#define TIE_RUNTIME_DISPLAY_CLASSIC_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum TieClassicOutputKind {
	TIE_CLASSIC_OUTPUT_INDEXED_FRAMEBUFFER = 0,
	TIE_CLASSIC_OUTPUT_DX5_SURFACE = 1,
} TieClassicOutputKind;

bool TieClassicDisplay_InitializeFrontend(void);
bool TieClassicDisplay_ActivateFrontend(void);
bool TieClassicDisplay_ActivateFlight(void);
bool TieClassicDisplay_ActivateFlightMode(uint16_t mode);

void TieClassicDisplay_Reset(void);
bool TieClassicDisplay_FrontendActive(void);
bool TieClassicDisplay_UsesDx5(void);
bool TieClassicDisplay_UsesExternalCursor(void);
TieClassicOutputKind TieClassicDisplay_OutputKind(void);
void TieClassicDisplay_Dimensions(int* width, int* height);

#endif
