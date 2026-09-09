#include "thunder/content/definition/NotificationContent.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace thunder {
namespace {

bool read_symbol(const ScriptNode& node, SymbolId& output) noexcept {
    if (node.kind != ScriptValueKind::Symbol) return false;
    output = node.symbol;
    return true;
}

} // namespace

NotificationContentDatabase::NotificationContentDatabase(SymbolTable& symbols) : symbols_(symbols) {
    sym_notification_ = symbols_.intern("notification");
    sym_scope_ = symbols_.intern("scope");
    sym_title_ = symbols_.intern("title");
    sym_body_ = symbols_.intern("body");
    sym_icon_ = symbols_.intern("icon");
    sym_category_ = symbols_.intern("category");
    sym_priority_ = symbols_.intern("priority");
    sym_dedupe_ = symbols_.intern("dedupe");
    sym_lifetime_ticks_ = symbols_.intern("lifetime_ticks");
    sym_potential_ = symbols_.intern("potential");
    sym_action_ = symbols_.intern("action");
    sym_key_ = symbols_.intern("key");
    sym_label_ = symbols_.intern("label");
    sym_allow_ = symbols_.intern("allow");
    sym_effect_ = symbols_.intern("effect");
    lookup_.reserve(256u);
}

ScopeType NotificationContentDatabase::parse_scope(const ScriptNode& node) const noexcept {
    if (node.kind != ScriptValueKind::Symbol) return ScopeType::None;
    const auto text = symbols_.text(node.symbol);
    if (text == "country") return ScopeType::Country;
    if (text == "state") return ScopeType::State;
    if (text == "province") return ScopeType::Province;
    if (text == "pop") return ScopeType::Pop;
    if (text == "market") return ScopeType::Market;
    return ScopeType::None;
}

bool NotificationContentDatabase::parse_u64(const ScriptNode& node,
                                             std::uint64_t& value) const noexcept {
    // ThunderScript numeric literals are doubles. Refuse integers outside the exact range so
    // content cannot silently produce a platform-dependent authoritative lifetime.
    constexpr double max_exact_integer = 9'007'199'254'740'991.0;
    if (node.kind != ScriptValueKind::Number || !std::isfinite(node.number) ||
        node.number < 0.0 || node.number > max_exact_integer ||
        std::floor(node.number) != node.number) return false;
    value = static_cast<std::uint64_t>(node.number);
    return true;
}

bool NotificationContentDatabase::parse_notification(
    const ScriptObject& object, NotificationDefinitionSpec& out,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    out.key = object.name;
    out.line = object.line;
    bool ok = true;
    std::unordered_set<std::uint32_t> action_keys;
    for (const auto& field : object.fields) {
        if (field.key == sym_scope_) {
            out.scope = parse_scope(field);
            if (out.scope == ScopeType::None) {
                diagnostics.push_back({"notification scope requires country/state/province/pop/market", field.line});
                ok = false;
            }
        } else if (field.key == sym_title_) {
            if (!read_symbol(field, out.title)) {
                diagnostics.push_back({"notification title requires a localization key", field.line});
                ok = false;
            }
        } else if (field.key == sym_body_) {
            if (!read_symbol(field, out.body)) {
                diagnostics.push_back({"notification body requires a localization key", field.line});
                ok = false;
            }
        } else if (field.key == sym_icon_) {
            if (!read_symbol(field, out.icon)) {
                diagnostics.push_back({"notification icon requires a stable asset key", field.line});
                ok = false;
            }
        } else if (field.key == sym_category_) {
            if (!read_symbol(field, out.category)) {
                diagnostics.push_back({"notification category requires a stable key", field.line});
                ok = false;
            }
        } else if (field.key == sym_priority_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({"notification priority requires low/normal/high/critical", field.line});
                ok = false;
            } else {
                const auto text = symbols_.text(field.symbol);
                if (text == "low") out.priority = NotificationPriority::Low;
                else if (text == "normal") out.priority = NotificationPriority::Normal;
                else if (text == "high") out.priority = NotificationPriority::High;
                else if (text == "critical") out.priority = NotificationPriority::Critical;
                else {
                    diagnostics.push_back({"notification priority requires low/normal/high/critical", field.line});
                    ok = false;
                }
            }
        } else if (field.key == sym_dedupe_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({"notification dedupe requires stack/suppress/replace", field.line});
                ok = false;
            } else {
                const auto text = symbols_.text(field.symbol);
                if (text == "stack") out.dedupe = NotificationDedupePolicy::Stack;
                else if (text == "suppress") out.dedupe = NotificationDedupePolicy::Suppress;
                else if (text == "replace") out.dedupe = NotificationDedupePolicy::Replace;
                else {
                    diagnostics.push_back({"notification dedupe requires stack/suppress/replace", field.line});
                    ok = false;
                }
            }
        } else if (field.key == sym_lifetime_ticks_) {
            if (!parse_u64(field, out.lifetime_ticks)) {
                diagnostics.push_back({"notification lifetime_ticks requires an exact non-negative integer", field.line});
                ok = false;
            }
        } else if (field.key == sym_potential_) {
            if (!read_symbol(field, out.potential)) {
                diagnostics.push_back({"notification potential requires a script name", field.line});
                ok = false;
            }
        } else if (field.key == sym_action_) {
            if (field.kind != ScriptValueKind::Block) {
                diagnostics.push_back({"notification action requires a block", field.line});
                ok = false;
                continue;
            }
            NotificationActionSpec action;
            action.line = field.line;
            for (const auto& child : field.children) {
                if (child.key == sym_key_) {
                    if (!read_symbol(child, action.key)) {
                        diagnostics.push_back({"notification action key requires a stable symbol", child.line});
                        ok = false;
                    }
                } else if (child.key == sym_label_) {
                    if (!read_symbol(child, action.label)) {
                        diagnostics.push_back({"notification action label requires a localization key", child.line});
                        ok = false;
                    }
                } else if (child.key == sym_allow_) {
                    if (!read_symbol(child, action.allow)) {
                        diagnostics.push_back({"notification action allow requires a script name", child.line});
                        ok = false;
                    }
                } else if (child.key == sym_effect_) {
                    if (!read_symbol(child, action.effect)) {
                        diagnostics.push_back({"notification action effect requires a script name", child.line});
                        ok = false;
                    }
                }
            }
            if (!action.key.valid()) {
                diagnostics.push_back({"notification action requires key", field.line});
                ok = false;
            } else if (!action_keys.insert(action.key.value()).second) {
                diagnostics.push_back({"notification contains duplicate action key", field.line});
                ok = false;
            }
            if (!action.label.valid()) {
                diagnostics.push_back({"notification action requires label", field.line});
                ok = false;
            }
            if (action.key.valid() && action.label.valid()) out.actions.push_back(action);
        }
    }
    if (out.scope == ScopeType::None) {
        diagnostics.push_back({"notification requires scope", object.line});
        ok = false;
    }
    if (!out.title.valid()) {
        diagnostics.push_back({"notification requires title", object.line});
        ok = false;
    }
    if (!out.body.valid()) {
        diagnostics.push_back({"notification requires body", object.line});
        ok = false;
    }
    return ok;
}

