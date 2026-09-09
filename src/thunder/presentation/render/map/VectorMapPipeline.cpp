
#include "thunder/presentation/render/map/VectorMapPipeline.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <utility>

namespace thunder {

void VectorMapSystem::clear() noexcept {
    polygons_.clear();
    edges_.clear();
    binary_polylines_.clear();
    far_polylines_.clear();
    medium_polylines_.clear();
    near_polylines_.clear();
    temp_vertices_.clear();
    temp_indices_.clear();
    screen_cache_valid_ = false;
}

void VectorMapSystem::add_province_polygon(ProvinceId province, CountryId country, StateId state,
                                          std::span<const VectorPoint> contour) {
    if (contour.size() < 3) return;
    VectorProvincePolygon poly;
    poly.province = province;
    poly.country = country;
    poly.state = state;
    poly.points.assign(contour.begin(), contour.end());
    polygons_.push_back(std::move(poly));
}

void VectorMapSystem::rebuild_shared_edges() {
    edges_.clear();
    if (polygons_.empty()) return;

    struct CanonicalKey {
        int x0, y0, x1, y1;
        bool operator<(const CanonicalKey& o) const noexcept {
            if (x0 != o.x0) return x0 < o.x0;
            if (y0 != o.y0) return y0 < o.y0;
            if (x1 != o.x1) return x1 < o.x1;
            return y1 < o.y1;
        }
    };

    struct EdgeOwner {
        ProvinceId province;
        CountryId country;
        StateId state;
        VectorPoint p0;
        VectorPoint p1;
    };

    std::map<CanonicalKey, std::vector<EdgeOwner>> edge_map;

    auto quantize = [](float v) -> int {
        return static_cast<int>(std::round(v * 100.0f));
    };

    for (const auto& poly : polygons_) {
        const auto n = poly.points.size();
        for (std::size_t i = 0; i < n; ++i) {
            const auto& ptA = poly.points[i];
            const auto& ptB = poly.points[(i + 1) % n];
            int qx0 = quantize(ptA.x);
            int qy0 = quantize(ptA.y);
            int qx1 = quantize(ptB.x);
            int qy1 = quantize(ptB.y);

            CanonicalKey key{qx0, qy0, qx1, qy1};
            if (qx0 > qx1 || (qx0 == qx1 && qy0 > qy1)) {
                key = {qx1, qy1, qx0, qy0};
            }
            edge_map[key].push_back({poly.province, poly.country, poly.state, ptA, ptB});
        }
    }

    edges_.reserve(edge_map.size());
    for (const auto& [key, owners] : edge_map) {
        if (owners.empty()) continue;

        VectorEdge edge;
        edge.p0 = owners[0].p0;
        edge.p1 = owners[0].p1;
        edge.left_province = owners[0].province;

        if (owners.size() == 1) {
            edge.right_province = ProvinceId{};
            edge.border_class = VectorBorderClass::Coastline;
        } else {
            edge.right_province = owners[1].province;
            if (owners[0].country != owners[1].country) {
                edge.border_class = VectorBorderClass::Country;
            } else if (owners[0].state != owners[1].state) {
                edge.border_class = VectorBorderClass::State;
            } else {
                edge.border_class = VectorBorderClass::Province;
            }
        }
        edges_.push_back(edge);
    }
}

void VectorMapSystem::generate_border_mesh(VectorBorderMesh& out_mesh, float zoom_scale) const {
    out_mesh.clear();
    if (edges_.empty()) return;

    out_mesh.vertices.reserve(edges_.size() * 4);
    out_mesh.indices.reserve(edges_.size() * 6);

    for (const auto& edge : edges_) {
        float width_px = 1.2f;
        std::uint32_t rgba = 0x803a271du; // Dark iron gall ink for province borders

        switch (edge.border_class) {
        case VectorBorderClass::Country:
            width_px = 4.2f * zoom_scale;
            rgba = 0xffd4af37u; // Illuminated gold / brass border
            break;
        case VectorBorderClass::State:
            width_px = 2.4f * zoom_scale;
            rgba = 0xb05c3c24u; // Sepia brown state boundary
            break;
        case VectorBorderClass::Coastline:
            width_px = 2.8f * zoom_scale;
            rgba = 0xc02b1f14u; // Deep engraved shoreline contour
            break;
        case VectorBorderClass::Province:
        default:
            width_px = 1.2f * zoom_scale;
            rgba = 0x804a3525u; // Fine vintage ink line
            break;
        }

        const float dx = edge.p1.x - edge.p0.x;
        const float dy = edge.p1.y - edge.p0.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;

        const float nx = -dy / len;
        const float ny = dx / len;

        // Engraved-groove border: a faint light-catching rim offset toward the
        // lower-right is laid down first; the dark ink line drawn over it
        // leaves a bright inner edge, reading as a groove cut into the paper.
        const auto append_strip = [&](float off_x, float off_y, float width, std::uint32_t strip_rgba) {
            const float half_w = width * 0.5f;
            const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());
            const float q0x = edge.p0.x + off_x;
            const float q0y = edge.p0.y + off_y;
            const float q1x = edge.p1.x + off_x;
            const float q1y = edge.p1.y + off_y;
            out_mesh.vertices.push_back({q0x - nx * half_w, q0y - ny * half_w, 0.0f, nx, ny, width, -1.0f, strip_rgba});
            out_mesh.vertices.push_back({q0x + nx * half_w, q0y + ny * half_w, 0.0f, nx, ny, width, +1.0f, strip_rgba});
            out_mesh.vertices.push_back({q1x - nx * half_w, q1y - ny * half_w, 0.0f, nx, ny, width, -1.0f, strip_rgba});
            out_mesh.vertices.push_back({q1x + nx * half_w, q1y + ny * half_w, 0.0f, nx, ny, width, +1.0f, strip_rgba});
            out_mesh.indices.push_back(base_idx);
            out_mesh.indices.push_back(base_idx + 1);
            out_mesh.indices.push_back(base_idx + 2);
            out_mesh.indices.push_back(base_idx + 2);
            out_mesh.indices.push_back(base_idx + 1);
            out_mesh.indices.push_back(base_idx + 3);
        };

        const float groove = 0.9f * zoom_scale;
        append_strip(groove, groove, width_px, 0x58fff6e0u);
        append_strip(0.0f, 0.0f, width_px, rgba);
    }
}

