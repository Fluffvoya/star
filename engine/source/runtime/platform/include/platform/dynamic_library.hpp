#pragma once

#include "platform/export.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace star {

enum class DynamicLibraryErrorCode : std::uint8_t {
    InvalidPath,
    LoadFailed,
    NotOpen,
    InvalidSymbol,
    SymbolNotFound,
    OutOfMemory,
};

struct DynamicLibraryError {
    DynamicLibraryErrorCode code;
    std::string message;
};

class STAR_PLATFORM_API DynamicLibrary final {
  public:
    using Function = void (*)();

    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;
    DynamicLibrary(DynamicLibrary &&other) noexcept;
    DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;
    ~DynamicLibrary();

    // Relative paths are resolved against the current directory. SDL initialization is unnecessary.
    [[nodiscard]] static std::expected<DynamicLibrary, DynamicLibraryError>
    open(const std::filesystem::path &path) noexcept;

    // Cast the result to the exported function's signature before calling it.
    // Stop using all borrowed symbols and calls before releasing the library.
    [[nodiscard]] std::expected<Function, DynamicLibraryError> findFunction(std::string_view name) const noexcept;
    [[nodiscard]] bool isOpen() const noexcept;

  private:
    explicit DynamicLibrary(void *handle) noexcept;
    void close() noexcept;

    void *m_handle;
};

} // namespace star
