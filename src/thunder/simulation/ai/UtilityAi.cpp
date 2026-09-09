#include "thunder/simulation/ai/UtilityAi.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/economy/EconomyDefinitions.hpp"
#include "thunder/scripting/ScopeResolver.hpp"
#include "thunder/simulation/kernel/World.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thunder {

void UtilityAiEngine::clear_content() {
    actions_.clear();
    plans_.clear();
    state_.clear();
    plan_state_.clear();
}

std::uint32_t UtilityAiEngine::add_action(AiActionDefinition action) {
    if (action.key.empty()) throw std::invalid_argument("AI action key is empty");
    for (const auto& existing : actions_) {
        if (existing.key == action.key) throw std::invalid_argument("duplicate AI action key");
    }
    if (action.scope == ScopeType::None) throw std::invalid_argument("AI action scope is none");
    if (action.valid && action.valid->scope != action.scope) throw std::invalid_argument("AI valid script scope mismatch");
    if (action.utility && action.utility->scope != action.scope) throw std::invalid_argument("AI utility scope mismatch");
    if (action.effect && action.effect->scope != action.scope) throw std::invalid_argument("AI effect script scope mismatch");
    const auto id = static_cast<std::uint32_t>(actions_.size());
    actions_.push_back(std::move(action));
    return id;
}

std::uint32_t UtilityAiEngine::add_plan(AiPlanDefinition plan) {
    if (plan.key.empty()) throw std::invalid_argument("AI plan key is empty");
    for (const auto& existing : plans_) {
        if (existing.key == plan.key) throw std::invalid_argument("duplicate AI plan key");
    }
    if (plan.scope == ScopeType::None) throw std::invalid_argument("AI plan scope is none");
    if (plan.valid && plan.valid->scope != plan.scope) throw std::invalid_argument("AI plan valid script scope mismatch");
    if (plan.priority && plan.priority->scope != plan.scope) throw std::invalid_argument("AI plan priority scope mismatch");
    if (plan.completion && plan.completion->scope != plan.scope) throw std::invalid_argument("AI plan completion scope mismatch");
    if (plan.action_keys.empty()) throw std::invalid_argument("AI plan requires at least one action");
    for (std::size_t i = 0; i < plan.action_keys.size(); ++i) {
        if (plan.action_keys[i].empty()) throw std::invalid_argument("AI plan action key is empty");
        for (std::size_t j = 0; j < i; ++j) {
            if (plan.action_keys[j] == plan.action_keys[i]) throw std::invalid_argument("duplicate action in AI plan");
        }
        const auto found = std::find_if(actions_.begin(), actions_.end(), [&](const AiActionDefinition& action) {
            return action.key == plan.action_keys[i];
        });
        if (found == actions_.end()) throw std::invalid_argument("AI plan references unknown action");
        if (found->scope != plan.scope) throw std::invalid_argument("AI plan action scope mismatch");
    }
    const auto id = static_cast<std::uint32_t>(plans_.size());
    plans_.push_back(std::move(plan));
    return id;
}

bool UtilityAiEngine::on_cooldown(std::uint32_t action, ScopeRef scope, std::uint64_t tick) const noexcept {
    if (action >= actions_.size() || actions_[action].cooldown_ticks == 0u) return false;
    for (const auto& record : state_) {
        if (record.action == action && record.scope == scope) {
            const auto cooldown = static_cast<std::uint64_t>(actions_[action].cooldown_ticks);
            return tick < record.last_tick || tick - record.last_tick < cooldown;
        }
    }
    return false;
}

bool UtilityAiEngine::plan_contains_action(const AiPlanDefinition& plan, std::string_view action_key) const noexcept {
    return std::find(plan.action_keys.begin(), plan.action_keys.end(), action_key) != plan.action_keys.end();
}

AiPlanState* UtilityAiEngine::active_plan_state(ScopeRef scope) noexcept {
    for (auto& state : plan_state_) if (state.scope == scope) return &state;
    return nullptr;
}

