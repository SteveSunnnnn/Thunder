#version 460

// FXAA (fast approximate anti-aliasing). This is the anti-aliasing path for
// hardware where MSAA resolve bandwidth is too expensive: integrated GPUs and
// high-DPI displays at low quality tiers.
//
// Runs on the resolved LDR image after tonemapping, in display space where
// edge contrast is perceptually meaningful.

layout(set = 0, binding = 0) uniform sampler2D srcColor;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    vec4 view;    // MapView, consumed by fullscreen.vert
    vec4 params;  // xy = texel size (1/width, 1/height)
} pc;

const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;
const float FXAA_SPAN_MAX = 8.0;
const float FXAA_EDGE_THRESHOLD = 1.0 / 8.0;
const float FXAA_EDGE_THRESHOLD_MIN = 1.0 / 24.0;

float luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texel = pc.params.xy;
    if (texel.x <= 0.0 || texel.y <= 0.0) {
        outColor = vec4(texture(srcColor, uv).rgb, 1.0);
        return;
    }

    vec3 rgb_nw = texture(srcColor, uv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgb_ne = texture(srcColor, uv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgb_sw = texture(srcColor, uv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgb_se = texture(srcColor, uv + vec2( 1.0,  1.0) * texel).rgb;
    vec3 rgb_m = texture(srcColor, uv).rgb;

    float l_nw = luma(rgb_nw);
    float l_ne = luma(rgb_ne);
    float l_sw = luma(rgb_sw);
    float l_se = luma(rgb_se);
    float l_m = luma(rgb_m);

    float l_min = min(l_m, min(min(l_nw, l_ne), min(l_sw, l_se)));
    float l_max = max(l_m, max(max(l_nw, l_ne), max(l_sw, l_se)));
    float l_range = l_max - l_min;

    // Flat regions early out. The map is dominated by flat paper fills, so
    // most of the screen pays only the five fetches above.
    if (l_range < max(FXAA_EDGE_THRESHOLD_MIN, l_max * FXAA_EDGE_THRESHOLD)) {
        outColor = vec4(rgb_m, 1.0);
        return;
    }

    // Cross gradients give the dominant edge direction.
    vec2 dir;
    dir.x = -((l_nw + l_ne) - (l_sw + l_se));
    dir.y = ((l_nw + l_sw) - (l_ne + l_se));

    float dir_reduce = max((l_nw + l_ne + l_sw + l_se) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * texel;

    // Two-tap estimate along the edge, then a wider four-tap estimate. Taking
    // the narrower result when the wider one overshoots the local luma range
    // is what keeps FXAA from over-blurring high-contrast corners.
    vec3 rgb_a = 0.5 * (texture(srcColor, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                        texture(srcColor, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgb_b = rgb_a * 0.5 + 0.25 * (texture(srcColor, uv + dir * -0.5).rgb +
                                       texture(srcColor, uv + dir * 0.5).rgb);

    float l_b = luma(rgb_b);
    outColor = vec4((l_b < l_min || l_b > l_max) ? rgb_a : rgb_b, 1.0);
}
