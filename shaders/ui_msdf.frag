#version 460

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    vec2 invViewport;
    float pxRange;
    float mapTextMode;
} pc;

float median3(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    vec3 sampleValue = texture(atlas, uv).rgb;
    float signedDistance = median3(sampleValue.r, sampleValue.g, sampleValue.b) - 0.5;

    // Convert the atlas-space distance range into screen pixels.  A constant
    // multiplier produces broken thin strokes when UI scale or DPI changes;
    // derivatives keep coverage stable from small labels to large headings.
    vec2 unitRange = vec2(max(pc.pxRange, 1.0)) / vec2(textureSize(atlas, 0));
    vec2 screenTexSize = vec2(1.0) / max(fwidth(uv), vec2(1e-6));
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.15);
    // A tiny coverage bias keeps high-contrast serif hairlines from turning
    // grey at dense HUD sizes while derivative scaling preserves large-title
    // edges. This is the MSDF equivalent of restrained small-size hinting.
    float smallGlyphBias = mix(0.035, 0.0, clamp((screenPxRange - 1.15) / 2.0, 0.0, 1.0));
    float alpha = clamp((signedDistance + smallGlyphBias) * screenPxRange + 0.5, 0.0, 1.0);

    if (pc.mapTextMode > 0.5) {
        // Opaque ink in the glyph body, with coverage only at its edge.
        // The old translucent white halo made map labels look washed out.
        outColor = vec4(color.rgb, color.a * alpha);
    } else {
        outColor = vec4(color.rgb, color.a * alpha);
    }
}
