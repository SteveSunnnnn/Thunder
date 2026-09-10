#include "thunder/presentation/render/StrategicCamera.hpp"

namespace thunder {

namespace {
constexpr double pi = 3.1415926535897932384626433832795;
}

void StrategicCamera::set_viewport(std::uint32_t width, std::uint32_t height) noexcept {
    state_.viewport_width = std::max(width, 1u);
    state_.viewport_height = std::max(height, 1u);
}

double StrategicCamera::ground_meters_per_pixel() const noexcept {
    const double fov_rad = state_.vertical_fov_deg * pi / 180.0;
    const double vertical_span = 2.0 * state_.altitude_m * std::tan(fov_rad * 0.5);
    return vertical_span / static_cast<double>(std::max(state_.viewport_height, 1u));
}

void StrategicCamera::pan_pixels(double dx, double dy) noexcept {
    const double meters_per_pixel = ground_meters_per_pixel();
    const double yaw = state_.yaw_deg * pi / 180.0;
    const double right_x = std::cos(yaw);
    const double right_y = std::sin(yaw);
    const double forward_x = -right_y;
    const double forward_y = right_x;

    state_.center.x -= dx * meters_per_pixel * right_x;
    state_.center.y -= dx * meters_per_pixel * right_y;
    state_.center.x += dy * meters_per_pixel * forward_x;
    state_.center.y += dy * meters_per_pixel * forward_y;
}

void StrategicCamera::zoom_steps(double steps,
                                 double focus_x_normalized,
                                 double focus_y_normalized,
                                 double minimum_altitude,
                                 double maximum_altitude) noexcept {
    const double lower_altitude = std::clamp(
        std::min(minimum_altitude, maximum_altitude), min_altitude_m, max_altitude_m);
    const double upper_altitude = std::clamp(
        std::max(minimum_altitude, maximum_altitude), lower_altitude, max_altitude_m);
    state_.altitude_m = std::clamp(state_.altitude_m, lower_altitude, upper_altitude);
    const double old_mpp = ground_meters_per_pixel();
    const double factor = std::exp(-steps * 0.16);
    state_.altitude_m = std::clamp(state_.altitude_m * factor,
                                   lower_altitude, upper_altitude);
    const double new_mpp = ground_meters_per_pixel();

    // Keep the approximate ground point under the cursor stable while zooming.
    // The offset must be derived from the *effective* clamped zoom.  Otherwise
    // repeated wheel input at a game-specific zoom limit continues panning the
    // map even though the altitude cannot change.
    const double half_width = static_cast<double>(state_.viewport_width) * 0.5;
    const double half_height = static_cast<double>(state_.viewport_height) * 0.5;
    const double dx_pixels = focus_x_normalized * half_width;
    const double dy_pixels = focus_y_normalized * half_height;
    const double delta = old_mpp - new_mpp;
    const double yaw = state_.yaw_deg * pi / 180.0;
    const double right_x = std::cos(yaw);
    const double right_y = std::sin(yaw);
    const double forward_x = -right_y;
    const double forward_y = right_x;

    state_.center.x += dx_pixels * delta * right_x;
    state_.center.y += dx_pixels * delta * right_y;
    state_.center.x -= dy_pixels * delta * forward_x;
    state_.center.y -= dy_pixels * delta * forward_y;
}

double StrategicCamera::zoom_fraction() const noexcept {
    const double log_min = std::log(min_altitude_m);
    const double log_max = std::log(max_altitude_m);
    const double alt = std::clamp(state_.altitude_m, min_altitude_m, max_altitude_m);
    return (std::log(alt) - log_min) / (log_max - log_min);
}

void StrategicCamera::clamp() noexcept {
    state_.altitude_m = std::clamp(state_.altitude_m, min_altitude_m, max_altitude_m);
    state_.pitch_deg = std::clamp(state_.pitch_deg, 25.0, 88.0);
    state_.vertical_fov_deg = std::clamp(state_.vertical_fov_deg, 25.0, 80.0);
    state_.viewport_width = std::max(state_.viewport_width, 1u);
    state_.viewport_height = std::max(state_.viewport_height, 1u);
}

} // namespace thunder
