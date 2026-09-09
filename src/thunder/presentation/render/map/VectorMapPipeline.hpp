#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace thunder {

enum class VectorBorderClass : std::uint8_t {
    Province = 0,   // Subtle double-inked province boundary
    State = 1,      // Medium dark ink state boundary
    Country = 2,    // Sovereign illuminated gold/brass border
    Coastline = 3   // Land-water boundary with antique engraved wave hachures
};

struct VectorPoint {
    float x = 0.0f;
    float y = 0.0f;
    friend bool operator==(const VectorPoint&, const VectorPoint&) = default;
};

struct VectorBorderVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float width_px = 2.0f;
    float edge_dist = 0.0f; // -1.0 on left edge, 0.0 on center line, +1.0 on right edge
    std::uint32_t rgba = 0xffffffffu;
};

struct VectorBorderMesh {
    std::vector<VectorBorderVertex> vertices;
    std::vector<std::uint32_t> indices;

    void clear() noexcept {
        vertices.clear();
        indices.clear();
    }
    [[nodiscard]] std::size_t vertex_count() const noexcept { return vertices.size(); }
    [[nodiscard]] std::size_t index_count() const noexcept { return indices.size(); }
};

struct VectorProvincePolygon {
    ProvinceId province{};
    CountryId country{};
    StateId state{};
    std::vector<VectorPoint> points;
};

struct VectorEdge {
    VectorPoint p0{};
    VectorPoint p1{};
    ProvinceId left_province{};
    ProvinceId right_province{};
    VectorBorderClass border_class = VectorBorderClass::Province;
};

struct VectorBoundaryPolyline {
    std::uint32_t location_a = 0;
    std::uint32_t location_b = 0;
    float min_u = 0.0f;
    float max_u = 0.0f;
    float min_v = 0.0f;
    float max_v = 0.0f;
    std::vector<VectorPoint> points; // Normalized map coordinates [0..1]
    VectorBorderClass authored_class = VectorBorderClass::Province;
};

class VectorMapSystem {
public:
    VectorMapSystem() = default;

    void clear() noexcept;
    void add_province_polygon(ProvinceId province, CountryId country, StateId state, std::span<const VectorPoint> contour);
    void rebuild_shared_edges();

    // Load Thunder-native, map-source-independent boundary vector layers.
    bool load_binary_boundaries(const std::filesystem::path& path);
    bool load_lod_boundaries(const std::filesystem::path& far_path,
                             const std::filesystem::path& medium_path,
                             const std::filesystem::path& near_path);

    [[nodiscard]] std::span<const VectorBoundaryPolyline> binary_polylines() const noexcept {
        if (!near_polylines_.empty()) return near_polylines_;
        if (!medium_polylines_.empty()) return medium_polylines_;
        if (!far_polylines_.empty()) return far_polylines_;
        return binary_polylines_;
    }

    // Render screen-space anti-aliased vector boundaries dynamically according to camera viewport
    void render_screen_boundaries(
        UiDrawList& ui,
        double center_u, double center_v,
        double half_u, double half_v,
        int screen_w, int screen_h,
        const std::function<VectorBorderClass(std::uint32_t loc_a, std::uint32_t loc_b)>& classifier,
        float line_scale = 1.0f,
        const std::function<bool(float u, float v)>& is_land = {}) const;

    // Compute the screen-space boundary mesh and cache it, without appending to
    // a draw list. Returns true when the cached geometry was rebuilt (camera
    // moved, window resized, or first build); false when the previous cache was
    // reused or the inputs are invalid. render_screen_boundaries() delegates to
    // this and then appends the cached spans, so both paths stay in sync.
    bool build_screen_boundaries(
        double center_u, double center_v,
        double half_u, double half_v,
        int screen_w, int screen_h,
        const std::function<VectorBorderClass(std::uint32_t loc_a, std::uint32_t loc_b)>& classifier,
        float line_scale = 1.0f,
        const std::function<bool(float u, float v)>& is_land = {}) const;

    [[nodiscard]] std::span<const UiVertex> cached_screen_vertices() const noexcept { return temp_vertices_; }
    [[nodiscard]] std::span<const std::uint32_t> cached_screen_indices() const noexcept { return temp_indices_; }
    [[nodiscard]] bool screen_cache_valid() const noexcept { return screen_cache_valid_; }


    // Ownership/map-mode changes can invalidate classifications while the
    // camera remains stationary. Ordinary camera movement is detected by the
    // cache automatically.
    void invalidate_screen_cache() noexcept { screen_cache_valid_ = false; }

    // Generate antique parchment border geometry (illuminated gold country borders, iron gall ink lines)
    void generate_border_mesh(VectorBorderMesh& out_mesh, float zoom_scale = 1.0f) const;

    // Generate antique coastline wave hachures (parallel engraved wave lines around coast)
    void generate_coastline_hachures(VectorBorderMesh& out_mesh, std::size_t rings = 3, float ring_spacing = 4.0f) const;

    // Generate nautical rhumb lines (antique cartographic navigation rays)
    void generate_rhumb_lines(VectorBorderMesh& out_mesh, VectorPoint center, float radius, std::size_t rays = 16) const;

    // Generate dynamic vector diagonal hatching ribbons for occupied / war-goal provinces
    void generate_hatching_mesh(ProvinceId province, VectorBorderMesh& out_mesh,
                               std::uint32_t stripe_rgba = 0x808b261fu,
                               float stripe_spacing = 14.0f) const;

    // Calculate smooth blend factor between antique parchment map and 3D satellite terrain
    [[nodiscard]] static float calculate_paper_map_blend(double camera_altitude_m) noexcept;

    [[nodiscard]] std::size_t polygon_count() const noexcept { return polygons_.size(); }
    [[nodiscard]] std::size_t edge_count() const noexcept { return edges_.size(); }
    [[nodiscard]] std::span<const VectorEdge> edges() const noexcept { return edges_; }

private:
    std::vector<VectorProvincePolygon> polygons_;
    std::vector<VectorEdge> edges_;
    std::vector<VectorBoundaryPolyline> binary_polylines_;
    std::vector<VectorBoundaryPolyline> far_polylines_;
    std::vector<VectorBoundaryPolyline> medium_polylines_;
    std::vector<VectorBoundaryPolyline> near_polylines_;
    mutable std::vector<UiVertex> temp_vertices_;
    mutable std::vector<std::uint32_t> temp_indices_;
    mutable bool screen_cache_valid_ = false;
    mutable double cached_center_u_ = 0.0;
    mutable double cached_center_v_ = 0.0;
    mutable double cached_half_u_ = 0.0;
    mutable double cached_half_v_ = 0.0;
    mutable int cached_screen_w_ = 0;
    mutable int cached_screen_h_ = 0;
    mutable float cached_line_scale_ = 0.0f;
};

} // namespace thunder
