#include "thunder/foundation/base/Hash.hpp"
#include "thunder/runtime/save/ReplayJournal.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

using namespace thunder;

namespace {

constexpr std::size_t header_bytes = 64;
constexpr std::size_t journal_checksum_offset = 40;
constexpr std::size_t payload_checksum_offset = 48;

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

std::uint64_t read_u64_le(std::span<const std::byte> bytes, std::size_t offset) {
    assert(offset + sizeof(std::uint64_t) <= bytes.size());
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset++])) << shift;
    return value;
}

std::uint64_t byte_checksum(std::span<const std::byte> bytes) {
    Fnv1a64 hash;
    hash.add_bytes(bytes);
    return hash.value();
}

void refresh_payload_checksum(std::vector<std::byte>& bytes) {
    assert(bytes.size() >= header_bytes);
    write_u64_le(bytes, payload_checksum_offset,
                 byte_checksum(std::span<const std::byte>{bytes}.subspan(header_bytes)));
}

template <class Mutation>
void expect_decode_failure_is_atomic(const ReplayJournalBlob& valid, Mutation mutate) {
    auto malformed = valid.bytes;
    mutate(malformed);

    ReplayJournal destination;
    destination.record(2, CommandType::SetTaxRate, CountryId{77}, 0.25);
    destination.checkpoint(3, 0x7777u);
    const auto before_checksum = destination.checksum();
    const auto before_entries = destination.entries().size();
    const auto before_checkpoints = destination.checkpoints().size();

    bool rejected = false;
    try {
        (void)ReplayJournalCodec::decode(malformed, destination);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(destination.checksum() == before_checksum);
    assert(destination.entries().size() == before_entries);
    assert(destination.checkpoints().size() == before_checkpoints);
}

ReplayJournal make_journal() {
    ReplayJournal journal;
    journal.record(1, CommandType::SetTaxRate, CountryId{0}, 0.125);
    journal.record(1, CommandType::AddTreasury, CountryId{4}, -0.0);
    journal.record(27, CommandType::AddTreasury, CountryId{4}, -123'456.75);
    journal.checkpoint(24, 0x0123456789abcdefull);
    journal.checkpoint(48, 0xfedcba9876543210ull);
    return journal;
}

void test_validation_and_desync_contract() {
    const auto journal = make_journal();
    assert(journal.validate_order());
    assert(journal.validate());

    const auto absent = journal.verify_checkpoint(25, 123u);
    assert(absent.status == ReplayCheckpointStatus::NotRecorded);
    assert(!absent.matched());
    assert(absent.tick == 25u);
    assert(absent.actual_checksum == 123u);

    const auto match = journal.verify_checkpoint(24, 0x0123456789abcdefull);
    assert(match.status == ReplayCheckpointStatus::Match);
    assert(match.matched());
    assert(match.expected_checksum == match.actual_checksum);

    const auto desync = journal.verify_checkpoint(48, 0xdeadbeefull);
    assert(desync.status == ReplayCheckpointStatus::Desync);
    assert(!desync.matched());
    assert(desync.expected_checksum == 0xfedcba9876543210ull);
    assert(desync.actual_checksum == 0xdeadbeefull);

    ReplayJournal ordering;
    ordering.record(5, CommandType::SetTaxRate, CountryId{0}, 0.1);
    bool entry_rejected = false;
    try {
        ordering.record(4, CommandType::SetTaxRate, CountryId{0}, 0.2);
    } catch (const std::invalid_argument&) {
        entry_rejected = true;
    }
    assert(entry_rejected);
    ordering.checkpoint(6, 1);
    bool checkpoint_rejected = false;
    try {
        ordering.checkpoint(6, 2);
    } catch (const std::invalid_argument&) {
        checkpoint_rejected = true;
    }
    assert(checkpoint_rejected);
    bool finite_rejected = false;
    try {
        ordering.record(7, CommandType::AddTreasury, CountryId{0},
                        std::numeric_limits<double>::infinity());
    } catch (const std::invalid_argument&) {
        finite_rejected = true;
    }
    assert(finite_rejected);
}

void test_versioned_round_trip_and_stable_layout() {
    const auto source = make_journal();
    const auto first = ReplayJournalCodec::encode(source);
    const auto second = ReplayJournalCodec::encode(source);

    assert(first.metadata.version == 1u);
    assert(first.metadata.entry_count == 3u);
    assert(first.metadata.checkpoint_count == 2u);
    assert(first.metadata.journal_checksum == source.checksum());
    // Golden v1 semantic digest: changing canonical field order requires a new
    // replay schema version and an explicit migration path.
    assert(source.checksum() == 0x4700118bb893178dull);
    assert(first.bytes == second.bytes);
    assert(first.bytes.size() == header_bytes + 3u * 24u + 2u * 16u);
    assert(std::to_integer<char>(first.bytes[0]) == 'C');
    assert(std::to_integer<char>(first.bytes[7]) == '1');
    assert(read_u64_le(first.bytes, 16) == 3u);
    assert(read_u64_le(first.bytes, 24) == 2u);
    assert(read_u64_le(first.bytes, 32) == 104u);
    assert(read_u64_le(first.bytes, journal_checksum_offset) == source.checksum());
    assert(read_u64_le(first.bytes, payload_checksum_offset) ==
           byte_checksum(std::span<const std::byte>{first.bytes}.subspan(header_bytes)));
    assert(read_u64_le(first.bytes, payload_checksum_offset) == 0x7a274271ce46ee85ull);

    // Fixed-width v1 entry layout: tick/u8 type/3 reserved/u32 stable id/f64.
    assert(read_u64_le(first.bytes, header_bytes) == 1u);
    assert(std::to_integer<std::uint8_t>(first.bytes[header_bytes + 8]) ==
           static_cast<std::uint8_t>(CommandType::SetTaxRate));
    assert(read_u64_le(first.bytes, header_bytes + 16) == std::bit_cast<std::uint64_t>(0.125));
    assert(read_u64_le(first.bytes, header_bytes + 24 + 16) ==
           std::bit_cast<std::uint64_t>(-0.0));

    ReplayJournal restored;
    restored.record(99, CommandType::AddTreasury, CountryId{99}, 1.0);
    const auto metadata = ReplayJournalCodec::decode(first.bytes, restored);
    assert(metadata.version == first.metadata.version);
    assert(metadata.entry_count == first.metadata.entry_count);
    assert(metadata.checkpoint_count == first.metadata.checkpoint_count);
    assert(metadata.journal_checksum == first.metadata.journal_checksum);
    assert(restored.checksum() == source.checksum());
    assert(restored.entries().size() == source.entries().size());
    assert(restored.checkpoints().size() == source.checkpoints().size());
    for (std::size_t index = 0; index < source.entries().size(); ++index) {
        const auto& expected = source.entries()[index];
        const auto& actual = restored.entries()[index];
        assert(actual.tick == expected.tick);
        assert(actual.type == expected.type);
        assert(actual.country == expected.country);
        assert(std::bit_cast<std::uint64_t>(actual.value) ==
               std::bit_cast<std::uint64_t>(expected.value));
    }
    for (std::size_t index = 0; index < source.checkpoints().size(); ++index) {
        assert(restored.checkpoints()[index].tick == source.checkpoints()[index].tick);
        assert(restored.checkpoints()[index].world_checksum ==
               source.checkpoints()[index].world_checksum);
    }
    assert(ReplayJournalCodec::encode(restored).bytes == first.bytes);
}

void test_corruption_schema_and_atomic_decode() {
    const auto valid = ReplayJournalCodec::encode(make_journal());

    expect_decode_failure_is_atomic(valid, [](auto& bytes) { bytes.back() ^= std::byte{0x01}; });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) { bytes.resize(bytes.size() - 1u); });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) { bytes.push_back(std::byte{0}); });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) { bytes[0] = std::byte{'X'}; });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) { write_u32_le(bytes, 8, 2); });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) { write_u32_le(bytes, 12, 63); });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) { write_u64_le(bytes, 56, 1); });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) {
        write_u64_le(bytes, 16, 10'000'001u);
    });

    // Recompute the byte-level hash so these reach structural/semantic checks.
    expect_decode_failure_is_atomic(valid, [](auto& bytes) {
        bytes[header_bytes + 9] = std::byte{1};
        refresh_payload_checksum(bytes);
    });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) {
        bytes[header_bytes + 8] = std::byte{0xff};
        refresh_payload_checksum(bytes);
    });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) {
        write_u64_le(bytes, header_bytes + 16, 0x7ff8000000000000ull);
        refresh_payload_checksum(bytes);
    });
    expect_decode_failure_is_atomic(valid, [](auto& bytes) {
        write_u64_le(bytes, journal_checksum_offset,
                     read_u64_le(bytes, journal_checksum_offset) ^ 1u);
    });
}

