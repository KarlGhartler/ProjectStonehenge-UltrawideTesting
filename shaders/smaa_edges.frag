#version 450
// SMAA 1x pass 1 (SMAA.hlsl, MIT, cmake/Dependencies.cmake): luma edge detection on the copy of the
// frame buffer. The vertex-shader offsets of SMAA are computed here per pixel.
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform Params {
    vec4 metrics;  // 1 / width, 1 / height, width, height of the working images
} pc;

#define SMAA_RT_METRICS pc.metrics
#define SMAA_GLSL_4 1
#define SMAA_PRESET_HIGH 1
#include "SMAA.hlsl"

layout(binding = 0) uniform sampler2D color_tex;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec2 o_edges;

void main() {
    vec4 offset[3];
    SMAAEdgeDetectionVS(v_uv, offset);
    o_edges = SMAALumaEdgeDetectionPS(v_uv, offset, color_tex);
}
