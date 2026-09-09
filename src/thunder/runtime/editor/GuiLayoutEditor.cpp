#include "GuiLayoutEditor.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace thunder {

std::uint64_t GuiLayoutEditor::add_widget(GuiWidgetDef def) {
    def.id = next_id_++;
    widgets_.push_back(std::move(def));
    return widgets_.back().id;
}

void GuiLayoutEditor::remove_widget(std::uint64_t id) {
    std::erase_if(widgets_, [id](const GuiWidgetDef& w) { return w.id == id; });
    if (selected_id_ == id) {
        selected_id_ = std::nullopt;
    }
}

void GuiLayoutEditor::clear_widgets() {
    widgets_.clear();
    selected_id_ = std::nullopt;
}

void GuiLayoutEditor::select_widget(std::uint64_t id) {
    deselect_all();
    selected_id_ = id;
    for (auto& w : widgets_) {
        if (w.id == id) w.is_selected = true;
    }
}

void GuiLayoutEditor::deselect_all() {
    selected_id_ = std::nullopt;
    for (auto& w : widgets_) {
        w.is_selected = false;
    }
}

std::optional<std::uint64_t> GuiLayoutEditor::selected_widget_id() const noexcept {
    return selected_id_;
}

const GuiWidgetDef* GuiLayoutEditor::selected_widget() const noexcept {
    if (!selected_id_) return nullptr;
    for (const auto& w : widgets_) {
        if (w.id == *selected_id_) return &w;
    }
    return nullptr;
}

std::optional<std::uint64_t> GuiLayoutEditor::widget_at(float x, float y) const {
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        if (x >= it->bounds.x && x <= it->bounds.x + it->bounds.w &&
            y >= it->bounds.y && y <= it->bounds.y + it->bounds.h) {
            return it->id;
        }
    }
    return std::nullopt;
}

GuiDragMode GuiLayoutEditor::detect_drag_mode(const GuiWidgetDef& widget, float x, float y) const {
    bool on_right = (x >= widget.bounds.x + widget.bounds.w - kHandleSize && x <= widget.bounds.x + widget.bounds.w);
    bool on_bottom = (y >= widget.bounds.y + widget.bounds.h - kHandleSize && y <= widget.bounds.y + widget.bounds.h);
    
    if (on_right && on_bottom) return GuiDragMode::ResizeCorner;
    if (on_right) return GuiDragMode::ResizeRight;
    if (on_bottom) return GuiDragMode::ResizeBottom;
    return GuiDragMode::Move;
}

void GuiLayoutEditor::on_mouse_down(float x, float y) {
    auto id_opt = widget_at(x, y);
    if (id_opt) {
        select_widget(*id_opt);
        auto* w = const_cast<GuiWidgetDef*>(selected_widget());
        drag_mode_ = detect_drag_mode(*w, x, y);
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_original_bounds_ = w->bounds;
        is_dragging_ = true;
    } else {
        deselect_all();
    }
}

void GuiLayoutEditor::on_mouse_drag(float x, float y) {
    if (!is_dragging_ || !selected_id_) return;
    auto* w = const_cast<GuiWidgetDef*>(selected_widget());
    if (!w) return;

    float dx = x - drag_start_x_;
    float dy = y - drag_start_y_;
    
    auto guides = compute_guides(w->id);

    if (drag_mode_ == GuiDragMode::Move) {
        float new_x = drag_original_bounds_.x + dx;
        float new_y = drag_original_bounds_.y + dy;
        w->bounds.x = snap_to_guide(new_x, guides, true);
        w->bounds.y = snap_to_guide(new_y, guides, false);
    } else if (drag_mode_ == GuiDragMode::ResizeRight) {
        float new_w = std::max(20.0f, drag_original_bounds_.w + dx);
        w->bounds.w = snap_to_guide(w->bounds.x + new_w, guides, true) - w->bounds.x;
    } else if (drag_mode_ == GuiDragMode::ResizeBottom) {
        float new_h = std::max(20.0f, drag_original_bounds_.h + dy);
        w->bounds.h = snap_to_guide(w->bounds.y + new_h, guides, false) - w->bounds.y;
    } else if (drag_mode_ == GuiDragMode::ResizeCorner) {
        float new_w = std::max(20.0f, drag_original_bounds_.w + dx);
        float new_h = std::max(20.0f, drag_original_bounds_.h + dy);
        w->bounds.w = snap_to_guide(w->bounds.x + new_w, guides, true) - w->bounds.x;
        w->bounds.h = snap_to_guide(w->bounds.y + new_h, guides, false) - w->bounds.y;
    }
}

void GuiLayoutEditor::on_mouse_up() {
    is_dragging_ = false;
    drag_mode_ = GuiDragMode::None;
}

void GuiLayoutEditor::on_mouse_move(float x, float y) {
    for (auto& w : widgets_) {
        w.is_hovered = (x >= w.bounds.x && x <= w.bounds.x + w.bounds.w &&
                        y >= w.bounds.y && y <= w.bounds.y + w.bounds.h);
    }
}

