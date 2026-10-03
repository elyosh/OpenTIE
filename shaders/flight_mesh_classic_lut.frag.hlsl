/*
 * Flight-overlay mesh fragment — engine-faithful 16-step LUT path.
 *
 * Reproduces `drawpol_getlightvalue` (drawpol.c:498-617) end-to-end:
 * decal walk, marking-state animation, target-highlight remap, the
 * unlit branch, and the materialcolors → palette LUT chain. All ramp
 * and palette accesses are discrete; Gouraud shading blends resolved
 * ramp colours in place of the original span dither.
 *
 * Texture / sampler slots:
 *
 *   t0 space2 = g_materialcolors (R8_UNORM, 16 × 45 — palette INDICES)
 *   t1 space2 = g_palette        (BGRA8_SRGB, 256 × 1 — live VGA palette)
 *   t2 space2 = g_decals         (StructuredBuffer<FlightDecal>)
 *   t3 space2 = g_decal_verts    (StructuredBuffer<FlightDecalVert>)
 *   s0 space2 = g_sampler        (nearest; every access is a Load).
 *
 * Cbuffer:
 *   b0 space3 = MeshPSUniforms (marking_state_offset, line thickness
 *                                tuning).
 */

cbuffer MeshPSUniforms : register(b0, space3)
{
    float marking_state_offset;
    float line_thickness_mul;
    float line_floor_mul;
    float _pad_mesh_ps;
};

/* materialcolors[720] as a 16×45 R8_UNORM texture — palette INDICES,
 * one per cell. SDL_GPU forbids USAGE_SAMPLER on integer formats; host
 * uploads as UNORM and we decode back to the byte via *255 + 0.5
 * rounding. */
Texture2D<float>  g_materialcolors : register(t0, space2);
Texture2D<float4> g_palette         : register(t1, space2);
SamplerState      g_sampler         : register(s0, space2);

struct FlightDecal
{
    uint  vert_offset;
    uint  vert_count;
    uint  color;
    uint  material;
    float bbox_min_u;
    float bbox_min_v;
    float bbox_max_u;
    float bbox_max_v;
};
struct FlightDecalVert
{
    float u;
    float v;
};
StructuredBuffer<FlightDecal>     g_decals      : register(t2, space2);
StructuredBuffer<FlightDecalVert> g_decal_verts : register(t3, space2);

/* Crossing-number point-in-polygon test in (u, v). Standard algorithm:
 * cast a horizontal ray from `p` to +infinity, count edge crossings;
 * odd = inside. Handles concave decal polygons. */
bool point_in_decal(float2 p, uint v_off, uint v_cnt)
{
    bool inside = false;
    for (uint e = 0; e < v_cnt; ++e) {
        const uint i0 = v_off + e;
        const uint i1 = v_off + ((e + 1u == v_cnt) ? 0u : (e + 1u));
        FlightDecalVert a = g_decal_verts[i0];
        FlightDecalVert b = g_decal_verts[i1];
        if ((a.v > p.y) != (b.v > p.y)) {
            float x_at = a.u + (b.u - a.u) * (p.y - a.v) / (b.v - a.v);
            if (p.x < x_at)
                inside = !inside;
        }
    }
    return inside;
}