const AiPlanState* UtilityAiEngine::active_plan_state(ScopeRef scope) const noexcept {
    for (const auto& state : plan_state_) if (state.scope == scope) return &state;
    return nullptr;
}

void UtilityAiEngine::mark_executed(std::uint32_t action, ScopeRef scope, std::uint64_t tick) {
    for (auto& record : state_) {
        if (record.action == action && record.scope == scope) {
            record.last_tick = tick;
            return;
        }
    }
    state_.push_back({action, scope, tick});
}

AiChoice UtilityAiEngine::choose(const World& world, ScopeRef scope, std::uint64_t tick) const {
    AiChoice best{};
    if (!ScopeResolver::valid(world, scope)) return best;
    for (std::uint32_t i = 0; i < actions_.size(); ++i) {
        const auto& action = actions_[i];
        if (action.scope != scope.type || on_cooldown(i, scope, tick)) continue;
        if (action.valid && !vm_.evaluate(*action.valid, world, scope)) continue;
        const double utility = action.base_utility + (action.utility ? vm_.evaluate(*action.utility, world, scope) : 0.0);
        if (!std::isfinite(utility)) continue;
        if (!best.valid || utility > best.utility ||
            (utility == best.utility && action.key < actions_[best.action].key)) {
            best = {i, scope, utility, true};
        }
    }
    return best;
}

AiChoice UtilityAiEngine::choose_for_plan(const World& world, ScopeRef scope, std::uint32_t plan,
                                           std::uint64_t tick) const {
    AiChoice best{};
    if (plan >= plans_.size() || !ScopeResolver::valid(world, scope)) return best;
    const auto& plan_definition = plans_[plan];
    if (plan_definition.scope != scope.type) return best;
    for (std::uint32_t i = 0; i < actions_.size(); ++i) {
        const auto& action = actions_[i];
        if (action.scope != scope.type || !plan_contains_action(plan_definition, action.key) || on_cooldown(i, scope, tick)) continue;
        if (action.valid && !vm_.evaluate(*action.valid, world, scope)) continue;
        const double utility = action.base_utility + (action.utility ? vm_.evaluate(*action.utility, world, scope) : 0.0);
        if (!std::isfinite(utility)) continue;
        if (!best.valid || utility > best.utility ||
            (utility == best.utility && action.key < actions_[best.action].key)) {
            best = {i, scope, utility, true};
        }
    }
    return best;
}

AiPlanChoice UtilityAiEngine::choose_plan(const World& world, ScopeRef scope, std::uint64_t) const {
    AiPlanChoice best{};
    if (!ScopeResolver::valid(world, scope)) return best;
    for (std::uint32_t i = 0; i < plans_.size(); ++i) {
        const auto& plan = plans_[i];
        if (plan.scope != scope.type) continue;
        if (plan.valid && !vm_.evaluate(*plan.valid, world, scope)) continue;
        const double priority = plan.base_priority + (plan.priority ? vm_.evaluate(*plan.priority, world, scope) : 0.0);
        if (!std::isfinite(priority)) continue;
        if (!best.valid || priority > best.priority ||
            (priority == best.priority && plan.key < plans_[best.plan].key)) {
            best = {i, scope, priority, true};
        }
    }
    return best;
}

bool UtilityAiEngine::execute_best(World& world, ScopeRef scope, std::uint64_t tick) {
    const auto choice = choose(world, scope, tick);
    if (!choice.valid) return false;
    const auto& effect = actions_[choice.action].effect;
    if (effect && !vm_.execute_if(*effect, world, scope, {}, tick)) return false;
    mark_executed(choice.action, scope, tick);
    return true;
}

