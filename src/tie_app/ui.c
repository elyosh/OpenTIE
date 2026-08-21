#include "tie_app/ui.h"

#include <stdio.h>
#include <string.h>

static void TieUi_SetColor(float color[4], float r, float g, float b, float a) {
	color[0] = r;
	color[1] = g;
	color[2] = b;
	color[3] = a;
}

static void TieUi_ApplyTieTheme(AeronUiContext* context) {
	AeronUiTheme theme = *AeronUi_DefaultTheme();
	TieUi_SetColor(theme.surface, 0.009f, 0.010f, 0.013f, 0.99f);
	TieUi_SetColor(theme.surface_border, 0.055f, 0.062f, 0.075f, 1.0f);
	TieUi_SetColor(theme.title_bar, 0.030f, 0.036f, 0.050f, 1.0f);
	TieUi_SetColor(theme.title_bar_low, 0.014f, 0.017f, 0.024f, 1.0f);
	TieUi_SetColor(theme.title_text, 0.90f, 0.91f, 0.94f, 1.0f);
	TieUi_SetColor(theme.accent, 0.62f, 0.030f, 0.036f, 1.0f);
	TieUi_SetColor(theme.focus_outline, 0.90f, 0.065f, 0.075f, 1.0f);
	TieUi_SetColor(theme.row_highlight, 0.90f, 0.10f, 0.11f, 0.05f);
	TieUi_SetColor(theme.slider_track, 0.034f, 0.038f, 0.048f, 1.0f);
	AeronUi_SetTheme(context, &theme);
}

bool TieUi_Init(TieUi* ui, const TieAppUiConfig* config, char* error, size_t error_capacity) {
	if (!ui || !config || !config->font[0]) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "invalid application UI configuration");
		return false;
	}
	memset(ui, 0, sizeof *ui);
	AeronCommandBuffer* command = Aeron_AcquireCommandBuffer();
	if (!command) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "UI font upload command-buffer acquisition failed");
		return false;
	}
	if (!AeronFontAtlas_LoadVfs(&ui->font, command, Aeron_GetVfs(), AERON_VFS_ROOT_RESOURCE, config->font,
								64u * 1024u * 1024u)) {
		Aeron_CancelCommandBuffer(command);
		if (error && error_capacity)
			snprintf(error, error_capacity,
					 "required UI font atlas RESOURCE/%s.{fnt,png} is missing, unreadable, or invalid",
					 config->font);
		return false;
	}
	if (!Aeron_SubmitCommandBuffer(command)) {
		AeronFontAtlas_Release(&ui->font);
		if (error && error_capacity)
			snprintf(error, error_capacity, "UI font atlas upload submission failed");
		return false;
	}
	ui->context = AeronUi_Create(NULL);
	if (!ui->context) {
		AeronFontAtlas_Release(&ui->font);
		if (error && error_capacity)
			snprintf(error, error_capacity, "application UI context creation failed");
		return false;
	}
	TieUi_ApplyTieTheme(ui->context);
	AeronUi_SetFonts(ui->context, &(AeronUiFontSet) {
									  .regular = &ui->font,
									  .title = &ui->font,
								  });
	return true;
}

void TieUi_Shutdown(TieUi* ui) {
	if (!ui)
		return;
	AeronUi_Destroy(ui->context);
	AeronFontAtlas_Release(&ui->font);
	memset(ui, 0, sizeof *ui);
}

AeronUiContext* TieUi_Context(TieUi* ui) { return ui ? ui->context : NULL; }