/* Engine targetmapping[39] (drawpol.c:212). */
static const uint TARGETMAPPING[39] = {
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x01, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

/* Engine highlightmapping[9] (drawpol.c:224). */
static const uint HIGHLIGHTMAPPING[9] = {
    0x18, 0x19, 0x1a,
    0x28, 0x29, 0x2a,
    0x2b, 0x2c, 0x2d,
};

/* Marking-state offset application + target-highlight remap. The engine
 * path: drawpol_getlightvalue's `markcoloroffset[14]` is the per-craft
 * -1/0/+1 shift on material 14 only; the highlight remap fires when the
 * per-mesh highlight is non-zero and replaces the material index with
 * `highlightmapping[3*hl + targetmapping[m-1]]`. Returns the final
 * clamped material row index 0..44. */
int resolve_material_index(int v_mat_raw, float v_highlight)
{
    int mapped_lo7 = v_mat_raw & 0x7F;
    if (mapped_lo7 == 14) {
        mapped_lo7 += (int)marking_state_offset;
    }
    int hl_raw = (int)round(v_highlight);
    if (hl_raw != 0) {
        int hl_color = (hl_raw == 3) ? 0 : hl_raw;
        int mapped6  = mapped_lo7 & 0x3F;
        int idx      = (mapped6 >= 1 && mapped6 <= 39)
                        ? (int)TARGETMAPPING[mapped6 - 1] : 0;
        mapped_lo7 = (int)HIGHLIGHTMAPPING[3 * hl_color + idx];
    }
    return clamp(mapped_lo7 - 1, 0, 44);
}

uint ramp_index(int row, int entry)
{
    return (uint)round(g_materialcolors.Load(int3(clamp(entry, 0, 15), row, 0)) * 255.0f);
}
float3 palette_color(uint index)
{
    return g_palette.Load(int3(index & 0xFFu, 0, 0)).rgb;
}
float3 smooth_ramp(int row, float shade)
{
    int entry = (int)floor(shade);
    float t = frac(shade), t2 = t * t, t3 = t2 * t, inverse = 1 - t;
    /* Cubic B-spline weights smooth repeated shades without colour overshoot.
     * Palette loads resolve to linear RGB; taps stay within this material row. */
    float4 weights = float4(inverse * inverse * inverse, 4 - 6 * t2 + 3 * t3,
                            1 + 3 * t + 3 * t2 - 3 * t3, t3) / 6.0f;
    return palette_color(ramp_index(row, entry - 1)) * weights.x
         + palette_color(ramp_index(row, entry)) * weights.y
         + palette_color(ramp_index(row, entry + 1)) * weights.z
         + palette_color(ramp_index(row, entry + 2)) * weights.w;
}

/* Resolved per-fragment shading inputs after decal walk + highlight
 * remap. */
struct ShadingPrep
{
    int  color_byte;     /* final palette byte for the unlit branch    */
    int  material;       /* 0..44 materialcolors row index             */
    bool is_marking;     /* decal hit — flips brightness offset path   */
    bool is_unlit;       /* color & 0x80 set AND not is_marking        */
};

/* Decal walk + final-color-byte / final-material resolution. Last-hit-
 * wins matches the engine's xtrans2_processedge swap chain (xtrans2.c
 * :756): authors emit background polygons first and foreground details
 * last, so a later marking's start edge always fires inside an earlier
 * one. */
ShadingPrep prepare_shading(
    float4 pos,
    float  v_color,
    float  v_material,
    float  v_highlight,
    float  face_u,
    float  face_v,
    float  decal_offset,
    float  decal_count,
    float  v_markings_enabled)
{
    int  color_byte = (int)round(v_color) & 0xFF;
    int  v_mat_raw  = (int)round(v_material);
    bool is_marking = false;

    /* Per-mesh drawmarkingsflag gate (DRAW_drawcraft override, draw.c
     * :893-895). Below-0.5 short-circuits the decal loop. */
    int n   = (v_markings_enabled < 0.5f) ? 0 : (int)round(decal_count);
    int off = (int)round(decal_offset);
    if (n > 0) {
        float2 frag_uv = float2(face_u, face_v);
        float2 grad_x  = float2(ddx(face_u), ddx(face_v));
        float2 grad_y  = float2(ddy(face_u), ddy(face_v));
        /* SV_Position.w after perspective divide is `1 / clip.w`. */
        float eye_z_world = 1.0f / max(pos.w, 1e-9f);
        [loop] for (int d = 0; d < n; ++d) {
            FlightDecal dr = g_decals[off + d];
            bool hit = false;
            if (dr.vert_count == 2u) {
                /* Line marking. Engine pixel width drawpol.c:776-779. */
                FlightDecalVert a = g_decal_verts[dr.vert_offset + 0u];
                FlightDecalVert b = g_decal_verts[dr.vert_offset + 1u];
                float2 av  = float2(a.u, a.v);
                float2 bv  = float2(b.u, b.v);
                float2 ab  = bv - av;
                float  ll  = max(dot(ab, ab), 1e-12f);
                float  t   = saturate(dot(frag_uv - av, ab) / ll);
                float2 closest = av + t * ab;
                float  dist_uv = length(frag_uv - closest);

                float base = (float)((dr.material >> 8u) & 0xFFu);
                float divisor = max(eye_z_world / 256.0f, 1.0f);
                float pixel_thick =
                    base * line_thickness_mul / divisor
                    + line_thickness_mul * line_floor_mul;
                float2 line_dir_uv =
                    ab * (1.0f / sqrt(ll));
                float2 perp_uv = float2(-line_dir_uv.y, line_dir_uv.x);
                float perp_per_x = dot(grad_x, perp_uv);
                float perp_per_y = dot(grad_y, perp_uv);
                float perp_per_pixel =
                    sqrt(perp_per_x * perp_per_x +
                         perp_per_y * perp_per_y);
                float dist_threshold =
                    0.5f * pixel_thick * perp_per_pixel;
                hit = (dist_uv <= dist_threshold);
            } else {
                bool bbox_in = (frag_uv.x >= dr.bbox_min_u) &
                               (frag_uv.x <= dr.bbox_max_u) &
                               (frag_uv.y >= dr.bbox_min_v) &
                               (frag_uv.y <= dr.bbox_max_v);
                if (bbox_in)
                    hit = point_in_decal(frag_uv, dr.vert_offset,
                                         dr.vert_count);
            }
            if (hit) {
                color_byte = (int)(dr.color & 0xFFu);
                /* Tag material with bit 0x80 so the brightness-offset
                 * branches downstream gate via is_marking. */
                v_mat_raw  = (int)((dr.material & 0x7Fu) | 0x80u);
                is_marking = true;
            }
        }
    }

    ShadingPrep sp;
    sp.color_byte = color_byte;
    sp.is_marking = is_marking;
    /* Unlit gate: bit 0x80 on the color byte AND not a marking hit.
     * the converter strips 0x80 from 3D face colors; it survives only on
     * laser-bolt / line-poly vertices and on marking vertices whose
     * decal color carries the engine's `(c >> 6) & 3` brightness
     * offset bits. */
    sp.is_unlit  = ((color_byte & 0x80) != 0) && !is_marking;
    sp.material  = resolve_material_index(v_mat_raw, v_highlight);
    return sp;
}

float4 main(float4 pos          : SV_Position,
            noperspective float raw_dot : TEXCOORD2,
            float  v_color      : COLOR0,
            float  v_material   : COLOR1,
            float  v_highlight  : COLOR2,
            float  v_emissive   : COLOR3,
            float  face_u       : TEXCOORD3,
            float  face_v       : TEXCOORD4,
            float  decal_offset : TEXCOORD5,
            float  decal_count  : TEXCOORD6,
            float  v_markings_enabled : TEXCOORD7,
            /* Coloured local-light accumulation from the VS. The LUT
             * path quantises onto the materialcolors ramp, so local
             * lights can't drive shade selection; we add `local_rgb`
             * additively on top of the palette colour so explosion
             * tints still appear over classic shading. */
            float3 local_rgb    : COLOR4,
            nointerpolation float v_gouraud : TEXCOORD8,
            bool   is_front     : SV_IsFrontFace) : SV_Target0
{
    ShadingPrep sp = prepare_shading(pos, v_color, v_material, v_highlight,
                                     face_u, face_v,
                                     decal_offset, decal_count,
                                     v_markings_enabled);

    float4 rgb = float4(0.0f, 0.0f, 0.0f, 1.0f);

    /* Unlit branch — `drawpol_drawlineface` (xtrans2.c:1096) consumes
     * edge[4] colour directly as a palette index. */
    if (sp.is_unlit) {
        rgb.rgb = palette_color((uint)sp.color_byte);
    } else {
        /* Back-face lambert flip: engine's two-sided face path
         * (drawpol.c:962-964) negates rotlight when rendering the
         * back side, which inverts face_dot's sign. With pipeline
         * cull_mode = NONE we draw both sides, so reproduce the
         * negation per-fragment from SV_IsFrontFace. */
        float side_sign = is_front ? 1.0f : -1.0f;
        float lambert = saturate(side_sign * raw_dot);

        /* Gouraud faces store markings unshaded (drawpol.c:1325); the
         * span renderer draws colours >= 0x40 directly and shades the
         * rest along the material ramp. Target highlights take the flat
         * marking path. */
        bool gouraud = v_gouraud > 0.5f;
        bool highlighted = (int)round(v_highlight) != 0;
        int row = sp.material;
        bool interpolated = gouraud && !sp.is_marking;
        bool direct = false;
        uint color = (uint)sp.color_byte;
        if (gouraud && sp.is_marking && !highlighted) {
            if ((color & 0x3Fu) == 14u)
                color = (color + (uint)(int)marking_state_offset) & 0xFFu;
            direct = color >= 0x40u;
            interpolated = !direct;
            row = max((int)color - 1, 0);
        }

        if (direct) {
            rgb.rgb = palette_color(color);
        } else if (interpolated) {
            /* Keep the original 63-light ramp coordinate fractional
             * (xtrans2_outputxt). Palette indices themselves are discrete. */
            float shade = clamp((63.0f - lambert * 64.0f) * 0.25f, 0.0f, 15.0f);
            rgb.rgb = smooth_ramp(row, shade);
        } else {
            /* Flat shade = 15 - (face_dot >> 11). Row 0 = brightest. */
            int entry = 15 - min(15, (int)floor(lambert * 16.0f));
            uint index = ramp_index(row, entry);
            /* Marking colour bits 6-7 subtract a 0..3 brightness offset;
             * face vertices have those bits masked off by the converter. */
            if (sp.is_marking)
                index -= (uint)((sp.color_byte >> 6) & 3);
            rgb.rgb = palette_color(index);
        }
    }

    /* Per-mesh HDR emissive multiplier from MeshTableUniforms
     * .mesh_emissive (host-driven by genus + mesh_type). */
    rgb.rgb *= v_emissive;
    /* Coloured local-light add. After the palette resolve so an
     * explosion's tint reads as a glow on top of the engine shade.
     * HDR pre-tonemap; values > 1.0 feed the bloom chain. */
    rgb.rgb += local_rgb;

    return rgb;
}
