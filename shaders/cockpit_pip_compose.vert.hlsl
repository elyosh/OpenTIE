/*
 * Cockpit 3D-CRT compositor vertex shader.
 *
 * Emits a textured quad covering the instruments[2] rect on the
 * cockpit RT. SV_VertexID 0..3 walks a TRIANGLESTRIP (BL, BR, TL, TR
 * — bit 0 = X corner, bit 1 = Y corner). The dst quad arrives in NDC
 * (x_min, y_min, width, height) already translated from cockpit-coord
 * top-left by the host (cockpit_gpu.c::blit_cockpit_quad math), so the
 * shader does no extra NDC conversion.
 *
 * The UV rect is whatever sub-rect of the PIP RT the host wants to
 * sample (typically full 0..1). v=0 lives at the top of the texture,
 * v=1 at the bottom; the BL corner samples v=v1 (bottom of texture)
 * because BL is at the bottom of NDC.
 *
 * Used by the HD 3D-CRT composite step described in
 * docs/remaster-flightsim.md §6.6.7 — paired with
 * cockpit_pip_compose.frag.hlsl which samples (color RT, alpha mask)
 * and outputs PMA-encoded (color × mask, mask).
 */

cbuffer PipComposeVS : register(b0, space1)
{
    /* dst_rect.xy = NDC bottom-left, dst_rect.zw = NDC width/height. */
    float4 dst_rect;
    /* src_rect = (u0, v0, u1, v1); (u0, v0) is the texture top-left. */
    float4 src_rect;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    float cx = (float)(vid & 1u);
    float cy = (float)((vid >> 1u) & 1u);

    float2 pos = dst_rect.xy + float2(cx, cy) * dst_rect.zw;

    /* BL (cy=0) samples v1 (bottom of texture), TL (cy=1) samples v0
     * (top of texture) — flips the texture-Y-down convention onto
     * NDC's +Y-up convention. */
    float2 uv = float2(
        src_rect.x + cx * (src_rect.z - src_rect.x),
        src_rect.w + cy * (src_rect.y - src_rect.w));

    VSOut o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.uv       = uv;
    return o;
}
