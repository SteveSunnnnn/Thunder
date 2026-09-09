#include "thunder/runtime/editor/MapEditorSystem.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    using namespace thunder;
    std::cout << "Thunder Map Editor v1.0\n";
    std::cout << "====================\n";

    MapEditorSystem editor;
    editor.set_active(true);

    if (argc > 1) {
        const std::string command = argv[1];
        if (command == "--help" || command == "-h") {
            std::cout << "Usage: thunder_map_editor [options]\n"
                      << "  --info                Display current editor configuration\n"
                      << "  --paint-test          Run automated paint stroke and undo test\n"
                      << "  --help                Show this help message\n";
            return 0;
        }
        if (command == "--paint-test") {
            editor.set_tool(EditorTool::PaintProvince);
            editor.set_brush_radius(50.0f);
            editor.set_paint_province(ProvinceId{101});
            editor.on_mouse_down(200.0f, 200.0f);
            editor.on_mouse_drag(220.0f, 220.0f);
            editor.on_mouse_up();
            std::cout << "Executed paint stroke. Undo depth: " << editor.undo_depth() << "\n";
            editor.undo();
            std::cout << "Undo successful. Can redo: " << (editor.can_redo() ? "true" : "false") << "\n";
            editor.redo();
            std::cout << "Redo successful. Can undo: " << (editor.can_undo() ? "true" : "false") << "\n";
            return 0;
        }
    }

    std::cout << "Editor initialized in active mode.\n"
              << "Brush radius: " << editor.brush().radius_m << "m\n"
              << "Brush shape: " << (editor.brush().shape == EditorBrushShape::Circle ? "Circle" : "Square") << "\n"
              << "Ready for map editing operations.\n";
    return 0;
}
