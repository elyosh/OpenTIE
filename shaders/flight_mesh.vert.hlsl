/*
 * Flight-overlay mesh vertex shader (docs/remaster-flightsim.md §6.2).
 *
 * Per-vertex format mirrors the ShipModelData converter emitting from the
 * binary's ShipModelData blob:
 *   Position    float3   craft-local coords (engine raw int16 cast to float)
 *   Normal      float3   craft-local unit normal
 *   Color       uint8    palette index — drives the classic-LUT
 *                        fragment shader; ignored by HD-textured
 *                        variants
 *   MaterialId  uint8    classic materialcolors[] base (45 ramps)
 *
 * Per-draw uniforms:
 *   view_proj       world→clip matrix (snapshot quaternion + camera.pos
 *                   translated + projection matrix composed host-side
 *                   once per frame).
 *   craft_to_world  craft world placement — translation + rotation
 *                   derived from TieFlightObjectState.pos +
 *                   TieFlightObjectState.ori.
 */

cbuffer MeshVSUniforms : register(b0, space1)
{
    row_major float4x4 view_proj;
    row_major float4x4 craft_to_world;
    /* Engine lightflag=0 (gates only) — skip the world-rotation of the
     * normal so the fragment shader's lambert mirrors classic's
     * `n_local · lightWorld` dot. See tie.c:1876 + drawpol.c:601. */
    float              light_local_frame;
    /* Engine `gouraudflag` toggle (snapshot-shipped per tick).
     * 0.0 → flat shading (face normal); 1.0 → per-vertex Gouraud
     * on faces whose flag_byte has bit 0x40 set. */
    float              gouraud_enabled;
    float2             _pad_gv;
    /* Directional light direction in world space. Q15-derived float,
     * NOT normalised — matches the engine's `light_world / 32768`
     * so `floor(lambert * 16)` bins to `face_dot >> 11`. */
    float3             directional_dir;
    float              _pad_dir;
    /* World→craft-local rotation pre-divided by the per-craft total
     * scale (TIE classic half scale × ms_factor) so the result lives in
     * the same int16-as-float craft-local frame as `v.position`.
     * Transforms world-space light positions into the local frame
     * the per-vertex local-light loop operates in. */
    row_major float3x3 world_to_craft;
    /* Craft origin in scene-local native units — subtract from each light's
     * world pos before applying `world_to_craft`. */
    float3             craft_world_pos;
    float              _pad_cwp;
    uint               mesh_table_index;
    uint3              _pad_mesh_table;
};

/* World-space active explosion lights for the per-vertex Gouraud
 * accumulator. Cbuffer + struct + loop live in the shared helper;
 * the cbuffer slot binding (b1 space1) is centralised there. */
#include "flight_local_lights.hlsli"

/* Per-craft per-mesh affine transforms + per-mesh visibility / highlight
 * / markings / emissive. Each triplet `mesh_rot[3*i .. 3*i+2]` is a
 * row-major 3x4 affine the host built from fview_componentrotation.
 * Static meshes have identity. */
#include "flight_mesh_table.hlsli"

struct VSIn
{
    float3 position             : POSITION;
    float3 normal               : NORMAL;     /* face normal — flat path */
    float3 vertex_normal        : NORMAL1;    /* rebuilt per-vertex normal */
    float  color                : COLOR0;     /* raw palette index, 0..255 */
    float  material_id          : COLOR1;     /* materialcolors row, 0..44 */
    float  mesh_index           : COLOR2;     /* which mesh_rot[] entry to use */
    float  face_flags           : COLOR3;     /* bit 0: face has flag_byte & 0x40 (Gouraud-eligible)
                                               * bit 1: face flag_byte == 194 (two-sided unlit edge —
                                               *        preserve negative dot, drawpol.c:559) */
    float  face_u               : TEXCOORD0;
    float  face_v               : TEXCOORD1;
    /* Window into the per-species FlightDecal[] SSBO for this face.
     * decal_count == 0 → no markings on this face. */
    float  decal_offset         : TEXCOORD2;
    float  decal_count          : TEXCOORD3;
};

