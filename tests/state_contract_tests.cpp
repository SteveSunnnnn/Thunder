#include "thunder/foundation/base/Hash.hpp"
#include "thunder/runtime/engine/ThunderEngine.hpp"
#include "thunder/simulation/kernel/CommandQueue.hpp"
#include "thunder/runtime/save/ReplayJournal.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/simulation/ai/UtilityAi.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace thunder;

namespace {

constexpr std::size_t save_header_bytes_v4 = 60;
constexpr std::size_t runtime_checksum_offset_v4 = 36;
constexpr std::size_t payload_checksum_offset_v4 = 52;
constexpr std::size_t clock_day_index_payload_offset = 24;

CountryId seed_world(ThunderEngine& engine, std::string tag, double treasury = 100.0) {
    return engine.world().countries.create({std::move(tag), 1'000.0, 500.0, treasury, 0.2});
}

void write_u32_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    assert(offset + sizeof(value) <= bytes.size());
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffu);
}

void write_u64_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    assert(offset + sizeof(value) <= bytes.size());
    for (unsigned shift = 0; shift < 64; shift += 8)
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffu);
}

std::uint64_t byte_checksum(std::span<const std::byte> bytes) {
    Fnv1a64 hash;
    hash.add_bytes(bytes);
    return hash.value();
}

void refresh_save_payload_checksum(std::vector<std::byte>& bytes) {
    assert(bytes.size() >= save_header_bytes_v4);
    write_u64_le(bytes, payload_checksum_offset_v4,
                 byte_checksum(std::span<const std::byte>{bytes}.subspan(save_header_bytes_v4)));
}

std::uint64_t legacy_v4_runtime_checksum(const ThunderEngine& engine) {
    Fnv1a64 hash;
    hash.add(engine.gameplay().checksum());
    hash.add(engine.ai().checksum());
    return hash.value();
}

void test_clock_state_validation_and_checksum() {
    GameClock midnight;
    for (std::uint64_t tick = 0; tick < 40; ++tick) {
        assert(midnight.validate_state());
        midnight.advance_tick();
    }
    assert(midnight.validate_state());

    GameClock evening{{1900, 2, 28, 18}};
    assert(evening.validate_state());
    evening.advance_tick();
    assert(evening.date().year == 1900);
    assert(evening.date().month == 3u);
    assert(evening.date().day == 1u);
    assert(evening.date().hour == 0u);
    assert(evening.day_index() == 1u);
    assert(evening.validate_state());

    assert(GameClock::validate_state({2000, 2, 29, 12}, 0, 0));
    assert(!GameClock::validate_state({1900, 2, 29, 0}, 0, 0));
    assert(!GameClock::validate_state({2000, 13, 1, 0}, 0, 0));
    assert(!GameClock::validate_state({2000, 1, 1, 5}, 0, 0));
    assert(!GameClock::validate_state({2000, 1, 2, 0}, 4, 0));
    assert(!GameClock::validate_state({2000, 1, 2, 6}, 5, 2));

    GameClock changed_day;
    const auto baseline_checksum = changed_day.checksum();
    changed_day.restore_state(changed_day.date(), changed_day.tick_index(), 1);
    assert(changed_day.checksum() != baseline_checksum);
    assert(!changed_day.validate_state());
}

void test_v4_clock_checksum_and_legacy_read_compatibility() {
    ThunderEngine source{{0u, 0x1111222233334444ull, 0x5555666677778888ull}};
    seed_world(source, "SRC");
    source.advance_ticks(5);
    assert(source.clock().tick_index() == 5u);
    assert(source.clock().day_index() == 1u);
    assert(source.clock().validate_state());
    const auto save = source.make_save();
    assert(save.metadata.version == 5u);
    assert(save.bytes.size() >= save_header_bytes_v4 + 32u);
    assert(save.metadata.runtime_checksum != legacy_v4_runtime_checksum(source));

    ThunderEngine restored{{0u, 0x1111222233334444ull, 0x5555666677778888ull}};
    restored.restore(save.bytes);
    assert(restored.engine_checksum() == source.engine_checksum());
    assert(restored.clock().day_index() == source.clock().day_index());

    // Prior v4 saves authenticated only Gameplay + AI in this header field.
    // Rewriting the current blob to that exact legacy digest exercises the
    // compatibility branch without weakening newly emitted v4 saves.
    auto legacy_v4 = save.bytes;
    write_u64_le(legacy_v4, runtime_checksum_offset_v4, legacy_v4_runtime_checksum(source));
    ThunderEngine legacy_restored{{0u, 0x1111222233334444ull, 0x5555666677778888ull}};
    legacy_restored.restore(legacy_v4);
    assert(legacy_restored.engine_checksum() == source.engine_checksum());
}