void GuiLayoutEditor::on_delete_key() {
    if (selected_id_) {
        remove_widget(*selected_id_);
    }
}

void GuiLayoutEditor::render_widgets(UiDrawList& ui, UiRect /*screen*/) const {
    const auto& t = ui.theme();
    for (const auto& w : widgets_) {
        if (w.type == "panel") {
            ui.wood_panel(w.bounds);
        } else if (w.type == "button") {
            ui.brass_button(w.bounds, w.label.empty() ? "Button" : w.label);
        } else if (w.type == "parchment") {
            ui.parchment_panel(w.bounds);
        } else if (w.type == "progress_bar") {
            ui.progress_bar(w.bounds, 0.65f);
        } else {
            ui.quad(w.bounds, ui_blend(0x00000000u, t.colors.state_selected, 0.25f));
            ui.text(w.label.empty() ? w.type : w.label, w.bounds.x + 4.0f, w.bounds.y + 4.0f,
                    t.type.body, t.colors.text_primary);
        }
    }
}

void GuiLayoutEditor::render_overlay(UiDrawList& ui, UiRect /*screen*/) const {
    const auto& t = ui.theme();
    if (auto* w = selected_widget()) {
        std::vector<float> box_pts{
            w->bounds.x, w->bounds.y,
            w->bounds.x + w->bounds.w, w->bounds.y,
            w->bounds.x + w->bounds.w, w->bounds.y + w->bounds.h,
            w->bounds.x, w->bounds.y + w->bounds.h,
            w->bounds.x, w->bounds.y
        };
        ui.polyline(box_pts, t.colors.state_focus);

        float hs = kHandleSize;
        ui.quad({w->bounds.x - hs * 0.5f, w->bounds.y - hs * 0.5f, hs, hs}, t.colors.border_selected);
        ui.quad({w->bounds.x + w->bounds.w - hs * 0.5f, w->bounds.y - hs * 0.5f, hs, hs}, t.colors.border_selected);
        ui.quad({w->bounds.x - hs * 0.5f, w->bounds.y + w->bounds.h - hs * 0.5f, hs, hs}, t.colors.border_selected);
        ui.quad({w->bounds.x + w->bounds.w - hs * 0.5f, w->bounds.y + w->bounds.h - hs * 0.5f, hs, hs}, t.colors.border_selected);
    }
}

void GuiLayoutEditor::render_property_panel(UiDrawList& ui, UiRect screen) const {
    const auto& t = ui.theme();
    float pw = 240.0f;
    float ph = 300.0f;
    float px = screen.x + screen.w - pw - 12.0f;
    float py = screen.y + 44.0f;

    ui.parchment_panel({px, py, pw, ph});
    ui.text("GUI LAYOUT INSPECTOR", px + 12.0f, py + 12.0f, t.type.major_header, t.materials.parchment_margin);
    ui.quad({px + 12.0f, py + 28.0f, pw - 24.0f, 1.0f}, t.colors.border_normal);

    if (auto* w = selected_widget()) {
        ui.text("ID: #" + std::to_string(w->id), px + 14.0f, py + 38.0f, t.type.secondary, t.materials.parchment_text);
        ui.text("Type: " + w->type, px + 14.0f, py + 58.0f, t.type.secondary, t.materials.parchment_text);
        ui.text("Label: " + w->label, px + 14.0f, py + 78.0f, t.type.secondary, t.materials.parchment_text);
        ui.text("X: " + std::to_string(static_cast<int>(w->bounds.x)), px + 14.0f, py + 98.0f, t.type.secondary, t.materials.parchment_text);
        ui.text("Y: " + std::to_string(static_cast<int>(w->bounds.y)), px + 14.0f, py + 118.0f, t.type.secondary, t.materials.parchment_text);
        ui.text("W: " + std::to_string(static_cast<int>(w->bounds.w)), px + 14.0f, py + 138.0f, t.type.secondary, t.materials.parchment_text);
        ui.text("H: " + std::to_string(static_cast<int>(w->bounds.h)), px + 14.0f, py + 158.0f, t.type.secondary, t.materials.parchment_text);
    } else {
        ui.text("No widget selected", px + 14.0f, py + 48.0f, t.type.secondary, t.materials.parchment_text_muted);
    }
}

bool GuiLayoutEditor::save_layout(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) return false;
    for (const auto& w : widgets_) {
        out << w.id << " " << w.type << " " << w.bounds.x << " " << w.bounds.y << " " << w.bounds.w << " " << w.bounds.h << " " << (w.label.empty() ? "_" : w.label) << "\n";
    }
    return true;
}

bool GuiLayoutEditor::load_layout(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    clear_widgets();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        GuiWidgetDef w;
        if (iss >> w.id >> w.type >> w.bounds.x >> w.bounds.y >> w.bounds.w >> w.bounds.h >> w.label) {
            if (w.label == "_") w.label = "";
            widgets_.push_back(w);
            next_id_ = std::max(next_id_, w.id + 1);
        }
    }
    return true;
}

