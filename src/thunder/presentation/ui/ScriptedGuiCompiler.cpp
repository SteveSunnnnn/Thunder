#include "thunder/presentation/ui/ScriptedGui.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace thunder {

namespace {

[[nodiscard]] std::string value_type_name(UiValueType type) {
    switch (type) {
    case UiValueType::None: return "none";
    case UiValueType::Boolean: return "boolean";
    case UiValueType::Number: return "number";
    case UiValueType::Text: return "text";
    case UiValueType::LocalizationKey: return "localization_key";
    case UiValueType::Asset: return "asset";
    case UiValueType::Color: return "color";
    case UiValueType::Entity: return "entity";
    case UiValueType::Collection: return "collection";
    case UiValueType::NumberSeries: return "number_series";
    case UiValueType::TextSeries: return "text_series";
    }
    return "unknown";
}

struct GuiObjectDecl {
    SymbolId name{};
    UiStableKey stable_key = 0;
    UiDataContextId context{};
    const ScriptNode* root = nullptr;
    std::uint32_t line = 0;
};

struct TemplateEdge {
    UiTemplateId target{};
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct BoundProperty {
    UiBindingId binding{};
    const UiDataPropertySchema* property = nullptr;
};

struct NodeKeyRecord {
    UiStableKey key = 0;
    SymbolId symbol{};
};

[[nodiscard]] std::optional<UiWidgetKind> widget_kind(std::string_view name) noexcept {
    if (name == "panel") return UiWidgetKind::Panel;
    if (name == "row") return UiWidgetKind::Row;
    if (name == "column") return UiWidgetKind::Column;
    if (name == "stack") return UiWidgetKind::Stack;
    if (name == "label") return UiWidgetKind::Label;
    if (name == "button") return UiWidgetKind::Button;
    if (name == "image") return UiWidgetKind::Image;
    if (name == "module") return UiWidgetKind::Module;
    if (name == "scroll_view") return UiWidgetKind::ScrollView;
    if (name == "list") return UiWidgetKind::List;
    if (name == "grid") return UiWidgetKind::Grid;
    if (name == "chart") return UiWidgetKind::Chart;
    if (name == "spacer") return UiWidgetKind::Spacer;
    if (name == "progress") return UiWidgetKind::Progress;
    if (name == "template_instance" || name == "use") return UiWidgetKind::TemplateInstance;
    return std::nullopt;
}

[[nodiscard]] bool is_container(UiWidgetKind kind) noexcept {
    return kind == UiWidgetKind::Panel || kind == UiWidgetKind::Row ||
           kind == UiWidgetKind::Column || kind == UiWidgetKind::Stack ||
           kind == UiWidgetKind::ScrollView || kind == UiWidgetKind::Button;
}

[[nodiscard]] bool parse_bool_symbol(std::string_view text, bool& value) noexcept {
    if (text == "yes" || text == "true" || text == "on") { value = true; return true; }
    if (text == "no" || text == "false" || text == "off") { value = false; return true; }
    return false;
}

} // namespace

class ScriptedGuiCompilePass {
public:
    ScriptedGuiCompilePass(SymbolTable& symbols, const ScriptedGuiSchema& schema,
                           const ScriptParseResult& parsed)
        : symbols_(symbols), schema_(schema), parsed_(parsed) {}

    ScriptedGuiCompileResult run() {
        collect_objects();
        if (&schema_.symbols() != &symbols_) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "scripted GUI schema and source must use the same SymbolTable", 0u, 0u);
            return std::move(result_);
        }
        assign_records();
        compile_roots();
        detect_template_cycles();
        return std::move(result_);
    }