bool UtilityAiEngine::execute_planned_best(World& world, ScopeRef scope, std::uint64_t tick) {
    if (plans_.empty()) return execute_best(world, scope, tick);
    if (!ScopeResolver::valid(world, scope)) return false;

    auto erase_scope_plan = [&]() {
        std::erase_if(plan_state_, [&](const AiPlanState& state) { return state.scope == scope; });
    };

    auto* active = active_plan_state(scope);
    if (active != nullptr) {
        if (active->plan >= plans_.size()) {
            erase_scope_plan();
            active = nullptr;
        } else {
            const auto& plan = plans_[active->plan];
            if ((plan.valid && !vm_.evaluate(*plan.valid, world, scope)) ||
                (plan.completion && vm_.evaluate(*plan.completion, world, scope))) {
                erase_scope_plan();
                active = nullptr;
            } else if (tick >= active->started_tick && tick - active->started_tick >= plan.commitment_ticks) {
                const auto candidate = choose_plan(world, scope, tick);
                const double current_priority = plan.base_priority + (plan.priority ? vm_.evaluate(*plan.priority, world, scope) : 0.0);
                if (candidate.valid && candidate.plan != active->plan &&
                    (!std::isfinite(current_priority) || candidate.priority > current_priority)) {
                    erase_scope_plan();
                    plan_state_.push_back({candidate.plan, scope, tick, tick});
                    active = &plan_state_.back();
                }
            }
        }
    }

    if (active == nullptr) {
        const auto chosen = choose_plan(world, scope, tick);
        if (!chosen.valid) return false;
        plan_state_.push_back({chosen.plan, scope, tick, tick});
        active = &plan_state_.back();
    }

    const auto choice = choose_for_plan(world, scope, active->plan, tick);
    active->last_tick = tick;
    if (!choice.valid) return false;
    const auto& effect = actions_[choice.action].effect;
    if (effect && !vm_.execute_if(*effect, world, scope, {}, tick)) return false;
    mark_executed(choice.action, scope, tick);

    if (active->plan < plans_.size()) {
        const auto& plan = plans_[active->plan];
        if (plan.completion && vm_.evaluate(*plan.completion, world, scope)) erase_scope_plan();
    }
    return true;
}

std::size_t UtilityAiEngine::run_all(World& world, ScopeType type, std::size_t budget, std::uint64_t tick) {
    std::size_t count = 0;
    for (const auto scope : ScopeResolver::all(world, type)) {
        if (count >= budget) break;
        if (execute_best(world, scope, tick)) ++count;
    }
    return count;
}

std::size_t UtilityAiEngine::run_plans(World& world, ScopeType type, std::size_t budget, std::uint64_t tick) {
    std::size_t count = 0;
    for (const auto scope : ScopeResolver::all(world, type)) {
        if (count >= budget) break;
        if (execute_planned_best(world, scope, tick)) ++count;
    }
    return count;
}

void UtilityAiEngine::validate_state(std::span<const AiActionState> state, const World& world,
                                     std::uint64_t current_tick) const {
    for (std::size_t i = 0; i < state.size(); ++i) {
        const auto& record = state[i];
        if (record.action >= actions_.size()) throw std::runtime_error("AI save references missing action");
        if (record.scope.type != actions_[record.action].scope || !ScopeResolver::valid(world, record.scope))
            throw std::runtime_error("AI save scope mismatch or out of range");
        if (record.last_tick > current_tick) throw std::runtime_error("AI save cooldown tick is in the future");
        for (std::size_t j = 0; j < i; ++j) {
            if (state[j].action == record.action && state[j].scope == record.scope)
                throw std::runtime_error("AI save contains duplicate cooldown state");
        }
    }
}

void UtilityAiEngine::validate_plan_state(std::span<const AiPlanState> state, const World& world,
                                          std::uint64_t current_tick) const {
    for (std::size_t i = 0; i < state.size(); ++i) {
        const auto& record = state[i];
        if (record.plan >= plans_.size()) throw std::runtime_error("AI save references missing plan");
        if (record.scope.type != plans_[record.plan].scope || !ScopeResolver::valid(world, record.scope))
            throw std::runtime_error("AI plan save scope mismatch or out of range");
        if (record.started_tick > record.last_tick || record.last_tick > current_tick)
            throw std::runtime_error("AI plan save tick is invalid");
        for (std::size_t j = 0; j < i; ++j) {
            if (state[j].scope == record.scope) throw std::runtime_error("AI save contains multiple active plans for one scope");
        }
    }
}

