#pragma once

#include "reflection/export.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace star::reflect {

using TypeId = std::uint64_t;
using ModuleId = std::uint64_t;

[[nodiscard]] constexpr TypeId makeTypeId(std::string_view name) {
    TypeId hash = 14695981039346656037ull;
    for (const unsigned char character : name) {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    return hash;
}

enum class TypeKind : std::uint8_t {
    Struct,
    Class,
    Enum,
};

enum class PropertyKind : std::uint8_t {
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    String,
    Reflected,
};

enum class PropertyFlags : std::uint32_t {
    None = 0,
    Transient = 1u << 0,
    ReadOnly = 1u << 1,
};

constexpr PropertyFlags operator|(PropertyFlags left, PropertyFlags right) {
    return static_cast<PropertyFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(PropertyFlags value, PropertyFlags flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct TypeInfo;

struct PropertyInfo {
    std::string_view name;
    PropertyKind kind;
    PropertyFlags flags;
    const TypeInfo *nested_type;
    void *(*get_mutable)(void *object);
    const void *(*get_const)(const void *object);
    std::string (*format_scalar)(const void *member) = nullptr;
    bool (*parse_scalar)(std::string_view text, void *member) = nullptr;
    std::string_view former_name{};
};

struct TypeOps {
    void (*construct)(void *object);
    void (*destroy)(void *object);
    void (*copy_construct)(void *destination, const void *source);
    bool (*equals)(const void *left, const void *right);
    void (*swap)(void *left, void *right) noexcept = nullptr;
};

struct TypeInfo {
    TypeId id;
    std::string_view name;
    TypeKind kind;
    std::size_t size;
    std::size_t alignment;
    std::span<const PropertyInfo> properties;
    TypeOps ops;
    ModuleId owner;
    std::uint32_t version;
    const TypeInfo *base;
    std::string_view former_name{};
};

enum class RegistryErrorCode : std::uint8_t {
    DuplicateType,
    TypeOwnerMismatch,
    InvalidType,
};

struct RegistryError {
    RegistryErrorCode code;
    TypeId type_id;
    std::string message;
};

class STAR_REFLECTION_API TypeRegistry final {
  public:
    [[nodiscard]] const TypeInfo *find(TypeId id) const noexcept;
    [[nodiscard]] const TypeInfo *find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const TypeInfo *> derivedFrom(const TypeInfo &base) const;

    [[nodiscard]] std::expected<void, RegistryError> registerModule(ModuleId owner,
                                                                    std::span<const TypeInfo *const> types);
    void unregisterModule(ModuleId owner) noexcept;
    void clear() noexcept;

  private:
    std::unordered_map<TypeId, const TypeInfo *> m_types;
};

namespace detail {

template <typename T> consteval PropertyKind propertyKind() {
    if constexpr (std::is_same_v<T, bool>) {
        return PropertyKind::Boolean;
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T> &&
                         requires(const char *p, T &value) { std::from_chars(p, p, value); }) {
        return PropertyKind::SignedInteger;
    } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> &&
                         requires(const char *p, T &value) { std::from_chars(p, p, value); }) {
        return PropertyKind::UnsignedInteger;
    } else if constexpr (std::is_floating_point_v<T>) {
        return PropertyKind::FloatingPoint;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return PropertyKind::String;
    } else {
        return PropertyKind::Reflected;
    }
}

template <typename Owner, typename Member, Member Owner::*MemberPointer> void *getMutable(void *object) {
    return &(static_cast<Owner *>(object)->*MemberPointer);
}

template <typename Owner, typename Member, Member Owner::*MemberPointer> const void *getConst(const void *object) {
    return &(static_cast<const Owner *>(object)->*MemberPointer);
}

template <typename T> void construct(void *object) { std::construct_at(static_cast<T *>(object)); }
template <typename T> void destroy(void *object) { std::destroy_at(static_cast<T *>(object)); }
template <typename T> void copyConstruct(void *destination, const void *source) {
    std::construct_at(static_cast<T *>(destination), *static_cast<const T *>(source));
}
template <typename T> bool equals(const void *left, const void *right) {
    if constexpr (requires(const T &a, const T &b) { a == b; }) {
        return *static_cast<const T *>(left) == *static_cast<const T *>(right);
    } else {
        return false;
    }
}

template <typename T> std::string formatScalar(const void *member) {
    const auto &value = *static_cast<const T *>(member);
    if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else {
        if constexpr (std::is_floating_point_v<T>) {
            if (std::isnan(value)) {
                return ".nan";
            }
            if (std::isinf(value)) {
                return std::signbit(value) ? "-.inf" : ".inf";
            }
        }
        char buffer[128];
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, converted.ptr);
    }
}

template <typename T> bool parseScalar(std::string_view text, void *member) {
    auto &destination = *static_cast<T *>(member);
    if constexpr (std::is_same_v<T, std::string>) {
        destination = text;
    } else if constexpr (std::is_same_v<T, bool>) {
        if (text == "true" || text == "True" || text == "TRUE") {
            destination = true;
        } else if (text == "false" || text == "False" || text == "FALSE") {
            destination = false;
        } else {
            return false;
        }
    } else {
        if constexpr (std::is_floating_point_v<T>) {
            if (text == ".nan" || text == ".NaN" || text == ".NAN") {
                destination = std::numeric_limits<T>::quiet_NaN();
                return true;
            }
            if (text == ".inf" || text == ".Inf" || text == ".INF") {
                destination = std::numeric_limits<T>::infinity();
                return true;
            }
            if (text == "-.inf" || text == "-.Inf" || text == "-.INF") {
                destination = -std::numeric_limits<T>::infinity();
                return true;
            }
        }
        if (text.starts_with('+')) {
            text.remove_prefix(1);
            if (text.starts_with('-')) {
                return false;
            }
        }
        if (text.empty()) {
            return false;
        }
        T value{};
        const auto converted = [&] {
            if constexpr (std::is_integral_v<T>) {
                int base = 10;
                if (text.starts_with("0x") || text.starts_with("0o")) {
                    base = text[1] == 'x' ? 16 : 8;
                    text.remove_prefix(2);
                }
                return std::from_chars(text.data(), text.data() + text.size(), value, base);
            } else {
                return std::from_chars(text.data(), text.data() + text.size(), value);
            }
        }();
        if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size()) {
            return false;
        }
        destination = value;
    }
    return true;
}

template <typename T> void swapObjects(void *left, void *right) noexcept {
    using std::swap;
    swap(*static_cast<T *>(left), *static_cast<T *>(right));
}

} // namespace detail

