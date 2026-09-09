#include "thunder/runtime/save/ReplayJournal.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thunder {

namespace {

constexpr std::array<char, 8> replay_magic{'C', 'O', 'R', 'E', 'R', 'P', 'L', '1'};
constexpr std::uint32_t replay_version = 1;
constexpr std::uint32_t replay_header_bytes = 64;
constexpr std::uint64_t replay_entry_bytes = 24;
constexpr std::uint64_t replay_checkpoint_bytes = 16;
constexpr std::uint64_t max_replay_entries = 10'000'000;
constexpr std::uint64_t max_replay_checkpoints = 1'000'000;

[[nodiscard]] bool known_command_type(CommandType type) noexcept {
    switch (type) {
        case CommandType::SetTaxRate:
        case CommandType::AddTreasury:
            return true;
    }
    return false;
}

// Replay wire ids are a persistence contract and intentionally do not depend
// on the enum declaration order in CommandQueue.hpp.
[[nodiscard]] std::uint8_t command_type_wire_id(CommandType type) {
    switch (type) {
        case CommandType::SetTaxRate: return 0;
        case CommandType::AddTreasury: return 1;
    }
    throw std::invalid_argument("unknown replay command type");
}

[[nodiscard]] CommandType command_type_from_wire(std::uint8_t wire_id) {
    switch (wire_id) {
        case 0: return CommandType::SetTaxRate;
        case 1: return CommandType::AddTreasury;
        default: throw std::runtime_error("unknown command type in Thunder replay journal");
    }
}

class Writer {
public:
    void u8(std::uint8_t value) { bytes.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) {
        for (unsigned shift = 0; shift < 16; shift += 8) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void raw(std::span<const std::byte> value) { bytes.insert(bytes.end(), value.begin(), value.end()); }

    std::vector<std::byte> bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> input) : bytes_(input) {}

    [[nodiscard]] std::uint8_t u8() {
        need(1);
        return std::to_integer<std::uint8_t>(bytes_[position_++]);
    }
    [[nodiscard]] std::uint16_t u16() {
        std::uint16_t value = 0;
        for (unsigned shift = 0; shift < 16; shift += 8)
            value = static_cast<std::uint16_t>(value | static_cast<std::uint16_t>(u8()) << shift);
        return value;
    }
    [[nodiscard]] std::uint32_t u32() {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }
    [[nodiscard]] std::uint64_t u64() {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }
    [[nodiscard]] double f64() { return std::bit_cast<double>(u64()); }
    [[nodiscard]] bool done() const noexcept { return position_ == bytes_.size(); }

private:
    void need(std::size_t size) const {
        if (position_ > bytes_.size() || size > bytes_.size() - position_)
            throw std::runtime_error("truncated Thunder replay journal");
    }

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

void hash_u8(Fnv1a64& hash, std::uint8_t value) noexcept {
    const auto byte = static_cast<std::byte>(value);
    hash.add_bytes(std::span<const std::byte>{&byte, 1});
}

void hash_u32(Fnv1a64& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32; shift += 8)
        hash_u8(hash, static_cast<std::uint8_t>(value >> shift));
}

void hash_u64(Fnv1a64& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64; shift += 8)
        hash_u8(hash, static_cast<std::uint8_t>(value >> shift));
}

[[nodiscard]] std::uint64_t byte_checksum(std::span<const std::byte> bytes) noexcept {
    Fnv1a64 hash;
    hash.add_bytes(bytes);
    return hash.value();
}

[[nodiscard]] std::uint64_t checked_payload_size(std::uint64_t entry_count,
                                                 std::uint64_t checkpoint_count) {
    if (entry_count > max_replay_entries)
        throw std::runtime_error("replay entry count exceeds safety cap");
    if (checkpoint_count > max_replay_checkpoints)
        throw std::runtime_error("replay checkpoint count exceeds safety cap");
    if (entry_count > (std::numeric_limits<std::uint64_t>::max() -
                       checkpoint_count * replay_checkpoint_bytes) /
                          replay_entry_bytes)
        throw std::runtime_error("replay payload size overflow");
    return entry_count * replay_entry_bytes + checkpoint_count * replay_checkpoint_bytes;
}

} // namespace

