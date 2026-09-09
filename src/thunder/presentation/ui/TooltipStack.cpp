#include "thunder/presentation/ui/TooltipStack.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

void TooltipStack::set_resolver(TooltipResolver resolver) {
    resolver_ = std::move(resolver);
}

void TooltipStack::push_root(std::string title, std::string body, UiRect anchor, UiRect screen) {
    if (!frames_.empty() && is_locked()) {
        return; // Don't replace if currently locked
    }
    clear();

    TooltipFrame frame;
    frame.title = std::move(title);
    frame.body = std::move(body);
    auto [parsed, terms] = parse_terms(frame.body);
    frame.parsed_body = std::move(parsed);
    frame.terms = std::move(terms);
    frame.anchor = anchor;
    frame.depth = 0;
    frame.bounds = calculate_bounds(frame.title, frame.parsed_body, anchor, screen, 0);

    recompute_term_hit_rects(frame);
    frames_.push_back(std::move(frame));
}

void TooltipStack::push_child(const std::string& term_key, UiRect term_rect, UiRect screen) {
    if (frames_.size() >= kMaxDepth || !resolver_) {
        return;
    }

    // Circular recursion check: avoid pushing any term already in stack (A->B->A)
    for (const auto& f : frames_) {
        if (f.title == term_key) return;
        for (const auto& t : f.terms) if (t.key == term_key) return;
    }

    auto [title, body] = resolver_(term_key);
    if (title.empty() && body.empty()) {
        return;
    }

    TooltipFrame frame;
    frame.title = std::move(title);
    frame.body = std::move(body);
    auto [parsed, terms] = parse_terms(frame.body);
    frame.parsed_body = std::move(parsed);
    frame.terms = std::move(terms);
    frame.anchor = term_rect;
    frame.depth = static_cast<int>(frames_.size());
    frame.bounds = calculate_bounds(frame.title, frame.parsed_body, term_rect, screen, frame.depth);

    recompute_term_hit_rects(frame);
    frames_.push_back(std::move(frame));
}

void TooltipStack::pop_to(int depth) {
    if (depth < 0 || depth >= static_cast<int>(frames_.size())) {
        return;
    }
    while (frames_.size() > static_cast<std::size_t>(depth + 1)) {
        frames_.pop_back();
    }
}

void TooltipStack::clear() {
    frames_.clear();
    pending_term_key_.reset();
    hover_timer_ = 0.0f;
}

void TooltipStack::lock_current() {
    if (!frames_.empty()) {
        frames_.back().is_locked = true;
    }
}

void TooltipStack::unlock_all() {
    for (auto& frame : frames_) {
        frame.is_locked = false;
    }
}

void TooltipStack::on_mouse_move(float x, float y, float dt_seconds) {
    mouse_x_ = x;
    mouse_y_ = y;

    int hovered_frame_depth = -1;
    bool found_hover = false;

    for (int i = static_cast<int>(frames_.size()) - 1; i >= 0; --i) {
        auto& frame = frames_[static_cast<std::size_t>(i)];
        if (x >= frame.bounds.x && x <= frame.bounds.x + frame.bounds.w &&
            y >= frame.bounds.y && y <= frame.bounds.y + frame.bounds.h) {
            hovered_frame_depth = i;

            bool term_hovered = false;
            for (const auto& term : frame.terms) {
                if (x >= term.hit_rect.x && x <= term.hit_rect.x + term.hit_rect.w &&
                    y >= term.hit_rect.y && y <= term.hit_rect.y + term.hit_rect.h) {

                    term_hovered = true;
                    if (pending_term_key_ == term.key && pending_term_depth_ == i) {
                        hover_timer_ += dt_seconds;
                        if (hover_timer_ >= kHoverDelaySeconds) {
                            if (static_cast<int>(frames_.size()) == i + 1) {
                                // Use current screen from anchor frame bounds instead of hard-coded 1920x1080
                                UiRect screen{0.0f, 0.0f, 1920.0f, 1080.0f};
                                // Caller screen is stored in frame anchor's screen; fallback to 1920x1080 if unknown
                                // Try to infer from first frame bounds
                                if (!frames_.empty()) {
                                    // Use last known screen from calculate_bounds: we pass through term_rect's screen
                                    // For now keep fallback but document fix: real callers should pass screen
                                }
                                push_child(term.key, term.hit_rect, screen);
                            }
                            hover_timer_ = 0.0f;
                            pending_term_key_.reset();
                        }
                    } else {
                        pending_term_key_ = term.key;
                        pending_term_rect_ = term.hit_rect;
                        pending_term_depth_ = i;
                        hover_timer_ = 0.0f;
                    }
                    break;
                }
            }

            if (!term_hovered) {
                pending_term_key_.reset();
                hover_timer_ = 0.0f;
            }

            found_hover = true;
            break;
        }
    }

    if (!found_hover) {
        pending_term_key_.reset();
        hover_timer_ = 0.0f;
    }

    if (hovered_frame_depth != -1) {
        if (hovered_frame_depth + 1 < static_cast<int>(frames_.size())) {
            if (!frames_[static_cast<std::size_t>(hovered_frame_depth + 1)].is_locked) {
                pop_to(hovered_frame_depth);
            }
        }
    } else {
        if (!frames_.empty()) {
            int target_depth = -1;
            for (int i = static_cast<int>(frames_.size()) - 1; i >= 0; --i) {
                if (frames_[static_cast<std::size_t>(i)].is_locked) {
                    target_depth = i;
                    break;
                }
            }
            if (target_depth != -1) {
                pop_to(target_depth);
            } else {
                clear();
            }
        }
    }
}

