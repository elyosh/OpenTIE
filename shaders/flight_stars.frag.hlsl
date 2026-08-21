/* Analytic rounded coverage inside the original one-pixel bounding box. */

struct VSOut
{
    float4                   position : SV_Position;
    float2                   local    : TEXCOORD0;
    nointerpolation float3   color    : COLOR0;
};

float4 main(VSOut input) : SV_Target
{
    float radius = length(input.local);
    float feather = max(fwidth(radius), 1.0e-4f);
    float coverage = 1.0f - smoothstep(max(1.0f - feather, 0.0f), 1.0f, radius);
    return float4(input.color * coverage, coverage);
}
