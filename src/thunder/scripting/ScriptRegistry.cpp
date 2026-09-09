#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {
namespace {

bool argument_well_formed(ScriptArgument argument) noexcept {
    switch (argument.kind) {
        case ScriptArgumentKind::None: return false;
        case ScriptArgumentKind::Number: return std::isfinite(argument.number);
        case ScriptArgumentKind::SymbolHash: return true;
        case ScriptArgumentKind::Boolean:
            return argument.number == 0.0 || argument.number == 1.0;
        case ScriptArgumentKind::Scope: return argument.scope_value.valid();
    }
    return false;
}

CountryId numeric_country_argument(const World& world, double value) noexcept {
    if (!std::isfinite(value) || value < 0.0 ||
        value > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        std::floor(value) != value) return {};
    const CountryId id{static_cast<std::uint32_t>(value)};
    return static_cast<std::size_t>(id.value()) < world.countries.size() ? id : CountryId{};
}

CountryId country_argument(const World& world, ScriptArgument argument) noexcept {
    if (argument.kind == ScriptArgumentKind::Scope) {
        if (argument.scope_value.type != ScopeType::Country) return {};
        const CountryId id{argument.scope_value.raw_id};
        return static_cast<std::size_t>(id.value()) < world.countries.size() ? id : CountryId{};
    }
    if (argument.kind == ScriptArgumentKind::Number) {
        return numeric_country_argument(world, argument.number);
    }
    if (argument.kind != ScriptArgumentKind::SymbolHash) return {};
    CountryId match{};
    for (std::size_t i = 0; i < world.countries.size(); ++i) {
        const CountryId id{static_cast<std::uint32_t>(i)};
        if (script_symbol_hash(world.countries.tag(id)) != argument.symbol_hash) continue;
        if (match.valid()) return {}; // hash collision or duplicate tag: fail closed
        match = id;
    }
    return match;
}

std::uint64_t hash_argument(ScriptArgument argument) noexcept {
    if (argument.kind == ScriptArgumentKind::SymbolHash) return argument.symbol_hash;
    if (argument.kind != ScriptArgumentKind::Number) return 0u;
    constexpr double max_exact_integer = 9'007'199'254'740'991.0;
    if (!std::isfinite(argument.number) || !(argument.number > 0.0) ||
        argument.number > max_exact_integer || std::floor(argument.number) != argument.number) return 0u;
    return static_cast<std::uint64_t>(argument.number);
}

EconomyAmount economy_amount_argument(double value) noexcept {
    if (!std::isfinite(value)) return 0;
    const auto scaled = value * static_cast<double>(economy_scale);
    if (scaled <= 0.0) return 0;
    if (scaled >= static_cast<double>(std::numeric_limits<EconomyAmount>::max()))
        return std::numeric_limits<EconomyAmount>::max();
    return static_cast<EconomyAmount>(std::llround(scaled));
}

} // namespace

TriggerPrimitiveId ScriptRegistry::register_trigger(std::string name, ScopeType scope, Trigger trigger) {
    if (trigger == nullptr) throw std::invalid_argument("null trigger primitive");
    if (trigger_lookup_.contains(name)) throw std::runtime_error("duplicate trigger primitive: " + name);
    if (triggers_.size() >= static_cast<std::size_t>(std::numeric_limits<TriggerPrimitiveId::rep_type>::max())) {
        throw std::overflow_error("too many trigger primitives");
    }
    const auto id = TriggerPrimitiveId{static_cast<TriggerPrimitiveId::rep_type>(triggers_.size())};
    triggers_.push_back({name, scope, trigger});
    trigger_lookup_.emplace(std::move(name), id);
    return id;
}

EffectPrimitiveId ScriptRegistry::register_effect(std::string name, ScopeType scope, Effect effect) {
    if (effect == nullptr) throw std::invalid_argument("null effect primitive");
    if (effect_lookup_.contains(name)) throw std::runtime_error("duplicate effect primitive: " + name);
    if (effects_.size() >= static_cast<std::size_t>(std::numeric_limits<EffectPrimitiveId::rep_type>::max())) {
        throw std::overflow_error("too many effect primitives");
    }
    const auto id = EffectPrimitiveId{static_cast<EffectPrimitiveId::rep_type>(effects_.size())};
    effects_.push_back({name, scope, effect});
    effect_lookup_.emplace(std::move(name), id);
    return id;
}