private:
    [[nodiscard]] std::string_view text(SymbolId symbol) const noexcept {
        try { return symbols_.text(symbol); } catch (...) { return {}; }
    }

    void diagnostic(ScriptedGuiDiagnosticCode code, std::string message,
                    std::uint32_t line, std::uint32_t column) {
        result_.diagnostics.push_back({code, std::move(message), line, column});
    }

    [[nodiscard]] const ScriptNode* field(std::span<const ScriptNode> fields,
                                          std::string_view wanted) const noexcept {
        for (const auto& value : fields) if (text(value.key) == wanted) return &value;
        return nullptr;
    }

    void validate_unique_fields(std::span<const ScriptNode> fields) {
        for (std::size_t index = 0; index < fields.size(); ++index) {
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (fields[index].key == fields[previous].key) {
                    diagnostic(ScriptedGuiDiagnosticCode::DuplicateField,
                               "duplicate scripted GUI field '" + std::string{text(fields[index].key)} + "'",
                               fields[index].line, fields[index].column);
                    break;
                }
            }
        }
    }

    void collect_objects() {
        for (const auto& object : parsed_.objects) {
            const auto type_name = text(object.type);
            const bool is_template = type_name == "ui_template" || type_name == "gui_template";
            const bool is_screen = type_name == "scripted_gui" || type_name == "gui_screen";
            if (!is_template && !is_screen) continue;
            validate_unique_fields(object.fields);
            for (const auto& value : object.fields) {
                const auto key = text(value.key);
                if (key != "context" && key != "root")
                    diagnostic(ScriptedGuiDiagnosticCode::UnknownField,
                               "unknown top-level scripted GUI field '" + std::string{key} + "'",
                               value.line, value.column);
            }
            GuiObjectDecl declaration;
            declaration.name = object.name;
            declaration.stable_key = ui_stable_key(text(object.name));
            declaration.line = object.line;
            const auto* context_field = field(object.fields, "context");
            if (context_field == nullptr) {
                diagnostic(ScriptedGuiDiagnosticCode::MissingField,
                           "scripted GUI object requires context", object.line, 0u);
            } else if (context_field->kind != ScriptValueKind::Symbol) {
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "scripted GUI context must be an identifier", context_field->line,
                           context_field->column);
            } else {
                declaration.context = schema_.find_context(context_field->symbol);
                if (!declaration.context.valid())
                    diagnostic(ScriptedGuiDiagnosticCode::UnknownContext,
                               "unknown scripted GUI context '" + std::string{text(context_field->symbol)} + "'",
                               context_field->line, context_field->column);
            }
            const auto* root_field = field(object.fields, "root");
            if (root_field == nullptr) {
                diagnostic(ScriptedGuiDiagnosticCode::MissingField,
                           "scripted GUI object requires root", object.line, 0u);
            } else if (root_field->kind != ScriptValueKind::Block) {
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "scripted GUI root must be a widget block", root_field->line,
                           root_field->column);
            } else {
                declaration.root = root_field;
            }
            auto& declarations = is_template ? templates_ : screens_;
            bool duplicate = false;
            for (const auto& previous : declarations) {
                if (previous.name == declaration.name) {
                    diagnostic(ScriptedGuiDiagnosticCode::DuplicateObject,
                               "duplicate scripted GUI object '" + std::string{text(object.name)} + "'",
                               object.line, 0u);
                    duplicate = true;
                    break;
                }
                if (previous.stable_key == declaration.stable_key) {
                    diagnostic(ScriptedGuiDiagnosticCode::StableKeyCollision,
                               "scripted GUI object stable-key collision", object.line, 0u);
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) declarations.push_back(declaration);
        }
        const auto by_key = [](const GuiObjectDecl& left, const GuiObjectDecl& right) {
            return left.stable_key < right.stable_key;
        };
        std::sort(templates_.begin(), templates_.end(), by_key);
        std::sort(screens_.begin(), screens_.end(), by_key);
    }

    void assign_records() {
        result_.blueprint.templates_.reserve(templates_.size());
        for (const auto& declaration : templates_)
            result_.blueprint.templates_.push_back({declaration.stable_key, declaration.context, {}});
        result_.blueprint.screens_.reserve(screens_.size());
        for (const auto& declaration : screens_)
            result_.blueprint.screens_.push_back({declaration.stable_key, declaration.context, {}});
        template_edges_.resize(templates_.size());
    }

    [[nodiscard]] UiTemplateId find_template(SymbolId name) const noexcept {
        for (std::size_t index = 0; index < templates_.size(); ++index) {
            if (templates_[index].name == name)
                return UiTemplateId{static_cast<UiTemplateId::rep_type>(index)};
        }
        return {};
    }

    void compile_roots() {
        result_.blueprint.nodes_.reserve((templates_.size() + screens_.size()) * 8u);
        for (std::size_t index = 0; index < templates_.size(); ++index) {
            const auto& declaration = templates_[index];
            if (declaration.root == nullptr || !declaration.context.valid()) continue;
            std::vector<NodeKeyRecord> node_keys;
            const UiTemplateId owner{static_cast<UiTemplateId::rep_type>(index)};
            result_.blueprint.templates_[index].root = compile_node(
                *declaration.root, {}, declaration.context, {}, declaration.stable_key,
                owner, node_keys);
        }
        for (std::size_t index = 0; index < screens_.size(); ++index) {
            const auto& declaration = screens_[index];
            if (declaration.root == nullptr || !declaration.context.valid()) continue;
            std::vector<NodeKeyRecord> node_keys;
            result_.blueprint.screens_[index].root = compile_node(
                *declaration.root, {}, declaration.context, {}, declaration.stable_key,
                {}, node_keys);
        }
    }

    [[nodiscard]] bool allowed_field(UiWidgetKind kind, std::string_view key) const noexcept {
        if (key == "type" || key == "id" || key == "visible" || key == "enabled" ||
            key == "command" || key == "tooltip" || key == "width" || key == "height" ||
            key == "min_width" || key == "min_height" || key == "max_width" ||
            key == "max_height" || key == "grow") return true;
        if (key == "children") return is_container(kind);
        if (key == "gap") return kind == UiWidgetKind::Row || kind == UiWidgetKind::Column;
        if (key == "text") return kind == UiWidgetKind::Label || kind == UiWidgetKind::Button ||
                                  kind == UiWidgetKind::Panel;
        if (key == "icon") return kind == UiWidgetKind::Image || kind == UiWidgetKind::Button;
        if (key == "module") return kind == UiWidgetKind::Module;
        if (key == "selected") return kind == UiWidgetKind::Button;
        if (key == "hovered" || key == "pressed" || key == "focused")
            return kind == UiWidgetKind::Button;
        if (key == "style") return kind == UiWidgetKind::Panel ||
                                   kind == UiWidgetKind::Button ||
                                   kind == UiWidgetKind::Label ||
                                   kind == UiWidgetKind::Image;
        if (key == "value") return kind == UiWidgetKind::Progress;
        if (key == "template") return kind == UiWidgetKind::TemplateInstance;
        if (key == "items" || key == "item_template" || key == "row_height" ||
            key == "overscan" || key == "virtualized")
            return kind == UiWidgetKind::List || kind == UiWidgetKind::Grid;
        if (key == "columns" || key == "column_gap" || key == "row_gap")
            return kind == UiWidgetKind::Grid;
        if (key == "series" || key == "labels" || key == "chart_kind" ||
            key == "max_points" || key == "include_zero") return kind == UiWidgetKind::Chart;
        return false;
    }

    [[nodiscard]] std::optional<UiWidgetKind> resolve_kind(const ScriptNode& block,
                                                            std::string_view implied) {
        const auto* type_field = field(block.children, "type");
        if (!implied.empty() && implied != "widget") {
            const auto implied_kind = widget_kind(implied);
            if (!implied_kind) return std::nullopt;
            if (type_field != nullptr) {
                if (type_field->kind != ScriptValueKind::Symbol ||
                    widget_kind(text(type_field->symbol)) != implied_kind) {
                    diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                               "widget shorthand conflicts with explicit type",
                               type_field->line, type_field->column);
                }
            }
            return implied_kind;
        }
        if (type_field == nullptr) {
            if (field(block.children, "template") != nullptr) return UiWidgetKind::TemplateInstance;
            diagnostic(ScriptedGuiDiagnosticCode::MissingField,
                       "widget requires type", block.line, block.column);
            return std::nullopt;
        }
        if (type_field->kind != ScriptValueKind::Symbol) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "widget type must be an identifier", type_field->line, type_field->column);
            return std::nullopt;
        }
        const auto kind = widget_kind(text(type_field->symbol));
        if (!kind)
            diagnostic(ScriptedGuiDiagnosticCode::UnknownWidget,
                       "unknown widget type '" + std::string{text(type_field->symbol)} + "'",
                       type_field->line, type_field->column);
        return kind;
    }

    void constant_number(const ScriptNode* value, UiConstantTarget target) {
        if (value == nullptr) return;
        if (value->kind != ScriptValueKind::Number || !std::isfinite(value->number)) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "numeric widget property requires a finite number", value->line, value->column);
            return;
        }
        result_.blueprint.constants_.push_back(
            {std::bit_cast<std::uint64_t>(value->number), target, UiValueType::Number});
    }

    void constant_symbol(const ScriptNode* value, UiConstantTarget target, UiValueType type_value) {
        if (value == nullptr) return;
        if (value->kind != ScriptValueKind::Symbol) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "symbol widget property requires an identifier or quoted string",
                       value->line, value->column);
            return;
        }
        const std::uint64_t payload = ui_stable_key(text(value->symbol));
        result_.blueprint.constants_.push_back({payload, target, type_value});
    }

    void constant_bool(const ScriptNode* value, UiConstantTarget target) {
        if (value == nullptr) return;
        bool parsed = false;
        bool bool_value = false;
        if (value->kind == ScriptValueKind::Symbol)
            parsed = parse_bool_symbol(text(value->symbol), bool_value);
        if (!parsed) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "boolean widget property requires yes/no", value->line, value->column);
            return;
        }
        result_.blueprint.constants_.push_back(
            {bool_value ? 1u : 0u, target, UiValueType::Boolean});
    }

    [[nodiscard]] BoundProperty compile_binding(const ScriptNode* value,
                                                UiBindingTarget target,
                                                UiValueType expected,
                                                UiDataContextId start_context,
                                                bool required = false) {
        if (value == nullptr) {
            if (required)
                diagnostic(ScriptedGuiDiagnosticCode::MissingField,
                           "required widget binding is missing", 0u, 0u);
            return {};
        }
        if (value->kind != ScriptValueKind::Block) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "bound widget property requires { bind = property.path }",
                       value->line, value->column);
            return {};
        }
        validate_unique_fields(value->children);
        const ScriptNode* bind_field = nullptr;
        for (const auto& child : value->children) {
            if (text(child.key) == "bind") bind_field = &child;
            else diagnostic(ScriptedGuiDiagnosticCode::UnknownField,
                            "unknown binding field '" + std::string{text(child.key)} + "'",
                            child.line, child.column);
        }
        if (bind_field == nullptr) {
            diagnostic(ScriptedGuiDiagnosticCode::MissingField,
                       "binding block requires bind", value->line, value->column);
            return {};
        }
        if (bind_field->kind != ScriptValueKind::Symbol) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "binding path must be an identifier or expression", bind_field->line, bind_field->column);
            return {};
        }
        auto path = text(bind_field->symbol);
        if (path.starts_with('[') && path.ends_with(']')) {
            path = path.substr(1, path.size() - 2);
        }
        std::vector<CompiledUiBindingStep> steps;
        UiDataContextId context_id = start_context;
        const UiDataPropertySchema* final_property = nullptr;
        std::size_t begin = 0;
        while (begin <= path.size()) {
            const auto dot = path.find('.', begin);
            const auto end = dot == std::string_view::npos ? path.size() : dot;
            const auto segment = path.substr(begin, end - begin);
            if (segment.empty()) {
                diagnostic(ScriptedGuiDiagnosticCode::UnknownBinding,
                           "invalid empty segment in binding '" + std::string{path} + "'",
                           bind_field->line, bind_field->column);
                return {};
            }
            final_property = schema_.find_property(context_id, segment);
            if (final_property == nullptr) {
                diagnostic(ScriptedGuiDiagnosticCode::UnknownBinding,
                           "unknown binding '" + std::string{path} + "' at segment '" +
                               std::string{segment} + "'",
                           bind_field->line, bind_field->column);
                return {};
            }
            steps.push_back({context_id, final_property->slot});
            const bool final = dot == std::string_view::npos;
            if (final) break;
            if (final_property->value_type != UiValueType::Entity ||
                !final_property->related_context.valid()) {
                diagnostic(ScriptedGuiDiagnosticCode::BindingTypeMismatch,
                           "binding '" + std::string{path} + "' traverses non-entity property '" +
                               std::string{segment} + "'",
                           bind_field->line, bind_field->column);
                return {};
            }
            context_id = final_property->related_context;
            begin = dot + 1u;
        }
        if (final_property == nullptr) return {};
        const bool compatible = final_property->value_type == expected ||
            (target == UiBindingTarget::Text &&
             final_property->value_type == UiValueType::LocalizationKey);
        if (!compatible) {
            diagnostic(ScriptedGuiDiagnosticCode::BindingTypeMismatch,
                       "binding '" + std::string{path} + "' has type " +
                           value_type_name(final_property->value_type) + ", expected " +
                           value_type_name(expected),
                       bind_field->line, bind_field->column);
            return {};
        }
        if (steps.size() > std::numeric_limits<std::uint16_t>::max()) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "binding path exceeds step limit", bind_field->line, bind_field->column);
            return {};
        }
        const auto binding_index = result_.blueprint.bindings_.size();
        if (binding_index >= static_cast<std::size_t>(UiBindingId::invalid_value)) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "scripted GUI binding count exceeds ID range", bind_field->line,
                       bind_field->column);
            return {};
        }
        const auto first_step = static_cast<std::uint32_t>(result_.blueprint.binding_steps_.size());
        result_.blueprint.binding_steps_.insert(result_.blueprint.binding_steps_.end(),
                                                steps.begin(), steps.end());
        result_.blueprint.bindings_.push_back(
            {first_step, static_cast<std::uint16_t>(steps.size()), target,
             final_property->value_type});
        return {UiBindingId{static_cast<UiBindingId::rep_type>(binding_index)}, final_property};
    }

    void compile_bool_gate(const ScriptNode* value, UiBindingTarget target,
                           UiDataContextId context_id, UiNodeFlags flag,
                           UiNodeFlags& flags) {
        if (value == nullptr) return;
        if (value->kind == ScriptValueKind::Block) {
            (void)compile_binding(value, target, UiValueType::Boolean, context_id);
            return;
        }
        bool bool_value = false;
        if (value->kind != ScriptValueKind::Symbol ||
            !parse_bool_symbol(text(value->symbol), bool_value)) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "visible/enabled requires yes/no or a boolean binding",
                       value->line, value->column);
            return;
        }
        if (bool_value) flags = flags | flag;
        else flags = static_cast<UiNodeFlags>(static_cast<std::uint8_t>(flags) &
                                              ~static_cast<std::uint8_t>(flag));
    }

    [[nodiscard]] bool numeric_value(const ScriptNode* value, double& out,
                                     double minimum, double maximum,
                                     std::string_view label) {
        if (value == nullptr || value->kind != ScriptValueKind::Number ||
            !std::isfinite(value->number) || value->number < minimum || value->number > maximum) {
            if (value != nullptr)
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           std::string{label} + " is outside its supported numeric range",
                           value->line, value->column);
            return false;
        }
        out = value->number;
        return true;
    }

    [[nodiscard]] bool bool_value(const ScriptNode* value, bool default_value,
                                  std::string_view label) {
        if (value == nullptr) return default_value;
        bool parsed = false;
        if (value->kind == ScriptValueKind::Symbol)
            parsed = parse_bool_symbol(text(value->symbol), default_value);
        if (!parsed)
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       std::string{label} + " requires yes/no", value->line, value->column);
        return default_value;
    }

    [[nodiscard]] UiTemplateId template_reference(const ScriptNode* value,
                                                  UiDataContextId expected_context,
                                                  std::optional<UiTemplateId> owner,
                                                  const UiDataPropertySchema* collection_property = nullptr) {
        if (value == nullptr) {
            diagnostic(ScriptedGuiDiagnosticCode::MissingField,
                       "template reference is required", 0u, 0u);
            return {};
        }
        if (value->kind != ScriptValueKind::Symbol) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "template reference must be an identifier", value->line, value->column);
            return {};
        }
        const auto id = find_template(value->symbol);
        if (!id.valid()) {
            diagnostic(ScriptedGuiDiagnosticCode::UnknownTemplate,
                       "unknown scripted GUI template '" + std::string{text(value->symbol)} + "'",
                       value->line, value->column);
            return {};
        }
        const auto& target = result_.blueprint.templates_[id.value()];
        UiDataContextId wanted = expected_context;
        if (collection_property != nullptr) wanted = collection_property->related_context;
        if (wanted.valid() && target.context != wanted) {
            diagnostic(ScriptedGuiDiagnosticCode::ContextTypeMismatch,
                       "template context does not match its instantiation context",
                       value->line, value->column);
        }
        if (owner && owner->valid())
            template_edges_[owner->value()].push_back({id, value->line, value->column});
        return id;
    }

    void compile_list(UiNodeId node_id, UiDataContextId context_id,
                      std::optional<UiTemplateId> owner, const ScriptNode& block) {
        auto items = compile_binding(field(block.children, "items"), UiBindingTarget::Items,
                                     UiValueType::Collection, context_id, true);
        const auto item_template = template_reference(field(block.children, "item_template"),
                                                      context_id, owner, items.property);
        CompiledUiListMetadata metadata;
        metadata.items = items.binding;
        metadata.item_template = item_template;
        double numeric = 0.0;
        if (const auto* value = field(block.children, "row_height");
            numeric_value(value, numeric, 0.001, 1'000'000.0, "list row_height"))
            metadata.row_height = static_cast<float>(numeric);
        if (const auto* value = field(block.children, "overscan");
            numeric_value(value, numeric, 0.0, 65535.0, "list overscan") && std::floor(numeric) == numeric)
            metadata.overscan = static_cast<std::uint16_t>(numeric);
        metadata.virtualized = bool_value(field(block.children, "virtualized"), true,
                                          "list virtualized");
        auto& node = result_.blueprint.nodes_[node_id.value()];
        node.auxiliary_index = static_cast<std::uint32_t>(result_.blueprint.lists_.size());
        result_.blueprint.lists_.push_back(metadata);
    }

    void compile_grid(UiNodeId node_id, UiDataContextId context_id,
                      std::optional<UiTemplateId> owner, const ScriptNode& block) {
        auto items = compile_binding(field(block.children, "items"), UiBindingTarget::Items,
                                     UiValueType::Collection, context_id, true);
        const auto item_template = template_reference(field(block.children, "item_template"),
                                                      context_id, owner, items.property);
        CompiledUiGridMetadata metadata;
        metadata.items = items.binding;
        metadata.item_template = item_template;
        double numeric = 0.0;
        if (const auto* value = field(block.children, "row_height");
            numeric_value(value, numeric, 0.001, 1'000'000.0, "grid row_height"))
            metadata.row_height = static_cast<float>(numeric);
        if (const auto* value = field(block.children, "column_gap");
            numeric_value(value, numeric, 0.0, 1'000'000.0, "grid column_gap"))
            metadata.column_gap = static_cast<float>(numeric);
        if (const auto* value = field(block.children, "row_gap");
            numeric_value(value, numeric, 0.0, 1'000'000.0, "grid row_gap"))
            metadata.row_gap = static_cast<float>(numeric);
        if (const auto* value = field(block.children, "columns");
            numeric_value(value, numeric, 1.0, 65535.0, "grid columns") && std::floor(numeric) == numeric)
            metadata.columns = static_cast<std::uint16_t>(numeric);
        if (const auto* value = field(block.children, "overscan");
            numeric_value(value, numeric, 0.0, 65535.0, "grid overscan") && std::floor(numeric) == numeric)
            metadata.overscan = static_cast<std::uint16_t>(numeric);
        metadata.virtualized = bool_value(field(block.children, "virtualized"), true,
                                          "grid virtualized");
        auto& node = result_.blueprint.nodes_[node_id.value()];
        node.auxiliary_index = static_cast<std::uint32_t>(result_.blueprint.grids_.size());
        result_.blueprint.grids_.push_back(metadata);
    }

    void compile_chart(UiNodeId node_id, UiDataContextId context_id,
                       const ScriptNode& block) {
        const auto series = compile_binding(field(block.children, "series"),
                                            UiBindingTarget::ChartSeries,
                                            UiValueType::NumberSeries, context_id, true);
        const auto labels = field(block.children, "labels") != nullptr
            ? compile_binding(field(block.children, "labels"), UiBindingTarget::ChartLabels,
                              UiValueType::TextSeries, context_id)
            : BoundProperty{};
        CompiledUiChartMetadata metadata;
        metadata.series = series.binding;
        metadata.labels = labels.binding;
        double numeric = 0.0;
        if (const auto* value = field(block.children, "max_points");
            numeric_value(value, numeric, 2.0,
                          static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
                          "chart max_points") && std::floor(numeric) == numeric)
            metadata.max_points = static_cast<std::uint32_t>(numeric);
        if (const auto* value = field(block.children, "chart_kind"); value != nullptr) {
            if (value->kind != ScriptValueKind::Symbol) {
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "chart_kind must be line, area or bar", value->line, value->column);
            } else {
                const auto name = text(value->symbol);
                if (name == "line") metadata.kind = UiChartKind::Line;
                else if (name == "area") metadata.kind = UiChartKind::Area;
                else if (name == "bar") metadata.kind = UiChartKind::Bar;
                else diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                                "chart_kind must be line, area or bar", value->line, value->column);
            }
        }
        metadata.include_zero = bool_value(field(block.children, "include_zero"), false,
                                           "chart include_zero");
        auto& node = result_.blueprint.nodes_[node_id.value()];
        node.auxiliary_index = static_cast<std::uint32_t>(result_.blueprint.charts_.size());
        result_.blueprint.charts_.push_back(metadata);
    }

    [[nodiscard]] UiNodeId compile_node(const ScriptNode& block, std::string_view implied_kind,
                                        UiDataContextId context_id, UiNodeId parent,
                                        UiStableKey owner_key,
                                        std::optional<UiTemplateId> owner_template,
                                        std::vector<NodeKeyRecord>& node_keys) {
        validate_unique_fields(block.children);
        const auto resolved_kind = resolve_kind(block, implied_kind);
        const auto kind = resolved_kind.value_or(UiWidgetKind::Panel);
        for (const auto& value : block.children) {
            if (!allowed_field(kind, text(value.key)))
                diagnostic(ScriptedGuiDiagnosticCode::UnknownField,
                           "field '" + std::string{text(value.key)} + "' is not valid for this widget",
                           value.line, value.column);
        }
        const auto node_index = result_.blueprint.nodes_.size();
        if (node_index >= static_cast<std::size_t>(UiNodeId::invalid_value)) {
            diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                       "scripted GUI node count exceeds ID range", block.line, block.column);
            return {};
        }
        const UiNodeId node_id{static_cast<UiNodeId::rep_type>(node_index)};
        CompiledUiNode node;
        node.parent = parent;
        node.kind = kind;
        node.first_binding = static_cast<std::uint32_t>(result_.blueprint.bindings_.size());
        node.first_constant = static_cast<std::uint32_t>(result_.blueprint.constants_.size());
        if (const auto* id_field = field(block.children, "id"); id_field != nullptr) {
            if (id_field->kind != ScriptValueKind::Symbol) {
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "widget id must be an identifier", id_field->line, id_field->column);
            } else {
                const auto local_key = ui_stable_key(text(id_field->symbol));
                for (const auto& previous : node_keys) {
                    if (previous.key == local_key) {
                        const auto code = previous.symbol == id_field->symbol
                            ? ScriptedGuiDiagnosticCode::DuplicateNodeId
                            : ScriptedGuiDiagnosticCode::StableKeyCollision;
                        diagnostic(code, "duplicate or colliding widget id '" +
                                   std::string{text(id_field->symbol)} + "'",
                                   id_field->line, id_field->column);
                    }
                }
                node_keys.push_back({local_key, id_field->symbol});
                node.stable_key = ui_stable_node_key(owner_key, text(id_field->symbol));
            }
        }
        result_.blueprint.nodes_.push_back(node);

        auto& compiled = result_.blueprint.nodes_[node_id.value()];
        compile_bool_gate(field(block.children, "visible"), UiBindingTarget::Visible,
                          context_id, UiNodeFlags::VisibleByDefault, compiled.flags);
        compile_bool_gate(field(block.children, "enabled"), UiBindingTarget::Enabled,
                          context_id, UiNodeFlags::EnabledByDefault, compiled.flags);

        if (const auto* command_field = field(block.children, "command"); command_field != nullptr) {
            if (command_field->kind != ScriptValueKind::Symbol) {
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "widget command must be an identifier", command_field->line,
                           command_field->column);
            } else {
                compiled.command = schema_.find_command(command_field->symbol);
                if (!compiled.command.valid())
                    diagnostic(ScriptedGuiDiagnosticCode::UnknownCommand,
                               "unknown scripted GUI command '" +
                                   std::string{text(command_field->symbol)} + "'",
                               command_field->line, command_field->column);
                else if (const auto* command_schema = schema_.command(compiled.command))
                    compiled.command_key = command_schema->stable_key;
            }
        }
        if (const auto* tooltip = field(block.children, "tooltip"); tooltip != nullptr) {
            if (tooltip->kind != ScriptValueKind::Symbol)
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "tooltip must be a localization key", tooltip->line, tooltip->column);
            else compiled.tooltip_key = ui_stable_key(text(tooltip->symbol));
        }

        const auto* text_field = field(block.children, "text");
        if (text_field != nullptr) {
            if (text_field->kind == ScriptValueKind::Block)
                (void)compile_binding(text_field, UiBindingTarget::Text, UiValueType::Text, context_id);
            else constant_symbol(text_field, UiConstantTarget::Text, UiValueType::LocalizationKey);
        }
        const auto* icon_field = field(block.children, "icon");
        if (icon_field != nullptr) {
            if (icon_field->kind == ScriptValueKind::Block)
                (void)compile_binding(icon_field, UiBindingTarget::Icon, UiValueType::Asset, context_id);
            else constant_symbol(icon_field, UiConstantTarget::Icon, UiValueType::Asset);
        }
        const auto* module_field = field(block.children, "module");
        if (module_field != nullptr)
            constant_symbol(module_field, UiConstantTarget::Module, UiValueType::Asset);
        const auto* value_field = field(block.children, "value");
        if (value_field != nullptr) {
            if (value_field->kind == ScriptValueKind::Block)
                (void)compile_binding(value_field, UiBindingTarget::Value, UiValueType::Number, context_id);
            else constant_number(value_field, UiConstantTarget::Value);
        }
        const auto* selected_field = field(block.children, "selected");
        if (selected_field != nullptr) {
            if (selected_field->kind == ScriptValueKind::Block)
                (void)compile_binding(selected_field, UiBindingTarget::Selected,
                                      UiValueType::Boolean, context_id);
            else constant_bool(selected_field, UiConstantTarget::Selected);
        }
        const auto* hovered_field = field(block.children, "hovered");
        if (hovered_field != nullptr) constant_bool(hovered_field, UiConstantTarget::Hovered);
        const auto* pressed_field = field(block.children, "pressed");
        if (pressed_field != nullptr) constant_bool(pressed_field, UiConstantTarget::Pressed);
        const auto* focused_field = field(block.children, "focused");
        if (focused_field != nullptr) constant_bool(focused_field, UiConstantTarget::Focused);
        const auto* style_field = field(block.children, "style");
        if (style_field != nullptr) {
            if (style_field->kind != ScriptValueKind::Symbol) {
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "style requires a known semantic UI style",
                           style_field->line, style_field->column);
            } else {
                const std::string_view style_name = text(style_field->symbol);
                UiSurfaceStyle style = UiSurfaceStyle::Standard;
                bool known = true;
                if (style_name == "wood") style = UiSurfaceStyle::Wood;
                else if (style_name == "parchment") style = UiSurfaceStyle::Parchment;
                else if (style_name == "leather") style = UiSurfaceStyle::Leather;
                else if (style_name == "recessed") style = UiSurfaceStyle::Recessed;
                else if (style_name == "hud") style = UiSurfaceStyle::Hud;
                else if (style_name == "dock") style = UiSurfaceStyle::Dock;
                else if (style_name == "stat") style = UiSurfaceStyle::Stat;
                else if (style_name == "section") style = UiSurfaceStyle::Section;
                else if (style_name == "nav") style = UiSurfaceStyle::Nav;
                else if (style_name == "primary") style = UiSurfaceStyle::Primary;
                else if (style_name == "secondary") style = UiSurfaceStyle::Secondary;
                else if (style_name == "positive") style = UiSurfaceStyle::Positive;
                else if (style_name == "empty") style = UiSurfaceStyle::Empty;
                else if (style_name == "time") style = UiSurfaceStyle::Time;
                else if (style_name == "top") style = UiSurfaceStyle::Top;
                else if (style_name == "country") style = UiSurfaceStyle::Country;
                else if (style_name == "footer") style = UiSurfaceStyle::Footer;
                else if (style_name == "tab") style = UiSurfaceStyle::Tab;
                else if (style_name == "medallion") style = UiSurfaceStyle::Medallion;
                else if (style_name == "tool_primary") style = UiSurfaceStyle::ToolPrimary;
                else if (style_name == "tool_secondary") style = UiSurfaceStyle::ToolSecondary;
                else if (style_name == "tool_utility") style = UiSurfaceStyle::ToolUtility;
                else if (style_name == "menu_row") style = UiSurfaceStyle::MenuRow;
                else if (style_name == "icon_inset") style = UiSurfaceStyle::IconInset;
                else if (style_name == "utility") style = UiSurfaceStyle::Utility;
                else if (style_name == "outliner") style = UiSurfaceStyle::Outliner;
                else if (style_name == "outliner_group") style = UiSurfaceStyle::OutlinerGroup;
                else if (style_name == "outliner_row") style = UiSurfaceStyle::OutlinerRow;
                else if (style_name == "modal_backdrop") style = UiSurfaceStyle::ModalBackdrop;
                else if (style_name == "modal") style = UiSurfaceStyle::Modal;
                else if (style_name == "center") style = UiSurfaceStyle::Center;
                else if (style_name == "center_muted") style = UiSurfaceStyle::CenterMuted;
                else if (style_name != "standard") known = false;
                if (!known) {
                    diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                               "unknown surface style '" + std::string{style_name} + "'",
                               style_field->line, style_field->column);
                } else {
                    result_.blueprint.constants_.push_back(
                        {static_cast<std::uint64_t>(style), UiConstantTarget::Style, UiValueType::None});
                }
            }
        }

        constant_number(field(block.children, "width"), UiConstantTarget::Width);
        constant_number(field(block.children, "height"), UiConstantTarget::Height);
        constant_number(field(block.children, "min_width"), UiConstantTarget::MinWidth);
        constant_number(field(block.children, "min_height"), UiConstantTarget::MinHeight);
        constant_number(field(block.children, "max_width"), UiConstantTarget::MaxWidth);
        constant_number(field(block.children, "max_height"), UiConstantTarget::MaxHeight);
        constant_number(field(block.children, "grow"), UiConstantTarget::Grow);
        constant_number(field(block.children, "gap"), UiConstantTarget::Gap);

        if (kind == UiWidgetKind::TemplateInstance) {
            const auto template_id = template_reference(field(block.children, "template"),
                                                        context_id, owner_template);
            compiled.auxiliary_index = template_id.valid() ? template_id.value() : kInvalidUiIndex;
        } else if (kind == UiWidgetKind::List) {
            compile_list(node_id, context_id, owner_template, block);
        } else if (kind == UiWidgetKind::Grid) {
            compile_grid(node_id, context_id, owner_template, block);
        } else if (kind == UiWidgetKind::Chart) {
            compile_chart(node_id, context_id, block);
        }

        compiled.binding_count = static_cast<std::uint16_t>(
            result_.blueprint.bindings_.size() - compiled.first_binding);
        compiled.constant_count = static_cast<std::uint16_t>(
            result_.blueprint.constants_.size() - compiled.first_constant);

        const auto* children = field(block.children, "children");
        if (children != nullptr) {
            if (children->kind != ScriptValueKind::Block) {
                diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                           "children must be a block", children->line, children->column);
            } else {
                UiNodeId previous_child{};
                for (const auto& child : children->children) {
                    if (child.kind != ScriptValueKind::Block) {
                        diagnostic(ScriptedGuiDiagnosticCode::InvalidValue,
                                   "child widget must be a block", child.line, child.column);
                        continue;
                    }
                    auto child_key = text(child.key);
                    std::string_view implied = child_key;
                    if (child_key == "widget") implied = "widget";
                    else if (child_key == "use") implied = "template_instance";
                    else if (!widget_kind(child_key)) {
                        diagnostic(ScriptedGuiDiagnosticCode::UnknownWidget,
                                   "unknown child widget shorthand '" + std::string{child_key} + "'",
                                   child.line, child.column);
                        continue;
                    }
                    const auto child_id = compile_node(child, implied, context_id, node_id,
                                                       owner_key, owner_template, node_keys);
                    if (!child_id.valid()) continue;
                    auto& parent_node = result_.blueprint.nodes_[node_id.value()];
                    if (!parent_node.first_child.valid()) parent_node.first_child = child_id;
                    if (previous_child.valid())
                        result_.blueprint.nodes_[previous_child.value()].next_sibling = child_id;
                    previous_child = child_id;
                }
            }
        }
        return node_id;
    }

    void detect_template_cycles() {
        enum class Mark : std::uint8_t { White, Gray, Black };
        std::vector<Mark> marks(templates_.size(), Mark::White);
        std::vector<UiTemplateId> stack;
        const std::function<void(UiTemplateId)> visit = [&](UiTemplateId current) {
            marks[current.value()] = Mark::Gray;
            stack.push_back(current);
            for (const auto& edge : template_edges_[current.value()]) {
                if (!edge.target.valid()) continue;
                if (marks[edge.target.value()] == Mark::White) {
                    visit(edge.target);
                } else if (marks[edge.target.value()] == Mark::Gray) {
                    std::string message = "scripted GUI template cycle: ";
                    auto begin = std::find(stack.begin(), stack.end(), edge.target);
                    for (auto it = begin; it != stack.end(); ++it) {
                        if (it != begin) message += " -> ";
                        message += std::string{text(templates_[it->value()].name)};
                    }
                    message += " -> " + std::string{text(templates_[edge.target.value()].name)};
                    diagnostic(ScriptedGuiDiagnosticCode::TemplateCycle, std::move(message),
                               edge.line, edge.column);
                }
            }
            stack.pop_back();
            marks[current.value()] = Mark::Black;
        };
        for (std::size_t index = 0; index < templates_.size(); ++index) {
            if (marks[index] == Mark::White)
                visit(UiTemplateId{static_cast<UiTemplateId::rep_type>(index)});
        }
    }

    SymbolTable& symbols_;
    const ScriptedGuiSchema& schema_;
    const ScriptParseResult& parsed_;
    ScriptedGuiCompileResult result_;
    std::vector<GuiObjectDecl> templates_;
    std::vector<GuiObjectDecl> screens_;
    std::vector<std::vector<TemplateEdge>> template_edges_;
};

ScriptedGuiCompileResult ScriptedGuiCompiler::compile(std::string_view source,
                                                       std::string_view source_name) {
    ThunderScriptParser parser{symbols_};
    auto parsed = parser.parse(source, source_name);
    auto result = compile(parsed);
    if (!parsed.diagnostics.empty()) {
        std::vector<ScriptedGuiDiagnostic> combined;
        combined.reserve(parsed.diagnostics.size() + result.diagnostics.size());
        for (auto& value : parsed.diagnostics)
            combined.push_back({ScriptedGuiDiagnosticCode::Syntax, std::move(value.message),
                                value.line, value.column});
        combined.insert(combined.end(),
                        std::make_move_iterator(result.diagnostics.begin()),
                        std::make_move_iterator(result.diagnostics.end()));
        result.diagnostics = std::move(combined);
    }
    return result;
}

ScriptedGuiCompileResult ScriptedGuiCompiler::compile(const ScriptParseResult& parsed) const {
    return ScriptedGuiCompilePass{symbols_, schema_, parsed}.run();
}
} // namespace thunder
