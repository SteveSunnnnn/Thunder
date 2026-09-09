#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/scripting/Scope.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace thunder {

// Stable across symbol-table insertion order and content load order. ScriptStableKey is
// used for transient invocation bindings (parameters, variables, event targets and
// collections); SymbolId remains the compact compiler-local identifier.
using ScriptStableKey = std::uint64_t;

[[nodiscard]] inline ScriptStableKey script_stable_key(std::string_view text) noexcept {
    Fnv1a64 hash;
    hash.add(text);
    return hash.value();
}

enum class ScriptArgumentKind : std::uint8_t { None, Number, SymbolHash, Boolean, Scope };

// Resolved, trivially-copyable value passed to native primitives and stored in a
// ScriptExecutionContext. The unused fields are kept zeroed by the factories so values
// have deterministic equality/checksum behaviour without a discriminated union.
struct ScriptArgument {
    ScriptArgumentKind kind = ScriptArgumentKind::None;
    double number = 0.0;
    std::uint64_t symbol_hash = 0;
    ScopeRef scope_value{};

    [[nodiscard]] static constexpr ScriptArgument numeric(double value) noexcept {
        constexpr auto maximum = std::numeric_limits<double>::max();
        if (!(value >= -maximum && value <= maximum)) return {};
        if (value == 0.0) value = 0.0; // Canonicalize negative zero for checksum equality.
        return {ScriptArgumentKind::Number, value, 0, {}};
    }
    [[nodiscard]] static constexpr ScriptArgument symbol(std::uint64_t hash) noexcept {
        return {ScriptArgumentKind::SymbolHash, 0.0, hash, {}};
    }
    [[nodiscard]] static constexpr ScriptArgument boolean(bool value) noexcept {
        return {ScriptArgumentKind::Boolean, value ? 1.0 : 0.0, 0, {}};
    }
    [[nodiscard]] static constexpr ScriptArgument scope(ScopeRef value) noexcept {
        return {ScriptArgumentKind::Scope, 0.0, 0, value};
    }

    [[nodiscard]] constexpr bool valid() const noexcept { return kind != ScriptArgumentKind::None; }
    [[nodiscard]] constexpr bool boolean_value() const noexcept {
        return kind == ScriptArgumentKind::Boolean && number != 0.0;
    }

    friend constexpr bool operator==(const ScriptArgument&, const ScriptArgument&) = default;
};

struct ScriptArgumentHash {
    [[nodiscard]] std::size_t operator()(const ScriptArgument& value) const noexcept {
        Fnv1a64 hash;
        hash.add(static_cast<std::uint8_t>(value.kind));
        switch (value.kind) {
            case ScriptArgumentKind::None: break;
            case ScriptArgumentKind::Number: hash.add(value.number); break;
            case ScriptArgumentKind::SymbolHash: hash.add(value.symbol_hash); break;
            case ScriptArgumentKind::Boolean: hash.add(value.boolean_value()); break;
            case ScriptArgumentKind::Scope:
                hash.add(static_cast<std::uint8_t>(value.scope_value.type));
                hash.add(value.scope_value.raw_id);
                break;
        }
        return static_cast<std::size_t>(hash.value());
    }
};

struct ScriptNamedValue {
    ScriptStableKey name = 0;
    ScriptArgument value{};
};

} // namespace thunder
