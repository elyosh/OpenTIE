/* Session-only point-light tuning and clustered-light telemetry. */

#include <imgui.h>

extern "C" {
#include "tie_remaster/flight/point_lights.h"
}

extern "C" void TiePointLightsEditor_Init(void) {}
extern "C" void TiePointLightsEditor_Shutdown(void) {}

extern "C" void TiePointLightsEditor_Draw(bool* p_open) {
	ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Point Lights", p_open)) {
		ImGui::End();
		return;
	}

	TieFlightPointLightParams params;
	TieFlightPointLights_GetParams(&params);
	bool changed = false;
	changed |= ImGui::Checkbox("Enabled", &params.enabled);
	changed |= ImGui::Checkbox("Clustered (auto)", &params.clustered);
	changed |= ImGui::Checkbox("Cluster occupancy", &params.cluster_debug);
	changed |= ImGui::SliderInt("Depth slices", &params.cluster_depth_slices, 4, 64);

	TieFlightPointLightStats stats;
	TieFlightPointLights_GetStats(&stats);
	ImGui::Text("Candidates %u, valid %u, invalid %u, overflow %u", stats.generated_count,
				stats.valid_count, stats.invalid_count, stats.candidate_overflow_count);
	ImGui::Text("Scene %u accepted, %u dropped; %u global", stats.scene.submitted_light_count,
				stats.scene.dropped_light_count, stats.scene.global_light_count);
	ImGui::Text("%s, tile %u, grid %ux%ux%u (%.1f MiB)",
				stats.scene.clustered_active ? "Clustered" : "Brute force", stats.scene.effective_tile_size,
				stats.scene.grid_x, stats.scene.grid_y, stats.scene.grid_z,
				(double)stats.scene.allocated_buffer_bytes / (1024.0 * 1024.0));

	changed |= ImGui::SliderFloat("Intensity scale", &params.scale, 0.05f, 8.0f, "%.2f",
								 ImGuiSliderFlags_Logarithmic);
	ImGui::TextDisabled("Multiplier on the source intensities; 1.0 uses the configured baseline.");
	changed |= ImGui::SliderFloat("Range scale", &params.range_scale, 0.25f, 8.0f, "%.2f",
								 ImGuiSliderFlags_Logarithmic);
	ImGui::TextDisabled("Multiplies the finite point-light visibility window.");
	changed |= ImGui::SliderFloat("Min distance", &params.min_distance, 4.0f, 1024.0f, "%.0f",
								 ImGuiSliderFlags_Logarithmic);
	ImGui::TextDisabled("Near clamp on the 0.5/d attenuation law in world units.");
	changed |= ImGui::SliderFloat("Specular weight", &params.spec_weight, 0.0f, 2.0f, "%.2f");
	changed |= ImGui::SliderFloat("Diffuse wrap", &params.diffuse_wrap, 0.0f, 1.0f, "%.2f");
	ImGui::TextDisabled("0 = Lambert, 1 = half-Lambert.");
	changed |= ImGui::SliderFloat("Contribution cap", &params.contrib_cap, 0.0f, 4.0f, "%.2f");
	ImGui::TextDisabled("Hue-preserving per-light radiance cap; 0 disables the cap.");

	ImGui::SeparatorText("Training headlight");
	changed |= ImGui::Checkbox("Training headlight enabled", &params.training_headlight_enabled);
	changed |= ImGui::ColorEdit3("Headlight color", params.training_headlight_color);
	changed |= ImGui::SliderFloat("Headlight intensity", &params.training_headlight_intensity, 1.0f, 100000.0f,
								  "%.0f", ImGuiSliderFlags_Logarithmic);
	changed |= ImGui::SliderFloat("Headlight range (m)", &params.training_headlight_range_m, 12.5f, 1600.0f,
								  "%.1f", ImGuiSliderFlags_Logarithmic);
	changed |= ImGui::SliderFloat("Headlight nose offset (m)", &params.training_headlight_nose_offset_m, 0.0f,
								  50.0f, "%.2f");

	ImGui::Separator();
	if (ImGui::Button("Reset to defaults")) {
		TieFlightPointLights_GetDefaultParams(&params);
		changed = true;
	}
	ImGui::TextDisabled("Edits apply on the next frame and are not persisted.\n"
						"Copy desired values into config.yaml under point_lights.");
	if (changed)
		TieFlightPointLights_SetParams(&params);

	ImGui::End();
}
