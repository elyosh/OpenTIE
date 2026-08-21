/*
 * Cockpit 3D-CRT compositor fragment shader.
 *
 * Samples the PIP color RT (t0) and the per-spec CRT alpha mask (t1)
 * and outputs PMA-encoded (rgb × mask, mask). With the standard PMA
 * over operator in the cockpit's render pass blend state, this leaves
 * dst untouched outside the OPEN mask region (so the cockpit base's
 * dark CRT bezel shows through) and replaces dst inside it (where the
 * silhouette of the targeted craft lands).
 *
 * Two samplers so the host can pick filtering per texture: typically
 * LINEAR for the silhouette color (the PIP RT renders at modest
 * resolution and gets stretched onto the cockpit's CRT rect) and
 * NEAREST for the mask (scanline-hard cutout edges matching the
 * engine's xtrans2_mask_read_delta rasterization).
 *
 * The mask texture is RGBA8 KTX2 with alpha = visibility (RGB unused,
 * zeroed) — see tools/cockpit/cockpit_extract.c::TieCockpitExtract_EmitCrtMasks. We
 * read `.a` so the same logic works if the mask is ever migrated to a
 * single-channel format.
 *
 * Used by the HD 3D-CRT composite step in cockpit_gpu — paired with
 * cockpit_pip_compose.vert.hlsl. See docs/remaster-flightsim.md §6.6.7.
 */

Texture2D<float4> g_color : register(t0, space2);
Texture2D<float4> g_mask  : register(t1, space2);
SamplerState      s_color : register(s0, space2);
SamplerState      s_mask  : register(s1, space2);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float4 c = g_color.Sample(s_color, input.uv);
    float  m = g_mask.Sample(s_mask, input.uv).a;
    /* PMA-encode: rgb premultiplied by alpha=m so the standard PMA
     * blend (src + (1-src.a)*dst) leaves dst untouched outside the
     * mask. Color RT's own alpha is dropped — the silhouette pre-pass
     * clears to opaque-black and writes alpha=1 everywhere, so it
     * carries no useful coverage information; mask is the only
     * coverage source. */
    return float4(c.rgb * m, m);
}
