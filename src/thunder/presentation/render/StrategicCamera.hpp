#pragma once

#include "thunder/foundation/geo/MercatorProjection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace thunder {

struct StrategicCameraState {
    WorldMeters center{};
    double altitude_m = 250'000.0;
    double pitch_deg = 52.0;
    double yaw_deg = 0.0;
    double vertical_fov_deg = 45.0;
    std::uint32_t viewport_width = 1600;
    std::uint32_t viewport_height = 900;
};

class StrategicCamera {
public:
    static constexpr double min_altitude_m = 350.0;
    static constexpr double max_altitude_m = 24'000'000.0;

    StrategicCamera() = default;
    explicit StrategicCamera(StrategicCameraState state) : state_(state) { clamp(); }

    [[nodiscard]] const StrategicCameraState& state() const noexcept { return state_; }
    [[nodiscard]] StrategicCameraState& state() noexcept { return state_; }

    void set_viewport(std::uint32_t width, std::uint32_t height) noexcept;
    void pan_pixels(double dx, double dy) noexcept;
    void zoom_steps(double steps,
                    double focus_x_normalized = 0.0,
                    double focus_y_normalized = 0.0,
                    double minimum_altitude_m = min_altitude_m,
                    double maximum_altitude_m = max_altitude_m) noexcept;

    [[nodiscard]] double ground_meters_per_pixel() const noexcept;
    [[nodiscard]] double zoom_fraction() const noexcept;

private:
    void clamp() noexcept;
    StrategicCameraState state_{};
};

} // namespace thunder
