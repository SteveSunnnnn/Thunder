#pragma once

#include <algorithm>
#include <cmath>

namespace thunder {

struct GeoCoordinate {
    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
};

struct WorldMeters {
    double x = 0.0;
    double y = 0.0;
};

class MercatorProjection {
public:
    static constexpr double earth_radius_m = 6'378'137.0;
    static constexpr double max_latitude_deg = 85.051129; // EPSG:3857 standard max latitude (+/-85.051129 deg)

    [[nodiscard]] static WorldMeters project(GeoCoordinate geo) noexcept {
        const double lat = std::clamp(geo.latitude_deg, -max_latitude_deg, max_latitude_deg);
        const double lon_rad = geo.longitude_deg * pi / 180.0;
        const double lat_rad = lat * pi / 180.0;
        return {
            earth_radius_m * lon_rad,
            earth_radius_m * std::log(std::tan((pi * 0.25) + (lat_rad * 0.5)))
        };
    }

    [[nodiscard]] static GeoCoordinate unproject(WorldMeters world) noexcept {
        const double lon_rad = world.x / earth_radius_m;
        const double lat_rad = (2.0 * std::atan(std::exp(world.y / earth_radius_m))) - (pi * 0.5);
        return {
            lon_rad * 180.0 / pi,
            std::clamp(lat_rad * 180.0 / pi, -max_latitude_deg, max_latitude_deg)
        };
    }

    [[nodiscard]] static double haversine_distance_m(GeoCoordinate a, GeoCoordinate b) noexcept {

        const double dlat = (b.latitude_deg - a.latitude_deg) * (pi / 180.0);
        const double dlon = (b.longitude_deg - a.longitude_deg) * (pi / 180.0);
        const double lat1 = a.latitude_deg * (pi / 180.0);
        const double lat2 = b.latitude_deg * (pi / 180.0);
        const double sin_half_dlat = std::sin(dlat * 0.5);
        const double sin_half_dlon = std::sin(dlon * 0.5);
        const double h = sin_half_dlat * sin_half_dlat +
                         std::cos(lat1) * std::cos(lat2) * sin_half_dlon * sin_half_dlon;
        const double c = 2.0 * std::asin(std::sqrt(std::clamp(h, 0.0, 1.0)));
        return earth_radius_m * c;
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
