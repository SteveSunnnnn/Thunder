#pragma once
#include "thunder/foundation/memory/FrameArena.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace thunder {

// CPU-side model of frames-in-flight. Vulkan backend will associate each slot
// with command pools/buffers, transient descriptors and one timeline value.
template <std::size_t FramesInFlight = 3>
class FrameRing {
    static_assert(FramesInFlight >= 2 && FramesInFlight <= 4);
public:
    struct FrameContext {
        FrameArena arena;
        std::uint64_t frame_number = 0;
        std::uint64_t retire_timeline = 0;
    };

    explicit FrameRing(std::size_t arena_bytes_per_frame = 4u * 1024u * 1024u)
        : frames_(make_frames(arena_bytes_per_frame)) {}

    // Non-blocking. If the chosen slot is still owned by the GPU, caller can
    // wait on the timeline semaphore or skip rendering instead of stalling blindly.
    [[nodiscard]] FrameContext* try_begin(std::uint64_t completed_timeline) noexcept {
        auto& frame = frames_[cursor_];
        if (frame.retire_timeline > completed_timeline) return nullptr;
        frame.arena.reset();
        frame.frame_number = next_frame_number_++;
        return &frame;
    }

    void submit(FrameContext& frame, std::uint64_t retire_timeline) noexcept {
        frame.retire_timeline = retire_timeline;
        cursor_ = (cursor_ + 1u) % FramesInFlight;
    }

    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

private:
    static std::array<FrameContext, FramesInFlight> make_frames(std::size_t arena_bytes) {
        if constexpr (FramesInFlight == 2) {
            return {FrameContext{FrameArena{arena_bytes}}, FrameContext{FrameArena{arena_bytes}}};
        } else if constexpr (FramesInFlight == 3) {
            return {FrameContext{FrameArena{arena_bytes}}, FrameContext{FrameArena{arena_bytes}}, FrameContext{FrameArena{arena_bytes}}};
        } else {
            return {FrameContext{FrameArena{arena_bytes}}, FrameContext{FrameArena{arena_bytes}}, FrameContext{FrameArena{arena_bytes}}, FrameContext{FrameArena{arena_bytes}}};
        }
    }

    std::array<FrameContext, FramesInFlight> frames_;
    std::size_t cursor_ = 0;
    std::uint64_t next_frame_number_ = 0;
};

} // namespace thunder