std::vector<GuiAlignmentGuide> GuiLayoutEditor::compute_guides(std::uint64_t exclude_id) const {
    std::vector<GuiAlignmentGuide> guides;
    for (const auto& w : widgets_) {
        if (w.id == exclude_id) continue;
        guides.push_back({true, w.bounds.x, kSnapDistance});
        guides.push_back({true, w.bounds.x + w.bounds.w, kSnapDistance});
        guides.push_back({true, w.bounds.x + w.bounds.w / 2.0f, kSnapDistance});
        
        guides.push_back({false, w.bounds.y, kSnapDistance});
        guides.push_back({false, w.bounds.y + w.bounds.h, kSnapDistance});
        guides.push_back({false, w.bounds.y + w.bounds.h / 2.0f, kSnapDistance});
    }
    return guides;
}

float GuiLayoutEditor::snap_to_guide(float value, const std::vector<GuiAlignmentGuide>& guides, bool vertical) const {
    for (const auto& g : guides) {
        if (g.is_vertical == vertical) {
            if (std::abs(value - g.position) <= g.snap_distance) {
                return g.position;
            }
        }
    }
    return value;
}

void GuiLayoutEditor::inspect_at(float x, float y) {
    auto id_opt = widget_at(x, y);
    if (!id_opt) {
        current_inspection_ = std::nullopt;
        return;
    }
    const auto* w = selected_widget();
    if (!w || w->id != *id_opt) {
        select_widget(*id_opt);
        w = selected_widget();
    }
    if (!w) return;

    GuiInspectInfo info;
    info.node_name = w->label.empty() ? ("Node#" + std::to_string(w->id)) : w->label;
    info.type = w->type;
    info.bounds = w->bounds;
    info.data_context = "DataContext[StableKey=" + std::to_string(w->id * 104729u) + "]";
    info.live_variables = {
        {"is_visible", "true"},
        {"is_hovered", w->is_hovered ? "true" : "false"},
        {"is_selected", w->is_selected ? "true" : "false"},
        {"bound_width", std::to_string(static_cast<int>(w->bounds.w)) + "px"},
        {"bound_height", std::to_string(static_cast<int>(w->bounds.h)) + "px"}
    };
    for (const auto& [k, v] : w->properties) {
        info.live_variables.push_back({k, v});
    }
    current_inspection_ = std::move(info);
}

void GuiLayoutEditor::render_dev_inspector(UiDrawList& ui, UiRect screen) const {
    if (!inspect_mode_ || !current_inspection_) return;
    const auto& t = ui.theme();
    const auto& info = *current_inspection_;

    // 1. Highlighting box (Blue/Cyan translucent overlay + gold boundary)
    ui.quad(info.bounds, 0x3300aaffu);
    std::vector<float> box_pts{
        info.bounds.x, info.bounds.y,
        info.bounds.x + info.bounds.w, info.bounds.y,
        info.bounds.x + info.bounds.w, info.bounds.y + info.bounds.h,
        info.bounds.x, info.bounds.y + info.bounds.h,
        info.bounds.x, info.bounds.y
    };
    ui.polyline(box_pts, t.colors.border_gold);

    // 2. Inspector HUD overlay Window
    float iw = 280.0f;
    float ih = 220.0f;
    float ix = std::clamp(info.bounds.x + info.bounds.w + 8.0f, screen.x + 8.0f, screen.x + screen.w - iw - 8.0f);
    float iy = std::clamp(info.bounds.y, screen.y + 8.0f, screen.y + screen.h - ih - 8.0f);

    ui.parchment_panel({ix, iy, iw, ih});
    ui.text("DEV UI INSPECTOR (Ctrl+F8)", ix + 10.0f, iy + 8.0f, t.type.major_header, t.colors.gold);
    ui.quad({ix + 10.0f, iy + 24.0f, iw - 20.0f, 1.0f}, t.colors.border_gold);

    ui.text("Node: " + info.node_name, ix + 12.0f, iy + 30.0f, t.type.secondary, t.materials.parchment_text);
    ui.text("Type: " + info.type, ix + 12.0f, iy + 48.0f, t.type.secondary, t.materials.parchment_text);
    ui.text("Context: " + info.data_context, ix + 12.0f, iy + 66.0f, t.type.secondary, t.materials.parchment_text_muted);

    float var_y = iy + 88.0f;
    ui.text("Live Variables:", ix + 12.0f, var_y, t.type.secondary, t.colors.gold);
    var_y += 18.0f;

    for (std::size_t i = 0; i < std::min<std::size_t>(info.live_variables.size(), 5u); ++i) {
        const auto& [k, v] = info.live_variables[i];
        ui.text(k + ": " + v, ix + 16.0f, var_y, t.type.secondary, t.materials.parchment_text);
        var_y += 16.0f;
    }
}

} // namespace thunder
