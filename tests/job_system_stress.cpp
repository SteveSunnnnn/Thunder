#include "thunder/foundation/jobs/DeterministicReduction.hpp"
#include "thunder/foundation/jobs/JobSystem.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace thunder;

int main() {
    constexpr std::size_t item_count = 32'768u;
    constexpr std::size_t grain = 512u;
    constexpr std::size_t chunks = (item_count + grain - 1u) / grain;

    JobSystem jobs{3u, 32u * 1024u};
    std::vector<std::uint64_t> values(item_count, 0u);
    DeterministicReduction<std::uint64_t> reduction;
    reduction.resize(chunks, 0u);

    for (std::size_t repeat = 0; repeat < 10'000u; ++repeat) {
        (void)jobs.parallel_for(item_count, grain,
            [&](JobContext& context, std::size_t chunk, std::size_t begin, std::size_t end) {
                auto temp = context.scratch.allocate<std::uint64_t>(4u);
                temp[0] = repeat;
                std::uint64_t local = 0u;
                for (std::size_t i = begin; i < end; ++i) {
                    const auto value = static_cast<std::uint64_t>((i + repeat) & 0xffffu);
                    values[i] = value;
                    local += value;
                }
                reduction.partial(chunk) = local;
            });
        const auto checksum = reduction.fold(std::uint64_t{0}, [](std::uint64_t a, std::uint64_t b) { return a + b; });
        if (checksum == 0u) return 2;
    }

    // Lifecycle stress specifically covers startup/shutdown synchronization.
    for (std::size_t repeat = 0; repeat < 1'000u; ++repeat) {
        JobSystem short_lived{2u, 1024u};
        (void)short_lived.parallel_for(100u, 1u,
            [](JobContext& context, std::size_t, std::size_t, std::size_t) {
                auto temp = context.scratch.allocate<std::byte>(900u);
                temp[0] = std::byte{0x5a};
            });
    }

    std::cout << "Thunder JobSystem stress passed\n";
    return 0;
}
