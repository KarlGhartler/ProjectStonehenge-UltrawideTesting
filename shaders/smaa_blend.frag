#version 450
// SMAA 1x pass 3 (SMAA.hlsl, MIT): neighborhood blending. RGB only: alpha keeps the frame buffer's
// own value (the GS alpha that later draws can test or blend with).
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform Params {
    vec4 metrics;
} pc;

#define SMAA_RT_METRICS pc.metrics
#define SMAA_GLSL_4 1
#define SMAA_PRESET_HIGH 1
#include "SMAA.hlsl"

layout(binding = 0) uniform sampler2D color_tex;  // linear
layout(binding = 1) uniform sampler2D blend_tex;  // linear

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec4 offset;
    SMAANeighborhoodBlendingVS(v_uv, offset);
    const vec3 rgb = SMAANeighborhoodBlendingPS(v_uv, offset, color_tex, blend_tex).rgb;
    o_color = vec4(rgb, texelFetch(color_tex, ivec2(gl_FragCoord.xy), 0).a);
}
