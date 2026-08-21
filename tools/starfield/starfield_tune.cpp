#include "imgui.h"
#include <SDL3/SDL.h>

#include "aeron/aeron.h"
#include "aeron/scene/bloom.h"
#include "aeron/scene/present.h"
#include "aeron/scene/scene3d.h"

extern "C" {
#include "starfield_core.h"
#include "starfield_preset.h"
}

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#ifndef TIE_SHADER_RELATIVE_DIR
#define TIE_SHADER_RELATIVE_DIR "shaders"
#endif

namespace {

constexpr float PI_F         = 3.14159265358979f;

struct TieStarfieldTuneApp {
    AeronScene3D          *scene         = nullptr;
    AeronSceneBloom       *bloom         = nullptr;
    AeronScenePresentChain *present_chain = nullptr;
    AeronTexture          *cube          = nullptr;
    AeronSampler          *sampler       = nullptr;
    AeronTextureFormat     present_format = AERON_TEXTURE_FORMAT_UNKNOWN;
    int                    render_w       = 0;
    int                    render_h       = 0;
    int                    cube_face      = 0;
    bool                   frame_ready    = false;
    bool                   bloom_ready    = false;

    /* Persistent CPU face buffers (RGB float), reused each re-render.
     * `base_faces` caches stars+background (recomputed only when star/bg
     * params change); `faces` is base + composited elements. Both
     * reallocated when the bake face size changes. */
    float *faces[6] = {0};
    float *base_faces[6] = {0};
    float *rgba_upload = nullptr;
    int    elem_mask_prev = 0;   /* faces an element occupied last edit */

    TieStarfieldParams params;
    TieStarfieldParams params_prev;       /* memcmp source for dirtiness */
    bool            cube_dirty = true;

    /* Artist-placed elements + their decoded images (parallel arrays). */
    TieStarfieldElement elements[STARFIELD_MAX_ELEMENTS];
    TieStarfieldImage   elem_img[STARFIELD_MAX_ELEMENTS] = {};
    int              n_elements    = 0;
    int              sel_element   = -1;
    bool             elements_dirty = false;

    /* Orbit camera + view controls (preview-only; not part of the
     * baked asset). */
    float yaw = 0.0f, pitch = 0.0f, fov_v_deg = 45.0f;
    float exposure = 1.0f;             /* skybox-sample exposure */
    float bg_srgb[3] = {0, 0, 0};      /* sRGB mirror of params.bg_color */
    bool  show_face_grid = false;      /* cube-face bounds + names overlay */

    /* HDR display state (preview-only). */
    bool  hdr_supported = false;
    bool  hdr_enabled   = false;
    bool  want_hdr      = false;
    bool  hdr_requested = false;
    float peak_scale    = 1.0f;
    bool  imgui_linear  = false;       /* ImGui style is in linear space */
    ImVec4 style_srgb[ImGuiCol_COUNT]; /* saved SDR-authored style colours */

