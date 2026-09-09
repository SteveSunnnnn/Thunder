#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/ScriptValue.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

class ScriptedGuiCompilePass;

// Scripted GUI is immutable content data. Stable keys identify content across load
// order changes; compact IDs and property slots are used while rendering.
using UiStableKey = ScriptStableKey;

[[nodiscard]] inline UiStableKey ui_stable_key(std::string_view text) noexcept {
    return script_stable_key(text);
}

[[nodiscard]] inline UiStableKey ui_stable_node_key(UiStableKey owner, std::string_view local) noexcept {
    Fnv1a64 hash;
    hash.add(owner);
    hash.add(local);
    return hash.value();
}

struct UiDataContextTag {};
struct UiDataPropertyTag {};
struct UiCommandTag {};
struct UiNodeTag {};
struct UiBindingTag {};
struct UiTemplateTag {};
struct UiScreenTag {};

using UiDataContextId = StrongId<UiDataContextTag>;
using UiDataPropertyId = StrongId<UiDataPropertyTag>;
using UiCommandId = StrongId<UiCommandTag>;
using UiNodeId = StrongId<UiNodeTag>;
using UiBindingId = StrongId<UiBindingTag>;
using UiTemplateId = StrongId<UiTemplateTag>;
using UiScreenId = StrongId<UiScreenTag>;

enum class UiValueType : std::uint8_t {
    None,
    Boolean,
    Number,
    Text,
    LocalizationKey,
    Asset,
    Color,
    Entity,
    Collection,
    NumberSeries,
    TextSeries
};

struct UiDataContextSchema {
    UiDataContextId id{};
    SymbolId name{};
    UiStableKey stable_key = 0;
    std::uint16_t property_count = 0;
};

struct UiDataPropertySchema {
    UiDataPropertyId id{};
    UiDataContextId context{};
    SymbolId name{};
    UiStableKey stable_key = 0;
    UiValueType value_type = UiValueType::None;
    // Entity target or Collection element context. Invalid for scalar values.
    UiDataContextId related_context{};
    std::uint16_t slot = 0;
};

struct UiCommandSchema {
    UiCommandId id{};
    SymbolId name{};
    UiStableKey stable_key = 0;
};

// Startup schema shared by content compilation and the runtime data-context
// provider. Registration is intentionally separate from a blueprint, so no game
// concept is built into Thunder's UI compiler.
class ScriptedGuiSchema {
public:
    explicit ScriptedGuiSchema(SymbolTable& symbols) noexcept : symbols_(symbols) {}

    UiDataContextId register_context(std::string_view name);
    UiDataPropertyId register_property(UiDataContextId context,
                                       std::string_view name,
                                       UiValueType value_type,
                                       UiDataContextId related_context = {});
    UiCommandId register_command(std::string_view name);

    // Friendly aliases for callers that use add_* terminology.
    UiDataContextId add_context(std::string_view name) { return register_context(name); }
    UiDataPropertyId add_property(UiDataContextId context,
                                  std::string_view name,
                                  UiValueType value_type,
                                  UiDataContextId related_context = {}) {
        return register_property(context, name, value_type, related_context);
    }
    UiCommandId add_command(std::string_view name) { return register_command(name); }

    [[nodiscard]] UiDataContextId find_context(SymbolId name) const noexcept;
    [[nodiscard]] UiDataContextId find_context(std::string_view name) const noexcept;
    [[nodiscard]] const UiDataPropertySchema* find_property(UiDataContextId context,
                                                            SymbolId name) const noexcept;
    [[nodiscard]] const UiDataPropertySchema* find_property(UiDataContextId context,
                                                            std::string_view name) const noexcept;
    [[nodiscard]] UiCommandId find_command(SymbolId name) const noexcept;
    [[nodiscard]] UiCommandId find_command(std::string_view name) const noexcept;

    [[nodiscard]] const UiDataContextSchema* context(UiDataContextId id) const noexcept;
    [[nodiscard]] const UiDataPropertySchema* property(UiDataPropertyId id) const noexcept;
    [[nodiscard]] const UiCommandSchema* command(UiCommandId id) const noexcept;
    [[nodiscard]] std::span<const UiDataContextSchema> contexts() const noexcept { return contexts_; }
    [[nodiscard]] std::span<const UiDataPropertySchema> properties() const noexcept { return properties_; }
    [[nodiscard]] std::span<const UiCommandSchema> commands() const noexcept { return commands_; }
    [[nodiscard]] SymbolTable& symbols() const noexcept { return symbols_; }

private:
    SymbolTable& symbols_;
    std::vector<UiDataContextSchema> contexts_;
    std::vector<UiDataPropertySchema> properties_;
    std::vector<UiCommandSchema> commands_;
};

