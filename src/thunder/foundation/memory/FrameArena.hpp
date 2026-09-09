#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace thunder {

// Per-frame linear allocator. Allocate during frame construction, reset only after
// the GPU timeline proves that frame is retired. No individual free and no heap
// churn in render submission hot paths.
class FrameArena {
public:
    explicit FrameArena(std::size_t capacity_bytes = 4u * 1024u * 1024u)
        : storage_(capacity_bytes) {}

    using Marker = std::size_t;

    void reset() noexcept { head_ = 0; }
    [[nodiscard]] Marker mark() const noexcept { return head_; }
    void rewind(Marker marker) noexcept { if (marker <= head_) head_ = marker; }

    [[nodiscard]] void* allocate_bytes(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        if (alignment == 0 || (alignment & (alignment - 1u)) != 0u) {
            throw std::invalid_argument("FrameArena alignment must be a power of two");
        }
        const auto aligned = (head_ + alignment - 1u) & ~(alignment - 1u);
        if (aligned > storage_.size() || size > storage_.size() - aligned) {
            throw std::bad_alloc{};
        }
        head_ = aligned + size;
        peak_ = head_ > peak_ ? head_ : peak_;
        return storage_.data() + aligned;
    }

    template <typename T>
        requires std::is_trivially_destructible_v<T>
    [[nodiscard]] std::span<T> allocate(std::size_t count) {
        if (count > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_alloc{};
        auto* ptr = static_cast<T*>(allocate_bytes(count * sizeof(T), alignof(T)));
        return {ptr, count};
    }

    [[nodiscard]] std::size_t used() const noexcept { return head_; }
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }

private:
    std::vector<std::byte> storage_;
    std::size_t head_ = 0;
    std::size_t peak_ = 0;
};

} // namespace thunder
