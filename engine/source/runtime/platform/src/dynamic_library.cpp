#include "platform/dynamic_library.hpp"

#include <new>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace star {

DynamicLibrary::DynamicLibrary(void *handle) noexcept : m_handle(handle) {}

DynamicLibrary::DynamicLibrary(DynamicLibrary &&other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}

DynamicLibrary &DynamicLibrary::operator=(DynamicLibrary &&other) noexcept {
    if (this != &other) {
        close();
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() { close(); }

std::expected<DynamicLibrary, DynamicLibraryError> DynamicLibrary::open(const std::filesystem::path &path) noexcept {
    try {
        using NativePath = std::filesystem::path;
        if (path.empty() || path.native().find(NativePath::value_type{}) != NativePath::string_type::npos) {
            return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::InvalidPath, "Invalid library path"});
        }

        std::error_code path_error;
        const NativePath absolute_path = std::filesystem::absolute(path, path_error);
        if (path_error) {
            return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::InvalidPath, path_error.message()});
        }

#if defined(_WIN32)
        constexpr auto k_search_flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
        void *handle = LoadLibraryExW(absolute_path.c_str(), nullptr, k_search_flags);
        if (handle == nullptr) {
            const auto error = GetLastError();
            return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::LoadFailed,
                                                       "LoadLibraryExW failed with error " + std::to_string(error)});
        }
#else
        void *handle = dlopen(absolute_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            const char *error = dlerror();
            return std::unexpected(
                DynamicLibraryError{DynamicLibraryErrorCode::LoadFailed, error != nullptr ? error : "dlopen failed"});
        }
#endif
        return DynamicLibrary(handle);
    } catch (const std::bad_alloc &) {
        return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::OutOfMemory, {}});
    }
}

std::expected<DynamicLibrary::Function, DynamicLibraryError>
DynamicLibrary::findFunction(std::string_view name) const noexcept {
    try {
        if (!isOpen()) {
            return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::NotOpen, "Library is not open"});
        }
        if (name.empty() || name.find('\0') != std::string_view::npos) {
            return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::InvalidSymbol, "Invalid symbol name"});
        }

        const std::string symbol_name(name);
#if defined(_WIN32)
        const auto symbol = GetProcAddress(static_cast<HMODULE>(m_handle), symbol_name.c_str());
        if (symbol == nullptr) {
            const auto error = GetLastError();
            return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::SymbolNotFound,
                                                       "GetProcAddress failed with error " + std::to_string(error)});
        }
#else
        static_cast<void>(dlerror());
        void *symbol = dlsym(m_handle, symbol_name.c_str());
        const char *error = dlerror();
        if (error != nullptr || symbol == nullptr) {
            return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::SymbolNotFound,
                                                       error != nullptr ? error : "Function has a null address"});
        }
#endif
        return reinterpret_cast<Function>(symbol);
    } catch (const std::bad_alloc &) {
        return std::unexpected(DynamicLibraryError{DynamicLibraryErrorCode::OutOfMemory, {}});
    }
}

bool DynamicLibrary::isOpen() const noexcept { return m_handle != nullptr; }

void DynamicLibrary::close() noexcept {
    if (isOpen()) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(m_handle));
#else
        dlclose(m_handle);
#endif
        m_handle = nullptr;
    }
}

} // namespace star