TriggerPrimitiveId ScriptRegistry::register_typed_trigger(std::string name, ScopeType scope, TypedTrigger trigger,
                                                          bool accepts_number, bool accepts_symbol,
                                                          bool accepts_boolean, bool accepts_scope) {
    if (trigger == nullptr) throw std::invalid_argument("null typed trigger primitive");
    if (trigger_lookup_.contains(name)) throw std::runtime_error("duplicate trigger primitive: " + name);
    if (!accepts_number && !accepts_symbol && !accepts_boolean && !accepts_scope)
        throw std::invalid_argument("typed trigger accepts no argument kinds");
    if (triggers_.size() >= static_cast<std::size_t>(std::numeric_limits<TriggerPrimitiveId::rep_type>::max()))
        throw std::overflow_error("too many trigger primitives");
    const auto id = TriggerPrimitiveId{static_cast<TriggerPrimitiveId::rep_type>(triggers_.size())};
    const auto mask = static_cast<std::uint8_t>((accepts_number ? number_argument_bit : 0u) |
                                                (accepts_symbol ? symbol_argument_bit : 0u) |
                                                (accepts_boolean ? boolean_argument_bit : 0u) |
                                                (accepts_scope ? scope_argument_bit : 0u));
    triggers_.push_back({name, scope, nullptr, trigger, mask});
    trigger_lookup_.emplace(std::move(name), id);
    return id;
}

EffectPrimitiveId ScriptRegistry::register_typed_effect(std::string name, ScopeType scope, TypedEffect effect,
                                                        bool accepts_number, bool accepts_symbol,
                                                        bool accepts_boolean, bool accepts_scope) {
    if (effect == nullptr) throw std::invalid_argument("null typed effect primitive");
    if (effect_lookup_.contains(name)) throw std::runtime_error("duplicate effect primitive: " + name);
    if (!accepts_number && !accepts_symbol && !accepts_boolean && !accepts_scope)
        throw std::invalid_argument("typed effect accepts no argument kinds");
    if (effects_.size() >= static_cast<std::size_t>(std::numeric_limits<EffectPrimitiveId::rep_type>::max()))
        throw std::overflow_error("too many effect primitives");
    const auto id = EffectPrimitiveId{static_cast<EffectPrimitiveId::rep_type>(effects_.size())};
    const auto mask = static_cast<std::uint8_t>((accepts_number ? number_argument_bit : 0u) |
                                                (accepts_symbol ? symbol_argument_bit : 0u) |
                                                (accepts_boolean ? boolean_argument_bit : 0u) |
                                                (accepts_scope ? scope_argument_bit : 0u));
    effects_.push_back({name, scope, nullptr, effect, mask});
    effect_lookup_.emplace(std::move(name), id);
    return id;
}

TriggerPrimitiveId ScriptRegistry::find_trigger(std::string_view name) const noexcept {
    const auto it = trigger_lookup_.find(name);
    return it == trigger_lookup_.end() ? TriggerPrimitiveId{} : it->second;
}

EffectPrimitiveId ScriptRegistry::find_effect(std::string_view name) const noexcept {
    const auto it = effect_lookup_.find(name);
    return it == effect_lookup_.end() ? EffectPrimitiveId{} : it->second;
}

ScopeType ScriptRegistry::trigger_scope(TriggerPrimitiveId id) const {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= triggers_.size()) throw std::out_of_range("invalid TriggerPrimitiveId");
    return triggers_[index].scope;
}

ScopeType ScriptRegistry::effect_scope(EffectPrimitiveId id) const {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= effects_.size()) throw std::out_of_range("invalid EffectPrimitiveId");
    return effects_[index].scope;
}

bool ScriptRegistry::trigger_accepts_argument(TriggerPrimitiveId id, ScriptArgumentKind kind) const {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= triggers_.size()) throw std::out_of_range("invalid TriggerPrimitiveId");
    std::uint8_t bit = 0u;
    switch (kind) {
        case ScriptArgumentKind::Number: bit = number_argument_bit; break;
        case ScriptArgumentKind::SymbolHash: bit = symbol_argument_bit; break;
        case ScriptArgumentKind::Boolean: bit = boolean_argument_bit; break;
        case ScriptArgumentKind::Scope: bit = scope_argument_bit; break;
        case ScriptArgumentKind::None: return false;
    }
    return (triggers_[index].argument_mask & bit) != 0u;
}

