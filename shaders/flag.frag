#version 460

layout(location = 0) in vec2 flagUv;
layout(location = 1) in float clothShade;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform FlagPush {
    vec4 placement;
    vec4 motion;
    vec4 primary;
    vec4 secondary;
    vec4 accent;
} pc;

float lineMask(float distanceToLine, float halfWidth) {
    float aa = max(fwidth(distanceToLine), 0.0015);
    return 1.0 - smoothstep(halfWidth - aa, halfWidth + aa, distanceToLine);
}

void main() {
    int pattern = int(round(pc.motion.w));
    vec3 color = pc.primary.rgb;
    if (pattern == 1) {
        color = flagUv.y < 0.333 ? pc.primary.rgb :
                flagUv.y < 0.666 ? pc.secondary.rgb : pc.accent.rgb;
    } else if (pattern == 2) {
        color = flagUv.x < 0.333 ? pc.primary.rgb :
                flagUv.x < 0.666 ? pc.secondary.rgb : pc.accent.rgb;
    } else if (pattern == 3) {
        float broad = max(lineMask(abs(flagUv.x - 0.38), 0.115),
                          lineMask(abs(flagUv.y - 0.50), 0.115));
        float narrow = max(lineMask(abs(flagUv.x - 0.38), 0.055),
                           lineMask(abs(flagUv.y - 0.50), 0.055));
        color = mix(color, pc.secondary.rgb, broad);
        color = mix(color, pc.accent.rgb, narrow);
    } else if (pattern == 4) {
        float diagonalDistance = min(abs(flagUv.y - flagUv.x),
                                     abs(flagUv.y - (1.0 - flagUv.x)));
        float crossDistance = min(abs(flagUv.x - 0.5), abs(flagUv.y - 0.5));
        float broad = max(lineMask(diagonalDistance, 0.105), lineMask(crossDistance, 0.105));
        float narrow = max(lineMask(diagonalDistance, 0.036), lineMask(crossDistance, 0.046));
        color = mix(color, pc.secondary.rgb, broad);
        color = mix(color, pc.accent.rgb, narrow);
    }

    float weave = sin(flagUv.x * 210.0) * sin(flagUv.y * 125.0) * 0.010;
    float edgeSoftening = smoothstep(0.0, 0.025, flagUv.x) *
                          smoothstep(0.0, 0.025, 1.0 - flagUv.x) *
                          smoothstep(0.0, 0.025, flagUv.y) *
                          smoothstep(0.0, 0.025, 1.0 - flagUv.y);
    color *= clamp(clothShade + weave, 0.58, 1.10);
    color = mix(color * 0.78, color, edgeSoftening);
    outColor = vec4(color, 1.0);
}
