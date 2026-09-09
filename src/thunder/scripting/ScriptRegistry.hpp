#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/scripting/Scope.hpp"
#include "thunder/scripting/ScriptValue.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace thunder {

class World;

struct TriggerPrimitiveTag {};
struct EffectPrimitiveTag {};
using TriggerPrimitiveId = StrongId<TriggerPrimitiveTag, std::uint16_t>;
using EffectPrimitiveId = StrongId<EffectPrimitiveTag, std::uint16_t>;

[[nodiscard]] inline std::uint64_t script_symbol_hash(std::string_view text) noexcept {
    return script_stable_key(text);
}

struct ScriptStringHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return (*this)(std::string_view{value});
    }
};
struct ScriptStringEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

class ScriptRegistry {
public:
    using Trigger = bool (*)(const World&, ScopeRef, double);
    using Effect = void (*)(World&, ScopeRef, double);
    using TypedTrigger = bool (*)(const World&, ScopeRef, ScriptArgument);
    using TypedEffect = void (*)(World&, ScopeRef, ScriptArgument);

    TriggerPrimitiveId register_trigger(std::string name, ScopeType scope, Trigger trigger);
    EffectPrimitiveId register_effect(std::string name, ScopeType scope, Effect effect);
    TriggerPrimitiveId register_typed_trigger(std::string name, ScopeType scope, TypedTrigger trigger,
                                               bool accepts_number = true, bool accepts_symbol = true,
                                               bool accepts_boolean = false, bool accepts_scope = false);
    EffectPrimitiveId register_typed_effect(std::string name, ScopeType scope, TypedEffect effect,
                                            bool accepts_number = true, bool accepts_symbol = true,
                                            bool accepts_boolean = false, bool accepts_scope = false);

    [[nodiscard]] TriggerPrimitiveId find_trigger(std::string_view name) const noexcept;
    [[nodiscard]] EffectPrimitiveId find_effect(std::string_view name) const noexcept;
    [[nodiscard]] ScopeType trigger_scope(TriggerPrimitiveId id) const;
    [[nodiscard]] ScopeType effect_scope(EffectPrimitiveId id) const;
    [[nodiscard]] bool trigger_accepts_argument(TriggerPrimitiveId id, ScriptArgumentKind kind) const;
    [[nodiscard]] bool effect_accepts_argument(EffectPrimitiveId id, ScriptArgumentKind kind) const;

    [[nodiscard]] bool evaluate_trigger(TriggerPrimitiveId id, const World& world, ScopeRef scope, double argument) const;
    [[nodiscard]] bool evaluate_trigger(TriggerPrimitiveId id, const World& world, ScopeRef scope, ScriptArgument argument) const;
    void execute_effect(EffectPrimitiveId id, World& world, ScopeRef scope, double argument) const;
    void execute_effect(EffectPrimitiveId id, World& world, ScopeRef scope, ScriptArgument argument) const;

    [[nodiscard]] bool evaluate_trigger(std::string_view name, const World& world, ScopeRef scope, double argument) const;
    void execute_effect(std::string_view name, World& world, ScopeRef scope, double argument) const;

    [[nodiscard]] std::size_t trigger_count() const noexcept { return triggers_.size(); }
    [[nodiscard]] std::size_t effect_count() const noexcept { return effects_.size(); }

    [[nodiscard]] std::string export_markdown_docs() const;
    [[nodiscard]] std::string export_schema_json() const;

    static ScriptRegistry make_builtin();

private:
    static constexpr std::uint8_t number_argument_bit = 1u << 0u;
    static constexpr std::uint8_t symbol_argument_bit = 1u << 1u;
    static constexpr std::uint8_t boolean_argument_bit = 1u << 2u;
    static constexpr std::uint8_t scope_argument_bit = 1u << 3u;
    struct TriggerEntry {
        std::string name; ScopeType scope; Trigger callback = nullptr; TypedTrigger typed_callback = nullptr;
        std::uint8_t argument_mask = number_argument_bit;
    };
    struct EffectEntry {
        std::string name; ScopeType scope; Effect callback = nullptr; TypedEffect typed_callback = nullptr;
        std::uint8_t argument_mask = number_argument_bit;
    };
    std::vector<TriggerEntry> triggers_;
    std::vector<EffectEntry> effects_;
    std::unordered_map<std::string, TriggerPrimitiveId, ScriptStringHash, ScriptStringEqual> trigger_lookup_;
    std::unordered_map<std::string, EffectPrimitiveId, ScriptStringHash, ScriptStringEqual> effect_lookup_;
};

} // namespace thunder
