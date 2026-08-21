/*
 * Per-mission world-lighting editor.
 *
 * Live-pokes the PBR fragment shader's WorldAmbient cbuffer so the
 * artist can tune the six world-axis ambient RGBs + sun colour
 * against an actual flying ship. Two "Save into library" buttons
 * commit the live edit back into the in-memory library — either to
 * slot 0 (`default:`, fallback for unmentioned battles) or to the
 * currently-active battle's slot. The clipboard export emits the
 * authored slots as YAML for paste into
 * <remaster_dir>/flight/world_ambient.yaml.
 *
 * No "current battle" picker — the editor always operates on the
 * battle the snapshot is rendering. That matches the artist workflow:
 * fly the mission you want to tune, edit, save.
 */

#include <imgui.h>

#include <cstdio>
#include <cstring>

extern "C" {
#include "tie_remaster/flight/pbr.h"
#include "tie_remaster/flight/world_ambient.h"
}

namespace {

constexpr size_t CLIPBOARD_BUF_CAP = 16 * 1024;

struct EditorState {
    bool   last_export_ok   = false;
    size_t last_export_size = 0;
};
EditorState g_state;

/* Edit one (RGB + reset) slot in place. Returns true when the value
 * changed this frame. Layout: ColorEdit3 on the left, "R" reset
 * button on the right. */
	bool TieWorldAmbientEditor_ColorWithReset(const char *label, float val[3], const float def[3]) {
    bool changed = false;
    if (ImGui::ColorEdit3(label, val)) changed = true;
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("R")) {
        memcpy(val, def, sizeof(float) * 3);
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset to (%.3f, %.3f, %.3f)",
                          def[0], def[1], def[2]);
    ImGui::PopID();
    return changed;
}

}  // namespace

extern "C" void TieWorldAmbient_EditorInit(void) {
}
extern "C" void TieWorldAmbient_EditorShutdown(void)              {}

