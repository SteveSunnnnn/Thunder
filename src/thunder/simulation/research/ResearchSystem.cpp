#include "thunder/simulation/research/ResearchSystem.hpp"

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/economy/EconomicTypes.hpp"
#include "thunder/simulation/grand_strategy/GrandStrategyStore.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {

std::uint64_t stable_key_hash(std::string_view key) noexcept {
    Fnv1a64 hash;
    hash.add(key);
    return hash.value();
}

std::uint64_t saturating_add_u64(std::uint64_t a, std::uint64_t b) noexcept {
    if (a > std::numeric_limits<std::uint64_t>::max() - b)
        return std::numeric_limits<std::uint64_t>::max();
    return a + b;
}

bool valid_country(CountryId id, std::size_t count) noexcept {
    return id.valid() && static_cast<std::size_t>(id.value()) < count;
}

} // namespace

ResearchSystem::ResearchSystem(const ScriptRegistry& registry,
                               const ScriptProgramDatabase* programs)
    : vm_(registry, programs) {
    definition_lookup_.reserve(512u);
}

void ResearchSystem::clear_content() {
    definitions_.clear();
    definition_lookup_.clear();
    finalized_ = false;
}

void ResearchSystem::set_program_database(const ScriptProgramDatabase* programs) noexcept {
    vm_.set_program_database(programs);
}

void ResearchSystem::set_rules(ResearchRules rules) {
    if (rules.max_innovation_milli == 0u)
        throw std::invalid_argument("research max innovation must be positive");
    rules.base_innovation_milli = std::min(rules.base_innovation_milli, rules.max_innovation_milli);
    rules.innovation_per_million_literate_population_milli =
        std::min(rules.innovation_per_million_literate_population_milli, rules.max_innovation_milli);
    // Probabilities and rates are authored in ppm. Clamp them before the
    // weekly additions below so malformed content cannot wrap uint32_t and
    // accidentally create an always/never spreading technology.
    rules.tech_spread_rate_ppm = std::min<std::uint32_t>(
        rules.tech_spread_rate_ppm, 1'000'000u);
    rules.tech_spread_base_chance_ppm = std::min<std::uint32_t>(
        rules.tech_spread_base_chance_ppm, 1'000'000u);
    rules_ = rules;
}

