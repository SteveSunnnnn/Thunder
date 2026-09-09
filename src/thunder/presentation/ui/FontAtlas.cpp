#include "thunder/presentation/ui/FontAtlas.hpp"
#include "thunder/presentation/ui/Utf8Decode.hpp"

#include "thunder/foundation/base/Hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace thunder {
namespace {
constexpr std::array<char, 8> magic{{'T','H','U','N','D','R','F','1'}};
constexpr std::uint32_t version = 1;
constexpr std::uint32_t record_bytes = 40;

template <class T>
void put(std::ostream& output, T value) {
    std::uint64_t bits = 0;
    if constexpr (std::is_same_v<T, float>) {
        bits = std::bit_cast<std::uint32_t>(value);
    } else {
        static_assert(std::is_integral_v<T>);
        bits = static_cast<std::uint64_t>(value);
    }
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        output.put(static_cast<char>((bits >> (index * 8u)) & 0xffu));
    }
    if (!output) throw std::runtime_error("font write failed");
}

template <class T>
T get(std::istream& input) {
    std::uint64_t bits = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        const int value = input.get();
        if (value < 0) throw std::runtime_error("truncated font atlas");
        bits |= static_cast<std::uint64_t>(static_cast<unsigned char>(value)) << (index * 8u);
    }
    if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    } else {
        static_assert(std::is_integral_v<T>);
        return static_cast<T>(bits);
    }
}


// decode_utf8 moved to thunder/presentation/ui/Utf8Decode.hpp (shared with UI staging).

} // namespace

void FontAtlas::set_metrics(std::uint32_t width, std::uint32_t height, float px_range, std::uint64_t texture_hash) {
    if (width == 0 || height == 0 || !std::isfinite(px_range) || px_range <= 0.0f || px_range > 64.0f) {
        throw std::invalid_argument("invalid font atlas metrics");
    }
    width_ = width;
    height_ = height;
    px_range_ = px_range;
    texture_hash_ = texture_hash;
}

void FontAtlas::set_glyphs(std::vector<FontGlyph> glyphs) {
    if (width_ == 0 || height_ == 0) {
        throw std::logic_error("font metrics must be set before glyphs");
    }
    std::sort(glyphs.begin(), glyphs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.codepoint < rhs.codepoint;
    });
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        const auto& glyph = glyphs[index];
        if (glyph.codepoint > 0x10ffffu ||
            (index != 0 && glyphs[index - 1].codepoint == glyph.codepoint) ||
            !std::isfinite(glyph.advance) || glyph.advance < 0.0f ||
            !std::isfinite(glyph.plane_left) || !std::isfinite(glyph.plane_bottom) ||
            !std::isfinite(glyph.plane_right) || !std::isfinite(glyph.plane_top) ||
            !std::isfinite(glyph.atlas_left) || !std::isfinite(glyph.atlas_bottom) ||
            !std::isfinite(glyph.atlas_right) || !std::isfinite(glyph.atlas_top) ||
            glyph.atlas_left < 0.0f || glyph.atlas_bottom < 0.0f ||
            glyph.atlas_right > static_cast<float>(width_) || glyph.atlas_top > static_cast<float>(height_) ||
            glyph.atlas_left > glyph.atlas_right || glyph.atlas_bottom > glyph.atlas_top) {
            throw std::invalid_argument("invalid font glyph");
        }
    }
    glyphs_ = std::move(glyphs);
}

const FontGlyph* FontAtlas::find(std::uint32_t codepoint) const noexcept {
    const auto it = std::lower_bound(glyphs_.begin(), glyphs_.end(), codepoint,
                                     [](const FontGlyph& glyph, std::uint32_t cp) {
                                         return glyph.codepoint < cp;
                                     });
    if (it != glyphs_.end() && it->codepoint == codepoint) return &*it;
    if (fallback_ != nullptr && fallback_ != this) return fallback_->find(codepoint);
    return nullptr;
}

const FontGlyph* FontAtlas::find_with_source(std::uint32_t codepoint, const FontAtlas*& source) const noexcept {
    const auto it = std::lower_bound(glyphs_.begin(), glyphs_.end(), codepoint,
                                     [](const FontGlyph& glyph, std::uint32_t cp) {
                                         return glyph.codepoint < cp;
                                     });
    if (it != glyphs_.end() && it->codepoint == codepoint) {
        source = this;
        return &*it;
    }
    if (fallback_ != nullptr && fallback_ != this) return fallback_->find_with_source(codepoint, source);
    source = nullptr;
    return nullptr;
}

