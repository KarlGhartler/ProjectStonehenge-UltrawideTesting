#version 450
// FXAA 3.11 quality preset 29 (NVIDIA FXAA3_11.h, BSD-3-Clause, cmake/Dependencies.cmake) on the copy
// of the frame buffer. Luma = green (the alpha channel holds the GS alpha, not luma); alpha keeps the
// frame buffer's own value.
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform Params {
    vec4 metrics;  // 1 / width, 1 / height, width, height
} pc;

#define FXAA_PC 1
#define FXAA_GLSL_130 1
#define FXAA_GREEN_AS_LUMA 1
#define FXAA_QUALITY__PRESET 29
#include "FXAA3_11.h"

layout(binding = 0) uniform sampler2D color_tex;  // linear

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    const vec4 zero = vec4(0.0);
    const vec3 rgb = FxaaPixelShader(v_uv, zero, color_tex, color_tex, color_tex, pc.metrics.xy, zero, zero, zero,
                                     0.75,    // subpixel aliasing removal
                                     0.166,   // edge threshold
                                     0.0833,  // edge threshold minimum
                                     8.0, 0.125, 0.05, zero)  // console parameters (unused on PC)
                         .rgb;
    o_color = vec4(rgb, texelFetch(color_tex, ivec2(gl_FragCoord.xy), 0).a);
}