void UtilityAiEngine::restore_state(std::vector<AiActionState> state) {
    for (const auto& record : state) {
        if (record.action >= actions_.size()) throw std::runtime_error("AI save references missing action");
        if (record.scope.type != actions_[record.action].scope) throw std::runtime_error("AI save scope mismatch");
    }
    state_ = std::move(state);
}

void UtilityAiEngine::restore_plan_state(std::vector<AiPlanState> state) {
    for (const auto& record : state) {
        if (record.plan >= plans_.size()) throw std::runtime_error("AI save references missing plan");
        if (record.scope.type != plans_[record.plan].scope) throw std::runtime_error("AI plan save scope mismatch");
    }
    plan_state_ = std::move(state);
}

std::uint64_t UtilityAiEngine::checksum_state(std::span<const AiActionState> state,
                                              std::span<const AiPlanState> plan_state) const noexcept {
    Fnv1a64 hash;
    std::uint64_t action_xor = 0;
    std::uint64_t action_sum = 0;
    for (const auto& action : actions_) {
        Fnv1a64 one;
        one.add(std::string_view{action.key});
        one.add(static_cast<std::uint8_t>(action.scope));
        one.add(action.base_utility);
        one.add(action.cooldown_ticks);
        const auto value = one.value();
        action_xor ^= value;
        action_sum += value * 0x9e3779b97f4a7c15ull;
    }
    hash.add(actions_.size()); hash.add(action_xor); hash.add(action_sum);

    std::uint64_t plan_xor = 0;
    std::uint64_t plan_sum = 0;
    for (const auto& plan : plans_) {
        Fnv1a64 one;
        one.add(std::string_view{plan.key});
        one.add(static_cast<std::uint8_t>(plan.scope));
        one.add(plan.base_priority);
        one.add(plan.commitment_ticks);
        std::uint64_t keys_xor = 0;
        std::uint64_t keys_sum = 0;
        for (const auto& key : plan.action_keys) {
            Fnv1a64 key_hash; key_hash.add(std::string_view{key});
            keys_xor ^= key_hash.value();
            keys_sum += key_hash.value() * 0x94d049bb133111ebull;
        }
        one.add(plan.action_keys.size()); one.add(keys_xor); one.add(keys_sum);
        const auto value = one.value();
        plan_xor ^= value;
        plan_sum += value * 0xbf58476d1ce4e5b9ull;
    }
    hash.add(plans_.size()); hash.add(plan_xor); hash.add(plan_sum);

    std::uint64_t state_xor = 0;
    std::uint64_t state_sum = 0;
    for (const auto& record : state) {
        Fnv1a64 one;
        if (record.action < actions_.size()) one.add(std::string_view{actions_[record.action].key});
        else one.add(record.action);
        one.add(static_cast<std::uint8_t>(record.scope.type)); one.add(record.scope.raw_id); one.add(record.last_tick);
        const auto value = one.value();
        state_xor ^= value; state_sum += value * 0x517cc1b727220a95ull;
    }
    hash.add(state.size()); hash.add(state_xor); hash.add(state_sum);

    std::uint64_t plan_state_xor = 0;
    std::uint64_t plan_state_sum = 0;
    for (const auto& record : plan_state) {
        Fnv1a64 one;
        if (record.plan < plans_.size()) one.add(std::string_view{plans_[record.plan].key});
        else one.add(record.plan);
        one.add(static_cast<std::uint8_t>(record.scope.type)); one.add(record.scope.raw_id);
        one.add(record.started_tick); one.add(record.last_tick);
        const auto value = one.value();
        plan_state_xor ^= value; plan_state_sum += value * 0xd6e8feb86659fd93ull;
    }
    hash.add(plan_state.size()); hash.add(plan_state_xor); hash.add(plan_state_sum);
    return hash.value();
}

std::uint64_t UtilityAiEngine::checksum() const noexcept {
    return checksum_state(state_, plan_state_);
}