void ReplayJournal::record(std::uint64_t tick, CommandType type, CountryId country, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("non-finite replay command");
    if (!entries_.empty() && tick < entries_.back().tick)
        throw std::invalid_argument("replay commands must be ordered by tick");
    entries_.push_back({tick, type, country, value});
}

void ReplayJournal::checkpoint(std::uint64_t tick, std::uint64_t checksum) {
    if (!checkpoints_.empty() && tick <= checkpoints_.back().tick)
        throw std::invalid_argument("replay checkpoints must be strictly ordered");
    checkpoints_.push_back({tick, checksum});
}

bool ReplayJournal::validate_order() const noexcept {
    for (std::size_t index = 1; index < entries_.size(); ++index)
        if (entries_[index].tick < entries_[index - 1].tick) return false;
    for (std::size_t index = 1; index < checkpoints_.size(); ++index)
        if (checkpoints_[index].tick <= checkpoints_[index - 1].tick) return false;
    return true;
}

bool ReplayJournal::validate() const noexcept {
    if (!validate_order()) return false;
    for (const auto& entry : entries_) {
        if (!known_command_type(entry.type) || !entry.country.valid() || !std::isfinite(entry.value))
            return false;
    }
    return true;
}

std::uint64_t ReplayJournal::checksum() const noexcept {
    Fnv1a64 hash;
    hash.add(std::string_view{"Thunder.ReplayJournal.v1"});
    hash_u64(hash, static_cast<std::uint64_t>(entries_.size()));
    for (const auto& entry : entries_) {
        hash_u64(hash, entry.tick);
        // validate() has already established that the switch is exhaustive.
        hash_u8(hash, entry.type == CommandType::SetTaxRate ? 0u : 1u);
        hash_u32(hash, entry.country.value());
        hash_u64(hash, std::bit_cast<std::uint64_t>(entry.value));
    }
    hash_u64(hash, static_cast<std::uint64_t>(checkpoints_.size()));
    for (const auto& checkpoint : checkpoints_) {
        hash_u64(hash, checkpoint.tick);
        hash_u64(hash, checkpoint.world_checksum);
    }
    return hash.value();
}

ReplayCheckpointVerification ReplayJournal::verify_checkpoint(
    std::uint64_t tick, std::uint64_t actual_checksum) const noexcept {
    const auto found = std::lower_bound(
        checkpoints_.begin(), checkpoints_.end(), tick,
        [](const ReplayCheckpoint& checkpoint, std::uint64_t requested_tick) {
            return checkpoint.tick < requested_tick;
        });
    if (found == checkpoints_.end() || found->tick != tick)
        return {ReplayCheckpointStatus::NotRecorded, tick, 0, actual_checksum};
    const auto status = found->world_checksum == actual_checksum
        ? ReplayCheckpointStatus::Match
        : ReplayCheckpointStatus::Desync;
    return {status, tick, found->world_checksum, actual_checksum};
}

