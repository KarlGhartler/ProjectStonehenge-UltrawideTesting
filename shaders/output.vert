#version 450
// Output pass (ADR-006): one full-viewport triangle; the viewport is the 4:3 (or 16:9) rectangle.

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
