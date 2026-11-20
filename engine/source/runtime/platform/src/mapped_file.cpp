#include "platform/mapped_file.hpp"

#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace star {

namespace {

[[nodiscard]] std::error_code lastFileError() noexcept {
#if defined(_WIN32)
    return {static_cast<int>(GetLastError()), std::system_category()};
#else
    return {errno, std::generic_category()};
#endif
}

#if !defined(_WIN32)
struct FileDescriptor {
    int value;

    ~FileDescriptor() { ::close(value); }
};
#endif

} // namespace

MappedFile::MappedFile(void *address, std::size_t size) noexcept : m_address(address), m_size(size) {}

MappedFile::MappedFile(MappedFile &&other) noexcept
    : m_address(std::exchange(other.m_address, nullptr)), m_size(std::exchange(other.m_size, 0)) {}

MappedFile &MappedFile::operator=(MappedFile &&other) noexcept {
    if (this != &other) {
        close();
        m_address = std::exchange(other.m_address, nullptr);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

MappedFile::~MappedFile() { close(); }

std::expected<MappedFile, std::error_code> MappedFile::open(const std::filesystem::path &path) noexcept {
    using NativePath = std::filesystem::path;
    if (path.empty() || path.native().find(NativePath::value_type{}) != NativePath::string_type::npos) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

#if defined(_WIN32)
    const HANDLE raw_file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (raw_file == INVALID_HANDLE_VALUE) {
        return std::unexpected(lastFileError());
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> file(raw_file, &CloseHandle);

    if (GetFileType(file.get()) != FILE_TYPE_DISK) {
        return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info)) {
        return std::unexpected(lastFileError());
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
    }
    const auto file_size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
#else
    const int raw_file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (raw_file < 0) {
        return std::unexpected(lastFileError());
    }
    const FileDescriptor file{raw_file};

    struct stat info{};
    if (fstat(file.value, &info) != 0) {
        return std::unexpected(lastFileError());
    }
    if (!S_ISREG(info.st_mode)) {
        return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
    }
    const auto file_size = info.st_size;
#endif

    if (file_size == 0) {
        return MappedFile(nullptr, 0);
    }
    if (!std::in_range<std::size_t>(file_size) || !std::in_range<std::ptrdiff_t>(file_size)) {
        return std::unexpected(std::make_error_code(std::errc::file_too_large));
    }
    const auto size = static_cast<std::size_t>(file_size);

#if defined(_WIN32)
    const std::unique_ptr<void, decltype(&CloseHandle)> mapping(
        CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr), &CloseHandle);
    if (!mapping) {
        return std::unexpected(lastFileError());
    }
    void *address = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, size);
    if (address == nullptr) {
        return std::unexpected(lastFileError());
    }
#else
    void *address = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, file.value, 0);
    if (address == MAP_FAILED) {
        return std::unexpected(lastFileError());
    }
#endif
    return MappedFile(address, size);
}

std::span<const std::byte> MappedFile::getBytes() const noexcept {
    if (m_size == 0) {
        return {};
    }
    return {static_cast<const std::byte *>(m_address), m_size};
}

void MappedFile::close() noexcept {
    if (m_size != 0) {
#if defined(_WIN32)
        UnmapViewOfFile(m_address);
#else
        munmap(m_address, m_size);
#endif
        m_address = nullptr;
        m_size = 0;
    }
}

} // namespace star
