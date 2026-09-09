#include "thunder/simulation/economy/BuildingStore.hpp"
#include <algorithm>
#include <stdexcept>

namespace thunder {

void BuildingHotData::reserve(std::size_t count) {
    markets.reserve(count);
    types.reserve(count);
    levels.reserve(count);
    production_methods.reserve(count);
    employees.reserve(count);
    wage_offer_milli.reserve(count);
    cash_milli.reserve(count);
    last_profit_milli.reserve(count);
}

std::size_t BuildingHotData::memory_bytes() const noexcept {
    return markets.capacity() * sizeof(MarketId)
        + types.capacity() * sizeof(BuildingTypeId)
        + levels.capacity() * sizeof(std::uint16_t)
        + production_methods.capacity() * sizeof(ProductionMethodId)
        + employees.capacity() * sizeof(PopulationCount)
        + wage_offer_milli.capacity() * sizeof(EconomyPrice)
        + cash_milli.capacity() * sizeof(EconomyAmount)
        + last_profit_milli.capacity() * sizeof(EconomyAmount);
}

void BuildingColdData::reserve(std::size_t count) {
    provinces.reserve(count);
}

std::size_t BuildingColdData::memory_bytes() const noexcept {
    return provinces.capacity() * sizeof(ProvinceId);
}

void BuildingStore::reserve(std::size_t count) {
    hot_.reserve(count);
    cold_.reserve(count);
    slot_pool_.reserve(count);
}

BuildingId BuildingStore::create(BuildingInit init) {
    const SlotHandle handle = slot_pool_.allocate();
    const auto raw = handle.index;

    if (raw < hot_.markets.size()) {
        hot_.markets[raw] = init.market;
        hot_.types[raw] = init.type;
        hot_.levels[raw] = init.level;
        hot_.production_methods[raw] = init.production_method;
        hot_.employees[raw] = 0u;
        hot_.wage_offer_milli[raw] = init.wage_offer_milli;
        hot_.cash_milli[raw] = init.cash_milli;
        hot_.last_profit_milli[raw] = 0;

        cold_.provinces[raw] = init.province;
    } else {
        hot_.markets.push_back(init.market);
        hot_.types.push_back(init.type);
        hot_.levels.push_back(init.level);
        hot_.production_methods.push_back(init.production_method);
        hot_.employees.push_back(0u);
        hot_.wage_offer_milli.push_back(init.wage_offer_milli);
        hot_.cash_milli.push_back(init.cash_milli);
        hot_.last_profit_milli.push_back(0);

        cold_.provinces.push_back(init.province);
    }

    ++market_membership_revision_;
    ++province_membership_revision_;
    return BuildingId{raw};
}

void BuildingStore::destroy(BuildingId id) {
    const auto i = index(id);
    const SlotHandle h{static_cast<std::uint32_t>(i), slot_pool_.generations()[i]};
    if (!slot_pool_.is_alive(h)) throw std::out_of_range("destroy of dead BuildingId");
    hot_.levels[i] = 0;
    hot_.employees[i] = 0;
    hot_.markets[i] = MarketId{};
    hot_.types[i] = BuildingTypeId{};
    hot_.production_methods[i] = ProductionMethodId{};
    hot_.wage_offer_milli[i] = 0;
    hot_.cash_milli[i] = 0;
    hot_.last_profit_milli[i] = 0;
    cold_.provinces[i] = ProvinceId{};
    slot_pool_.release(h);
    ++market_membership_revision_;
    ++province_membership_revision_;
}

CompactionMap BuildingStore::compact() {
    auto map = build_compaction_map(slot_pool_);
    if (map.is_identity()) return map;

    compact_column(hot_.markets, map);
    compact_column(hot_.types, map);
    compact_column(hot_.levels, map);
    compact_column(hot_.production_methods, map);
    compact_column(hot_.employees, map);
    compact_column(hot_.wage_offer_milli, map);
    compact_column(hot_.cash_milli, map);
    compact_column(hot_.last_profit_milli, map);

    compact_column(cold_.provinces, map);

    apply_compaction(slot_pool_, map);
    ++market_membership_revision_;
    ++province_membership_revision_;
    return map;
}

std::size_t BuildingStore::index(BuildingId id) const {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= size()) throw std::out_of_range("invalid BuildingId");
    if (!slot_pool_.is_index_alive(static_cast<std::uint32_t>(i))) throw std::out_of_range("dead BuildingId");
    return i;
}

MarketId BuildingStore::market(BuildingId id) const { return hot_.markets[index(id)]; }
ProvinceId BuildingStore::province(BuildingId id) const { return cold_.provinces[index(id)]; }
BuildingTypeId BuildingStore::type(BuildingId id) const { return hot_.types[index(id)]; }
std::uint16_t BuildingStore::level(BuildingId id) const { return hot_.levels[index(id)]; }
ProductionMethodId BuildingStore::production_method(BuildingId id) const { return hot_.production_methods[index(id)]; }
PopulationCount BuildingStore::employees(BuildingId id) const { return hot_.employees[index(id)]; }
EconomyPrice BuildingStore::wage_offer(BuildingId id) const { return hot_.wage_offer_milli[index(id)]; }
EconomyAmount BuildingStore::cash(BuildingId id) const { return hot_.cash_milli[index(id)]; }
EconomyAmount BuildingStore::last_profit(BuildingId id) const { return hot_.last_profit_milli[index(id)]; }

void BuildingStore::set_market(BuildingId id, MarketId value) {
    const auto i = index(id);
    if (hot_.markets[i] == value) return;
    hot_.markets[i] = value;
    ++market_membership_revision_;
}

void BuildingStore::set_province(BuildingId id, ProvinceId value) {
    const auto i = index(id);
    if (cold_.provinces[i] == value) return;
    cold_.provinces[i] = value;
    ++province_membership_revision_;
}

void BuildingStore::set_level(BuildingId id, std::uint16_t value) { hot_.levels[index(id)] = value; }
void BuildingStore::set_production_method(BuildingId id, ProductionMethodId value) { hot_.production_methods[index(id)] = value; }
void BuildingStore::set_employees(BuildingId id, PopulationCount value) { hot_.employees[index(id)] = value; }
void BuildingStore::set_wage_offer(BuildingId id, EconomyPrice value) { hot_.wage_offer_milli[index(id)] = value; }
void BuildingStore::add_cash(BuildingId id, EconomyAmount delta) {
    const auto i = index(id);
    hot_.cash_milli[i] = saturating_add(hot_.cash_milli[i], delta);
}
void BuildingStore::set_last_profit(BuildingId id, EconomyAmount value) { hot_.last_profit_milli[index(id)] = value; }

std::uint64_t BuildingStore::checksum() const noexcept {
    Fnv1a64 h;
    const auto n = size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!slot_pool_.is_index_alive(static_cast<std::uint32_t>(i))) { h.add(std::uint32_t{0}); continue; }
        const BuildingId id{static_cast<BuildingId::rep_type>(i)};
        h.add(market(id).value());
        h.add(province(id).value());
        h.add(type(id).value());
        h.add(level(id));
        h.add(production_method(id).value());
        h.add(employees(id));
        h.add(wage_offer(id));
        h.add(cash(id));
        h.add(last_profit(id));
    }
    for (auto w : slot_pool_.bitmap()) h.add(w);
    return h.value();
}

} // namespace thunder
