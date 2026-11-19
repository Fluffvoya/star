#pragma once

#include "core/export.hpp"

#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace star {

enum class CVarFlags : std::uint8_t {
    None = 0,
    Scalability = 1,
};

enum class CVarError : std::uint8_t {
    InvalidName,
    DuplicateName,
    NotFound,
    InvalidValue,
    AllocationFailed,
};

using CVarValue = std::variant<bool, int, float, std::string>;

struct CVarInfo {
    std::string description;
    CVarFlags flags;
    CVarValue default_value;
    CVarValue value;
};

struct CVarAssignment {
    std::string_view name;
    std::string_view value;
};

class STAR_CORE_API CVarRegistry final {
  public:
    [[nodiscard]] static std::expected<CVarInfo, CVarError> getInfo(std::string_view name) noexcept;
    [[nodiscard]] static std::expected<void, CVarError> setFromString(std::string_view name,
                                                                      std::string_view text) noexcept;
    [[nodiscard]] static std::expected<void, CVarError>
    setFromStrings(std::span<const CVarAssignment> assignments) noexcept;
};

namespace detail {

struct CVarRecord;

[[nodiscard]] STAR_CORE_API std::expected<CVarRecord *, CVarError>
registerCVar(std::string_view name, CVarValue value, std::string_view description, CVarFlags flags) noexcept;
STAR_CORE_API void unregisterCVar(CVarRecord &record) noexcept;
[[nodiscard]] STAR_CORE_API std::expected<CVarValue, CVarError> readCVar(const CVarRecord &record) noexcept;
[[nodiscard]] STAR_CORE_API std::expected<void, CVarError> writeCVar(CVarRecord &record, CVarValue value) noexcept;

} // namespace detail

template <class T>
concept CVarType =
    std::same_as<T, bool> || std::same_as<T, int> || std::same_as<T, float> || std::same_as<T, std::string>;

// Registers a typed value for this object's lifetime. Join users of the typed handle before destroying it.
template <CVarType T> class CVar final {
  public:
    CVar(std::string_view name, T default_value, std::string_view description,
         CVarFlags flags = CVarFlags::None) noexcept
        : m_record(detail::registerCVar(name, std::move(default_value), description, flags)) {}
    CVar(const CVar &) = delete;
    CVar &operator=(const CVar &) = delete;

    ~CVar() {
        if (m_record) {
            detail::unregisterCVar(**m_record);
        }
    }

    [[nodiscard]] std::expected<T, CVarError> get() const noexcept {
        if (!m_record) {
            return std::unexpected(m_record.error());
        }
        auto value = detail::readCVar(**m_record);
        if (!value) {
            return std::unexpected(value.error());
        }
        return std::get<T>(std::move(*value));
    }

    [[nodiscard]] std::expected<void, CVarError> set(T value) noexcept {
        if (!m_record) {
            return std::unexpected(m_record.error());
        }
        return detail::writeCVar(**m_record, std::move(value));
    }

  private:
    std::expected<detail::CVarRecord *, CVarError> m_record;
};

} // namespace star
