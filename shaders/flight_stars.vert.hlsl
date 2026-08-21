/* TIE's original star axes, projected at infinity and expanded to one
 * aspect-corrected classic framebuffer pixel. */

cbuffer FlightStarsVS : register(b0, space1)
{
    row_major float4x4 view_proj;
    float2             pixel_to_clip;
    float2             half_size_px;
    uint               starfield_style;
    float3             _pad_style;
    float4             star_colors[4];
};

/* xyz = TIE-world direction axis, w = palette slot 0..3. */
StructuredBuffer<float4> stars : register(t0, space0);

struct VSOut
{
    float4                   position : SV_Position;
    float2                   local    : TEXCOORD0;
    nointerpolation float3   color    : COLOR0;
};

VSOut main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    static const uint CORNERS[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    uint corner_id = CORNERS[vertex_id % 6u];
    float2 corner = float2(
        (corner_id == 1u || corner_id == 2u) ? 1.0f : -1.0f,
        (corner_id == 2u || corner_id == 3u) ? 1.0f : -1.0f);

    float4 star = stars[instance_id];
    float4 clip = mul(view_proj, float4(star.xyz, 0.0f));

    VSOut output;
    output.local = corner;
    output.color = starfield_style == 1u
        ? star.www
        : star_colors[min((uint)(star.w + 0.5f), 3u)].rgb;
    if (abs(clip.w) <= 1.0e-5f)
    {
        output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
        return output;
    }

    /* The classic renderer negates all three eye coordinates when depth
     * is negative, making each source vector an antipodal axis. */
    if (clip.w < 0.0f)
        clip = -clip;

    float2 local_px = corner * half_size_px;
    float2 clip_offset = local_px * float2(pixel_to_clip.x, -pixel_to_clip.y) * clip.w;
    output.position = float4(clip.xy + clip_offset, 0.0f, clip.w);
    return output;
}
