/*
 * HDR & Display inspector — Aeron application variant.
 *
 * The sdl3 application's hdr_state (mode + nits calibration) is gone; HDR
 * output is a single desired-on/off flag owned by TieRemaster, with the peak
 * scale
 * derived from the display's reported headroom. This tool surfaces
 * that flag, the flight tonemap knobs, and read-only diagnostics.
 */

#include "tie_remaster/flight/renderer.h"
#include "tie_remaster/remaster.h"

#include "aeron/render.h"

#include <imgui.h>

extern "C" void TieHdrInspector_Init(void)
{
}

extern "C" void TieHdrInspector_Shutdown(void)
{
}

static const char *TieHdrInspector_FormatName(AeronTextureFormat f)
{
    switch (f) {
    case AERON_TEXTURE_FORMAT_RGBA8_UNORM:      return "RGBA8 UNORM";
    case AERON_TEXTURE_FORMAT_RGBA8_SRGB:       return "RGBA8 sRGB";
    case AERON_TEXTURE_FORMAT_BGRA8_UNORM:      return "BGRA8 UNORM";
    case AERON_TEXTURE_FORMAT_BGRA8_SRGB:       return "BGRA8 sRGB";
    case AERON_TEXTURE_FORMAT_RGBA16_FLOAT:     return "RGBA16F (HDR)";
    case AERON_TEXTURE_FORMAT_R10G10B10A2_UNORM: return "RGB10A2";
    default:                                    return "(other)";
    }
}

extern "C" void TieHdrInspector_Draw(bool *p_open)
{
    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HDR & Display", p_open)) {
        ImGui::End();
        return;
    }

    const bool hdr_available = Aeron_OutputSupportsHdr() != 0;
    const bool hdr_active    = Aeron_OutputHdrEnabled() != 0;

    /* ---- Output mode ---- */
    ImGui::TextUnformatted("Output");
    TieVideoOptions video_options{};
    TieRemaster_GetVideoOptions(&video_options);
    bool want_hdr = video_options.hdr;
    if (ImGui::Checkbox("HDR output (when available)", &want_hdr)) {
        video_options.hdr = want_hdr;
        TieRemaster_ApplyVideoOptions(&video_options);
    }
    if (!hdr_available && want_hdr)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
                           "HDR unavailable on this display — SDR composition.");

    static const char *content_gamma_names[] = { "2.2", "2.4", "sRGB" };
    int content_gamma = static_cast<int>(video_options.sdr_content_gamma);
#if defined(__APPLE__)
    const bool tone_mapping_editable = false;
#else
    const bool tone_mapping_editable = hdr_active;
