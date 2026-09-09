#pragma once
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace thunder {

// Per-chunk partials are indexed by deterministic chunk ID, never worker ID.
// Dynamic scheduling may change which CPU executes a chunk, but final fold order
// stays identical across worker counts and runs.
template <typename T>
class DeterministicReduction {
public:
    void resize(std::size_t chunks, const T& initial = T{}) {
        partials_.assign(chunks, initial);
    }

    [[nodiscard]] T& partial(std::size_t chunk_index) noexcept { return partials_[chunk_index]; }
    [[nodiscard]] const T& partial(std::size_t chunk_index) const noexcept { return partials_[chunk_index]; }
    [[nodiscard]] std::span<const T> partials() const noexcept { return partials_; }
    [[nodiscard]] std::size_t size() const noexcept { return partials_.size(); }

    template <typename Combine>
    [[nodiscard]] T fold(T initial, Combine&& combine) const {
        for (const auto& value : partials_) initial = std::forward<Combine>(combine)(std::move(initial), value);
        return initial;
    }

private:
    std::vector<T> partials_;
};

} // namespace thunder
