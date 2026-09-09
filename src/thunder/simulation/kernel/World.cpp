#include "thunder/simulation/kernel/World.hpp"
#include "thunder/foundation/base/Hash.hpp"

namespace thunder {
namespace {
// Slot liveness is already part of the individual SoA store checksums.  The
// allocator's generation counters and LIFO free-list are authoritative too:
// they decide which future handles are valid and which index is recycled next.
// Keep this contribution in the world checksum (rather than the legacy store
// checksums) so pre-SLT1 save migrations can continue to compare their old
// component hashes byte-for-byte.
} // namespace

void hash_slot_allocator_state(Fnv1a64& hash, const SlotPool& pool) noexcept {
    const auto generations = pool.generations();
    hash.add(generations.size());
    for (const auto generation : generations) hash.add(generation);

    const auto bitmap = pool.bitmap();
    hash.add(bitmap.size());
    for (const auto word : bitmap) hash.add(word);

    const auto free_list = pool.free_list();
    hash.add(free_list.size());
    for (const auto index : free_list) hash.add(index);
}

std::uint64_t World::checksum() const noexcept {
    Fnv1a64 h;
    h.add(countries.checksum());
    h.add(markets.checksum());
    h.add(buildings.checksum());
    hash_slot_allocator_state(h, buildings.slot_pool());
    h.add(pops.checksum());
    hash_slot_allocator_state(h, pops.slot_pool());
    h.add(geography.checksum());
    h.add(grand_strategy.checksum());
    h.add(currencies.checksum());
    h.add(banks.checksum());
    h.add(trade_policies.checksum());
    h.add(construction.checksum());
    // Empty global script state is omitted to keep historical v1-v4 checksums
    // byte-compatible. Once content installs state, GLB1 is authoritative and
    // the tagged checksum makes it visible to save/load and desync detection.
    if (!global_scripts.empty()) {
        h.add(0x31424c47u); // "GLB1" domain separator.
        h.add(global_scripts.checksum());
    }
    return h.value();
}

} // namespace thunder