void test_checksum_covers_order_ids_values_and_checkpoints() {
    const auto baseline = make_journal();

    ReplayJournal reordered;
    reordered.record(1, CommandType::AddTreasury, CountryId{4}, -0.0);
    reordered.record(1, CommandType::SetTaxRate, CountryId{0}, 0.125);
    reordered.record(27, CommandType::AddTreasury, CountryId{4}, -123'456.75);
    reordered.checkpoint(24, 0x0123456789abcdefull);
    reordered.checkpoint(48, 0xfedcba9876543210ull);
    assert(reordered.checksum() != baseline.checksum());

    ReplayJournal changed_id;
    changed_id.record(1, CommandType::SetTaxRate, CountryId{1}, 0.125);
    changed_id.record(1, CommandType::AddTreasury, CountryId{4}, -0.0);
    changed_id.record(27, CommandType::AddTreasury, CountryId{4}, -123'456.75);
    changed_id.checkpoint(24, 0x0123456789abcdefull);
    changed_id.checkpoint(48, 0xfedcba9876543210ull);
    assert(changed_id.checksum() != baseline.checksum());

    ReplayJournal changed_checkpoint = make_journal();
    changed_checkpoint.clear();
    changed_checkpoint.record(1, CommandType::SetTaxRate, CountryId{0}, 0.125);
    changed_checkpoint.record(1, CommandType::AddTreasury, CountryId{4}, -0.0);
    changed_checkpoint.record(27, CommandType::AddTreasury, CountryId{4}, -123'456.75);
    changed_checkpoint.checkpoint(24, 0x0123456789abcdefull);
    changed_checkpoint.checkpoint(48, 0xfedcba9876543211ull);
    assert(changed_checkpoint.checksum() != baseline.checksum());

    ReplayJournal invalid;
    invalid.record(1, CommandType::SetTaxRate, CountryId{}, 0.1);
    assert(!invalid.validate());
    bool encode_rejected = false;
    try {
        (void)ReplayJournalCodec::encode(invalid);
    } catch (const std::invalid_argument&) {
        encode_rejected = true;
    }
    assert(encode_rejected);
}

} // namespace

int main() {
    test_validation_and_desync_contract();
    test_versioned_round_trip_and_stable_layout();
    test_corruption_schema_and_atomic_decode();
    test_checksum_covers_order_ids_values_and_checkpoints();
    std::cout << "Thunder 1.0 replay journal tests: PASS\n";
    return 0;
}
