#version 450
// Output pass filters (ADR-006 §3), sampled with a bilinear sampler:
//   filter 0 = sharp: integer nearest pre-scale, then bilinear to the final size. Each source
//              pixel is drawn as a flat block of `prescale` output pixels per axis, and only the
//              remaining fractional scale is blended at the block edges.
//   filter 1 = smooth: plain bilinear.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 0) uniform sampler2D u_source;

layout(push_constant) uniform Params {
    vec2 source_size;   // native image size in pixels
    vec2 output_size;   // viewport size in pixels
    int filter_mode;
} params;

void main() {
    if (params.filter_mode == 0) {
        vec2 prescale = max(floor(params.output_size / params.source_size), vec2(1.0));
        vec2 texel = v_uv * params.source_size;
        vec2 texel_floor = floor(texel);
        vec2 frac_part = texel - texel_floor;
        // Within a source pixel, keep the centre region flat and squeeze the blend into the
        // outer 1/(2*prescale) on each side.
        vec2 region = 0.5 - 0.5 / prescale;
        vec2 centre_dist = frac_part - 0.5;
        vec2 f = (centre_dist - clamp(centre_dist, -region, region)) * prescale + 0.5;
        o_color = texture(u_source, (texel_floor + f) / params.source_size);
    } else {
        o_color = texture(u_source, v_uv);
    }
}
