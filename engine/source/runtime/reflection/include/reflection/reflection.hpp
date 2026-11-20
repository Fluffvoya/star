#pragma once

#include "reflection/export.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
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
};

struct TypeOps {
    void (*construct)(void *object);
    void (*destroy)(void *object);
    void (*copy_construct)(void *destination, const void *source);
    bool (*equals)(const void *left, const void *right);
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
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        return PropertyKind::SignedInteger;
    } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
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

} // namespace detail

template <typename Owner, typename Member, Member Owner::*MemberPointer>
constexpr PropertyInfo makeProperty(std::string_view name, PropertyFlags flags = PropertyFlags::None,
                                    const TypeInfo *nested_type = nullptr) {
    return PropertyInfo{name,
                        detail::propertyKind<Member>(),
                        flags,
                        nested_type,
                        &detail::getMutable<Owner, Member, MemberPointer>,
                        &detail::getConst<Owner, Member, MemberPointer>};
}

template <typename T> constexpr TypeOps makeTypeOps() {
    static_assert(std::is_default_constructible_v<T> && std::is_destructible_v<T> && std::is_copy_constructible_v<T>);
    return TypeOps{&detail::construct<T>, &detail::destroy<T>, &detail::copyConstruct<T>, &detail::equals<T>};
}

} // namespace star::reflect