void test_clock_corruption_rejected_atomically() {
    ThunderEngine source{{0u, 0u, 0u}};
    seed_world(source, "SOURCE");
    source.advance_ticks(5);
    auto malformed = source.make_save().bytes;

    // Keep framing valid so validation reaches authoritative clock semantics.
    write_u64_le(malformed, save_header_bytes_v4 + clock_day_index_payload_offset, 2u);
    refresh_save_payload_checksum(malformed);

    ThunderEngine victim{{0u, 0u, 0u}};
    const auto country = seed_world(victim, "VICTIM", 25.0);
    victim.queue_command(CommandType::AddTreasury, country, 10.0);
    victim.replay().checkpoint(10, 0x1234u);
    const auto engine_before = victim.engine_checksum();
    const auto replay_entries_before = victim.replay().entries().size();
    const auto replay_checkpoints_before = victim.replay().checkpoints().size();

    bool rejected = false;
    try {
        victim.restore(malformed);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(victim.engine_checksum() == engine_before);
    assert(victim.world().countries.tag(country) == "VICTIM");
    assert(victim.replay().entries().size() == replay_entries_before);
    assert(victim.replay().checkpoints().size() == replay_checkpoints_before);

    // A failed restore must retain the pending command as well as replay data.
    victim.advance_tick();
    assert(victim.world().countries.treasury(country) == 35.0);

    // A valid-looking calendar mutation is caught by the clock checksum even
    // when the tick/day/hour scheduling tuple itself remains structurally valid.
    malformed = source.make_save().bytes;
    write_u32_le(malformed, save_header_bytes_v4 + 8u, 3u);
    refresh_save_payload_checksum(malformed);
    const auto before_second_failure = victim.engine_checksum();
    rejected = false;
    try {
        victim.restore(malformed);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(victim.engine_checksum() == before_second_failure);
}

void test_successful_restore_clears_transient_input_and_replay() {
    ThunderEngine source{{0u, 0u, 0u}};
    const auto source_country = seed_world(source, "RESTORED", 100.0);
    source.advance_ticks(1);
    const auto save = source.make_save();

    ThunderEngine target{{0u, 0u, 0u}};
    const auto target_country = seed_world(target, "OLD", 1.0);
    target.queue_command(CommandType::AddTreasury, target_country, 999.0);
    target.replay().checkpoint(100, 0xabcdefu);
    assert(!target.replay().entries().empty());
    assert(!target.replay().checkpoints().empty());

    target.restore(save.bytes);
    assert(target.replay().entries().empty());
    assert(target.replay().checkpoints().empty());
    assert(target.world().countries.tag(source_country) == "RESTORED");
    assert(target.world().countries.treasury(source_country) == 100.0);

    // The command queued against the pre-restore world must never leak into
    // the restored timeline.
    target.advance_tick();
    assert(target.world().countries.treasury(source_country) == 100.0);
}

void test_invalid_clock_cannot_be_saved() {
    ThunderEngine engine{{0u, 0u, 0u}};
    seed_world(engine, "BAD");
    engine.clock().restore_state({1836, 1, 2, 0}, 4, 0);
    assert(!engine.clock().validate_state());
    bool rejected = false;
    try {
        (void)engine.make_save();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

} // namespace

int main() {
    test_clock_state_validation_and_checksum();
    test_v4_clock_checksum_and_legacy_read_compatibility();
    test_clock_corruption_rejected_atomically();
    test_successful_restore_clears_transient_input_and_replay();
    test_invalid_clock_cannot_be_saved();
    std::cout << "Thunder 1.0 authoritative state contract tests: PASS\n";
    return 0;
}
