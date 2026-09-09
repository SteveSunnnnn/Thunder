#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/foundation/memory/SlotPool.hpp"
#include "thunder/simulation/economy/BuildingStore.hpp"
#include "thunder/simulation/economy/CountryStore.hpp"
#include "thunder/simulation/economy/CurrencyStore.hpp"
#include "thunder/simulation/economy/BankStore.hpp"
#include "thunder/simulation/economy/TradePolicyStore.hpp"
#include "thunder/simulation/economy/MarketStore.hpp"
#include "thunder/simulation/economy/PopStore.hpp"
#include "thunder/simulation/economy/ConstructionStore.hpp"
#include "thunder/simulation/world/GeographyStore.hpp"
#include "thunder/simulation/world/WorldMapHierarchy.hpp"
#include "thunder/simulation/grand_strategy/GrandStrategyStore.hpp"
#include "thunder/simulation/kernel/AuthoritativeStoreRegistry.hpp"
#include "thunder/scripting/GlobalScriptStore.hpp"
#include <cstddef>
#include <cstdint>

namespace thunder {

// Hashes a slot pool's authoritative allocator state (generations, liveness
// bitmap, LIFO free-list order). Part of World::checksum and of the legacy
// save-digest variants in the save codec.
void hash_slot_allocator_state(Fnv1a64& hash, const SlotPool& pool) noexcept;

class World {
public:
    CountryStore countries;
    MarketStore markets;
    BuildingStore buildings;
    PopStore pops;
    GeographyStore geography;
    WorldMapHierarchy map_hierarchy;
    GrandStrategyStore grand_strategy;
    CurrencyStore currencies;
    BankStore banks;
    TradePolicyStore trade_policies;
    ConstructionStore construction;
    GlobalScriptStore global_scripts;

    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::uint64_t registry_checksum() const noexcept {
        // Generic path: if registry has entries, use it; otherwise fall back
        // to legacy World::checksum for save-compat. Keeps old saves valid.
        auto& reg = AuthoritativeStoreRegistry::instance();
        return reg.stores().empty() ? checksum() : reg.combined_checksum();
    }
    [[nodiscard]] std::size_t economy_memory_bytes() const noexcept {
        return markets.memory_bytes() + buildings.memory_bytes() + pops.memory_bytes() + geography.memory_bytes() + grand_strategy.memory_bytes() + currencies.memory_bytes() + banks.memory_bytes() + trade_policies.memory_bytes() + construction.memory_bytes();
    }
    [[nodiscard]] std::uint64_t global_script_checksum() const noexcept { return global_scripts.checksum(); }
};

} // namespace thunder