void VectorMapSystem::generate_coastline_hachures(VectorBorderMesh& out_mesh, std::size_t rings, float ring_spacing) const {
    for (const auto& edge : edges_) {
        if (edge.border_class != VectorBorderClass::Coastline) continue;

        const float dx = edge.p1.x - edge.p0.x;
        const float dy = edge.p1.y - edge.p0.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;

        const float nx = -dy / len;
        const float ny = dx / len;

        for (std::size_t r = 1; r <= rings; ++r) {
            const float offset = static_cast<float>(r) * ring_spacing;
            const float line_w = 1.0f;
            const float half_w = line_w * 0.5f;
            const std::uint32_t alpha = static_cast<std::uint32_t>(std::max(10, 80 - static_cast<int>(r) * 20));
            const std::uint32_t hachure_rgba = (alpha << 24) | 0x003e2c1e; // Fading vintage water ink

            const float p0x = edge.p0.x + nx * offset;
            const float p0y = edge.p0.y + ny * offset;
            const float p1x = edge.p1.x + nx * offset;
            const float p1y = edge.p1.y + ny * offset;

            const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());
            out_mesh.vertices.push_back({p0x - nx * half_w, p0y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, hachure_rgba});
            out_mesh.vertices.push_back({p0x + nx * half_w, p0y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, hachure_rgba});
            out_mesh.vertices.push_back({p1x - nx * half_w, p1y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, hachure_rgba});
            out_mesh.vertices.push_back({p1x + nx * half_w, p1y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, hachure_rgba});

            out_mesh.indices.push_back(base_idx);
            out_mesh.indices.push_back(base_idx + 1);
            out_mesh.indices.push_back(base_idx + 2);

            out_mesh.indices.push_back(base_idx + 2);
            out_mesh.indices.push_back(base_idx + 1);
            out_mesh.indices.push_back(base_idx + 3);
        }
    }
}

