#include "thunder/presentation/render/flag/DynamicFlag3D.hpp"

#include <algorithm>
#include <cmath>

namespace thunder {

DynamicFlag3D::DynamicFlag3D(DynamicFlag3DConfig config) {
    configure(config);
}

void DynamicFlag3D::configure(DynamicFlag3DConfig config) {
    config.columns = std::clamp<std::uint16_t>(config.columns, 2u, 128u);
    config.rows = std::clamp<std::uint16_t>(config.rows, 2u, 64u);
    config.width = std::clamp(config.width, 0.1f, 8.0f);
    config.height = std::clamp(config.height, 0.1f, 8.0f);
    config.wind_strength = std::clamp(config.wind_strength, 0.0f, 0.5f);
    config.wave_frequency = std::clamp(config.wave_frequency, 0.1f, 32.0f);
    config.wave_speed = std::clamp(config.wave_speed, 0.0f, 12.0f);
    config_ = config;
    rebuild();
}

void DynamicFlag3D::rebuild() {
    vertices_.clear();
    indices_.clear();
    vertices_.reserve(static_cast<std::size_t>(config_.columns) * config_.rows);
    for (std::uint16_t row = 0; row < config_.rows; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(config_.rows - 1u);
        for (std::uint16_t column = 0; column < config_.columns; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(config_.columns - 1u);
            vertices_.push_back({u * config_.width,
                                 (v - 0.5f) * config_.height, 0.0f, u, v});
        }
    }
    indices_.reserve(static_cast<std::size_t>(config_.columns - 1u) *
                     static_cast<std::size_t>(config_.rows - 1u) * 6u);
    for (std::uint16_t row = 0; row + 1u < config_.rows; ++row) {
        for (std::uint16_t column = 0; column + 1u < config_.columns; ++column) {
            const auto a = static_cast<std::uint32_t>(row) * config_.columns + column;
            const auto b = a + 1u;
            const auto c = a + config_.columns;
            const auto d = c + 1u;
            indices_.insert(indices_.end(), {a, c, b, b, c, d});
        }
    }
    cloth_vertex_count_ = vertices_.size();
}

void DynamicFlag3D::update(float time_seconds) noexcept {
    if (!std::isfinite(time_seconds)) time_seconds = 0.0f;
    for (std::size_t index = 0; index < cloth_vertex_count_; ++index) {
        auto& vertex = vertices_[index];
        const float pinned = vertex.u * vertex.u;
        const float phase = vertex.u * config_.wave_frequency -
                            time_seconds * config_.wave_speed + vertex.v * 1.7f;
        const float secondary = std::sin(phase * 0.53f + vertex.v * 4.1f) * 0.32f;
        const float wave = (std::sin(phase) + secondary) * config_.wind_strength * pinned;
        vertex.x = vertex.u * config_.width;
        vertex.y = (vertex.v - 0.5f) * config_.height + wave * 0.16f;
        vertex.z = wave;
    }
}

} // namespace thunder
