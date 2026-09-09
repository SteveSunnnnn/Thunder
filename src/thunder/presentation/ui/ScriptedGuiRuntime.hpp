#pragma once

#include "thunder/presentation/ui/ScriptedGui.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

// ScriptedGuiRuntime is presentation-derived retained state. It is deliberately
// outside World, save games, replay, and deterministic simulation checksums.
// Providers expose stable identities and opaque handles without making the UI
// compiler depend on game-specific entity types.
struct UiDataEntityRef {
    UiDataContextId context{};
    UiStableKey stable_key = 0;
    std::uint64_t handle = 0;

    [[nodiscard]] bool valid() const noexcept { return context.valid() && stable_key != 0; }
    friend bool operator==(const UiDataEntityRef&, const UiDataEntityRef&) = default;
};

struct UiDataCollectionRef {
    UiDataContextId element_context{};
    UiStableKey stable_key = 0;
    std::uint64_t handle = 0;
    std::size_t size = 0;

    [[nodiscard]] bool valid() const noexcept {
        return element_context.valid() && stable_key != 0;
    }
    friend bool operator==(const UiDataCollectionRef&, const UiDataCollectionRef&) = default;
};

struct UiDataSeriesRef {
    UiStableKey stable_key = 0;
    std::uint64_t handle = 0;
    std::size_t size = 0;

    [[nodiscard]] bool valid() const noexcept { return stable_key != 0; }
    friend bool operator==(const UiDataSeriesRef&, const UiDataSeriesRef&) = default;
};

// A tagged, non-owning provider result. Text is copied into the retained
// snapshot before read_property returns to the caller.
struct UiDataValue {
    UiValueType type = UiValueType::None;
    bool boolean = false;
    double number = 0.0;
    UiStableKey stable_key = 0;
    std::string_view text{};
    UiDataEntityRef entity{};
    UiDataCollectionRef collection{};
    UiDataSeriesRef series{};

    [[nodiscard]] static UiDataValue boolean_value(bool value) noexcept;
    [[nodiscard]] static UiDataValue number_value(double value) noexcept;
    [[nodiscard]] static UiDataValue text_value(std::string_view value) noexcept;
    [[nodiscard]] static UiDataValue key_value(UiValueType type, UiStableKey value) noexcept;
    [[nodiscard]] static UiDataValue entity_value(UiDataEntityRef value) noexcept;
    [[nodiscard]] static UiDataValue collection_value(UiDataCollectionRef value) noexcept;
    [[nodiscard]] static UiDataValue series_value(UiValueType type,
                                                  UiDataSeriesRef value) noexcept;
};

class ScriptedGuiDataProvider {
public:
    virtual ~ScriptedGuiDataProvider() = default;

    // property_slot belongs to source.context and was validated at content
    // compile time. Returning a different value type is treated as unavailable
    // data, never as an unchecked conversion.
    [[nodiscard]] virtual bool read_property(UiDataEntityRef source,
                                             std::uint16_t property_slot,
                                             UiDataValue& out) const noexcept = 0;
    [[nodiscard]] virtual bool collection_element(const UiDataCollectionRef& collection,
                                                  std::size_t index,
                                                  UiDataEntityRef& out) const noexcept = 0;
    [[nodiscard]] virtual bool number_series_value(const UiDataSeriesRef&,
                                                   std::size_t,
                                                   double&) const noexcept {
        return false;
    }
    [[nodiscard]] virtual bool text_series_value(const UiDataSeriesRef&,
                                                 std::size_t,
                                                 UiStableKey&) const noexcept {
        return false;
    }
};

struct UiCollectionViewport {
    // May name either a compiled node stable key or one concrete runtime
    // instance key. Instance keys take precedence for repeated templates.
    UiStableKey node_or_instance_key = 0;
    float scroll_y = 0.0f;
    float viewport_height = 0.0f;
};

enum class UiRuntimeDirty : std::uint32_t {
    None = 0,
    Visibility = 1u << 0u,
    Enabled = 1u << 1u,
    Text = 1u << 2u,
    Value = 1u << 3u,
    Selected = 1u << 4u,
    Items = 1u << 5u,
    Chart = 1u << 6u,
    Interaction = 1u << 7u,
    Topology = 1u << 8u,
    DataValidity = 1u << 9u,
    Icon = 1u << 10u,
    All = (1u << 11u) - 1u
};

[[nodiscard]] constexpr UiRuntimeDirty operator|(UiRuntimeDirty left,
                                                 UiRuntimeDirty right) noexcept {
    return static_cast<UiRuntimeDirty>(static_cast<std::uint32_t>(left) |
                                       static_cast<std::uint32_t>(right));
}
constexpr UiRuntimeDirty& operator|=(UiRuntimeDirty& left, UiRuntimeDirty right) noexcept {
    left = left | right;
    return left;
}
[[nodiscard]] constexpr bool has_dirty(UiRuntimeDirty value, UiRuntimeDirty wanted) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(wanted)) != 0u;
}

