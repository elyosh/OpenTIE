/* Runtime SSAO controls and AO-buffer visualization. */

#include <imgui.h>

extern "C" {
#include "aeron/asset/opt_model.h"
#include "tie_remaster/flight/renderer.h"
#include "tie_remaster/flight/ssao.h"
#include "tie_app/config/app_config.h"
}

namespace {

bool TieSsaoInspector_ResetButton(const char *id, float *value, float reset_to) {
    ImGui::SameLine();
    ImGui::PushID(id);
    bool clicked = ImGui::SmallButton("R");
    if (clicked) *value = reset_to;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset to %.5f", (double)reset_to);
    ImGui::PopID();
    return clicked;
}

char g_ssao_config_error[256] = {0};

}  // namespace

extern "C" void TieSsaoInspector_Init(void)
{
}
extern "C" void TieSsaoInspector_Shutdown(void)              {}

extern "C" void TieSsaoInspector_Draw(bool *p_open)
{
    ImGui::SetNextWindowSize(ImVec2(440, 360), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("SSAO", p_open)) {
        ImGui::End();
        return;
    }

    TieFlightRenderer *g = TieFlightRenderer_Current();
    if (!g) {
        ImGui::TextDisabled("(No active TieFlightRenderer — start a flight scene)");
        ImGui::End();
        return;
    }

    ImGui::Text("State");
    AeronSceneSsaoSettings settings;
    TieFlightRenderer_SsaoGet(g, &settings);
	bool changed = false;

    /* Quality tier — Off/Low/High. Drives the compute kernel (taps +
     * rotation) and whether the bilateral blur runs. */
    const char *const tier_names[] = { "Off", "Low", "High" };
    int tier = settings.ssao_quality;
    if (ImGui::Combo("Quality", &tier, tier_names, 3)) {
        settings.ssao_quality = tier;
        changed = true;
    }
    ImGui::TextDisabled(
        "Off  = chain skipped (monolithic geometry pass).\n"
        "Low  = 8-tap un-rotated kernel, no bilateral blur.\n"
        "High = 16-tap rotated kernel + separable blur H+V.");

    if (settings.ssao_quality == SSAO_OFF) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
            "Quality = Off → SSAO pass is skipped entirely.");
    } else if (settings.ssao_intensity <= 0.0f) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
            "Intensity = 0 → no occlusion. Raise it below.");
    } else {
        ImGui::BulletText("Pass active — pre-pass (depth+normal) → SSAO");
        ImGui::BulletText("compute/blur → forward color (AO in the FS).");
    }

    ImGui::Separator();
    ImGui::Text("Apply knobs");
	changed |= ImGui::SliderFloat("Intensity", &settings.ssao_intensity, 0.0f, 1.0f, "%.3f");
	changed |= TieSsaoInspector_ResetButton(
			"rint", &settings.ssao_intensity,
			TieAppConfig_Current()
					? TieAppConfig_Current()->defaults.render.ssao.ssao_intensity
					: settings.ssao_intensity);
    ImGui::TextDisabled("0 = AO off, 1 = full effect.");

	changed |= ImGui::SliderFloat(
			"Power", &settings.ssao_power, 0.1f, 8.0f, "%.2f",
			ImGuiSliderFlags_Logarithmic);
	changed |= TieSsaoInspector_ResetButton(
			"rpow", &settings.ssao_power,
			TieAppConfig_Current()
					? TieAppConfig_Current()->defaults.render.ssao.ssao_power
					: settings.ssao_power);
    ImGui::TextDisabled(
        "Nonlinear contrast: `pow(ao, power)`. 1.0 = linear.\n"
        "Higher values push mid-tones toward black for a crunchier\n"
        "AO. Typical 1.0–4.0.");

	changed |= ImGui::SliderFloat(
			"Direct occlusion", &settings.ssao_direct, 0.0f, 1.0f, "%.2f");
	changed |= TieSsaoInspector_ResetButton(
			"rdir", &settings.ssao_direct,
			TieAppConfig_Current()
					? TieAppConfig_Current()->defaults.render.ssao.ssao_direct
					: settings.ssao_direct);
    ImGui::TextDisabled(
        "How much AO darkens the DIRECT sunlight, not just ambient.\n"
        "0 = ambient-only (physically correct, subtle when ambient is\n"
        "low); 1 = direct diffuse fully occluded. Specular / emissive /\n"
        "point lights are never occluded.");

    ImGui::Separator();
    ImGui::Text("Compute knobs");
    changed |= ImGui::SliderFloat("Radius (native view units)", &settings.ssao_radius_view,
                                  1.0f, 16384.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("%.2f metres", settings.ssao_radius_view * AERON_OPT_METERS_PER_UNIT);
    changed |= ImGui::SliderFloat("Bias (native view units)", &settings.ssao_bias_view,
                                  0.0f, 1024.0f, "%.2f");
    ImGui::TextDisabled("%.3f metres", settings.ssao_bias_view * AERON_OPT_METERS_PER_UNIT);
    ImGui::TextDisabled(
        "Depth-compare bias. Raise if you see banding on flat\n"
        "surfaces (self-occlusion), lower for sharper contact AO.");

    ImGui::Separator();
    ImGui::Text("Debug visualisation");
    bool debug_viz = settings.ssao_debug_viz != 0;
    if (ImGui::Checkbox("Show raw AO instead of modulated scene", &debug_viz)) {
        settings.ssao_debug_viz = debug_viz;
        changed = true;
    }
    ImGui::TextDisabled(
        "When on, the apply pass overwrites the scene RT with the\n"
        "AO map as grayscale (white = lit, dark = occluded). Cockpit\n"
        "overlay still draws on top. Useful for diagnosing whether\n"
        "AO is being computed at all vs. simply too subtle to see.");
    if (debug_viz) {
        ImGui::TextColored(ImVec4(0.8f, 1, 0.8f, 1),
            "VIZ ON — what you see IS the AO map.");
    }

    ImGui::Separator();
	TieAppConfigState *config = TieAppConfig_Current();
	bool restore = ImGui::Button("Reset to shared defaults");
	if (restore) {
		if (TieAppConfig_RestoreSsao(
					config, g_ssao_config_error,
					sizeof g_ssao_config_error)) {
			TieFlightRenderer_SsaoSet(g, &config->requested.render.ssao);
			g_ssao_config_error[0] = '\0';
		}
    }
    ImGui::SameLine();
	if (ImGui::Button("Disable (intensity = 0)")) {
		settings.ssao_intensity = 0.0f;
		changed = true;
	}

	if (!restore && changed) {
		if (TieAppConfig_SetSsao(
					config, &settings,
					g_ssao_config_error, sizeof g_ssao_config_error)) {
			TieFlightRenderer_SsaoSet(g, &config->requested.render.ssao);
			g_ssao_config_error[0] = '\0';
		}
	}
	if (g_ssao_config_error[0])
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
						   g_ssao_config_error);

    ImGui::End();
}