bool ScriptRegistry::effect_accepts_argument(EffectPrimitiveId id, ScriptArgumentKind kind) const {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= effects_.size()) throw std::out_of_range("invalid EffectPrimitiveId");
    std::uint8_t bit = 0u;
    switch (kind) {
        case ScriptArgumentKind::Number: bit = number_argument_bit; break;
        case ScriptArgumentKind::SymbolHash: bit = symbol_argument_bit; break;
        case ScriptArgumentKind::Boolean: bit = boolean_argument_bit; break;
        case ScriptArgumentKind::Scope: bit = scope_argument_bit; break;
        case ScriptArgumentKind::None: return false;
    }
    return (effects_[index].argument_mask & bit) != 0u;
}

bool ScriptRegistry::evaluate_trigger(TriggerPrimitiveId id, const World& world, ScopeRef scope, double argument) const {
    return evaluate_trigger(id, world, scope, ScriptArgument::numeric(argument));
}

bool ScriptRegistry::evaluate_trigger(TriggerPrimitiveId id, const World& world, ScopeRef scope, ScriptArgument argument) const {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= triggers_.size()) throw std::out_of_range("invalid TriggerPrimitiveId");
    const auto& entry = triggers_[index];
    if (entry.scope != scope.type) throw std::runtime_error("trigger scope mismatch");
    if (!argument_well_formed(argument)) throw std::runtime_error("trigger argument is not canonical and finite");
    if (!trigger_accepts_argument(id, argument.kind)) throw std::runtime_error("trigger argument kind mismatch");
    if (entry.typed_callback != nullptr) return entry.typed_callback(world, scope, argument);
    if (argument.kind != ScriptArgumentKind::Number || entry.callback == nullptr) throw std::runtime_error("numeric trigger callback unavailable");
    return entry.callback(world, scope, argument.number);
}

void ScriptRegistry::execute_effect(EffectPrimitiveId id, World& world, ScopeRef scope, double argument) const {
    execute_effect(id, world, scope, ScriptArgument::numeric(argument));
}

void ScriptRegistry::execute_effect(EffectPrimitiveId id, World& world, ScopeRef scope, ScriptArgument argument) const {
    const auto index = static_cast<std::size_t>(id.value());
    if (!id.valid() || index >= effects_.size()) throw std::out_of_range("invalid EffectPrimitiveId");
    const auto& entry = effects_[index];
    if (entry.scope != scope.type) throw std::runtime_error("effect scope mismatch");
    if (!argument_well_formed(argument)) throw std::runtime_error("effect argument is not canonical and finite");
    if (!effect_accepts_argument(id, argument.kind)) throw std::runtime_error("effect argument kind mismatch");
    if (entry.typed_callback != nullptr) { entry.typed_callback(world, scope, argument); return; }
    if (argument.kind != ScriptArgumentKind::Number || entry.callback == nullptr) throw std::runtime_error("numeric effect callback unavailable");
    entry.callback(world, scope, argument.number);
}

bool ScriptRegistry::evaluate_trigger(std::string_view name, const World& world, ScopeRef scope, double argument) const {
    const auto id = find_trigger(name);
    if (!id.valid()) throw std::runtime_error("unknown trigger: " + std::string{name});
    return evaluate_trigger(id, world, scope, argument);
}

void ScriptRegistry::execute_effect(std::string_view name, World& world, ScopeRef scope, double argument) const {
    const auto id = find_effect(name);
    if (!id.valid()) throw std::runtime_error("unknown effect: " + std::string{name});
    execute_effect(id, world, scope, argument);
}

