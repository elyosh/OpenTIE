/* Runtime PBR lighting controls; world-ambient editing is separate. */

#include <imgui.h>

extern "C" {
#include "tie_remaster/flight/pbr.h"
#include "tie_app/config/app_config.h"
}

namespace {

/* Compact `Reset` button next to each slider. Saves a SameLine + ID
 * push at every call site, and the dotted-vertical layout reads
 * better in the panel. */
bool TiePbrGlobalInspector_ResetButton(const char *id, float *value, float reset_to) {
    ImGui::SameLine();
    ImGui::PushID(id);
    bool clicked = ImGui::SmallButton("R");
    if (clicked) *value = reset_to;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset to %.3f", reset_to);
    ImGui::PopID();
    return clicked;
}

char g_pbr_config_error[256] = {0};

void TiePbrGlobalInspector_ApplyConfig(const TieFlightPbrConfig &config, TieFlightPbrUniforms *uniforms) {
	uniforms->light_intensity = config.light_intensity;
	uniforms->global_spec_mul = config.global_specular_multiplier;
	uniforms->light_wrap = config.light_wrap;
	uniforms->spec_geom_adapt =
			config.geometric_specular_adaptation ? 1.0f : 0.0f;
	TieFlightPbr_SetUniforms(uniforms);
}

}  // namespace

extern "C" void TiePbrInspector_Init(void)
{
}
extern "C" void TiePbrInspector_Shutdown(void)              {}

