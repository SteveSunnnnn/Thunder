#version 460

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uvIn;
layout(location = 2) in vec4 colorIn;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

// Thunder UI coordinates and Vulkan framebuffer coordinates both use a top-left
// origin with +Y pointing down when the viewport height is positive.  Do not
// apply the OpenGL-style Y flip here; doing so mirrors geometry while scissors
// and pointer hit testing remain in the original coordinate system.
layout(push_constant) uniform Push {
    vec2 invViewport;
    float pxRange;
    float reserved;
} pc;

void main() {
    uv = uvIn;
    color = colorIn;
    vec2 p = pos * pc.invViewport * 2.0 - 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
}
