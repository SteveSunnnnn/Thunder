#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace thunder {

struct SupplyHub {
    ProvinceId province{};
    CountryId owner{};
    std::int32_t capacity_units = 1000;
    std::int32_t current_stock_units = 1000;
    std::int32_t railway_level = 1;
};

struct SupplyConnection {
    ProvinceId from{};
    ProvinceId to{};
    std::int32_t throughput_capacity = 500;
    bool is_maritime = false;
    float efficiency_factor = 1.0f; // Drops under convoy raiding
};

class LogisticsNetwork {
public:
    LogisticsNetwork() = default;

    void add_supply_hub(SupplyHub hub);
    void add_connection(SupplyConnection conn);

    // Naval convoy raiding interception reducing maritime throughput
    void apply_convoy_raiding(ProvinceId sea_node, float raiding_intensity);

    // Calculates supply availability at a target frontline province (returns factor in [0.0, 1.0])
    [[nodiscard]] float calculate_frontline_supply_factor(ProvinceId frontline_prov) const noexcept;

    [[nodiscard]] std::size_t hub_count() const noexcept { return hubs_.size(); }
    [[nodiscard]] std::size_t connection_count() const noexcept { return connections_.size(); }

private:
    std::vector<SupplyHub> hubs_;
    std::vector<SupplyConnection> connections_;
};

} // namespace thunder
