#include "thunder/runtime/editor/MapEditorSystem.hpp"
#include "thunder/runtime/editor/GuiLayoutEditor.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>

using namespace thunder;

void test_editor_toggle() {
    MapEditorSystem editor;
    assert(!editor.is_active());
    editor.toggle();
    assert(editor.is_active());
    editor.set_active(false);
    assert(!editor.is_active());
    std::cout << "test_editor_toggle [PASS]\n";
}

void test_tool_selection() {
    MapEditorSystem editor;
    editor.set_tool(EditorTool::PaintTerrain);
    assert(editor.brush().tool == EditorTool::PaintTerrain);
    std::cout << "test_tool_selection [PASS]\n";
}

void test_brush_configuration() {
    MapEditorSystem editor;
    editor.set_brush_radius(100.0f);
    editor.set_brush_shape(EditorBrushShape::Square);
    editor.set_paint_province(ProvinceId{42});
    assert(editor.brush().radius_m == 100.0f);
    assert(editor.brush().shape == EditorBrushShape::Square);
    assert(editor.brush().paint_province.value() == 42);
    std::cout << "test_brush_configuration [PASS]\n";
}

void test_paint_stroke_and_undo() {
    MapEditorSystem editor;
    editor.set_tool(EditorTool::PaintProvince);
    editor.on_mouse_down(100.0f, 100.0f);
    editor.on_mouse_drag(110.0f, 110.0f);
    editor.on_mouse_up();
    assert(editor.undo_depth() >= 1);
    
    editor.undo();
    assert(editor.undo_depth() == 0);
    assert(editor.can_redo() == true);
    std::cout << "test_paint_stroke_and_undo [PASS]\n";
}

void test_redo() {
    MapEditorSystem editor;
    editor.set_tool(EditorTool::PaintProvince);
    editor.on_mouse_down(10.0f, 10.0f);
    editor.on_mouse_up();
    editor.undo();
    assert(editor.can_redo());
    editor.redo();
    assert(editor.can_undo());
    std::cout << "test_redo [PASS]\n";
}

void test_province_selection() {
    MapEditorSystem editor;
    editor.select_province(ProvinceId{5});
    assert(editor.selected_province().value().value() == 5);
    std::cout << "test_province_selection [PASS]\n";
}

void test_render_produces_geometry() {
    MapEditorSystem editor;
    UiDrawList ui;
    UiRect screen{0, 0, 1920, 1080};
    editor.set_active(true);
    editor.render_tool_palette(ui, screen);
    editor.render_property_inspector(ui, screen);
    editor.render_brush_preview(ui, 500, 500, 1.0f);
    editor.render_status_bar(ui, screen);
    assert(!ui.vertices().empty());
    std::cout << "test_render_produces_geometry [PASS]\n";
}

void test_gui_widget_add_remove() {
    GuiLayoutEditor editor;
    auto id1 = editor.add_widget({0, "panel", "p1", {0,0,10,10}, {}, false, false});
    auto id2 = editor.add_widget({0, "button", "b1", {10,10,10,10}, {}, false, false});
    auto id3 = editor.add_widget({0, "text", "t1", {20,20,10,10}, {}, false, false});
    (void)id1; (void)id3;
    assert(editor.widget_count() == 3);
    editor.remove_widget(id2);
    assert(editor.widget_count() == 2);
    std::cout << "test_gui_widget_add_remove [PASS]\n";
}

void test_gui_widget_drag() {
    GuiLayoutEditor editor;
    auto id = editor.add_widget({0, "panel", "p1", {100, 100, 200, 50}, {}, false, false});
    (void)id;
    editor.on_mouse_down(150, 125);
    editor.on_mouse_drag(250, 225);
    editor.on_mouse_up();
    
    assert(editor.widgets()[0].bounds.x == 200);
    assert(editor.widgets()[0].bounds.y == 200);
    std::cout << "test_gui_widget_drag [PASS]\n";
}

void test_gui_layout_save_load() {
    GuiLayoutEditor editor;
    editor.add_widget({0, "panel", "p1", {100, 100, 200, 50}, {}, false, false});
    editor.save_layout("test_layout.txt");
    editor.clear_widgets();
    editor.load_layout("test_layout.txt");
    assert(editor.widget_count() == 1);
    assert(editor.widgets()[0].bounds.x == 100);
    std::filesystem::remove("test_layout.txt");
    std::cout << "test_gui_layout_save_load [PASS]\n";
}

void test_gui_alignment_guides() {
    GuiLayoutEditor editor;
    editor.add_widget({0, "panel", "p1", {100, 100, 200, 50}, {}, false, false});
    auto id2 = editor.add_widget({0, "button", "b1", {300, 300, 50, 50}, {}, false, false});
    (void)id2;
    
    editor.on_mouse_down(325, 325);
    // Drag mouse by -198px (from 325 to 127), so widget left edge moves from 300 to 102 (within 6px snap of 100)
    editor.on_mouse_drag(127, 325);
    editor.on_mouse_up();
    
    assert(editor.widgets()[1].bounds.x == 100);
    std::cout << "test_gui_alignment_guides [PASS]\n";
}

int main() {
    test_editor_toggle();
    test_tool_selection();
    test_brush_configuration();
    test_paint_stroke_and_undo();
    test_redo();
    test_province_selection();
    test_render_produces_geometry();
    test_gui_widget_add_remove();
    test_gui_widget_drag();
    test_gui_layout_save_load();
    test_gui_alignment_guides();
    return 0;
}
