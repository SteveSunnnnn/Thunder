#include "thunder/presentation/ui/ScriptedGuiPainter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace thunder {
namespace {

[[nodiscard]] float bounded(float value, float minimum, float maximum) noexcept {
    if (!std::isfinite(value)) return minimum;
    return std::clamp(value, minimum, maximum);
}

} // namespace

ScriptedGuiPainter::LayoutStyle
ScriptedGuiPainter::style_for(const UiRuntimeNode& node) const noexcept {
    LayoutStyle style;
    if (blueprint_ == nullptr || !node.blueprint_node.valid() ||
        node.blueprint_node.value() >= blueprint_->nodes().size()) return style;
    const auto& compiled = blueprint_->nodes()[node.blueprint_node.value()];
    for (std::size_t index = 0; index < compiled.constant_count; ++index) {
        const auto& value = blueprint_->constants()[compiled.first_constant + index];
        if (value.value_type != UiValueType::Number) continue;
        const float number = static_cast<float>(value.number());
        switch (value.target) {
        case UiConstantTarget::Width: style.width = number; break;
        case UiConstantTarget::Height: style.height = number; break;
        case UiConstantTarget::MinWidth: style.min_width = number; break;
        case UiConstantTarget::MinHeight: style.min_height = number; break;
        case UiConstantTarget::MaxWidth: style.max_width = number; break;
        case UiConstantTarget::MaxHeight: style.max_height = number; break;
        case UiConstantTarget::Grow: style.grow = std::max(number, 0.0f); break;
        case UiConstantTarget::Gap: style.gap = std::max(number, 0.0f); break;
        default: break;
        }
    }
    return style;
}

void ScriptedGuiPainter::paint(const ScriptedGuiRuntime& runtime,
                               UiDrawList& draw_list,
                               UiRect screen,
                               const ScriptedGuiTextResolver& resolve_text) const {
    if (blueprint_ == nullptr || runtime.root_index() == kInvalidUiRuntimeNode ||
        runtime.root_index() >= runtime.nodes().size() || screen.w <= 0.0f || screen.h <= 0.0f) return;
    tooltip_.reset();
    paint_node(runtime, runtime.root_index(), draw_list, screen, resolve_text, 0u);
    if (tooltip_ && resolve_text) {
        const auto tooltip_text = resolve_text(tooltip_->key);
        if (!tooltip_text.empty()) draw_tooltip(draw_list, screen, tooltip_->anchor, tooltip_text);
    }
    tooltip_.reset();
}

void ScriptedGuiPainter::paint_children(const ScriptedGuiRuntime& runtime,
                                        const UiRuntimeNode& node,
                                        UiDrawList& draw_list,
                                        UiRect content,
                                        const ScriptedGuiTextResolver& resolve_text,
                                        std::uint16_t depth) const {
    // Collect visible children without a per-node heap allocation. A small
    // inline buffer covers the overwhelmingly common case (rows/columns/stacks
    // with a handful of children); deeper trees spill to a heap vector that is
    // reused across the recursion. This is a pure allocation-count change and
    // does not alter the resulting layout or draw order.
    std::array<std::uint32_t, 24> inline_children{};
    std::vector<std::uint32_t> overflow_children;
    std::uint32_t count = 0;
    auto child = node.first_child;
    while (child != kInvalidUiRuntimeNode && child < runtime.nodes().size()) {
        if (runtime.nodes()[child].visible) {
            if (count < inline_children.size()) {
                inline_children[count] = child;
            } else {
                if (count == inline_children.size()) {
                    overflow_children.assign(inline_children.begin(), inline_children.end());
                }
                overflow_children.push_back(child);
            }
            ++count;
        }
        child = runtime.nodes()[child].next_sibling;
    }
    if (count == 0) return;
    const std::uint32_t* const children =
        count <= inline_children.size() ? inline_children.data() : overflow_children.data();

    if (node.kind == UiWidgetKind::Stack || node.kind == UiWidgetKind::Panel) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto index = children[i];
            const auto child_style = style_for(runtime.nodes()[index]);
            UiRect child_rect = content;
            if (child_style.width >= 0.0f)
                child_rect.w = bounded(child_style.width, child_style.min_width, child_style.max_width);
            if (child_style.height >= 0.0f)
                child_rect.h = bounded(child_style.height, child_style.min_height, child_style.max_height);
            paint_node(runtime, index, draw_list, child_rect, resolve_text, depth + 1u);
        }
        return;
    }

    const bool horizontal = node.kind == UiWidgetKind::Row;
    const float gap = [&] {
        const auto own = style_for(node);
        return own.gap >= 0.0f ? own.gap : theme_.gap;
    }();
    const float available = std::max(0.0f, (horizontal ? content.w : content.h) -
        gap * static_cast<float>(count - 1u));
    float fixed = 0.0f;
    float total_grow = 0.0f;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = children[i];
        const auto child_style = style_for(runtime.nodes()[index]);
        const float explicit_size = horizontal ? child_style.width : child_style.height;
        if (explicit_size >= 0.0f) fixed += explicit_size;
        else total_grow += child_style.grow > 0.0f ? child_style.grow : 1.0f;
    }
    const float flexible = std::max(0.0f, available - fixed);
    float cursor = horizontal ? content.x : content.y;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = children[i];
        const auto child_style = style_for(runtime.nodes()[index]);
        const float explicit_size = horizontal ? child_style.width : child_style.height;
        const float weight = child_style.grow > 0.0f ? child_style.grow : 1.0f;
        float size = explicit_size >= 0.0f ? explicit_size :
            (total_grow > 0.0f ? flexible * weight / total_grow : 0.0f);
        size = horizontal ? bounded(size, child_style.min_width, child_style.max_width)
                          : bounded(size, child_style.min_height, child_style.max_height);
        UiRect child_rect = content;
        if (horizontal) {
            child_rect.x = cursor;
            child_rect.w = size;
        } else {
            child_rect.y = cursor;
            child_rect.h = size;
        }
        paint_node(runtime, index, draw_list, child_rect, resolve_text, depth + 1u);
        cursor += size + gap;
    }
}
} // namespace thunder