    /* Path state + async native file dialog. */
    char        out_path[1024]    = "stars.ktx2";
    char        preset_path[1024] = "starfield.tune.yaml";
    std::string status;
    bool        dialog_open = false;
    std::mutex  dialog_mutex;
    int         dialog_callback_action = 0;
    int         dialog_result_action = 0;
    bool        dialog_result_ready = false;
    std::string dialog_result_path;
};

/* ---------- GPU resources ---------- */

bool TieStarfieldTune_CreateResources(TieStarfieldTuneApp &a)
{
    AeronSamplerDesc sampler{};
    sampler.min_filter = AERON_FILTER_LINEAR;
    sampler.mag_filter = AERON_FILTER_LINEAR;
    sampler.mip_filter = AERON_FILTER_LINEAR;
    sampler.address_u = AERON_ADDRESS_CLAMP_TO_EDGE;
    sampler.address_v = AERON_ADDRESS_CLAMP_TO_EDGE;
    sampler.address_w = AERON_ADDRESS_CLAMP_TO_EDGE;
    sampler.max_lod = 1000.0f;
    a.sampler = Aeron_CreateSampler(&sampler);

    /* Cube + face buffers are sized lazily in rebuild_cube to the
     * current bake face size, so the preview renders at exactly the
     * resolution (and therefore the star size) of the baked asset. */
    return a.sampler != nullptr;
}

/* (Re)allocate the preview cube + CPU face buffers for `face`-pixel
 * faces. Releases any prior storage first. */
bool TieStarfieldTune_ResizeCubeStorage(TieStarfieldTuneApp &a, int face)
{
    Aeron_DestroyTexture(a.cube);
    a.cube = nullptr;
    std::free(a.rgba_upload);
    a.rgba_upload = nullptr;
    for (int f = 0; f < 6; ++f) {
        std::free(a.faces[f]);      a.faces[f] = nullptr;
        std::free(a.base_faces[f]); a.base_faces[f] = nullptr;
    }

    AeronTextureDesc cube{};
    cube.width = face;
    cube.height = face;
    cube.mip_count = 1;
    cube.format = AERON_TEXTURE_FORMAT_RGBA32_FLOAT;
    cube.usage = AERON_TEXTURE_USAGE_SAMPLED | AERON_TEXTURE_USAGE_TRANSFER_DST;
    cube.cube = 1;
    cube.debug_name = "starfield_tune.preview_cube";
    a.cube = Aeron_CreateTexture(&cube);

    bool ok = a.cube != nullptr;
    size_t texels = (size_t)face * (size_t)face;
    for (int f = 0; f < 6; ++f) {
        a.faces[f]      = (float *)std::calloc(texels * 3, sizeof(float));
        a.base_faces[f] = (float *)std::calloc(texels * 3, sizeof(float));
        if (!a.faces[f] || !a.base_faces[f]) ok = false;
    }
    a.rgba_upload = (float *)std::malloc(texels * 4 * sizeof(float));
    if (!a.rgba_upload) ok = false;
    if (!ok) {
        Aeron_DestroyTexture(a.cube);
        a.cube = nullptr;
        for (int f = 0; f < 6; ++f) {
            std::free(a.faces[f]);
            std::free(a.base_faces[f]);
            a.faces[f] = nullptr;
            a.base_faces[f] = nullptr;
        }
        std::free(a.rgba_upload);
        a.rgba_upload = nullptr;
    }
    a.cube_face = ok ? face : 0;
    return ok;
}

/* Render stars+background into the cached base faces (the expensive
 * step). Resizes storage if the bake face size changed. */
bool TieStarfieldTune_ComputeBase(TieStarfieldTuneApp &a)
{
    int face = a.params.face_size;
    if (face != a.cube_face && !TieStarfieldTune_ResizeCubeStorage(a, face)) return false;
    if (!a.cube) return false;
    TieStarfieldCore_StarfieldRenderFaces(&a.params, a.base_faces);   /* fills bg + stars */
    return true;
}

/* Bitmask of faces any enabled element can touch. Uses the same cone as
 * the compositor's quick-reject plus a small margin, so it never
 * under-selects (over-selecting only re-uploads an unchanged face). */
int TieStarfieldTune_ElementFaceMask(TieStarfieldTuneApp &a)
{
    static const float N[6][3] = {
        { 1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
    int mask = 0;
    for (int i = 0; i < a.n_elements; ++i) {
        const TieStarfieldElement &e = a.elements[i];
        if (!e.enabled || !a.elem_img[i].rgba) continue;
        float cp = std::cos(e.pitch), sp = std::sin(e.pitch);
        float cy = std::cos(e.yaw),   sy = std::sin(e.yaw);
        float D[3] = { cp*sy, sp, cp*cy };
        float tan_v = std::tan(0.5f * e.size_deg * PI_F / 180.f);
        float tan_h = tan_v * ((float)a.elem_img[i].w / (float)a.elem_img[i].h);
        float cone  = std::atan(std::sqrt(tan_h*tan_h + tan_v*tan_v));
        float rc    = std::cos(0.9553f + cone + 0.1f);   /* +margin */
        for (int f = 0; f < 6; ++f)
            if (D[0]*N[f][0]+D[1]*N[f][1]+D[2]*N[f][2] >= rc) mask |= 1 << f;
    }
    return mask;
}

/* Reset the masked faces from the cached base, re-composite the
 * elements onto them, and upload only those faces. */
bool TieStarfieldTune_Recomposite(TieStarfieldTuneApp &a, AeronCommandBuffer *cmd, int mask)
{
    if (!a.cube || !cmd || !a.rgba_upload || a.cube_face < 1 || mask == 0) return false;
    int face = a.cube_face;
    size_t texels = (size_t)face * face;

    for (int f = 0; f < 6; ++f)
        if (mask & (1 << f))
            std::memcpy(a.faces[f], a.base_faces[f], texels * 3 * sizeof(float));

    /* Each element writes only the faces it touches — all within `mask`
     * (mask ⊇ the union of current element faces), which we just reset. */
    for (int i = 0; i < a.n_elements; ++i)
        TieStarfieldElements_StarfieldCompositeElement(a.faces, face, &a.elements[i],
                                    &a.elem_img[i]);

    for (int f = 0; f < 6; ++f) {
        if (!(mask & (1 << f))) continue;
        const float *src = a.faces[f];
        for (int y = 0; y < face; ++y) {
            for (int x = 0; x < face; ++x) {
                size_t source = ((size_t)y * face + (size_t)(face - 1 - x)) * 3;
                size_t dest = ((size_t)y * face + (size_t)x) * 4;
                a.rgba_upload[dest+0] = src[source+0];
                a.rgba_upload[dest+1] = src[source+1];
                a.rgba_upload[dest+2] = src[source+2];
                a.rgba_upload[dest+3] = 1.0f;
            }
        }
        AeronTextureUploadDesc upload{};
        upload.texture = a.cube;
        upload.width = face;
        upload.height = face;
        upload.raw_data = a.rgba_upload;
        upload.raw_size = (uint32_t)(texels * 4 * sizeof(float));
        /* Match TieStarfieldCore_StarfieldMirrorCubeX without mutating the
         * natural-space CPU faces used by element placement. */
        upload.layer = f == 0 ? 1 : f == 1 ? 0 : f;
        if (!Aeron_UploadTextureDataCmd(cmd, &upload)) return false;
    }
    return true;
}

/* Build Aeron's world-to-eye camera quaternion from the orbit controls. */
void TieStarfieldTune_FillCamera(const TieStarfieldTuneApp &a, float aspect, AeronSceneCamera &camera)
{
    float pitch_s = std::sin(0.5f * a.pitch);
    float pitch_c = std::cos(0.5f * a.pitch);
    float yaw_s = std::sin(0.5f * a.yaw);
    float yaw_c = std::cos(0.5f * a.yaw);
    camera.ori[0] = pitch_c * yaw_c;
    camera.ori[1] = pitch_s * yaw_c;
    camera.ori[2] = -pitch_c * yaw_s;
    camera.ori[3] = -pitch_s * yaw_s;
    camera.v_half_rad = 0.5f * a.fov_v_deg * PI_F / 180.0f;
    camera.h_half_rad = std::atan(std::tan(camera.v_half_rad) * aspect);
    camera.near_z = 0.001f;
}

bool TieStarfieldTune_EnsureScene(TieStarfieldTuneApp &a, int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (a.scene && a.bloom && a.render_w == width && a.render_h == height) return true;

    AeronScene3DDesc desc{};
    desc.rt_width = width;
    desc.rt_height = height;
    desc.color_format = AERON_TEXTURE_FORMAT_R11G11B10_UFLOAT;
    desc.sample_count = AERON_SAMPLE_COUNT_1;
    desc.temporal_mode = AERON_TEMPORAL_OFF;
    desc.view_space_to_meters = 1.0f;
    AeronScene3D *scene = AeronScene_Create(&desc);
    AeronSceneBloom *bloom = scene ? AeronSceneBloom_Create(width, height) : nullptr;
    if (!scene || !bloom) {
        AeronSceneBloom_Destroy(bloom);
        AeronScene_Destroy(scene);
        return false;
    }

    AeronSceneBloom_Destroy(a.bloom);
    AeronScene_Destroy(a.scene);
    a.scene = scene;
    a.bloom = bloom;
    a.render_w = width;
    a.render_h = height;
    a.frame_ready = false;
    a.bloom_ready = false;
    return true;
}

bool TieStarfieldTune_EnsurePresentChain(TieStarfieldTuneApp &a)
{
    AeronTextureFormat format = Aeron_SwapchainFormat();
    if (a.present_chain && a.present_format == format) return true;
    AeronScenePresentChain *replacement = AeronScenePresentChain_Create(format);
    if (!replacement) return false;
    AeronScenePresentChain_Destroy(a.present_chain);
    a.present_chain = replacement;
    a.present_format = format;
    return true;
}

bool TieStarfieldTune_RenderScene(TieStarfieldTuneApp &a, AeronCommandBuffer *cmd)
{
    if (!a.scene || !a.cube || !cmd || a.render_w <= 0 || a.render_h <= 0) return false;
    AeronSceneCamera camera{};
    TieStarfieldTune_FillCamera(a, (float)a.render_w / (float)a.render_h, camera);
    if (!AeronScene_Begin(a.scene, &camera)) return false;
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    AeronScene_SetClearColor(a.scene, clear);
    AeronScene_SetSkyCube(a.scene, a.cube, nullptr, a.exposure);
    if (!AeronScene_Render(a.scene, cmd)) return false;

    a.bloom_ready = false;
    if (AeronSceneBloom_Intensity() > 0.0f) {
        if (!AeronSceneBloom_Apply(a.bloom, cmd, AeronScene_ColorTexture(a.scene),
                                   a.render_w, a.render_h, a.render_h))
            return false;
        a.bloom_ready = true;
    }
    a.frame_ready = true;
    return true;
}

void TieStarfieldTune_DrawSceneToSwapchain(AeronCommandBuffer *cmd, AeronRenderPass *pass,
                                            AeronRenderTarget *, int, int, void *userdata)
{
    TieStarfieldTuneApp &a = *(TieStarfieldTuneApp *)userdata;
    if (!a.frame_ready || !a.present_chain || !a.scene || !a.sampler) return;
    AeronRenderTarget *scene_rt = AeronScene_SceneRt(a.scene);
    AeronRenderTarget *bloom_rt = a.bloom_ready ? AeronSceneBloom_ColorRt(a.bloom) : nullptr;
    static const float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    AeronScenePresentChain_Draw(a.present_chain, pass, Aeron_RenderTargetGetTexture(scene_rt),
                                a.sampler, bloom_rt ? Aeron_RenderTargetGetTexture(bloom_rt) : nullptr,
                                a.bloom_ready ? AeronSceneBloom_Intensity() : 0.0f,
                                a.render_w, a.render_h, 1.0f, tint, 0);
    (void)cmd;
}

/* Orbit the camera from mouse drag/wheel, but only when ImGui is not
 * using the mouse (i.e. the cursor is over the skybox background, not a
 * control window). */
void TieStarfieldTune_HandleCamera(TieStarfieldTuneApp &a)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        a.yaw   += io.MouseDelta.x * 0.005f;
        a.pitch += io.MouseDelta.y * 0.005f;
        const float lim = 0.5f * PI_F - 0.01f;
        if (a.pitch >  lim) a.pitch =  lim;
        if (a.pitch < -lim) a.pitch = -lim;
    }
    if (io.MouseWheel != 0.0f) {
        a.fov_v_deg -= io.MouseWheel * 3.0f;
        if (a.fov_v_deg < 20.0f)  a.fov_v_deg = 20.0f;
        if (a.fov_v_deg > 120.0f) a.fov_v_deg = 120.0f;
    }
}

/* ---------- HDR display ---------- */

static float TieStarfieldElements_SrgbToLinear(float s)
{
    return s <= 0.04045f ? s / 12.92f
                         : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

static float TieStarfieldTune_LinearToSrgb(float l)
{
    if (l <= 0.0f) return 0.0f;
    if (l >= 1.0f) return 1.0f;
    return l <= 0.0031308f ? l * 12.92f
                           : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}

/* The baked bg_color is linear; the UI edits it in sRGB so a dark sky
 * sits at pickable values instead of ~0.004 linear. Refresh the sRGB
 * mirror after anything that changes params.bg_color out-of-band (load,
 * startup). */
void TieStarfieldTune_SyncBgSrgb(TieStarfieldTuneApp &a)
{
    for (int i = 0; i < 3; ++i)
        a.bg_srgb[i] = TieStarfieldTune_LinearToSrgb(a.params.bg_color[i]);
}

/* ---------- elements ---------- */

void TieStarfieldTune_ReloadElementImage(TieStarfieldTuneApp &a, int i)
{
    TieStarfieldElements_StarfieldImageFree(&a.elem_img[i]);
    if (a.elements[i].path[0])
        TieStarfieldElements_StarfieldImageLoad(a.elements[i].path, &a.elem_img[i]);
}

/* Reload every element's decoded image (after a preset load / startup). */
void TieStarfieldTune_ReloadAllElementImages(TieStarfieldTuneApp &a)
{
    for (int i = 0; i < a.n_elements; ++i) TieStarfieldTune_ReloadElementImage(a, i);
}

void TieStarfieldTune_AddElement(TieStarfieldTuneApp &a, const char *path)
{
    if (a.n_elements >= STARFIELD_MAX_ELEMENTS) {
        a.status = "Element limit reached";
        return;
    }
    int i = a.n_elements++;
    TieStarfieldElements_StarfieldElementDefault(&a.elements[i]);
    std::snprintf(a.elements[i].path, STARFIELD_PATH_MAX, "%s", path);
    a.elem_img[i] = TieStarfieldImage{};
    TieStarfieldTune_ReloadElementImage(a, i);
    a.sel_element = i;
    a.elements_dirty = true;
    a.status = a.elem_img[i].rgba ? std::string("Added ") + path
                                  : std::string("Load failed: ") + path;
}

void TieStarfieldTune_RemoveElement(TieStarfieldTuneApp &a, int i)
{
    if (i < 0 || i >= a.n_elements) return;
    TieStarfieldElements_StarfieldImageFree(&a.elem_img[i]);
    for (int k = i; k < a.n_elements - 1; ++k) {
        a.elements[k] = a.elements[k + 1];
        a.elem_img[k] = a.elem_img[k + 1];
    }
    a.elem_img[a.n_elements - 1] = TieStarfieldImage{};
    a.n_elements--;
    if (a.sel_element >= a.n_elements) a.sel_element = a.n_elements - 1;
    a.elements_dirty = true;
}

/* ImGui authors style colours as sRGB-display values; on a linear scRGB
 * swapchain they must be sRGB→linear decoded or the panels read washed
 * out. Snapshot the SDR style once, then swap rgb (not alpha) between
 * the saved sRGB set and a linearized copy as HDR toggles. */
void TieStarfieldTune_CaptureStyle(TieStarfieldTuneApp &a)
{
    const ImVec4 *c = ImGui::GetStyle().Colors;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) a.style_srgb[i] = c[i];
}

void TieStarfieldTune_ApplyStyleSpace(TieStarfieldTuneApp &a, bool linear)
{
    if (linear == a.imgui_linear) return;
    ImVec4 *c = ImGui::GetStyle().Colors;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        const ImVec4 s = a.style_srgb[i];
        c[i] = linear ? ImVec4(TieStarfieldElements_SrgbToLinear(s.x), TieStarfieldElements_SrgbToLinear(s.y),
                               TieStarfieldElements_SrgbToLinear(s.z), s.w)
                      : s;
    }
    a.imgui_linear = linear;
}