ScriptRegistry ScriptRegistry::make_builtin() {
    ScriptRegistry registry;
    registry.trigger_lookup_.reserve(64u);
    registry.effect_lookup_.reserve(64u);
    registry.triggers_.reserve(64u);
    registry.effects_.reserve(64u);
    registry.register_trigger("population_above", ScopeType::Country,
        [](const World& world, ScopeRef scope, double threshold) {
            return world.countries.population(CountryId{scope.raw_id}) > threshold;
        });
    registry.register_trigger("gdp_above", ScopeType::Country,
        [](const World& world, ScopeRef scope, double threshold) {
            return world.countries.gdp(CountryId{scope.raw_id}) > threshold;
        });
    registry.register_trigger("treasury_above", ScopeType::Country,
        [](const World& world, ScopeRef scope, double threshold) {
            return world.countries.treasury(CountryId{scope.raw_id}) > threshold;
        });
    registry.register_trigger("tax_rate_above", ScopeType::Country,
        [](const World& world, ScopeRef scope, double threshold) {
            return world.countries.tax_rate(CountryId{scope.raw_id}) > threshold;
        });
    registry.register_effect("add_treasury", ScopeType::Country,
        [](World& world, ScopeRef scope, double amount) {
            const CountryId country{scope.raw_id};
            const auto replacement = world.countries.treasury(country) + amount;
            if (!std::isfinite(replacement)) throw std::range_error("add_treasury overflow");
            world.countries.add_treasury(country, amount);
        });
    registry.register_effect("set_tax_rate", ScopeType::Country,
        [](World& world, ScopeRef scope, double rate) {
            world.countries.set_tax_rate(CountryId{scope.raw_id}, rate);
        });
    registry.register_effect("set_gdp", ScopeType::Country,
        [](World& world, ScopeRef scope, double value) { world.countries.set_gdp(CountryId{scope.raw_id}, value); });
    registry.register_trigger("national_debt_above", ScopeType::Country,
        [](const World& world, ScopeRef scope, double value) {
            return world.countries.national_debt_milli(CountryId{scope.raw_id}) > economy_amount_argument(value);
        });
    registry.register_trigger("bank_reserves_above", ScopeType::Country,
        [](const World& world, ScopeRef scope, double value) {
            const CountryId country{scope.raw_id};
            const auto bank = world.banks.primary_bank(country, world.countries.primary_currency(country));
            return bank.valid() && world.banks.bank(bank).reserves_milli > economy_amount_argument(value);
        });
    registry.register_trigger("bank_lending_capacity_above", ScopeType::Country,
        [](const World& world, ScopeRef scope, double value) {
            const CountryId country{scope.raw_id};
            const auto bank = world.banks.primary_bank(country, world.countries.primary_currency(country));
            return bank.valid() && world.banks.lendable_capacity(bank) > economy_amount_argument(value);
        });
    registry.register_effect("set_import_tariff", ScopeType::Country,
        [](World& world, ScopeRef scope, double rate) {
            const CountryId country{scope.raw_id};
            auto policy = world.trade_policies.get(country);
            policy.import_tariff_ppm = static_cast<std::int32_t>(std::clamp(rate, 0.0, 1.0) * ppm_scale + 0.5);
            world.trade_policies.set(country, policy);
        });
    registry.register_effect("set_export_tariff", ScopeType::Country,
        [](World& world, ScopeRef scope, double rate) {
            const CountryId country{scope.raw_id};
            auto policy = world.trade_policies.get(country);
            policy.export_tariff_ppm = static_cast<std::int32_t>(std::clamp(rate, 0.0, 1.0) * ppm_scale + 0.5);
            world.trade_policies.set(country, policy);
        });
    registry.register_effect("set_trade_logistics_capacity", ScopeType::Country,
        [](World& world, ScopeRef scope, double amount) {
            const CountryId country{scope.raw_id};
            auto policy = world.trade_policies.get(country);
            policy.logistics_capacity_milli = economy_amount_argument(amount);
            world.trade_policies.set(country, policy);
        });
    registry.register_effect("issue_sovereign_bonds", ScopeType::Country,
        [](World& world, ScopeRef scope, double amount) {
            (void)world.countries.issue_sovereign_bonds(
                CountryId{scope.raw_id}, economy_amount_argument(amount), world);
        });
    registry.register_effect("repay_sovereign_debt", ScopeType::Country,
        [](World& world, ScopeRef scope, double amount) {
            (void)world.countries.repay_sovereign_debt(
                CountryId{scope.raw_id}, economy_amount_argument(amount), world);
        });
    registry.register_typed_effect("set_primary_currency", ScopeType::Country,
        [](World& world, ScopeRef scope, ScriptArgument argument) {
            const auto key = hash_argument(argument);
            if (key != 0u && world.currencies.contains(key))
                world.countries.set_primary_currency(CountryId{scope.raw_id}, key);
        }, true, true, false, false);

    registry.register_trigger("state_population_above", ScopeType::State,
        [](const World& world, ScopeRef scope, double threshold) { double total=0.0; for(std::size_t i=0;i<world.pops.size();++i){if(!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i)))continue;const auto p=PopId{static_cast<std::uint32_t>(i)};const auto pr=world.pops.province(p);if(pr.valid()&&world.geography.province_state(pr)==StateId{scope.raw_id})total+=static_cast<double>(world.pops.population(p));} return total>threshold; });
    registry.register_trigger("province_population_above", ScopeType::Province,
        [](const World& world, ScopeRef scope, double threshold) { double total=0.0; for(std::size_t i=0;i<world.pops.size();++i){if(!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i)))continue;const auto p=PopId{static_cast<std::uint32_t>(i)};if(world.pops.province(p)==ProvinceId{scope.raw_id})total+=static_cast<double>(world.pops.population(p));} return total>threshold; });
    registry.register_trigger("pop_size_above", ScopeType::Pop, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.pops.population(PopId{s.raw_id}))>x;});
    registry.register_trigger("pop_sol_above", ScopeType::Pop, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.pops.standard_of_living_milli(PopId{s.raw_id}))>x*1000.0;});
    registry.register_trigger("pop_literacy_above", ScopeType::Pop, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.pops.literacy_permyriad(PopId{s.raw_id}))>x*10000.0;});
    registry.register_trigger("market_supply_above", ScopeType::Market, [](const World& world, ScopeRef s,double x){EconomyAmount t=0;for(auto v:world.markets.supply_row(MarketId{s.raw_id}))t=saturating_add(t,v);return static_cast<double>(t)>x;});
    registry.register_trigger("market_demand_above", ScopeType::Market, [](const World& world, ScopeRef s,double x){EconomyAmount t=0;for(auto v:world.markets.demand_row(MarketId{s.raw_id}))t=saturating_add(t,v);return static_cast<double>(t)>x;});

    // Building scope (first expanded script domain, spec 04): levels/employees
    // compare in whole units; profit/cash are milli-backed and scale by 1000
    // like their pop counterparts.
    registry.register_trigger("building_level_above", ScopeType::Building, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.buildings.level(BuildingId{s.raw_id}))>x;});
    registry.register_trigger("building_employees_above", ScopeType::Building, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.buildings.employees(BuildingId{s.raw_id}))>x;});
    registry.register_trigger("building_profit_above", ScopeType::Building, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.buildings.last_profit(BuildingId{s.raw_id}))>x*1000.0;});
    registry.register_trigger("building_cash_above", ScopeType::Building, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.buildings.cash(BuildingId{s.raw_id}))>x*1000.0;});
    registry.register_trigger("building_wage_above", ScopeType::Building, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.buildings.wage_offer(BuildingId{s.raw_id}))>x;});

    registry.register_effect("set_building_level", ScopeType::Building, [](World& world,ScopeRef s,double x){if(!std::isfinite(x)||x<1.0||x>65535.0)return;world.buildings.set_level(BuildingId{s.raw_id},static_cast<std::uint16_t>(x));});
    registry.register_effect("set_building_employees", ScopeType::Building, [](World& world,ScopeRef s,double x){const auto scaled=x;if(!std::isfinite(scaled)||scaled<0.0||scaled>static_cast<double>(std::numeric_limits<std::uint16_t>::max()))return;world.buildings.set_employees(BuildingId{s.raw_id},static_cast<std::uint16_t>(scaled));});
    registry.register_effect("building_add_cash", ScopeType::Building, [](World& world,ScopeRef s,double x){const auto scaled=x*1000.0;if(!std::isfinite(scaled))return;world.buildings.add_cash(BuildingId{s.raw_id},static_cast<EconomyAmount>(scaled));});

    // Military/diplomatic scope primitives (spec 04 expansion batch two):
    // armies carry whole-unit manpower and ppm organization; fronts/wars carry
    // milli progress/score on the shared ±100'000 relation scale (content
    // values are whole units, scaled by 1000 like the pop columns).
    registry.register_trigger("army_manpower_above", ScopeType::Army, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.grand_strategy.armys()[s.raw_id].manpower)>x;});
    registry.register_trigger("army_organization_above", ScopeType::Army, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.grand_strategy.armys()[s.raw_id].organization_ppm)>x*1'000'000.0;});
    registry.register_effect("army_add_manpower", ScopeType::Army, [](World& world,ScopeRef s,double x){
        if(!std::isfinite(x))return;
        auto& army=world.grand_strategy.armys_mut()[s.raw_id];
        const auto delta=static_cast<std::int64_t>(x);
        const auto result=static_cast<std::int64_t>(army.manpower)+delta;
        army.manpower=static_cast<std::uint32_t>(std::clamp<std::int64_t>(result,0,std::numeric_limits<std::uint32_t>::max()));
    });
    registry.register_effect("set_army_organization", ScopeType::Army, [](World& world,ScopeRef s,double x){
        if(!std::isfinite(x))return;
        world.grand_strategy.armys_mut()[s.raw_id].organization_ppm=static_cast<std::uint32_t>(std::clamp(x,0.0,1.0)*1'000'000.0);
    });
    registry.register_trigger("front_progress_above", ScopeType::Front, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.grand_strategy.fronts()[s.raw_id].progress_milli)>x*1000.0;});
    registry.register_effect("shift_front_progress", ScopeType::Front, [](World& world,ScopeRef s,double x){
        if(!std::isfinite(x))return;
        auto& front=world.grand_strategy.fronts_mut()[s.raw_id];
        const auto shifted=static_cast<std::int64_t>(front.progress_milli)+static_cast<std::int64_t>(x*1000.0);
        front.progress_milli=static_cast<std::int32_t>(std::clamp<std::int64_t>(shifted,-100'000,100'000));
    });
    registry.register_typed_trigger("war_active", ScopeType::War, [](const World& world, ScopeRef s, ScriptArgument argument){
        return argument.kind==ScriptArgumentKind::Boolean
            ? (argument.boolean_value()==world.grand_strategy.wars()[s.raw_id].active)
            : world.grand_strategy.wars()[s.raw_id].active;
    }, /*number*/false, /*symbol*/false, /*boolean*/true);
    registry.register_trigger("war_score_above", ScopeType::War, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.grand_strategy.wars()[s.raw_id].war_score_milli)>x*1000.0;});
    registry.register_trigger("war_weeks_above", ScopeType::War, [](const World& world, ScopeRef s,double x){return static_cast<double>(world.grand_strategy.wars()[s.raw_id].weeks)>x;});
    registry.register_effect("shift_war_score", ScopeType::War, [](World& world,ScopeRef s,double x){
        if(!std::isfinite(x))return;
        auto& war=world.grand_strategy.wars_mut()[s.raw_id];
        const auto shifted=static_cast<std::int64_t>(war.war_score_milli)+static_cast<std::int64_t>(x*1000.0);
        war.war_score_milli=static_cast<std::int32_t>(std::clamp<std::int64_t>(shifted,-100'000,100'000));
    });

    registry.register_effect("set_state_owner", ScopeType::State, [](World& world,ScopeRef s,double x){const auto owner=numeric_country_argument(world,x);if(!owner.valid())return;world.geography.set_state_owner(StateId{s.raw_id},owner);});
    registry.register_effect("set_province_owner", ScopeType::Province, [](World& world,ScopeRef s,double x){const auto owner=numeric_country_argument(world,x);if(!owner.valid())return;world.geography.set_province_owner(ProvinceId{s.raw_id},owner);});
    registry.register_effect("set_pop_wealth", ScopeType::Pop, [](World& world,ScopeRef s,double x){const auto scaled=x*1000.0;if(!std::isfinite(scaled)||scaled<static_cast<double>(std::numeric_limits<std::int32_t>::min())||scaled>static_cast<double>(std::numeric_limits<std::int32_t>::max()))return;world.pops.set_wealth_milli(PopId{s.raw_id},static_cast<std::int32_t>(scaled));});
    registry.register_effect("set_pop_literacy", ScopeType::Pop, [](World& world,ScopeRef s,double x){const auto v=static_cast<std::uint16_t>(std::clamp(x,0.0,1.0)*10000.0);world.pops.set_literacy_permyriad(PopId{s.raw_id},v);});
    registry.register_effect("set_market_owner", ScopeType::Market, [](World& world,ScopeRef s,double x){const auto owner=numeric_country_argument(world,x);if(!owner.valid())return;world.markets.set_owner(MarketId{s.raw_id},owner);});

    // Grand-strategy primitives accept either legacy numeric IDs/hashes or stable symbolic
    // references. Named country arguments resolve against country tags; named law arguments
    // compile to deterministic FNV-1a hashes. Numeric arguments remain supported for compatibility.
    registry.register_trigger("government_legitimacy_above", ScopeType::Country,
        [](const World& world, ScopeRef s, double x) {
            const auto* government = world.grand_strategy.government_for(CountryId{s.raw_id});
            return government != nullptr && static_cast<double>(government->legitimacy_ppm) > x * 1'000'000.0;
        });
    registry.register_trigger("government_stability_above", ScopeType::Country,
        [](const World& world, ScopeRef s, double x) {
            const auto* government = world.grand_strategy.government_for(CountryId{s.raw_id});
            return government != nullptr && static_cast<double>(government->stability_milli) > x * 1'000.0;
        });
    registry.register_typed_trigger("has_enacted_law", ScopeType::Country,
        [](const World& world, ScopeRef s, ScriptArgument x) {
            const auto key = hash_argument(x);
            if (key == 0u) return false;
            const CountryId country{s.raw_id};
            for (const auto& law : world.grand_strategy.laws())
                if (law.country == country && law.key_hash == key && law.enacted) return true;
            return false;
        });
    registry.register_typed_trigger("has_alliance_with", ScopeType::Country,
        [](const World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            return target.valid() && target != CountryId{s.raw_id} &&
                world.grand_strategy.has_active_treaty(CountryId{s.raw_id}, target, TreatyKind::Alliance);
        }, true, true, false, true);
    registry.register_typed_trigger("at_war_with", ScopeType::Country,
        [](const World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (!target.valid()) return false;
            const CountryId country{s.raw_id};
            for (const auto& war : world.grand_strategy.wars())
                if (war.active && ((war.attacker == country && war.defender == target) ||
                                   (war.attacker == target && war.defender == country))) return true;
            return false;
        }, true, true, false, true);
    registry.register_typed_trigger("has_diplomatic_play_with", ScopeType::Country,
        [](const World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (!target.valid()) return false;
            const CountryId country{s.raw_id};
            for (const auto& play : world.grand_strategy.diplomatic_plays()) {
                const bool pair = (play.initiator == country && play.target == target) ||
                                  (play.initiator == target && play.target == country);
                if (pair && play.phase != DiplomaticPlayPhase::Resolved) return true;
            }
            return false;
        }, true, true, false, true);

    registry.register_typed_effect("improve_relations_with", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (target.valid() && target != CountryId{s.raw_id}) world.grand_strategy.adjust_relation(CountryId{s.raw_id}, target, 5'000);
        }, true, true, false, true);
    registry.register_typed_effect("damage_relations_with", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (target.valid() && target != CountryId{s.raw_id}) world.grand_strategy.adjust_relation(CountryId{s.raw_id}, target, -5'000);
        }, true, true, false, true);
    registry.register_typed_effect("form_alliance_with", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (target.valid() && target != CountryId{s.raw_id}) (void)world.grand_strategy.create_treaty(CountryId{s.raw_id}, target, TreatyKind::Alliance);
        }, true, true, false, true);
    registry.register_typed_effect("form_trade_agreement_with", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (target.valid() && target != CountryId{s.raw_id}) (void)world.grand_strategy.create_treaty(CountryId{s.raw_id}, target, TreatyKind::TradeAgreement);
        }, true, true, false, true);
    registry.register_typed_effect("break_alliance_with", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (!target.valid()) return;
            const CountryId country{s.raw_id};
            const auto treaties = world.grand_strategy.treatys();
            for (std::size_t i = 0; i < treaties.size(); ++i) {
                const auto& treaty = treaties[i];
                const bool pair = (treaty.first == country && treaty.second == target) ||
                                  (treaty.first == target && treaty.second == country);
                if (pair && treaty.kind == TreatyKind::Alliance && treaty.active) {
                    (void)world.grand_strategy.break_treaty(TreatyId{static_cast<TreatyId::rep_type>(i)});
                    return;
                }
            }
        }, true, true, false, true);
    registry.register_typed_effect("start_diplomatic_play_with", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (target.valid() && target != CountryId{s.raw_id})
                (void)world.grand_strategy.start_diplomatic_play(CountryId{s.raw_id}, target, 1u);
        }, true, true, false, true);
    registry.register_typed_effect("back_down_from_play_with", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto target = country_argument(world, x);
            if (!target.valid()) return;
            const CountryId country{s.raw_id};
            const auto plays = world.grand_strategy.diplomatic_plays();
            for (std::size_t i = 0; i < plays.size(); ++i) {
                const auto& play = plays[i];
                const bool pair = (play.initiator == country && play.target == target) ||
                                  (play.initiator == target && play.target == country);
                if (pair && play.phase != DiplomaticPlayPhase::Resolved) {
                    (void)world.grand_strategy.back_down(DiplomaticPlayId{static_cast<DiplomaticPlayId::rep_type>(i)}, country);
                    return;
                }
            }
        }, true, true, false, true);
    registry.register_typed_effect("start_law_enactment", ScopeType::Country,
        [](World& world, ScopeRef s, ScriptArgument x) {
            const auto key = hash_argument(x);
            if (key != 0u) (void)world.grand_strategy.start_law_enactment(CountryId{s.raw_id}, key);
        });
    return registry;
}

