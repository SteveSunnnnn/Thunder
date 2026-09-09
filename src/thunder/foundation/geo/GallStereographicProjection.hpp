#pragma once

#include "thunder/foundation/geo/MercatorProjection.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

// Gall Stereographic Projection (EPSG: 54016 / ESRI: 54016)
// A perspective stereographic cylindrical projection with standard parallels at 45° N and 45° S.
// Standard projection choice in grand strategy engines (such as Europa Universalis V / Project Caesar)
// to significantly reduce polar and high-latitude area distortion while preserving continental shapes.
class GallStereographicProjection {
public:
    static constexpr double earth_radius_m = 6'378'137.0;
    static constexpr double standard_parallel_rad = 0.78539816339744830962; // 45 deg (pi / 4)
    static constexpr double max_latitude_deg = 89.9; // Can project up to near poles unlike Mercator (+/-85.05°)

    [[nodiscard]] static WorldMeters project(GeoCoordinate geo) noexcept {
        const double lat = std::clamp(geo.latitude_deg, -max_latitude_deg, max_latitude_deg);
        const double lon_rad = geo.longitude_deg * (pi / 180.0);
        const double lat_rad = lat * (pi / 180.0);
        // x = R * lambda * cos(45°) = R * lambda / sqrt(2)
        // y = R * (1 + sqrt(2)/2) * tan(phi / 2) = R * (1 + 1/sqrt(2)) * tan(phi / 2)
        constexpr double inv_sqrt2 = 0.70710678118654752440;
        constexpr double y_scale = 1.0 + inv_sqrt2; // ~1.7071067811865475
        return {
            earth_radius_m * lon_rad * inv_sqrt2,
            earth_radius_m * y_scale * std::tan(lat_rad * 0.5)
        };
    }

    [[nodiscard]] static GeoCoordinate unproject(WorldMeters world) noexcept {
        constexpr double sqrt2 = 1.41421356237309504880;
        constexpr double y_scale = 1.0 + 0.70710678118654752440;
        const double lon_rad = (world.x * sqrt2) / earth_radius_m;
        const double lat_rad = 2.0 * std::atan(world.y / (earth_radius_m * y_scale));
        return {
            lon_rad * (180.0 / pi),
            std::clamp(lat_rad * (180.0 / pi), -max_latitude_deg, max_latitude_deg)
        };
    }

    [[nodiscard]] static double euclidean_distance_m(WorldMeters a, WorldMeters b) noexcept {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

private:
    static constexpr double pi = 3.1415926535897932384626433832795;
};

} // namespace thunder
