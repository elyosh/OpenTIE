/*
 * TIE debug tools — registry glue for the Aeron debug overlay.
 *
 * The tool .cpp files keep the sdl3 application's <name>_init / _shutdown /
 * _draw(bool*) shape; this TU adapts each _draw to Aeron's
 * AeronDebugToolFn (int* open) and registers them in menu order.
 */

#include "tie_remaster/debug/debug_tools.h"

#include "aeron/debug.h"

extern "C" {
    void TieCameraInspector_Init      (void);
    void TieCameraInspector_Shutdown  (void);
    void TieCameraInspector_Draw      (bool *p_open);

    void TiePbrInspector_Init    (void);
    void TiePbrInspector_Shutdown(void);
    void TiePbrInspector_Draw    (bool *p_open);

    void TieWorldAmbient_EditorInit    (void);
    void TieWorldAmbient_EditorShutdown(void);
    void TieWorldAmbient_EditorDraw    (bool *p_open);

    void TiePointLightsEditor_Init     (void);
    void TiePointLightsEditor_Shutdown (void);
    void TiePointLightsEditor_Draw     (bool *p_open);

    void TieGltfMeshInspector_Init     (void);
    void TieGltfMeshInspector_Shutdown (void);
    void TieGltfMeshInspector_Draw     (bool *p_open);

    void TieSsaoInspector_Init          (void);
    void TieSsaoInspector_Shutdown      (void);
    void TieSsaoInspector_Draw          (bool *p_open);

    void TieDirectionalShadowInspector_Init    (void);
    void TieDirectionalShadowInspector_Shutdown(void);
    void TieDirectionalShadowInspector_Draw    (bool *p_open);

    void TieMotionBlurInspector_Init     (void);
    void TieMotionBlurInspector_Shutdown (void);
    void TieMotionBlurInspector_Draw     (bool *p_open);

    void TieHdrInspector_Init           (void);
    void TieHdrInspector_Shutdown       (void);
    void TieHdrInspector_Draw           (bool *p_open);

    void TieCockpitInspector_Init       (void);
    void TieCockpitInspector_Shutdown   (void);
    void TieCockpitInspector_Draw       (bool *p_open);
}

typedef struct TieDebugTool {
    const char *menu_label;
    void      (*init)(void);
    void      (*shutdown)(void);
    void      (*draw)(bool *p_open);
} TieDebugTool;

static const TieDebugTool g_tie_tools[] = {
    { "Camera / World Inspector", TieCameraInspector_Init,
      TieCameraInspector_Shutdown,  TieCameraInspector_Draw },
    { "PBR — Global",             TiePbrInspector_Init,
      TiePbrInspector_Shutdown, TiePbrInspector_Draw },
    { "PBR — World Ambient",      TieWorldAmbient_EditorInit,
      TieWorldAmbient_EditorShutdown, TieWorldAmbient_EditorDraw },
    { "Point Lights",             TiePointLightsEditor_Init,
      TiePointLightsEditor_Shutdown, TiePointLightsEditor_Draw },
    { "glTF Mesh Inspector",      TieGltfMeshInspector_Init,
      TieGltfMeshInspector_Shutdown, TieGltfMeshInspector_Draw },
    { "SSAO",                     TieSsaoInspector_Init,
      TieSsaoInspector_Shutdown,    TieSsaoInspector_Draw },
    { "Directional Shadows",      TieDirectionalShadowInspector_Init,
      TieDirectionalShadowInspector_Shutdown,
      TieDirectionalShadowInspector_Draw },
    { "Motion Blur",              TieMotionBlurInspector_Init,
      TieMotionBlurInspector_Shutdown, TieMotionBlurInspector_Draw },
    { "HDR & Display",            TieHdrInspector_Init,
      TieHdrInspector_Shutdown,     TieHdrInspector_Draw },
    { "Cockpit",                  TieCockpitInspector_Init,
      TieCockpitInspector_Shutdown, TieCockpitInspector_Draw },
};
static const int g_tie_tool_count =
    (int)(sizeof g_tie_tools / sizeof g_tie_tools[0]);

/* Adapter: Aeron passes int* visibility, the tools take bool*. `user`
 * is the TieDebugTool entry. */
static void TieDebugTools_TieDebugToolThunk(int *open, void *user)
{
    const TieDebugTool *tool = (const TieDebugTool *)user;
    bool b = *open != 0;
    tool->draw(&b);
    *open = b ? 1 : 0;
}

extern "C" void TieDebugTools_Register(void)
{
    if (!Aeron_DebugUiAvailable()) return;
    for (int i = 0; i < g_tie_tool_count; ++i) {
        if (g_tie_tools[i].init) g_tie_tools[i].init();
        Aeron_DebugRegisterTool(g_tie_tools[i].menu_label,
                                TieDebugTools_TieDebugToolThunk,
                                (void *)&g_tie_tools[i]);
    }
}

extern "C" void TieDebugTools_Shutdown(void)
{
    if (!Aeron_DebugUiAvailable()) return;
    for (int i = 0; i < g_tie_tool_count; ++i)
        if (g_tie_tools[i].shutdown) g_tie_tools[i].shutdown();
}
