#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace thunder {

// -----------------------------------------------------------------------------
// Synchronization and Scheduling Architecture Notice:
// RenderGraph is a standalone DAG topological analyzer and hazard compiler
// designed for static plan verification and future multi-queue frame
// orchestration.
//
// In the current runtime, VulkanFrame (vulkan/VulkanFrame.cpp) is the single
// authoritative source of truth for runtime frame scheduling, barrier insertion
// (VkImageMemoryBarrier2), and command buffer recording. RenderGraph compiled
// plans are not consumed directly by VulkanFrame at runtime.
// -----------------------------------------------------------------------------

enum class RenderQueue : std::uint8_t { Graphics, Compute, Transfer };
enum class RenderResourceKind : std::uint8_t { Buffer, Image };
enum class RenderUsage : std::uint8_t {
    Undefined,
    Vertex,
    Index,
    Indirect,
    Uniform,
    StorageRead,
    StorageWrite,
    Sampled,
    ColorAttachment,
    DepthAttachment,
    TransferSrc,
    TransferDst,
    Present
};

struct RenderResourceHandle {
    std::uint32_t value = 0xffffffffu;
    friend bool operator==(RenderResourceHandle, RenderResourceHandle) = default;
};

struct RenderPassHandle {
    std::uint32_t value = 0xffffffffu;
    friend bool operator==(RenderPassHandle, RenderPassHandle) = default;
};

struct RenderAccess {
    RenderResourceHandle resource;
    RenderUsage usage = RenderUsage::Undefined;
    bool write = false;
};

struct RenderBarrier {
    RenderResourceHandle resource;
    RenderPassHandle before;
    RenderPassHandle after;
    RenderUsage from = RenderUsage::Undefined;
    RenderUsage to = RenderUsage::Undefined;
    bool cross_queue = false;
};

struct RenderPassDesc {
    std::string name;
    RenderQueue queue = RenderQueue::Graphics;
    std::vector<RenderAccess> accesses;
};

class RenderGraph {
public:
    RenderResourceHandle add_resource(std::string name, RenderResourceKind kind);
    RenderPassHandle add_pass(RenderPassDesc pass);
    void clear();
    void compile();

    [[nodiscard]] std::span<const RenderPassHandle> order() const noexcept { return order_; }
    [[nodiscard]] std::span<const std::vector<RenderPassHandle>> batches() const noexcept { return batches_; }
    [[nodiscard]] std::span<const RenderBarrier> barriers() const noexcept { return barriers_; }
    [[nodiscard]] std::string_view pass_name(RenderPassHandle pass) const;
    [[nodiscard]] std::string_view resource_name(RenderResourceHandle resource) const;

private:
    struct Resource { std::string name; RenderResourceKind kind; };

    [[nodiscard]] std::size_t pass_index(RenderPassHandle pass) const;
    [[nodiscard]] std::size_t resource_index(RenderResourceHandle resource) const;

    std::vector<Resource> resources_;
    std::vector<RenderPassDesc> passes_;
    std::vector<RenderPassHandle> order_;
    std::vector<std::vector<RenderPassHandle>> batches_;
    std::vector<RenderBarrier> barriers_;
};

} // namespace thunder