DiplomaticPlayAiStance StrategicAiEvaluator::evaluate_diplomatic_play(const World& world, CountryId country, DiplomaticPlayId play) {
    if (!play.valid() || static_cast<std::size_t>(play.value()) >= world.grand_strategy.diplomatic_plays().size()) {
        return DiplomaticPlayAiStance::Hold;
    }
    const auto& p = world.grand_strategy.diplomatic_plays()[play.value()];
    if (country != p.initiator && country != p.target) {
        return DiplomaticPlayAiStance::Hold;
    }

    const auto opponent = (country == p.initiator) ? p.target : p.initiator;

    std::uint64_t my_manpower = 0;
    std::uint64_t opponent_manpower = 0;
    for (const auto& army : world.grand_strategy.armys()) {
        if (army.country == country) my_manpower += army.manpower;
        else if (army.country == opponent) opponent_manpower += army.manpower;
    }

    if (my_manpower == 0 && opponent_manpower > 10'000) {
        return DiplomaticPlayAiStance::Yield;
    }
    if (my_manpower >= opponent_manpower * 2 && opponent_manpower > 0) {
        return DiplomaticPlayAiStance::Escalate;
    }
    if (opponent_manpower > my_manpower) {
        return DiplomaticPlayAiStance::MobilizeAndSway;
    }
    return DiplomaticPlayAiStance::Hold;
}

bool StrategicAiEvaluator::should_ai_back_down(const World& world, CountryId country, DiplomaticPlayId play) {
    return evaluate_diplomatic_play(world, country, play) == DiplomaticPlayAiStance::Yield;
}

std::vector<CountryId> StrategicAiEvaluator::pick_sway_targets(const World& world, CountryId sponsor, DiplomaticPlayId play) {
    std::vector<CountryId> targets;
    if (!play.valid() || static_cast<std::size_t>(play.value()) >= world.grand_strategy.diplomatic_plays().size()) {
        return targets;
    }
    const auto& p = world.grand_strategy.diplomatic_plays()[play.value()];
    const auto opponent = (sponsor == p.initiator) ? p.target : p.initiator;

    for (std::size_t ci = 0; ci < world.countries.size(); ++ci) {
        const auto candidate = CountryId{static_cast<CountryId::rep_type>(ci)};
        if (candidate == sponsor || candidate == opponent) continue;

        if (world.grand_strategy.relation_milli(sponsor, candidate) > 0) {
            targets.push_back(candidate);
        }
    }
    return targets;
}

std::vector<GoodId> StrategicAiEvaluator::evaluate_economic_shortages(
    const World& world, CountryId country, const EconomyDefinitions& definitions) {
    std::vector<GoodId> shortages;
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const auto market = MarketId{static_cast<MarketId::rep_type>(mi)};
        if (world.markets.owner(market) == country) {
            const auto gc = world.markets.good_count();
            for (std::size_t gi = 0; gi < gc; ++gi) {
                const auto good = GoodId{static_cast<GoodId::rep_type>(gi)};
                // Judge scarcity against each good's own base price. The old
                // flat `> 1000` test treated expensive industrial goods as
                // permanently scarce (their base price alone clears it) while
                // ignoring a cheap raw material that had risen tenfold.
                const auto base = definitions.good(good).base_price_milli;
                if (base <= 0) continue;
                const auto price = world.markets.price(market, good);
                if (price * 100 > base * 150) {
                    shortages.push_back(good);
                }
            }
        }
    }
    return shortages;
}

void StrategicAiEvaluator::auto_balance_frontlines(World& world, CountryId country, FrontId front) {
    if (!front.valid() || static_cast<std::size_t>(front.value()) >= world.grand_strategy.fronts().size()) return;
    const auto& f = world.grand_strategy.fronts()[front.value()];
    if (country != f.first && country != f.second) return;

    for (auto& army : world.grand_strategy.armys_mut()) {
        if (army.country == country && army.location != f.state) {
            army.location = f.state;
        }
    }
}

} // namespace thunder


