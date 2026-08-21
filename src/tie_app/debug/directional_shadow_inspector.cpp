/*
 * Directional-shadow inspector for the cooked-glb HD flight renderer.
 * Runtime edits are sanitized by TieFlightRenderer_ShadowsSet and applied on
 * the next scene frame; Aeron supplies the cascade/caster statistics.
 */

#include <cstdio>

#include <imgui.h>

extern "C" {
#include "aeron/asset/opt_model.h"
#include "tie_remaster/flight/renderer.h"
#include "tie_remaster/flight/shadows.h"
#include "tie_app/config/app_config.h"
}

static char g_shadow_config_error[256];

extern "C" void TieDirectionalShadowInspector_Init(void) {
}

extern "C" void TieDirectionalShadowInspector_Shutdown(void) {
}

extern "C" void TieDirectionalShadowInspector_Draw(bool *p_open) {
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Directional Shadows", p_open)) {
        ImGui::End();
        return;
    }

    TieFlightRenderer *gpu = TieFlightRenderer_Current();
    if (!gpu) {
        ImGui::TextDisabled("(No active TieFlightRenderer — start a flight scene)");
        ImGui::End();
        return;
    }

    TieFlightShadowSettings settings;
    TieFlightRenderer_ShadowsGet(gpu, &settings);
    bool changed = false;

    bool enabled = settings.enabled != 0;
    if (ImGui::Checkbox("Enabled", &enabled)) {
        settings.enabled = enabled;
        changed = true;
    }
    static const char *const atlas_names[] = {
			"1024", "2048", "4096", "8192"};
    int atlas_index =
			settings.atlas_size == 1024 ? 0 : settings.atlas_size == 2048 ? 1
									  : settings.atlas_size == 8192       ? 3
																		  : 2;
    if (ImGui::Combo("Atlas size", &atlas_index, atlas_names, 4)) {
        settings.atlas_size = (uint32_t)(1024 << atlas_index);
        changed = true;
    }
    int cascade_count = (int)settings.cascade_count;
    if (ImGui::SliderInt("Cascades", &cascade_count, 1, AERON_SCENE_SHADOW_MAX_CASCADES)) {
        settings.cascade_count = (uint32_t)cascade_count;
        changed = true;
    }
    static const char *const fit_names[] = {"Stable", "Frustum", "Scene dependent"};
    int fit_mode = (int)settings.fit_mode;
    if (ImGui::Combo("Cascade fit", &fit_mode, fit_names, 3)) {
        settings.fit_mode = (uint32_t)fit_mode;
        changed = true;
    }
    changed |= ImGui::SliderFloat("Maximum distance (native view units)",
                                  &settings.max_distance, 1024.0f, 1048576.0f,
                                  "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("%.3f km", settings.max_distance * AERON_OPT_METERS_PER_UNIT / 1000.0f);

    bool explicit_splits = settings.explicit_splits != 0;
    if (ImGui::Checkbox("Explicit splits", &explicit_splits)) {
        settings.explicit_splits = explicit_splits;
        changed = true;
    }
    if (settings.explicit_splits) {
        for (int split = 0;
             split < AERON_SCENE_SHADOW_MAX_CASCADES - 1; split++) {
            char label[24];
            std::snprintf(label, sizeof label, "Split %d position",
                          split + 1);
            const float lower =
					split == 0 ? 0.001f : settings.split_positions[split - 1] + 0.001f;
            const float upper =
					split == 2 ? 0.999f : settings.split_positions[split + 1] - 0.001f;
            changed |= ImGui::SliderFloat(
                label, &settings.split_positions[split], lower, upper,
                "%.3f");
        }
        ImGui::TextDisabled("The first %d position(s) are active.",
                            settings.cascade_count - 1);
    } else {
        changed |= ImGui::SliderFloat("Split lambda",
                                      &settings.split_lambda,
                                      0.0f, 1.0f, "%.2f");
    }

    static const char *const filter_names[] = {
        "Hard",
        "Low (3x3 PCF / 8-tap PCSS)",
        "Medium (5x5 PCF / 16-tap PCSS)",
        "High (7x7 PCF / 24-tap PCSS)",
    };
    int filter_quality = (int)settings.filter_quality;
    if (ImGui::Combo("Filter", &filter_quality, filter_names, 4)) {
        settings.filter_quality = (uint32_t)filter_quality;
        changed = true;
    }
    changed |= ImGui::SliderFloat("Filter radius",
                                  &settings.filter_radius,
                                  0.5f, 3.0f, "%.2f texels");
    bool contact_hardening = settings.contact_hardening != 0;
    if (ImGui::Checkbox("Contact-hardening PCSS", &contact_hardening)) {
        settings.contact_hardening = contact_hardening;
        changed = true;
    }
    if (!settings.contact_hardening) ImGui::BeginDisabled();
    changed |= ImGui::SliderFloat(
        "Light angular radius",
        &settings.light_angular_radius_degrees,
        0.0f, 5.0f, "%.3f deg");
    changed |= ImGui::SliderFloat(
        "Maximum filter radius", &settings.max_filter_radius,
        settings.filter_radius, 16.0f, "%.2f texels");
    changed |= ImGui::SliderFloat(
        "Minimum PCSS radius", &settings.pcss_min_filter_radius,
        0.5f, settings.filter_radius, "%.2f texels");
    if (!settings.contact_hardening) ImGui::EndDisabled();

    changed |= ImGui::SliderFloat("Normal bias (texels)", &settings.normal_bias_texels,
                                  0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Depth bias (texels)",
                                  &settings.depth_bias_texels,
                                  0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Cascade transition",
                                  &settings.transition_fraction,
                                  0.0f, 0.5f, "%.2f");
    changed |= ImGui::SliderFloat("Distance fade",
                                  &settings.distance_fade_fraction,
                                  0.0f, 0.5f, "%.2f");
	bool debug_cascades = settings.debug_cascades != 0;
	if (ImGui::Checkbox("Visualize cascades", &debug_cascades)) {
		settings.debug_cascades = debug_cascades;
		changed = true;
	}

    AeronSceneDirectionalShadowStats stats;
    TieFlightRenderer_ShadowsGetStats(gpu, &stats);
    ImGui::Separator();
    if (stats.active) {
        ImGui::Text("Active: %u cascades, %u atlas",
                    stats.cascade_count, stats.atlas_size);
        ImGui::Text("Candidates: %u, shadow-only dropped: %u",
                    stats.candidate_count, stats.dropped_shadow_only);
        for (uint32_t cascade = 0;
             cascade < stats.cascade_count; cascade++) {
            ImGui::Text(
                "C%u %.0f..%.0f: %u draws, %u triangles, %.3f units/texel (%.3f m)",
                cascade, stats.split_near[cascade],
                stats.split_far[cascade], stats.caster_count[cascade],
                stats.triangle_count[cascade],
                stats.world_units_per_texel[cascade],
                stats.world_units_per_texel[cascade] * AERON_OPT_METERS_PER_UNIT);
        }
    } else {
        ImGui::TextDisabled(
            "Inactive (disabled, non-HD render style, or no flight scene).");
    }

	bool restore = ImGui::Button("Reset to shipped defaults");
	TieAppConfigState *config = TieAppConfig_Current();
	if (restore) {
		if (TieAppConfig_RestoreShadows(
					config, g_shadow_config_error,
					sizeof g_shadow_config_error)) {
			TieFlightRenderer_ShadowsSet(gpu, &config->requested.render.shadows);
			g_shadow_config_error[0] = '\0';
		}
	} else if (changed) {
		if (TieAppConfig_SetShadows(
					config, &settings, g_shadow_config_error,
					sizeof g_shadow_config_error)) {
			TieFlightRenderer_ShadowsSet(gpu, &config->requested.render.shadows);
			g_shadow_config_error[0] = '\0';
		}
    }
    ImGui::TextDisabled(
			"Edits apply on the next frame and are saved as user overrides.");
	if (g_shadow_config_error[0])
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
						   g_shadow_config_error);
    ImGui::TextDisabled(
        "GPU group 'Directional shadows' is atlas rendering.\n"
        "Sampling cost is inside the main PBR color pass.");

    ImGui::End();
}
