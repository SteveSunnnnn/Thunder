#version 460

// The map pass is one continuous material. The resident pyramids supply
// stable categorical geography; all pigment, terrain splatting and
// water variation is sampled in world-UV space so page boundaries cannot
// restart the texture. Patch identity (fine level, morph) arrives as flat
// varyings from world_map.vert so every derived field here blends the two
// sampled levels exactly like the displaced geometry does.
layout(location = 0) in vec2 uv;
layout(location = 1) in vec2 camera_delta;
layout(location = 2) in float closeFactor;
layout(location = 3) in float v_ndc_y;
layout(location = 4) flat in uvec2 v_levels;
layout(location = 5) flat in float v_morph;
layout(location = 6) flat in float v_rectx;
layout(location = 7) flat in float v_rectz;
layout(location = 8) flat in float v_recty;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D politicalPalette;
layout(set = 1, binding = 0) uniform sampler2D height_pyramid;
layout(set = 1, binding = 1) uniform highp usampler2D province_id_tex;
layout(set = 1, binding = 2) uniform sampler2D coast_sdf_tex;
layout(set = 1, binding = 6) uniform sampler2D chart_coast_tex;
// Index is the packed R16 raster code (0 is water/invalid sentinel).
// uvec2 = {Country_ID, ProvincePoliticalFlags}; never compare display colors.
layout(std430, set = 1, binding = 5) readonly buffer PoliticalIdentity {
    uvec2 political_identity[];
};

layout(push_constant) uniform Push {
    vec4 view;
    vec4 camera;
    vec4 shading_dbg; // x = visual-debug bitmask (uniform branch only)
    vec4 pad;
};

// Visual-debug predicates. Push constants are visible at global scope, so
// these work inside helper functions too (a main()-local decode would not).
// destiny --shading-debug <bits>: 1 no rivers, 2 no farm, 4 no shadow-march,
// 8 height-only greyscale, 16 no coast, 32 no graticule, 64 flat political,
// 256 rebuild shared-edge uv, 512 gate relief identically in both stages,
// 2048 restore the legacy hard shadow-march gate. Default 0 is the
// production path.
uint sdbg_bits() { return uint(shading_dbg.x + 0.5); }
bool sdbg_rivers_off() { return (sdbg_bits() & 1u) != 0u; }
bool sdbg_farm_off() { return (sdbg_bits() & 2u) != 0u; }
bool sdbg_march_off() { return (sdbg_bits() & 4u) != 0u; }
bool sdbg_height_only() { return (sdbg_bits() & 8u) != 0u; }
// Second debug bank, added while hunting the near-view hairline artefact.
// bit4 isolates the coastline SDF: it is the only sampled quantity whose
// screen-space derivative (fwidth) feeds back into a mask that is then
// mixed into `land`, so a discontinuity in it becomes a hard line.
bool sdbg_coast_off() { return (sdbg_bits() & 16u) != 0u; }
bool sdbg_grat_off() { return (sdbg_bits() & 32u) != 0u; }
bool sdbg_political_off() { return (sdbg_bits() & 64u) != 0u; }
// bit9 opts in to gating relief by the same land mask in both stages. Today
// this stage shades an ungated mountain while the vertex stage snaps
// displacement to zero at the land mask, which is a genuine inconsistency
// (see relief_gate below). It is NOT enabled by default: measured at the Alps
// camera it changes nothing at 60 km but moves ~2% of the 800 km frame, so it
// needs a visual verdict before it can become the default.
bool sdbg_consistent_relief_gate() { return (sdbg_bits() & 512u) != 0u; }
// Restores the pre-fix hard `>` gate on the shadow march, for A/B only. The
// march is now faded in across a band (see the shadow block); this bit brings
// back the step that produced the near-view hairline so the two can be
// compared in one build. Remove once the fix has had a visual sign-off.
bool sdbg_legacy_march_gate() { return (sdbg_bits() & 2048u) != 0u; }
// Material bisection bits. 8192 flattens the albedo to a constant while keeping
// lighting/march/normal untouched - a line that survives it is lit, not painted;
// a line that vanishes lives in the material splat. 16384 disables the city
// block grid (90 m blocks on a 108 m pixel moire suspect).
bool sdbg_flat_albedo() { return (sdbg_bits() & 8192u) != 0u; }
bool sdbg_city_off() { return (sdbg_bits() & 16384u) != 0u; }

// The compile-time pyramid geometry. Byte-identical with world_map.vert.
layout(std140, set = 1, binding = 3) uniform WorldLevels {
    vec4 base_pages;        // {40, 28}
    vec4 height_inv_size;   // 1 / height pyramid extent
    vec4 page_inv_size;     // 1 / province & SDF pyramid extent
    vec4 height_origin[4];  // level rectangle origin, height texels
    vec4 page_origin[4];    // level rectangle origin, page texels
    vec4 height_scale_bias; // metres = h01 * x + y
} lv;

// ---- Resident-pyramid sampling (byte-identical with world_map.vert) -------

vec2 map_uv(vec2 value) {
    return vec2(fract(value.x), clamp(value.y, 0.0, 1.0));
}

vec2 level_pages(uint L) { return lv.base_pages.zw / float(1u << L); }

// Allocated page grid: the same as level_pages except level 3 rounds its
// partial bottom row up, because the texture band reserves the full row.
vec2 level_grid(uint L) { return ceil(lv.base_pages.xy / float(1u << L)); }

// Baked height at (level, uv), 0..1 over the pack's u16 encoding. The 65x65
// corner grid bakes the shared edge corner of adjacent pages twice, so a
// LINEAR tap spanning two pages interpolates duplicated values correctly;
// only the dateline column (page 39's last corner vs page 0's first, which
// sit at opposite ends of the texture band) needs a clamp into the band.
float sample_height01(uint L, vec2 uv) {
    vec2 muv = map_uv(uv);
    vec2 pages = level_pages(L);
    vec2 p = muv * pages + vec2(0.0, level_grid(L).y - pages.y);
    vec2 pg = min(floor(p), level_grid(L) - 1.0);
    vec2 f = p - pg;
    vec2 texel = lv.height_origin[L].xy + pg * 65.0 + 0.5 + f * 64.0;
    texel = min(texel, lv.height_origin[L].xy + level_grid(L) * 65.0 - 1.0);
    return textureLod(height_pyramid, texel * lv.height_inv_size.xy, 0.0).r;
}

float sample_chart_distance_m(uint L, vec2 value) {
    vec2 p = map_uv(value) * level_pages(L) * 128.0;
    vec2 span = level_grid(L) * 128.0;
    p.y += span.y - level_pages(L).y * 128.0;
    p -= 0.5;
    ivec2 base = ivec2(floor(p));
    vec2 f = fract(p);
    int width = int(ceil(level_pages(L).x * 128.0));
    int x0 = ((base.x % width) + width) % width;
    int x1 = (x0 + 1) % width;
    int y0 = clamp(base.y, 0, int(span.y) - 1);
    int y1 = clamp(base.y + 1, 0, int(span.y) - 1);
    ivec2 o = ivec2(lv.page_origin[L].xy);
    float a = texelFetch(chart_coast_tex, o + ivec2(x0, y0), 0).r;
    float b = texelFetch(chart_coast_tex, o + ivec2(x1, y0), 0).r;
    float c = texelFetch(chart_coast_tex, o + ivec2(x0, y1), 0).r;
    float d = texelFetch(chart_coast_tex, o + ivec2(x1, y1), 0).r;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y) * (32767.0 * 32.0);
}

float sample_height_m(uint L, vec2 uv) {
    return sample_height01(L, uv) * lv.height_scale_bias.x + lv.height_scale_bias.y;
}

// Signed coast distance in pack units (1 unit = 0.5 m, negative = water;
// lakes are water on the pack side as well). R16_SNORM stores raw/32767, so
// scaling by 32767 recovers the exact i16 the pack bakes.
// Wrapping tx modulo px.x handles antimeridian and boundary wrapping cleanly
// without cross-level bleeding in the resident pyramid.
float sample_coast_raw(uint L, vec2 uv) {
    vec2 muv = map_uv(uv);
    vec2 px = level_pages(L) * 128.0;
    vec2 span = level_grid(L) * 128.0;
    float py = muv.y * px.y + span.y - px.y;
    float clamped_y = clamp(py, 0.5, span.y - 0.5);

    float tx = mod(muv.x * px.x - 0.5, px.x);
    if (tx < 0.0) tx += px.x;
    int x0 = int(floor(tx));
    int x1 = (x0 + 1) % int(px.x);
    float fx = fract(tx);

    float ty = clamped_y - 0.5;
    int y0 = clamp(int(floor(ty)), 0, int(span.y) - 1);
    int y1 = clamp(y0 + 1, 0, int(span.y) - 1);
    float fy = fract(ty);

    ivec2 o = ivec2(lv.page_origin[L].xy);
    float s00 = texelFetch(coast_sdf_tex, o + ivec2(x0, y0), 0).r;
    float s10 = texelFetch(coast_sdf_tex, o + ivec2(x1, y0), 0).r;
    float s01 = texelFetch(coast_sdf_tex, o + ivec2(x0, y1), 0).r;
    float s11 = texelFetch(coast_sdf_tex, o + ivec2(x1, y1), 0).r;
    float s0 = mix(s00, s10, fx);
    float s1 = mix(s01, s11, fx);
    return mix(s0, s1, fy) * 32767.0;
}

// Province ids are texel-fetched from the R16_UINT plane; the clamp into the
// level band replaces the old per-atlas-slot guard and keeps the +1 corner
// taps of the border filters inside the level's own rectangle.
// Wrapping t.x modulo span.x handles antimeridian and boundary wrapping cleanly.
uint province_texel(uint L, ivec2 t) {
    ivec2 span = ivec2(level_grid(L) * 128.0);
    int width = int(ceil(level_pages(L).x * 128.0));
    int wrapped_x = ((t.x % width) + width) % width;
    int clamped_y = clamp(t.y, 0, span.y - 1);
    return texelFetch(province_id_tex,
                      ivec2(lv.page_origin[L].xy) + ivec2(wrapped_x, clamped_y), 0).r;
}

uint sample_province(uint L, vec2 uv) {
    vec2 muv = map_uv(uv);
    vec2 px = level_pages(L) * 128.0;
    float py = muv.y * px.y + (level_grid(L).y * 128.0 - px.y);
    return province_texel(L, ivec2(int(floor(muv.x * px.x)), int(floor(py))));
}

// Morph-aware quantities: displacement and every derived field here must
// blend the two levels identically or shading and geometry drift apart.
float terrain_base_metres(uint La, uint Lb, float m, vec2 uv) {
    float h = sample_height_m(La, uv);
    if (m < 1.0) h = mix(sample_height_m(Lb, uv), h, m);
    return h;
}

float terrain_gate(uint La, uint Lb, float m, vec2 uv) {
    float sdf = sample_coast_raw(La, uv);
    if (m < 1.0) sdf = mix(sample_coast_raw(Lb, uv), sdf, m);
    return smoothstep(-300.0, 300.0, sdf);
}

float patch_height_m(vec2 uv) {
    return terrain_base_metres(v_levels.x, v_levels.y, v_morph, uv);
}

float patch_coast_raw(vec2 uv) {
    float sdf = sample_coast_raw(v_levels.x, uv);
    if (v_morph < 1.0) sdf = mix(sample_coast_raw(v_levels.y, uv), sdf, v_morph);
    return sdf;
}

// Baked global elevation rescaled so 10 km is full scale. The procedural fbm
// rides on top of this as detail whose amplitude grows with the base height:
// real ranges get real mountains, real basins stay quiet. Byte-identical in
// both stages.
float baked_base_height(vec2 mapUv) {
    return clamp(max(patch_height_m(mapUv), 0.0) / 10000.0, 0.0, 1.0);
}

// Land gate for relief, shared byte-identically with world_map.vert.
//
// Relief has to be gated by the *same* expression in both stages. The
// retired RGBA height atlas carried binary lake/spatial masks; the coast SDF
// encodes the same water side continuously (negative = water, ocean and
// lakes both), so a ~150 m transition band replaces the old smoothstep over
// the 0/1 mask. The integer province id is deliberately NOT part of the
// gate: it is nearest-sampled and therefore inherently discontinuous, which
// is exactly what has to be kept out of a term that displaces geometry.
float relief_gate(vec2 value) {
    return terrain_gate(v_levels.x, v_levels.y, v_morph, map_uv(value));
}

vec4 political_colour(uint provinceId) {
    if (provinceId == 0u) return vec4(0.0);
    ivec2 size = textureSize(politicalPalette, 0);
    uint capacity = uint(size.x * size.y);
    if (provinceId >= capacity) return vec4(0.0);
    ivec2 coordinate = ivec2(int(provinceId % uint(size.x)),
                             int(provinceId / uint(size.x)));
    return texelFetch(politicalPalette, coordinate, 0);
}

// (relief_gate above is the only mask-style quantity the material path needs;
// the retired height_at/lake_at/spatial_land_at accessors died with the atlas.)

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float value_noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 smooth_f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(hash12(cell), hash12(cell + vec2(1.0, 0.0)), smooth_f.x),
               mix(hash12(cell + vec2(0.0, 1.0)), hash12(cell + vec2(1.0)), smooth_f.x),
               smooth_f.y);
}

layout(constant_id = 0) const int BASE_OCTAVES = 5;
// Caps the relief fbm per quality tier (Low 3 / Medium 4 / High and Ultra 5).
// The runtime term below may only take octaves away, never add them back.
layout(constant_id = 1) const int DETAIL_OCTAVES = 5;

// Metres covered by one screen pixel. Derived from camera altitude rather than
// fwidth because the vertex stage has to agree with the fragment stage exactly:
// a derivative is undefined in a vertex shader, so an fwidth-based term would
// desynchronise the relief the geometry is displaced by from the relief the
// surface is shaded with.
float pixel_footprint_m() {
    const float altitude = max(camera.x, 350.0);
    return max(altitude * 0.0018, 1.0);
}

// Relief octaves worth evaluating for this pixel. High-frequency octaves stop
// carrying information once a pixel covers more ground than their period; past
// that point they only alias into far-view noise.
int relief_octaves() {
    const float footprint = pixel_footprint_m();
    int lod = DETAIL_OCTAVES;
    if (footprint > 8000.0) lod = 0;
    else if (footprint > 3000.0) lod = 1;
    else if (footprint > 1200.0) lod = 2;
    else if (footprint > 400.0) lod = 3;
    return min(DETAIL_OCTAVES, lod);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rotate = mat2(0.80, -0.60, 0.60, 0.80);
    for (int octave = 0; octave < BASE_OCTAVES; ++octave) {
        value += amplitude * value_noise(p);
        p = rotate * p * 2.03 + 17.1;
        amplitude *= 0.48;
    }
    return value;
}

// Gradient (Perlin-style) noise, quintic fade. Replaces value_noise inside the
// ridged fbm: value-noise iso-contours hug the lattice axes (each cell is a
// bilinear blend of four corners, so a v=0.5 contour runs as one long straight
// segment through the cell and continues into its neighbours), which made every
// octave's ridge 1-|2v-1| a set of straight creases along the two lattice axes
// - perpendicular pairs at i*36.87 deg. The *16 finite-difference normal then
// drew a paired light/dark hairline along each crease, and the slope-driven
// material masks (snow adhesion, cliff rock) painted rock lines across the
// snow fields along them. Gradient noise has no lattice-aligned iso-contours,
// so ridges become terrain-shaped instead of lattice-shaped. Byte-identical
// with world_map.vert.
vec2 grad_dir(vec2 c) {
    float a = hash12(c) * 6.2831853;
    return vec2(cos(a), sin(a));
}