extern "C" void TieWorldAmbient_EditorDraw(bool *p_open) {
    ImGui::SetNextWindowSize(ImVec2(520, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("PBR — World Ambient", p_open)) {
        ImGui::End();
        return;
    }

    TieWorldAmbientCube live, def;
    TieFlightPbr_GetWorldAmbient(&live);
    TieFlightPbr_GetWorldAmbientDefault(&def);

    const int active_battle = TieFlightPbr_GetActiveBattle();
    if (active_battle < 0) {
        ImGui::TextDisabled("No active battle yet (waiting for snapshot).");
    } else {
        ImGui::Text("Active battle: %d", active_battle);
    }
    ImGui::TextDisabled(
        "Live edits push into the PBR fragment shader's WorldAmbient\n"
        "cbuffer on every change — tweak with the ship visible to see\n"
        "the result. Commit into the library to persist across battle\n"
        "swaps, then export to YAML for the bundle.");
    ImGui::Separator();

    bool dirty = false;
    ImGui::Text("Ambient cube (world ±axis)");
    if (TieWorldAmbientEditor_ColorWithReset("+X (right)",   live.pos_x, def.pos_x)) dirty = true;
    if (TieWorldAmbientEditor_ColorWithReset("-X (left)",    live.neg_x, def.neg_x)) dirty = true;
    if (TieWorldAmbientEditor_ColorWithReset("+Y (forward)", live.pos_y, def.pos_y)) dirty = true;
    if (TieWorldAmbientEditor_ColorWithReset("-Y (back)",    live.neg_y, def.neg_y)) dirty = true;
    if (TieWorldAmbientEditor_ColorWithReset("+Z (up)",      live.pos_z, def.pos_z)) dirty = true;
    if (TieWorldAmbientEditor_ColorWithReset("-Z (down)",    live.neg_z, def.neg_z)) dirty = true;
    ImGui::TextDisabled(
        "Six RGBs, one per world axis. The PBR FS blends them by N²\n"
        "along each axis — surfaces facing +Y pick up `pos_y`, facing\n"
        "-Y pick up `neg_y`, etc. For TIE world space: +Y is forward,\n"
        "+Z is up, +X is right (engine convention).");

    ImGui::Separator();
    ImGui::Text("Direct sun");
    if (TieWorldAmbientEditor_ColorWithReset("Sun color", live.sun_color, def.sun_color)) dirty = true;
    ImGui::TextDisabled(
        "Multiplies the sun's lambert + Cook-Torrance contributions.\n"
        "The sun direction comes from the engine snapshot — not editable.");

    if (dirty) TieFlightPbr_SetWorldAmbient(&live);

    ImGui::Separator();
	if (ImGui::Button("Reset live to configured default")) {
        TieFlightPbr_SetWorldAmbient(&def);
    }

    ImGui::Separator();
    ImGui::Text("Save into library");

    /* Default-slot commit: always available, this is the fallback used
     * for any battle without its own override. */
    if (ImGui::Button("Save to default slot")) {
        TieWorldAmbientLibrary lib;
        TieFlightPbr_GetWorldAmbientLibrary(&lib);
        TieWorldAmbientCube cur;
        TieFlightPbr_GetWorldAmbient(&cur);
        lib.slot[0]        = cur;
		lib.authored_mask |= UINT64_C(1);
        TieFlightPbr_SetWorldAmbientLibrary(&lib);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Slot 0 = `default:` in the YAML — used as\n"
                          "fallback for any battle without its own entry.");

    /* Per-battle commit: only meaningful when a battle is actually
     * active. Greyed out otherwise. */
    ImGui::SameLine();
    const bool battle_valid =
        (active_battle >= 0 && active_battle < WORLD_AMBIENT_BATTLE_MAX);
    ImGui::BeginDisabled(!battle_valid);
    char btn_label[64];
    snprintf(btn_label, sizeof btn_label,
             "Save to battle %d slot", active_battle);
    if (ImGui::Button(btn_label)) {
        TieWorldAmbientLibrary lib;
        TieFlightPbr_GetWorldAmbientLibrary(&lib);
        TieWorldAmbientCube cur;
        TieFlightPbr_GetWorldAmbient(&cur);
        int s = active_battle + 1;
        lib.slot[s]        = cur;
		lib.authored_mask |= (UINT64_C(1) << s);
        TieFlightPbr_SetWorldAmbientLibrary(&lib);
        /* Re-resolve so the saved slot becomes live this frame too. */
        TieFlightPbr_RefreshForBattle((uint8_t)active_battle);
    }
    ImGui::EndDisabled();
    if (!battle_valid && ImGui::IsItemHovered())
        ImGui::SetTooltip("Enabled once a battle snapshot has been "
                          "rendered (active_battle != -1).");

    ImGui::Separator();
    if (ImGui::Button("Copy library to clipboard (YAML)")) {
        TieWorldAmbientLibrary lib;
        TieFlightPbr_GetWorldAmbientLibrary(&lib);
        static char buf[CLIPBOARD_BUF_CAP];
        size_t n = TieWorldAmbient_EmitYaml(&lib, buf, sizeof buf);
        if (n > 0) {
            ImGui::SetClipboardText(buf);
            g_state.last_export_ok   = true;
            g_state.last_export_size = n;
        } else {
            g_state.last_export_ok   = false;
            g_state.last_export_size = 0;
        }
    }
    ImGui::TextDisabled(
        "Emits every authored slot. Paste into\n"
        "<remaster_dir>/flight/world_ambient.yaml.");
    if (g_state.last_export_size > 0) {
        if (g_state.last_export_ok) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                               "Copied %zu bytes to clipboard.",
                               g_state.last_export_size);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                               "Export failed (buffer cap %zu B).",
                               CLIPBOARD_BUF_CAP);
        }
    }

    ImGui::End();
}
