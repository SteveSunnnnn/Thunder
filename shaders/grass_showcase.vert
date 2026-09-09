#version 460

// Fullscreen triangle. uv maps the swapchain to [0,1]^2.
layout(location = 0) out vec2 out_uv;

void main() {
    vec2 pos = vec2((gl_VertexIndex << 1u) & 2u, gl_VertexIndex & 2u);
    out_uv = pos;
    gl_Position = vec4(pos * 2.0f - 1.0f, 0.0f, 1.0f);
}