/* ---------- native file dialog ---------- */

void TieStarfieldTune_DialogCb(void *userdata, const char * const *filelist, int /*filter*/)
{
    TieStarfieldTuneApp *a = (TieStarfieldTuneApp *)userdata;
    std::lock_guard<std::mutex> lock(a->dialog_mutex);
    a->dialog_result_action = filelist && filelist[0] ? a->dialog_callback_action : 0;
    a->dialog_result_path = filelist && filelist[0] ? filelist[0] : "";
    a->dialog_result_ready = true;
}

bool TieStarfieldTune_BeginDialog(TieStarfieldTuneApp &a, int action)
{
    if (a.dialog_open) return false;
    std::lock_guard<std::mutex> lock(a.dialog_mutex);
    a.dialog_callback_action = action;
    a.dialog_result_action = 0;
    a.dialog_result_path.clear();
    a.dialog_result_ready = false;
    a.dialog_open = true;
    return true;
}

void TieStarfieldTune_DrainDialog(TieStarfieldTuneApp &a)
{
    int action = 0;
    std::string picked;
    {
        std::lock_guard<std::mutex> lock(a.dialog_mutex);
        if (!a.dialog_result_ready) return;
        action = a.dialog_result_action;
        picked = a.dialog_result_path;
        a.dialog_result_ready = false;
        a.dialog_callback_action = 0;
    }
    a.dialog_open = false;
    if (action == 0 || picked.empty()) return;

    char err[256] = {0};
    switch (action) {
    case 1:
        std::snprintf(a.out_path, sizeof a.out_path, "%s", picked.c_str());
        a.status = TieStarfieldCore_StarfieldBakeKtx2(&a.params, a.elements,
                                        a.n_elements, picked.c_str())
                  ? std::string("Baked ") + picked
                  : std::string("Bake FAILED: ") + picked;
        break;
    case 2:
        std::snprintf(a.preset_path, sizeof a.preset_path, "%s", picked.c_str());
        a.status = TieStarfieldPreset_Save(picked.c_str(), &a.params, a.elements,
                                          a.n_elements, err, sizeof err)
                  ? std::string("Saved ") + picked
                  : std::string("Save failed: ") + err;
        break;
    case 3:
        std::snprintf(a.preset_path, sizeof a.preset_path, "%s", picked.c_str());
        if (TieStarfieldPreset_Load(picked.c_str(), &a.params, a.elements,
                                  &a.n_elements, STARFIELD_MAX_ELEMENTS,
                                  err, sizeof err)) {
            a.status = std::string("Loaded ") + picked;
            a.cube_dirty = true;
            a.elements_dirty = true;
            a.sel_element = a.n_elements - 1;
            TieStarfieldTune_ReloadAllElementImages(a);
            TieStarfieldTune_SyncBgSrgb(a);
        } else {
            a.status = std::string("Load failed: ") + err;
        }
        break;
    case 4:
        TieStarfieldTune_AddElement(a, picked.c_str());
        break;
    }
}

