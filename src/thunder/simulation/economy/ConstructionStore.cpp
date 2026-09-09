#include "thunder/simulation/economy/ConstructionStore.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace thunder {

ConstructionProjectId ConstructionStore::enqueue(ConstructionProjectInit init) {
    if (next_id_ == ConstructionProjectId::invalid_value)
        throw std::overflow_error("construction project id sequence exhausted");
    ConstructionProjectRecord rec;
    rec.id = ConstructionProjectId{next_id_};
    ++next_id_;
    rec.country = init.country;
    rec.province = init.province;
    rec.target_building = init.target_building;
    rec.kind = init.kind;
    rec.target_pm = init.target_pm;
    rec.monument_key_hash = init.monument_key_hash;
    rec.total_points_required = std::max(1u, init.total_points_required);
    rec.total_cost_milli = init.total_cost_milli;
    rec.progress_points = 0;
    rec.paid_cost_milli = 0;
    rec.weekly_progress_ppm = 0;
    rec.paused = false;

    // Assign priority at the tail of the country's queue
    std::uint32_t max_pri = 0;
    for (const auto& p : projects_) {
        if (p.country == init.country) {
            if (p.priority == std::numeric_limits<std::uint32_t>::max())
                throw std::overflow_error("construction priority sequence exhausted");
            max_pri = std::max(max_pri, static_cast<std::uint32_t>(p.priority + 1u));
        }
    }
    rec.priority = max_pri;
    projects_.push_back(rec);
    return rec.id;
}

ConstructionProjectId ConstructionStore::enqueue_expansion(CountryId country, BuildingId building,
                                                           std::uint32_t points,
                                                           EconomyAmount cost_milli) {
    ConstructionProjectInit init;
    init.country = country;
    init.target_building = building;
    init.kind = ConstructionKind::ExpandBuilding;
    init.total_points_required = points == 0 ? 100u : points;
    init.total_cost_milli = cost_milli;
    return enqueue(init);
}

ConstructionProjectId ConstructionStore::enqueue_pm_upgrade(CountryId country, BuildingId building,
                                                            ProductionMethodId target_pm,
                                                            std::uint32_t points,
                                                            EconomyAmount cost_milli) {
    ConstructionProjectInit init;
    init.country = country;
    init.target_building = building;
    init.kind = ConstructionKind::UpgradeProductionMethod;
    init.target_pm = target_pm;
    init.total_points_required = points == 0 ? 50u : points;
    init.total_cost_milli = cost_milli;
    return enqueue(init);
}

ConstructionProjectId ConstructionStore::enqueue_monument(CountryId country, ProvinceId province,
                                                          std::string_view monument_key,
                                                          std::uint32_t points,
                                                          EconomyAmount cost_milli) {
    ConstructionProjectInit init;
    init.country = country;
    init.province = province;
    init.kind = ConstructionKind::ConstructMonument;
    init.monument_key_hash = economy_stable_key(monument_key);
    init.total_points_required = points == 0 ? 500u : points;
    init.total_cost_milli = cost_milli;
    return enqueue(init);
}

bool ConstructionStore::cancel(ConstructionProjectId id) {
    const auto it = std::find_if(projects_.begin(), projects_.end(),
                                 [id](const auto& p) { return p.id == id; });
    if (it != projects_.end()) {
        projects_.erase(it);
        return true;
    }
    return false;
}

bool ConstructionStore::set_paused(ConstructionProjectId id, bool paused) {
    for (auto& p : projects_) {
        if (p.id == id) {
            p.paused = paused;
            return true;
        }
    }
    return false;
}

