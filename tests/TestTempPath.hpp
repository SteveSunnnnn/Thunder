#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace thunder_test {

// CTest may run multiple build configurations in parallel. Keep every test
// artifact isolated instead of relying on a process-global fixed filename.
inline std::filesystem::path unique_temp_path(std::string_view stem) {
    static std::atomic<std::uint64_t> sequence{0u};
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto serial = sequence.fetch_add(1u, std::memory_order_relaxed);
#if defined(_WIN32)
    const auto process = static_cast<std::uint64_t>(::_getpid());
#else
    const auto process = static_cast<std::uint64_t>(::getpid());
#endif
    std::string name(stem);
    name.append(".");
    name.append(std::to_string(process));
    name.append(".");
    name.append(std::to_string(now));
    name.append(".");
    name.append(std::to_string(serial));
    return std::filesystem::temp_directory_path() / name;
}

} // namespace thunder_test