bool NotificationContentDatabase::ingest(
    const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    bool ok = parsed.ok();
    for (const auto& object : parsed.objects) {
        if (object.type != sym_notification_) continue;
        NotificationDefinitionSpec spec;
        if (!parse_notification(object, spec, diagnostics)) {
            ok = false;
            continue;
        }
        const auto found = lookup_.find(spec.key.value());
        if (found == lookup_.end()) {
            const auto index = static_cast<std::uint32_t>(notifications_.size());
            lookup_.emplace(spec.key.value(), index);
            notifications_.push_back(std::move(spec));
        } else {
            notifications_[found->second] = std::move(spec);
        }
    }
    return ok;
}

bool NotificationContentDatabase::bind(
    const ScriptProgramDatabase& programs, NotificationRuntime& notifications,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    notifications.clear_content();
    notifications.set_program_database(&programs);
    bool ok = true;
    const auto resolve_script = [&](SymbolId name, ScopeType scope, std::uint32_t line,
                                    std::string_view role,
                                    std::optional<ScriptProgram>& output) {
        if (!name.valid()) return true;
        const auto* program = programs.find_script(name);
        if (program == nullptr) {
            diagnostics.push_back({std::string{role} + " references unknown script: " +
                                   std::string{symbols_.text(name)}, line});
            return false;
        }
        if (program->scope != scope) {
            diagnostics.push_back({std::string{role} + " script scope mismatch: " +
                                   std::string{symbols_.text(name)}, line});
            return false;
        }
        output = *program;
        return true;
    };

    for (const auto& spec : notifications_) {
        NotificationDefinition definition;
        definition.key = std::string{symbols_.text(spec.key)};
        definition.scope = spec.scope;
        definition.title_key = std::string{symbols_.text(spec.title)};
        definition.body_key = std::string{symbols_.text(spec.body)};
        if (spec.icon.valid()) definition.icon_key = std::string{symbols_.text(spec.icon)};
        if (spec.category.valid()) definition.category_key = std::string{symbols_.text(spec.category)};
        definition.priority = spec.priority;
        definition.dedupe = spec.dedupe;
        definition.lifetime_ticks = spec.lifetime_ticks;
        bool definition_ok = resolve_script(spec.potential, spec.scope, spec.line,
                                            "notification potential", definition.potential);
        definition.actions.reserve(spec.actions.size());
        for (const auto& action_spec : spec.actions) {
            NotificationActionDefinition action;
            action.key = std::string{symbols_.text(action_spec.key)};
            action.label_key = std::string{symbols_.text(action_spec.label)};
            definition_ok &= resolve_script(action_spec.allow, spec.scope, action_spec.line,
                                            "notification action allow", action.allow);
            definition_ok &= resolve_script(action_spec.effect, spec.scope, action_spec.line,
                                            "notification action effect", action.effect);
            definition.actions.push_back(std::move(action));
        }
        if (!definition_ok) {
            ok = false;
            continue;
        }
        try {
            (void)notifications.add_definition(std::move(definition));
        } catch (const std::exception& error) {
            diagnostics.push_back({error.what(), spec.line});
            ok = false;
        }
    }
    return ok;
}

std::size_t NotificationContentDatabase::memory_bytes() const noexcept {
    std::size_t bytes = notifications_.capacity() * sizeof(NotificationDefinitionSpec);
    for (const auto& definition : notifications_)
        bytes += definition.actions.capacity() * sizeof(NotificationActionSpec);
    bytes += lookup_.size() * (sizeof(std::uint32_t) * 2u);
    return bytes;
}

} // namespace thunder
