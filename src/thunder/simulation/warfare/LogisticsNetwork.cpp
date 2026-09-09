#include "thunder/simulation/warfare/LogisticsNetwork.hpp"
#include <algorithm>

namespace thunder {

void LogisticsNetwork::add_supply_hub(SupplyHub hub) {
    hubs_.push_back(std::move(hub));
}

void LogisticsNetwork::add_connection(SupplyConnection conn) {
    connections_.push_back(std::move(conn));
}

void LogisticsNetwork::apply_convoy_raiding(ProvinceId sea_node, float raiding_intensity) {
    for (auto& conn : connections_) {
        if (conn.is_maritime && (conn.from == sea_node || conn.to == sea_node)) {
            conn.efficiency_factor = std::clamp(conn.efficiency_factor - raiding_intensity, 0.1f, 1.0f);
        }
    }
}

float LogisticsNetwork::calculate_frontline_supply_factor(ProvinceId frontline_prov) const noexcept {
    if (hubs_.empty()) return 0.5f;

    // Check direct hub
    for (const auto& h : hubs_) {
        if (h.province == frontline_prov) {
            return h.current_stock_units > 0 ? 1.0f : 0.4f;
        }
    }

    // Check incoming connection
    float best_efficiency = 0.3f;
    for (const auto& conn : connections_) {
        if (conn.to == frontline_prov || conn.from == frontline_prov) {
            best_efficiency = std::max(best_efficiency, conn.efficiency_factor * 0.9f);
        }
    }

    return std::clamp(best_efficiency, 0.1f, 1.0f);
}

} // namespace thunder