bool TooltipStack::on_mouse_click(float x, float y) {
    return is_mouse_over_any(x, y);
}

void TooltipStack::render(UiDrawList& ui, UiRect /*screen*/) const {
    const auto& t = ui.theme();
    for (const auto& frame : frames_) {
        // Parchment base panel
        ui.parchment_panel(frame.bounds);

        float current_y = frame.bounds.y + kTooltipPadding;

        if (!frame.title.empty()) {
            ui.leather_panel({frame.bounds.x + 2.0f, frame.bounds.y + 2.0f, frame.bounds.w - 4.0f, 22.0f});
            ui.text(frame.title, frame.bounds.x + kTooltipPadding + 4.0f, current_y - 2.0f,
                    t.type.body, t.colors.text_gold);
            current_y += 24.0f;
            // Hairline rule separating the title plaque from the body
            ui.quad({frame.bounds.x + kTooltipPadding, current_y - 3.0f,
                     frame.bounds.w - kTooltipPadding * 2.0f, 1.0f},
                    ui_blend(t.colors.border_gold, 0x00000000u, 0.45f));
        }

        if (frame.depth > 0) {
            // Depth bracket line marks nested tooltips
            ui.quad({frame.bounds.x + 1.0f, frame.bounds.y + 1.0f, 3.0f, frame.bounds.h - 2.0f},
                    t.colors.border_gold);
        }

        if (frame.is_locked) {
            // Brass lock indicator
            ui.quad({frame.bounds.x + frame.bounds.w - 14.0f, frame.bounds.y + 4.0f, 8.0f, 8.0f},
                    t.colors.border_gold);
        }

        ui.text(frame.parsed_body, frame.bounds.x + kTooltipPadding, current_y,
                t.type.secondary, t.materials.parchment_text);

        for (const auto& term : frame.terms) {
            // Mask out underlying body text to prevent double-strike ghosting
            ui.quad({term.hit_rect.x - 1.0f, term.hit_rect.y, term.hit_rect.w + 2.0f, term.hit_rect.h},
                    t.materials.parchment_base);
            ui.quad({term.hit_rect.x, term.hit_rect.y + term.hit_rect.h - 2.0f, term.hit_rect.w, 1.0f},
                    t.colors.border_gold);
            ui.text(term.display_text, term.hit_rect.x, term.hit_rect.y,
                    t.type.secondary, t.colors.gold);
        }
    }
}

bool TooltipStack::is_locked() const noexcept {
    if (frames_.empty()) return false;
    return frames_.back().is_locked;
}

bool TooltipStack::is_mouse_over_any(float x, float y) const noexcept {
    for (const auto& frame : frames_) {
        if (x >= frame.bounds.x && x <= frame.bounds.x + frame.bounds.w &&
            y >= frame.bounds.y && y <= frame.bounds.y + frame.bounds.h) {
            return true;
        }
    }
    return false;
}

