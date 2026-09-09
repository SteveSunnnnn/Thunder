#include "thunder/runtime/editor/GuiLayoutEditor.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    using namespace thunder;
    std::cout << "Thunder GUI Layout Editor v1.0\n";
    std::cout << "===========================\n";

    GuiLayoutEditor editor;
    editor.set_inspect_mode(true);

    if (argc > 1) {
        const std::string command = argv[1];
        if (command == "--help" || command == "-h") {
            std::cout << "Usage: thunder_gui_editor [options]\n"
                      << "  --info                Display current editor configuration\n"
                      << "  --help                Show this help message\n";
            return 0;
        }
    }

    std::cout << "GUI Layout Editor initialized in inspector mode.\n"
              << "Widget count: " << editor.widget_count() << "\n"
              << "Inspector mode active: " << (editor.is_inspect_mode() ? "true" : "false") << "\n";
    return 0;
}
