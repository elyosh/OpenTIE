/*
 * HD hyperspace-streak vertex shader.
 *
 * Renders the engine's draw_drawhyperstar (draw.c:540) output as a
 * thickness-expanded line quad. Geometry comes pre-mirrored from the
 * CPU (flight_hyperstars.c emits one line per mirror per slot).
 *
 * Per-vertex inputs:
 *   pos_a    — line endpoint A in world space (identical across the 4
 *              corners of one quad).
 *   pos_b    — line endpoint B (also identical across corners).
 *   endpoint — 0.0 / 1.0 picks which of pos_a or pos_b this vertex
 *              sits at.
 *   side     — -1.0 / +1.0 selects which perpendicular side of the
 *              line.
 *   color    — pre-resolved RGBA from palette[252 + (slot & 3)]; the
 *              fragment shader outputs it verbatim.
 *
 * Same screen-space perpendicular-expansion math as flight_line.vert:
 * project A and B to clip, derive the screen-space direction from the
 * NDC delta, perpendicular = (-dy, dx) / len, then offset clip-space
 * xy by (perp × thick_px × side × 0.5 × pixel_to_clip × clip_w) so
 * the offset survives the perspective divide.
 *
 * Streak thickness mirrors the engine's classic formula
 *     thick_px = (thickness_mul × thickness_base) / (avg_w / 256)
 * with thickness_base = 0x40 = 64 (the hyperstardata polymesh's edge
 * thickness lo byte). The `+ 1.0` engine-quirk thickness bump is
 * intentional here too — it matches drawpol_drawlineface's unconditional
 * `thickness++`, so the HD line pixel-count tracks classic exactly.
 */

cbuffer HyperstarsVSUniforms : register(b0, space1)
{
    row_major float4x4 view_proj;
    float2             pixel_to_clip_xy;
    float              thickness_mul;
    float              _pad;
};

struct VSIn
{
    float3 pos_a    : POSITION;
    float3 pos_b    : POSITION1;
    float  endpoint : COLOR0;
    float  side     : COLOR1;
    float4 color    : COLOR2;
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

VSOut main(VSIn v)
{
    bool   at_b      = v.endpoint > 0.5f;
    float3 this_pos  = at_b ? v.pos_b : v.pos_a;

    float4 clip_this = mul(view_proj, float4(this_pos, 1.0f));
    float4 clip_a    = mul(view_proj, float4(v.pos_a, 1.0f));
    float4 clip_b    = mul(view_proj, float4(v.pos_b, 1.0f));

    /* Project A and B to NDC then convert to pixel-space direction.
     * Same direction for every corner of this quad. */
    float2 ndc_a   = clip_a.xy / max(clip_a.w, 1e-6f);
    float2 ndc_b   = clip_b.xy / max(clip_b.w, 1e-6f);
    float2 dir_px  = (ndc_b - ndc_a) / pixel_to_clip_xy;
    float  len_px  = length(dir_px);
    float2 perp_px = (len_px > 1e-3f)
                   ? float2(-dir_px.y, dir_px.x) / len_px
                   : float2(0.0f, 0.0f);

    /* Engine's hyperstardata edge thickness lo byte is 0x40 = 64. */
    float thickness_base = 64.0f;
    float avg_w    = max(0.5f * (clip_a.w + clip_b.w), 1e-3f);
    float thick_px = (thickness_mul * thickness_base) / max(avg_w / 256.0f, 1.0f)
                   + 1.0f;

    float2 offset_clip = perp_px * (thick_px * 0.5f) *
                         pixel_to_clip_xy * v.side * clip_this.w;

    VSOut o;
    o.position = float4(clip_this.x + offset_clip.x,
                        clip_this.y + offset_clip.y,
                        clip_this.z,
                        clip_this.w);
    o.color    = v.color;
    return o;
}
