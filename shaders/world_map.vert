#version 460

// One instanced patch per SSBO element, tessellated in the vertex stage so
// the resident pyramid can carry a shallow 3-D relief at close range and a
// quiet paper plane at distance. Adjacent patches sample the same continuous
// height field, and the dateline is resolved with the shortest-arc rule, so
// no streaming boundary or wrap seam introduces a step.
layout(location = 0) out vec2 uv;           // continuous: unwrapped u, clamped v
layout(location = 1) out vec2 camera_delta;
layout(location = 2) out float closeFactor;
layout(location = 3) out float v_ndc_y;
layout(location = 4) flat out uvec2 v_levels; // patch fine/coarse levels
layout(location = 5) flat out float v_morph;
layout(location = 6) flat out float v_rectx;  // TEMP DIAG: raw patch u0
layout(location = 7) flat out float v_rectz;  // TEMP DIAG: raw patch du
layout(location = 8) flat out float v_recty;  // TEMP DIAG: raw patch v0

layout(set = 1, binding = 0) uniform sampler2D height_pyramid;
layout(set = 1, binding = 2) uniform sampler2D coast_sdf_tex;

layout(push_constant) uniform MapView {
    vec4 view;        // center uv, half extents
    vec4 camera;      // altitude metres, pitch degrees
    vec4 shading_dbg; // x = visual-debug bitmask
    vec4 pad;
};

// The compile-time pyramid geometry. Byte-identical with world_map.frag.
layout(std140, set = 1, binding = 3) uniform WorldLevels {
    vec4 base_pages;        // {40, 28}
    vec4 height_inv_size;   // 1 / height pyramid extent
    vec4 page_inv_size;     // 1 / province & SDF pyramid extent
    vec4 height_origin[4];  // level rectangle origin, height texels
    vec4 page_origin[4];    // level rectangle origin, page texels
    vec4 height_scale_bias; // metres = h01 * x + y
} lv;

struct WorldMapPatchGpu {
    vec4 rect;    // u0, v0, du, dv in continuous map UV
    uint level_a; // fine level actually sampled
    uint level_b; // coarse morph target
    float morph;  // 1 = fine only
    float pad;
};
layout(std430, set = 1, binding = 4) readonly buffer Patches {
    WorldMapPatchGpu patches[];
};

// Quality-tier octave caps. These mirror world_map.frag and are fed from the
// same specialisation constants; if the two stages resolve a different octave
// count the silhouette the geometry is displaced by drifts away from the relief
// the fragment shades.
layout(constant_id = 0) const int BASE_OCTAVES = 5;
layout(constant_id = 1) const int DETAIL_OCTAVES = 5;

// Enough vertices remain inside a patch to show relief at the first
// close/medium clip level without multiplying the far-world draw excessively.
// High-efficiency 32x32 vertex mesh provides smooth 3D relief without zoom stalls
const int GRID = 32;

// Metres covered by one screen pixel. Derived from camera altitude rather than
// a derivative, because a derivative is undefined in the vertex stage and the
// fragment stage has to arrive at the same number.
float pixel_footprint_m() {
    const float altitude = max(camera.x, 350.0);
    return max(altitude * 0.0018, 1.0);
}

// Keep byte-identical to relief_octaves() in world_map.frag.
int relief_octaves() {
    const float footprint = pixel_footprint_m();
    int lod = DETAIL_OCTAVES;
    if (footprint > 8000.0) lod = 0;
    else if (footprint > 3000.0) lod = 1;
    else if (footprint > 1200.0) lod = 2;
    else if (footprint > 400.0) lod = 3;
    return min(DETAIL_OCTAVES, lod);
}

// ---- Resident-pyramid sampling (byte-identical with world_map.frag) -------

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

// Morph-aware quantities: displacement and every fragment-stage derived field
// must blend the two levels identically or shading and geometry drift apart.
float terrain_base_metres(uint La, uint Lb, float m, vec2 uv) {
    float h = sample_height_m(La, uv);
    if (m < 1.0) h = mix(sample_height_m(Lb, uv), h, m);
    return h;
}