std::uint32_t ResearchSystem::add_or_replace_definition(TechnologyDefinition definition) {
    if (definition.key.empty()) throw std::invalid_argument("technology key must not be empty");
    if (definition.cost_milli == 0u) throw std::invalid_argument("technology cost must be positive");
    const auto computed_hash = stable_key_hash(definition.key);
    if (definition.key_hash == 0u) definition.key_hash = computed_hash;
    if (definition.key_hash != computed_hash)
        throw std::invalid_argument("technology key hash does not match stable key");

    const auto found = definition_lookup_.find(definition.key_hash);
    if (found != definition_lookup_.end()) {
        const auto index = found->second;
        if (definitions_[index].key != definition.key)
            throw std::invalid_argument("technology stable-key hash collision");
        definitions_[index] = std::move(definition);
        finalized_ = false;
        return index;
    }

    if (definitions_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::overflow_error("too many technology definitions");
    const auto index = static_cast<std::uint32_t>(definitions_.size());
    definition_lookup_.emplace(definition.key_hash, index);
    definitions_.push_back(std::move(definition));
    finalized_ = false;
    return index;
}

bool ResearchSystem::finalize_definitions(std::vector<std::string>& diagnostics) {
    bool ok = true;
    for (const auto& definition : definitions_) {
        std::vector<std::uint64_t> seen;
        seen.reserve(definition.prerequisites.size());
        for (const auto prerequisite : definition.prerequisites) {
            if (prerequisite == definition.key_hash) {
                diagnostics.push_back("technology " + definition.key + " depends on itself");
                ok = false;
            } else if (!definition_lookup_.contains(prerequisite)) {
                diagnostics.push_back("technology " + definition.key + " references missing prerequisite");
                ok = false;
            }
            if (std::find(seen.begin(), seen.end(), prerequisite) != seen.end()) {
                diagnostics.push_back("technology " + definition.key + " contains a duplicate prerequisite");
                ok = false;
            } else {
                seen.push_back(prerequisite);
            }
        }
    }

    // Deterministic DFS in definition registration order catches longer cycles.
    std::vector<std::uint8_t> color(definitions_.size(), 0u);
    const auto visit = [&](const auto& self, std::uint32_t index) -> bool {
        if (color[index] == 1u) return false;
        if (color[index] == 2u) return true;
        color[index] = 1u;
        for (const auto prerequisite : definitions_[index].prerequisites) {
            const auto found = definition_lookup_.find(prerequisite);
            if (found != definition_lookup_.end() && !self(self, found->second)) return false;
        }
        color[index] = 2u;
        return true;
    };
    for (std::uint32_t i = 0; i < definitions_.size(); ++i) {
        if (color[i] == 0u && !visit(visit, i)) {
            diagnostics.push_back("technology prerequisite graph contains a cycle at " + definitions_[i].key);
            ok = false;
            break;
        }
    }
    finalized_ = ok;
    return ok;
}

const TechnologyDefinition* ResearchSystem::find(std::uint64_t key_hash) const noexcept {
    const auto found = definition_lookup_.find(key_hash);
    return found == definition_lookup_.end() ? nullptr : &definitions_[found->second];
}

const TechnologyDefinition* ResearchSystem::find(std::string_view key) const noexcept {
    const auto* definition = find(stable_key_hash(key));
    return definition != nullptr && definition->key == key ? definition : nullptr;
}

bool ResearchSystem::completed(const World& world, CountryId country, std::uint64_t key_hash) const noexcept {
    for (const auto& record : world.grand_strategy.technologys()) {
        if (record.country == country && record.key_hash == key_hash && record.unlocked) return true;
    }
    return false;
}

bool ResearchSystem::queued(const World& world, CountryId country, std::uint64_t key_hash) const noexcept {
    for (const auto& record : world.grand_strategy.technologys()) {
        if (record.country == country && record.key_hash == key_hash) return true;
    }
    return false;
}

bool ResearchSystem::potential_passes(const TechnologyDefinition& definition,
                                      const World& world, CountryId country) const {
    if (!definition.potential.has_value()) return true;
    return vm_.evaluate(*definition.potential, world, ScopeRef::country(country));
}

bool ResearchSystem::prerequisites_complete(const TechnologyDefinition& definition,
                                            const World& world, CountryId country) const noexcept {
    for (const auto prerequisite : definition.prerequisites) {
        if (!completed(world, country, prerequisite)) return false;
    }
    return true;
}

bool ResearchSystem::is_era_unlocked(const World& world, CountryId country, std::uint16_t era) const noexcept {
    if (era == 0u) return true;

    std::uint32_t prev_era_completed = 0u;
    std::uint32_t prev_era_total = 0u;
    for (const auto& def : definitions_) {
        if (def.era == era - 1u) {
            ++prev_era_total;
            if (completed(world, country, def.key_hash)) {
                ++prev_era_completed;
            }
        }
    }
    if (prev_era_total == 0u) return true;
    return prev_era_completed >= std::min<std::uint32_t>(prev_era_total, rules_.min_era_techs_required);
}

bool ResearchSystem::enqueue(World& world, CountryId country, std::uint64_t key_hash) {
    if (!finalized_ || !valid_country(country, world.countries.size())) return false;
    const auto* definition = find(key_hash);
    if (definition == nullptr || queued(world, country, key_hash)) return false;
    if (!potential_passes(*definition, world, country)) return false;
    (void)world.grand_strategy.add_technology({country, key_hash, 0u, false});
    return true;
}

bool ResearchSystem::enqueue(World& world, CountryId country, std::string_view key) {
    const auto* definition = find(key);
    return definition != nullptr && enqueue(world, country, definition->key_hash);
}


TechnologyId ResearchSystem::active_research(const World& world, CountryId country) const noexcept {
    const auto records = world.grand_strategy.technologys();
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (record.country != country || record.unlocked) continue;
        const auto* definition = find(record.key_hash);
        if (definition == nullptr || !prerequisites_complete(*definition, world, country) || !is_era_unlocked(world, country, definition->era)) continue;
        // Potential is deliberately not evaluated in this noexcept query. The
        // weekly authoritative path performs that final eligibility check.
        return TechnologyId{static_cast<TechnologyId::rep_type>(i)};
    }
    return {};
}