#endif
    ImGui::BeginDisabled(!tone_mapping_editable);
    const int gamma_count = content_gamma == TIE_SDR_CONTENT_GAMMA_SRGB ? 3 : 2;
    if (ImGui::Combo("SDR content gamma", &content_gamma,
                     content_gamma_names, gamma_count)) {
        video_options.sdr_content_gamma = static_cast<TieSdrContentGamma>(content_gamma);
        TieRemaster_ApplyVideoOptions(&video_options);
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!tone_mapping_editable);
    float paper_white = video_options.paper_white_nits;
    bool automatic_paper_white = video_options.paper_white_auto;
    if (ImGui::Checkbox("Automatic paper white", &automatic_paper_white)) {
        video_options.paper_white_auto = automatic_paper_white;
        if (!automatic_paper_white)
            video_options.paper_white_nits = 203.0f;
        TieRemaster_ApplyVideoOptions(&video_options);
    }
    if (!automatic_paper_white &&
        ImGui::SliderFloat("Paper white", &paper_white,
                           80.0f, 1000.0f, "%.0f nits")) {
        video_options.paper_white_nits = paper_white;
        TieRemaster_ApplyVideoOptions(&video_options);
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    /* ---- Tonemap operator ---- */
    ImGui::TextUnformatted("Tonemap operator");
    int op = AeronScenePresent_TonemapOp();
    int new_op = op;
    ImGui::RadioButton("ACES",             &new_op, AERON_SCENE_TONEMAP_ACES);
    ImGui::SameLine();
    ImGui::RadioButton("AGX (parametric)", &new_op, AERON_SCENE_TONEMAP_AGX_PARAMETRIC);
    if (new_op != op) AeronScenePresent_SetTonemapOp(new_op);
    ImGui::TextDisabled(
        "Both operators are inline ALU implementations (no texture taps).\n"
        "AGX is a polynomial approximation of the AgX default-contrast\n"
        "sigmoid; ACES is the Stephen Hill 2017 fit.");

    int agx_look = AeronScenePresent_AgxLook();
    int new_agx_look = agx_look;
    ImGui::TextUnformatted("AgX look");
    ImGui::RadioButton("Base", &new_agx_look, AERON_SCENE_AGX_LOOK_BASE);
    ImGui::SameLine();
    ImGui::RadioButton("Punchy", &new_agx_look, AERON_SCENE_AGX_LOOK_PUNCHY);
    if (new_agx_look != agx_look)
        AeronScenePresent_SetAgxLook(new_agx_look);
    if (new_agx_look == AERON_SCENE_AGX_LOOK_PUNCHY) {
        float punchy_power = AeronScenePresent_AgxPunchyPower();
        if (ImGui::SliderFloat("Punchy power", &punchy_power, 0.5f, 2.0f, "%.2f"))
            AeronScenePresent_SetAgxPunchyPower(punchy_power);
        float punchy_saturation = AeronScenePresent_AgxPunchySaturation();
        if (ImGui::SliderFloat("Punchy saturation", &punchy_saturation, 0.0f, 2.0f, "%.2f"))
            AeronScenePresent_SetAgxPunchySaturation(punchy_saturation);
    }

    /* ---- ACES pre-exposure ---- */
    ImGui::TextUnformatted("ACES pre-exposure");
    float aces_exp = AeronScenePresent_AcesExposure();
    if (ImGui::SliderFloat("##acesexp", &aces_exp, 1.0f, 3.0f, "%.2fx"))
        AeronScenePresent_SetAcesExposure(aces_exp);
    ImGui::TextDisabled(
        "Scene multiplier applied before the ACES fit (SDR and HDR).\n"
        "The Hill RRT+ODT crushes shadows/low-mids vs AgX; this lifts\n"
        "the toe to match. 1.6 anchors ACES mid-grey on AgX's; 1.0 is\n"
        "the raw fit. No effect when the AGX operator is selected.");

    /* ---- AgX EOTF exponent ---- */
    ImGui::TextUnformatted("Parametric AgX EOTF exponent");
    float gamma = AeronScenePresent_EotfExponent();
    if (ImGui::SliderFloat("##eotf", &gamma, 1.8f, 2.6f, "%.3f"))
        AeronScenePresent_SetEotfExponent(gamma);
    ImGui::TextDisabled(
        "Tail pow() in the inline (parametric) AgX paths — SDR and HDR.\n"
        "Reference values: 2.2 (Mikamiko / Three.js / Bevy inline convention,\n"
        "and what HDR has shipped with) ; 2.4 (sRGB spec upper-segment).");

    ImGui::Separator();

    /* ---- Bloom present kernel ---- */
    ImGui::TextUnformatted("Bloom present kernel");
    int bk = AeronScenePresent_BloomKernel();
    int new_bk = bk;
    ImGui::RadioButton("1 tap",  &new_bk, AERON_SCENE_BLOOM_KERNEL_1_TAP);
    ImGui::SameLine();
    ImGui::RadioButton("4 taps", &new_bk, AERON_SCENE_BLOOM_KERNEL_4_TAP);
    if (new_bk != bk) AeronScenePresent_SetBloomKernel(new_bk);
    ImGui::TextDisabled(
        "Controls how bloom_mip0 is sampled in the final present FS.\n"
        "4-tap is the original ±1-mip-texel box; 1-tap is a single\n"
        "bilinear sample (~75%% less bloom-read traffic on RGBA16F).");

    ImGui::Separator();

    /* ---- Diagnostics ---- */
    ImGui::TextUnformatted("Diagnostics");
    ImGui::Text("HDR status       : %s",
                Aeron_OutputHdrStatusName(Aeron_OutputHdrStatus()));
    ImGui::Text("HDR requested    : %s", want_hdr ? "yes" : "no");
    ImGui::Text("HDR available    : %s", hdr_available ? "yes" : "no");
    ImGui::Text("HDR active       : %s",
                hdr_active ? "yes (HDR_EXTENDED_LINEAR)" : "no (SDR)");
    ImGui::Text("Display headroom : %.2fx (drives the HDR peak scale)",
                (double)Aeron_OutputHdrHeadroom());
    ImGui::Text("SDR white level  : %.3f", (double)Aeron_OutputSdrWhiteLevel());
    ImGui::Text("SDR content gamma: %s",
                Aeron_OutputSdrContentGamma() == 0.0f ? "piecewise sRGB"
                                                      : "power curve");
    if (Aeron_OutputSdrContentGamma() != 0.0f) {
        ImGui::SameLine();
        ImGui::Text("(%.2f)", (double)Aeron_OutputSdrContentGamma());
    }
    ImGui::Text("Paper white      : %s",
                Aeron_OutputPaperWhiteNits() == 0.0f ? "OS default"
                                                     : "override");
    if (Aeron_OutputPaperWhiteNits() != 0.0f) {
        ImGui::SameLine();
        ImGui::Text("(%.0f nits)",
                    (double)Aeron_OutputPaperWhiteNits());
    }
    ImGui::Text("Render driver    : %s", Aeron_RenderDriverName());
    ImGui::Text("Swapchain format : %s", TieHdrInspector_FormatName(Aeron_SwapchainFormat()));

    ImGui::End();
}