// Continuous land gate. The retired RGBA height atlas carried binary lake and
// spatial masks; the coast SDF encodes the same water side continuously
// (negative = water, ocean and lakes both), so a ~150 m transition band
// replaces the old smoothstep over the 0/1 mask.
float terrain_gate(uint La, uint Lb, float m, vec2 uv) {
    float sdf = sample_coast_raw(La, uv);
    if (m < 1.0) sdf = mix(sample_coast_raw(Lb, uv), sdf, m);
    return smoothstep(-300.0, 300.0, sdf);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Gradient (Perlin-style) noise, quintic fade. Byte-identical with
// world_map.frag: value-noise iso-contours hug the lattice axes, which made
// every octave's ridge 1-|2v-1| a set of straight creases along the two lattice
// axes of that octave - perpendicular pairs at i*36.87 deg - and the *16
// finite-difference normal drew hairlines along each of them. Gradient noise
// has no lattice-aligned iso-contours, so ridges become terrain-shaped.
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
// radius scales with the pixel footprint so octaves too fine to resolve become
// rounded hummocks instead of sub-pixel creases. Byte-identical with .frag.
float sabs(float x, float k) { return sqrt(x * x + k * k) - k; }

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
        // Ridge corner radius: ~2 px on the ground, in noise-value units of
        // this octave (0.024 for octave 0 at 60 km, 0.49 for octave 4).
        float k = clamp(2.0 * footprint / half_period_m, 0.02, 0.6);
        float x = gradient_noise01(p) * 2.0 - 1.0;
        float n = 1.0 - sabs(x, k);
        n = n * n;
        // Per-octave fade instead of a hard integer cut, so nothing pops
        // between LOD tiers. Byte-identical with world_map.frag.
        float fade = 1.0 - smoothstep(0.35, 1.25, footprint / (2.0 * half_period_m));
        total += n * amp * weight * fade;
        // Byte-identical weight ramp to world_map.frag: a smoothstep floor
        // instead of a hard clamp keeps the ridge weight C1 so the vertex
        // displacement and the fragment shading see the same continuous field.
        float wr = clamp(n * 1.8, 0.0, 1.0);
        weight = mix(0.20, 1.0, wr * wr * (3.0 - 2.0 * wr));
        // The energy rescale must see the same fade the contribution saw.
        amplitude_sum += a_full * fade;
        a_full *= 0.48;
        p = rot * p * 2.12;
        amp *= 0.48;
        half_period_m /= 2.12;
    }

    const float full_amplitude_sum = 1.0 + 0.48 + 0.2304 + 0.110592 + 0.0530842;

    float h = total * (full_amplitude_sum / max(amplitude_sum, 1.0e-4)) / 1.55;

    // ---- Erosion-inspired geometry (cheap, pure-function, no iteration) ----
    // Byte-identical with world_map.frag: both stages must compute the same h
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
    // Soft-saturation knee, byte-identical with world_map.frag: h can reach
    // ~1.2 while the return clamps at 1.0, which pinned the highest peaks on
    // a flat plateau whose rim is a C0 height contour the *16 normal gain
    // draws as a dark ring around every snow cap. Everything below the knee
    // is bit-identical to the linear map; the rim is C1; the maximum lands
    // strictly below 1 so the clamp never engages.
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

// Real global elevation from the resident pyramid, with the procedural fbm as
// height-dependent detail on top. Byte-identical with continuous_relief()
// in world_map.frag so displacement and shading cannot drift apart.
float terrainHeight(vec2 value, uint La, uint Lb, float m) {
    vec2 uv = map_uv(value);
    float gate = terrain_gate(La, Lb, m, uv);
    if (gate < 0.001) return 0.0;

    float baked = clamp(max(terrain_base_metres(La, Lb, m, uv), 0.0) / 10000.0, 0.0, 1.0);
    if (uint(shading_dbg.x + 0.5) == 0u) return baked * gate;
    int oct = relief_octaves();
    float proceduralRelief = oct > 0 ? alpine_mountain_relief(uv, oct, baked) : 0.0;
    float mountain_factor = clamp(max(smoothstep(0.06, 0.20, baked), 0.0), 0.0, 1.0);
    float detail_amp = mix(0.04, 0.70, mountain_factor);

    return clamp((baked + proceduralRelief * detail_amp) * gate, 0.0, 1.0);
}

void main() {
    WorldMapPatchGpu P = patches[gl_InstanceIndex];
    const vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    const int cell = gl_VertexIndex / 6;
    const vec2 gridCell = vec2(cell % GRID, cell / GRID);
    const vec2 local = (gridCell + corners[gl_VertexIndex % 6]) / float(GRID);
    // Keep u continuous (unwrapped): shared patch edges evaluate the exact
    // same bitwise expression, and the fragment stage wraps AFTER
    // interpolation, so a dateline-crossing triangle never sees fract mid-way.
    float u = P.rect.x + local.x * P.rect.z;
    float v = P.rect.y + local.y * P.rect.w;
    uv = vec2(u, clamp(v, 0.0, 1.0));
    v_levels = uvec2(P.level_a, P.level_b);
    v_morph = P.morph;
    v_rectx = P.rect.x;
    v_rectz = P.rect.z;
    v_recty = P.rect.y;

    float altitude = max(camera.x, 350.0);
    closeFactor = 1.0 - smoothstep(800.0, 3000.0, altitude);
    float relief = closeFactor > 0.001 ? terrainHeight(uv, P.level_a, P.level_b, P.morph) : 0.0;

    // Continuous projection: patches are emitted already unwrapped by the streamer
    // across the view bounds [center_u - half_u, center_u + half_u], so adjacent
    // patches always share identical continuous boundary coordinates without cracks.
    float du = u - view.x;
    float dv = v - view.y;
    camera_delta = vec2(du, dv);

    vec2 ndc = vec2(du / max(view.z, 1.0e-6), dv / max(view.w, 1.0e-6));
    float pitch = radians(clamp(camera.y, 25.0, 88.0));
    float tilt_amount = clamp((radians(90.0) - pitch) / radians(55.0), 0.0, 1.0) * closeFactor;

    // Clean, continuous 3D oblique elevation:
    // Physical mountain peaks soar vertically (-Y in screen space) based on terrain altitude
    float elevation_h = relief * 0.18 * closeFactor * mix(1.0, 0.5, smoothstep(600.0, 800.0, altitude));

    // Smooth oblique foreshortening and vertical 3D elevation displacement
    float screen_x = ndc.x;
    float screen_y = ndc.y - elevation_h * tilt_amount * 0.90;
    // Standard Vulkan depth: near (south, ndc.y=+1) is smaller depth, far (north, ndc.y=-1) is larger depth.
    // Mountain peaks rise toward the viewer, decreasing depth further.
    float depth = clamp((1.0 - ndc.y) * 0.5 * 0.6 + 0.2 - elevation_h * 0.35, 0.01, 0.99);

    gl_Position = vec4(screen_x, screen_y, depth, 1.0);
    v_ndc_y = screen_y;
}
