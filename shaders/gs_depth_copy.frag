#version 450
// Z buffer drawn into another Z buffer (src/platform/render/gs/gs_vulkan.cpp, depth copy): the GS
// reads a Z buffer as a texture and draws it into a frame buffer that a later pass uses as its Z
// buffer (the no-save intro and mission 12 halve the scene's depth for the half-resolution cloud
// pass). The port keeps depth in the targets' depth images, so the sprite samples the source
// target's depth image at its native texel (nearest) and writes it as this target's depth; colour is
// not written.

layout(location = 0) in vec4 v_color;  // gs.vert outputs (only the texture coordinates are used)
layout(location = 1) noperspective in vec3 v_tex;
layout(location = 2) in float v_fog;

layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 0) uniform sampler2D src_depth;

layout(push_constant) uniform Params {
    vec2 target_size;  // gs.vert
    int src_scale;     // host texels per GS texel of the source depth image
    float ratio;       // 2^(source Z bits - destination Z bits): the same Z value in the other format
    ivec2 src_size;    // source depth image size in host texels
} pc;

void main() {
    ivec2 t = ivec2(floor(v_tex.xy)) * pc.src_scale + pc.src_scale / 2;
    t = clamp(t, ivec2(0), pc.src_size - 1);
    gl_FragDepth = clamp(texelFetch(src_depth, t, 0).r * pc.ratio, 0.0, 1.0);
    o_color = vec4(0.0);
}