enum class UiWidgetKind : std::uint8_t {
    Panel,
    Row,
    Column,
    Stack,
    Label,
    Button,
    Image,
    Module,
    ScrollView,
    List,
    Grid,
    Chart,
    Spacer,
    Progress,
    TemplateInstance
};

enum class UiBindingTarget : std::uint8_t {
    Visible,
    Enabled,
    Text,
    Value,
    Icon,
    Selected,
    Items,
    ChartSeries,
    ChartLabels
};

enum class UiConstantTarget : std::uint8_t {
    Text,
    Value,
    Icon,
    Selected,
    Hovered,
    Pressed,
    Focused,
    Style,
    Width,
    Height,
    MinWidth,
    MinHeight,
    MaxWidth,
    MaxHeight,
    Grow,
    Gap,
    Module
};

// Surface material declared by content (`style = ...` on a panel). The
// painter maps the semantic role onto the active theme's materials; content
// never names colors or textures.
enum class UiSurfaceStyle : std::uint8_t {
    Standard,
    Wood,
    Parchment,
    Leather,
    Recessed,
    // Semantic HUD roles. These remain game-agnostic: content chooses the
    // role while the active theme owns every color, edge and text treatment.
    Hud,
    Dock,
    Stat,
    Section,
    Nav,
    Primary,
    Secondary,
    Positive,
    Empty,
    Time,
    Top,
    Country,
    Footer,
    Tab,
    Medallion,
    ToolPrimary,
    ToolSecondary,
    ToolUtility,
    MenuRow,
    IconInset,
    Utility,
    Outliner,
    OutlinerGroup,
    OutlinerRow,
    ModalBackdrop,
    Modal,
    Center,
    CenterMuted
};

enum class UiChartKind : std::uint8_t { Line, Area, Bar };

enum class UiNodeFlags : std::uint8_t {
    None = 0,
    VisibleByDefault = 1u << 0u,
    EnabledByDefault = 1u << 1u
};

[[nodiscard]] constexpr UiNodeFlags operator|(UiNodeFlags left, UiNodeFlags right) noexcept {
    return static_cast<UiNodeFlags>(static_cast<std::uint8_t>(left) |
                                    static_cast<std::uint8_t>(right));
}
[[nodiscard]] constexpr bool has_flag(UiNodeFlags flags, UiNodeFlags wanted) noexcept {
    return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(wanted)) != 0u;
}

// One statically resolved step in a binding such as owner.capital.display_name.
// The runtime asks the current context provider for slot, then moves to the
// returned entity context before consuming the next step.
struct CompiledUiBindingStep {
    UiDataContextId context{};
    std::uint16_t property_slot = 0;
};

struct CompiledUiBinding {
    std::uint32_t first_step = 0;
    std::uint16_t step_count = 0;
    UiBindingTarget target = UiBindingTarget::Visible;
    UiValueType value_type = UiValueType::None;
};

// Constants are encoded without owning strings. Text/localization and asset
// payloads are stable hashes; numbers are IEEE-754 bit patterns.
struct CompiledUiConstant {
    std::uint64_t payload = 0;
    UiConstantTarget target = UiConstantTarget::Text;
    UiValueType value_type = UiValueType::None;

    [[nodiscard]] double number() const noexcept;
    [[nodiscard]] UiStableKey stable_key() const noexcept { return payload; }
};

inline constexpr std::uint32_t kInvalidUiIndex = 0xffff'ffffu;

struct CompiledUiNode {
    UiStableKey stable_key = 0;
    UiNodeId parent{};
    UiNodeId first_child{};
    UiNodeId next_sibling{};
    std::uint32_t first_binding = 0;
    std::uint32_t first_constant = 0;
    // Meaning is selected by kind: template ID, list/grid/chart metadata index.
    std::uint32_t auxiliary_index = kInvalidUiIndex;
    UiCommandId command{};
    UiStableKey command_key = 0;
    UiStableKey tooltip_key = 0;
    std::uint16_t binding_count = 0;
    std::uint16_t constant_count = 0;
    UiWidgetKind kind = UiWidgetKind::Panel;
    UiNodeFlags flags = UiNodeFlags::VisibleByDefault | UiNodeFlags::EnabledByDefault;
};

