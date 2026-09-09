#include "thunder/content/definition/ResearchContent.hpp"

#include "thunder/scripting/ScriptRegistry.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace thunder {
namespace {

TechnologyCategory category_from_text(std::string_view text) noexcept {
    if (text == "society") return TechnologyCategory::Society;
    if (text == "production") return TechnologyCategory::Production;
    if (text == "military") return TechnologyCategory::Military;
    return TechnologyCategory::Custom;
}

} // namespace

ResearchContentDatabase::ResearchContentDatabase(SymbolTable& symbols) : symbols_(symbols) {
    sym_technology_ = symbols_.intern("technology");
    sym_research_rules_ = symbols_.intern("research_rules");
    sym_category_ = symbols_.intern("category");
    sym_era_ = symbols_.intern("era");
    sym_cost_ = symbols_.intern("cost");
    sym_prerequisites_ = symbols_.intern("prerequisites");
    sym_prerequisite_ = symbols_.intern("prerequisite");
    sym_unlocks_ = symbols_.intern("unlocks");
    sym_potential_ = symbols_.intern("potential");
    sym_on_researched_ = symbols_.intern("on_researched");
    sym_base_innovation_ = symbols_.intern("base_innovation_milli");
    sym_literate_innovation_ = symbols_.intern("innovation_per_million_literate_population_milli");
    sym_max_innovation_ = symbols_.intern("max_innovation_milli");
    sym_tech_spread_rate_ = symbols_.intern("tech_spread_rate_ppm");
    sym_tech_spread_base_chance_ = symbols_.intern("tech_spread_base_chance_ppm");
    technology_lookup_.reserve(512u);
}

bool ResearchContentDatabase::parse_u16(const ScriptNode& node, std::uint16_t& value) const noexcept {
    if (node.kind != ScriptValueKind::Number || !std::isfinite(node.number) || node.number < 0.0 ||
        node.number > static_cast<double>(std::numeric_limits<std::uint16_t>::max()) ||
        std::floor(node.number) != node.number) return false;
    value = static_cast<std::uint16_t>(node.number);
    return true;
}

bool ResearchContentDatabase::parse_u32(const ScriptNode& node, std::uint32_t& value) const noexcept {
    if (node.kind != ScriptValueKind::Number || !std::isfinite(node.number) || node.number < 0.0 ||
        node.number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        std::floor(node.number) != node.number) return false;
    value = static_cast<std::uint32_t>(node.number);
    return true;
}

bool ResearchContentDatabase::parse_technology(
    const ScriptObject& object, TechnologyDefinitionSpec& out,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    out.key = object.name;
    out.line = object.line;
    bool ok = true;
    for (const auto& field : object.fields) {
        if (field.key == sym_category_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({"technology category requires a symbolic value", field.line});
                ok = false;
            } else out.category = field.symbol;
        } else if (field.key == sym_era_) {
            if (!parse_u16(field, out.era)) {
                diagnostics.push_back({"technology era requires an unsigned 16-bit integer", field.line});
                ok = false;
            }
        } else if (field.key == sym_cost_) {
            if (!parse_u32(field, out.cost_milli) || out.cost_milli == 0u) {
                diagnostics.push_back({"technology cost requires a positive integer", field.line});
                ok = false;
            }
        } else if (field.key == sym_prerequisite_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({"technology prerequisite requires a technology key", field.line});
                ok = false;
            } else out.prerequisites.push_back(field.symbol);
        } else if (field.key == sym_prerequisites_) {
            if (field.kind != ScriptValueKind::Block) {
                diagnostics.push_back({"technology prerequisites requires a block", field.line});
                ok = false;
                continue;
            }
            for (const auto& child : field.children) {
                if (child.kind != ScriptValueKind::Symbol) {
                    diagnostics.push_back({"technology prerequisite entry requires a technology key", child.line});
                    ok = false;
                } else out.prerequisites.push_back(child.symbol);
            }
        } else if (field.key == sym_unlocks_) {
            if (field.kind != ScriptValueKind::Block) {
                diagnostics.push_back({"technology unlocks requires a block", field.line});
                ok = false;
                continue;
            }
            for (const auto& child : field.children) {
                if (child.kind != ScriptValueKind::Symbol) {
                    diagnostics.push_back({"technology unlock entry requires a stable key", child.line});
                    ok = false;
                } else out.unlock_keys.push_back(child.symbol);
            }
        } else if (field.key == sym_potential_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({"technology potential requires a script name", field.line});
                ok = false;
            } else out.potential = field.symbol;
        } else if (field.key == sym_on_researched_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({"technology on_researched requires a script name", field.line});
                ok = false;
            } else out.on_researched = field.symbol;
        }
    }
    return ok;
}

