/*
 * Motion-blur inspector — quality tier (Off/Low/High), shutter, and a
 * velocity-buffer debug-viz toggle. The viz false-colours velocity_rt
 * over the scene so the per-object motion and the camera-rotational sky
 * fill can be verified (the starfield shows a smooth rotational gradient
 * as the camera turns).
 *
 * All state goes through the TieFlightRenderer_Mb* API so this tool needs no
 * renderer internals.
 */

#include <imgui.h>

extern "C" {
#include "tie_remaster/flight/renderer.h"
#include "tie_remaster/flight/motion_blur.h"
}

extern "C" void TieMotionBlurInspector_Init(void)
{
}
extern "C" void TieMotionBlurInspector_Shutdown(void)           {}

extern "C" void TieMotionBlurInspector_Draw(bool *p_open)
{
    ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Motion Blur", p_open)) {
        ImGui::End();
        return;
    }

    TieFlightRenderer *g = TieFlightRenderer_Current();
    if (!g) {
        ImGui::TextDisabled("(No active TieFlightRenderer — start a flight scene)");
        ImGui::End();
        return;
    }

    MbQuality quality    = TieFlightRenderer_MbGetQuality(g);
    float     shutter    = TieFlightRenderer_MbGetShutter(g);
    bool      viz        = TieFlightRenderer_MbGetVelocityViz(g);
    bool      keep_pause = TieFlightRenderer_MbGetPauseKeepBlur(g);
    bool      cam_blur   = TieFlightRenderer_MbGetCameraBlur(g);

    /* Quality tier. Switching to/from Off lazily (re)allocates
     * velocity_rt inside the setter. */
    const char *const tier_names[] = { "Off", "Low", "High" };
    int tier = (int)quality;
    if (ImGui::Combo("Quality", &tier, tier_names, 3))
        TieFlightRenderer_MbSetQuality(g, (MbQuality)tier);
    ImGui::TextDisabled(
        "Off = no velocity buffer; renderer flow unchanged.\n"
        "Low = velocity-weighted gather (8 taps) along own velocity.\n"
        "High = adds TileMax/NeighborMax so fast objects streak onto\n"
        "       adjacent pixels (16 taps).\n"
        "HD style only — ignored in classic / XvT.");

    if (quality == MB_OFF) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
            "Quality = Off → velocity_rt / mb_rt not allocated.");
    } else {
        ImGui::BulletText("2-RT pre-pass + sky fill → velocity_rt.");
        ImGui::BulletText("Resolve color_rt → mb_rt; cockpit drawn sharp.");
    }

    ImGui::Separator();
    if (ImGui::SliderFloat("Shutter", &shutter, 0.0f, 8.0f, "%.2f"))
        TieFlightRenderer_MbSetShutter(g, shutter);
    if (ImGui::SmallButton("Reset shutter")) TieFlightRenderer_MbSetShutter(g, 0.5f);
    ImGui::TextDisabled(
        "Blur length. ~0.5 = 180 deg (physical); push higher to\n"
        "exaggerate while verifying. Auto-scaled by frame time; the\n"
        "blur is still clamped to a max screen radius.");

    if (ImGui::Checkbox("Keep blur when paused", &keep_pause))
        TieFlightRenderer_MbSetPauseKeepBlur(g, keep_pause);
    ImGui::TextDisabled(
        "On (default): a paused frame holds the last motion's blur —\n"
        "pause mid-turn to inspect the streak. Off: crisp still.");

    if (ImGui::Checkbox("Blur camera motion", &cam_blur))
        TieFlightRenderer_MbSetCameraBlur(g, cam_blur);
    ImGui::TextDisabled(
        "On (default): camera pans / turns blur the whole frame.\n"
        "Off: only moving ships and bolts streak; panning a static\n"
        "scene stays crisp (object-only motion blur).");

    ImGui::Separator();
    if (ImGui::Checkbox("Velocity debug viz", &viz))
        TieFlightRenderer_MbSetVelocityViz(g, viz);
    ImGui::TextDisabled(
        "False-colour velocity_rt over the scene: +X velocity -> red,\n"
        "+Y -> green, magnitude -> blue. Turn the camera and the\n"
        "starfield should show a smooth rotational gradient.\n"
        "No-op when Quality = Off.");

    ImGui::End();
}