struct CompiledUiListMetadata {
    UiBindingId items{};
    UiTemplateId item_template{};
    float row_height = 24.0f;
    std::uint16_t overscan = 2;
    bool virtualized = true;
};

struct CompiledUiGridMetadata {
    UiBindingId items{};
    UiTemplateId item_template{};
    float row_height = 24.0f;
    float column_gap = 0.0f;
    float row_gap = 0.0f;
    std::uint16_t columns = 1;
    std::uint16_t overscan = 1;
    bool virtualized = true;
};

struct CompiledUiChartMetadata {
    UiBindingId series{};
    UiBindingId labels{};
    std::uint32_t max_points = 512;
    UiChartKind kind = UiChartKind::Line;
    bool include_zero = false;
};

struct CompiledUiTemplate {
    UiStableKey stable_key = 0;
    UiDataContextId context{};
    UiNodeId root{};
};

struct CompiledUiScreen {
    UiStableKey stable_key = 0;
    UiDataContextId context{};
    UiNodeId root{};
};

class ScriptedGuiBlueprint {
public:
    [[nodiscard]] std::span<const CompiledUiNode> nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::span<const CompiledUiBinding> bindings() const noexcept { return bindings_; }
    [[nodiscard]] std::span<const CompiledUiBindingStep> binding_steps() const noexcept { return binding_steps_; }
    [[nodiscard]] std::span<const CompiledUiConstant> constants() const noexcept { return constants_; }
    [[nodiscard]] std::span<const CompiledUiListMetadata> lists() const noexcept { return lists_; }
    [[nodiscard]] std::span<const CompiledUiGridMetadata> grids() const noexcept { return grids_; }
    [[nodiscard]] std::span<const CompiledUiChartMetadata> charts() const noexcept { return charts_; }
    [[nodiscard]] std::span<const CompiledUiTemplate> templates() const noexcept { return templates_; }
    [[nodiscard]] std::span<const CompiledUiScreen> screens() const noexcept { return screens_; }

    [[nodiscard]] const CompiledUiTemplate* find_template(UiStableKey key) const noexcept;
    [[nodiscard]] const CompiledUiScreen* find_screen(UiStableKey key) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    friend class ScriptedGuiCompiler;
    friend class ScriptedGuiCompilePass;
    std::vector<CompiledUiNode> nodes_;
    std::vector<CompiledUiBinding> bindings_;
    std::vector<CompiledUiBindingStep> binding_steps_;
    std::vector<CompiledUiConstant> constants_;
    std::vector<CompiledUiListMetadata> lists_;
    std::vector<CompiledUiGridMetadata> grids_;
    std::vector<CompiledUiChartMetadata> charts_;
    std::vector<CompiledUiTemplate> templates_;
    std::vector<CompiledUiScreen> screens_;
};

enum class ScriptedGuiDiagnosticCode : std::uint8_t {
    Syntax,
    DuplicateObject,
    StableKeyCollision,
    DuplicateField,
    DuplicateNodeId,
    UnknownField,
    MissingField,
    InvalidValue,
    UnknownContext,
    UnknownWidget,
    UnknownCommand,
    UnknownTemplate,
    UnknownBinding,
    BindingTypeMismatch,
    ContextTypeMismatch,
    TemplateCycle
};

struct ScriptedGuiDiagnostic {
    ScriptedGuiDiagnosticCode code = ScriptedGuiDiagnosticCode::Syntax;
    std::string message;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct ScriptedGuiCompileResult {
    ScriptedGuiBlueprint blueprint;
    std::vector<ScriptedGuiDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class ScriptedGuiCompiler {
public:
    ScriptedGuiCompiler(SymbolTable& symbols, const ScriptedGuiSchema& schema) noexcept
        : symbols_(symbols), schema_(schema) {}

    [[nodiscard]] ScriptedGuiCompileResult compile(std::string_view source,
                                                   std::string_view source_name = {});
    [[nodiscard]] ScriptedGuiCompileResult compile(const ScriptParseResult& parsed) const;

private:
    SymbolTable& symbols_;
    const ScriptedGuiSchema& schema_;
};

} // namespace thunder