void ResearchSystem::rebuild_innovation(const World& world) {
    const auto country_count = world.countries.size();
    innovation_milli_.assign(country_count, rules_.base_innovation_milli);
    literate_pop_cache_.assign(country_count, 0u);
    total_pop_cache_.assign(country_count, 0u);
    if (innovation_milli_.empty()) return;

    const auto pop_sizes = world.pops.populations();
    const auto pop_literacy = world.pops.literacy_all();
    const auto pop_provinces = world.pops.provinces();
    const auto pop_markets = world.pops.markets();
    const auto province_owners = world.geography.province_owners();

    for (std::size_t i = 0; i < pop_sizes.size(); ++i) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        CountryId country{};
        const auto province = pop_provinces[i];
        if (province.valid() && static_cast<std::size_t>(province.value()) < province_owners.size()) {
            country = province_owners[province.value()];
        } else {
            const auto market = pop_markets[i];
            if (market.valid() && static_cast<std::size_t>(market.value()) < world.markets.size())
                country = world.markets.owner(market);
        }
        if (!valid_country(country, country_count)) continue;
        const auto c_idx = static_cast<std::size_t>(country.value());
        total_pop_cache_[c_idx] += pop_sizes[i];
        const auto weighted = static_cast<std::uint64_t>(pop_sizes[i]) * pop_literacy[i] / 10'000u;
        literate_pop_cache_[c_idx] = saturating_add_u64(literate_pop_cache_[c_idx], weighted);
    }

    for (std::size_t i = 0; i < country_count; ++i) {
        const auto country = CountryId{static_cast<CountryId::rep_type>(i)};

        // Education institution multiplier (up to +75% innovation boost from Level 5 modern education)
        std::uint32_t edu_bonus_ppm = 0u;
        for (const auto& inst : world.grand_strategy.institutions()) {
            if (inst.country == country && inst.level > 0) {
                edu_bonus_ppm = std::min<std::uint32_t>(750'000u, edu_bonus_ppm + inst.level * 150'000u);
            }
        }

        const auto from_population = detail::mul_div_u64_saturating(
            literate_pop_cache_[i], rules_.innovation_per_million_literate_population_milli,
            1'000'000u, rules_.max_innovation_milli);
        const auto amplified_pop = from_population + (from_population * edu_bonus_ppm / 1'000'000u);

        const auto total = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(rules_.base_innovation_milli) + amplified_pop,
            rules_.max_innovation_milli);
        innovation_milli_[i] = static_cast<std::uint32_t>(total);
    }
}