bool ConstructionStore::move_up(ConstructionProjectId id) {
    for (std::size_t i = 0; i < projects_.size(); ++i) {
        if (projects_[i].id == id) {
            // Find preceding project in the same country
            for (std::size_t j = i; j > 0; --j) {
                if (projects_[j - 1].country == projects_[i].country) {
                    std::swap(projects_[i].priority, projects_[j - 1].priority);
                    std::swap(projects_[i], projects_[j - 1]);
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

bool ConstructionStore::move_down(ConstructionProjectId id) {
    for (std::size_t i = 0; i < projects_.size(); ++i) {
        if (projects_[i].id == id) {
            // Find succeeding project in the same country
            for (std::size_t j = i + 1; j < projects_.size(); ++j) {
                if (projects_[j].country == projects_[i].country) {
                    std::swap(projects_[i].priority, projects_[j].priority);
                    std::swap(projects_[i], projects_[j]);
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

const ConstructionProjectRecord* ConstructionStore::find(ConstructionProjectId id) const noexcept {
    for (const auto& p : projects_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

std::vector<ConstructionProjectId> ConstructionStore::country_queue(CountryId country) const {
    std::vector<const ConstructionProjectRecord*> subset;
    for (const auto& p : projects_) {
        if (p.country == country) subset.push_back(&p);
    }
    std::sort(subset.begin(), subset.end(), [](const auto* a, const auto* b) {
        return a->priority < b->priority;
    });
    std::vector<ConstructionProjectId> result;
    result.reserve(subset.size());
    for (const auto* p : subset) result.push_back(p->id);
    return result;
}

std::uint32_t ConstructionStore::pm_transition_progress_ppm(BuildingId building) const noexcept {
    for (const auto& p : projects_) {
        if (p.target_building == building && p.kind == ConstructionKind::UpgradeProductionMethod) {
            if (p.total_points_required == 0) return 1'000'000;
            return static_cast<std::uint32_t>(
                mul_div_nonnegative(p.progress_points, 1'000'000, p.total_points_required));
        }
    }
    return 1'000'000; // Not transitioning, 100% stable
}

EconomyAmount ConstructionStore::country_weekly_construction_capacity(CountryId country,
                                                                      const World& world) const {
    if (!country.valid() || static_cast<std::size_t>(country.value()) >= world.countries.size()) {
        return 10;
    }
    // Base capacity = 10 construction points per week
    EconomyAmount points = 10;
    const double gdp_val = world.countries.gdp(country);
    // +1 point per 50 units GDP
    points += static_cast<EconomyAmount>(gdp_val / 50.0);
    // +1 point per 10,000 investment pool funds
    const auto pool_cash = world.grand_strategy.investment_pool_cash(country);
    if (pool_cash > 0) {
        points += std::min<EconomyAmount>(50, pool_cash / 10'000);
    }
    return std::clamp<EconomyAmount>(points, 5, 500);
}

JobDispatchStats ConstructionStore::tick_weekly(World& world) {
    return tick_weekly(world, {});
}

namespace {
constexpr std::uint32_t kConstructionBasketCap = 64u;
} // namespace

JobDispatchStats ConstructionStore::tick_weekly(World& world, std::span<const RecipeFlow> construction_basket) {
    if (projects_.empty()) return JobDispatchStats{};
    const auto basket = construction_basket.size() > kConstructionBasketCap
        ? construction_basket.subspan(0, kConstructionBasketCap) : construction_basket;

    const std::size_t num_countries = world.countries.size();
    std::vector<std::size_t> completed_indices;

    for (std::size_t ci = 0; ci < num_countries; ++ci) {
        const CountryId country{static_cast<CountryId::rep_type>(ci)};
        auto remaining_capacity = static_cast<std::uint32_t>(
            country_weekly_construction_capacity(country, world));

        for (std::size_t pi = 0; pi < projects_.size() && remaining_capacity > 0; ++pi) {
            auto& p = projects_[pi];
            if (p.country != country || p.paused) continue;

            const auto needed = p.total_points_required > p.progress_points
                ? p.total_points_required - p.progress_points
                : 0u;
            if (needed == 0) continue;

            // Maximum points this project can take in a single week (cap at 25 points to allow queue parallelism)
            const auto allocated_points = std::min({remaining_capacity, needed, 25u});
            if (allocated_points == 0) continue;

            // Calculate weekly capital and material goods cost
            const EconomyAmount weekly_cost = p.total_cost_milli > 0
                ? mul_div_nonnegative(p.total_cost_milli, allocated_points, p.total_points_required)
                : static_cast<EconomyAmount>(allocated_points * 500);

            // R5: resolve the market that supplies this project's materials.
            MarketId supply_market{};
            if (p.target_building.valid() &&
                static_cast<std::size_t>(p.target_building.value()) < world.buildings.size() &&
                world.buildings.slot_pool().is_index_alive(p.target_building.value())) {
                supply_market = world.buildings.market(p.target_building);
            } else if (p.province.valid() && p.province.value() < world.geography.province_count()) {
                const auto prov_state = world.geography.province_state(p.province);
                if (prov_state.valid()) supply_market = world.geography.state_market(prov_state);
            }
            const bool has_supply_market = supply_market.valid() &&
                static_cast<std::size_t>(supply_market.value()) < world.markets.size();

            // Material requirement for the allocated points (Leontief: every
            // basket good is needed in fixed proportion; the scarcest one
            // throttles progress).
            struct GoodsLeg { std::size_t good; EconomyAmount required; EconomyAmount drawn; EconomyAmount price; };
            GoodsLeg legs[kConstructionBasketCap];
            std::size_t leg_count = 0;
            EconomyAmount goods_value_required = 0;
            if (has_supply_market && !basket.empty()) {
                const auto prices = world.markets.price_row(supply_market);
                for (const auto& flow : basket) {
                    const auto gi = static_cast<std::size_t>(flow.good.value());
                    if (gi >= prices.size()) continue;
                    const auto required = mul_div_nonnegative(
                        flow.quantity_milli_per_1000_workers,
                        static_cast<EconomyAmount>(allocated_points), 1);
                    if (required <= 0 || leg_count >= kConstructionBasketCap) continue;
                    legs[leg_count++] = {gi, required, 0, prices[gi]};
                    goods_value_required = saturating_add(goods_value_required,
                        mul_div_nonnegative(required, prices[gi], economy_scale));
                }
            }
            const EconomyAmount outlay_required = saturating_add(weekly_cost, goods_value_required);

            // Attempt funding: the project's own contractor cash first (the
            // escrowed seed for expansion projects), then the domestic
            // investment pool, then the treasury.
            EconomyAmount funded = 0;
            EconomyAmount from_building = 0;
            EconomyAmount from_pool = 0;
            if (p.target_building.valid() &&
                static_cast<std::size_t>(p.target_building.value()) < world.buildings.size() &&
                world.buildings.slot_pool().is_index_alive(p.target_building.value())) {
                auto building_cash = world.buildings.cash_mut();
                const auto avail = std::max<EconomyAmount>(0, saturating_sub(
                    building_cash[p.target_building.value()], building_credit_limit_milli));
                from_building = std::min(outlay_required, avail);
                if (from_building > 0) {
                    building_cash[p.target_building.value()] =
                        saturating_sub(building_cash[p.target_building.value()], from_building);
                    funded += from_building;
                }
            }
            from_pool = world.grand_strategy.withdraw_investment_pool_funds(
                country, saturating_sub(outlay_required, funded));
            funded = saturating_add(funded, from_pool);
            if (funded < outlay_required) {
                const auto rem_cost = outlay_required - funded;
                const auto treasury_avail = std::max<EconomyAmount>(0, world.countries.treasury_milli(country));
                const auto from_treasury = std::min(rem_cost, treasury_avail);
                if (from_treasury > 0) {
                    world.countries.add_treasury_milli(country, -from_treasury);
                    funded += from_treasury;
                }
            }
            const std::uint32_t fund_ratio_ppm = outlay_required > 0
                ? static_cast<std::uint32_t>(std::min<EconomyAmount>(
                    ppm_scale, mul_div_nonnegative(funded, ppm_scale, outlay_required)))
                : static_cast<std::uint32_t>(ppm_scale);

            // Draw materials proportionally to funding, then throttle by the
            // scarcest physical leg.
            std::uint32_t goods_ratio_ppm = static_cast<std::uint32_t>(ppm_scale);
            EconomyAmount goods_value_drawn = 0;
            if (has_supply_market && leg_count > 0) {
                auto inventory = world.markets.inventory_row(supply_market);
                for (std::size_t li = 0; li < leg_count; ++li) {
                    auto& leg = legs[li];
                    const auto planned = mul_div_nonnegative(leg.required, fund_ratio_ppm, ppm_scale);
                    const auto drawn = std::min(planned, std::max<EconomyAmount>(0, inventory[leg.good]));
                    leg.drawn = drawn;
                    inventory[leg.good] = saturating_sub(inventory[leg.good], drawn);
                    goods_value_drawn = saturating_add(goods_value_drawn,
                        mul_div_nonnegative(drawn, leg.price, economy_scale));
                    if (leg.required > 0) {
                        const auto leg_ratio = static_cast<std::uint32_t>(std::min<EconomyAmount>(
                            ppm_scale, mul_div_nonnegative(drawn, ppm_scale, leg.required)));
                        goods_ratio_ppm = std::min(goods_ratio_ppm, leg_ratio);
                    }
                }
                // Sellers are paid market price into the clearing account.
                if (goods_value_drawn > 0)
                    world.markets.add_clearing_cash(supply_market, goods_value_drawn);
            }
            const std::uint32_t progress_ratio_ppm = std::min(fund_ratio_ppm, goods_ratio_ppm);

            // Refund the unfunded remainder: only the actually consumed money
            // stays sunk; escrow first, then pool, then treasury.
            const EconomyAmount money_needed = mul_div_nonnegative(weekly_cost, progress_ratio_ppm, ppm_scale);
            const EconomyAmount actual_outlay = saturating_add(money_needed, goods_value_drawn);
            if (funded > actual_outlay) {
                auto refund = funded - actual_outlay;
                if (refund > 0 && from_building > 0) {
                    const auto leg = std::min(refund, from_building);
                    world.buildings.add_cash(p.target_building, leg);
                    refund -= leg;
                }
                if (refund > 0 && from_pool > 0) {
                    const auto leg = std::min(refund, from_pool);
                    world.grand_strategy.add_investment_pool_funds(country, leg);
                    refund -= leg;
                }
                if (refund > 0) world.countries.add_treasury_milli(country, refund);
                funded = actual_outlay;
            }

            // Advance progress proportionally to actual funding AND materials.
            const auto actual_points = static_cast<std::uint32_t>(
                mul_div_nonnegative(allocated_points, progress_ratio_ppm, ppm_scale));
            if (actual_points > 0) {
                p.progress_points += actual_points;
                p.paid_cost_milli = saturating_add(p.paid_cost_milli, funded);
                p.weekly_progress_ppm = static_cast<std::uint32_t>(
                    mul_div_nonnegative(actual_points, 1'000'000, p.total_points_required));
            }
            remaining_capacity -= actual_points;

            // If project is complete, execute transformation
            if (p.progress_points >= p.total_points_required) {
                switch (p.kind) {
                case ConstructionKind::ExpandBuilding: {
                    if (p.target_building.valid() &&
                        static_cast<std::size_t>(p.target_building.value()) < world.buildings.size() &&
                        world.buildings.slot_pool().is_index_alive(p.target_building.value())) {
                        const auto cur_lvl = world.buildings.level(p.target_building);
                        // A completed expansion must never wrap a saturated
                        // level back to zero. Treat max-level buildings as a
                        // no-op while still consuming the queued project.
                        if (cur_lvl < std::numeric_limits<std::uint16_t>::max())
                            world.buildings.set_level(
                                p.target_building, static_cast<std::uint16_t>(cur_lvl + 1u));
                    }
                    break;
                }
                case ConstructionKind::UpgradeProductionMethod: {
                    if (p.target_building.valid() &&
                        static_cast<std::size_t>(p.target_building.value()) < world.buildings.size() &&
                        world.buildings.slot_pool().is_index_alive(p.target_building.value()) &&
                        p.target_pm.valid()) {
                        world.buildings.set_production_method(p.target_building, p.target_pm);
                    }
                    break;
                }
                case ConstructionKind::ConstructMonument: {
                    world.countries.add_prestige(p.country, 25.0);
                    world.countries.set_gdp(p.country, world.countries.gdp(p.country) + 50.0);
                    break;
                }
                }
                completed_indices.push_back(pi);
            }
        }
    }

    // Remove completed projects in reverse order
    std::sort(completed_indices.rbegin(), completed_indices.rend());
    for (const auto idx : completed_indices) {
        if (idx < projects_.size()) {
            projects_.erase(projects_.begin() + idx);
        }
    }

    return JobDispatchStats{};
}

std::uint64_t ConstructionStore::checksum() const noexcept {
    Fnv1a64 h;
    h.add(projects_.size());
    for (const auto& p : projects_) {
        h.add(p.id.value());
        h.add(p.country.value());
        h.add(p.province.value());
        h.add(p.target_building.value());
        h.add(static_cast<std::uint8_t>(p.kind));
        h.add(p.target_pm.value());
        h.add(p.monument_key_hash);
        h.add(p.progress_points);
        h.add(p.total_points_required);
        h.add(p.weekly_progress_ppm);
        h.add(p.total_cost_milli);
        h.add(p.paid_cost_milli);
        h.add(p.paused ? 1u : 0u);
        h.add(p.priority);
    }
    return h.value();
}

bool ConstructionStore::validate(const World& world) const {
    constexpr std::size_t max_projects = 100'000u;
    if (projects_.size() > max_projects) return false;

    std::unordered_set<std::uint32_t> ids;
    ids.reserve(projects_.size());
    for (const auto& p : projects_) {
        if (!p.id.valid() || !ids.insert(p.id.value()).second) return false;
        if (next_id_ != ConstructionProjectId::invalid_value && p.id.value() >= next_id_)
            return false;
        if (!p.country.valid() || p.country.value() >= world.countries.size()) return false;
        if (p.province.valid() && p.province.value() >= world.geography.province_count()) return false;
        // A zero total cost is the public API's sentinel for the default
        // per-point construction price (500 milli-units/point), so paid cost
        // can legitimately be non-zero even when the serialized total is 0.
        if (p.total_points_required == 0u || p.progress_points > p.total_points_required ||
            p.weekly_progress_ppm > 1'000'000u || p.total_cost_milli < 0 ||
            p.paid_cost_milli < 0 ||
            (p.total_cost_milli > 0 && p.paid_cost_milli > p.total_cost_milli))
            return false;

        switch (p.kind) {
        case ConstructionKind::ExpandBuilding:
            if (!p.target_building.valid() || p.target_building.value() >= world.buildings.size() ||
                !world.buildings.slot_pool().is_index_alive(p.target_building.value()) ||
                p.target_pm.valid() || p.monument_key_hash != 0u)
                return false;
            break;
        case ConstructionKind::UpgradeProductionMethod:
            if (!p.target_building.valid() || p.target_building.value() >= world.buildings.size() ||
                !world.buildings.slot_pool().is_index_alive(p.target_building.value()) ||
                !p.target_pm.valid() || p.monument_key_hash != 0u)
                return false;
            break;
        case ConstructionKind::ConstructMonument:
            if (!p.province.valid() || p.province.value() >= world.geography.province_count() ||
                p.monument_key_hash == 0u || p.target_building.valid() || p.target_pm.valid())
                return false;
            break;
        default:
            return false;
        }
    }
    return true;
}

std::size_t ConstructionStore::memory_bytes() const noexcept {
    return sizeof(ConstructionStore) + projects_.capacity() * sizeof(ConstructionProjectRecord);
}

void ConstructionStore::clear() noexcept {
    projects_.clear();
    next_id_ = 0;
}

void ConstructionStore::restore_project(const ConstructionProjectRecord& rec) {
    if (!rec.id.valid()) throw std::invalid_argument("invalid construction project id");
    if (std::find_if(projects_.begin(), projects_.end(), [&](const auto& p) {
            return p.id == rec.id;
        }) != projects_.end())
        throw std::invalid_argument("duplicate construction project id");
    projects_.push_back(rec);
    if (rec.id.value() == ConstructionProjectId::invalid_value - 1u) {
        next_id_ = ConstructionProjectId::invalid_value;
    } else if (rec.id.value() >= next_id_) {
        next_id_ = rec.id.value() + 1;
    }
}

} // namespace thunder