ReplayJournalBlob ReplayJournalCodec::encode(const ReplayJournal& journal) {
    if (!journal.validate()) throw std::invalid_argument("invalid replay journal state");

    const auto entry_count = static_cast<std::uint64_t>(journal.entries().size());
    const auto checkpoint_count = static_cast<std::uint64_t>(journal.checkpoints().size());
    const auto payload_size = checked_payload_size(entry_count, checkpoint_count);
    if (payload_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw std::runtime_error("replay payload exceeds addressable size");

    Writer payload;
    payload.bytes.reserve(static_cast<std::size_t>(payload_size));
    for (const auto& entry : journal.entries()) {
        payload.u64(entry.tick);
        payload.u8(command_type_wire_id(entry.type));
        payload.u8(0);
        payload.u16(0);
        payload.u32(entry.country.value());
        payload.f64(entry.value);
    }
    for (const auto& checkpoint : journal.checkpoints()) {
        payload.u64(checkpoint.tick);
        payload.u64(checkpoint.world_checksum);
    }

    const auto journal_checksum = journal.checksum();
    Writer output;
    output.bytes.reserve(replay_header_bytes + payload.bytes.size());
    output.raw(std::as_bytes(std::span{replay_magic}));
    output.u32(replay_version);
    output.u32(replay_header_bytes);
    output.u64(entry_count);
    output.u64(checkpoint_count);
    output.u64(payload_size);
    output.u64(journal_checksum);
    output.u64(byte_checksum(payload.bytes));
    output.u64(0);
    output.raw(payload.bytes);

    return {{replay_version, entry_count, checkpoint_count, journal_checksum},
            std::move(output.bytes)};
}

ReplayJournalMetadata ReplayJournalCodec::decode(std::span<const std::byte> bytes,
                                                 ReplayJournal& journal) {
    if (bytes.size() < replay_header_bytes) throw std::runtime_error("truncated Thunder replay journal");

    Reader header(bytes.first(replay_header_bytes));
    for (const auto expected : replay_magic)
        if (header.u8() != static_cast<std::uint8_t>(expected))
            throw std::runtime_error("invalid Thunder replay journal magic");
    const auto version = header.u32();
    if (version != replay_version) throw std::runtime_error("unsupported Thunder replay journal version");
    if (header.u32() != replay_header_bytes)
        throw std::runtime_error("invalid Thunder replay journal header size");
    const auto entry_count = header.u64();
    const auto checkpoint_count = header.u64();
    const auto payload_size = header.u64();
    const auto expected_journal_checksum = header.u64();
    const auto expected_payload_checksum = header.u64();
    if (header.u64() != 0) throw std::runtime_error("non-zero Thunder replay journal reserved field");
    if (!header.done()) throw std::runtime_error("invalid Thunder replay journal header");

    const auto calculated_payload_size = checked_payload_size(entry_count, checkpoint_count);
    if (payload_size != calculated_payload_size)
        throw std::runtime_error("Thunder replay journal payload size mismatch");
    if (payload_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        bytes.size() - replay_header_bytes != static_cast<std::size_t>(payload_size))
        throw std::runtime_error("Thunder replay journal file size mismatch");
    const auto payload = bytes.subspan(replay_header_bytes);
    if (byte_checksum(payload) != expected_payload_checksum)
        throw std::runtime_error("Thunder replay journal payload checksum mismatch");

    ReplayJournal decoded;
    Reader records(payload);
    for (std::uint64_t index = 0; index < entry_count; ++index) {
        const auto tick = records.u64();
        const auto raw_type = records.u8();
        if (records.u8() != 0 || records.u16() != 0)
            throw std::runtime_error("non-zero Thunder replay entry reserved field");
        const auto country = CountryId{records.u32()};
        const auto value = records.f64();
        const auto type = command_type_from_wire(raw_type);
        if (!country.valid()) throw std::runtime_error("invalid CountryId in Thunder replay journal");
        decoded.record(tick, type, country, value);
    }
    for (std::uint64_t index = 0; index < checkpoint_count; ++index) {
        const auto tick = records.u64();
        const auto checksum = records.u64();
        decoded.checkpoint(tick, checksum);
    }
    if (!records.done()) throw std::runtime_error("trailing Thunder replay journal payload bytes");
    if (!decoded.validate()) throw std::runtime_error("invalid Thunder replay journal state");
    if (decoded.checksum() != expected_journal_checksum)
        throw std::runtime_error("Thunder replay journal semantic checksum mismatch");

    journal = std::move(decoded);
    return {version, entry_count, checkpoint_count, expected_journal_checksum};
}

} // namespace thunder
