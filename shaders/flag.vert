#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec2 flagUv;
layout(location = 1) out float clothShade;

layout(push_constant) uniform FlagPush {
    vec4 placement; // origin xy, local-to-NDC scale zw
    vec4 motion;    // time, wind strength, frequency, pattern
    vec4 primary;
    vec4 secondary;
    vec4 accent;
} pc;

void main() {
    float pinned = inUv.x * inUv.x;
    float phase = inUv.x * pc.motion.z - pc.motion.x + inUv.y * 1.7;
    float secondaryWave = sin(phase * 0.53 + inUv.y * 4.1) * 0.32;
    float wave = (sin(phase) + secondaryWave) * pc.motion.y * pinned;

    // Compact perspective camera matching the dedicated badge viewport used
    // by the reference implementation. Depth changes now alter silhouette
    // and highlight instead of merely tinting a flat rectangle.
    vec3 local = inPosition;
    local.z += wave;
    local.y += wave * 0.14;
    float perspective = 2.15 / max(2.15 - local.z, 1.25);
    vec2 projected = local.xy * perspective;
    vec2 position = pc.placement.xy + projected * pc.placement.zw;
    gl_Position = vec4(position, 0.0, 1.0);
    flagUv = inUv;
    float foldNormal = cos(phase) * pc.motion.y * pc.motion.z * pinned;
    clothShade = clamp(0.82 + foldNormal * 0.34 + wave * 0.24, 0.56, 1.10);
}