struct VSOut
{
    float4 position     : SV_Position;
    /* Per-vertex SIGNED dot(outward_normal, to_light). Unclamped —
     * the FS clamps via `max(side_sign * raw_dot, 0)`, which gives
     * correct two-sided lighting (back-facing fragments whose
     * outward normal faces away from L are LIT on the inward side).
     * Linear scalar interpolation matches the engine's vertexlight[]
     * cross-scanline interpolation. */
    float  raw_dot      : TEXCOORD2;
    float  v_color      : COLOR0;
    float  v_material   : COLOR1;
    /* Per-mesh highlight value (see MeshTableUniforms.mesh_highlight).
     * Constant across a triangle — interpolation is a no-op. */
    float  v_highlight  : COLOR2;
    /* Per-mesh emissive multiplier. Constant across a triangle.
     *
     * NOTE: declaration order is load-bearing — SPIRV-Cross assigns
     * user(locn) by declaration index, so VS-output / FS-input slot
     * positions must match between flight_mesh.vert, flight_line.vert,
     * and flight_mesh_classic_lut.frag. */
    float  v_emissive   : COLOR3;
    /* Face-local (u, v) for the FS decal point-in-polygon test. */
    float  face_u       : TEXCOORD3;
    float  face_v       : TEXCOORD4;
    float  decal_offset : TEXCOORD5;
    float  decal_count  : TEXCOORD6;
    /* Per-mesh marking-emission gate (1.0 = draw decals, 0.0 = skip). */
    float  v_markings_enabled : TEXCOORD7;
    /* Per-vertex COLOURED local-light accumulation. Each explosion /
     * particle light contributes `saturate(range * gain / d2t) *
     * lights[li].color` where gain = dot(to-light, outward_normal) +
     * 1. Independent of the directional lambert and the sun colour:
     * the FS adds `albedo * local_rgb` to base_rgb so the
     * explosion's authored tint (orange / ion-blue) survives all the
     * way to the framebuffer. HDR: can exceed 1.0 when several
     * lights stack — bloom catches it. */
    float3 local_rgb    : COLOR4;
};

