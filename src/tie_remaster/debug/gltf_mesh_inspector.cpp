/*
 * Scene-mesh inspector for cooked GLB and runtime-converted OPT models.
 *
 * Sections:
 *   - Configured model backend.
 *   - Per-species table: loaded scene meshes with their stats.
 *
 * Source policy is fixed by configuration, so this window exposes cache
 * state and reload actions only.
 */

#include <imgui.h>

#include <cstring>

extern "C" {
#include "tie_remaster/flight/renderer.h"
#include "tie_remaster/flight/mesh_draw.h"
}

namespace {

constexpr int kMaxSpecies = 256;

void TieGltfMeshInspector_DrawPipelineToggle(TieFlightRenderer *g)
{
    ImGui::SeparatorText("Configured model backend");
    ImGui::TextUnformatted(TieFlightRenderer_UsesSceneModels(g)
        ? "Scene mesh (GLB or runtime OPT)"
        : "DOS ShipModelData");

    ImGui::SeparatorText("Reload");
    if (ImGui::Button("Reload all loaded .glb")) {
        TieFlightRenderer_SceneRequestReload(g);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Drops every scene-model species cache slot (released GPU "
            "buffers + atlases). The next frame's per-craft ensure "
            "loop re-uploads species currently in the snapshot from "
            "disk; others rebuild lazily when they next appear.");
    }
}

void TieGltfMeshInspector_DrawSpeciesTable(TieFlightRenderer *g)
{
    ImGui::SeparatorText("Loaded ships");
    if (!ImGui::BeginTable("species", 8,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 320.0f)))
        return;
    ImGui::TableSetupColumn("idx",     ImGuiTableColumnFlags_WidthFixed, 36.0f);
    ImGui::TableSetupColumn("species", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("prims",   ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("verts",   ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("mats",    ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("vars",    ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("asset",   ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("act",     ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableHeadersRow();

    int shown = 0;
    for (int i = 0; i < kMaxSpecies; ++i) {
        TieFlightSceneSpeciesInfo info{};
        if (!TieFlightRenderer_SceneSpeciesInfo(g, (uint16_t)i, &info)) continue;
        /* Skip rows with neither a catalog entry nor a ready cache — too
         * noisy to render every empty species_idx. */
        if (!info.ready && info.asset_path[0] == '\0') continue;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%d", (int)info.species_idx);

        ImGui::TableNextColumn();
        if (info.display_name[0])
            ImGui::TextUnformatted(info.display_name);
        else if (info.symbolic_name[0])
            ImGui::TextUnformatted(info.symbolic_name);
        else
            ImGui::TextDisabled("(unnamed)");

        ImGui::TableNextColumn();
        if (info.ready)        ImGui::Text("%u", info.primitive_count);
        else if (info.tried)   ImGui::TextDisabled("fail");
        else                   ImGui::TextDisabled("--");

        ImGui::TableNextColumn();
        if (info.ready) ImGui::Text("%u", info.total_vertex_count);
        else            ImGui::TextDisabled("--");

        ImGui::TableNextColumn();
        if (info.ready) ImGui::Text("%u", info.material_count);
        else            ImGui::TextDisabled("--");

        ImGui::TableNextColumn();
        if (info.ready) ImGui::Text("%u", info.variant_count);
        else            ImGui::TextDisabled("--");

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(info.asset_path[0]
            ? info.asset_path : "(no catalog entry)");

        ImGui::TableNextColumn();
        /* Per-row reload only meaningful for slots with a catalog entry —
         * otherwise the ensure-call has nothing to load. */
        if (info.asset_path[0]) {
            ImGui::PushID(i);
            const bool can_reload = info.ready || info.tried;
            ImGui::BeginDisabled(!can_reload);
            if (ImGui::SmallButton("reload")) {
                TieFlightRenderer_SceneRequestReloadOne(g, (uint16_t)i);
            }
            ImGui::EndDisabled();
            if (can_reload && ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Drop this species' scene-model cache. Rebuilds "
                    "from disk on the next frame it's drawn.");
            }
            ImGui::PopID();
        }
        shown++;
    }
    ImGui::EndTable();
    ImGui::TextDisabled("%d species with a gltf: entry or loaded cache.", shown);
}

}  // namespace

extern "C" void TieGltfMeshInspector_Init(void)
{
}
extern "C" void TieGltfMeshInspector_Shutdown(void)           {}

extern "C" void TieGltfMeshInspector_Draw(bool *p_open)
{
    ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Mesh Inspector", p_open)) {
        ImGui::End();
        return;
    }
    TieFlightRenderer *g = TieFlightRenderer_Current();
    if (!g) {
        ImGui::TextDisabled(
            "TieFlightRenderer not initialised. Start a flight mission or wait\n"
            "for the renderer to come up.");
        ImGui::End();
        return;
    }
    TieGltfMeshInspector_DrawPipelineToggle(g);
    TieGltfMeshInspector_DrawSpeciesTable(g);
    ImGui::End();
}
