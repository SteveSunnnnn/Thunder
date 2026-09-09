#include "thunder/presentation/ui/LocalizationRichText.hpp"

namespace thunder {

void render_rich_text(UiDrawList& ui, std::span<const RichTextToken> tokens,
                      float start_x, float start_y, float font_size,
                      UiRect scissor) {
    float current_x = start_x;
    for (const auto& token : tokens) {
        if (token.is_icon) {
            ui.quad({current_x, start_y - 2.0f, font_size, font_size},
                    0xffd4af37u, scissor);
            current_x += font_size + 4.0f;
        } else {
            const float approximate_width =
                static_cast<float>(token.text.size()) * (font_size * 0.55f);
            ui.text(token.text, current_x, start_y, font_size, token.rgba, scissor);
            current_x += approximate_width;
        }
    }
}

} // namespace thunder
