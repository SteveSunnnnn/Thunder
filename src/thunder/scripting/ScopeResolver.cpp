#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"

namespace thunder {

bool ScopeResolver::valid(const World& w, ScopeRef s) noexcept {
    switch (s.type) {
        case ScopeType::Country: return static_cast<std::size_t>(s.raw_id) < w.countries.size();
        case ScopeType::State: return static_cast<std::size_t>(s.raw_id) < w.geography.state_count();
        case ScopeType::Province: return static_cast<std::size_t>(s.raw_id) < w.geography.province_count();
        case ScopeType::Pop:
            return static_cast<std::size_t>(s.raw_id) < w.pops.size() &&
                w.pops.slot_pool().is_index_alive(s.raw_id);
        case ScopeType::Market: return static_cast<std::size_t>(s.raw_id) < w.markets.size();
        case ScopeType::Building:
            return static_cast<std::size_t>(s.raw_id) < w.buildings.size() &&
                w.buildings.slot_pool().is_index_alive(s.raw_id);
        case ScopeType::Army:
            return static_cast<std::size_t>(s.raw_id) < w.grand_strategy.armys().size();
        case ScopeType::Front:
            return static_cast<std::size_t>(s.raw_id) < w.grand_strategy.fronts().size();
        case ScopeType::War:
            return static_cast<std::size_t>(s.raw_id) < w.grand_strategy.wars().size();
        default: return false;
    }
}

ScopeRef ScopeResolver::owner(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    switch (s.type) {
        case ScopeType::Country: return s;
        case ScopeType::State: return ScopeRef::country(w.geography.state_owner(StateId{s.raw_id}));
        case ScopeType::Province: return ScopeRef::country(w.geography.province_owner(ProvinceId{s.raw_id}));
        case ScopeType::Market: return ScopeRef::country(w.markets.owner(MarketId{s.raw_id}));
        case ScopeType::Building: return ScopeRef::country(w.markets.owner(w.buildings.market(BuildingId{s.raw_id})));
        case ScopeType::Army:
            return ScopeRef::country(w.grand_strategy.armys()[s.raw_id].country);
        case ScopeType::Pop: {
            const auto p = w.pops.province(PopId{s.raw_id});
            if (p.valid()) return ScopeRef::country(w.geography.province_owner(p));
            const auto m = w.pops.market(PopId{s.raw_id});
            return m.valid() ? ScopeRef::country(w.markets.owner(m)) : ScopeRef{};
        }
        default: return {};
    }
}

ScopeRef ScopeResolver::market(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    switch (s.type) {
        case ScopeType::Market: return s;
        case ScopeType::State: return ScopeRef::market(w.geography.state_market(StateId{s.raw_id}));
        case ScopeType::Province: return ScopeRef::market(w.geography.province_market(ProvinceId{s.raw_id}));
        case ScopeType::Pop: return ScopeRef::market(w.pops.market(PopId{s.raw_id}));
        case ScopeType::Building: return ScopeRef::market(w.buildings.market(BuildingId{s.raw_id}));
        default: return {};
    }
}

ScopeRef ScopeResolver::state(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    switch (s.type) {
        case ScopeType::State: return s;
        case ScopeType::Province: return ScopeRef::state(w.geography.province_state(ProvinceId{s.raw_id}));
        case ScopeType::Pop: {
            const auto p = w.pops.province(PopId{s.raw_id});
            return p.valid() ? ScopeRef::state(w.geography.province_state(p)) : ScopeRef{};
        }
        case ScopeType::Army:
            return ScopeRef::state(w.grand_strategy.armys()[s.raw_id].location);
        case ScopeType::Front:
            return ScopeRef::state(w.grand_strategy.fronts()[s.raw_id].state);
        default: return {};
    }
}

ScopeRef ScopeResolver::province(const World& w, ScopeRef s) noexcept {
    if (!valid(w, s)) return {};
    if (s.type == ScopeType::Province) return s;
    if (s.type == ScopeType::Pop) {
        const auto id = w.pops.province(PopId{s.raw_id});
        return id.valid() ? ScopeRef::province(id) : ScopeRef{};
    }
    if (s.type == ScopeType::Building) {
        const auto id = w.buildings.province(BuildingId{s.raw_id});
        return id.valid() ? ScopeRef::province(id) : ScopeRef{};
    }
    return {};
}

std::vector<ScopeRef> ScopeResolver::all(const World& w, ScopeType t) {
    std::size_t n = 0;
    switch (t) {
        case ScopeType::Country: n = w.countries.size(); break;
        case ScopeType::State: n = w.geography.state_count(); break;
        case ScopeType::Province: n = w.geography.province_count(); break;
        case ScopeType::Pop: n = w.pops.size(); break;
        case ScopeType::Market: n = w.markets.size(); break;
        case ScopeType::Building: n = w.buildings.size(); break;
        case ScopeType::Army: n = w.grand_strategy.armys().size(); break;
        case ScopeType::Front: n = w.grand_strategy.fronts().size(); break;
        case ScopeType::War: n = w.grand_strategy.wars().size(); break;
        default: break;
    }
    std::vector<ScopeRef> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (t == ScopeType::Pop &&
            !w.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        if (t == ScopeType::Building &&
            !w.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        out.push_back({t, static_cast<std::uint32_t>(i)});
    }
    return out;
}

std::vector<ScopeRef> ScopeResolver::children(const World& w, ScopeRef s, ScopeType target) {
    std::vector<ScopeRef> out;
    if (!valid(w, s) || target == ScopeType::None) return out;
    // Same-type iterators are world-global (Clausewitz semantics): from inside
    // a country scope `every_country` still enumerates ALL countries, not the
    // current one. Scoped variants are expressed by navigation (`owner =`,
    // state/province selectors) plus `limit = { ... }` gating.
    if (s.type == target) {
        return all(w, target);
    }

    if (s.type == ScopeType::Country) {
        const auto c = CountryId{s.raw_id};
        if (target == ScopeType::State) {
            for (std::size_t i = 0; i < w.geography.state_count(); ++i) {
                const auto id = StateId{static_cast<std::uint32_t>(i)};
                if (w.geography.state_owner(id) == c) out.push_back(ScopeRef::state(id));
            }
        } else if (target == ScopeType::Province) {
            for (std::size_t i = 0; i < w.geography.province_count(); ++i) {
                const auto id = ProvinceId{static_cast<std::uint32_t>(i)};
                if (w.geography.province_owner(id) == c) out.push_back(ScopeRef::province(id));
            }
        } else if (target == ScopeType::Market) {
            for (std::size_t i = 0; i < w.markets.size(); ++i) {
                const auto id = MarketId{static_cast<std::uint32_t>(i)};
                if (w.markets.owner(id) == c) out.push_back(ScopeRef::market(id));
            }
        }
    }

    if (s.type == ScopeType::State && target == ScopeType::Province) {
        const auto st = StateId{s.raw_id};
        for (std::size_t i = 0; i < w.geography.province_count(); ++i) {
            const auto id = ProvinceId{static_cast<std::uint32_t>(i)};
            if (w.geography.province_state(id) == st) out.push_back(ScopeRef::province(id));
        }
    }

    // Buildings are owned through their market's country; province/state links
    // come from the cold column. Only live slots are candidates.
    if (target == ScopeType::Building) {
        for (std::size_t i = 0; i < w.buildings.size(); ++i) {
            if (!w.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
            const auto b = BuildingId{static_cast<std::uint32_t>(i)};
            bool match = false;
            if (s.type == ScopeType::Market) {
                match = w.buildings.market(b) == MarketId{s.raw_id};
            } else if (s.type == ScopeType::Province) {
                match = w.buildings.province(b) == ProvinceId{s.raw_id};
            } else if (s.type == ScopeType::State) {
                const auto pr = w.buildings.province(b);
                match = pr.valid() && w.geography.province_state(pr) == StateId{s.raw_id};
            } else if (s.type == ScopeType::Country) {
                const auto market = w.buildings.market(b);
                match = market.valid() && w.markets.owner(market) == CountryId{s.raw_id};
            }
            if (match) out.push_back(ScopeRef::building(b));
        }
    }

    // Military/diplomatic domains (dense id-indexed record vectors).
    if (target == ScopeType::Army) {
        const auto& armys = w.grand_strategy.armys();
        for (std::size_t i = 0; i < armys.size(); ++i) {
            const auto& army = armys[i];
            bool match = false;
            if (s.type == ScopeType::Country) {
                match = army.country == CountryId{s.raw_id};
            } else if (s.type == ScopeType::State) {
                match = army.location == StateId{s.raw_id};
            }
            if (match) out.push_back(ScopeRef::army(ArmyId{static_cast<std::uint32_t>(i)}));
        }
    }
    if (target == ScopeType::Front) {
        const auto& fronts = w.grand_strategy.fronts();
        for (std::size_t i = 0; i < fronts.size(); ++i) {
            const auto& front = fronts[i];
            bool match = false;
            if (s.type == ScopeType::Country) {
                match = front.first == CountryId{s.raw_id} || front.second == CountryId{s.raw_id};
            } else if (s.type == ScopeType::War) {
                // Mirror the warfare weekly pass: a front participates in a war
                // when its country pair matches attacker/defender in either
                // order.
                const auto& war = w.grand_strategy.wars()[s.raw_id];
                match = (front.first == war.attacker && front.second == war.defender) ||
                        (front.first == war.defender && front.second == war.attacker);
            }
            if (match) out.push_back(ScopeRef::front(FrontId{static_cast<std::uint32_t>(i)}));
        }
    }
    if (target == ScopeType::War) {
        if (s.type == ScopeType::Country) {
            const auto& wars = w.grand_strategy.wars();
            for (std::size_t i = 0; i < wars.size(); ++i) {
                // Country-scoped iteration covers wars the country can act on:
                // active conflicts only. every_war (global) still sees records.
                if (!wars[i].active) continue;
                if (wars[i].attacker == CountryId{s.raw_id} || wars[i].defender == CountryId{s.raw_id})
                    out.push_back(ScopeRef::war(WarId{static_cast<std::uint32_t>(i)}));
            }
        }
    }

    if (target == ScopeType::Pop) {
        for (std::size_t i = 0; i < w.pops.size(); ++i) {
            if (!w.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
            const auto p = PopId{static_cast<std::uint32_t>(i)};
            bool match = false;
            if (s.type == ScopeType::Province) {
                match = w.pops.province(p) == ProvinceId{s.raw_id};
            } else if (s.type == ScopeType::Market) {
                match = w.pops.market(p) == MarketId{s.raw_id};
            } else if (s.type == ScopeType::State) {
                const auto pr = w.pops.province(p);
                match = pr.valid() && w.geography.province_state(pr) == StateId{s.raw_id};
            } else if (s.type == ScopeType::Country) {
                const auto pr = w.pops.province(p);
                if (pr.valid()) match = w.geography.province_owner(pr) == CountryId{s.raw_id};
                else {
                    const auto m = w.pops.market(p);
                    match = m.valid() && w.markets.owner(m) == CountryId{s.raw_id};
                }
            }
            if (match) out.push_back(ScopeRef::pop(p));
        }
    }
    return out;
}

} // namespace thunder