extern "C" void TiePbrInspector_Draw(bool *p_open)
{
    ImGui::SetNextWindowSize(ImVec2(440, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("PBR — Global", p_open)) {
        ImGui::End();
        return;
    }

    TieFlightPbrUniforms u;
    TieFlightPbr_GetUniforms(&u);
	TieAppConfigState *config = TieAppConfig_Current();
	TieFlightPbrUniforms def = u;
	if (config) {
		def.light_intensity = config->defaults.pbr.light_intensity;
		def.global_spec_mul = config->defaults.pbr.global_specular_multiplier;
		def.light_wrap = config->defaults.pbr.light_wrap;
		def.spec_geom_adapt =
				config->defaults.pbr.geometric_specular_adaptation ? 1.0f : 0.0f;
	}

	bool persistent_dirty = false;
	bool transient_dirty = false;

    ImGui::Text("Lighting");
	if (ImGui::SliderFloat("Light intensity", &u.light_intensity,
						   0.0f, 4.0f, "%.3f")) persistent_dirty = true;
	if (TiePbrGlobalInspector_ResetButton("rli", &u.light_intensity,
					 def.light_intensity)) persistent_dirty = true;
	if (ImGui::SliderFloat("Light wrap", &u.light_wrap,
						   0.0f, 1.0f, "%.3f")) persistent_dirty = true;
	if (TiePbrGlobalInspector_ResetButton("rlw", &u.light_wrap,
					 def.light_wrap)) persistent_dirty = true;
	if (ImGui::SliderFloat("Global spec mul", &u.global_spec_mul,
						   0.0f, 4.0f, "%.3f")) persistent_dirty = true;
	if (TiePbrGlobalInspector_ResetButton("rgs", &u.global_spec_mul,
					 def.global_spec_mul)) persistent_dirty = true;
    ImGui::TextDisabled(
        "Light wrap is a single softness knob: 0 = Lambert (sharp\n"
        "terminator), 1 = Half-Lambert (full hemisphere wrap with\n"
        "squared falloff for a soft curved terminator). Intermediate\n"
        "values blend the linear-wrap and squared responses, so the\n"
        "lower half mostly shifts the terminator while the upper half\n"
        "increasingly curves the falloff near it.");

    ImGui::Separator();
    ImGui::Text("Specular normal adaptation");
    bool spec_adapt = (u.spec_geom_adapt != 0.0f);
    if (ImGui::Checkbox("Geometric-aware spec adaptation", &spec_adapt)) {
        u.spec_geom_adapt = spec_adapt ? 1.0f : 0.0f;
		persistent_dirty = true;
    }
    ImGui::TextDisabled(
        "ON (default): as the shading normal nears the view horizon\n"
        "         (N·V→0) the specular normal blends to the geometric\n"
        "         face normal, so specular reflects the real surface\n"
        "         instead of a hard N·V=0 cutoff. True silhouettes keep\n"
        "         their Fresnel rim. Affects specular only.\n"
        "OFF:          raw shading normal — legacy hard cutoff. Use to\n"
        "         A/B the fix on the low-poly Gouraud hulls.");

    ImGui::Separator();
    /* Term isolation: force the FS to output a single component of
     * the shading composition (or a raw geometric quantity). */
    static const char *isolate_labels[] = {
        "Off (compose normally)",
        "Diffuse only",
        "Specular only (Cook-Torrance)",
        "Geometry term G (grayscale)",
        "Diag: N·V grayscale (saturated)",
        "Diag: N·L grayscale",
        "Diag: normal as RGB",
        "Diag: view vector as RGB",
        "Diag: N·V signed (white=parallel, gray=perp, black=anti)",
    };
    int isolate = (int)(u.debug_isolate_term + 0.5f);
    if (isolate < 0 || isolate > 8) isolate = 0;
    if (ImGui::Combo("Isolate term", &isolate, isolate_labels,
                     IM_ARRAYSIZE(isolate_labels))) {
        u.debug_isolate_term = (float)isolate;
		transient_dirty = true;
    }
    ImGui::TextDisabled(
        "Diffuse / Specular isolate the two composed terms. G shows\n"
        "the Smith-Schlick geometry / visibility factor as grayscale\n"
        "— bright in the middle of the lit hemisphere, falling off at\n"
        "the terminator and at grazing view. Lower roughness narrows\n"
        "the bright region; higher roughness broadens it.\n"
        "\n"
        "Diagnostic modes bypass shading and emissive multipliers entirely:\n"
        "  N·V grayscale: white = face-on, black = silhouette (correct\n"
        "                 surfaces should show smooth dark-rim falloff).\n"
        "  N·L grayscale: white = light-facing, black = back-lit.\n"
        "  Normal as RGB: maps world-space N.xyz → (R, G, B). Constant-\n"
        "                 colour panels indicate flat shading; gradients\n"
        "                 indicate per-vertex Gouraud normals.");

    ImGui::Separator();
	bool restore = ImGui::Button("Reset persistent values");
	if (restore) {
		if (TieAppConfig_RestorePbrGlobals(
					config, g_pbr_config_error,
					sizeof g_pbr_config_error)) {
			TiePbrGlobalInspector_ApplyConfig(config->requested.pbr, &u);
			g_pbr_config_error[0] = '\0';
		}
    }

	if (!restore && persistent_dirty) {
		TieFlightPbrConfig edited = config ? config->requested.pbr
										: TieFlightPbrConfig{};
		edited.light_intensity = u.light_intensity;
		edited.global_specular_multiplier = u.global_spec_mul;
		edited.light_wrap = u.light_wrap;
		edited.geometric_specular_adaptation = spec_adapt;
		if (TieAppConfig_SetPbrGlobals(
					config, &edited, g_pbr_config_error,
					sizeof g_pbr_config_error)) {
			TiePbrGlobalInspector_ApplyConfig(config->requested.pbr, &u);
			g_pbr_config_error[0] = '\0';
		}
	} else if (transient_dirty) {
		TieFlightPbr_SetUniforms(&u);
	}
	if (g_pbr_config_error[0])
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
						   g_pbr_config_error);

    /* ---- Telemetry block — handy for screenshots / bug reports ---- */
    ImGui::Separator();
    ImGui::Text("Live state");
    ImGui::BulletText("Light intensity: %.3f", u.light_intensity);
    ImGui::BulletText("Global spec mul: %.3f", u.global_spec_mul);
    ImGui::End();
}
