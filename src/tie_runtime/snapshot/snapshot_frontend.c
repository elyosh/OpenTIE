#include "tie_runtime/snapshot/snapshot_frontend.h"

#include <string.h>

#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

#include "tie_runtime/integration/landru_adapter.h"
#include "tie_runtime/snapshot/capture_views.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

void TieFrontendSnapshot_CaptureText(void) {
	const int count = TieRecoveredText_SnapshotLineCount();
	for (int index = 0; index < count; ++index) {
		TieRecoveredTextSnapshotLine source;
		if (!TieRecoveredText_ReadSnapshotLine(index, &source))
			continue;
		TieUIText* output = TieSnapshotBuilder_AllocUIText();
		if (!output)
			break;
		memcpy(output->text, source.text, sizeof output->text);
		output->x = source.x;
		output->y = source.y;
		output->color_index = source.color;
		output->bold_color_index = source.bold_color;
		output->shadow_color_index = source.shadow_color;
		output->shadow = 1;
		output->font_id = 0;
		output->font_domain = TIE_FONT_DOMAIN_LANDRU;
		output->target = TIE_EMIT_TARGET_CUTSCENE;
	}
}

void TieFrontendSnapshot_CaptureTitle(void) {
	const int count = TieRecoveredTitle_SnapshotLineCount();
	if (count <= 0)
		return;
	TieSnapshotBuilder_SetSceneClock(lview_Get_View_Time(), ltimer_Frame_Progress(),
									 ltimer_Frame_Period_Us());
	for (int index = 0; index < count; ++index) {
		TieTitleCrawlLine* output = TieSnapshotBuilder_AllocTitleCrawlLine();
		if (!output)
			break;
		if (!TieRecoveredTitle_ReadSnapshotLine(index, output->text, sizeof output->text, &output->initial_y))
			break;
		output->font_id = 2;
		output->font_domain = TIE_FONT_DOMAIN_TITLE;
	}
}

void TieFrontendSnapshot_CaptureLogo(void) {
	LandruActorRenderState actors[128];
	const int count = TieRecoveredLogo_ReadSnapshotActors(actors, 128);
	for (int index = 0; index < count; ++index) {
		if (!TieLandruAdapter_EmitActorState(&actors[index]))
			break;
	}
}
