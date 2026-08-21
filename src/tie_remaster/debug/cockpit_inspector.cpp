/*
 * Cockpit inspector.
 *
 * Artist-iteration tooling for the cockpit overlay. The reload button
 * drops every cached cockpit asset (layout YAML, base canopy bitmap,
 * HUD parts atlas, damage overlay, CRT mask) so they re-read from disk
 * on the next frame — edit the per-cockpit YAML or re-export an atlas,
 * click, and see it live without restarting.
 */

#include "tie_remaster/flight/cockpit/renderer.h"

#include <imgui.h>

extern "C" void TieCockpitInspector_Init(void)
{
}

extern "C" void TieCockpitInspector_Shutdown(void)
{
}

extern "C" void TieCockpitInspector_Draw(bool *p_open)
{
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cockpit", p_open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reload cockpit from disk"))
        TieCockpitRenderer_RequestReload();

    ImGui::TextDisabled(
        "Re-reads the active cockpit's layout YAML, canopy bitmap, HUD\n"
        "parts atlas, damage overlay, and CRT mask. Edit the per-cockpit\n"
        "*_hud_layout.yaml (positions, hdr_boost) or re-export an atlas,\n"
        "then click to see it live. Takes effect on the next frame.");

    ImGui::End();
}
