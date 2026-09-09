#include "thunder/presentation/ui/UiTheme.hpp"

#include <cmath>
#include <cstdio>

namespace thunder {

const UiTheme& UiTheme::victorian() noexcept {
    static const UiTheme instance{};
    return instance;
}

namespace {

[[nodiscard]] std::uint32_t channel(std::uint32_t rgba, int shift) noexcept {
    return (rgba >> shift) & 0xffu;
}

[[nodiscard]] std::uint8_t lerp_channel(std::uint32_t a, std::uint32_t b, int shift, float t) noexcept {
    const float mixed = static_cast<float>(channel(a, shift)) * (1.0f - t) +
                        static_cast<float>(channel(b, shift)) * t;
    return static_cast<std::uint8_t>(std::lround(mixed));
}

} // namespace

std::uint32_t ui_blend(std::uint32_t base, std::uint32_t overlay, float t) noexcept {
    if (!std::isfinite(t)) t = 0.0f;
    if (t <= 0.0f) return base;
    if (t >= 1.0f) return overlay;
    const std::uint32_t a = lerp_channel(base, overlay, 24, t);
    const std::uint32_t r = lerp_channel(base, overlay, 16, t);
    const std::uint32_t g = lerp_channel(base, overlay, 8, t);
    const std::uint32_t b = lerp_channel(base, overlay, 0, t);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

std::uint32_t ui_apply_overlay(std::uint32_t base, std::uint32_t overlay) noexcept {
    const float t = static_cast<float>(channel(overlay, 24)) / 255.0f;
    return ui_blend(base, overlay, t);
}

std::string ui_format_number(double value, int decimals, bool compact) {
    if (!std::isfinite(value)) return "—";
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;

    const char* suffix = "";
    double scaled = value;
    if (compact) {
        const double magnitude = std::fabs(value);
        if (magnitude >= 1.0e15) { scaled = value / 1.0e15; suffix = "Q"; decimals = 1; }
        else if (magnitude >= 1.0e12) { scaled = value / 1.0e12; suffix = "T"; decimals = 1; }
        else if (magnitude >= 1.0e9) { scaled = value / 1.0e9; suffix = "B"; decimals = 1; }
        else if (magnitude >= 1.0e6) { scaled = value / 1.0e6; suffix = "M"; decimals = 1; }
        else if (magnitude >= 1.0e4) { scaled = value / 1.0e3; suffix = "K"; decimals = 1; }
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, scaled);
    std::string digits = buffer;

    // Suppress negative zero like "-0" or "-0.0"
    if (!digits.empty() && digits[0] == '-') {
        bool only_zero = true;
        for (std::size_t i = 1; i < digits.size(); ++i) {
            if (digits[i] != '0' && digits[i] != '.') {
                only_zero = false;
                break;
            }
        }
        if (only_zero) {
            digits.erase(digits.begin());
        }
    }

    // Thousands separators on the integer part only.
    const auto dot = digits.find('.');
    const std::size_t int_end = dot == std::string::npos ? digits.size() : dot;
    std::size_t sign_len = (!digits.empty() && (digits[0] == '-' || digits[0] == '+')) ? 1u : 0u;
    if (int_end - sign_len > 3) {
        std::string grouped;
        grouped.reserve(digits.size() + 4);
        grouped.append(digits, 0, sign_len);
        const std::size_t int_len = int_end - sign_len;
        const std::size_t first_group = int_len % 3 == 0 ? 3 : int_len % 3;
        grouped.append(digits, sign_len, first_group);
        for (std::size_t i = sign_len + first_group; i < int_end; i += 3) {
            grouped.push_back(',');
            grouped.append(digits, i, 3);
        }
        if (dot != std::string::npos) grouped.append(digits, dot, std::string::npos);
        digits = std::move(grouped);
    }
    digits.append(suffix);
    return digits;
}

std::string ui_format_delta(double value, int decimals) {
    if (!std::isfinite(value)) return "—";
    std::string text = ui_format_number(std::fabs(value), decimals, false);
    if (value > 0.0) text.insert(text.begin(), '+');
    else if (value < 0.0) text.insert(text.begin(), '-');
    return text;
}

std::uint32_t ui_delta_color(const UiTheme& theme, double value) noexcept {
    if (!std::isfinite(value) || value == 0.0) return theme.colors.text_secondary;
    return value > 0.0 ? theme.colors.text_positive : theme.colors.text_negative;
}

std::string ui_format_currency(double value, std::string_view symbol, bool compact, int decimals, bool explicit_plus) {
    if (!std::isfinite(value)) return "—";
    const double abs_val = std::fabs(value);
    const bool is_zero = abs_val < 1e-12;
    const bool negative = !is_zero && (value < 0.0);
    const bool positive = !is_zero && (value > 0.0);

    int effective_decimals = 0;
    if (decimals >= 0) {
        effective_decimals = (compact && abs_val < 1.0e4) ? 0 : decimals;
    } else {
        effective_decimals = (compact && abs_val >= 1.0e4) ? 1 : 0;
    }

    std::string num = ui_format_number(abs_val, effective_decimals, compact);

    bool num_is_zero = true;
    for (char c : num) {
        if (c >= '1' && c <= '9') {
            num_is_zero = false;
            break;
        }
    }

    std::string result;
    if (!num_is_zero && positive && explicit_plus) {
        result.append("+");
    } else if (!num_is_zero && negative) {
        result.append("-");
    }
    result.append(symbol);
    result.append(num);
    return result;
}

float ui_estimate_text_width(std::string_view text, float font_size) noexcept {
    if (text.empty() || !std::isfinite(font_size) || font_size <= 0.0f) return 0.0f;
    float max_line_em = 0.0f;
    float cur_line_em = 0.0f;
    for (std::size_t index = 0; index < text.size();) {
        const auto c = static_cast<unsigned char>(text[index]);
        if (c == '\n') {
            max_line_em = std::max(max_line_em, cur_line_em);
            cur_line_em = 0.0f;
            ++index;
            continue;
        }
        if (c < 0x80u) {
            if (c == ' ') cur_line_em += 0.31f;
            else if (c >= '0' && c <= '9') cur_line_em += 0.53f;
            else if (c >= 'A' && c <= 'Z') cur_line_em += 0.61f;
            else if (c >= 'a' && c <= 'z') cur_line_em += 0.48f;
            else cur_line_em += 0.34f;
            ++index;
            continue;
        }
        cur_line_em += 0.96f; // Multibyte CJK / foreign glyphs
        if ((c & 0xe0u) == 0xc0u) index += 2u;
        else if ((c & 0xf0u) == 0xe0u) index += 3u;
        else if ((c & 0xf8u) == 0xf0u) index += 4u;
        else ++index;
        index = std::min(index, text.size());
    }
    max_line_em = std::max(max_line_em, cur_line_em);
    return max_line_em * font_size;
}

} // namespace thunder