VSOut main(VSIn v)
{
    uint mi = (uint)clamp((int)round(v.mesh_index), 0,
                         AERON_MAX_MESH_SLOTS - 1);

    /* Visibility check: destroyed meshes get visibility = 0. Discard
     * the vertex by emitting a clip-space position behind the camera
     * (negative w), which fails all clip tests. */
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

    /* Per-mesh affine: three rows of the 3x4 affine live at
     * mesh_rot[3*mi + 0..2]; xyz dot the vertex, w is the translation. */
    float4 r0 = flight_mesh_table_row(mesh_table_index, mi, 0);
    float4 r1 = flight_mesh_table_row(mesh_table_index, mi, 1);
    float4 r2 = flight_mesh_table_row(mesh_table_index, mi, 2);
    float3 rotated_local;
    rotated_local.x = dot(r0.xyz, v.position) + r0.w;
    rotated_local.y = dot(r1.xyz, v.position) + r1.w;
    rotated_local.z = dot(r2.xyz, v.position) + r2.w;
    float3 rotated_normal;
    rotated_normal.x = dot(r0.xyz, v.normal);
    rotated_normal.y = dot(r1.xyz, v.normal);
    rotated_normal.z = dot(r2.xyz, v.normal);
    float3 rotated_vnormal;
    rotated_vnormal.x = dot(r0.xyz, v.vertex_normal);
    rotated_vnormal.y = dot(r1.xyz, v.vertex_normal);
    rotated_vnormal.z = dot(r2.xyz, v.vertex_normal);

    float4 world_p = mul(craft_to_world, float4(rotated_local, 1.0f));

    /* Per-face flag derivation. Bits packed by the converter:
     *   bit 0: face flag_byte & 0x40 set (Gouraud-eligible).
     *   bit 1: face flag_byte == 194 — used by line geometry only;
     *          unused in the triangle path now that raw_dot is
     *          shipped signed and the FS clamps per-side. */
    int  ff              = (int)round(v.face_flags);
    bool face_is_gouraud = (ff & 0x01) != 0;
    bool use_vnorm       = (gouraud_enabled > 0.5f) && face_is_gouraud;

    float3 shading_local = use_vnorm ? rotated_vnormal : rotated_normal;

    /* Real world-frame normal for the non-gate directional dot.
     * `craft_to_world` bakes the classic half scale × ms_factor into
     * its rotation block, so the result arrives scaled — normalize. */
    float3 world_n_real =
        normalize(mul((float3x3)craft_to_world, shading_local));

    /* Directional dot. Engine convention: `light_local_frame != 0`
     * (engine lightflag=0, gate material) skips the craft→world
     * rotation on the normal, so the dot is computed in the gate's
     * local frame against the world-frame light direction — the
     * engine's intended mismatch that ties gate lighting to each
     * gate's pose instead of staying world-fixed (tie.c:1876 +
     * drawpol.c:601). Non-gate paths use the world-frame normal so
     * the lambert is a standard cosine.
     *
     * SHIPPED SIGNED — the FS applies `max(side_sign * raw_dot, 0)`
     * so a back-facing fragment whose outward normal points away
     * from L (i.e. the inward / visible side faces L) lights up
     * correctly on the visible side. The engine's per-side `rotlight`
     * negation trick (drawpol.c:962-964) is reproduced FS-side. */
    float3 dot_n = (light_local_frame != 0.0f)
        ? normalize(shading_local)
        : world_n_real;
    float dir_dot = dot(dot_n, directional_dir);

    /* Local-light loop. Engine reference: drawpol.c:563-592. Per
     * light:
     *   d        = length(vertex - light)             in craft-local
     *                                                    int16 units
     *   range    = anim-frame table value             (16..480 typ.)
     *   range << 7 cutoff                             skip if d larger
     *   ndot     = dot((light - vertex) / d, n_out)    [-1, +1]
     *   gain     = ndot + 1.0                          [0, 2]
     *   d2t      = d² / falloff_sq + 1                attenuator
     *   contrib  = saturate(range * gain / d2t)        per-light cap
     *
     * Engine's gain is biased by 0x8000 = 1.0 in signed Q15 (the
     * `n_dot + 0x8000` shifts [-1,1] → [0, 2], NOT [-0.5, 0.5]).
     * Engine's vertexlight saturates at 0x7FFF (= 1.0 HD) AFTER each
     * light; we saturate per-light here and accumulate COLOURED so
     * the snapshot's authored tint survives. Multiple lights can push
     * local_rgb above 1 — that's deliberate HDR feeding the bloom
     * pass.
     *
     * Falloff denominator (was a hardcoded 4096 = 64² in the engine):
     * now comes from `lights[li].falloff_sq` which is the host's
     * `falloff_radius_engine²`. Larger = wider/softer glow at the
     * same peak brightness; smaller = tighter spot. Decoupled from
     * `range`, which still controls peak brightness + cull cutoff. */
    /* Pick the normal that the Gouraud accumulator should consume —
     * matches the engine's classic per-vertex behaviour. Same
     * `use_vnorm` decision the directional Lambert path made above. */
    float3 light_normal = use_vnorm ? rotated_vnormal : rotated_normal;
    float3 local_rgb    = accumulate_local_lights(rotated_local,
                                                   light_normal,
                                                   world_to_craft,
                                                   craft_world_pos);

    VSOut o;
    o.position     = mul(view_proj, world_p);
    o.raw_dot      = dir_dot;            /* signed, FS clamps per-side */
    o.v_color      = v.color;
    o.v_material   = v.material_id;
    o.v_highlight  = flight_mesh_table_scalar(mesh_table_index,
                                              AERON_MESH_HIGHLIGHT_OFFSET, mi);
    o.face_u       = v.face_u;
    o.face_v       = v.face_v;
    o.decal_offset = v.decal_offset;
    o.decal_count  = v.decal_count;
    o.v_markings_enabled = flight_mesh_table_scalar(
        mesh_table_index, AERON_MESH_MARKINGS_OFFSET, mi);
    o.v_emissive = flight_mesh_table_scalar(
        mesh_table_index, AERON_MESH_EMISSIVE_OFFSET, mi);
    o.local_rgb    = local_rgb;
    return o;
}
