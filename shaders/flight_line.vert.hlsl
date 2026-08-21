/*
 * Flight-overlay line vertex shader — expands a 4-vertex/6-index quad
 * input into a screen-facing thin band the engine's
 * drawpol_drawlineface produces.
 *
 * All 4 vertices of one line share the SAME (pos_a, pos_b) — the
 * line direction is consistent across the quad's corners so the
 * computed perpendicular doesn't flip sign between endpoints. Per
 * corner:
 *   `endpoint` (0.0 or 1.0) picks which of pos_a / pos_b this vertex
 *               sits at.
 *   `side`     (-1.0 or +1.0) selects which side of the line.
 *
 * Engine thickness math (drawpol.c:418):
 *     thick_px = (thicknessMultiple × thickness_base) / (avg_eye_z >> 8)
 * with eye_z in native engine world units. Our clip.w uses the same
 * native view scale, so `>> 8` is represented by division by 256.
 *
 * Host-pushed uniforms (LineVSUniforms):
 *   pixel_to_clip_xy  — (2/viewport_w, 2/viewport_h). One screen
 *                       pixel = this vec in NDC; before perspective
 *                       divide, multiply by clip.w.
 *   thickness_mul     — engine VGA=1, SVGA=2 equivalent, scaled to
 *                       the HD render resolution.
 *
 * Fragment shader is shared with flight_mesh_classic_lut.frag.
 */

/* Must mirror flight_mesh.vert.hlsl's MeshVSUniforms layout
 * byte-for-byte so a single host-side push to slot 0 feeds both
 * shaders. Lines only consume view_proj, craft_to_world, and
 * directional_dir; the rest is declared for layout parity. */
cbuffer MeshVSUniforms : register(b0, space1)
{
    row_major float4x4 view_proj;
    row_major float4x4 craft_to_world;
    float              light_local_frame;
    float              gouraud_enabled;
    float2             _pad_gv;
    float3             directional_dir;
    float              _pad_dir;
    row_major float3x3 world_to_craft;
    float3             craft_world_pos;
    float              _pad_cwp;
    uint               mesh_table_index;
    uint3              _pad_mesh_table;
};

cbuffer LineVSUniforms : register(b1, space1)
{
    float2 pixel_to_clip_xy;
    float  thickness_mul;
    /* Tunable engine-pixel floor multiplier. Default 0.5 = 1 SVGA
     * pixel of screen coverage. */
    float  line_floor_mul;
};

/* Same per-mesh affine + visibility cbuffer the triangle pipeline
 * uses — antennas on a rotating turret animate with it, and antennas
 * on a destroyed mesh disappear with it. */
#include "flight_mesh_table.hlsli"

struct VSIn
{
    float3 pos_a          : POSITION;
    float3 pos_b          : POSITION1;
    float3 normal         : NORMAL;
    float  endpoint       : COLOR0;
    float  side           : COLOR1;
    float  color          : COLOR2;
    float  material_id    : COLOR3;
    float  thickness_base : COLOR4;
    float  mesh_index     : COLOR5;
};

struct VSOut
{
    float4 position     : SV_Position;
    /* Signed scalar lambert — same contract as flight_mesh.vert.hlsl
     * so the shared FS can consume one interpolant regardless of
     * pipeline. Lines are flat-shaded (no Gouraud-eligibility). */
    float  raw_dot      : TEXCOORD2;
    float  v_color      : COLOR0;
    float  v_material   : COLOR1;
    float  v_highlight  : COLOR2;
    /* Per-mesh emissive multiplier from MeshTableUniforms.mesh_emissive.
     *
     * NOTE: declaration order is load-bearing — SPIRV-Cross assigns
     * user(locn) by declaration index, so this MUST sit at the same
     * index as in the mesh VS and the shared FS. */
    float  v_emissive   : COLOR3;
    /* Layout parity with flight_mesh.vert.hlsl. Lines have no face
     * plane; the FS's decal loop short-circuits at decal_count == 0. */
    float  face_u       : TEXCOORD3;
    float  face_v       : TEXCOORD4;
    float  decal_offset : TEXCOORD5;
    float  decal_count  : TEXCOORD6;
    float  v_markings_enabled : TEXCOORD7;
    /* Coloured local-light accumulation. Lines never accumulate local
     * lights (the engine's drawpol_drawlineface uses vertexlight only
     * for the directional dot, and there's no local-light loop on the
     * line path) — ship 0 for layout parity with the mesh VS. */
    float3 local_rgb    : COLOR4;
};

