#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in mat4 instanceTransform;
layout(location = 0) out vec3 localPosition;

void main() {
    localPosition = inPosition;
    gl_Position = instanceTransform * vec4(inPosition, 1.0);
}
