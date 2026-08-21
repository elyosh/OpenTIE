/*
 * HD hyperspace-streak fragment shader.
 *
 * Outputs the VS-interpolated color verbatim. No texture sampling.
 * The CPU side resolves palette[252 + (slot & 3)] per instance and
 * writes the RGBA value into each of the line's 4 vertices, so the
 * fragment shader just forwards it.
 *
 * Pipeline blend state is additive (ONE + ONE), so the output value
 * lands as a bright contribution over whatever the mesh / skybox
 * passes already drew.
 */

struct VSOut
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

float4 main(VSOut input) : SV_Target
{
    return input.color;
}