/* ---------- face overlay ---------- */

/* Draw the 12 cube edges (tessellated so face boundaries curve through
 * the camera) plus a +X/-Y… label at each front-facing face centre,
 * into the ImGui background draw list over the skybox. Projects world
 * directions through the same orbit camera the skybox uses. */
void TieStarfieldTune_DrawFaceOverlay(TieStarfieldTuneApp &a)
{
    ImGuiIO &io = ImGui::GetIO();
    float sw = io.DisplaySize.x, sh = io.DisplaySize.y;
    if (sw < 1.f || sh < 1.f) return;

    float cp = std::cos(a.pitch), sp = std::sin(a.pitch);
    float cyy = std::cos(a.yaw),  syy = std::sin(a.yaw);
    float F[3] = { cp*syy, sp, cp*cyy };
    float wup[3] = { 0, 1, 0 };
    float R[3] = { wup[1]*F[2]-wup[2]*F[1], wup[2]*F[0]-wup[0]*F[2],
                   wup[0]*F[1]-wup[1]*F[0] };
    float rl = std::sqrt(R[0]*R[0]+R[1]*R[1]+R[2]*R[2]);
    if (rl < 1e-5f) { R[0]=1; R[1]=0; R[2]=0; rl=1; }
    R[0]/=rl; R[1]/=rl; R[2]/=rl;
    float U[3] = { F[1]*R[2]-F[2]*R[1], F[2]*R[0]-F[0]*R[2], F[0]*R[1]-F[1]*R[0] };
    float tan_v = std::tan(0.5f * a.fov_v_deg * PI_F / 180.f);
    float tan_h = tan_v * sw / sh;

    auto project = [&](float wx, float wy, float wz, ImVec2 &out) -> bool {
        float ex =  R[0]*wx + R[1]*wy + R[2]*wz;
        float ey = -(U[0]*wx + U[1]*wy + U[2]*wz);
        float ez =  F[0]*wx + F[1]*wy + F[2]*wz;
        if (ez <= 1e-3f) return false;          /* behind the camera */
        out.x = ((ex/ez)/tan_h * 0.5f + 0.5f) * sw;
        out.y = (0.5f + (ey/ez)/tan_v * 0.5f) * sh;
        return true;
    };

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const ImU32 line = IM_COL32(120, 180, 255, 150);
    const int NSEG = 32;
    for (int axis = 0; axis < 3; ++axis) {
        int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
        for (int s = 0; s < 4; ++s) {
            float v[3];
            v[a1] = (s & 1) ? 1.f : -1.f;
            v[a2] = (s & 2) ? 1.f : -1.f;
            ImVec2 prev; bool have = false;
            for (int i = 0; i <= NSEG; ++i) {
                v[axis] = -1.f + 2.f * (float)i / NSEG;
                float inv = 1.f / std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
                ImVec2 p;
                bool ok = project(v[0]*inv, v[1]*inv, v[2]*inv, p);
                if (ok && have) dl->AddLine(prev, p, line, 1.5f);
                prev = p; have = ok;
            }
        }
    }

    /* Labels drawn at an explicit (larger) font size via the draw-list
     * overload, so the overlay text scales independently of the UI font. */
    ImFont *font    = ImGui::GetFont();
    float   fsize   = ImGui::GetFontSize() * 2.5f;
    float   tscale  = fsize / ImGui::GetFontSize();
    float   shadow  = fsize * 0.06f;
    static const float C[6][3] = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
    static const char *NM[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    for (int f = 0; f < 6; ++f) {
        if (C[f][0]*F[0]+C[f][1]*F[1]+C[f][2]*F[2] <= 0.15f) continue; /* front */
        ImVec2 p;
        if (!project(C[f][0], C[f][1], C[f][2], p)) continue;
        ImVec2 ts = ImGui::CalcTextSize(NM[f]);
        ImVec2 at(p.x - ts.x * tscale * 0.5f, p.y - ts.y * tscale * 0.5f);
        dl->AddText(font, fsize, ImVec2(at.x+shadow, at.y+shadow),
                    IM_COL32(0,0,0,200), NM[f]);
        dl->AddText(font, fsize, at, IM_COL32(160, 210, 255, 255), NM[f]);
    }
}

/* ---------- UI ---------- */

void TieStarfieldTune_BuildUi(TieStarfieldTuneApp &a)
{
    ImGui::Begin("Starfield");

    if (ImGui::CollapsingHeader("Counts", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragInt("stars", &a.params.num_stars, 50.0f, 1, 200000);
        long long seed = (long long)a.params.seed;
        if (ImGui::InputScalar("seed", ImGuiDataType_S64, &seed))
            a.params.seed = (uint64_t)(seed < 0 ? 0 : seed);
        ImGui::SameLine();
        if (ImGui::Button("Reroll"))
            a.params.seed = Aeron_NowUs();
        ImGui::SliderFloat("intensity", &a.params.intensity, 0.1f, 16.0f,
                           "%.2f");
    }

    if (ImGui::CollapsingHeader("Brightness distribution",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("floor", &a.params.bright_floor, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("power", &a.params.bright_pow, 0.5f, 12.0f, "%.2f");
        ImGui::SeparatorText("size tiers (thresh = brightness cutoff, "
                             "sigma = star size in 3840px-ref pixels)");
        for (int i = 0; i < STARFIELD_TIERS; ++i) {
            ImGui::PushID(i);
            ImGui::SliderFloat("thresh", &a.params.tier_thresh[i], 0.0f, 1.0f,
                               "%.3f");
            ImGui::SliderFloat("sigma", &a.params.tier_sigma[i], 0.25f, 4.0f,
                               "%.2f");
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Colour", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("tint sigma", &a.params.tint_sigma, 0.0f, 0.5f,
                           "%.3f");
        ImGui::SliderFloat("tint strength", &a.params.tint_strength, 0.0f, 3.0f,
                           "%.2f");
        ImGui::SliderFloat("tint bias", &a.params.tint_bias, -0.4f, 0.4f,
                           "%.3f");
        ImGui::SameLine();
        ImGui::TextDisabled("(- bluer / + warmer)");
        /* Edited in sRGB (a dark sky is a normal dark-blue swatch);
         * stored linear for the bake. */
        if (ImGui::ColorEdit3("background", a.bg_srgb))
            for (int i = 0; i < 3; ++i)
                a.params.bg_color[i] = TieStarfieldElements_SrgbToLinear(a.bg_srgb[i]);
    }

    if (ImGui::CollapsingHeader("Elements (planets / moons)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Add image…") && TieStarfieldTune_BeginDialog(a, 4)) {
            static const SDL_DialogFileFilter filters[] = { { "Image", "png;hdr" } };
            SDL_ShowOpenFileDialog(TieStarfieldTune_DialogCb, &a, nullptr, filters, 1, nullptr,
                                   false);
        }
        /* List. */
        for (int i = 0; i < a.n_elements; ++i) {
            ImGui::PushID(i);
            const char *p = a.elements[i].path;
            const char *base = std::strrchr(p, '/');
            base = base ? base + 1 : p;
            char label[128];
            std::snprintf(label, sizeof label, "%s%s", base,
                          a.elem_img[i].rgba ? "" : " (load failed)");
            if (ImGui::Selectable(label, a.sel_element == i))
                a.sel_element = i;
            ImGui::PopID();
        }

        if (a.sel_element >= 0 && a.sel_element < a.n_elements) {
            TieStarfieldElement &e = a.elements[a.sel_element];
            ImGui::SeparatorText("selected element");
            bool ch = false;
            ch |= ImGui::Checkbox("enabled", &e.enabled);
            if (ImGui::Button("Place at view centre")) {
                e.yaw = a.yaw; e.pitch = a.pitch; ch = true;
            }
            ch |= ImGui::SliderAngle("yaw",   &e.yaw,  -180.0f, 180.0f);
            ch |= ImGui::SliderAngle("pitch", &e.pitch, -89.0f,  89.0f);
            ch |= ImGui::SliderFloat("size (deg)", &e.size_deg, 0.5f, 120.0f,
                                     "%.1f");
            ch |= ImGui::SliderFloat("roll (deg)", &e.roll_deg, -180.0f, 180.0f,
                                     "%.0f");
            ch |= ImGui::SliderFloat("intensity", &e.intensity, 0.0f, 8.0f,
                                     "%.2f");
            ch |= ImGui::ColorEdit3("tint", e.tint);
            if (ImGui::Button("Remove")) { TieStarfieldTune_RemoveElement(a, a.sel_element); ch = true; }
            if (ch) a.elements_dirty = true;
        }
    }

    if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("exposure", &a.exposure, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("fov", &a.fov_v_deg, 20.0f, 120.0f, "%.0f");
        if (ImGui::Button("Reset camera")) { a.yaw = a.pitch = 0.0f; }
        ImGui::Checkbox("Face overlay (+X / -Y …)", &a.show_face_grid);

        if (a.hdr_supported) {
            /* Aeron applies the request between ImGui frames. */
            ImGui::Checkbox("HDR display (engine AgX)", &a.want_hdr);
            if (a.hdr_enabled)
                ImGui::Text("display headroom %.2fx SDR white",
                            (double)a.peak_scale);
        } else {
            ImGui::TextDisabled("HDR display: not supported here");
        }
    }

    if (ImGui::CollapsingHeader("Output / bake",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        static const int sizes[] = { 256, 512, 1024, 2048 };
        int cur = 2;
        for (int i = 0; i < 4; ++i) if (sizes[i] == a.params.face_size) cur = i;
        const char *labels[] = { "256", "512", "1024", "2048" };
        if (ImGui::Combo("face size", &cur, labels, 4))
            a.params.face_size = sizes[cur];
        ImGui::Checkbox("zstd", &a.params.zstd);
        ImGui::InputText("path", a.out_path, sizeof a.out_path);
        if (ImGui::Button("Bake to path")) {
            a.status = TieStarfieldCore_StarfieldBakeKtx2(&a.params, a.elements,
                                           a.n_elements, a.out_path)
                     ? std::string("Baked ") + a.out_path
                     : std::string("Bake FAILED: ") + a.out_path;
        }
        ImGui::SameLine();
        if (ImGui::Button("Bake KTX2…") && TieStarfieldTune_BeginDialog(a, 1)) {
            static const SDL_DialogFileFilter filters[] = { { "KTX2 cubemap", "ktx2" } };
            SDL_ShowSaveFileDialog(TieStarfieldTune_DialogCb, &a, nullptr, filters, 1,
                                   a.out_path);
        }
    }

    if (ImGui::CollapsingHeader("Preset", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Save preset…") && TieStarfieldTune_BeginDialog(a, 2)) {
            static const SDL_DialogFileFilter filters[] = { { "tune YAML", "yaml" } };
            SDL_ShowSaveFileDialog(TieStarfieldTune_DialogCb, &a, nullptr, filters, 1,
                                   a.preset_path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load preset…") && TieStarfieldTune_BeginDialog(a, 3)) {
            static const SDL_DialogFileFilter filters[] = { { "tune YAML", "yaml" } };
            SDL_ShowOpenFileDialog(TieStarfieldTune_DialogCb, &a, nullptr, filters, 1,
                                   a.preset_path, false);
        }
    }

    ImGui::TextWrapped("Drag the background to orbit; wheel to zoom. "
                       "FOV %.0f°", (double)a.fov_v_deg);
    if (!a.status.empty())
        ImGui::TextWrapped("%s", a.status.c_str());

    ImGui::End();

    if (a.show_face_grid) TieStarfieldTune_DrawFaceOverlay(a);
}

void TieStarfieldTune_DrawApplication(void *userdata)
{
    TieStarfieldTuneApp &a = *(TieStarfieldTuneApp *)userdata;
    TieStarfieldTune_ApplyStyleSpace(a, a.hdr_enabled);
    TieStarfieldTune_BuildUi(a);
    TieStarfieldTune_HandleCamera(a);
}

void TieStarfieldTune_Destroy(TieStarfieldTuneApp &a)
{
    AeronScenePresentChain_Destroy(a.present_chain);
    AeronSceneBloom_Destroy(a.bloom);
    AeronScene_Destroy(a.scene);
    Aeron_DestroyTexture(a.cube);
    Aeron_DestroySampler(a.sampler);
    std::free(a.rgba_upload);
    for (int f = 0; f < 6; ++f) { std::free(a.faces[f]); std::free(a.base_faces[f]); }
    for (int i = 0; i < a.n_elements; ++i) TieStarfieldElements_StarfieldImageFree(&a.elem_img[i]);
}

}  // namespace

int main(int argc, char **argv)
{
    TieStarfieldTuneApp app;
    TieStarfieldCore_StarfieldDefaultParams(&app.params);
    if (app.params.seed == 0) app.params.seed = 1;   /* deterministic start */

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
            char err[256];
            if (!TieStarfieldPreset_Load(argv[++i], &app.params, app.elements,
                                       &app.n_elements, STARFIELD_MAX_ELEMENTS,
                                       err, sizeof err))
                std::fprintf(stderr, "starfield_tune: --preset: %s\n", err);
            else
                std::snprintf(app.preset_path, sizeof app.preset_path, "%s",
                              argv[i]);
        }
    }

    AeronConfig config{};
    config.org_name = "tie";
    config.app_name = "starfield_tune";
    config.shader_path = TIE_SHADER_RELATIVE_DIR;
    config.window_title = "starfield_tune";
    config.window_width = 1400;
    config.window_height = 900;
    config.logical_width = 1400;
    config.logical_height = 900;
    config.presentation_mode = AERON_PRESENTATION_STRETCH;
    config.clear_color_enabled = 1;
    config.clear_color_rgba[3] = 1.0f;
    if (!Aeron_Init(&config)) {
        std::fprintf(stderr, "starfield_tune: Aeron_Init failed\n");
        return 1;
    }
    if (!Aeron_DebugUiAvailable()) {
        std::fprintf(stderr, "starfield_tune: Aeron debug UI support is required\n");
        Aeron_Shutdown();
        return 1;
    }
    if (!TieStarfieldTune_CreateResources(app)) {
        std::fprintf(stderr, "starfield_tune: GPU resource init failed\n");
        Aeron_Shutdown();
        return 1;
    }

    Aeron_DebugSetApplication(TieStarfieldTune_DrawApplication, &app);
    Aeron_DebugUiSetVisible(1);
    TieStarfieldTune_CaptureStyle(app);
    TieStarfieldTune_SyncBgSrgb(app);
    TieStarfieldTune_ReloadAllElementImages(app);

    while (!Aeron_QuitRequested() && !Aeron_FatalErrorRequested()) {
        if (app.want_hdr != app.hdr_requested) {
            if (Aeron_SetOutputHdr(app.want_hdr ? 1 : 0)) {
                app.hdr_requested = app.want_hdr;
            } else {
                app.want_hdr = app.hdr_requested;
                app.status = std::string("HDR switch failed: ") + Aeron_RenderLastError();
            }
        }
        Aeron_BeginFrame();
        app.hdr_supported = Aeron_OutputSupportsHdr() != 0;
        app.hdr_enabled = Aeron_OutputHdrEnabled() != 0;
        app.peak_scale = Aeron_OutputHdrHeadroom();
        TieStarfieldTune_DrainDialog(app);

        int render_w = 0;
        int render_h = 0;
        if (!Aeron_GetPresentationPixelSize(&render_w, &render_h) ||
            !TieStarfieldTune_EnsureScene(app, render_w, render_h) ||
            !TieStarfieldTune_EnsurePresentChain(app)) {
            Aeron_RequestFatalRendererError("starfield preview resource preparation");
            break;
        }

        AeronCommandBuffer *cmd = Aeron_AcquireCommandBuffer();
        if (!cmd) {
            Aeron_RequestFatalRendererError("starfield command-buffer acquisition");
            break;
        }

        /* Star/background params changed → recompute the cached base and
         * re-composite every face. Only elements changed → re-composite
         * just the faces they touch (and the ones they just left). The
         * camera/exposure are applied per-frame in the draw pass. */
        bool params_changed =
            std::memcmp(&app.params, &app.params_prev,
                        sizeof(TieStarfieldParams)) != 0;
        bool ok = true;
        if (params_changed || app.cube_dirty) {
            if (TieStarfieldTune_ComputeBase(app)) {
                ok = TieStarfieldTune_Recomposite(app, cmd, 0x3F);
                if (ok) app.elem_mask_prev = TieStarfieldTune_ElementFaceMask(app);
            } else ok = false;
            if (ok) {
                app.params_prev = app.params;
                app.cube_dirty = false;
                app.elements_dirty = false;
            }
        } else if (app.elements_dirty) {
            int cur = TieStarfieldTune_ElementFaceMask(app);
            ok = TieStarfieldTune_Recomposite(app, cmd, cur | app.elem_mask_prev);
            if (ok) {
                app.elem_mask_prev = cur;
                app.elements_dirty = false;
            }
        }
        if (ok) ok = TieStarfieldTune_RenderScene(app, cmd);
        if (!ok) {
            Aeron_CancelCommandBuffer(cmd);
            Aeron_RequestFatalRendererError("starfield frame recording");
            break;
        }
        if (!Aeron_SubmitCommandBuffer(cmd)) {
            Aeron_RequestFatalRendererError("starfield command-buffer submission");
            break;
        }

        AeronSwapchainRenderLayerDesc layer{};
        layer.callback = TieStarfieldTune_DrawSceneToSwapchain;
        layer.userdata = &app;
        layer.required_width = render_w;
        layer.required_height = render_h;
        layer.debug_label = "Starfield preview present";
        if (!Aeron_SubmitSwapchainRenderLayer(&layer) || !Aeron_Present()) {
            Aeron_RequestFatalRendererError("starfield presentation");
            break;
        }
    }

    int exit_status = Aeron_FatalErrorRequested() ? 1 : 0;
    Aeron_DebugSetApplication(nullptr, nullptr);
    TieStarfieldTune_Destroy(app);
    Aeron_Shutdown();
    return exit_status;
}
