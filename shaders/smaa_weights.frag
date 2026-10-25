#version 450
// SMAA 1x pass 2 (SMAA.hlsl, MIT): blending weights from the edges and the precomputed area and
// search textures (Textures/AreaTex.h, SearchTex.h, uploaded by gs_antialias.cpp).
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform Params {
    vec4 metrics;
} pc;

#define SMAA_RT_METRICS pc.metrics
#define SMAA_GLSL_4 1
#define SMAA_PRESET_HIGH 1
#include "SMAA.hlsl"

layout(binding = 0) uniform sampler2D edges_tex;   // linear
layout(binding = 1) uniform sampler2D area_tex;    // linear
layout(binding = 2) uniform sampler2D search_tex;  // nearest

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_weights;

void main() {
    vec4 offset[3];
    vec2 pixcoord;
    SMAABlendingWeightCalculationVS(v_uv, pixcoord, offset);
    o_weights = SMAABlendingWeightCalculationPS(v_uv, pixcoord, offset, edges_tex, area_tex, search_tex, vec4(0.0));
}