std::uint64_t FontAtlas::checksum() const noexcept {
    Fnv1a64 hash;
    hash.add(width_);
    hash.add(height_);
    hash.add(px_range_);
    hash.add(texture_hash_);
    hash.add(glyphs_.size());
    for (const auto& glyph : glyphs_) {
        hash.add(glyph.codepoint);
        hash.add(glyph.advance);
        hash.add(glyph.plane_left);
        hash.add(glyph.plane_bottom);
        hash.add(glyph.plane_right);
        hash.add(glyph.plane_top);
        hash.add(glyph.atlas_left);
        hash.add(glyph.atlas_bottom);
        hash.add(glyph.atlas_right);
        hash.add(glyph.atlas_top);
    }
    return hash.value();
}

void FontAtlas::append_text(UiDrawList& draw_list,
                            std::string_view utf8,
                            float x,
                            float baseline_y,
                            float size,
                            std::uint32_t rgba,
                            UiRect scissor) const {
    if (size <= 0.0f || !std::isfinite(size)) return;
    float pen = x;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto codepoint = decode_utf8(utf8, index);
        const FontAtlas* source = this;
        const FontGlyph* glyph = find_with_source(codepoint, source);
        if (glyph == nullptr) glyph = find_with_source(0xfffdu, source);
        if (glyph == nullptr) glyph = find_with_source(static_cast<std::uint32_t>('?'), source);
        if (glyph == nullptr || source == nullptr || source->width_ == 0 || source->height_ == 0) continue;

        const UiRect rect{
            pen + glyph->plane_left * size,
            baseline_y - glyph->plane_top * size,
            (glyph->plane_right - glyph->plane_left) * size,
            (glyph->plane_top - glyph->plane_bottom) * size};
        const float u0 = glyph->atlas_left / static_cast<float>(source->width_);
        const float u1 = glyph->atlas_right / static_cast<float>(source->width_);
        const float v0 = 1.0f - glyph->atlas_top / static_cast<float>(source->height_);
        const float v1 = 1.0f - glyph->atlas_bottom / static_cast<float>(source->height_);
        if (rect.w > 0.0f && rect.h > 0.0f) {
            draw_list.quad_uv(rect, u0, v0, u1, v1, rgba, scissor, source->texture_hash_, UiBatchKind::MsdfText);
        }
        pen += glyph->advance * size;
    }
}

void FontAtlas::write(const std::filesystem::path& path) const {
    if (width_ == 0 || height_ == 0 || glyphs_.size() > 2'000'000u) {
        throw std::runtime_error("invalid font atlas state");
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create font atlas");
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    put(output, version);
    put(output, static_cast<std::uint32_t>(glyphs_.size()));
    put(output, width_);
    put(output, height_);
    put(output, px_range_);
    put(output, texture_hash_);
    put(output, checksum());
    put(output, record_bytes);
    for (const auto& glyph : glyphs_) {
        put(output, glyph.codepoint);
        put(output, glyph.advance);
        put(output, glyph.plane_left);
        put(output, glyph.plane_bottom);
        put(output, glyph.plane_right);
        put(output, glyph.plane_top);
        put(output, glyph.atlas_left);
        put(output, glyph.atlas_bottom);
        put(output, glyph.atlas_right);
        put(output, glyph.atlas_top);
    }
}

FontAtlas FontAtlas::read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open font atlas");
    std::array<char, 8> got{};
    input.read(got.data(), static_cast<std::streamsize>(got.size()));
    if (got != magic) throw std::runtime_error("invalid font atlas magic");
    if (get<std::uint32_t>(input) != version) throw std::runtime_error("unsupported font atlas version");
    const auto glyph_count = get<std::uint32_t>(input);
    if (glyph_count > 2'000'000u) throw std::runtime_error("font glyph count exceeds cap");
    const auto width = get<std::uint32_t>(input);
    const auto height = get<std::uint32_t>(input);
    const auto px_range = get<float>(input);
    const auto texture_hash = get<std::uint64_t>(input);
    const auto expected = get<std::uint64_t>(input);
    if (get<std::uint32_t>(input) != record_bytes) throw std::runtime_error("font record size mismatch");
    FontAtlas atlas;
    atlas.set_metrics(width, height, px_range, texture_hash);
    std::vector<FontGlyph> glyphs;
    glyphs.reserve(glyph_count);
    for (std::uint32_t n = 0; n < glyph_count; ++n) {
        FontGlyph glyph;
        glyph.codepoint = get<std::uint32_t>(input);
        glyph.advance = get<float>(input);
        glyph.plane_left = get<float>(input);
        glyph.plane_bottom = get<float>(input);
        glyph.plane_right = get<float>(input);
        glyph.plane_top = get<float>(input);
        glyph.atlas_left = get<float>(input);
        glyph.atlas_bottom = get<float>(input);
        glyph.atlas_right = get<float>(input);
        glyph.atlas_top = get<float>(input);
        glyphs.push_back(glyph);
    }
    char extra = 0;
    if (input.get(extra)) throw std::runtime_error("trailing font atlas bytes");
    atlas.set_glyphs(std::move(glyphs));
    if (atlas.checksum() != expected) throw std::runtime_error("font atlas checksum mismatch");
    return atlas;
}

} // namespace thunder