void VectorMapSystem::generate_rhumb_lines(VectorBorderMesh& out_mesh, VectorPoint center, float radius, std::size_t rays) const {
    if (rays == 0 || radius <= 0.0f) return;
    constexpr float pi = 3.1415926535f;
    const float step = (2.0f * pi) / static_cast<float>(rays);
    const float line_w = 0.8f;
    const float half_w = line_w * 0.5f;
    constexpr std::uint32_t rhumb_rgba = 0x408b6f4eu; // Subtle aged navigation gold/brown

    for (std::size_t i = 0; i < rays; ++i) {
        const float angle = static_cast<float>(i) * step;
        const float cos_a = std::cos(angle);
        const float sin_a = std::sin(angle);

        const float p0x = center.x;
        const float p0y = center.y;
        const float p1x = center.x + cos_a * radius;
        const float p1y = center.y + sin_a * radius;

        const float nx = -sin_a;
        const float ny = cos_a;

        const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());
        out_mesh.vertices.push_back({p0x - nx * half_w, p0y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, rhumb_rgba});
        out_mesh.vertices.push_back({p0x + nx * half_w, p0y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, rhumb_rgba});
        out_mesh.vertices.push_back({p1x - nx * half_w, p1y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, rhumb_rgba});
        out_mesh.vertices.push_back({p1x + nx * half_w, p1y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, rhumb_rgba});

        out_mesh.indices.push_back(base_idx);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 2);

        out_mesh.indices.push_back(base_idx + 2);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 3);
    }
}

void VectorMapSystem::generate_hatching_mesh(ProvinceId province, VectorBorderMesh& out_mesh,
                                            std::uint32_t stripe_rgba, float stripe_spacing) const {
    const VectorProvincePolygon* target = nullptr;
    for (const auto& p : polygons_) {
        if (p.province == province) {
            target = &p;
            break;
        }
    }
    if (!target || target->points.size() < 3) return;

    float min_x = target->points[0].x, max_x = target->points[0].x;
    float min_y = target->points[0].y, max_y = target->points[0].y;
    for (const auto& pt : target->points) {
        min_x = std::min(min_x, pt.x);
        max_x = std::max(max_x, pt.x);
        min_y = std::min(min_y, pt.y);
        max_y = std::max(max_y, pt.y);
    }

    const float start_c = min_y - max_x;
    const float end_c = max_y - min_x;
    const float stripe_w = 2.0f;
    const float half_w = stripe_w * 0.5f;
    const float nx = -0.7071f;
    const float ny = 0.7071f;

    for (float c = start_c; c <= end_c; c += stripe_spacing) {
        float x0 = min_x;
        float y0 = x0 + c;
        float x1 = max_x;
        float y1 = x1 + c;

        if (y0 < min_y) { x0 += (min_y - y0); y0 = min_y; }
        if (y1 > max_y) { x1 -= (y1 - max_y); y1 = max_y; }
        if (x0 >= x1 || y0 > max_y || y1 < min_y) continue;

        const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());
        out_mesh.vertices.push_back({x0 - nx * half_w, y0 - ny * half_w, 0.0f, nx, ny, stripe_w, -1.0f, stripe_rgba});
        out_mesh.vertices.push_back({x0 + nx * half_w, y0 + ny * half_w, 0.0f, nx, ny, stripe_w, +1.0f, stripe_rgba});
        out_mesh.vertices.push_back({x1 - nx * half_w, y1 - ny * half_w, 0.0f, nx, ny, stripe_w, -1.0f, stripe_rgba});
        out_mesh.vertices.push_back({x1 + nx * half_w, y1 + ny * half_w, 0.0f, nx, ny, stripe_w, +1.0f, stripe_rgba});

        out_mesh.indices.push_back(base_idx);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 2);

        out_mesh.indices.push_back(base_idx + 2);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 3);
    }
}

