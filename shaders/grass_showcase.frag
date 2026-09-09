#version 460

// Grass material showcase: the sampling model that moves into world_map.frag.
// Split view — left: the current procedural paint, right: the P0 grass
// material set (albedo + tangent-space normal + ORM) under a movable sun.
// Controls live in the push constants: mode (split / textured / procedural),
// sun direction, and the tiling density (world metres per 4 m texture tile).

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D albedo_tex;
layout(set = 0, binding = 1) uniform sampler2D normal_tex;
layout(set = 0, binding = 2) uniform sampler2D orm_tex;

layout(push_constant) uniform Push {
    vec4 light_dir;    // xyz: direction TOWARD the light (normalized on CPU)
    vec4 params;       // x: mode (0 split / 1 textured / 2 procedural)
                       // y: tiles across the screen
                       // z: aspect (width/height)
                       // w: anti-tiling on (1) / naive tiling (0)
    vec4 style;        // x: stylize (1 = V3 painterly muted, 0 = raw photo PBR)
} pc;

float hash21(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

float value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // quintic: no visible cell grid
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    // Two octaves: large-scale patches + medium wobble. Kills the blocky
    // single-octave cell structure that showed up when zoomed out.
    return value_noise(p) * 0.65f + value_noise(p * 2.7f + 11.1f) * 0.35f;
}

vec3 procedural_grass(vec2 world_uv) {
    // The current paint: a flat base with cheap sinusoidal/hash variation.
    vec3 base = vec3(0.20, 0.33, 0.12);
    float patches = fbm(world_uv * 3.0);
    float blades = value_noise(world_uv * 24.0);
    vec3 col = base * (0.75 + 0.5 * patches) * (0.9 + 0.2 * blades);
    return col;
}

float blinn_spec(vec3 n, vec3 l, vec3 v, float roughness) {
    vec3 h = normalize(l + v);
    float spec_exp = mix(128.0, 4.0, clamp(roughness, 0.0, 1.0));
    float spec = pow(max(dot(n, h), 0.0), spec_exp);
    // Energy-ish scaling with roughness.
    return spec * mix(1.0, 0.15, roughness);
}

void main() {
    float aspect = pc.params.z;
    vec2 view_uv = vec2(in_uv.x * aspect, in_uv.y);
    vec2 world_uv = view_uv * pc.params.y;

    int mode = int(pc.params.x + 0.5);
    bool textured_side = (mode == 1) || (mode == 0 && in_uv.x >= 0.5);

    vec3 color;
    if (textured_side) {
        // Anti-tiling (the industry-standard combination, as used by the
        // Paradox map games): two samples of the SAME texture at different
        // rotation/scale + a low-frequency noise mask blending them, plus a
        // large-scale brightness tint. The repetition period of each sample
        // never aligns, so the visible "wallpaper" pattern dissolves. Toggle
        // with T to compare against naive tiling.
        const bool anti_tiling = pc.params.w > 0.5;
        vec2 uv_a = world_uv;
        vec2 uv_b = vec2(world_uv.x * 0.36 - world_uv.y * 0.48,
                         world_uv.x * 0.48 + world_uv.y * 0.36) + 0.37;
        if (!anti_tiling) uv_b = world_uv;

        vec3 albedo_a = texture(albedo_tex, uv_a).rgb;
        vec3 albedo_b = texture(albedo_tex, uv_b).rgb;
        vec3 n_sample = texture(normal_tex, uv_a).rgb;
        vec3 n_sample_b = texture(normal_tex, uv_b).rgb;
        vec3 orm = texture(orm_tex, uv_a).rgb;

        float variation = fbm(world_uv * 0.11);
        float blend = smoothstep(0.35, 0.65, variation);
        vec3 albedo = mix(albedo_a, albedo_b, blend);
        vec3 n_sample_mixed = mix(n_sample, n_sample_b, blend);
        // Large-scale brightness tint (macro variation): in production the
        // lush/dry albedo variants replace the brightness term here. Strong
        // enough to dominate the tiled pattern at strategic zoom.
        float macro = fbm(world_uv * 0.05 + 7.3);
        float tint = 0.72 + 0.56 * macro;

        float ao = orm.r;
        float roughness = clamp(orm.g, 0.05, 1.0);
        // Metallic is zero for natural grass (validated in the pack QA); kept
        // in the lighting anyway so the demo shows the full ORM response.
        float metallic = orm.b;

        // Tangent frame for a flat ground plane (X right, Y "up" in uv space,
        // Z out of the screen toward the viewer).
        float stylize = pc.style.x;
        vec3 n_ts = n_sample_mixed * 2.0 - 1.0;
        n_ts = mix(n_ts, vec3(0.0, 0.0, 1.0), 0.65 * stylize); // soften relief
        vec3 n_geom = vec3(0.0, 0.0, 1.0);
        vec3 t = vec3(1.0, 0.0, 0.0);
        vec3 b = vec3(0.0, 1.0, 0.0);
        vec3 n = normalize(t * n_ts.x + b * n_ts.y + n_geom * n_ts.z);

        vec3 l = normalize(pc.light_dir.xyz);
        vec3 v = vec3(0.0, 0.0, 1.0);

        // V3 painterly treatment (matches the reference screenshots): the
        // V3 painterly treatment (matches the reference screenshots): the
        // ground is a MUTED, desaturated sage field — texture detail and
        // normal response are subtle, specular is nearly absent, and lighting
        // is ambient-dominant. The visual richness comes from the prop layer
        // (trees/buildings/roads), not the ground albedo.
        vec3 sage_tint = vec3(0.42, 0.47, 0.38);
        float luma = dot(albedo, vec3(0.299, 0.587, 0.114));
        vec3 muted = mix(vec3(luma), albedo, 0.30) * sage_tint * 2.1;
        albedo = mix(albedo, muted, stylize);
        roughness = mix(roughness, 0.95, stylize);
        metallic *= (1.0 - stylize);

        float ndl = max(dot(n, l), 0.0);
        float diffuse = mix(ndl, 0.55 + 0.45 * ndl, stylize) * ao;
        vec3 albedo_linear = albedo * albedo; // approximate sRGB -> linear
        vec3 diffuse_col = albedo_linear * diffuse;
        vec3 spec = vec3(blinn_spec(n * mix(1.0, 0.35, stylize), l, v, roughness)) *
                    mix(vec3(0.04), albedo_linear, metallic) * ndl *
                    mix(1.0, 0.15, stylize);
        vec3 ambient = albedo_linear * mix(0.30, 0.62, stylize) * ao;
        color = (diffuse_col + spec + ambient) * tint;
        // The swapchain is an sRGB format: the hardware performs the final
        // linear -> sRGB encode. Manual gamma here would double-encode and
        // wash the material out.
    } else {
        vec3 paint = procedural_grass(world_uv);
        // Same sun response as the textured side's geometry normal (flat).
        vec3 l = normalize(pc.light_dir.xyz);
        float ndl = max(dot(vec3(0.0, 0.0, 1.0), l), 0.0);
        color = paint * (0.35 + 0.65 * ndl);
    }

    if (mode == 0) {
        // Hairline divider at the split.
        float d = abs(in_uv.x - 0.5);
        if (d < 0.0012) color = vec3(1.0);
    }
    out_color = vec4(color, 1.0);
}