ResearchTickStats ResearchSystem::run_weekly(World& world, std::uint64_t weekly_tick) {
    ResearchTickStats stats;
    // Preserve the legacy standalone cadence (the spread phase advances the
    // implicit counter) while allowing ThunderEngine to pin both phases to the
    // persisted GameClock week.
    if (weekly_tick != automatic_weekly_tick) weekly_ticks_ = weekly_tick;
    rebuild_innovation(world);
    if (!finalized_) return stats;

    for (std::size_t country_index = 0; country_index < world.countries.size(); ++country_index) {
        const auto country = CountryId{static_cast<CountryId::rep_type>(country_index)};
        const auto technology = active_research(world, country);
        if (!technology.valid()) continue;
        const auto record_index = static_cast<std::size_t>(technology.value());
        auto& record = world.grand_strategy.technologys_[record_index];
        const auto* definition = find(record.key_hash);
        if (definition == nullptr) { ++stats.stalled_countries; continue; }
        if (!potential_passes(*definition, world, country)) { ++stats.stalled_countries; continue; }

        ++stats.countries_with_research;
        auto delta = detail::mul_div_u64_saturating(
            innovation_milli_[country_index], 1'000'000u, definition->cost_milli, 1'000'000u);
        if (delta == 0u) { ++stats.stalled_countries; continue; }

        const auto next = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(record.progress_ppm) + delta, 1'000'000u);
        record.progress_ppm = static_cast<std::uint32_t>(next);
        if (record.progress_ppm != 1'000'000u) continue;

        record.unlocked = true;
        ++stats.completed_technologies;

        // World-First Discovery Prestige Reward
        bool is_world_first = true;
        for (const auto& other_rec : world.grand_strategy.technologys()) {
            if (other_rec.key_hash == record.key_hash && other_rec.country != country && other_rec.unlocked) {
                is_world_first = false;
                break;
            }
        }
        if (is_world_first) {
            world.countries.add_prestige(country, 5.0);
            ++stats.world_first_discoveries;
        }

        if (definition->on_researched.has_value()) {
            (void)vm_.execute_if(*definition->on_researched, world, ScopeRef::country(country));
        }
    }
    return stats;
}

