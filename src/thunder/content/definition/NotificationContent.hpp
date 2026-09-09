#pragma once

#include "thunder/simulation/gameplay/NotificationRuntime.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace thunder {

struct NotificationActionSpec {
    SymbolId key{};
    SymbolId label{};
    SymbolId allow{};
    SymbolId effect{};
    std::uint32_t line = 0u;
};

struct NotificationDefinitionSpec {
    SymbolId key{};
    ScopeType scope = ScopeType::None;
    SymbolId title{};
    SymbolId body{};
    SymbolId icon{};
    SymbolId category{};
    NotificationPriority priority = NotificationPriority::Normal;
    NotificationDedupePolicy dedupe = NotificationDedupePolicy::Stack;
    std::uint64_t lifetime_ticks = 0u;
    SymbolId potential{};
    std::vector<NotificationActionSpec> actions;
    std::uint32_t line = 0u;
};

class NotificationContentDatabase {
public:
    explicit NotificationContentDatabase(SymbolTable& symbols);

    bool ingest(const ScriptParseResult& parsed,
                std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool bind(const ScriptProgramDatabase& programs, NotificationRuntime& notifications,
              std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    [[nodiscard]] std::span<const NotificationDefinitionSpec> notifications() const noexcept {
        return notifications_;
    }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] ScopeType parse_scope(const ScriptNode& node) const noexcept;
    [[nodiscard]] bool parse_u64(const ScriptNode& node, std::uint64_t& value) const noexcept;
    bool parse_notification(const ScriptObject& object, NotificationDefinitionSpec& out,
                            std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    SymbolTable& symbols_;
    SymbolId sym_notification_{};
    SymbolId sym_scope_{};
    SymbolId sym_title_{};
    SymbolId sym_body_{};
    SymbolId sym_icon_{};
    SymbolId sym_category_{};
    SymbolId sym_priority_{};
    SymbolId sym_dedupe_{};
    SymbolId sym_lifetime_ticks_{};
    SymbolId sym_potential_{};
    SymbolId sym_action_{};
    SymbolId sym_key_{};
    SymbolId sym_label_{};
    SymbolId sym_allow_{};
    SymbolId sym_effect_{};

    std::vector<NotificationDefinitionSpec> notifications_;
    std::unordered_map<std::uint32_t, std::uint32_t> lookup_;
};

} // namespace thunder
