#include "thunder/simulation/world/ProvinceAdjacencyGraph.hpp"

#include <algorithm>
#include <stdexcept>

namespace thunder {

void ProvinceAdjacencyGraph::build(std::uint32_t province_count, std::span<const ProvinceAdjacencyInput> edges) {
    struct Directed {
        std::uint64_t key; // (from << 32) | neighbor.province
        std::uint16_t flags;
        std::uint16_t base_cost_q8;
    };

    std::vector<Directed> directed;
    directed.reserve(edges.size() * 2u);
    for (const auto& edge : edges) {
        if (!edge.a.valid() || !edge.b.valid() || edge.a == edge.b ||
            edge.a.value() >= province_count || edge.b.value() >= province_count) continue;
        const auto a = static_cast<std::uint64_t>(edge.a.value());
        const auto b = static_cast<std::uint64_t>(edge.b.value());
        directed.push_back({(a << 32u) | b, edge.flags, edge.base_cost_q8});
        directed.push_back({(b << 32u) | a, edge.flags, edge.base_cost_q8});
    }

    std::sort(directed.begin(), directed.end(), [](const Directed& lhs, const Directed& rhs) {
        if (lhs.key != rhs.key) return lhs.key < rhs.key;
        return lhs.flags < rhs.flags;
    });

    // Deduplicate duplicate GIS edges. Prefer the lowest movement cost and OR flags.
    std::size_t write = 0;
    for (std::size_t read = 0; read < directed.size(); ++read) {
        if (write > 0u && directed[write - 1u].key == directed[read].key) {
            auto& dst = directed[write - 1u];
            dst.flags = static_cast<std::uint16_t>(dst.flags | directed[read].flags);
            dst.base_cost_q8 = std::min(dst.base_cost_q8, directed[read].base_cost_q8);
        } else {
            directed[write++] = directed[read];
        }
    }
    directed.resize(write);

    offsets_.assign(static_cast<std::size_t>(province_count) + 1u, 0u);
    for (const auto& item : directed) {
        const auto from = static_cast<std::size_t>(item.key >> 32u);
        ++offsets_[from + 1u];
    }
    for (std::size_t i = 1; i < offsets_.size(); ++i) offsets_[i] += offsets_[i - 1u];

    neighbors_.resize(directed.size());
    for (std::size_t i = 0; i < directed.size(); ++i) {
        neighbors_[i] = {static_cast<std::uint32_t>(directed[i].key & 0xffffffffu),
                         directed[i].flags, directed[i].base_cost_q8};
    }
}


void ProvinceAdjacencyGraph::load_csr(std::span<const std::uint32_t> offsets, std::span<const ProvinceNeighbor> neighbors) {
    if (offsets.empty() || offsets.front() != 0u || offsets.back() != neighbors.size())
        throw std::invalid_argument("invalid province adjacency CSR offsets");
    for (std::size_t i = 1; i < offsets.size(); ++i)
        if (offsets[i] < offsets[i - 1u]) throw std::invalid_argument("province adjacency offsets not monotonic");
    const auto province_count_value = static_cast<std::uint32_t>(offsets.size() - 1u);
    for (std::size_t p = 0; p < province_count_value; ++p) {
        std::uint32_t previous = 0u;
        bool first = true;
        for (std::uint32_t i = offsets[p]; i < offsets[p + 1u]; ++i) {
            const auto& n = neighbors[i];
            if (n.province >= province_count_value || n.province == p)
                throw std::invalid_argument("invalid province adjacency neighbor reference");
            if (!first && n.province <= previous)
                throw std::invalid_argument("province adjacency row must be strictly sorted");
            previous = n.province;
            first = false;
        }
    }
    offsets_.assign(offsets.begin(), offsets.end());
    neighbors_.assign(neighbors.begin(), neighbors.end());
}

std::span<const ProvinceNeighbor> ProvinceAdjacencyGraph::neighbors(ProvinceId province) const noexcept {
    const auto val = province.value();
    if (!province.valid() || static_cast<std::size_t>(val) + 1u >= offsets_.size()) return {};
    const auto* offsets = offsets_.data();
    const auto begin = offsets[val];
    const auto end = offsets[static_cast<std::size_t>(val) + 1u];
    return std::span<const ProvinceNeighbor>{neighbors_.data() + begin, static_cast<std::size_t>(end - begin)};
}

bool ProvinceAdjacencyGraph::adjacent(ProvinceId a, ProvinceId b) const noexcept {
    const auto row = neighbors(a);
    const auto target = b.value();
    if (row.size() <= 16u) {
        for (const auto& n : row) {
            if (n.province == target) return true;
            if (n.province > target) return false;
        }
        return false;
    }
    const auto it = std::lower_bound(row.begin(), row.end(), target, [](const ProvinceNeighbor& n, std::uint32_t value) {
        return n.province < value;
    });
    return it != row.end() && it->province == target;
}

bool ProvinceAdjacencyGraph::is_symmetric() const noexcept {
    for (std::uint32_t from = 0; from < province_count(); ++from) {
        for (const auto& edge : neighbors(ProvinceId{from})) {
            const auto reverse = neighbors(ProvinceId{edge.province});
            const auto it = std::lower_bound(reverse.begin(), reverse.end(), from,
                                             [](const ProvinceNeighbor& n, std::uint32_t value) {
                                                 return n.province < value;
                                             });
            if (it == reverse.end() || it->province != from ||
                it->flags != edge.flags || it->base_cost_q8 != edge.base_cost_q8) return false;
        }
    }
    return true;
}

} // namespace thunder
