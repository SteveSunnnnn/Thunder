#include "thunder/presentation/ui/ScriptedGuiRuntime.hpp"

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/presentation/ui/StrategyUi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace thunder {

namespace {

[[nodiscard]] inline UiStableKey combine_instance_key(UiStableKey parent_key,
                                                      UiStableKey node_key,
                                                      std::size_t index = 0) noexcept {
    Fnv1a64 hash;
    hash.add(parent_key);
    hash.add(node_key);
    if (index != 0) {
        hash.add(static_cast<std::uint64_t>(index));
    }
    return hash.value();
}

} // namespace


ScriptedGuiRuntime::ScriptedGuiRuntime(const ScriptedGuiBlueprint& blueprint,
                                       ScriptedGuiRuntimeConfig config)
    : blueprint_(&blueprint), config_(config) {}

void ScriptedGuiRuntime::clear() noexcept {
    removed_keys_.clear();
    for (const auto& node : nodes_) {
        if (node.instance_key != 0) {
            removed_keys_.push_back(node.instance_key);
        }
    }
    nodes_.clear();
    mount_kind_ = MountKind::None;
    mount_key_ = 0;
    mount_root_ = UiNodeId{};
    mount_context_ = UiDataContextId{};
    mount_source_ = UiDataEntityRef{};
    root_index_ = kInvalidUiRuntimeNode;
    force_rebuild_ = false;
}

bool ScriptedGuiRuntime::instantiate_screen(UiStableKey screen_key, UiDataEntityRef root) noexcept {
    if (blueprint_ == nullptr || !root.valid()) return false;
    const auto* screen = blueprint_->find_screen(screen_key);
    if (screen == nullptr || screen->context != root.context || !screen->root.valid()) {
        return false;
    }
    clear();
    mount_kind_ = MountKind::Screen;
    mount_key_ = screen_key;
    mount_root_ = screen->root;
    mount_context_ = screen->context;
    mount_source_ = root;
    force_rebuild_ = true;
    return true;
}

bool ScriptedGuiRuntime::instantiate_template(UiStableKey template_key, UiDataEntityRef root) noexcept {
    if (blueprint_ == nullptr || !root.valid()) return false;
    const auto* tmpl = blueprint_->find_template(template_key);
    if (tmpl == nullptr || tmpl->context != root.context || !tmpl->root.valid()) {
        return false;
    }
    clear();
    mount_kind_ = MountKind::Template;
    mount_key_ = template_key;
    mount_root_ = tmpl->root;
    mount_context_ = tmpl->context;
    mount_source_ = root;
    force_rebuild_ = true;
    return true;
}

const UiRuntimeNode* ScriptedGuiRuntime::find(UiStableKey instance_key) const noexcept {
    if (instance_key == 0) return nullptr;
    for (const auto& node : nodes_) {
        if (node.instance_key == instance_key) return &node;
    }
    return nullptr;
}

const UiRuntimeNode* ScriptedGuiRuntime::find_blueprint_node(UiStableKey node_key) const noexcept {
    if (node_key == 0 || blueprint_ == nullptr) return nullptr;
    for (const auto& node : nodes_) {
        if (!node.blueprint_node.valid()) continue;
        if (node.blueprint_node.value() < blueprint_->nodes().size()) {
            if (blueprint_->nodes()[node.blueprint_node.value()].stable_key == node_key) {
                return &node;
            }
        }
    }
    return nullptr;
}

UiRuntimeChartView ScriptedGuiRuntime::chart_view(std::uint32_t node_index) const noexcept {
    if (node_index >= nodes_.size()) return {};
    const auto& node = nodes_[node_index];
    return UiRuntimeChartView{
        .values = node.chart_values,
        .labels = node.chart_labels,
        .source_points = node.chart_source_points,
        .kind = node.chart_kind,
        .include_zero = node.chart_include_zero,
        .downsampled = node.chart_downsampled
    };
}

UiRuntimeDiff ScriptedGuiRuntime::refresh(const ScriptedGuiDataProvider& provider,
                                          std::span<const UiCollectionViewport> viewports) {
    ++generation_;
    UiRuntimeDiff diff{
        .generation = generation_,
        .dirty_nodes = 0,
        .removed_nodes = removed_keys_.size(),
        .provider_errors = 0,
        .truncated = false
    };
    removed_keys_.clear();

    if (!mounted() || blueprint_ == nullptr) {
        return diff;
    }

    const auto instantiate_subtree = [this, &diff](auto& self,
                                                  UiNodeId bp_id,
                                                  UiDataEntityRef source,
                                                  std::uint32_t parent_idx,
                                                  UiStableKey parent_key,
                                                  std::uint16_t depth) -> std::uint32_t {
        if (!bp_id.valid() || bp_id.value() >= blueprint_->nodes().size()) {
            return kInvalidUiRuntimeNode;
        }
        if (depth > config_.max_template_depth || nodes_.size() >= config_.max_nodes) {
            diff.truncated = true;
            return kInvalidUiRuntimeNode;
        }

        const auto& compiled = blueprint_->nodes()[bp_id.value()];
        const auto instance_key = (parent_key != 0)
                                      ? combine_instance_key(parent_key, compiled.stable_key)
                                      : compiled.stable_key;

        UiRuntimeNode node{};
        node.instance_key = instance_key;
        node.parent_key = parent_key;
        node.blueprint_node = bp_id;
        node.source = source;
        node.parent = parent_idx;
        node.first_child = kInvalidUiRuntimeNode;
        node.next_sibling = kInvalidUiRuntimeNode;
        node.kind = compiled.kind;
        node.visible = has_flag(compiled.flags, UiNodeFlags::VisibleByDefault);
        node.enabled = has_flag(compiled.flags, UiNodeFlags::EnabledByDefault);
        node.command = compiled.command;
        node.command_key = compiled.command_key;
        node.tooltip_key = compiled.tooltip_key;
        node.generation = generation_;
        node.dirty = UiRuntimeDirty::Topology | UiRuntimeDirty::Visibility | UiRuntimeDirty::Enabled;

        for (std::size_t i = 0; i < compiled.constant_count; ++i) {
            const auto& c = blueprint_->constants()[compiled.first_constant + i];
            switch (c.target) {
            case UiConstantTarget::Text:
                if (c.value_type == UiValueType::LocalizationKey) {
                    node.text_key = c.stable_key();
                    node.text_is_localization = true;
                } else if (c.value_type == UiValueType::Text) {
                    node.text_key = c.stable_key();
                    node.text_is_localization = false;
                }
                break;
            case UiConstantTarget::Value:
                node.value = c.number();
                break;
            case UiConstantTarget::Icon:
                node.icon_key = c.stable_key();
                break;
            case UiConstantTarget::Module:
                node.module_key = c.stable_key();
                break;
            case UiConstantTarget::Selected:
                node.selected = (c.payload != 0);
                node.selected_mix = node.selected ? 1.0f : 0.0f;
                break;
            case UiConstantTarget::Hovered:
                node.hovered = (c.payload != 0);
                node.hover_mix = node.hovered ? 1.0f : 0.0f;
                break;
            case UiConstantTarget::Pressed:
                node.pressed = (c.payload != 0);
                node.press_mix = node.pressed ? 1.0f : 0.0f;
                break;
            case UiConstantTarget::Focused:
                node.focused = (c.payload != 0);
                node.focus_mix = node.focused ? 1.0f : 0.0f;
                break;
            case UiConstantTarget::Style:
                node.surface_style = static_cast<UiSurfaceStyle>(c.payload);
                break;
            default:
                break;
            }
        }

        if (node.kind == UiWidgetKind::Chart && compiled.auxiliary_index != kInvalidUiIndex &&
            compiled.auxiliary_index < blueprint_->charts().size()) {
            const auto& chart_meta = blueprint_->charts()[compiled.auxiliary_index];
            node.chart_kind = chart_meta.kind;
            node.chart_include_zero = chart_meta.include_zero;
        }

        const auto current_idx = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(std::move(node));

        if (parent_idx != kInvalidUiRuntimeNode && parent_idx < nodes_.size()) {
            if (nodes_[parent_idx].first_child == kInvalidUiRuntimeNode) {
                nodes_[parent_idx].first_child = current_idx;
            } else {
                auto sibling = nodes_[parent_idx].first_child;
                while (nodes_[sibling].next_sibling != kInvalidUiRuntimeNode) {
                    sibling = nodes_[sibling].next_sibling;
                }
                nodes_[sibling].next_sibling = current_idx;
            }
        }

        if (compiled.kind == UiWidgetKind::TemplateInstance) {
            if (compiled.auxiliary_index != kInvalidUiIndex &&
                compiled.auxiliary_index < blueprint_->templates().size()) {
                const auto& tmpl = blueprint_->templates()[compiled.auxiliary_index];
                if (tmpl.root.valid()) {
                    self(self, tmpl.root, source, current_idx, instance_key, depth + 1);
                }
            }
        } else if (compiled.kind != UiWidgetKind::List && compiled.kind != UiWidgetKind::Grid) {
            auto child_id = compiled.first_child;
            while (child_id.valid() && child_id.value() < blueprint_->nodes().size()) {
                self(self, child_id, source, current_idx, instance_key, depth);
                child_id = blueprint_->nodes()[child_id.value()].next_sibling;
            }
        }

        return current_idx;
    };

    // Clear last frame's dirty flags BEFORE rebuilding: freshly instantiated
    // nodes report Topology|Visibility|Enabled, and resetting after the build
    // wiped that flag so the retained-mode renderer never learned the tree had
    // been (re)created.
    for (auto& node : nodes_) {
        node.dirty = UiRuntimeDirty::None;
    }

    if (force_rebuild_ || nodes_.empty()) {
        nodes_.clear();
        root_index_ = instantiate_subtree(instantiate_subtree, mount_root_, mount_source_,
                                           kInvalidUiRuntimeNode, 0, 0);
        force_rebuild_ = false;
    }

    const auto initial_node_count = nodes_.size();
    for (std::size_t n_idx = 0; n_idx < initial_node_count; ++n_idx) {
        auto& node = nodes_[n_idx];
        if (!node.blueprint_node.valid() || node.blueprint_node.value() >= blueprint_->nodes().size()) {
            continue;
        }
        const auto& compiled = blueprint_->nodes()[node.blueprint_node.value()];

        for (std::size_t b_idx = 0; b_idx < compiled.binding_count; ++b_idx) {
            const auto& binding = blueprint_->bindings()[compiled.first_binding + b_idx];
            const auto steps = blueprint_->binding_steps().subspan(binding.first_step, binding.step_count);

            UiDataEntityRef cur_entity = node.source;
            UiDataValue out_val{};
            bool read_ok = true;

            for (std::size_t s = 0; s < steps.size(); ++s) {
                if (!cur_entity.valid() || !provider.read_property(cur_entity, steps[s].property_slot, out_val)) {
                    read_ok = false;
                    break;
                }
                if (s + 1 < steps.size()) {
                    cur_entity = out_val.entity;
                }
            }

            if (!read_ok) {
                ++diff.provider_errors;
                if (node.data_valid) {
                    node.data_valid = false;
                    node.dirty |= UiRuntimeDirty::DataValidity;
                }
                continue;
            }

            if (!node.data_valid) {
                node.data_valid = true;
                node.dirty |= UiRuntimeDirty::DataValidity;
            }

            switch (binding.target) {
            case UiBindingTarget::Visible:
                if (node.visible != out_val.boolean) {
                    node.visible = out_val.boolean;
                    node.dirty |= UiRuntimeDirty::Visibility;
                }
                break;
            case UiBindingTarget::Enabled:
                if (node.enabled != out_val.boolean) {
                    node.enabled = out_val.boolean;
                    node.dirty |= UiRuntimeDirty::Enabled;
                }
                break;
            case UiBindingTarget::Selected:
                if (node.selected != out_val.boolean) {
                    node.selected = out_val.boolean;
                    node.dirty |= UiRuntimeDirty::Selected;
                }
                break;
            case UiBindingTarget::Value:
                if (node.value != out_val.number) {
                    node.value = out_val.number;
                    node.dirty |= UiRuntimeDirty::Value;
                }
                break;
            case UiBindingTarget::Text:
                if (out_val.type == UiValueType::LocalizationKey) {
                    if (node.text_key != out_val.stable_key || !node.text_is_localization) {
                        node.text_key = out_val.stable_key;
                        node.text_is_localization = true;
                        node.text.clear();
                        node.dirty |= UiRuntimeDirty::Text;
                    }
                } else if (out_val.type == UiValueType::Text) {
                    if (node.text != out_val.text || node.text_is_localization) {
                        node.text = std::string(out_val.text.substr(0, config_.max_text_bytes));
                        // Truncating at an arbitrary byte index can split a
                        // multi-byte codepoint, producing invalid UTF-8 that the
                        // font atlas renders as U+FFFD. Back off to the last
                        // complete codepoint boundary.
                        while (!node.text.empty() &&
                               (static_cast<unsigned char>(node.text.back()) & 0xC0u) == 0x80u) {
                            node.text.pop_back();
                        }
                        if (!node.text.empty() &&
                            (static_cast<unsigned char>(node.text.back()) & 0xC0u) == 0xC0u) {
                            node.text.pop_back();
                        }
                        node.text_is_localization = false;
                        node.text_key = 0;
                        node.dirty |= UiRuntimeDirty::Text;
                    }
                }
                break;
            case UiBindingTarget::Icon:
                if (node.icon_key != out_val.stable_key) {
                    node.icon_key = out_val.stable_key;
                    node.dirty |= UiRuntimeDirty::Icon;
                }
                break;
            case UiBindingTarget::Items:
                if (node.items != out_val.collection || node.item_count != out_val.collection.size) {
                    node.items = out_val.collection;
                    node.item_count = out_val.collection.size;
                    node.dirty |= UiRuntimeDirty::Items;
                }
                break;
            case UiBindingTarget::ChartSeries: {
                const auto total_pts = out_val.series.size;
                node.chart_source_points = total_pts;
                std::uint32_t max_pts = config_.max_chart_points;
                if (compiled.auxiliary_index != kInvalidUiIndex &&
                    compiled.auxiliary_index < blueprint_->charts().size()) {
                    const auto& chart_meta = blueprint_->charts()[compiled.auxiliary_index];
                    if (chart_meta.max_points > 0) {
                        max_pts = std::min(max_pts, chart_meta.max_points);
                    }
                }

                std::vector<float> new_values;
                bool downsampled = false;
                if (total_pts <= max_pts || max_pts < 2) {
                    new_values.reserve(total_pts);
                    for (std::size_t i = 0; i < total_pts; ++i) {
                        double v = 0.0;
                        if (provider.number_series_value(out_val.series, i, v)) {
                            new_values.push_back(static_cast<float>(v));
                        }
                    }
                } else {
                    downsampled = true;
                    new_values.reserve(max_pts);
                    const double step = static_cast<double>(total_pts - 1) / static_cast<double>(max_pts - 1);
                    for (std::size_t i = 0; i < max_pts; ++i) {
                        const auto start_idx = static_cast<std::size_t>(static_cast<double>(i) * step);
                        const auto end_idx = static_cast<std::size_t>(static_cast<double>(i + 1) * step);
                        double peak = 0.0;
                        bool has_peak = false;
                        for (std::size_t k = start_idx; k <= end_idx && k < total_pts; ++k) {
                            double cur = 0.0;
                            if (provider.number_series_value(out_val.series, k, cur)) {
                                if (!has_peak || cur > peak) {
                                    peak = cur;
                                    has_peak = true;
                                }
                            }
                        }
                        if (has_peak) {
                            new_values.push_back(static_cast<float>(peak));
                        }
                    }
                }

                if (node.chart_values != new_values || node.chart_downsampled != downsampled) {
                    node.chart_values = std::move(new_values);
                    node.chart_downsampled = downsampled;
                    node.dirty |= UiRuntimeDirty::Chart;
                }
                break;
            }
            case UiBindingTarget::ChartLabels: {
                std::vector<UiStableKey> new_labels;
                const auto count = std::min(out_val.series.size, static_cast<std::size_t>(config_.max_chart_points));
                new_labels.reserve(count);
                for (std::size_t i = 0; i < count; ++i) {
                    UiStableKey key = 0;
                    if (provider.text_series_value(out_val.series, i, key)) {
                        new_labels.push_back(key);
                    }
                }
                if (node.chart_labels != new_labels) {
                    node.chart_labels = std::move(new_labels);
                    node.dirty |= UiRuntimeDirty::Chart;
                }
                break;
            }
            }
        }

        if (node.kind == UiWidgetKind::List || node.kind == UiWidgetKind::Grid) {
            float scroll_y = 0.0f;
            float viewport_h = config_.default_viewport_height;
            for (const auto& vp : viewports) {
                if (vp.node_or_instance_key == node.instance_key ||
                    vp.node_or_instance_key == compiled.stable_key) {
                    scroll_y = vp.scroll_y;
                    if (vp.viewport_height > 0.0f) {
                        viewport_h = vp.viewport_height;
                    }
                    break;
                }
            }

            if (node.kind == UiWidgetKind::List && compiled.auxiliary_index < blueprint_->lists().size()) {
                const auto& list_meta = blueprint_->lists()[compiled.auxiliary_index];
                const auto old_window = node.item_window;
                if (list_meta.virtualized) {
                    node.item_window = virtualize_rows(node.item_count, list_meta.row_height,
                                                       scroll_y, viewport_h, list_meta.overscan);
                } else {
                    node.item_window = UiVirtualWindow{0, node.item_count, 0.0f, 0.0f};
                }
                node.visible_item_count = std::min(node.item_window.count, config_.max_visible_collection_items);
                if (old_window.first != node.item_window.first || old_window.count != node.item_window.count) {
                    node.dirty |= UiRuntimeDirty::Items;
                }
            } else if (node.kind == UiWidgetKind::Grid && compiled.auxiliary_index < blueprint_->grids().size()) {
                const auto& grid_meta = blueprint_->grids()[compiled.auxiliary_index];
                const auto old_window = node.item_window;
                const auto cols = grid_meta.columns > 0 ? grid_meta.columns : 1u;
                const auto total_rows = (node.item_count + cols - 1u) / cols;
                const float row_h = grid_meta.row_height + grid_meta.row_gap;
                if (grid_meta.virtualized) {
                    node.item_window = virtualize_rows(total_rows, row_h, scroll_y, viewport_h,
                                                       grid_meta.overscan);
                } else {
                    node.item_window = UiVirtualWindow{0, total_rows, 0.0f, 0.0f};
                }
                node.visible_item_count = std::min(node.item_window.count * cols,
                                                   config_.max_visible_collection_items);
                if (old_window.first != node.item_window.first || old_window.count != node.item_window.count) {
                    node.dirty |= UiRuntimeDirty::Items;
                }
            }
        }
    }

    for (auto& node : nodes_) {
        if (node.dirty != UiRuntimeDirty::None) {
            node.generation = generation_;
            ++diff.dirty_nodes;
        }
    }

    return diff;
}

} // namespace thunder