float VectorMapSystem::calculate_paper_map_blend(double camera_altitude_m) noexcept {
    constexpr double low_alt = 60000.0;
    constexpr double high_alt = 800000.0;

    if (camera_altitude_m <= low_alt) return 0.0f;
    if (camera_altitude_m >= high_alt) return 1.0f;

    const double t = (camera_altitude_m - low_alt) / (high_alt - low_alt);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

static bool parse_boundary_file(const std::filesystem::path& path, std::vector<VectorBoundaryPolyline>& out_polylines) {
    out_polylines.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    char magic[8]{};
    stream.read(magic, 8);
    if (stream.gcount() != 8 ||
        (std::memcmp(magic, "THUNDRV1", 8) != 0 && std::memcmp(magic, "THUNDRV2", 8) != 0)) {
        return false;
    }
    const bool version_two_magic = std::memcmp(magic, "THUNDRV2", 8) == 0;

    std::uint32_t version = 0;
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1 && version != 2) return false;

    std::uint32_t count = 0;
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!stream || count > 500'000u) return false;

    out_polylines.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t loc_a = 0, loc_b = 0, vcount = 0;
        std::uint8_t authored_class = static_cast<std::uint8_t>(VectorBorderClass::Province);
        stream.read(reinterpret_cast<char*>(&loc_a), sizeof(loc_a));
        stream.read(reinterpret_cast<char*>(&loc_b), sizeof(loc_b));
        if (version >= 2 || version_two_magic) {
            std::uint8_t reserved = 0;
            stream.read(reinterpret_cast<char*>(&authored_class), sizeof(authored_class));
            stream.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));
            if (!stream || authored_class > static_cast<std::uint8_t>(VectorBorderClass::Coastline)) return false;
        }
        stream.read(reinterpret_cast<char*>(&vcount), sizeof(vcount));
        if (!stream || vcount > 100'000u) return false;

        VectorBoundaryPolyline poly;
        poly.location_a = loc_a;
        poly.location_b = loc_b;
        poly.authored_class = static_cast<VectorBorderClass>(authored_class);
        poly.points.resize(vcount);

        stream.read(reinterpret_cast<char*>(poly.points.data()),
                    static_cast<std::streamsize>(vcount * sizeof(VectorPoint)));
        if (!stream) return false;

        // Precompute AABB for instant spatial viewport culling
        if (!poly.points.empty()) {
            float min_u = poly.points[0].x, max_u = poly.points[0].x;
            float min_v = poly.points[0].y, max_v = poly.points[0].y;
            for (const auto& pt : poly.points) {
                min_u = std::min(min_u, pt.x);
                max_u = std::max(max_u, pt.x);
                min_v = std::min(min_v, pt.y);
                max_v = std::max(max_v, pt.y);
            }
            poly.min_u = min_u;
            poly.max_u = max_u;
            poly.min_v = min_v;
            poly.max_v = max_v;
        }

        out_polylines.push_back(std::move(poly));
    }
    return true;
}

bool VectorMapSystem::load_binary_boundaries(const std::filesystem::path& path) {
    screen_cache_valid_ = false;
    return parse_boundary_file(path, binary_polylines_);
}

bool VectorMapSystem::load_lod_boundaries(const std::filesystem::path& far_path,
                                         const std::filesystem::path& medium_path,
                                         const std::filesystem::path& near_path) {
    screen_cache_valid_ = false;
    bool any_loaded = false;
    if (!far_path.empty() && std::filesystem::exists(far_path)) {
        if (parse_boundary_file(far_path, far_polylines_)) any_loaded = true;
    }
    if (!medium_path.empty() && std::filesystem::exists(medium_path)) {
        if (parse_boundary_file(medium_path, medium_polylines_)) any_loaded = true;
    }
    if (!near_path.empty() && std::filesystem::exists(near_path)) {
        if (parse_boundary_file(near_path, near_polylines_)) any_loaded = true;
    }
    return any_loaded;
}

void VectorMapSystem::render_screen_boundaries(
    UiDrawList& ui,
    double center_u, double center_v,
    double half_u, double half_v,
    int screen_w, int screen_h,
    const std::function<VectorBorderClass(std::uint32_t loc_a, std::uint32_t loc_b)>& classifier,
    float line_scale,
    const std::function<bool(float u, float v)>& is_land) const {
    (void)build_screen_boundaries(center_u, center_v, half_u, half_v, screen_w, screen_h,
                                  classifier, line_scale, is_land);
    if (screen_cache_valid_) {
        ui.append_geometry(temp_vertices_, temp_indices_);
    }
}

