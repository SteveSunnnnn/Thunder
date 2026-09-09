#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <memory>

namespace thunder {

// Read-only random-access file optimized for streaming workers. On POSIX this
// uses pread(), so multiple threads can read one descriptor without sharing a
// mutable seek position or serializing on a stream mutex.
class RandomAccessFile {
public:
    RandomAccessFile();
    ~RandomAccessFile();
    RandomAccessFile(RandomAccessFile&&) noexcept;
    RandomAccessFile& operator=(RandomAccessFile&&) noexcept;

    RandomAccessFile(const RandomAccessFile&) = delete;
    RandomAccessFile& operator=(const RandomAccessFile&) = delete;

    void open(const std::filesystem::path& path);
    void close() noexcept;
    void read_at(std::uint64_t offset, std::span<std::byte> destination) const;

    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace thunder
