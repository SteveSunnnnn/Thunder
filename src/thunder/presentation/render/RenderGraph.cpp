#include "thunder/presentation/render/RenderGraph.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>

namespace thunder {

namespace {
constexpr std::uint32_t invalid = std::numeric_limits<std::uint32_t>::max();

void add_edge(std::vector<std::vector<std::uint32_t>>& edges,
              std::vector<std::uint32_t>& indegree,
              std::uint32_t from, std::uint32_t to) {
    if (from == invalid || from == to) return;
    auto& row = edges[from];
    if (std::find(row.begin(), row.end(), to) != row.end()) return;
    row.push_back(to);
    ++indegree[to];
}
} // namespace

RenderResourceHandle RenderGraph::add_resource(std::string name, RenderResourceKind kind) {
    const auto id = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back({std::move(name), kind});
    return {id};
}

RenderPassHandle RenderGraph::add_pass(RenderPassDesc pass) {
    if (pass.name.empty()) throw std::invalid_argument("render pass requires a name");
    const auto id = static_cast<std::uint32_t>(passes_.size());
    passes_.push_back(std::move(pass));
    return {id};
}

void RenderGraph::clear() {
    resources_.clear();
    passes_.clear();
    order_.clear();
    batches_.clear();
    barriers_.clear();
}

std::size_t RenderGraph::pass_index(RenderPassHandle pass) const {
    if (pass.value >= passes_.size()) throw std::out_of_range("invalid RenderPassHandle");
    return pass.value;
}

std::size_t RenderGraph::resource_index(RenderResourceHandle resource) const {
    if (resource.value >= resources_.size()) throw std::out_of_range("invalid RenderResourceHandle");
    return resource.value;
}

std::string_view RenderGraph::pass_name(RenderPassHandle pass) const { return passes_[pass_index(pass)].name; }
std::string_view RenderGraph::resource_name(RenderResourceHandle resource) const { return resources_[resource_index(resource)].name; }

void RenderGraph::compile() {
    order_.clear();
    batches_.clear();
    barriers_.clear();

    const auto pass_count = static_cast<std::uint32_t>(passes_.size());
    const auto resource_count = static_cast<std::uint32_t>(resources_.size());
    std::vector<std::vector<std::uint32_t>> edges(pass_count);
    std::vector<std::uint32_t> indegree(pass_count, 0);

    struct AccessRecord {
        std::uint32_t pass = invalid;
        RenderUsage usage = RenderUsage::Undefined;
        bool write = false;
        RenderQueue queue = RenderQueue::Graphics;
    };
    struct ResourceState {
        AccessRecord writer{};
        std::vector<AccessRecord> readers;
    };
    std::vector<ResourceState> state(resource_count);

    auto add_dependency = [&](std::uint32_t resource, const AccessRecord& before,
                              std::uint32_t after_pass, const RenderAccess& after,
                              RenderQueue after_queue) {
        if (before.pass == invalid || before.pass == after_pass) return;
        add_edge(edges, indegree, before.pass, after_pass);
        barriers_.push_back({
            RenderResourceHandle{resource},
            RenderPassHandle{before.pass},
            RenderPassHandle{after_pass},
            before.usage,
            after.usage,
            before.queue != after_queue
        });
    };

    // Build hazards and transitions in declaration order. Multiple same-queue
    // readers of the same usage may remain parallel. A write synchronizes with
    // every prior reader, not merely the last one. Queue ownership or image
    // usage transitions also serialize read/read accesses conservatively.
    for (std::uint32_t pass_index_value = 0; pass_index_value < pass_count; ++pass_index_value) {
        const auto& pass = passes_[pass_index_value];

        std::vector<std::uint32_t> seen;
        seen.reserve(pass.accesses.size());
        for (const auto& access : pass.accesses) {
            const auto resource = static_cast<std::uint32_t>(resource_index(access.resource));
            if (std::find(seen.begin(), seen.end(), resource) != seen.end())
                throw std::invalid_argument("render pass contains duplicate access to one resource");
            seen.push_back(resource);

            auto& s = state[resource];
            if (access.write) {
                add_dependency(resource, s.writer, pass_index_value, access, pass.queue);
                for (const auto& reader : s.readers)
                    add_dependency(resource, reader, pass_index_value, access, pass.queue);
                s.readers.clear();
                s.writer = {pass_index_value, access.usage, true, pass.queue};
                continue;
            }

            add_dependency(resource, s.writer, pass_index_value, access, pass.queue);

            const bool image = resources_[resource].kind == RenderResourceKind::Image;
            bool transition_from_readers = false;
            for (const auto& reader : s.readers) {
                if (reader.queue != pass.queue || (image && reader.usage != access.usage)) {
                    transition_from_readers = true;
                    break;
                }
            }
            if (transition_from_readers) {
                for (const auto& reader : s.readers)
                    add_dependency(resource, reader, pass_index_value, access, pass.queue);
                s.readers.clear();
            }
            s.readers.push_back({pass_index_value, access.usage, false, pass.queue});
        }
    }

    // Stable Kahn waves. Each wave contains passes with no dependency between
    // them and is suitable for parallel command recording.
    std::vector<std::uint32_t> ready;
    ready.reserve(pass_count);
    for (std::uint32_t p = 0; p < pass_count; ++p)
        if (indegree[p] == 0) ready.push_back(p);

    while (!ready.empty()) {
        std::sort(ready.begin(), ready.end());
        std::vector<RenderPassHandle> batch;
        batch.reserve(ready.size());
        std::vector<std::uint32_t> next;
        for (const auto p : ready) {
            const RenderPassHandle handle{p};
            order_.push_back(handle);
            batch.push_back(handle);
            for (const auto n : edges[p])
                if (--indegree[n] == 0) next.push_back(n);
        }
        batches_.push_back(std::move(batch));
        ready = std::move(next);
    }

    if (order_.size() != passes_.size())
        throw std::runtime_error("cycle detected in render graph");

    std::sort(barriers_.begin(), barriers_.end(), [](const RenderBarrier& a, const RenderBarrier& b) {
        if (a.after.value != b.after.value) return a.after.value < b.after.value;
        if (a.resource.value != b.resource.value) return a.resource.value < b.resource.value;
        return a.before.value < b.before.value;
    });
}

} // namespace thunder