bool VectorMapSystem::build_screen_boundaries(
    double center_u, double center_v,
    double half_u, double half_v,
    int screen_w, int screen_h,
    const std::function<VectorBorderClass(std::uint32_t loc_a, std::uint32_t loc_b)>& classifier,
    float line_scale,
    const std::function<bool(float u, float v)>& is_land) const {
    if (half_u <= 1e-6 || half_v <= 1e-6 || screen_w <= 0 || screen_h <= 0) {
        // Invalid viewport: nothing to draw. If a previous valid mesh was
        // cached, drop it so the draw-list wrapper and the persistent overlay
        // both emit nothing rather than reusing stale geometry.
        if (screen_cache_valid_) {
            temp_vertices_.clear();
            temp_indices_.clear();
            screen_cache_valid_ = false;
            return true;
        }
        return false;
    }

    if (screen_cache_valid_ && center_u == cached_center_u_ && center_v == cached_center_v_ &&
        half_u == cached_half_u_ && half_v == cached_half_v_ && screen_w == cached_screen_w_ &&
        screen_h == cached_screen_h_ && line_scale == cached_line_scale_) {
        return false;
    }

    // 1. Select the appropriate LOD level
    const std::vector<VectorBoundaryPolyline>* polylines_ptr = nullptr;
    if (half_u > 0.16 && !far_polylines_.empty()) {
        polylines_ptr = &far_polylines_;
    } else if (half_u > 0.05 && !medium_polylines_.empty()) {
        polylines_ptr = &medium_polylines_;
    } else if (!far_polylines_.empty()) {
        // Near detail is represented by the GPU raster-ID boundary pass. The
        // authored near vector file contains hundreds of thousands of leaf
        // segments; walking all of them on every mouse-drag frame would make
        // the UI thread fall behind the camera and create an apparent map/
        // border desynchronisation. Keep the CPU overlay to sovereign/coast
        // strokes at this scale and let the page atlas draw leaf hairlines.
        polylines_ptr = &far_polylines_;
    } else if (!near_polylines_.empty()) {
        polylines_ptr = &near_polylines_;
    } else if (!binary_polylines_.empty()) {
        polylines_ptr = &binary_polylines_;
    } else if (!medium_polylines_.empty()) {
        polylines_ptr = &medium_polylines_;
    } else if (!far_polylines_.empty()) {
        polylines_ptr = &far_polylines_;
    }
    if (!polylines_ptr || polylines_ptr->empty()) return false;
    const auto& polylines = *polylines_ptr;

    // 2. Zoom-dependent fade for province-level borders (strictly hidden at world view)
    const float province_fade = std::clamp((0.040f - static_cast<float>(half_u)) / 0.018f, 0.0f, 1.0f);
    const bool show_provinces = province_fade > 0.01f;

    // 3. Clear and reuse geometry buffers
    temp_vertices_.clear();
    temp_indices_.clear();

    const double vis_half_u = half_u * 1.06;
    const double vis_half_v = half_v * 1.06;

    for (const auto& poly : polylines) {
        if (poly.points.size() < 2) continue;

        // Instant AABB culling on entire polyline (culls 98% of world at close zoom!)
        double poly_mid_u = static_cast<double>(poly.min_u + poly.max_u) * 0.5;
        double delta_center_u = poly_mid_u - center_u;
        delta_center_u -= std::round(delta_center_u);
        double poly_half_w = static_cast<double>(poly.max_u - poly.min_u) * 0.5;
        if (std::abs(delta_center_u) > vis_half_u + poly_half_w) continue;

        double poly_mid_v = static_cast<double>(poly.min_v + poly.max_v) * 0.5;
        double poly_half_h = static_cast<double>(poly.max_v - poly.min_v) * 0.5;
        if (std::abs(poly_mid_v - center_v) > vis_half_v + poly_half_h) continue;

        const auto cls = classifier ? classifier(poly.location_a, poly.location_b) : poly.authored_class;
        if (cls == VectorBorderClass::Province && !show_provinces) {
            continue;
        }

        float width_px = 1.0f;
        std::uint32_t rgba = 0x803a271du;

        switch (cls) {
        case VectorBorderClass::Country:
            width_px = 0.95f * line_scale;
            rgba = 0xd63b3430u; // thin carbon/charcoal sovereign ink
            break;
        case VectorBorderClass::State:
            width_px = 0.72f * line_scale;
            rgba = 0x9a61584du; // quieter area/state ink
            break;
        case VectorBorderClass::Coastline:
            width_px = 0.48f * line_scale; // natural edge, never a political outline
            rgba = 0x704f7d7du; // pale cyan/graphite land-edge separation
            break;
        case VectorBorderClass::Province:
        default: {
            width_px = 0.55f * line_scale;
            const auto alpha = static_cast<std::uint32_t>(province_fade * 72.0f);
            rgba = (alpha << 24) | 0x003e342cu; // quiet administrative hairline
            break;
        }
        }

        const float half_w = width_px * 0.5f;

        const std::size_t n = poly.points.size();
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const auto& p0 = poly.points[i];
            const auto& p1 = poly.points[i + 1];

            // 1. Calculate relative dx along torus (shortest delta)
            double du = static_cast<double>(p1.x - p0.x);
            du -= std::round(du);
            double dv = static_cast<double>(p1.y - p0.y);

            // Skip abnormal / wrap seam crossing segments
            if (std::abs(du) > 0.03 || std::abs(dv) > 0.03) continue;

            // 2. Project p0 relative to camera center
            double delta_u0 = static_cast<double>(p0.x) - center_u;
            delta_u0 -= std::round(delta_u0);

            // 3. Project p1 strictly relative to p0 (NEVER wrap p1 independently!)
            double delta_u1 = delta_u0 + du;

            // 4. Screen coordinates
            float sx0 = static_cast<float>((delta_u0 / (half_u * 2.0) + 0.5) * static_cast<double>(screen_w));
            float sx1 = static_cast<float>((delta_u1 / (half_u * 2.0) + 0.5) * static_cast<double>(screen_w));

            float sy0 = static_cast<float>(((static_cast<double>(p0.y) - center_v) / (half_v * 2.0) + 0.5) * static_cast<double>(screen_h));
            float sy1 = static_cast<float>(((static_cast<double>(p1.y) - center_v) / (half_v * 2.0) + 0.5) * static_cast<double>(screen_h));

            // Screen margin culling
            constexpr float margin = 30.0f;
            const float sw = static_cast<float>(screen_w);
            const float sh = static_cast<float>(screen_h);
            if ((sx0 < -margin && sx1 < -margin) ||
                (sx0 > sw + margin && sx1 > sw + margin) ||
                (sy0 < -margin && sy1 < -margin) ||
                (sy0 > sh + margin && sy1 > sh + margin)) {
                continue;
            }

            const float dx = sx1 - sx0;
            const float dy = sy1 - sy0;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.60f || len > static_cast<float>(screen_w) * 0.25f) continue;

            const float nx = -dy / len;
            const float ny = dx / len;

            // 1. Country Borders: soft semi-transparent gradient shadow on BOTH sides
            if (cls == VectorBorderClass::Country) {
                const float shadow_w = 6.0f * line_scale;
                constexpr std::uint32_t shadow_center_rgba = 0x50181410u; // ~31% alpha
                constexpr std::uint32_t shadow_outer_rgba = 0x00181410u;  // 0% alpha

                // Negative side shadow (-nx to center)
                auto base = static_cast<std::uint32_t>(temp_vertices_.size());
                temp_vertices_.push_back(UiVertex{sx0 - nx * shadow_w, sy0 - ny * shadow_w, 0.0f, 0.0f, shadow_outer_rgba});
                temp_vertices_.push_back(UiVertex{sx0, sy0, 0.5f, 0.0f, shadow_center_rgba});
                temp_vertices_.push_back(UiVertex{sx1, sy1, 0.5f, 1.0f, shadow_center_rgba});
                temp_vertices_.push_back(UiVertex{sx1 - nx * shadow_w, sy1 - ny * shadow_w, 0.0f, 1.0f, shadow_outer_rgba});
                temp_indices_.insert(temp_indices_.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});

                // Positive side shadow (center to +nx)
                base = static_cast<std::uint32_t>(temp_vertices_.size());
                temp_vertices_.push_back(UiVertex{sx0, sy0, 0.5f, 0.0f, shadow_center_rgba});
                temp_vertices_.push_back(UiVertex{sx0 + nx * shadow_w, sy0 + ny * shadow_w, 1.0f, 0.0f, shadow_outer_rgba});
                temp_vertices_.push_back(UiVertex{sx1 + nx * shadow_w, sy1 + ny * shadow_w, 1.0f, 1.0f, shadow_outer_rgba});
                temp_vertices_.push_back(UiVertex{sx1, sy1, 0.5f, 1.0f, shadow_center_rgba});
                temp_indices_.insert(temp_indices_.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
            }
            // 2. Coastlines: soft semi-transparent gradient shadow on CONTINENTAL/LAND side ONLY
            else if (cls == VectorBorderClass::Coastline) {
                float land_sign = 1.0f;
                if (is_land) {
                    const double mid_u = (static_cast<double>(p0.x) + static_cast<double>(p1.x)) * 0.5;
                    const double mid_v = (static_cast<double>(p0.y) + static_cast<double>(p1.y)) * 0.5;
                    const double du_uv = static_cast<double>(p1.x - p0.x);
                    const double dv_uv = static_cast<double>(p1.y - p0.y);
                    const double l_uv = std::sqrt(du_uv * du_uv + dv_uv * dv_uv);
                    if (l_uv > 1e-6) {
                        const double nx_uv = -dv_uv / l_uv;
                        const double ny_uv = du_uv / l_uv;
                        constexpr double step = 0.0004;
                        if (is_land(static_cast<float>(mid_u + nx_uv * step),
                                    static_cast<float>(mid_v + ny_uv * step))) {
                            land_sign = 1.0f;
                        } else {
                            land_sign = -1.0f;
                        }
                    }
                }
                const float shadow_w = 5.0f * line_scale;
                constexpr std::uint32_t shadow_center_rgba = 0x44181410u; // ~27% alpha
                constexpr std::uint32_t shadow_outer_rgba = 0x00181410u;  // 0% alpha

                // Single shadow strip extending into the land side
                const auto base = static_cast<std::uint32_t>(temp_vertices_.size());
                temp_vertices_.push_back(UiVertex{sx0, sy0, 0.0f, 0.0f, shadow_center_rgba});
                temp_vertices_.push_back(UiVertex{sx0 + land_sign * nx * shadow_w, sy0 + land_sign * ny * shadow_w, 1.0f, 0.0f, shadow_outer_rgba});
                temp_vertices_.push_back(UiVertex{sx1 + land_sign * nx * shadow_w, sy1 + land_sign * ny * shadow_w, 1.0f, 1.0f, shadow_outer_rgba});
                temp_vertices_.push_back(UiVertex{sx1, sy1, 0.0f, 1.0f, shadow_center_rgba});
                temp_indices_.insert(temp_indices_.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
            }

            // The coastline is a natural land edge. Keep only its very faint
            // cyan separation; country borders receive the crisp carbon thunder.
            const auto base = static_cast<std::uint32_t>(temp_vertices_.size());
            temp_vertices_.push_back(UiVertex{sx0 - nx * half_w, sy0 - ny * half_w, 0.0f, 0.0f, rgba});
            temp_vertices_.push_back(UiVertex{sx0 + nx * half_w, sy0 + ny * half_w, 1.0f, 0.0f, rgba});
            temp_vertices_.push_back(UiVertex{sx1 + nx * half_w, sy1 + ny * half_w, 1.0f, 1.0f, rgba});
            temp_vertices_.push_back(UiVertex{sx1 - nx * half_w, sy1 - ny * half_w, 0.0f, 1.0f, rgba});
            temp_indices_.insert(temp_indices_.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        }
    }

    cached_center_u_ = center_u;
    cached_center_v_ = center_v;
    cached_half_u_ = half_u;
    cached_half_v_ = half_v;
    cached_screen_w_ = screen_w;
    cached_screen_h_ = screen_h;
    cached_line_scale_ = line_scale;
    screen_cache_valid_ = true;

    // Geometry rebuilt: callers that own a persistent GPU overlay should
    // re-upload it. Subsequent stationary frames return false and skip the
    // rebuild entirely.
    return true;
}

} // namespace thunder
