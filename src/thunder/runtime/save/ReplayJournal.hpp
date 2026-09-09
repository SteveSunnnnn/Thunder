#pragma once
#include "thunder/simulation/kernel/CommandQueue.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {
struct ReplayEntry { std::uint64_t tick=0; CommandType type{}; CountryId country{}; double value=0.0; };
struct ReplayCheckpoint { std::uint64_t tick=0; std::uint64_t world_checksum=0; };

enum class ReplayCheckpointStatus : std::uint8_t {
    NotRecorded,
    Match,
    Desync,
};

struct ReplayCheckpointVerification {
    ReplayCheckpointStatus status = ReplayCheckpointStatus::NotRecorded;
    std::uint64_t tick = 0;
    std::uint64_t expected_checksum = 0;
    std::uint64_t actual_checksum = 0;

    [[nodiscard]] bool matched() const noexcept { return status == ReplayCheckpointStatus::Match; }
};

class ReplayJournal {
public:
    void record(std::uint64_t tick, CommandType type, CountryId country, double value);
    void checkpoint(std::uint64_t tick, std::uint64_t checksum);
    [[nodiscard]] std::span<const ReplayEntry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::span<const ReplayCheckpoint> checkpoints() const noexcept { return checkpoints_; }
    [[nodiscard]] bool validate_order() const noexcept;
    [[nodiscard]] bool validate() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] ReplayCheckpointVerification verify_checkpoint(
        std::uint64_t tick, std::uint64_t actual_checksum) const noexcept;
    void clear() noexcept { entries_.clear(); checkpoints_.clear(); }
private:
    std::vector<ReplayEntry> entries_;
    std::vector<ReplayCheckpoint> checkpoints_;
};

struct ReplayJournalMetadata {
    std::uint32_t version = 1;
    std::uint64_t entry_count = 0;
    std::uint64_t checkpoint_count = 0;
    std::uint64_t journal_checksum = 0;
};

struct ReplayJournalBlob {
    ReplayJournalMetadata metadata{};
    std::vector<std::byte> bytes;
};

// Versioned, endian-stable replay persistence. Decoding is transactional: the
// destination journal is replaced only after framing, payload and semantic
// validation have all succeeded.
class ReplayJournalCodec {
public:
    [[nodiscard]] static ReplayJournalBlob encode(const ReplayJournal& journal);
    static ReplayJournalMetadata decode(std::span<const std::byte> bytes,
                                        ReplayJournal& journal);
};
} // namespace thunder