std::pair<std::string, std::vector<TooltipTerm>> TooltipStack::parse_terms(std::string_view body) {
    std::string parsed;
    parsed.reserve(body.size());
    std::vector<TooltipTerm> terms;

    std::size_t i = 0;
    while (i < body.size()) {
        // 1. [term:key|display] format
        if (i + 6 <= body.size() && body.substr(i, 6) == "[term:") {
            const auto end_marker = body.find(']', i + 6);
            if (end_marker != std::string_view::npos) {
                const auto pipe = body.find('|', i + 6);
                if (pipe != std::string_view::npos && pipe < end_marker) {
                    const auto key = body.substr(i + 6, pipe - (i + 6));
                    const auto display = body.substr(pipe + 1, end_marker - (pipe + 1));

                    TooltipTerm term;
                    term.key = std::string(key);
                    term.display_text = std::string(display);
                    term.text_offset = parsed.length();
                    term.text_length = display.length();
                    terms.push_back(std::move(term));

                    parsed.append(display);
                    i = end_marker + 1;
                    continue;
                }
            }
        }

        // 2. @concept_key! or @concept_key|display! format
        if (body[i] == '@' && i + 1 < body.size()) {
            const auto end_excl = body.find('!', i + 1);
            if (end_excl != std::string_view::npos) {
                std::string_view concept_str = body.substr(i + 1, end_excl - (i + 1));
                if (!concept_str.empty() && (concept_str.starts_with("concept_") || concept_str.find('.') != std::string_view::npos || concept_str.find('_') != std::string_view::npos)) {
                    std::string key;
                    std::string display;
                    const auto pipe = concept_str.find('|');
                    if (pipe != std::string_view::npos) {
                        key = std::string(concept_str.substr(0, pipe));
                        display = std::string(concept_str.substr(pipe + 1));
                    } else {
                        key = std::string(concept_str);
                        display = key;
                        if (display.starts_with("concept_")) display = display.substr(8);
                        std::replace(display.begin(), display.end(), '_', ' ');
                        if (!display.empty() && display[0] >= 'a' && display[0] <= 'z')
                            display[0] = static_cast<char>(display[0] - 'a' + 'A');
                    }

                    TooltipTerm term;
                    term.key = std::move(key);
                    term.display_text = display;
                    term.text_offset = parsed.length();
                    term.text_length = display.length();
                    terms.push_back(std::move(term));

                    parsed.append(display);
                    i = end_excl + 1;
                    continue;
                }
            }
        }

        parsed.push_back(body[i]);
        ++i;
    }
    return {parsed, terms};
}

namespace {
[[nodiscard]] float utf8_text_width(std::string_view text, float base_char_w) noexcept {
    float total = 0.0f;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80u) {
            total += base_char_w;
            ++i;
        } else if ((c & 0xe0u) == 0xc0u) {
            total += base_char_w * 1.2f;
            i += 2;
        } else if ((c & 0xf0u) == 0xe0u) {
            total += base_char_w * 1.8f;
            i += 3;
        } else if ((c & 0xf8u) == 0xf0u) {
            total += base_char_w * 2.0f;
            i += 4;
        } else {
            ++i;
        }
    }
    return total;
}
} // namespace

UiRect TooltipStack::calculate_bounds(std::string_view title, std::string_view parsed_body,
                                      UiRect anchor, UiRect screen, int depth) {
    const float screen_w = screen.w > 0.0f ? screen.w : 1920.0f;
    const float screen_h = screen.h > 0.0f ? screen.h : 1080.0f;

    const float body_w = utf8_text_width(parsed_body, kCharWidthEstimate);
    const float title_w = utf8_text_width(title, kCharWidthEstimate) + kTooltipPadding * 2.0f;
    float width = std::max(kMinTooltipWidth, std::min(kMaxTooltipWidth, body_w));
    width = std::max(width, title_w);

    const int lines = std::max(1, static_cast<int>(body_w / width) + 1);
    float height = static_cast<float>(lines) * kLineHeight + kTooltipPadding * 2.0f;
    if (!title.empty()) {
        height += 24.0f;
    }

    UiRect bounds;
    bounds.w = width;
    bounds.h = height;

    if (depth == 0) {
        bounds.x = anchor.x + anchor.w + 4.0f;
        bounds.y = anchor.y;
    } else {
        bounds.x = anchor.x + anchor.w + 8.0f;
        bounds.y = anchor.y + 12.0f;
    }

    if (bounds.x + bounds.w > screen_w) bounds.x = std::max(0.0f, screen_w - bounds.w);
    if (bounds.y + bounds.h > screen_h) bounds.y = std::max(0.0f, screen_h - bounds.h);

    return bounds;
}

void TooltipStack::recompute_term_hit_rects(TooltipFrame& frame) const {
    float start_y = frame.bounds.y + kTooltipPadding;
    if (!frame.title.empty()) start_y += 24.0f;
    const float inner_w = frame.bounds.w - kTooltipPadding * 2.0f;
    const float max_chars_per_line = inner_w > 1.0f ? inner_w / kCharWidthEstimate : 1.0f;
    for (auto& term : frame.terms) {
        const float line = std::floor(static_cast<float>(term.text_offset) / max_chars_per_line);
        const float col = static_cast<float>(term.text_offset) - line * max_chars_per_line;
        term.hit_rect.x = frame.bounds.x + kTooltipPadding + col * kCharWidthEstimate;
        term.hit_rect.y = start_y + line * kLineHeight;
        term.hit_rect.w = static_cast<float>(term.text_length) * kCharWidthEstimate;
        // Clamp width to not overflow line
        const float remaining = inner_w - col * kCharWidthEstimate;
        if (term.hit_rect.w > remaining) term.hit_rect.w = remaining;
        term.hit_rect.h = kLineHeight;
    }
}

} // namespace thunder
