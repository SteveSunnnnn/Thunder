#pragma once

// Shared UTF-8 decoder for presentation text consumers (FontAtlas, UI staging).
// Single source of truth: both call sites previously carried byte-identical
// copies of this function.
//
// Returns the codepoint at `index` and advances it past the consumed bytes.
// Malformed, truncated or out-of-range sequences yield U+FFFD and advance by
// exactly one byte, so every caller makes forward progress.
#include <cstdint>
#include <string_view>

namespace thunder {

[[nodiscard]] inline std::uint32_t decode_utf8(std::string_view text, std::size_t& index) noexcept {
    if (index >= text.size()) return 0xfffd;
    const auto byte = static_cast<unsigned char>(text[index]);
    if (byte < 0x80u) {
        ++index;
        return byte;
    }
    auto continuation = [&](std::size_t offset) {
        return index + offset < text.size() &&
               (static_cast<unsigned char>(text[index + offset]) & 0xc0u) == 0x80u;
    };
    if ((byte & 0xe0u) == 0xc0u && continuation(1)) {
        const auto b1 = static_cast<unsigned char>(text[index + 1u]);
        const auto cp = (static_cast<std::uint32_t>(byte & 0x1fu) << 6u) |
                        static_cast<std::uint32_t>(b1 & 0x3fu);
        index += 2u;
        return cp >= 0x80u ? cp : 0xfffd;
    }
    if ((byte & 0xf0u) == 0xe0u && continuation(1) && continuation(2)) {
        const auto b1 = static_cast<unsigned char>(text[index + 1u]);
        const auto b2 = static_cast<unsigned char>(text[index + 2u]);
        const auto cp = (static_cast<std::uint32_t>(byte & 0x0fu) << 12u) |
                        (static_cast<std::uint32_t>(b1 & 0x3fu) << 6u) |
                        static_cast<std::uint32_t>(b2 & 0x3fu);
        index += 3u;
        return cp >= 0x800u && !(cp >= 0xd800u && cp <= 0xdfffu) ? cp : 0xfffd;
    }
    if ((byte & 0xf8u) == 0xf0u && continuation(1) && continuation(2) && continuation(3)) {
        const auto b1 = static_cast<unsigned char>(text[index + 1u]);
        const auto b2 = static_cast<unsigned char>(text[index + 2u]);
        const auto b3 = static_cast<unsigned char>(text[index + 3u]);
        const auto cp = (static_cast<std::uint32_t>(byte & 0x07u) << 18u) |
                        (static_cast<std::uint32_t>(b1 & 0x3fu) << 12u) |
                        (static_cast<std::uint32_t>(b2 & 0x3fu) << 6u) |
                        static_cast<std::uint32_t>(b3 & 0x3fu);
        index += 4u;
        return cp >= 0x10000u && cp <= 0x10ffffu ? cp : 0xfffd;
    }
    ++index;
    return 0xfffd;
}

} // namespace thunder