namespace {
const char* scope_to_string(ScopeType s) noexcept {
    switch (s) {
        case ScopeType::None: return "None";
        case ScopeType::Country: return "Country";
        case ScopeType::State: return "State";
        case ScopeType::Province: return "Province";
        case ScopeType::Pop: return "Pop";
        case ScopeType::Market: return "Market";
        default: return "Unknown";
    }
}

std::string format_argument_types(std::uint8_t mask) {
    std::string types;
    if (mask & (1u << 0u)) types += "number, ";
    if (mask & (1u << 1u)) types += "symbol, ";
    if (mask & (1u << 2u)) types += "boolean, ";
    if (mask & (1u << 3u)) types += "scope, ";
    if (types.size() >= 2) types.resize(types.size() - 2);
    else types = "none";
    return types;
}
} // namespace

std::string ScriptRegistry::export_markdown_docs() const {
    std::string out = "# ThunderScript API Documentation\n\n";
    out += "## Triggers (" + std::to_string(triggers_.size()) + ")\n\n";
    out += "| Trigger | Scope | Argument Types |\n";
    out += "|:--------|:------|:---------------|\n";
    for (const auto& t : triggers_) {
        out += "| `" + t.name + "` | " + scope_to_string(t.scope) + " | " + format_argument_types(t.argument_mask) + " |\n";
    }

    out += "\n## Effects (" + std::to_string(effects_.size()) + ")\n\n";
    out += "| Effect | Scope | Argument Types |\n";
    out += "|:-------|:------|:---------------|\n";
    for (const auto& e : effects_) {
        out += "| `" + e.name + "` | " + scope_to_string(e.scope) + " | " + format_argument_types(e.argument_mask) + " |\n";
    }
    return out;
}

std::string ScriptRegistry::export_schema_json() const {
    std::string out = "{\n  \"triggers\": [\n";
    for (std::size_t i = 0; i < triggers_.size(); ++i) {
        const auto& t = triggers_[i];
        out += "    {\"name\": \"" + t.name + "\", \"scope\": \"" + scope_to_string(t.scope) +
               "\", \"arguments\": \"" + format_argument_types(t.argument_mask) + "\"}";
        if (i + 1 < triggers_.size()) out += ",";
        out += "\n";
    }
    out += "  ],\n  \"effects\": [\n";
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        const auto& e = effects_[i];
        out += "    {\"name\": \"" + e.name + "\", \"scope\": \"" + scope_to_string(e.scope) +
               "\", \"arguments\": \"" + format_argument_types(e.argument_mask) + "\"}";
        if (i + 1 < effects_.size()) out += ",";
        out += "\n";
    }
    out += "  ]\n}";
    return out;
}

} // namespace thunder