inline constexpr std::uint32_t kInvalidUiRuntimeNode = 0xffff'ffffu;

struct UiRuntimeNode {
    UiStableKey instance_key = 0;
    UiStableKey parent_key = 0;
    UiStableKey children_signature = 0;
    UiNodeId blueprint_node{};
    UiDataEntityRef source{};
    std::uint32_t parent = kInvalidUiRuntimeNode;
    std::uint32_t first_child = kInvalidUiRuntimeNode;
    std::uint32_t next_sibling = kInvalidUiRuntimeNode;
    UiWidgetKind kind = UiWidgetKind::Panel;

    bool visible = true;
    bool enabled = true;
    bool selected = false;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    // Presentation-only state interpolation. These values are retained across
    // data refreshes so controls have mechanical 80-120 ms transitions rather
    // than snapping between flat color swatches.
    float hover_mix = 0.0f;
    float press_mix = 0.0f;
    float selected_mix = 0.0f;
    float focus_mix = 0.0f;
    bool data_valid = true;
    bool text_is_localization = false;
    UiSurfaceStyle surface_style = UiSurfaceStyle::Standard;
    double value = 0.0;
    std::string text;
    UiStableKey text_key = 0;
    UiStableKey icon_key = 0;
    UiStableKey module_key = 0;

    UiDataCollectionRef items{};
    std::size_t item_count = 0;
    UiVirtualWindow item_window{};
    std::size_t visible_item_count = 0;

    std::vector<float> chart_values;
    std::vector<UiStableKey> chart_labels;
    std::size_t chart_source_points = 0;
    UiChartKind chart_kind = UiChartKind::Line;
    bool chart_include_zero = false;
    bool chart_downsampled = false;

    UiCommandId command{};
    UiStableKey command_key = 0;
    UiStableKey tooltip_key = 0;

    std::uint64_t generation = 0;
    UiRuntimeDirty dirty = UiRuntimeDirty::None;
};

struct UiRuntimeChartView {
    std::span<const float> values;
    std::span<const UiStableKey> labels;
    std::size_t source_points = 0;
    UiChartKind kind = UiChartKind::Line;
    bool include_zero = false;
    bool downsampled = false;
};

struct UiRuntimeDiff {
    std::uint64_t generation = 0;
    std::size_t dirty_nodes = 0;
    std::size_t removed_nodes = 0;
    std::size_t provider_errors = 0;
    bool truncated = false;
};

struct ScriptedGuiRuntimeConfig {
    std::size_t max_nodes = 16'384;
    std::size_t max_visible_collection_items = 4'096;
    std::size_t max_text_bytes = 16'384;
    std::uint32_t max_chart_points = 4'096;
    std::uint16_t max_template_depth = 64;
    float default_viewport_height = 720.0f;
};

class ScriptedGuiRuntime {
public:
    explicit ScriptedGuiRuntime(const ScriptedGuiBlueprint& blueprint,
                                ScriptedGuiRuntimeConfig config = {});

    [[nodiscard]] bool instantiate_screen(UiStableKey screen_key,
                                          UiDataEntityRef root) noexcept;
    [[nodiscard]] bool instantiate_template(UiStableKey template_key,
                                            UiDataEntityRef root) noexcept;
    void clear() noexcept;

    [[nodiscard]] UiRuntimeDiff refresh(
        const ScriptedGuiDataProvider& provider,
        std::span<const UiCollectionViewport> viewports = {});

    [[nodiscard]] std::span<const UiRuntimeNode> nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::span<UiRuntimeNode> nodes() noexcept { return nodes_; }
    [[nodiscard]] std::span<const UiStableKey> removed_keys() const noexcept {
        return removed_keys_;
    }
    [[nodiscard]] const UiRuntimeNode* find(UiStableKey instance_key) const noexcept;
    [[nodiscard]] const UiRuntimeNode* find_blueprint_node(UiStableKey node_key) const noexcept;
    [[nodiscard]] UiRuntimeChartView chart_view(std::uint32_t node_index) const noexcept;
    [[nodiscard]] std::uint32_t root_index() const noexcept { return root_index_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool mounted() const noexcept { return mount_root_.valid(); }

private:
    enum class MountKind : std::uint8_t { None, Screen, Template };

    const ScriptedGuiBlueprint* blueprint_ = nullptr;
    ScriptedGuiRuntimeConfig config_{};
    MountKind mount_kind_ = MountKind::None;
    UiStableKey mount_key_ = 0;
    UiNodeId mount_root_{};
    UiDataContextId mount_context_{};
    UiDataEntityRef mount_source_{};
    std::vector<UiRuntimeNode> nodes_;
    std::vector<UiStableKey> removed_keys_;
    std::uint32_t root_index_ = kInvalidUiRuntimeNode;
    std::uint64_t generation_ = 0;
    bool force_rebuild_ = false;
};

} // namespace thunder
