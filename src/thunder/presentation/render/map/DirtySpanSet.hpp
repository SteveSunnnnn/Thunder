#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

struct DirtySpan {
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return first + count; }
    friend constexpr bool operator==(DirtySpan, DirtySpan) = default;
};

// Tracks sparse dirty ranges without collapsing distant edits into one giant upload.
// mark() is allocation-free after reserve(). normalize() is intended once per frame.
class DirtySpanSet {
public:
    explicit DirtySpanSet(std::size_t reserve_spans = 32) { spans_.reserve(reserve_spans); }

    void clear() noexcept { spans_.clear(); normalized_ = true; }

    void mark(std::uint32_t index) { mark(index, 1u); }

    void mark(std::uint32_t first, std::uint32_t count) {
        if (count == 0u) return;
        spans_.push_back({first, count});
        normalized_ = false;
    }

    void normalize(std::uint32_t merge_gap = 0u) {
        if (normalized_ || spans_.size() < 2u) {
            normalized_ = true;
            return;
        }
        std::sort(spans_.begin(), spans_.end(), [](DirtySpan a, DirtySpan b) {
            return a.first < b.first || (a.first == b.first && a.count < b.count);
        });

        std::size_t write = 0;
        for (std::size_t read = 1; read < spans_.size(); ++read) {
            auto& current = spans_[write];
            const auto next = spans_[read];
            const std::uint64_t current_end = static_cast<std::uint64_t>(current.end());
            const std::uint64_t merge_limit = current_end + merge_gap;
            if (static_cast<std::uint64_t>(next.first) <= merge_limit) {
                const auto merged_end = std::max<std::uint64_t>(current_end, next.end());
                current.count = static_cast<std::uint32_t>(merged_end - current.first);
            } else {
                ++write;
                spans_[write] = next;
            }
        }
        spans_.resize(write + 1u);
        normalized_ = true;
    }

    void normalize_or_full(std::uint32_t total_elements, std::uint32_t merge_gap = 0u,
                           std::uint32_t dense_divisor = 32u, std::uint32_t minimum_marks = 64u) {
        if (total_elements == 0u) { clear(); return; }
        const auto dense_threshold = std::max<std::uint32_t>(minimum_marks, total_elements / std::max(1u, dense_divisor));
        if (spans_.size() >= dense_threshold) {
            spans_.clear();
            spans_.push_back({0u, total_elements});
            normalized_ = true;
            return;
        }
        normalize(merge_gap);
    }

    [[nodiscard]] std::size_t pending_span_count() const noexcept { return spans_.size(); }
    [[nodiscard]] std::span<const DirtySpan> spans() const noexcept { return spans_; }
    [[nodiscard]] bool empty() const noexcept { return spans_.empty(); }

    [[nodiscard]] std::uint64_t element_count() const noexcept {
        std::uint64_t total = 0;
        for (const auto span : spans_) total += span.count;
        return total;
    }

private:
    std::vector<DirtySpan> spans_;
    bool normalized_ = true;
};

} // namespace thunder