template <typename Owner, typename Member, Member Owner::*MemberPointer>
constexpr PropertyInfo makeProperty(std::string_view name, PropertyFlags flags = PropertyFlags::None,
                                    const TypeInfo *nested_type = nullptr, std::string_view former_name = {}) {
    constexpr auto kind = detail::propertyKind<Member>();
    PropertyInfo property{name,
                          kind,
                          flags,
                          nested_type,
                          &detail::getMutable<Owner, Member, MemberPointer>,
                          &detail::getConst<Owner, Member, MemberPointer>};
    if constexpr (kind != PropertyKind::Reflected) {
        property.format_scalar = &detail::formatScalar<Member>;
        property.parse_scalar = &detail::parseScalar<Member>;
    }
    property.former_name = former_name;
    return property;
}

template <typename T> constexpr TypeOps makeTypeOps() {
    static_assert(std::is_default_constructible_v<T> && std::is_destructible_v<T> && std::is_copy_constructible_v<T>);
    TypeOps ops{&detail::construct<T>, &detail::destroy<T>, &detail::copyConstruct<T>, &detail::equals<T>};
    if constexpr (std::is_nothrow_swappable_v<T>) {
        ops.swap = &detail::swapObjects<T>;
    }
    return ops;
}

} // namespace star::reflect
