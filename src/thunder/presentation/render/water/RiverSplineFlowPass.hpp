#pragma once

#include "thunder/presentation/render/PhysicalLighting.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct RiverControlPoint {
    Vec3 position{};
    float width_m = 20.0f;
    float depth_m = 3.0f;
    float flow_speed_mps = 2.0f;
};

struct RiverVertexGpu {
    Vec3 position;
    Vec3 normal;
    Vec3 flow_vector; // Direction & speed of water flow
    float river_uv_u; // U across river width [0..1]
    float river_uv_v; // V along river length
    float depth_m;
    float rapids_factor;
};

class RiverSplineFlowPass {
public:
    // Computes water flow direction, velocity, and rapids foam generation along river spline
    [[nodiscard]] static float compute_rapids_factor(float slope_gradient, float flow_speed) noexcept;

    // Evaluates river water surface PBR shading with directional flow map, specular sparkle, and shore foam
    [[nodiscard]] static Vec3 evaluate_river_lighting(
        const Vec3& world_pos,
        const Vec3& base_normal,
        const Vec3& flow_dir,
        float time_s,
        float depth_m,
        float rapids_factor,
        const Vec3& view_dir,
        const Vec3& sun_dir,
        const Vec3& sun_color,
        const Vec3& ambient_color) noexcept;
};

} // namespace thunder
