/*
 * Shared per-vertex local-light accumulator for the flight vertex
 * shaders.
 *
 * The classic mesh VS consumes the LightBufferGPU at VS cbuffer slot
 * b1 / space1. Keeping its cbuffers contiguous lets SDL's Metal backend
 * bind the mesh-table storage buffer immediately after them.
 *
 * The accumulator runs in CRAFT-LOCAL RAW ENGINE UNITS so the
 * engine's `d > range << 7` (== `d > range * 128`) cutoff and the
 * `rg * gain / d2t` contribution land on the unit the engine
 * designed the formula for. Callers must provide:
 *   - rotated_local   : vertex position after per-mesh affine, in
 *                       craft-local raw engine units.
 *   - rotated_normal  : vertex outward normal in the same frame
 *                       (the same value the directional Lambert dot
 *                       consumes — flat path uses face normal, smooth
 *                       path uses the modified-or-engine vertex
 *                       normal per the toggle).
 *   - world_to_craft  : 3×3 mapping (world_delta → craft-local raw),
 *                       pre-divided by the per-craft total scale so
 *                       the result is already in raw engine units.
 *   - craft_world_pos : craft origin in scene-local native units —
 *                       subtracted from each light's world position
 *                       before applying world_to_craft.
 *
 * The classic VS reads world_to_craft + craft_world_pos directly
 * from its PerCraftUniforms cbuffer. The OPT VS doesn't carry them
 * explicitly; it derives them from craft_to_world's 3×3 block (which
 * is rotation × axis_swap × uniform_scale, so block × blockᵀ =
 * scale²·I → block⁻¹ = blockᵀ / scale²) and craft_to_world's
 * translation column.
 */

#ifndef TIE_FLIGHT_LOCAL_LIGHTS_HLSLI
#define TIE_FLIGHT_LOCAL_LIGHTS_HLSLI

struct FlightLight {
    float3 pos;        /* scene-local native units */
    float  range;      /* engine raw units (16..480 from
                        * tie_makelocallights' anim-frame table) */
    float3 color;      /* linear RGB, can exceed 1.0 for HDR */
    float  falloff_sq; /* engine raw units squared (host pre-squares
                        * the user-facing falloff_radius_engine) */
};

cbuffer LightBuffer : register(b1, space1)
{
    uint        light_count;
    uint3       _pad_lc;
    FlightLight lights[16];
};

/* Per-vertex coloured local-light accumulator. Returns
 * sum_i { contrib_i × lights[i].color } where contrib_i is the
 * engine's saturate(range * gain / d2t) with gain =
 * dot(to-light, normal) + 1 and d2t = d²/falloff_sq + 1. Result is
 * additive on top of base diffuse; callers typically fold it as
 * `albedo * local_rgb`. */
float3 accumulate_local_lights(float3 rotated_local,
                               float3 rotated_normal,
                               float3x3 world_to_craft,
                               float3 craft_world_pos)
{
    float3 local_rgb = float3(0.0f, 0.0f, 0.0f);
    for (uint li = 0; li < light_count; ++li) {
        float3 lp_local = mul(world_to_craft,
                              lights[li].pos - craft_world_pos);
        float3 d3 = lp_local - rotated_local;
        float  d  = length(d3);
        float  rg = lights[li].range;
        if (d > rg * 128.0f) continue;       /* engine `d > range << 7` */
        float contrib;
        if (d < 1e-3f) {
            contrib = 1.0f;                  /* engine `if (!d) saturate` */
        } else {
            float ndot = dot(d3 / d, rotated_normal);
            float gain = ndot + 1.0f;
            if (gain <= 0.0f) continue;
            float falloff_sq = max(lights[li].falloff_sq, 1.0f);
            float d2t  = (d * d) / falloff_sq + 1.0f;
            contrib    = saturate(rg * gain / d2t);
        }
        local_rgb += contrib * lights[li].color;
    }
    return local_rgb;
}

#endif
