#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "thunder/presentation/ui/StrategyUi.hpp"

namespace thunder {

// A hoverable keyword term within tooltip body text
struct TooltipTerm {
    std::string key;           // lookup key for resolving child tooltip content
    std::string display_text;  // visible text in the tooltip body
    UiRect hit_rect{};         // computed screen-space hit region
    std::size_t text_offset = 0; // character offset in body string
    std::size_t text_length = 0; // character length of the term
};

// One level in the recursive tooltip stack
struct TooltipFrame {
    std::string title;                  // optional header title
    std::string body;                   // body text (may contain [term:KEY|Display] markers)
    std::string parsed_body;            // body with markers stripped (plain display text)
    std::vector<TooltipTerm> terms;     // extracted hoverable terms
    UiRect bounds{};                    // computed screen rect of this tooltip
    UiRect anchor{};                    // the rect this tooltip is anchored to
    UiRect screen{};                    // screen used for placement/clamping
    int depth = 0;                      // nesting level (0 = root)
    bool is_locked = false;             // pinned by hotkey
};

// Callback type for resolving a term key to tooltip title + body
using TooltipResolver = std::function<std::pair<std::string, std::string>(const std::string& key)>;

class TooltipStack {
public:
    static constexpr int kMaxDepth = 8;
    static constexpr float kHoverDelaySeconds = 0.3f;
    static constexpr float kTooltipPadding = 8.0f;
    static constexpr float kCharWidthEstimate = 7.5f;
    static constexpr float kLineHeight = 16.0f;
    static constexpr float kMaxTooltipWidth = 360.0f;
    static constexpr float kMinTooltipWidth = 120.0f;

    TooltipStack() = default;

    // Set the resolver callback for looking up term definitions
    void set_resolver(TooltipResolver resolver);

    // Push a new root tooltip (clears existing stack unless locked)
    void push_root(std::string title, std::string body, UiRect anchor, UiRect screen);

    // Push a child tooltip from a hovered term
    void push_child(const std::string& term_key, UiRect term_rect, UiRect screen);

    // Pop back to a specific depth (removes all deeper frames)
    void pop_to(int depth);

    // Clear entire stack
    void clear();

    // Lock/unlock the current topmost tooltip
    void lock_current();
    void unlock_all();

    // Input handling
    void on_mouse_move(float x, float y, float dt_seconds);
    bool on_mouse_click(float x, float y);  // returns true if click was consumed

    // Rendering
    void render(UiDrawList& ui, UiRect screen) const;

    // Queries
    [[nodiscard]] int depth() const noexcept { return static_cast<int>(frames_.size()); }
    [[nodiscard]] bool empty() const noexcept { return frames_.empty(); }
    [[nodiscard]] bool is_locked() const noexcept;
    [[nodiscard]] bool is_mouse_over_any(float x, float y) const noexcept;

private:
    // Parse [term:KEY|Display Text] markers from body text using zero-copy scanning
    static std::pair<std::string, std::vector<TooltipTerm>> parse_terms(std::string_view body);

    // Calculate tooltip dimensions from text content with robust screen clamping
    static UiRect calculate_bounds(std::string_view title, std::string_view parsed_body,
                                   UiRect anchor, UiRect screen, int depth);

    // Compute hit rects for terms within a rendered tooltip
    void recompute_term_hit_rects(TooltipFrame& frame) const;

    std::vector<TooltipFrame> frames_;
    TooltipResolver resolver_;

    // Hover tracking for child tooltip spawning
    float hover_timer_ = 0.0f;
    std::optional<std::string> pending_term_key_;
    UiRect pending_term_rect_{};
    int pending_term_depth_ = -1;
    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;
};

} // namespace thunder