void ResearchSystem::run_tech_spread_weekly(World& world, std::uint64_t weekly_tick) {
    if (!finalized_) return;
    if (weekly_tick == automatic_weekly_tick) {
        if (weekly_ticks_ != automatic_weekly_tick) ++weekly_ticks_;
    } else {
        weekly_ticks_ = weekly_tick;
    }
    const auto country_count = world.countries.size();

    // Ensure caches are up to date
    if (total_pop_cache_.size() != country_count) {
        rebuild_innovation(world);
    }

    for (std::size_t ci = 0; ci < country_count; ++ci) {
        const auto country = CountryId{static_cast<CountryId::rep_type>(ci)};

        // Absorptive capacity (scaled by literacy & education, baseline 100% when no pop demographics)
        std::uint32_t absorptive_capacity_ppm = 1'000'000u;
        if (total_pop_cache_[ci] > 0u) {
            const auto lit_ppm = static_cast<std::uint32_t>((literate_pop_cache_[ci] * 1'000'000ull) / total_pop_cache_[ci]);
            absorptive_capacity_ppm = std::clamp<std::uint32_t>(lit_ppm, 200'000u, 1'000'000u);
        }

        for (const auto& def : definitions_) {
            if (completed(world, country, def.key_hash)) continue;
            // Strict prerequisite check: CANNOT spread if prerequisites are missing!
            if (!prerequisites_complete(def, world, country)) continue;
            // Era gating check: CANNOT spread if earlier eras are not unlocked!
            if (!is_era_unlocked(world, country, def.era)) continue;

            CountryId best_donor{};
            std::uint32_t best_chance_ppm = 0u;

            for (std::size_t ni = 0; ni < country_count; ++ni) {
                if (ni == ci) continue;
                const auto neighbor = CountryId{static_cast<CountryId::rep_type>(ni)};
                if (!completed(world, neighbor, def.key_hash)) continue;

                const auto rel = world.grand_strategy.relation_milli(country, neighbor);
                if (rel < -25'000) continue; // Closed borders / hostile relations block spread

                std::uint32_t chance = rules_.tech_spread_base_chance_ppm;
                if (rel >= 50'000) chance += 50'000u;
                if (world.grand_strategy.has_active_treaty(country, neighbor, TreatyKind::CustomsUnion)) chance += 125'000u;
                if (world.grand_strategy.has_active_treaty(country, neighbor, TreatyKind::TradeAgreement)) chance += 100'000u;
                if (world.grand_strategy.has_active_treaty(country, neighbor, TreatyKind::Alliance)) chance += 75'000u;
                if (world.grand_strategy.has_active_treaty(country, neighbor, TreatyKind::InvestmentRights)) chance += 50'000u;

                if (chance > best_chance_ppm) {
                    best_chance_ppm = chance;
                    best_donor = neighbor;
                }
            }

            if (!best_donor.valid() || best_chance_ppm == 0u) continue;

            // Deterministic probabilistic roll
            Fnv1a64 roll_hash;
            roll_hash.add(weekly_ticks_);
            roll_hash.add(static_cast<std::uint32_t>(country.value()));
            roll_hash.add(def.key_hash);
            roll_hash.add(static_cast<std::uint32_t>(best_donor.value()));
            const auto roll = static_cast<std::uint32_t>(roll_hash.value() % 1'000'000u);

            if (roll >= best_chance_ppm) {
                // Roll failed this week -> slow, probabilistic propagation!
                continue;
            }

            if (!queued(world, country, def.key_hash)) {
                (void)world.grand_strategy.add_technology({country, def.key_hash, 0u, false});
            }
            // Deferred on_researched hook. The script receives `world` and may
            // grant technologies, which reallocates technologys_ and would
            // dangle the `record` reference this loop is iterating over.
            const ScriptProgram* pending_on_researched = nullptr;
            for (auto& record : world.grand_strategy.technologys_) {
                if (record.country == country && record.key_hash == def.key_hash && !record.unlocked) {
                    const auto cost_denom = std::max<std::uint64_t>(1u, static_cast<std::uint64_t>(def.cost_milli) / 1'000u);
                    auto spread_delta = std::max<std::uint32_t>(10u, static_cast<std::uint32_t>((1'000'000ull * rules_.tech_spread_rate_ppm) / (cost_denom * 1'000'000ull)));
                    // Modulate spread delta by receiving country's absorptive capacity
                    spread_delta = std::max<std::uint32_t>(5u, static_cast<std::uint32_t>((static_cast<std::uint64_t>(spread_delta) * absorptive_capacity_ppm) / 1'000'000ull));

                    record.progress_ppm = std::min<std::uint32_t>(1'000'000u, record.progress_ppm + spread_delta);
                    if (record.progress_ppm >= 1'000'000u) {
                        record.unlocked = true;
                        bool is_world_first = true;
                        for (const auto& other_rec : world.grand_strategy.technologys()) {
                            if (other_rec.key_hash == record.key_hash && other_rec.country != country && other_rec.unlocked) {
                                is_world_first = false;
                                break;
                            }
                        }
                        if (is_world_first) {
                            world.countries.add_prestige(country, 5.0);
                        }
                        if (def.on_researched.has_value()) {
                            pending_on_researched = &*def.on_researched;
                        }
                    }
                    break;
                }
            }
            if (pending_on_researched != nullptr) {
                (void)vm_.execute_if(*pending_on_researched, world, ScopeRef::country(country));
            }
        }
    }
}


bool ResearchSystem::validate_state(const World& world) const noexcept {
    // An empty catalogue is valid for low-level worlds and legacy save tools.
    // Once content is bound, every authoritative record must resolve by stable key.
    if (definitions_.empty()) return true;
    if (!finalized_) return false;
    const auto records = world.grand_strategy.technologys();
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (!valid_country(record.country, world.countries.size()) || find(record.key_hash) == nullptr)
            return false;
        if (record.unlocked != (record.progress_ppm == 1'000'000u)) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (records[j].country == record.country && records[j].key_hash == record.key_hash) return false;
        }
    }
    return true;
}

std::size_t ResearchSystem::immutable_bytes() const noexcept {
    std::size_t bytes = definitions_.capacity() * sizeof(TechnologyDefinition);
    for (const auto& definition : definitions_) {
        bytes += definition.key.capacity();
        bytes += definition.prerequisites.capacity() * sizeof(std::uint64_t);
        bytes += definition.unlock_keys.capacity() * sizeof(std::uint64_t);
    }
    bytes += definition_lookup_.size() * (sizeof(std::uint64_t) + sizeof(std::uint32_t));
    return bytes;
}

} // namespace thunder
