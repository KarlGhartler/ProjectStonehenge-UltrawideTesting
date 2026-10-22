#version 450
// Full-screen triangle for the anti-aliasing passes (src/platform/render/gs/gs_antialias.cpp): the
// viewport covers the whole working image and the scissor limits the pass to the view's rectangle.
// v_uv is the texture coordinate over the working image (0..1 inside it).

layout(location = 0) out vec2 v_uv;

void main() {
    const vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
