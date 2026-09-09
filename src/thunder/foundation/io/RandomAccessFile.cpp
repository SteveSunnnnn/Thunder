#include "thunder/foundation/io/RandomAccessFile.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


namespace thunder {

struct RandomAccessFile::Impl {
#if defined(__unix__) || defined(__APPLE__)
    int fd = -1;
#elif defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    mutable std::ifstream stream;
    mutable std::mutex mutex;
#endif
    std::uint64_t file_size = 0;
};

RandomAccessFile::RandomAccessFile() : impl_(std::make_unique<Impl>()) {}
RandomAccessFile::~RandomAccessFile() { close(); }
RandomAccessFile::RandomAccessFile(RandomAccessFile&&) noexcept = default;
RandomAccessFile& RandomAccessFile::operator=(RandomAccessFile&&) noexcept = default;

void RandomAccessFile::open(const std::filesystem::path& path) {
    close();
#if defined(__unix__) || defined(__APPLE__)
    impl_->fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (impl_->fd < 0) throw std::runtime_error("open failed for " + path.string() + ": " + std::strerror(errno));
    struct stat st{};
    if (::fstat(impl_->fd, &st) != 0 || st.st_size < 0) {
        const std::string error = std::strerror(errno);
        close();
        throw std::runtime_error("fstat failed for " + path.string() + ": " + error);
    }
    impl_->file_size = static_cast<std::uint64_t>(st.st_size);
#elif defined(_WIN32)
    impl_->handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateFileW failed for " + path.string() + " error=" + std::to_string(::GetLastError()));
    }
    LARGE_INTEGER size_val{};
    if (!::GetFileSizeEx(impl_->handle, &size_val) || size_val.QuadPart < 0) {
        close();
        throw std::runtime_error("GetFileSizeEx failed for " + path.string());
    }
    impl_->file_size = static_cast<std::uint64_t>(size_val.QuadPart);
#else
    impl_->stream.open(path, std::ios::binary);
    if (!impl_->stream) throw std::runtime_error("open failed for " + path.string());
    impl_->stream.seekg(0, std::ios::end);
    const auto end = impl_->stream.tellg();
    if (end < 0) { close(); throw std::runtime_error("size failed for " + path.string()); }
    impl_->file_size = static_cast<std::uint64_t>(end);
#endif
}

void RandomAccessFile::close() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    if (impl_ && impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
#elif defined(_WIN32)
    if (impl_ && impl_->handle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
    }
#else
    if (impl_ && impl_->stream.is_open()) impl_->stream.close();
#endif
    if (impl_) impl_->file_size = 0;
}

void RandomAccessFile::read_at(std::uint64_t offset, std::span<std::byte> destination) const {
    if (!is_open()) throw std::logic_error("RandomAccessFile is not open");
    if (offset > impl_->file_size || destination.size() > impl_->file_size - offset)
        throw std::out_of_range("RandomAccessFile read outside file");
    if (destination.empty()) return;
#if defined(__unix__) || defined(__APPLE__)
    std::size_t done = 0;
    while (done < destination.size()) {
        const auto absolute = offset + done;
        if (absolute > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
            throw std::overflow_error("RandomAccessFile offset exceeds off_t");
        const auto count = ::pread(impl_->fd, destination.data() + done, destination.size() - done,
                                   static_cast<off_t>(absolute));
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("pread failed: ") + std::strerror(errno));
        }
        if (count == 0) throw std::runtime_error("unexpected EOF during pread");
        done += static_cast<std::size_t>(count);
    }
#elif defined(_WIN32)
    std::size_t done = 0;
    while (done < destination.size()) {
        const std::uint64_t absolute = offset + done;
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(absolute & 0xFFFFFFFFull);
        ov.OffsetHigh = static_cast<DWORD>((absolute >> 32u) & 0xFFFFFFFFull);
        const auto to_read = static_cast<DWORD>(
            std::min<std::size_t>(destination.size() - done, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD bytes_read = 0;
        if (!::ReadFile(impl_->handle, destination.data() + done, to_read, &bytes_read, &ov)) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_HANDLE_EOF) throw std::runtime_error("unexpected EOF during ReadFile");
            throw std::runtime_error("ReadFile failed with error " + std::to_string(err));
        }
        if (bytes_read == 0) throw std::runtime_error("unexpected 0 bytes during ReadFile");
        done += bytes_read;
    }
#else
    std::lock_guard lock(impl_->mutex);
    impl_->stream.clear();
    impl_->stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    impl_->stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    if (!impl_->stream) throw std::runtime_error("random-access read failed");
#endif
}

std::uint64_t RandomAccessFile::size() const noexcept { return impl_ ? impl_->file_size : 0; }

bool RandomAccessFile::is_open() const noexcept {
#if defined(__unix__) || defined(__APPLE__)
    return impl_ && impl_->fd >= 0;
#elif defined(_WIN32)
    return impl_ && impl_->handle != INVALID_HANDLE_VALUE;
#else
    return impl_ && impl_->stream.is_open();
#endif
}


} // namespace thunder
