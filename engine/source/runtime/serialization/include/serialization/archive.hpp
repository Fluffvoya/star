#pragma once

#include "reflection/reflection.hpp"
#include "serialization/export.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace star::serialization {

struct PlainScalar {
    std::string text;
    friend bool operator==(const PlainScalar &, const PlainScalar &) = default;
};

struct ArchiveValue {
    using Sequence = std::vector<ArchiveValue>;
    using Mapping = std::map<std::string, ArchiveValue, std::less<>>;
    using Storage = std::variant<std::monostate, PlainScalar, std::string, Sequence, Mapping>;

    Storage value;
    friend bool operator==(const ArchiveValue &, const ArchiveValue &) = default;
};

struct ArchiveDocument {
    std::string type_name;
    std::uint32_t version = 1;
    ArchiveValue::Mapping fields;
    friend bool operator==(const ArchiveDocument &, const ArchiveDocument &) = default;
};

enum class ArchiveErrorCode : std::uint8_t {
    InvalidInput,
    UnsupportedVersion,
    TypeMismatch,
    UnsupportedProperty,
    UnknownField,
    MigrationFailed,
    OperationFailed,
};

struct ArchiveError {
    ArchiveErrorCode code;
    std::string path;
};

enum class LoadPolicy : std::uint8_t { Preserve, Strict };
enum class CapturePolicy : std::uint8_t { AllFields, OmitDefaults };

struct LoadResult {
    // Pass extensions back to captureObject to preserve unresolved values.
    ArchiveValue::Mapping extensions;
    std::vector<ArchiveError> diagnostics;
};

class Archive final {
  public:
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

    [[nodiscard]] bool isLoading() const noexcept { return m_loading; }

    template <typename T> bool value(std::string_view name, T &value, const reflect::TypeInfo *type = nullptr) {
        return m_transfer(m_context, name, reflect::makeValueInfo<T>(type), &value);
    }

  private:
    friend struct ArchiveAccess;
    using Transfer = bool (*)(void *context, std::string_view name, const reflect::PropertyInfo &property, void *value);

    Archive(bool loading, void *context, Transfer transfer)
        : m_loading(loading), m_context(context), m_transfer(transfer) {}

    bool m_loading;
    void *m_context;
    Transfer m_transfer;
};

// A migration can advance an older document before field matching.
using UpgradeFields = bool (*)(ArchiveValue::Mapping &fields, std::uint32_t from_version, std::uint32_t to_version);

[[nodiscard]] STAR_SERIALIZATION_API std::expected<ArchiveDocument, ArchiveError> readYaml(std::string_view source);
[[nodiscard]] STAR_SERIALIZATION_API std::expected<std::string, ArchiveError>
writeYaml(const ArchiveDocument &document);

[[nodiscard]] STAR_SERIALIZATION_API std::expected<ArchiveDocument, ArchiveError>
readBinary(std::span<const std::byte> source);
[[nodiscard]] STAR_SERIALIZATION_API std::expected<std::vector<std::byte>, ArchiveError>
writeBinary(const ArchiveDocument &document);

[[nodiscard]] STAR_SERIALIZATION_API std::expected<ArchiveDocument, ArchiveError>
captureObject(const reflect::TypeInfo &type, const void *object, const ArchiveValue::Mapping &extensions = {},
              CapturePolicy policy = CapturePolicy::AllFields);

// Loads direct and nested properties into a copy, then commits via TypeOps::swap.
[[nodiscard]] STAR_SERIALIZATION_API std::expected<LoadResult, ArchiveError>
applyObject(const ArchiveDocument &document, const reflect::TypeInfo &type, void *object,
            LoadPolicy policy = LoadPolicy::Preserve, UpgradeFields upgrade = nullptr);

} // namespace star::serialization
