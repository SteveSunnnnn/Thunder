#version 460

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// pc.view is consumed by fullscreen.vert; pc.params is this stage's grading
// control: x = srgb_output, y = dither_enable, z = exposure, w = vignette.
layout(push_constant) uniform Push {
    vec4 view;
    vec4 params;
} pc;

// Narkowicz ACES filmic approximation. Cheap, well-behaved, and the look the
// rest of the atlas palette was authored against.
vec3 aces_tonemap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3(0.0), vec3(1.0));
}

// Interleaved gradient noise: a low-discrepancy, temporally stable hash that
// avoids the fixed-pattern look of ordered dithering matrices.
float interleaved_gradient(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// Triangular-PDF dither. Summing two decorrelated uniform samples produces a
// triangular distribution, which is the only noise shape that removes
// quantisation banding without leaving a residual correlation between the
// error and the signal. Amplitude is exactly one 8-bit LSB.
vec3 triangular_pdf_dither(vec3 color, vec2 seed) {
    float r0 = interleaved_gradient(seed);
    float r1 = interleaved_gradient(seed + vec2(1031.0, 1739.0));
    float noise = (r0 + r1 - 1.0) / 255.0;
    return color + vec3(noise);
}

void main() {
    vec3 hdr = texture(hdrColor, uv).rgb;

    float exposure = pc.params.z > 0.0 ? pc.params.z : 1.0;
    vec3 mapped = aces_tonemap(hdr * exposure);

    // The atlas art direction is a printed sheet, so the grade stays subtle:
    // a gentle lift in the midtones and a very slight corner falloff that
    // reads as the curvature of a bound page rather than a lens vignette.
    float vignette = pc.params.w;
    if (vignette > 0.0) {
        vec2 centered = uv - 0.5;
        float falloff = 1.0 - dot(centered, centered) * vignette;
        mapped *= clamp(falloff, 0.0, 1.0);
    }

    // sRGB transfer happens before dithering: quantisation to 8 bit occurs in
    // display space, so that is where the noise has to be injected.
    if (pc.params.x == 0.0) {
        mapped = pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
    }

    if (pc.params.y != 0.0) {
        mapped = triangular_pdf_dither(mapped, gl_FragCoord.xy);
    }

    outColor = vec4(clamp(mapped, 0.0, 1.0), 1.0);
}