bool ResearchContentDatabase::parse_rules(const ScriptObject& object, ResearchRules& out,
                                          std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    bool ok = true;
    for (const auto& field : object.fields) {
        std::uint32_t value = 0u;
        if (field.key == sym_base_innovation_) {
            if (!parse_u32(field, value)) { diagnostics.push_back({"base innovation requires an unsigned integer", field.line}); ok = false; }
            else out.base_innovation_milli = value;
        } else if (field.key == sym_literate_innovation_) {
            if (!parse_u32(field, value)) { diagnostics.push_back({"literate-population innovation requires an unsigned integer", field.line}); ok = false; }
            else out.innovation_per_million_literate_population_milli = value;
        } else if (field.key == sym_max_innovation_) {
            if (!parse_u32(field, value) || value == 0u) { diagnostics.push_back({"max innovation requires a positive integer", field.line}); ok = false; }
            else out.max_innovation_milli = value;
        } else if (field.key == sym_tech_spread_rate_) {
            if (!parse_u32(field, value)) { diagnostics.push_back({"tech spread rate requires an unsigned integer", field.line}); ok = false; }
            else out.tech_spread_rate_ppm = value;
        } else if (field.key == sym_tech_spread_base_chance_) {
            if (!parse_u32(field, value)) { diagnostics.push_back({"tech spread base chance requires an unsigned integer", field.line}); ok = false; }
            else out.tech_spread_base_chance_ppm = value;
        }
    }
    if (out.base_innovation_milli > out.max_innovation_milli ||
        out.innovation_per_million_literate_population_milli > out.max_innovation_milli) {
        diagnostics.push_back({"research rule inputs must not exceed max_innovation_milli", object.line});
        ok = false;
    }
    return ok;
}

bool ResearchContentDatabase::ingest(const ScriptParseResult& parsed,
                                     std::vector<ScriptCompileDiagnostic>& diagnostics) {
    // Start from parsed.ok(), matching the other content databases: a file with
    // syntax errors must not contribute technologies when this database is
    // ingested on its own.
    bool ok = parsed.ok();
    for (const auto& object : parsed.objects) {
        if (object.type == sym_technology_) {
            TechnologyDefinitionSpec spec;
            if (!parse_technology(object, spec, diagnostics)) { ok = false; continue; }
            const auto found = technology_lookup_.find(spec.key.value());
            if (found == technology_lookup_.end()) {
                const auto index = static_cast<std::uint32_t>(technologies_.size());
                technology_lookup_.emplace(spec.key.value(), index);
                technologies_.push_back(std::move(spec));
            } else {
                technologies_[found->second] = std::move(spec);
            }
        } else if (object.type == sym_research_rules_) {
            ResearchRules replacement = rules_;
            if (parse_rules(object, replacement, diagnostics)) rules_ = replacement;
            else ok = false;
        }
    }
    return ok;
}

bool ResearchContentDatabase::bind(const ScriptProgramDatabase& programs, ResearchSystem& research,
                                   std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    research.clear_content();
    research.set_program_database(&programs);
    try {
        research.set_rules(rules_);
    } catch (const std::exception& error) {
        diagnostics.push_back({error.what(), 0u});
        return false;
    }

    bool ok = true;
    const auto resolve_script = [&](SymbolId id, std::uint32_t line, std::string_view purpose,
                                    std::optional<ScriptProgram>& output) {
        if (!id.valid()) return true;
        const auto* program = programs.find_script(id);
        if (program == nullptr) {
            diagnostics.push_back({std::string{"technology "} + std::string{purpose} +
                                   " references unknown script: " + std::string{symbols_.text(id)}, line});
            return false;
        }
        if (program->scope != ScopeType::Country) {
            diagnostics.push_back({std::string{"technology "} + std::string{purpose} +
                                   " script must use country scope", line});
            return false;
        }
        output = *program;
        return true;
    };

    for (const auto& spec : technologies_) {
        TechnologyDefinition definition;
        definition.key = std::string{symbols_.text(spec.key)};
        definition.key_hash = script_symbol_hash(definition.key);
        definition.category = spec.category.valid()
            ? category_from_text(symbols_.text(spec.category))
            : TechnologyCategory::Custom;
        definition.era = spec.era;
        definition.cost_milli = spec.cost_milli;
        definition.prerequisites.reserve(spec.prerequisites.size());
        for (const auto key : spec.prerequisites)
            definition.prerequisites.push_back(script_symbol_hash(symbols_.text(key)));
        definition.unlock_keys.reserve(spec.unlock_keys.size());
        for (const auto key : spec.unlock_keys)
            definition.unlock_keys.push_back(script_symbol_hash(symbols_.text(key)));
        bool definition_ok = resolve_script(spec.potential, spec.line, "potential", definition.potential);
        definition_ok &= resolve_script(spec.on_researched, spec.line, "on_researched", definition.on_researched);
        if (!definition_ok) { ok = false; continue; }
        try {
            (void)research.add_or_replace_definition(std::move(definition));
        } catch (const std::exception& error) {
            diagnostics.push_back({error.what(), spec.line});
            ok = false;
        }
    }

    std::vector<std::string> graph_diagnostics;
    if (!research.finalize_definitions(graph_diagnostics)) ok = false;
    for (auto& diagnostic : graph_diagnostics) diagnostics.push_back({std::move(diagnostic), 0u});
    return ok;
}

std::size_t ResearchContentDatabase::memory_bytes() const noexcept {
    std::size_t bytes = technologies_.capacity() * sizeof(TechnologyDefinitionSpec);
    for (const auto& definition : technologies_) {
        bytes += definition.prerequisites.capacity() * sizeof(SymbolId);
        bytes += definition.unlock_keys.capacity() * sizeof(SymbolId);
    }
    bytes += technology_lookup_.size() * (sizeof(std::uint32_t) * 2u);
    return bytes;
}

} // namespace thunder