float gradient_noise01(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(grad_dir(i), f);
    float b = dot(grad_dir(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(grad_dir(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(grad_dir(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    float g = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);   // ~[-0.71, 0.71]
    return clamp(0.5 + g * 0.72, 0.0, 1.0);
}

// Smooth |x| with corner radius k (C-infinity; equals |x| for |x| >> k). The
// ridge crest keeps its shape but the kink gets a rounded radius that scales
// with the pixel footprint, so octaves too fine to resolve become rounded
// hummocks instead of sub-pixel creases the normal gain turns into hairlines.
float sabs(float x, float k) { return sqrt(x * x + k * k) - k; }

// Ridged alpine fbm. `octaves` carries both the quality-tier cap and the runtime
// LOD term; callers pass min(DETAIL_OCTAVES, lod).
float realistic_alpine_fbm(vec2 world_pos_m, int octaves) {
    vec2 p = world_pos_m * (1.0 / 18000.0);
    vec2 w = vec2(gradient_noise01(p * 0.70), gradient_noise01(p * 0.70 + vec2(11.3, -7.9)));
    p += (w - 0.5) * 0.70;

    const mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
    // Both stages evaluate the same footprint from camera.x (no derivatives),
    // so the displacement and the shading round their creases identically.
    const float footprint = pixel_footprint_m();

    float total = 0.0;
    float amp = 1.0;
    float weight = 1.0;
    float half_period_m = 9000.0;
    float amplitude_sum = 0.0;
    float a_full = 1.0;

    for (int i = 0; i < octaves; ++i) {
        // Ridge corner radius: ~2 px on the ground, expressed in noise-value
        // units of this octave. At 60 km altitude this is 0.024 for octave 0
        // (sharp crest) and 0.49 for octave 4 (rounded hummock).
        float k = clamp(2.0 * footprint / half_period_m, 0.02, 0.6);
        float x = gradient_noise01(p) * 2.0 - 1.0;
        float n = 1.0 - sabs(x, k);
        n = n * n;
        // Per-octave fade instead of a hard integer cut, so nothing pops
        // between LOD tiers: an octave fades out once its half period drops
        // below ~1 px, continuously.
        float fade = 1.0 - smoothstep(0.35, 1.25, footprint / (2.0 * half_period_m));
        total += n * amp * weight * fade;
        // Clamping the ridge weight with a hard lower bound put two C1 creases
        // beside every ridge (at the n=0.556 and n=0.111 level sets). Fade in
        // the floor with a smoothstep instead so the weight is C1 everywhere.
        float wr = clamp(n * 1.8, 0.0, 1.0);
        weight = mix(0.20, 1.0, wr * wr * (3.0 - 2.0 * wr));
        // The energy rescale must see the same fade the contribution saw, or
        // fading an octave out would boost the survivors and far-view peaks
        // would drift instead of only losing their fine detail.
        amplitude_sum += a_full * fade;
        a_full *= 0.48;
        p = rot * p * 2.12;
        amp *= 0.48;
        half_period_m /= 2.12;
    }

    const float full_amplitude_sum = 1.0 + 0.48 + 0.2304 + 0.110592 + 0.0530842;

    float h = total * (full_amplitude_sum / max(amplitude_sum, 1.0e-4)) / 1.55;

    // ---- Erosion-inspired geometry (cheap, pure-function, no iteration) ----
    // Byte-identical with world_map.vert: both stages must compute the same h
    // or the vertex displacement and the fragment shading drift apart.
    // (1) Fluvial valley incision: carve branching valleys along a domain-warped
    // low-frequency network. ridged-inverted gradient noise is 1 on the valley
    // centre-lines, and gradient noise has no lattice-aligned iso-contours, so
    // the valleys are terrain-shaped rather than crease-shaped.
    vec2 vp = world_pos_m * (1.0 / 52000.0);
    vec2 vw = vec2(gradient_noise01(vp * 0.6 + vec2(31.0, 7.0)),
                   gradient_noise01(vp * 0.6 + vec2(-17.0, 23.0)));
    vp += (vw - 0.5) * 1.2;
    float vn_x = gradient_noise01(vp) * 2.0 - 1.0;
    float valley_net = max(1.0 - sabs(vn_x, 0.35), 0.0);
    valley_net = valley_net * valley_net;
    float carve_mask = smoothstep(0.12, 0.45, h) * (1.0 - smoothstep(0.92, 1.0, h));
    h -= valley_net * carve_mask * 0.06;
    // Soft-saturation knee. h can reach ~1.2 while the return clamps at 1.0,
    // which pinned the highest peaks on a flat plateau whose rim is a C0
    // height contour - the normal flips along that whole ring and draws a dark
    // outline around every snow cap. Compress only the top of the range with
    // a tanh knee: everything below 0.85 is bit-identical to the linear map
    // (so the snow/glacier thresholds at 0.56/0.66-0.80 do not move), the rim
    // is C1 (tanh'(0)=1 matches the identity slope), and the maximum lands
    // strictly below 1 so the clamp never engages. Byte-identical with .vert.
    const float knee = 0.85;
    if (h > knee) {
        h = knee + (1.0 - knee) * tanh((h - knee) / (1.0 - knee));
    }
    return pow(clamp(h, 0.0, 1.0), 1.25);
}

float latitude_degrees(vec2 mapUv) {
    float lat_rad = 2.0 * (atan(exp(3.1313170 - mapUv.y * 4.4482593)) - 0.7853981633974483);
    return lat_rad * 57.2957795;
}

float alpine_mountain_relief(vec2 mapUv, int octaves, float baked) {
    if (octaves <= 0) return 0.0;
    vec2 world_pos_m = vec2((mapUv.x - 0.5) * 40075016.686, 19971868.9 - mapUv.y * 28371606.8);
    float lon_deg = (mapUv.x - 0.5) * 360.0;
    float lat_deg = latitude_degrees(mapUv);

    // Major global mountain orogenic belts
    // 1. European Alps: lon 5.5 to 16.5, lat 44.5 to 48.2
    float d_alps = length(vec2((lon_deg - 10.5) / 5.5, (lat_deg - 46.6) / 1.5));
    float w_alps = 1.0 - smoothstep(0.35, 1.0, d_alps);

    // 2. Pyrenees
    float d_pyr = length(vec2((lon_deg - 0.8) / 2.8, (lat_deg - 42.6) / 0.9));
    float w_pyr = 1.0 - smoothstep(0.35, 1.0, d_pyr);

    // 3. Carpathians
    float d_carp = length(vec2((lon_deg - 22.0) / 4.5, (lat_deg - 47.5) / 2.0));
    float w_carp = 1.0 - smoothstep(0.35, 1.0, d_carp);

    // 4. Scandinavian Mountains
    float d_scan = length(vec2((lon_deg - 12.0) / 4.5, (lat_deg - 63.5) / 4.5));
    float w_scan = 1.0 - smoothstep(0.35, 1.0, d_scan);

    // 5. Caucasus
    float d_cauc = length(vec2((lon_deg - 43.5) / 5.0, (lat_deg - 42.5) / 1.8));
    float w_cauc = 1.0 - smoothstep(0.35, 1.0, d_cauc);

    // 6. Himalayas & Tibetan Plateau
    float d_him = length(vec2((lon_deg - 87.0) / 13.0, (lat_deg - 32.0) / 5.0));
    float w_him = 1.0 - smoothstep(0.35, 1.0, d_him);

    // 7. North American Rockies & Cascades
    float d_rock = length(vec2((lon_deg - (-114.0)) / 6.5, (lat_deg - 45.0) / 14.0));
    float w_rock = 1.0 - smoothstep(0.35, 1.0, d_rock);

    // 8. South American Andes
    float d_and = length(vec2((lon_deg - (-70.0)) / 4.0, (lat_deg - (-22.0)) / 32.0));
    float w_and = 1.0 - smoothstep(0.35, 1.0, d_and);

    // 9. East China / Huangshan / Southeast Hills (华东黄山、雁荡山、武夷山、大别山、江南丘陵)
    float d_huadong = length(vec2((lon_deg - 118.5) / 4.5, (lat_deg - 30.0) / 3.0));
    float w_huadong = 1.0 - smoothstep(0.35, 1.0, d_huadong);

    // 10. Shandong Hills & Mount Tai (泰山、鲁中丘陵)
    float d_shandong = length(vec2((lon_deg - 118.5) / 3.0, (lat_deg - 36.2) / 1.6));
    float w_shandong = 1.0 - smoothstep(0.35, 1.0, d_shandong);

    // 11. Central Sahara Massifs (Ahaggar & Tibesti volcanic massifs, ~3000m)
    float d_ahaggar = length(vec2((lon_deg - 5.5) / 3.2, (lat_deg - 23.3) / 2.2));
    float d_tibesti = length(vec2((lon_deg - 18.0) / 3.5, (lat_deg - 21.0) / 2.2));
    float w_sahara_mtn = max(1.0 - smoothstep(0.35, 1.0, d_ahaggar), 1.0 - smoothstep(0.35, 1.0, d_tibesti));

    // 12. Atlas Mountains (North Africa)
    float d_atlas = length(vec2((lon_deg - (-3.0)) / 6.5, (lat_deg - 32.5) / 2.0));
    float w_atlas = 1.0 - smoothstep(0.35, 1.0, d_atlas);

    // 13. Urals (Russia)
    float d_ural = length(vec2((lon_deg - 60.0) / 2.8, (lat_deg - 60.0) / 12.0));
    float w_ural = 1.0 - smoothstep(0.35, 1.0, d_ural);

    // 14. Altai & Tian Shan (Central Asia / Xinjiang)
    float d_altai = length(vec2((lon_deg - 86.0) / 8.0, (lat_deg - 46.0) / 4.5));
    float w_altai = 1.0 - smoothstep(0.35, 1.0, d_altai);

    // 15. Scottish Highlands & Grampians (UK)
    float d_scot = length(vec2((lon_deg - (-4.5)) / 2.2, (lat_deg - 57.0) / 1.5));
    float w_scot = 1.0 - smoothstep(0.35, 1.0, d_scot);

    // 16. Apennines (Italy)
    float d_apen = length(vec2((lon_deg - 13.0) / 2.5, (lat_deg - 42.5) / 3.0));
    float w_apen = 1.0 - smoothstep(0.35, 1.0, d_apen);

    // 17. Zagros & Alborz (Persia / Iran)
    float d_zagros = length(vec2((lon_deg - 51.0) / 6.0, (lat_deg - 32.5) / 4.0));
    float d_alborz = length(vec2((lon_deg - 52.0) / 5.0, (lat_deg - 36.0) / 1.5));
    float w_persia = max(1.0 - smoothstep(0.35, 1.0, d_zagros), 1.0 - smoothstep(0.35, 1.0, d_alborz));

    // 18. Japanese Alps / Honshu Spine (Japan)
    float d_japan = length(vec2((lon_deg - 137.5) / 3.5, (lat_deg - 36.0) / 3.0));
    float w_japan = 1.0 - smoothstep(0.35, 1.0, d_japan);

    // 19. Ethiopian Highlands (Horn of Africa)
    float d_eth = length(vec2((lon_deg - 38.5) / 3.5, (lat_deg - 9.5) / 3.0));
    float w_eth = 1.0 - smoothstep(0.35, 1.0, d_eth);

    // 20. Appalachian Mountains (Eastern US)
    float d_appal = length(vec2((lon_deg - (-79.0)) / 4.5, (lat_deg - 37.5) / 6.0));
    float w_appal = 1.0 - smoothstep(0.35, 1.0, d_appal);

    // 21. Drakensberg (Southern Africa)
    float d_drak = length(vec2((lon_deg - 29.0) / 2.5, (lat_deg - (-29.5)) / 2.5));
    float w_drak = 1.0 - smoothstep(0.35, 1.0, d_drak);

    // 22. Great Dividing Range (Australia)
    float d_gdr = length(vec2((lon_deg - 149.0) / 3.0, (lat_deg - (-33.0)) / 6.0));
    float w_gdr = 1.0 - smoothstep(0.35, 1.0, d_gdr);

    float mountain_belt = max(max(max(w_alps, w_pyr), max(w_carp, w_scan)),
                              max(max(w_cauc, w_him), max(w_rock, w_and)));
    mountain_belt = max(mountain_belt, max(max(w_huadong, w_shandong), max(w_sahara_mtn, w_atlas)));
    mountain_belt = max(mountain_belt, max(w_ural, w_altai));
    mountain_belt = max(mountain_belt, max(max(w_scot, w_apen), max(w_persia, w_japan)));
    mountain_belt = max(mountain_belt, max(max(w_eth, w_appal), max(w_drak, w_gdr)));

    // Combine DEM elevation and belt: lowlands (baked < 0.07) are plains; highlands or belts are mountains
    float mountain_factor = clamp(max(mountain_belt, smoothstep(0.06, 0.20, baked)), 0.0, 1.0);

    // Mountain relief: grand, soaring peaks with high amplitude (山地起伏大点)
    float mountain_h = 0.0;
    if (mountain_factor > 0.01) {
        mountain_h = realistic_alpine_fbm(world_pos_m, octaves) * 1.30;
    }

    // Plains: smooth, flat, calm ground without sharp creases (如果是平原的话可以稍微平坦一点)
    vec2 p_plain = world_pos_m * (1.0 / 30000.0);
    float plain_h = gradient_noise01(p_plain) * 0.025; // Gentle low-frequency undulation only

    // Desert dunes: macroscopic broad rolling swells, no dense repetitive micro-textures (不要用纹理贴图 太密集重复了)
    float lat_frac = abs(lat_deg) / 85.0;
    float subtropical_arid = (1.0 - smoothstep(0.04, 0.12, abs(lat_frac - 0.28))) *
        (smoothstep(-17.0, 0.0, lon_deg) * (1.0 - smoothstep(55.0, 65.0, lon_deg))); // Sahara & Arabia
    float asian_arid = (1.0 - smoothstep(0.06, 0.16, abs(lat_deg - 41.0) / 85.0)) *
        smoothstep(75.0, 85.0, lon_deg) * (1.0 - smoothstep(115.0, 122.0, lon_deg)); // Taklamakan & Gobi
    float southern_arid = (1.0 - smoothstep(0.04, 0.10, abs(lat_frac - 0.29))) *
        ((smoothstep(15.0, 20.0, lon_deg) * (1.0 - smoothstep(30.0, 35.0, lon_deg))) + // Kalahari
         (smoothstep(116.0, 122.0, lon_deg) * (1.0 - smoothstep(140.0, 146.0, lon_deg)))); // Outback
    float arid_belt = clamp(max(max(subtropical_arid, asian_arid), southern_arid), 0.0, 1.0);

    if (arid_belt > 0.01) {
        // Broad sweeping sand sea waves (wavelength 25km to 45km), gentle and painterly
        vec2 wind_dir = normalize(vec2(0.82, 0.57));
        vec2 cross_wind = vec2(-wind_dir.y, wind_dir.x);
        vec2 p_wind = vec2(dot(world_pos_m, cross_wind), dot(world_pos_m, wind_dir));
        float erg_swell = gradient_noise01(p_wind * (1.0 / 32000.0)) * 0.08;
        float dune_wave = sin(p_wind.x * (1.0 / 22000.0) + p_wind.y * (1.0 / 40000.0)) * 0.04;
        plain_h += (erg_swell + dune_wave) * arid_belt * (1.0 - mountain_factor * 0.6);
    }

    float elevation = mix(plain_h, mountain_h, mountain_factor);
    return clamp(elevation, 0.0, 1.0);
}

// Single source of truth for relief. The vertex stage derives its octave count
// from the same altitude term, so the silhouette and the shading cannot drift
// apart when the LOD drops octaves at distance.
float continuous_relief(vec2 mapUv) {
    float gate = relief_gate(mapUv);
    if (gate < 0.001) return 0.0;
    // Real global elevation from the resident pyramid, with the procedural fbm as
    // height-dependent detail on top. Byte-identical with terrainHeight()
    // in world_map.vert so displacement and shading cannot drift apart.
    float baked = baked_base_height(mapUv);
    if (sdbg_bits() == 0u) return baked * gate;
    int oct = relief_octaves();
    float height = oct > 0 ? alpine_mountain_relief(mapUv, oct, baked) : 0.0;
    float mountain_factor = clamp(max(smoothstep(0.06, 0.20, baked), 0.0), 0.0, 1.0);
    float detail_amp = mix(0.04, 0.70, mountain_factor);
    float combined = clamp(baked + height * detail_amp, 0.0, 1.0);
    // Gating here keeps normal finite differences, shadow march and shading
    // elevation in 100% byte-identical agreement with vertex displacement,
    // ensuring lakes and ocean stay flat with zero seams or mesh cracking.
    return combined * gate;
}

vec3 compute_smooth_normal(vec2 mapUv) {
    if (relief_gate(mapUv) < 0.001) return vec3(0.0, 0.0, 1.0);
    // Conformal metric finite-difference step matching physical ground meters
    // Earth equatorial circumference: 40075016.686 m, Mercator height: 28371606.8 m
    float eps_m = max(250.0, pixel_footprint_m());
    vec2 eps_uv = vec2(eps_m / 40075016.686, eps_m / 28371606.8);
    float hL = continuous_relief(mapUv - vec2(eps_uv.x, 0.0));
    float hR = continuous_relief(mapUv + vec2(eps_uv.x, 0.0));
    float hD = continuous_relief(mapUv - vec2(0.0, eps_uv.y));
    float hU = continuous_relief(mapUv + vec2(0.0, eps_uv.y));
    // Real mountain slopes: 30° to 60° slopes for dramatic chiaroscuro hillshading
    float slope_scale = 4000.0 / eps_m;
    float dh_dx = (hR - hL) * slope_scale;
    float dh_dy = (hU - hD) * slope_scale;
    return normalize(vec3(-dh_dx, -dh_dy, 1.0));
}

vec3 srgb_to_linear(vec3 value) {
    return pow(max(value, vec3(0.0)), vec3(2.2));
}

vec3 linear_to_srgb(vec3 value) {
    return pow(max(value, vec3(0.0)), vec3(1.0 / 2.2));
}

// ---- Colour grading stage (tonemap -> saturation -> LUT -> dither) ----
const float SATURATION = 1.08;  // pigment saturation lift
const float TINT_FLOOR = 0.28;  // deep-zoom minimum tint peak channel

float luma_of(vec3 value) {
    return dot(value, vec3(0.2126, 0.7152, 0.0722));
}

vec3 apply_saturation(vec3 value, float amount) {
    return max(mix(vec3(luma_of(value)), value, amount), vec3(0.0));
}

// Deep-zoom tint floor. Clamping each channel independently (max(c, k)) drags
// saturated dark colours toward grey, because the lifted channels rotate the
// hue. Scale the whole colour up instead so the hue survives, and only fall
// back to neutral grey for a genuinely achromatic tint.
vec3 tint_floor(vec3 tint) {
    float peak = max(max(tint.r, tint.g), tint.b);
    if (peak >= TINT_FLOOR) return tint;
    if (peak < 1.0e-4) return vec3(TINT_FLOOR * 0.5);
    return tint * (TINT_FLOOR / peak);
}

// ---- C3: screen-space fade for procedural detail ----
// `feature_m` is a feature's characteristic size in metres. Once a pixel
// covers more ground than the feature, the feature is sub-pixel and only
// contributes aliasing, so fade it toward zero.
float detail_fade(float feature_m, float pixel_m) {
    return 1.0 - smoothstep(feature_m * 0.35, feature_m * 1.25, pixel_m);
}

// Sub-pixel continuous anti-aliased isosurface border contour
// Smooths 8km staircase steps into natural curving boundaries
float sovereign_border_smooth(vec2 mapUv, uint centre_id, vec4 centre_colour, out float out_ribbon, out float out_prov_stroke, out vec3 out_pigment) {
    uint L = v_levels.x;
    vec2 px = level_pages(L) * 128.0;
    vec2 center = view.xy * px + vec2(0.0, level_grid(L).y * 128.0 - px.y) - 0.5;
    // Keep the varying fractional part small. At kilometre zoom, fwidth of a
    // 5000-wide absolute texture coordinate loses mantissa bits and produces
    // broken grid-like borders even when the shared source edge is continuous.
    vec2 p = camera_delta * px + fract(center);
    ivec2 base = ivec2(floor(center)) + ivec2(floor(p));
    vec2 f = fract(p);

    // Geography is constrained offline. No procedural displacement of borders.
    vec2 mf = f;

    // Sample 2x2 corner provinces. Same-level pages are physically adjacent in
    // the pyramid, so the taps cross page borders freely; province_texel only
    // clamps at the level band's edges (the dateline).
    uint id00 = province_texel(L, base);
    uint id10 = province_texel(L, base + ivec2(1, 0));
    uint id01 = province_texel(L, base + ivec2(0, 1));
    uint id11 = province_texel(L, base + ivec2(1, 1));

    vec4 c00 = political_colour(id00);
    vec4 c10 = political_colour(id10);
    vec4 c01 = political_colour(id01);
    vec4 c11 = political_colour(id11);

    // If centre is water or lake (alpha < 0.5), no sovereign borders cut across it
    if (centre_id == 0u || centre_colour.a < 0.5) {
        out_ribbon = 0.0;
        out_prov_stroke = 0.0;
        out_pigment = srgb_to_linear(centre_colour.rgb);
        return 0.0;
    }

    float pixels_per_texel = 1.0 / max(length(vec2(fwidth(p.x), fwidth(p.y))), 1.0e-4);

    // --- 1. Province / State internal boundary evaluation (V3 signature white state borders) ---
    // Evaluated for all land cells so internal state lines render inside countries
    uint pid_ref = 0u;
    if (id00 != 0u && c00.a > 0.5) pid_ref = id00;
    if (id10 != 0u && c10.a > 0.5 && (pid_ref == 0u || id10 < pid_ref)) pid_ref = id10;
    if (id01 != 0u && c01.a > 0.5 && (pid_ref == 0u || id01 < pid_ref)) pid_ref = id01;
    if (id11 != 0u && c11.a > 0.5 && (pid_ref == 0u || id11 < pid_ref)) pid_ref = id11;

    if (pid_ref == 0u) {
        out_prov_stroke = 0.0;
    } else {
        float q00 = (id00 == pid_ref) ? 1.0 : 0.0;
        float q10 = (id10 == pid_ref) ? 1.0 : 0.0;
        float q01 = (id01 == pid_ref) ? 1.0 : 0.0;
        float q11 = (id11 == pid_ref) ? 1.0 : 0.0;
        float prov_field = mix(mix(q00, q10, mf.x), mix(q01, q11, mf.x), mf.y);
        vec2 pgrad_tex = vec2(mix(q10 - q00, q11 - q01, mf.y),
                              mix(q01 - q00, q11 - q10, mf.x));
        float prov_texel_dist = abs(prov_field - 0.5) / max(length(pgrad_tex), 1.0e-3);
        float prov_dist_px = prov_texel_dist * pixels_per_texel;
        float prov_diff = abs(q00 - q10) + abs(q00 - q01) + abs(q10 - q11) + abs(q01 - q11);
        out_prov_stroke = prov_diff > 0.01 ? (1.0 - smoothstep(0.0, 1.1, prov_dist_px)) : 0.0;
    }

    // --- 2. Sovereign National Frontier evaluation ---
    // Find canonical reference country color across the cell (smallest non-zero, non-water ID)
    uint id_ref = 0u;
    if (id00 != 0u && c00.a > 0.5) id_ref = id00;
    if (id10 != 0u && c10.a > 0.5 && (id_ref == 0u || id10 < id_ref)) id_ref = id10;
    if (id01 != 0u && c01.a > 0.5 && (id_ref == 0u || id01 < id_ref)) id_ref = id01;
    if (id11 != 0u && c11.a > 0.5 && (id_ref == 0u || id11 < id_ref)) id_ref = id11;

    if (id_ref == 0u) {
        out_ribbon = 0.0;
        out_pigment = srgb_to_linear(centre_colour.rgb);
        return 0.0;
    }

    vec4 c_ref = political_colour(id_ref);

    // Weight of reference country across the 4 corners (only land corners count)
    uint owner_ref = political_identity[id_ref].x;
    float w00 = (id00 != 0u && c00.a > 0.5 && political_identity[id00].x == owner_ref) ? 1.0 : 0.0;
    float w10 = (id10 != 0u && c10.a > 0.5 && political_identity[id10].x == owner_ref) ? 1.0 : 0.0;
    float w01 = (id01 != 0u && c01.a > 0.5 && political_identity[id01].x == owner_ref) ? 1.0 : 0.0;
    float w11 = (id11 != 0u && c11.a > 0.5 && political_identity[id11].x == owner_ref) ? 1.0 : 0.0;

    // Check if cell has a sovereign boundary between distinct countries
    float diff_sum = abs(w00 - w10) + abs(w00 - w01) + abs(w10 - w11) + abs(w01 - w11);
    if (diff_sum < 0.01) {
        out_ribbon = 0.0;
        out_pigment = srgb_to_linear(centre_colour.rgb);
        return 0.0;
    }

    // Identify the secondary neighboring country color in this cell
    vec4 c_other = (w00 < 0.5) ? c00 : ((w10 < 0.5) ? c10 : ((w01 < 0.5) ? c01 : c11));

    // If neighbor is water, this is a coastline rather than a political frontier between two nations
    if (c_other.a < 0.5) {
        out_ribbon = 0.0;
        out_pigment = srgb_to_linear(centre_colour.rgb);
        return 0.0;
    }

    // Smooth continuous country membership field
    float field = mix(mix(w00, w10, mf.x), mix(w01, w11, mf.x), mf.y);

    // Continuous anti-aliased transition between country colors along field = 0.5
    float fill_blend = smoothstep(0.48, 0.52, field);
    out_pigment = srgb_to_linear(mix(c_other.rgb, c_ref.rgb, fill_blend));

    // Analytic distance in texels, converted to screen pixels without dFdx quad spikes
    vec2 grad_tex = vec2(mix(w10 - w00, w11 - w01, mf.y),
                         mix(w01 - w00, w11 - w10, mf.x));
    float texel_dist = abs(field - 0.5) / max(length(grad_tex), 1.0e-3);
    float dist_pixels = texel_dist * pixels_per_texel;

    // Border shadow (V3-measured): a soft dark gradient band ~8-15 px wide
    // flanking the hairline on both sides, like a pressed paper seam.
    out_ribbon = (1.0 - smoothstep(0.0, 12.0, dist_pixels));

    // V3 hairline stroke: measured 1-3 px on official screenshots
    float stroke = 1.0 - smoothstep(0.0, 1.1, dist_pixels);
    return stroke;
}

// (location_border_smooth was deleted with the atlas: its only caller always
// passed loc_stroke = 0.0, so the whole 4-tap path was dead.)

// Multi-layer PBR terrain splatting with metric scale procedural geomorphology
vec3 calculate_pbr_terrain_splat(vec2 mapUv, vec2 world_pos_m, float height, float slope, float moisture, float coast_sdf, uint centre_id, vec3 normal, vec3 sun_dir, out float out_roughness) {
    float lod_macro = smoothstep(600000.0, 3000000.0, camera.x);
    float lod_meso  = smoothstep(120000.0, 800000.0,  camera.x);
    float lod_micro = 1.0 - smoothstep(25000.0, 180000.0, camera.x);
    float lod_nano  = 1.0 - smoothstep(8000.0,  60000.0,  camera.x);

    float pixel_m = pixel_footprint_m();
    float fade_micro = detail_fade(90.0, pixel_m);  // 90 m cliffs and city blocks
    float fade_nano  = detail_fade(22.0, pixel_m);  // 22 m surface grain

    float micro_crag  = gradient_noise01(world_pos_m * (1.0 / 140.0)) * lod_micro * fade_micro;
    float nano_grain  = gradient_noise01(world_pos_m * (1.0 / 22.0)) * lod_nano * fade_nano;

    // Biome base albedo palettes (PBR calibrated linear)
    const vec3 c_beach     = vec3(0.68, 0.61, 0.46);
    const vec3 c_grass_low = vec3(0.20, 0.36, 0.14); // Lush lowland meadow
    const vec3 c_grass_dry = vec3(0.32, 0.38, 0.18); // Temperate grassland
    const vec3 c_forest    = vec3(0.07, 0.18, 0.08); // Dense pine/oak canopy
    const vec3 c_dirt      = vec3(0.32, 0.25, 0.17); // Rich humus loam
    const vec3 c_rock      = vec3(0.36, 0.35, 0.33); // Mountain granite
    const vec3 c_rock_dark = vec3(0.20, 0.19, 0.18);
    const vec3 c_scree     = vec3(0.46, 0.44, 0.40);
    const vec3 c_snow      = vec3(0.94, 0.96, 0.98);
    const vec3 c_ice       = vec3(0.72, 0.84, 0.90);

    float lat_deg = latitude_degrees(mapUv);
    float lat_fraction = abs(lat_deg) / 85.0;
    float subtropical_arid = 1.0 - smoothstep(0.04, 0.12, abs(lat_fraction - 0.28));
    float desert_mask = max(subtropical_arid * (1.0 - smoothstep(0.45, 0.75, height)),
                            (1.0 - smoothstep(0.18, 0.38, moisture)) * (1.0 - smoothstep(0.42, 0.70, height)));

    // 1. Lowland vegetation with moisture gradient and micro-variation
    vec3 grass = mix(c_grass_dry, c_grass_low, smoothstep(0.25, 0.65, moisture));
    grass *= 0.96 + nano_grain * 0.08;
    vec3 forest = mix(grass, c_forest, smoothstep(0.35, 0.70, moisture));
    forest *= 0.94 + micro_crag * 0.12;
    vec3 soil = mix(forest, c_dirt, smoothstep(0.18, 0.45, slope) * 0.6);

    // 2. Victoria 3 Farmland Patchwork (organic agricultural quilt in fertile lowlands and plains ONLY)
    // Mountain valleys and highlands remain 100% pure natural alpine pastures, forest groves, and wild rivers
    // FIX: is_lowland_plain previously 0.005-0.025 => farm invisible outside ultra-low valleys (Huadong 0.06 => 0). Widen to 0.18-0.38 so plains 0.06-0.30 are fertile.
    // FIX: lod_meso grew with altitude (farm visible only at 800km, invisible at 60km close). Invert to close-visible lod.
    float is_lowland_plain = (1.0 - smoothstep(0.18, 0.38, height)) * (1.0 - smoothstep(0.08, 0.22, slope));
    float farm_lod = 1.0 - smoothstep(70000.0, 900000.0, camera.x);
    float farm_detail_fade = detail_fade(1400.0, pixel_m) * 0.85 + 0.15;
    float farm_fertility = smoothstep(0.35, 0.60, moisture) * (1.0 - desert_mask * 0.85) * is_lowland_plain;
    if (farm_lod * farm_detail_fade > 0.05 && farm_fertility > 0.01 && !sdbg_farm_off()) {
        // High-contrast quilt: per-field hash (800m cells) gives sharp rectangular farmland vs smooth blobs.
        // Visible at 60km (7px/field) and 30km (15px/field) with parity to Alpine scree contrast.
        vec2 field_cell = floor(world_pos_m / 800.0);
        float field_hash = hash12(field_cell);
        float field_hash2 = hash12(field_cell + 17.0);
        vec3 c_wheat = vec3(0.72, 0.60, 0.22);
        vec3 c_pasture = vec3(0.18, 0.42, 0.14);
        vec3 c_plowed = vec3(0.36, 0.24, 0.16);
        vec3 field_col = field_hash < 0.38 ? c_pasture : (field_hash < 0.72 ? c_wheat : c_plowed);
        // Sub-field variation + hedgerow darkening on cell borders
        vec2 field_f = fract(world_pos_m / 800.0);
        float hedgerow = smoothstep(0.0, 0.06, min(min(field_f.x, 1.0 - field_f.x), min(field_f.y, 1.0 - field_f.y)));
        field_col *= 0.82 + 0.18 * hedgerow;
        field_col *= 0.92 + field_hash2 * 0.16;

        float lon_deg_f = (mapUv.x - 0.5) * 360.0;
        bool is_east_asia_farm = (lon_deg_f > 105.0 && lon_deg_f < 125.0 && lat_deg > 22.0 && lat_deg < 40.0);
        if (is_east_asia_farm) {
            // Jiangnan water town & rice paddy fields (flooded emerald mirrors + golden harvest + irrigation dykes)
            vec3 c_rice_green = vec3(0.14, 0.46, 0.16); // Lush vibrant rice shoots
            vec3 c_rice_water = vec3(0.18, 0.36, 0.34); // Flooded reflective paddy
            vec3 c_rice_ripe  = vec3(0.78, 0.68, 0.25); // Golden harvest field
            vec3 c_dyke       = vec3(0.24, 0.20, 0.15); // Mud & stone bunds
            vec3 paddy_col = field_hash < 0.42 ? c_rice_green : (field_hash < 0.75 ? c_rice_water : c_rice_ripe);
            paddy_col = mix(c_dyke, paddy_col, hedgerow);
            if (field_hash >= 0.42 && field_hash < 0.75) {
                // Specular sheen on flooded water paddies
                vec3 p_half = normalize(sun_dir + vec3(0.0, 0.0, 1.0));
                paddy_col += vec3(0.7, 0.65, 0.55) * pow(max(dot(normal, p_half), 0.0), 32.0) * 0.4;
            }
            field_col = paddy_col;
        }

        soil = mix(soil, field_col, farm_fertility * 0.95 * farm_lod * farm_detail_fade);
    }

    // 3. Dense Forest Groves and Woodlands in Valleys and Foothills (ForestCanopyInstancer)
    // FIX: grove_clump used lod_meso (far-only) => forest invisible close. Use close-visible grove_lod.
    float grove_lod = 1.0 - smoothstep(60000.0, 800000.0, camera.x);
    float grove_noise = gradient_noise01(world_pos_m * (1.0 / 1600.0));
    float grove_clump = gradient_noise01(world_pos_m * (1.0 / 420.0)) * grove_lod;
    float grove_mask = smoothstep(0.35, 0.65, grove_noise + moisture * 0.25 + grove_clump * 0.20) *
                       (1.0 - smoothstep(0.25, 0.45, slope)) * (1.0 - smoothstep(0.32, 0.58, height)) * (1.0 - desert_mask);
    
    // Species distribution: broadleaf oak/beech in lowlands vs alpine spruce/pine on slopes
    vec3 c_broadleaf = vec3(0.16, 0.32, 0.10);
    vec3 c_conifer   = vec3(0.06, 0.17, 0.07);
    float conifer_mix = smoothstep(0.12, 0.32, height);
    vec3 canopy_base = mix(c_broadleaf, c_conifer, conifer_mix);
    
    // Subsurface leaf scattering (SSS) when backlit by sun
    float leaf_sss = pow(max(dot(-sun_dir, normal), 0.0), 3.0) * 0.35;
    vec3 canopy_col = canopy_base * (0.85 + grove_clump * 0.24) + vec3(0.26, 0.48, 0.14) * leaf_sss;
    vec3 ground = mix(soil, canopy_col, grove_mask * 0.85 * grove_lod);

    // 4. Desert Sand Dunes in Arid Zones (Sweeping Barchan & Transverse Dunes)
    // FIX: sin stripes were wallpaper (8.8px FFT). Use large 2.6km/1.1km barchans (proven fbm scales like Alps)
    // + 42m ripples. Large wavelength survives vertex GRID (1km spacing) and gives 3D shading parity.
    float dune_n1 = gradient_noise01(world_pos_m * (1.0 / 2600.0) + vec2(7.0, 13.0));
    float dune_n2 = gradient_noise01(world_pos_m * (1.0 / 1100.0) + vec2(-5.0, 11.0));
    float dune_warp = fbm(world_pos_m * (1.0 / 5200.0)) * 0.55;
    float dune_wave = clamp((dune_n1 - 0.5) * 1.15 + 0.5, 0.0, 1.0) * 0.55
                    + clamp((dune_n2 - 0.5) * 1.25 + 0.5, 0.0, 1.0) * 0.35
                    + (dune_warp - 0.5) * 0.20 + 0.50 * 0.10;
    dune_wave = clamp(dune_wave, 0.0, 1.0);
    float dune_ridge = smoothstep(0.48, 0.74, dune_wave);
    float slipface = smoothstep(0.34, 0.54, dune_wave) * (1.0 - smoothstep(0.60, 0.86, dune_wave));
    float macro_dune = gradient_noise01(world_pos_m * (1.0 / 1600.0)) * 0.5 + 0.5;

    vec3 c_sand_base  = vec3(0.92, 0.68, 0.35); // Warm Saharan golden ochre
    vec3 c_sand_crest = vec3(1.05, 0.90, 0.52); // Sun-bleached dune crest (brighter for parity highlights)
    vec3 c_sand_shade = vec3(0.66, 0.42, 0.20); // Warm slipface shadow (deeper for 3D crevasse)
    vec3 desert = mix(c_sand_base, c_sand_shade, slipface * 0.75);
    desert = mix(desert, c_sand_crest, dune_ridge * 0.70);
    desert *= 0.88 + macro_dune * 0.18 + dune_wave * 0.08;
    // Add micro-shadow crevasse for dune troughs to create visible 3D at 60km
    desert *= 0.92 + dune_ridge * 0.10;

    // Rocky desert pavement / Hamada gravel vs golden erg dunes
    float hamada = smoothstep(0.58, 0.88, gradient_noise01(world_pos_m * (1.0 / 7000.0)));
    vec3 c_hamada_rock = vec3(0.52, 0.38, 0.26); // Desert varnish / reg pavement
    desert = mix(desert, c_hamada_rock, hamada * 0.50);

    // Oasis in desert depressions
    float oasis_noise = gradient_noise01(world_pos_m * (1.0 / 35000.0));
    float oasis_mask = smoothstep(0.83, 0.93, oasis_noise) * (1.0 - smoothstep(0.03, 0.10, height));
    vec3 c_oasis = vec3(0.12, 0.36, 0.14); // Date palm oasis grove
    desert = mix(desert, c_oasis, oasis_mask * 0.85);

    ground = mix(ground, desert, desert_mask);

    // 5. Beach & coastal sand (strictly restricted to ocean coastlines with wet sand darkening!)
    float beach_mask = (1.0 - smoothstep(0.01, 0.06, height)) *
                       (1.0 - smoothstep(50.0, 1200.0, max(coast_sdf, 0.0))) *
                       (1.0 - smoothstep(0.10, 0.25, slope));
    float wetness = 1.0 - smoothstep(0.0, 90.0, max(coast_sdf, 0.0));
    vec3 beach_col = mix(c_beach, c_beach * 0.65, wetness * 0.70);
    ground = mix(ground, beach_col, beach_mask);

    // 6. Alpine Meadows and Mountain Foothills (strictly gated out of desert!)
    float alpine_meadow = smoothstep(0.22, 0.35, height) * (1.0 - smoothstep(0.44, 0.58, height)) * (1.0 - smoothstep(0.12, 0.28, slope)) * (1.0 - desert_mask);
    ground = mix(ground, vec3(0.24, 0.36, 0.16), alpine_meadow * 0.75);

    // 7. Alpine scree & gravel moraine
    float scree_mask = smoothstep(0.32, 0.48, height) * (1.0 - smoothstep(0.52, 0.66, height)) * smoothstep(0.06, 0.18, slope) * (1.0 - desert_mask);
    ground = mix(ground, c_scree, scree_mask * 0.70);

    // Steep rock cliff splatting with natural geological rock fracture (no artificial periodic sine stripes)
    float cliff_mask = smoothstep(0.12, 0.30, slope) + smoothstep(0.42, 0.65, height) * 0.70;
    cliff_mask = clamp(cliff_mask, 0.0, 1.0);
    float rock_macro = gradient_noise01(world_pos_m * (1.0 / 1200.0));
    float rock_detail = gradient_noise01(world_pos_m * (1.0 / 320.0));
    vec3 cliff = mix(c_rock, c_rock_dark, smoothstep(0.32, 0.68, rock_macro * 0.65 + rock_detail * 0.35));
    vec3 c_desert_rock = vec3(0.64, 0.42, 0.26); // Ferruginous Saharan sandstone
    cliff = mix(cliff, c_desert_rock, desert_mask * 0.85);
    ground = mix(ground, cliff, cliff_mask * (1.0 - smoothstep(0.66, 0.80, height)));

    // 8. Dynamic Seasonal Snow & High Altitude Glacial Ice on Peaks (TerrainMaterialShading slope clearance)
    float snow_threshold = 0.68;
    float snow_slope_adhesion = 1.0 - smoothstep(0.22, 0.52, slope);
    float snow_mask = smoothstep(snow_threshold, snow_threshold + 0.10, height) * snow_slope_adhesion;
    vec3 snow = mix(c_snow, c_ice, smoothstep(0.08, 0.24, slope) * 0.5);
    ground = mix(ground, snow, snow_mask);

    // 9. Glacial Crevasses, Ice Tongues and Polar Ice Sheets
    float polar_ice = smoothstep(0.48, 0.65, lat_fraction);
    float alpine_glacier = smoothstep(0.66, 0.80, height) * (1.0 - smoothstep(0.18, 0.42, slope));
    float glacier_mask = max(polar_ice, alpine_glacier);

    if (glacier_mask > 0.01) {
        float crevasse_noise = gradient_noise01(world_pos_m * (1.0 / 450.0));
        vec3 c_glacier_ice = vec3(0.40, 0.75, 0.92); // Turquoise meltwater ice
        vec3 c_moraine     = vec3(0.28, 0.27, 0.26); // Rock debris moraine
        vec3 glacier_surface = mix(c_glacier_ice, c_moraine, smoothstep(0.40, 0.70, crevasse_noise));
        glacier_surface = mix(glacier_surface, c_snow, smoothstep(0.70, 0.84, height + polar_ice * 0.5));
        ground = mix(ground, glacier_surface, glacier_mask);
    }

    // 10. Rivers. Everything that feeds a derivative is computed here, in
    // uniform control flow; the branch below only consumes it.
    //
    // The old river had three compounding defects. (a) Its centreline walked a
    // value_noise v=0.5 contour - the same lattice-axis-aligned iso-line that
    // drew straight creases through the relief, here unrotated so the segments
    // were screen-horizontal/vertical. (b) Its width was computed in river_uv
    // units but compared against a ridge-unit distance: the ridge field slopes
    // ~3.5 units per uv unit, so the real river was ~3.5x narrower than
    // river_width_m says - sub-pixel at 60 km, imaging only as aliased dashes.
    // (c) The dark bank tone was not scaled by the height fade, so the bank
    // painted full-strength brown over snow wherever the centreline band
    // reached 0.56-0.65. All three are fixed below: gradient noise, a measured
    // field-units-per-pixel from fwidth, coverage AA instead of pixel-centre
    // lottery, a height cap below the snow line, slope gating, and the bank
    // sharing the river's fades.
    vec2  river_uv    = world_pos_m * (1.0 / 14000.0);
    float river_noise = gradient_noise01(river_uv * 2.2 + vec2(17.0, -31.0));
    float meander     = sin(world_pos_m.y * (1.0 / 2200.0)
                            + gradient_noise01(world_pos_m * (1.0 / 4600.0)) * 6.0) * 0.08;
    float river_field = abs(river_noise - 0.5) * 2.0 + meander - 0.18;   // 0 on the centreline
    float river_dist  = abs(river_field);
    // Measured, not assumed: field units per screen pixel. Removes the
    // ridge-unit vs uv-unit mismatch that made rivers 3.5x narrower than
    // river_width_m says.
    float field_per_px = max(fwidth(river_field), 1.0e-5);
    float dist_px      = river_dist / field_per_px;

    float dist_to_sea   = clamp(-coast_sdf / 7000.0, 0.0, 1.0);
    float river_width_m = mix(220.0, 45.0, clamp(height / 0.35 + dist_to_sea * 0.6, 0.0, 1.0));
    float half_w_px     = 0.5 * river_width_m / pixel_footprint_m();

    // Coverage AA: a river narrower than a pixel is drawn one pixel wide at an
    // opacity equal to its coverage, instead of lottery-aliased per pixel centre.
    float river_cov   = clamp(2.0 * half_w_px, 0.0, 1.0);
    float edge_px     = dist_px - max(half_w_px, 0.5);
    // Rivers stop below the snow line (0.56) and stay off cliff faces.
    float river_env   = (1.0 - smoothstep(0.40, 0.52, height))
                      * (1.0 - smoothstep(0.22, 0.38, slope));
    // FIX: lod_meso grew with altitude (rivers disappeared at <120km close range).
    // Use close-visible river_lod with resolution-aware river_cov so rivers remain prominent at tactical zoom.
    float river_lod   = 1.0 - smoothstep(80000.0, 900000.0, camera.x);
    float river_mask  = (1.0 - smoothstep(-0.5, 0.5, edge_px)) * river_cov * river_env * river_lod;
    float bank_mask   = (1.0 - smoothstep( 0.0, 1.5, edge_px)) * river_cov * river_env * river_lod;

    if ((river_mask > 0.002 || bank_mask > 0.002) && !sdbg_rivers_off()) {
        vec3 c_river_deep    = vec3(0.04, 0.18, 0.28);
        vec3 c_river_shallow = vec3(0.08, 0.35, 0.44);
        vec3 c_river = mix(c_river_shallow, c_river_deep, smoothstep(25.0, 100.0, river_width_m));
        float rapids = smoothstep(0.06, 0.22, slope);
        c_river = mix(c_river, vec3(0.92, 0.96, 0.99), rapids * 0.65);
        vec3 r_half = normalize(sun_dir + vec3(0.0, 0.0, 1.0));
        c_river += vec3(1.3, 1.2, 1.0) * pow(max(dot(normal, r_half), 0.0), 48.0) * 1.25;

        ground = mix(ground, vec3(0.14, 0.12, 0.08), bank_mask * 0.6);   // bank shares the river's fades
        ground = mix(ground, c_river, river_mask);
    }

    // 11. 3D Settlements, Cities & Rural Architecture
    if (lod_micro > 0.05 && height < 0.60 && slope < 0.32 && desert_mask < 0.65 && glacier_mask < 0.65
        && !sdbg_city_off()) {
        vec2 city_cell_p = world_pos_m * (1.0 / 48000.0);
        vec2 cell_idx = floor(city_cell_p);
        vec2 cell_fract = fract(city_cell_p);

        float min_city_dist = 10.0;
        vec2 best_city_center = vec2(0.0);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                vec2 neighbor = vec2(float(dx), float(dy));
                vec2 city_pt = neighbor + hash22(cell_idx + neighbor) * 0.70 + 0.15;
                float d = length(cell_fract - city_pt);
                if (d < min_city_dist) {
                    min_city_dist = d;
                    best_city_center = (cell_idx + city_pt) * 48000.0;
                }
            }
        }
        float city_dist_m = length(world_pos_m - best_city_center);
        float city_radius_m = 2800.0;
        float city_density = 1.0 - smoothstep(600.0, city_radius_m, city_dist_m);

        if (city_density > 0.01) {
            // Architectural blocks: 90m city blocks (solid, visible 3D rooftops, avenues and shadows)
            vec2 bld_p = (world_pos_m - best_city_center) * (1.0 / 90.0);
            vec2 bld_id = floor(bld_p);
            vec2 bld_f = fract(bld_p);
            float bld_hash = hash12(bld_id);

            float avenue_w = mix(0.18, 0.26, city_density);
            float road_x = smoothstep(avenue_w * 0.75, avenue_w, min(bld_f.x, 1.0 - bld_f.x));
            float road_y = smoothstep(avenue_w * 0.75, avenue_w, min(bld_f.y, 1.0 - bld_f.y));
            float is_building = road_x * road_y * step(0.15, bld_hash);

            // C2: one screen pixel expressed in building-cell units. Derived
            // from pixel_m, which was measured in uniform control flow; taking
            // a derivative here, inside a branched block, is undefined.
            float bld_w = max(pixel_m / 90.0, 1.0e-4) * 1.2;

            // Roof courtyard perimeter
            vec2 court_f = abs(bld_f - 0.5) * 2.0;
            float courtyard = (1.0 - smoothstep(0.35 - bld_w, 0.35 + bld_w, max(court_f.x, court_f.y))) *
                              step(0.50, bld_hash);
            is_building *= (1.0 - courtyard);

            vec3 c_roof_terracotta = vec3(0.76, 0.28, 0.16); // Red tile
            vec3 c_roof_slate      = vec3(0.28, 0.32, 0.38); // Blue slate
            vec3 c_stone_facade    = vec3(0.74, 0.68, 0.60); // Limestone
            vec3 c_cobblestone     = vec3(0.36, 0.34, 0.32); // Paved road

            float lon_deg_bld = (mapUv.x - 0.5) * 360.0;
            bool is_east_asia_city = (lat_deg > 22.0 && lat_deg < 42.0 && lon_deg_bld > 105.0 && lon_deg_bld < 125.0);
            if (is_east_asia_city) {
                // East Asian Jiangnan / Huizhou architecture: white walls and black slate tiles (粉墙黛瓦)
                c_roof_slate = vec3(0.18, 0.20, 0.22);
                c_stone_facade = vec3(0.90, 0.89, 0.86);
                c_cobblestone = vec3(0.32, 0.32, 0.30);
            }

            vec3 roof_col = mix(c_roof_terracotta, c_roof_slate, is_east_asia_city ? 0.90 : step(0.45, bld_hash));
            // 3D gable highlight according to sun (C2: pixel-wide terminator)
            float gable = smoothstep(0.5 - bld_w, 0.5 + bld_w,
                                     bld_f.x * sun_dir.x + bld_f.y * sun_dir.y);
            float roof_lighting = mix(0.85, 1.25, gable);
            vec3 bld_col = roof_col * roof_lighting;

            // 3D Directional cast shadow from building onto roads.
            // C2: the hard step edges were the near-view scratch artefacts;
            // widen the transition to one pixel instead.
            vec2 shadow_vec = -sun_dir.xy * 0.32;
            vec2 bld_shadow_f = bld_f + shadow_vec;
            vec2 shadow_in  = smoothstep(vec2(0.10) - bld_w, vec2(0.10) + bld_w, bld_shadow_f);
            vec2 shadow_out = 1.0 - smoothstep(vec2(0.90) - bld_w, vec2(0.90) + bld_w, bld_shadow_f);
            float in_shadow = shadow_in.x * shadow_out.x * shadow_in.y * shadow_out.y *
                              (1.0 - is_building);

            vec3 street = mix(c_cobblestone, c_stone_facade, 0.20) * (1.0 - in_shadow * 0.60);
            vec3 city_surface = mix(street, bld_col, is_building);

            ground = mix(ground, city_surface, city_density * lod_micro);
        }
    }

    // Debug 8192: flat albedo. Placed after the whole splat so every material
    // branch above is neutralised, before roughness/lighting so those stay live.
    if (sdbg_flat_albedo()) {
        ground = vec3(0.55);
    }
    out_roughness = mix(0.85, 0.65, cliff_mask);
    out_roughness = mix(out_roughness, 0.32, snow_mask * 0.8);
    out_roughness = mix(out_roughness, 0.55, beach_mask * 0.4);

    // Physically accurate, high-contrast mountain hillshading (Chiaroscuro)
    vec3 perturbed_normal = normal;
    if (desert_mask > 0.1) {
        // Dune ripple normal perturbation: micro-shading creates tangible 3D relief
        vec2 ripple_uv = world_pos_m * (1.0 / 42.0);
        float r_dx = gradient_noise01(ripple_uv + vec2(0.1, 0.0)) - gradient_noise01(ripple_uv - vec2(0.1, 0.0));
        float r_dy = gradient_noise01(ripple_uv + vec2(0.0, 0.1)) - gradient_noise01(ripple_uv - vec2(0.0, 0.1));
        vec3 ripple_norm = normalize(vec3(-r_dx * 3.5, -r_dy * 3.5, 1.0));
        perturbed_normal = normalize(mix(perturbed_normal, ripple_norm, desert_mask * lod_nano * 0.35));
    }
    float NdotL_raw = dot(perturbed_normal, sun_dir);
    float sun_direct = max(NdotL_raw, 0.0);
    float sun_ambient_wrap = clamp((NdotL_raw + 0.20) / 1.20, 0.0, 1.0);
    vec3 sun_color = vec3(1.38, 1.28, 1.08); // Brilliant alpine direct sunlight
    vec3 sky_color = mix(vec3(0.22, 0.30, 0.42), vec3(0.48, 0.42, 0.32), desert_mask * 0.65); // Cool mountain shadow ambient / warm desert dust haze

    // Smooth directional soft cast shadow across continuous mountain relief.
    //
    // This block owned the reported thin-line fault. At the Alps camera, forcing
    // the march off (bit 4) collapsed the strength of every detected hairline by
    // 32-94%, while every other channel bit - coast, graticule, political, page
    // slot clamp, uv rebuild, relief gate - left the frame bit-identical. Two
    // independent defects lived here, and they need separate fixes because they
    // draw lines in different places. See the tap comment below for the second.
    //
    // First: closeFactor, lod_meso and height are continuous fields, so testing
    // them with a hard `>` put a step of up to 0.85x of the direct term along
    // their level sets. closeFactor and lod_meso are functions of camera
    // altitude alone, so they are uniform over a frame and can only pop in time,
    // never draw a line; height is per-pixel, and its level set runs along the
    // foot of every massif. Just above the threshold the march reports full
    // occlusion at once - with current_h barely over it, every uphill tap sits
    // above the ray - then recovers as ray_h climbs, so the step read as a dark
    // notch with lit terrain on both sides. Fading the march in over a band
    // removes the discontinuity and keeps the shadow. Every smoothstep starts at
    // the old threshold, so the march is only ever weaker than before and never
    // active anywhere it was not: this cannot darken a pixel the old code left
    // lit, and the branch is entered on exactly the same pixels, so the tap cost
    // is unchanged. Measured effect: 0.012% of the near frame, and nothing at
    // all at 800 km or 18000 km, where closeFactor is already 0.287 and 0.
    float shadow = 1.0;
    float march_weight = 0.0;
    if (!sdbg_march_off()) {
        float h_gate = max(smoothstep(0.015, 0.06, height), smoothstep(0.02, 0.08, slope));
        march_weight = sdbg_legacy_march_gate()
            ? ((closeFactor > 0.3 && lod_meso > 0.05 && height > 0.04) ? 1.0 : 0.0)
            : smoothstep(0.30, 0.42, closeFactor)
              * smoothstep(0.05, 0.11, lod_meso)
              * h_gate;
    }
    if (march_weight > 0.0) {
        if (sdbg_legacy_march_gate()) {
            // Legacy A/B path: fixed 360 m steps, 5 strata on [0.5,5.5), the old
            // half-interval-blind jitter and the old 190/3600 vertical ramp. Kept
            // byte-for-byte so the pre-fix rendering can be reproduced in one
            // build for comparison (--shading-debug 2048). Remove once the new
            // march below has a visual sign-off.
            vec2 shadow_step_uv = -sun_dir.xy * (360.0 / 40075016.0);
            float dither = hash12(gl_FragCoord.xy);
            float current_h = height;
            float occlusion = 1.0;
            for (int s = 1; s <= 5; ++s) {
                float tap = float(s) - 0.5 + dither * 0.5;
                float sample_h = continuous_relief(mapUv + shadow_step_uv * tap);
                float ray_h = current_h + tap * (190.0 / 3600.0) * sun_dir.z;
                if (sample_h > ray_h) {
                    occlusion = min(occlusion,
                                    clamp((ray_h - sample_h) * 18.0 + 1.0, 0.15, 1.0));
                }
            }
            shadow = mix(1.0, occlusion, march_weight);
        } else {
            // Production march. The old march was correct about the *origin* of
            // the hairline but truncated the shadow: five 360 m steps reached
            // only ~1.5 km (~14 px at a 108 m pixel), while an Alps ridge of
            // height delta ~0.4 relief units (= 3200 m at 8000 m/unit) casts a
            // ~4.2 km shadow under a 37° sun. Every long ridge shadow was cut
            // flat at 14 px, and the cut line - the iso-distance contour, a
            // straight line along -sun_dir.xy independent of the terrain - is
            // exactly the reported streak. Verified experimentally: rotating
            // the sun 90° rotated the cut ~90°, and raising the tap count from
            // 5 to 20 grew the darkened area 10% -> 14.6%.
            //
            // One scale for everything. compute_smooth_normal differences relief
            // over a 500 m baseline and multiplies by 16, so one relief unit is
            // 500*16 = 8000 m. Use that as the single metre-per-relief-unit so
            // the march ray's rise, the slope bias and the terrain all agree.
            const float RELIEF_M = 8000.0;
            const int   TAPS     = 6;
            const float GROWTH   = 1.25;
            vec2  sun_xy   = normalize(sun_dir.xy);
            float tan_sun  = sun_dir.z / length(sun_dir.xy);
            // Resolution-aware first step (~1.5 px), not a fixed 360 m.
            float step_m   = max(pixel_footprint_m() * 1.5, 60.0);
            float reach_m  = step_m * (pow(GROWTH, float(TAPS)) - 1.0) / (GROWTH - 1.0);
            // Interleaved gradient noise: same cost as hash12 but blue-spectrum,
            // so the residual jitter is far friendlier to a later 3x3 / TAA pass.
            float dither = fract(52.9829189 *
                                 fract(dot(gl_FragCoord.xy,
                                           vec2(0.06711056, 0.00583715))));
            // Terrain slope (m/m) along the shadow direction, recovered from the
            // normalized normal already in hand: grad(height)/m = -normal.xy /
            // normal.z / RELIEF_M, so the rise per metre along -sun_xy is
            // dot(normal.xy, sun_xy) / normal.z / RELIEF_M. We lift the ray
            // origin onto (above) the local tangent plane to avoid self-occlusion
            // acne on slopes just shallower than the sun.
            float slope_along = max(dot(normal.xy, sun_xy) / max(normal.z, 0.05), 0.0);
            float occlusion  = 1.0;
            float dist_m     = step_m * dither;
            float seg        = step_m;
            for (int s = 0; s < TAPS; ++s) {
                dist_m += seg;
                // Conformal Web Mercator UV step preserving metric aspect ratio [40075016.686, 28371606.8]
                vec2 offset = -sun_xy * dist_m * vec2(1.0 / 40075016.686, 1.0 / 28371606.8);
                vec2 tap_uv = map_uv(mapUv + offset);
                float tap_gate = relief_gate(tap_uv);
                float h = 0.0;
                if (tap_gate > 0.001) {
                    float tap_baked = baked_base_height(tap_uv);
                    float tap_detail = sdbg_bits() == 0u ? 0.0 : alpine_mountain_relief(tap_uv, 1, tap_baked);
                    h = clamp(tap_baked + tap_detail * 0.45, 0.0, 1.0) * tap_gate;
                }
                // Ray height: true sun rise + slope bias (kills acne on the
                // first couple of steps) + a small constant lift.
                float ray_h = height + (dist_m * tan_sun +
                                        slope_along * min(dist_m, 2.0 * step_m) +
                                        40.0) / RELIEF_M;
                // Angular penumbra: excess occluder height over the ray, relative
                // to the distance to the occluder. A far occluder bleeds a soft
                // edge; a near one stays sharp. 0.10 is ~a 5.7 deg cone.
                float tap_occ = clamp(1.0 + (ray_h - h) * RELIEF_M / (dist_m * 0.10),
                                      0.0, 1.0);
                // Fade the *contribution* over the last 30% of reach so running
                // out of taps never draws a hard cut at a fixed distance.
                tap_occ = mix(1.0, tap_occ, 1.0 - smoothstep(0.7 * reach_m, reach_m, dist_m));
                occlusion = min(occlusion, tap_occ);
                seg *= GROWTH;
            }
            shadow = mix(1.0, max(occlusion, 0.15), march_weight);
        }
    }

    // Ambient occlusion in steep valleys
    float ao = clamp(1.0 - slope * 0.45, 0.35, 1.0);
    vec3 ambient = ground * sky_color * (perturbed_normal.z * 0.5 + 0.5) * ao;
    vec3 direct_lit = ground * sun_color * (sun_direct * shadow + sun_ambient_wrap * 0.15);

    // Crystalline snow sparkle glints under sunlight
    if (snow_mask > 0.01 && lod_nano > 0.1) {
        vec3 sparkle_half = normalize(sun_dir + vec3(nano_grain - 0.5, micro_crag - 0.5, 1.0));
        float sparkle = pow(max(dot(perturbed_normal, sparkle_half), 0.0), 36.0) * snow_mask * max(NdotL_raw, 0.0);
        direct_lit += vec3(0.85, 0.92, 1.0) * sparkle * 0.45 * lod_nano;
    }

    return direct_lit + ambient;
}

vec3 soft_light(vec3 base, vec3 blend) {
    vec3 d = mix(((16.0 * base - 12.0) * base + 4.0) * base,
                 sqrt(max(base, vec3(0.0))), step(vec3(0.25), base));
    return mix(base - (1.0 - 2.0 * blend) * base * (1.0 - base),
               base + (2.0 * blend - 1.0) * (d - base), step(vec3(0.5), blend));
}

vec3 occupation_ink(vec3 colour, uint id, float land_mask) {
    float phase = (gl_FragCoord.x + gl_FragCoord.y) * 0.70710678 / 16.0 - pad.x * 0.5;
    float wave = sin(phase * 6.28318530718);
    float aa = max(fwidth(wave), 0.001);
    float band = smoothstep(-aa, aa, wave);
    vec3 stripe = mix(srgb_to_linear(vec3(0.12, 0.09, 0.08)),
                      srgb_to_linear(vec3(0.75, 0.12, 0.08)), band);
    float enabled = (political_identity[id].y & 12u) != 0u ? 1.0 : 0.0;
    return mix(colour, stripe, land_mask * enabled * 0.42);
}

vec3 parchment_map(vec2 p, uint id, vec4 political, float land_mask, float coast) {
    float pixel_m = max(length(fwidth(camera_delta) * vec2(40075016.686, 28371606.8)), 1.0);
    float ribbon, state_stroke;
    vec3 pigment;
    float border = sovereign_border_smooth(p, id, political, ribbon, state_stroke, pigment);
    // Solid political ink: no paper texture, terrain illumination or translucent
    // wash modifies a country's interior. Edge coverage remains antialiased.
    vec3 paper = srgb_to_linear(vec3(0.80, 0.84, 0.85));
    vec3 printed_land = srgb_to_linear(political.rgb);
    vec3 colour = mix(paper, printed_land, land_mask);
    float coast_line = (1.0 - smoothstep(0.25, 1.25, abs(coast) * 0.5 / pixel_m)) *
                        (1.0 - smoothstep(28000.0, 31990.0, abs(coast)));
    colour = occupation_ink(colour, id, land_mask);
    colour = mix(colour, srgb_to_linear(vec3(0.25, 0.28, 0.29)),
                 land_mask * state_stroke * 0.32);
    colour = mix(colour, srgb_to_linear(vec3(0.12, 0.15, 0.17)),
                 max(border * land_mask * 0.90, coast_line * 0.80));
    return colour;
}

// Bounded near/middle material: DEM-driven surface with four procedural PBR
// layers. The old multi-octave relief generator remains available to the visual
// debug bank; production does not fabricate extra mountain ranges every pixel.
float surface_noise(float period_m) {
    vec2 scale = vec2(40075016.686, 28371606.8) / period_m;
    vec2 center = view.xy * scale;
    vec2 local = camera_delta * scale + fract(center);
    vec2 cell = floor(center) + floor(local);
    vec2 f = fract(local);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(cell), hash12(cell + vec2(1.0, 0.0)), f.x),
               mix(hash12(cell + vec2(0.0, 1.0)), hash12(cell + vec2(1.0)), f.x), f.y);
}

vec3 surface_map(vec2 p, uint id, vec4 political, float land_mask, float coast) {
    vec2 size_m = vec2(40075016.686, 28371606.8);
    vec2 world_m = p * size_m;
    vec2 texel = 1.0 / (level_pages(v_levels.x) * 64.0);
    float h = sample_height_m(v_levels.x, p);
    float hx = sample_height_m(v_levels.x, p + vec2(texel.x, 0.0));
    float hy = sample_height_m(v_levels.x, p + vec2(0.0, texel.y));
    vec2 gradient = vec2(hx - h, hy - h) / (texel * size_m);
    vec3 normal = normalize(vec3(-gradient, 1.0));
    float near_weight = 1.0 - smoothstep(600.0, 800.0, camera.x);
    float grain = surface_noise(12.0);
    float material_patch = surface_noise(230.0);
    float slope = length(gradient);
    float latitude = degrees(atan(sinh((19971868.88 - world_m.y) / 6378137.0)));
    float arid = exp(-pow((abs(latitude) - 25.0) / 12.0, 2.0)) * (1.0 - smoothstep(800.0, 2500.0, h));
    float rock = smoothstep(0.18, 0.65, slope);
    float snow = smoothstep(2400.0 - abs(latitude) * 12.0, 3600.0 - abs(latitude) * 12.0, h);
    vec4 weights = vec4((1.0 - arid) * (1.0 - rock), arid * (1.0 - rock), rock, snow * 2.0);
    weights /= max(dot(weights, vec4(1.0)), 0.001);
    vec3 albedo = weights.x * srgb_to_linear(mix(vec3(0.26, 0.34, 0.17), vec3(0.43, 0.47, 0.25), material_patch))
                + weights.y * srgb_to_linear(vec3(0.68, 0.56, 0.35))
                + weights.z * srgb_to_linear(vec3(0.44, 0.43, 0.40))
                + weights.w * srgb_to_linear(vec3(0.90, 0.93, 0.94));
    albedo *= 0.94 + 0.12 * grain * near_weight;
    float roughness = dot(weights, vec4(0.90, 0.84, 0.72, 0.55));
    vec3 light = normalize(vec3(-0.6, -0.4, 0.8));
    vec3 half_v = normalize(light + vec3(0.0, 0.0, 1.0));
    float nl = max(dot(normal, light), 0.0);
    float nh = max(dot(normal, half_v), 0.0);
    float a2 = pow(roughness, 4.0);
    float distribution = a2 / max(3.14159265 * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0), 0.001);
    vec3 terrain = albedo * (0.35 + nl * 0.9) + vec3(distribution * 0.025 * nl);
    terrain = mix(terrain, soft_light(terrain, srgb_to_linear(political.rgb)),
                  smoothstep(600.0, 800.0, camera.x) * 0.65);

    vec3 water = srgb_to_linear(vec3(0.14, 0.31, 0.39));
    if (camera.x < 800.0) {
        // Four Gerstner components; analytic slopes avoid iterative ray tracing.
        // This branch is uniform and fully disabled at middle/far distance.
        vec2 wave_slope = vec2(0.0);
        float crest = 0.0;
        for (int i = 0; i < 4; ++i) {
            float angle = float(i) * 1.83;
            vec2 direction = vec2(cos(angle), sin(angle));
            float k = 6.2831853 / (14.0 + float(i) * 11.0);
            float phase = mod(dot(view.xy * size_m, direction) * k, 6.2831853)
                          + dot(camera_delta * size_m, direction) * k - pad.x * sqrt(9.81 * k);
            wave_slope += direction * cos(phase) * 0.08;
            crest += sin(phase) * 0.25;
        }
        vec3 wn = normalize(vec3(-wave_slope, 1.0));
        float fresnel = 0.02 + 0.98 * pow(1.0 - wn.z, 5.0);
        vec3 transmission = exp(-vec3(0.11, 0.045, 0.025) * max(-h, 0.0));
        vec3 shallow = vec3(0.035, 0.23, 0.20) * transmission + vec3(0.01, 0.035, 0.07);
        vec3 ocean = mix(shallow, vec3(0.42, 0.60, 0.77), fresnel);
        ocean += pow(max(dot(wn, half_v), 0.0), 96.0) * 0.6;
        float foam = (1.0 - smoothstep(1.0, 35.0, abs(coast) * 0.5)) * smoothstep(0.1, 0.8, crest);
        ocean = mix(ocean, vec3(0.88, 0.95, 0.96), foam);
        water = mix(water, ocean, near_weight);
    }
    float ribbon, state_stroke;
    vec3 pigment;
    float border = sovereign_border_smooth(p, id, political, ribbon, state_stroke, pigment);
    vec3 colour = occupation_ink(mix(water, terrain, land_mask), id, land_mask);
    colour = mix(colour, srgb_to_linear(vec3(0.96, 0.88, 0.71)),
                  land_mask * max(state_stroke * 0.30, ribbon * 0.08));
    return mix(colour, srgb_to_linear(vec3(0.11, 0.09, 0.07)), border * land_mask * 0.85);
}