VSOut main(VSIn v)
{
    uint mi = (uint)clamp((int)round(v.mesh_index), 0,
                         AERON_MAX_MESH_SLOTS - 1);

    /* Visibility — destroyed meshes hide their lines too. Push the
     * vertex behind the camera (negative w) so the rasterizer drops
     * every triangle for this quad. */
    float vis = flight_mesh_table_scalar(mesh_table_index,
                                         AERON_MESH_VISIBILITY_OFFSET, mi);
    if (vis < 0.5f) {
        VSOut hidden;
        hidden.position     = float4(0.0f, 0.0f, 0.0f, -1.0f);
        hidden.raw_dot      = 0.0f;
        hidden.v_color      = v.color;
        hidden.v_material   = v.material_id;
        hidden.v_highlight  = 0.0f;
        hidden.face_u       = 0.0f;
        hidden.face_v       = 0.0f;
        hidden.decal_offset = 0.0f;
        hidden.decal_count  = 0.0f;
        hidden.v_markings_enabled = 0.0f;
        hidden.v_emissive   = 1.0f;
        hidden.local_rgb    = float3(0.0f, 0.0f, 0.0f);
        return hidden;
    }

    /* Apply the per-mesh affine to BOTH endpoints and the normal. */
    float4 r0 = flight_mesh_table_row(mesh_table_index, mi, 0);
    float4 r1 = flight_mesh_table_row(mesh_table_index, mi, 1);
    float4 r2 = flight_mesh_table_row(mesh_table_index, mi, 2);
    float3 a_rot, b_rot, n_rot;
    a_rot.x = dot(r0.xyz, v.pos_a) + r0.w;
    a_rot.y = dot(r1.xyz, v.pos_a) + r1.w;
    a_rot.z = dot(r2.xyz, v.pos_a) + r2.w;
    b_rot.x = dot(r0.xyz, v.pos_b) + r0.w;
    b_rot.y = dot(r1.xyz, v.pos_b) + r1.w;
    b_rot.z = dot(r2.xyz, v.pos_b) + r2.w;
    n_rot.x = dot(r0.xyz, v.normal);
    n_rot.y = dot(r1.xyz, v.normal);
    n_rot.z = dot(r2.xyz, v.normal);

    bool at_b = v.endpoint > 0.5f;
    float3 this_local = at_b ? b_rot : a_rot;

    float4 world_p     = mul(craft_to_world, float4(this_local, 1.0f));
    float4 world_a     = mul(craft_to_world, float4(a_rot,      1.0f));
    float4 world_b     = mul(craft_to_world, float4(b_rot,      1.0f));
    float3 world_n     = mul((float3x3)craft_to_world, n_rot);

    float4 clip_this = mul(view_proj, world_p);
    float4 clip_a    = mul(view_proj, world_a);
    float4 clip_b    = mul(view_proj, world_b);

    /* Project A and B to screen pixels. */
    float2 ndc_a  = clip_a.xy / max(clip_a.w, 1e-6f);
    float2 ndc_b  = clip_b.xy / max(clip_b.w, 1e-6f);
    float2 dir_px = (ndc_b - ndc_a) / pixel_to_clip_xy;
    float  len_px = length(dir_px);
    float2 perp_px = (len_px > 1e-3f)
                   ? float2(-dir_px.y, dir_px.x) / len_px
                   : float2(0.0f, 0.0f);

    /* Engine pixel width — see flight_mesh_classic_lut.frag.hlsl
     * line-marking branch for the engine derivation.
     *
     * Mirrors drawpol_drawlineface (drawpol.c:438-440): only divides
     * by `(eye_z >> 8)` when it's > 0; inside the cap, thickness
     * stays at `base × multiplier + 1`. */
    float avg_w     = max(0.5f * (clip_a.w + clip_b.w), 1e-3f);
    float ez_scaled = max(avg_w / 256.0f, 1.0f);
    /* engine-SVGA-faithful: `× 0.5` on the floor IS the engine's
     * `+1 SVGA pixel` (1/480), expressed in HD pixels given that
     * thickness_mul already encodes (2 × HD-px-per-SVGA-px). */
    float thick_px  = (thickness_mul * v.thickness_base) / ez_scaled
                    + thickness_mul * line_floor_mul;

    /* Offset in pixel space → NDC → clip space. */
    float2 offset_clip = perp_px * (thick_px * 0.5f) *
                         pixel_to_clip_xy * v.side * clip_this.w;

    /* Per-vertex SIGNED lambert. craft_to_world bakes the classic scale
     * into its rotation block — normalize recovers unit length. The
     * line's normal arrives outward (converter boundary negation) and
     * `directional_dir` is the host-normalised to-light direction.
     * Shipped signed; the FS clamps via `side_sign`. The FS unlit
     * branch ignores raw_dot for laser-bolt / line-poly edges. */
    float raw_dot = dot(normalize(world_n), directional_dir);

    VSOut o;
    o.position     = float4(clip_this.x + offset_clip.x,
                            clip_this.y + offset_clip.y,
                            clip_this.z,
                            clip_this.w);
    o.raw_dot      = raw_dot;
    o.v_color      = v.color;
    o.v_material   = v.material_id;
    o.v_highlight = flight_mesh_table_scalar(mesh_table_index,
                                             AERON_MESH_HIGHLIGHT_OFFSET, mi);
    o.face_u       = 0.0f;
    o.face_v       = 0.0f;
    o.decal_offset = 0.0f;
    o.decal_count  = 0.0f;
    o.v_markings_enabled = 0.0f;
    o.v_emissive = flight_mesh_table_scalar(mesh_table_index,
                                            AERON_MESH_EMISSIVE_OFFSET, mi);
    o.local_rgb    = float3(0.0f, 0.0f, 0.0f);
    return o;
}