void main() {
    vec2 mapUv = map_uv(uv);
    uint centre_id = sample_province(v_levels.x, mapUv);
    vec4 political = sdbg_political_off() ? vec4(0.55, 0.52, 0.47, 1.0)
                                          : political_colour(centre_id);
    // Continuous smooth coast signed distance field (pack units, 1 = 0.5 m)
    float coast_sdf = patch_coast_raw(mapUv);
    float coast_fw = max(fwidth(coast_sdf) * 1.5, 1.0);
    float land_sdf_mask = smoothstep(-coast_fw, coast_fw, coast_sdf);
    float raw_land = (centre_id != 0u && political.a >= 0.5) ? 1.0 : 0.0;
    float land_coast = sdbg_coast_off() ? raw_land : land_sdf_mask;
    // Water and lake discrimination: ocean has centre_id == 0 or blue == 0, lakes have political.a == 0 && political.b > 0.5
    bool is_water = (centre_id == 0u || political.a < 0.5);
    bool is_lake = (political.a < 0.5 && political.b > 0.5);
    float land = is_water ? 0.0 : land_coast;
    // Current visual reference: opaque country pigment, engraved warm sea,
    // dark feathered frontiers and DEM hill shading. Keep the detailed near
    // material below; only distant views bypass its physical iterations.
    float print_weight = smoothstep(2400.0, 3000.0, camera.x);
    if (camera.x >= 3000.0 && sdbg_bits() == 0u) {
        outColor = vec4(max(apply_saturation(parchment_map(mapUv, centre_id, political, land, coast_sdf),
                                            SATURATION), vec3(0.0)), 1.0);
        return;
    }

    // Stable geographic coordinates for continuous procedural variation
    vec2 world = (mapUv - vec2(0.5)) * vec2(34.0, 18.0);
    // =========================================================================
    // 1. Far 2D View: Saturated Flat Political Map (no paper substrate)
    // =========================================================================
    // The parchment base, aging stains, cellulose fibers, fold creases and
    // archival vignette were removed with the paper-map art direction. Land
    // pigment is applied directly in section 4; the ocean is a flat saturated
    // blue wash in section 2.

    // =========================================================================
    // 2. Ocean Shading: Dual-Layer (Pixel-Crisp Waterlines vs Near PBR Waves)
    // =========================================================================
    // Nautical-chart coastal waterlines (subtle):
    // coast_px is the exact distance offshore in screen pixels!
    float coast_px = -coast_sdf / max(fwidth(coast_sdf), 1.0);
    float w1 = 1.0 - smoothstep(0.4, 1.8, abs(coast_px - 4.0));
    float w2 = 1.0 - smoothstep(0.4, 1.8, abs(coast_px - 9.0));
    float w3 = 1.0 - smoothstep(0.4, 1.8, abs(coast_px - 16.0));
    float w4 = 1.0 - smoothstep(0.4, 2.0, abs(coast_px - 26.0));
    float w5 = 1.0 - smoothstep(0.4, 2.2, abs(coast_px - 40.0));
    float w6 = 1.0 - smoothstep(0.4, 2.5, abs(coast_px - 58.0));
    float waterline_engraving = max(max(w1, w2 * 0.85), max(w3 * 0.70, max(w4 * 0.55, max(w5 * 0.40, w6 * 0.28))));

    // Flat political-map ocean, V3-measured model: near-white warm-gray paper
    // sea when zoomed out (#e0e0d8..#f8f8f0), deepening to nautical blue as
    // the camera descends (mid-zoom Atlantic sampled #285070).
    float coast_proximity = clamp(coast_px / 45.0, 0.0, 1.0);
    vec3 c_coastal_water    = srgb_to_linear(vec3(0.83, 0.85, 0.83));
    vec3 c_deep_water       = srgb_to_linear(vec3(0.72, 0.76, 0.75));
    vec3 c_coastal_nautical = srgb_to_linear(vec3(0.40, 0.61, 0.76));
    vec3 c_deep_nautical    = srgb_to_linear(vec3(0.18, 0.35, 0.52));
    vec3 pale_sea = mix(c_coastal_water, c_deep_water, coast_proximity);
    vec3 nautical = mix(c_coastal_nautical, c_deep_nautical, coast_proximity);
    vec3 far_ocean = mix(pale_sea, nautical, smoothstep(0.15, 0.80, closeFactor));
    // Gentle sea mottle so the pale sea never reads as a flat sticker
    far_ocean *= 1.0 + (value_noise(world * 26.0 + vec2(3.0, 19.0)) - 0.5) * 0.035;
    // Faint nautical-chart waterlines: near-invisible zoomed out, clearer near
    vec3 c_waterline_ink = srgb_to_linear(vec3(0.10, 0.21, 0.32));
    far_ocean = mix(far_ocean, c_waterline_ink, waterline_engraving * mix(0.08, 0.45, closeFactor));

    // Near 3D Ocean: Multi-octave wave dynamics, Beer-Lambert depth absorption, Fresnel reflection
    // Use periodic longitude coordinates so waves wrap seamlessly across the antimeridian
    float wave_lon_rad = mapUv.x * 6.28318530718;
    vec2 wave_cyl = vec2(cos(wave_lon_rad), sin(wave_lon_rad)) * 24.0;
    vec2 wave_p = vec2(wave_cyl.x * 1.8 + wave_cyl.y * 1.2, world.y * 4.0);
    float wave_a = sin(dot(wave_p, vec2(0.92, 0.38)) * 12.0) * 0.5 + 0.5;
    float wave_b = sin(dot(wave_p, vec2(-0.41, 0.91)) * 19.0) * 0.5 + 0.5;
    float wave_fine = sin(dot(wave_p, vec2(0.75, -0.66)) * 38.0) * 0.5 + 0.5;
    float wave_ripple = sin(dot(wave_p, vec2(0.28, 0.96)) * 82.0) * 0.5 + 0.5;
    vec3 wave_normal = normalize(vec3(
        (wave_a - wave_b) * 0.32 + (wave_fine - wave_ripple) * 0.12,
        (wave_b - wave_fine) * 0.32 + (wave_a - wave_ripple) * 0.12,
        1.0
    ));
    vec3 sun_dir = normalize(vec3(-0.65, -0.45, 0.60));

    // Depth-based Beer-Lambert water color extinction
    float water_depth = clamp(-coast_sdf / 60.0, 0.0, 1.0);
    vec3 water_shallow = vec3(0.06, 0.32, 0.38); // Emerald/turquoise lagoon
    vec3 water_shelf   = vec3(0.03, 0.16, 0.26); // Continental shelf teal
    vec3 water_deep    = vec3(0.015, 0.055, 0.13); // Oceanic sapphire abyss
    vec3 water_extinction = mix(water_shallow, water_shelf, smoothstep(0.0, 0.35, water_depth));
    water_extinction = mix(water_extinction, water_deep, smoothstep(0.35, 1.0, water_depth));

    // Schlick's Fresnel reflection
    float VdotN = clamp(dot(vec3(0.0, 0.0, 1.0), wave_normal), 0.0, 1.0);
    float fresnel = 0.03 + 0.97 * pow(1.0 - VdotN, 5.0);
    vec3 sky_reflection = vec3(0.42, 0.58, 0.76) * (0.85 + 0.15 * dot(wave_normal, sun_dir));
    vec3 near_ocean = mix(water_extinction, sky_reflection, fresnel * 0.75);

    // Shoreline coastal surf & foam wavelets
    float shore_dist = abs(coast_sdf) * 0.05;
    float foam_wave = sin(shore_dist * 4.5 - (wave_a + wave_b) * 3.2) * 0.5 + 0.5;
    float foam_mask = (1.0 - smoothstep(0.0, 2.5, shore_dist)) * smoothstep(0.32, 0.82, foam_wave + wave_fine * 0.28);
    vec3 foam_color = vec3(0.95, 0.97, 0.99);
    near_ocean = mix(near_ocean, foam_color, foam_mask * 0.88 * closeFactor);

    // Sun specular sheen on water (painterly V3/CK3 style warm glint)
    vec3 half_v = normalize(sun_dir + vec3(0.0, 0.0, 1.0));
    float ocean_spec = pow(max(dot(wave_normal, half_v), 0.0), 42.0) * 0.55;
    near_ocean += vec3(1.10, 1.02, 0.90) * ocean_spec * (fresnel * 0.75 + 0.12);

    vec3 near_water = near_ocean;
    if (is_lake) {
        // Freshwater lakes: luminous sapphire / azure wash with subtle concentric waterlines
        vec3 c_lake_water = srgb_to_linear(vec3(0.36, 0.58, 0.70));
        float lake_ripple = sin((world.x * 4.0 + world.y * 4.0) * 5.0) * 0.5 + 0.5;
        far_ocean = mix(far_ocean, c_lake_water, 0.82);
        far_ocean += (lake_ripple - 0.5) * 0.015;
        vec3 lake_shelf = srgb_to_linear(vec3(0.06, 0.24, 0.32));
        near_water = mix(lake_shelf, sky_reflection, fresnel * 0.65);
    }

    vec3 final_water = mix(far_ocean, near_water, smoothstep(0.18, 0.82, closeFactor));

    // Land drop-shadow onto the sea (V3 paper-cutout depth cue): a soft gray
    // gradient band in the water hugging the coastline, far view only.
    float sea_coast_shadow = (1.0 - smoothstep(1.5, 10.0, coast_px)) * (1.0 - land);
    final_water *= 1.0 - sea_coast_shadow * 0.10 * (1.0 - smoothstep(0.15, 0.80, closeFactor));

    // =========================================================================
    // 3. Terrain Geomorphology & 3D PBR Multi-Material Splatting
    // =========================================================================
    vec2 world_pos_m = vec2((mapUv.x - 0.5) * 40075016.686, 19971868.9 - mapUv.y * 28371606.8);
    // ---- TEMP DIAGNOSTIC BITS (remove after the near-view wash-out is fixed) ----
    // 12582912 (= 8388608|4194304): raw interpolated mapUv and the view centre.
    //          R = mapUv.x, G = mapUv.y, B = view.x. If mapUv does not land on
    //          the camera longitude/latitude, the fragment's world position is
    //          wrong and every derived field (mountain belt, moisture, relief)
    //          is sampled somewhere else on the globe.
    if ((sdbg_bits() & 12582912u) == 12582912u) {
        outColor = vec4(mapUv, view.x, 1.0);
        return;
    }
    // 32768: the three relief quantities on one identical post path, so their
    //        display values are directly comparable.
    //        R = ungated relief, G = relief_gate, B = gated relief (== bit 8).
    if ((sdbg_bits() & 32768u) != 0u) {
        float ung = alpine_mountain_relief(mapUv, relief_octaves(), baked_base_height(mapUv));
        float gat = relief_gate(mapUv);
        outColor = vec4(ung, gat, ung * gat, 1.0);
        return;
    }
    // 65536: relief finite difference across three eps scales at once, gain 64
    //        around neutral 0.5. R = 250 m, G = 2.5 km, B = 25 km. If the small
    //        step is flat while the large step has structure, the high
    //        frequencies are being lost rather than the field being constant.
    if ((sdbg_bits() & 65536u) != 0u) {
        float d1 = continuous_relief(mapUv + vec2(250.0 / 40075016.0, 0.0))
                 - continuous_relief(mapUv - vec2(250.0 / 40075016.0, 0.0));
        float d2 = continuous_relief(mapUv + vec2(2500.0 / 40075016.0, 0.0))
                 - continuous_relief(mapUv - vec2(2500.0 / 40075016.0, 0.0));
        float d3 = continuous_relief(mapUv + vec2(25000.0 / 40075016.0, 0.0))
                 - continuous_relief(mapUv - vec2(25000.0 / 40075016.0, 0.0));
        outColor = vec4(vec3(0.5) + vec3(d1, d2, d3) * 64.0, 1.0);
        return;
    }
    // 131072: same three eps scales but on the UNGATED relief, to separate
    //         "the fbm has no high frequencies" from "the gate kills them".
    if ((sdbg_bits() & 131072u) != 0u) {
        int oc = relief_octaves();
        vec2 e1 = vec2(250.0 / 40075016.0, 0.0);
        vec2 e2 = vec2(2500.0 / 40075016.0, 0.0);
        vec2 e3 = vec2(25000.0 / 40075016.0, 0.0);
        float d1 = alpine_mountain_relief(mapUv + e1, oc, baked_base_height(mapUv + e1))
                 - alpine_mountain_relief(mapUv - e1, oc, baked_base_height(mapUv - e1));
        float d2 = alpine_mountain_relief(mapUv + e2, oc, baked_base_height(mapUv + e2))
                 - alpine_mountain_relief(mapUv - e2, oc, baked_base_height(mapUv - e2));
        float d3 = alpine_mountain_relief(mapUv + e3, oc, baked_base_height(mapUv + e3))
                 - alpine_mountain_relief(mapUv - e3, oc, baked_base_height(mapUv - e3));
        outColor = vec4(vec3(0.5) + vec3(d1, d2, d3) * 64.0, 1.0);
        return;
    }
    // 524288: normal view recomputed at eps = 2.5 km (10x the production step).
    // 1048576: normal view at eps = 25 km. If either shows structure while the
    //          production bit 4096 is flat, the finite-difference step is
    //          undershooting the terrain's finest period.
    if (((sdbg_bits() & 524288u) != 0u) || ((sdbg_bits() & 1048576u) != 0u)) {
        float step_m = ((sdbg_bits() & 524288u) != 0u) ? 2500.0 : 25000.0;
        vec2 e = vec2(step_m / 40075016.0, 0.0);
        float hL = continuous_relief(mapUv - e.xy);
        float hR = continuous_relief(mapUv + e.xy);
        float hD = continuous_relief(mapUv - e.yx);
        float hU = continuous_relief(mapUv + e.yx);
        vec3 n = normalize(vec3(-(hR - hL) * 16.0, -(hU - hD) * 16.0, 1.0));
        outColor = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }
    // 2097152: scalar readout. R = relief_octaves()/8, G = pixel_footprint/20000.
    if ((sdbg_bits() & 2097152u) != 0u && (sdbg_bits() & 10485760u) != 10485760u) {
        outColor = vec4(float(relief_octaves()) / 8.0, pixel_footprint_m() / 20000.0, 0.0, 1.0);
        return;
    }
    // ---- END TEMP DIAGNOSTIC BITS ----
    vec3 terrain_normal = compute_smooth_normal(mapUv);    float continuous_elevation = continuous_relief(mapUv);
    float slope = 1.0 - terrain_normal.z;
    // Debug bit 4096: normal-view pass-through. Early-outs before any material
    // work so the frame shows the raw relief normal with no shading, masks or
    // march on top. Used to test whether the reported hairlines sit on relief
    // creases: strong |grad(normal)| ridges in this frame that overlap the
    // lines in the production frame locate the creases directly, independent
    // of the sun and of the shadow march.
    if ((sdbg_bits() & 4096u) != 0u) {
        outColor = vec4(terrain_normal * 0.5 + 0.5, 1.0);
        return;
    }
    // Earth-like latitudinal climate belts & continental rain shadow
    float lat_deg = latitude_degrees(mapUv);
    float lat_fraction = abs(lat_deg) / 85.0; // 0 at equator, 1 at poles
    // Subtropical desert belt around lat 18°-32°
    float subtropical_arid = 1.0 - smoothstep(0.04, 0.12, abs(lat_fraction - 0.28));
    float continental_dry = smoothstep(50000.0, 400000.0, max(coast_sdf, 0.0));
    float climate_base = mix(0.70, 0.10, subtropical_arid * (0.60 + continental_dry * 0.40));
    float moisture = clamp(climate_base + 0.22 * fbm(world * 0.45 + vec2(11.0, -17.0)), 0.04, 0.95);

    float view_blend = smoothstep(0.18, 0.75, closeFactor);
    float mat_roughness = 0.85;
    vec3 pbr_terrain = vec3(0.0);
    if (view_blend > 0.001 || (sdbg_bits() & 4194304u) != 0u) {
        pbr_terrain = calculate_pbr_terrain_splat(mapUv, world_pos_m, continuous_elevation, slope, moisture, coast_sdf, centre_id, terrain_normal, sun_dir, mat_roughness);
    }
    // 4194304: raw pbr_terrain before the far/near view_blend mix, plus the
    //          three scalars that gate every material. If this is green the
    //          splat works and the fault is upstream in the mix; if it is the
    //          same warm paper tone, the splat itself is degraded.
    if ((sdbg_bits() & 4194304u) != 0u) {
        outColor = vec4(pbr_terrain, vec3(land, slope * 8.0, continuous_elevation));
        return;
    }
    // 262144: page tint. Each resident page gets a pseudo-random colour so
    //          page boundaries read as hard colour steps - directly flags
    //          whether a screen column sits on a page seam.
    if ((sdbg_bits() & 262144u) != 0u) {
        vec2 px = level_pages(v_levels.x) * 128.0;
        ivec2 t = ivec2(floor(mapUv * px));
        ivec2 page = t / 128;
        float idx = float(page.y * 64 + page.x);
        outColor = vec4(vec3(fract(idx * 0.618033988749895)), 1.0);
        return;
    }
    // 10485760 (= 8388608|2097152): baked DEM pass-through.
    //          R = sourceRelief (baked metres / 22000), G = land gate.
    //          Shows what the world pack's real global height data looks like.
    if ((sdbg_bits() & 10485760u) == 10485760u) {
        float metres = patch_height_m(mapUv);
        outColor = vec4(clamp(max(metres, 0.0) / 22000.0, 0.0, 1.0), relief_gate(mapUv), 0.0, 1.0);
        return;
    }
    // 33554432: mapUv rate measurement.
    //          R = dFdx(mapUv.x)*1000, G = dFdy(mapUv.y)*1000.
    if ((sdbg_bits() & 33554432u) != 0u) {
        outColor = vec4(dFdx(mapUv.x) * 1000.0, dFdy(mapUv.y) * 1000.0, 0.0, 1.0);
        return;
    }
    // 8388608: scalar readout for the wash-out root cause.
    //          R = mountain_belt (1 = inside an orogenic belt, 0 = plains),
    //          G = coast_sdf / 20000 m (0 = shoreline, 1 = inland >= 20 km),
    //          B = moisture.
    if ((sdbg_bits() & 8388608u) != 0u) {
        float lon_d = (mapUv.x - 0.5) * 360.0;
        float lat_d = latitude_degrees(mapUv);
        float belt = 0.0;
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d - 10.5) / 5.5, (lat_d - 46.6) / 1.5))));
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d - 0.8) / 2.8, (lat_d - 42.6) / 0.9))));
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d - 22.0) / 4.5, (lat_d - 47.5) / 2.0))));
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d - 12.0) / 4.5, (lat_d - 63.5) / 4.5))));
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d - 43.0) / 5.0, (lat_d - 42.5) / 1.8))));
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d - 87.0) / 13.0, (lat_d - 32.0) / 5.0))));
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d + 114.0) / 6.5, (lat_d - 45.0) / 14.0))));
        belt = max(belt, 1.0 - smoothstep(0.35, 1.0, length(vec2((lon_d + 70.0) / 4.0, (lat_d + 22.0) / 32.0))));
        outColor = vec4(belt, clamp(coast_sdf / 20000.0, 0.0, 1.0), moisture, 1.0);
        return;
    }

    // =========================================================================
    // 4. Political Shading (Saturated Flat Political Map, Victoria 3 Flatmap)
    // =========================================================================
    float inner_ribbon = 0.0;
    float prov_stroke = 0.0;
    vec3 pigment = srgb_to_linear(political.rgb);
    float country_stroke = sovereign_border_smooth(mapUv, centre_id, political, inner_ribbon, prov_stroke, pigment);

    // Far 2D View: official country pigment laid on flat, no paper substrate.
    // Country colors stay saturated and readable at strategic zoom.
    vec3 country_lin = pigment;

    // Check if region belongs to decentralized tribes / unorganized territory (light pale ivory-white: ~248, 246, 238)
    bool is_decentralized = (political.r > 0.94 && political.g > 0.93 && political.b > 0.90);
    if (is_decentralized) {
        inner_ribbon = 0.0;
    }
    
    // Flat political fill: direct pigment (decentralized land gets a neutral
    // uncolonized gray, not derived from any paper tone).
    vec3 far_land = country_lin;
    if (is_decentralized) {
        far_land = srgb_to_linear(vec3(0.78, 0.76, 0.72));
    }

    // Border shadow band (V3-measured subtlety: ~8-12% darkening at the seam)
    vec3 ribbon_color = country_lin * 0.80;
    far_land = mix(far_land, ribbon_color, inner_ribbon * 0.55);

    // V3 hand-painted gradients (pixel-verified on official screenshots):
    // a broad tonal drift across the continent, a small per-country lightness
    // bias, and a fine canvas grain - together they replace flat sticker fills.
    float tonal_drift  = (value_noise(world * 2.2 + vec2(31.0, 17.0)) - 0.5) * 0.10;
    float country_bias = (fract(float(centre_id) * 0.6180339887) - 0.5) * 0.06;
    float canvas_grain = (value_noise(world * 150.0 + vec2(11.0, -7.0)) - 0.5) * 0.05;
    far_land *= 1.0 + tonal_drift + country_bias + canvas_grain;

    // Global geographic desert aridity wash (Sahara, Arabia, Gobi, Taklamakan, Outback, Kalahari, Mojave, Atacama)
    // Warm natural sand wash without repetitive texture tiling
    float lon_deg = (mapUv.x - 0.5) * 360.0;
    float lat_abs = abs(lat_deg);
    float sahara_arabia = (1.0 - smoothstep(0.04, 0.13, abs(lat_abs / 85.0 - 0.28))) *
                          (smoothstep(-18.0, -8.0, lon_deg) * (1.0 - smoothstep(62.0, 72.0, lon_deg)));
    float asian_inland = (1.0 - smoothstep(0.06, 0.16, abs(lat_deg - 41.0) / 85.0)) *
                         (smoothstep(55.0, 70.0, lon_deg) * (1.0 - smoothstep(112.0, 120.0, lon_deg)));
    float outback_arid = (1.0 - smoothstep(0.04, 0.12, abs(lat_deg - (-25.0)) / 85.0)) *
                         (smoothstep(116.0, 122.0, lon_deg) * (1.0 - smoothstep(142.0, 148.0, lon_deg)));
    float kalahari_arid = (1.0 - smoothstep(0.03, 0.10, abs(lat_deg - (-24.0)) / 85.0)) *
                          (smoothstep(12.0, 17.0, lon_deg) * (1.0 - smoothstep(28.0, 33.0, lon_deg)));
    float na_arid = (1.0 - smoothstep(0.04, 0.11, abs(lat_deg - 32.0) / 85.0)) *
                    (smoothstep(-120.0, -114.0, lon_deg) * (1.0 - smoothstep(-100.0, -96.0, lon_deg)));
    float atacama_arid = (1.0 - smoothstep(0.03, 0.08, abs(lat_deg - (-22.0)) / 85.0)) *
                         (smoothstep(-72.0, -70.0, lon_deg) * (1.0 - smoothstep(-67.0, -65.0, lon_deg)));

    float geographic_desert = max(max(max(sahara_arabia, asian_inland), outback_arid),
                                  max(max(kalahari_arid, na_arid), atacama_arid));
    float desert_factor = max(geographic_desert, (1.0 - smoothstep(0.10, 0.28, moisture)) * 0.75);
    desert_factor *= (1.0 - smoothstep(0.35, 0.65, continuous_elevation)); // Alpine heights are rock/snow, not sand

    // Warm natural sand ochre tone & broad painterly sand sea swells (wavelength 35km - 70km, non-repetitive)
    vec3 c_desert_sand = srgb_to_linear(vec3(0.92, 0.77, 0.46));
    vec3 c_desert_shadow = srgb_to_linear(vec3(0.80, 0.62, 0.35));
    vec2 p_sand = world_pos_m * (1.0 / 42000.0);
    float macro_sand = gradient_noise01(p_sand) * 0.6 + gradient_noise01(p_sand * 1.73 + vec2(11.3, -7.5)) * 0.4;
    vec3 c_desert_body = mix(c_desert_sand, c_desert_shadow, macro_sand * 0.5);

    if (is_decentralized) {
        // Decentralized desert regions (Sahara, Rub' al Khali, Outback) display golden sand wash
        vec3 desert_wash = c_desert_body * (0.92 + macro_sand * 0.16);
        far_land = mix(far_land, desert_wash, desert_factor * 0.92);
    } else {
        // Sovereign nations in deserts (e.g. Qing in Gobi/Xinjiang, Egypt in Sahara):
        // light terrain hint only - political pigment must dominate on a flat map.
        vec3 glazed_desert = mix(far_land, c_desert_body * 1.12, 0.45);
        glazed_desert *= (0.90 + macro_sand * 0.18);
        far_land = mix(far_land, glazed_desert, desert_factor * 0.35);
    }

    // Victoria 3 / CK3 style cartographic hillshading on political map:
    // subtle terrain elevation shows through political pigments with mountain crest highlights
    // and gentle ambient valley shading, ensuring tactical terrain readability without obscuring political borders.
    float hill_shade = dot(terrain_normal, sun_dir) * 0.5 + 0.5;
    float mountain_prominence = smoothstep(0.10, 0.55, continuous_elevation);
    // Tactile embossed chiaroscuro: crest highlights catching sunlight + gentle valley depth
    float crest_highlight = pow(max(dot(terrain_normal, normalize(sun_dir + vec3(0.0, 0.0, 0.4))), 0.0), 4.0) * mountain_prominence;
    vec3 hill_tint = mix(vec3(0.92 + hill_shade * 0.16),
                         vec3(0.76 + hill_shade * 0.40) + vec3(0.12, 0.10, 0.07) * crest_highlight,
                         mountain_prominence * 0.75);
    far_land *= hill_tint;

    // Near 3D View: Photoreal Physical Terrain with Victoria 3 Cartographic Borders
    // In Victoria 3 close-up view, terrain is photorealistic PBR landscape (snow, granite cliffs, lush pastures)
    // with subtle territorial pigment wash and frontier inner ribbons to preserve geopolitical identity.
    vec3 territory_wash = country_lin * 1.25 + 0.10;
    float mid_weight = smoothstep(600.0, 800.0, camera.x);
    vec3 near_land = mix(pbr_terrain, soft_light(pbr_terrain, country_lin),
                         mid_weight * 0.70 + inner_ribbon * 0.15);

    // Transition curve: smooth non-linear crossfade around closeFactor
    vec3 land_colour = mix(far_land, near_land, view_blend);

    // =========================================================================
    // 5. Final Composition (Land vs Water)
    // =========================================================================
    vec3 map_colour = mix(final_water, land_colour, land);
    if (sdbg_height_only()) map_colour = vec3(clamp(continuous_elevation, 0.0, 1.0));

    // =========================================================================
    // Mercator Conformal Cartographic Graticule (Victoria 3 Navigational Ruler)
    // =========================================================================
    float lon_deg_g = (mapUv.x - 0.5) * 360.0;
    float lat_deg_g = latitude_degrees(mapUv);
    float d_lon = max(fwidth(lon_deg_g), 1e-4);
    float d_lat = max(fwidth(lat_deg_g), 1e-4);

    // Meridians every 15 degrees
    float mer_step = 15.0;
    float mer_dist_deg = abs(fract(lon_deg_g / mer_step + 0.5) - 0.5) * mer_step;
    float mer_dist_px = mer_dist_deg / d_lon;
    float mer_line = 1.0 - smoothstep(0.0, 1.1, mer_dist_px);

    // Parallels every 15 degrees (conformal Mercator spacing: 0°, 15°, 30°, 45°, 60°, 75°)
    float par_step = 15.0;
    float par_dist_deg = abs(fract(lat_deg_g / par_step + 0.5) - 0.5) * par_step;
    float par_dist_px = par_dist_deg / d_lat;
    float par_line = 1.0 - smoothstep(0.0, 1.1, par_dist_px);

    // Victoria 3 Equator line (prominent baseline with anti-aliased hierarchical graduation ruler)
    float eq_dist_px = abs(lat_deg_g) / d_lat;
    float eq_line = 1.0 - smoothstep(0.0, 1.3, eq_dist_px);

    // Hierarchical graduation ticks along the equator (10° major, 5° medium, 1° minor)
    // 10° major ticks: length 5.5px, sharp and visible across broad zoom
    float tick10_deg = abs(fract(lon_deg_g / 10.0 + 0.5) - 0.5) * 10.0;
    float tick10 = (1.0 - smoothstep(0.0, 1.0, tick10_deg / d_lon)) * (1.0 - smoothstep(4.5, 5.5, eq_dist_px));

    // 5° medium ticks: length 4.0px, fading at far zoom to eliminate sub-pixel clustering
    float tick5_deg = abs(fract(lon_deg_g / 5.0 + 0.5) - 0.5) * 5.0;
    float tick5_fade = 1.0 - smoothstep(0.35, 0.70, d_lon);
    float tick5 = (1.0 - smoothstep(0.0, 1.0, tick5_deg / d_lon)) * (1.0 - smoothstep(3.2, 4.0, eq_dist_px)) * tick5_fade;

    // 1° minor ticks: length 2.5px, smoothly fading out when zoomed out (fwidth > 0.12) to prevent moiré/aliasing
    float tick1_deg = abs(fract(lon_deg_g / 1.0 + 0.5) - 0.5) * 1.0;
    float tick1_fade = 1.0 - smoothstep(0.06, 0.16, d_lon);
    float tick1 = (1.0 - smoothstep(0.0, 1.0, tick1_deg / d_lon)) * (1.0 - smoothstep(1.8, 2.5, eq_dist_px)) * tick1_fade;

    float eq_graduation = max(max(tick10 * 1.25, tick5 * 1.05), tick1 * 0.85);
    float equator_total = max(eq_line * 1.4, eq_graduation);

    // Greenwich Prime Meridian (0° Longitude, Royal Observatory reference standard)
    float pm_dist_px = abs(lon_deg_g) / d_lon;
    float pm_line = 1.0 - smoothstep(0.0, 1.3, pm_dist_px);

    // Hierarchical latitude graduation ticks along the Prime Meridian (10° major, 5° medium, 1° minor)
    float pm_tick10_deg = abs(fract(lat_deg_g / 10.0 + 0.5) - 0.5) * 10.0;
    float pm_tick10 = (1.0 - smoothstep(0.0, 1.0, pm_tick10_deg / d_lat)) * (1.0 - smoothstep(4.5, 5.5, pm_dist_px));

    float pm_tick5_deg = abs(fract(lat_deg_g / 5.0 + 0.5) - 0.5) * 5.0;
    float pm_tick5_fade = 1.0 - smoothstep(0.35, 0.70, d_lat);
    float pm_tick5 = (1.0 - smoothstep(0.0, 1.0, pm_tick5_deg / d_lat)) * (1.0 - smoothstep(3.2, 4.0, pm_dist_px)) * pm_tick5_fade;

    float pm_tick1_deg = abs(fract(lat_deg_g / 1.0 + 0.5) - 0.5) * 1.0;
    float pm_tick1_fade = 1.0 - smoothstep(0.06, 0.16, d_lat);
    float pm_tick1 = (1.0 - smoothstep(0.0, 1.0, pm_tick1_deg / d_lat)) * (1.0 - smoothstep(1.8, 2.5, pm_dist_px)) * pm_tick1_fade;

    float pm_graduation = max(max(pm_tick10 * 1.25, pm_tick5 * 1.05), pm_tick1 * 0.85);
    float prime_meridian_total = max(pm_line * 1.4, pm_graduation);

    // Astronomical parallels:
    // Tropics of Cancer (+23.4365°) and Capricorn (-23.4365°), plus Arctic Circle (+66.5635°)
    float tropic_dist_px = min(abs(lat_deg_g - 23.4365), abs(lat_deg_g - (-23.4365))) / d_lat;
    float arctic_dist_px = abs(lat_deg_g - 66.5635) / d_lat;

    // Conformal anti-aliased nautical dashes (period 3° lon) with smooth transition to prevent twinkling at distance
    float dash_period = 3.0;
    float dash_phase = fract(lon_deg_g / dash_period);
    float dash_fw = d_lon / dash_period;
    float dash_mask = smoothstep(0.30 - dash_fw, 0.30 + dash_fw, dash_phase) *
                      (1.0 - smoothstep(0.70 - dash_fw, 0.70 + dash_fw, dash_phase));
    // When zoomed far out (dash_fw > 0.35), blend to solid subtle line to avoid sub-pixel flickering
    dash_mask = mix(dash_mask, 0.45, smoothstep(0.20, 0.40, dash_fw));

    float tropic_line = (1.0 - smoothstep(0.0, 1.1, tropic_dist_px)) * dash_mask;
    float arctic_line = (1.0 - smoothstep(0.0, 1.1, arctic_dist_px)) * dash_mask * 0.85;

    float grat_line = max(max(max(mer_line, par_line), max(equator_total, prime_meridian_total)), max(tropic_line * 0.90, arctic_line));

    // Soften graticule at the wrap seam so it never leaves an aliased vertical artifact
    float wrap_seam_mask = 1.0 - smoothstep(0.0, 0.005, min(mapUv.x, 1.0 - mapUv.x));
    grat_line *= (1.0 - wrap_seam_mask);
    if (!sdbg_grat_off())
        map_colour = mix(map_colour, srgb_to_linear(vec3(0.42, 0.47, 0.54)), grat_line * 0.14 * (1.0 - view_blend));

    // Atmospheric drifting cloud shadows in 3D near view
    float cloud_field = fbm(world * 0.075 + vec2(15.0, -22.0));
    float cloud_shadow = smoothstep(0.48, 0.80, cloud_field) * 0.22 * closeFactor;
    map_colour *= 1.0 - cloud_shadow;

    // =========================================================================
    // 6. Sovereign & Provincial Borders (Continuous Anti-Aliased Sub-pixel Lines)
    // =========================================================================
    // V3-style thin white internal state borders (pixel-verified: hairline,
    // translucent, everywhere on land; fade out as the camera descends into 3D)
    float state_line = prov_stroke * land * (1.0 - country_stroke);
    map_colour = mix(map_colour, srgb_to_linear(vec3(0.97, 0.97, 0.94)),
                     state_line * 0.38 * (1.0 - view_blend * 0.85));

    // Crisp engraved coastline (V3 flatmap detail): a dark ink seam at the
    // land/sea boundary, biased slightly into the water. The sea side already
    // carries the paper-cutout drop shadow; this inks the seam itself. Fades
    // toward the near view, where foam and PBR surf take over.
    float coast_ink = sdbg_coast_off() ? 0.0f
        : 1.0 - smoothstep(0.35, 1.45, abs(coast_px + 0.35));
    map_colour = mix(map_colour, srgb_to_linear(vec3(0.15, 0.13, 0.11)),
                     coast_ink * 0.42 * (1.0 - view_blend * 0.7));

    // Anti-aliased engraved ink border line, V3 hairline weight (~2 px)
    map_colour = occupation_ink(map_colour, centre_id, land);
    map_colour = mix(map_colour, srgb_to_linear(vec3(0.95, 0.84, 0.64)),
                     inner_ribbon * land * 0.12);
    map_colour = mix(map_colour, srgb_to_linear(vec3(0.13, 0.12, 0.11)), country_stroke * land * 0.80);

    // Coastline depth absorption & wet sand darkening
    float coast_width = max(fwidth(coast_sdf) * 2.0, 1.5);
    float coast_edge = sdbg_coast_off()
        ? 0.0
        : 1.0 - smoothstep(0.0, coast_width, abs(coast_sdf));
    float land_coast_shadow = coast_edge * land * 0.26;
    map_colour *= 1.0 - land_coast_shadow;

    // Near view coastal sea foam along water edge
    float foam_factor = coast_edge * (1.0 - land) * closeFactor * (0.45 + wave_fine * 0.55);
    map_colour = mix(map_colour, foam_color, foam_factor);

    // =========================================================================
    // 7. Full-Bleed Borderless Presentation (Victoria 3 / Crusader Kings 3 Style)
    // =========================================================================
    // In Victoria 3 and CK3, the map extends smoothly to the edges without artificial
    // frame borders or boxy polar neatlines, providing an immersive, expansive grand strategy view.

    // Aerial perspective & Rayleigh atmospheric horizon haze in 3D tilted view (VolumetricAtmosphereAndClouds)
    // In Vulkan clip space, -1.0 is the top of the screen (distant horizon)
    if (closeFactor > 0.15) {
        float horizon_haze = smoothstep(-0.20, -0.90, v_ndc_y) * closeFactor;
        vec3 sky_horizon = vec3(0.68, 0.76, 0.86);
        map_colour = mix(map_colour, sky_horizon, horizon_haze * 0.28);
    }

    if (print_weight > 0.0 && sdbg_bits() == 0u)
        map_colour = mix(map_colour, parchment_map(mapUv, centre_id, political, land, coast_sdf), print_weight);
    // Saturation stage of the grading chain (tonemap -> saturation -> LUT -> dither).
    map_colour = apply_saturation(map_colour, SATURATION);

    outColor = vec4(max(map_colour, vec3(0.0)), 1.0);
}
